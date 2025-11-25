// Solis Programming Language - Language Server Protocol
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/lsp/lsp.hpp"

#include "cli/lsp/symbol_index.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace solis {
namespace lsp {

// Convert byte offset to line/character position
Position Document::offsetToPosition(size_t offset) const {
  int line = 0;
  int character = 0;

  for (size_t i = 0; i < offset && i < text.length(); ++i) {
    if (text[i] == '\n') {
      line++;
      character = 0;
    } else {
      character++;
    }
  }

  return {line, character};
}

// Convert line/character position to byte offset
size_t Document::positionToOffset(const Position& pos) const {
  int currentLine = 0;
  size_t offset = 0;

  for (size_t i = 0; i < text.length(); ++i) {
    if (currentLine == pos.line) {
      return offset + pos.character;
    }

    if (text[i] == '\n') {
      currentLine++;
      offset = i + 1;
    }
  }

  return offset;
}

// Get line text at specified line number (0-indexed)
std::string Document::getLine(int line) const {
  std::istringstream stream(text);
  std::string result;
  int currentLine = 0;

  while (std::getline(stream, result)) {
    if (currentLine == line) {
      return result;
    }
    currentLine++;
  }

  return "";
}

// LanguageServer implementation

LanguageServer::LanguageServer() {}

// Handle document open notification
// Registers document and triggers analysis
void LanguageServer::didOpen(const std::string& uri, const std::string& text, int version) {
  auto doc = std::make_shared<Document>(uri, text, version);
  documents_[uri] = doc;
  analyzeDocument(uri);
}

// Handle document change notification
// Updates document content and re-analyzes with incremental compilation
void LanguageServer::didChange(const std::string& uri, const std::string& text, int version) {
  auto doc = getDocument(uri);
  if (doc) {
    doc->update(text, version);

    // Invalidate file in symbol index for incremental updates
    symbolIndex_.invalidateFile(uri);

    // Re-analyze document
    analyzeDocument(uri);

    // Re-analyze dependent files if needed
    auto dependents = symbolIndex_.getDependentFiles(uri);
    for (const auto& dep : dependents) {
      auto depDoc = getDocument(dep);
      if (depDoc) {
        analyzeDocument(dep);
      }
    }
  }
}

// Handle document close notification
// Removes document and associated analysis data
void LanguageServer::didClose(const std::string& uri) {
  documents_.erase(uri);
  interpreters_.erase(uri);
  typers_.erase(uri);
  asts_.erase(uri);
  definitions_.erase(uri);
  symbolIndex_.clearFile(uri);
}

// Get document by URI
std::shared_ptr<Document> LanguageServer::getDocument(const std::string& uri) {
  auto it = documents_.find(uri);
  if (it != documents_.end()) {
    return it->second;
  }
  return nullptr;
}

// Analyze document: parse, type-check, and generate diagnostics
void LanguageServer::analyzeDocument(const std::string& uri) {
  auto doc = getDocument(uri);
  if (!doc) {
    return;
  }

  try {
    // Parse document to AST
    {
      Lexer l(doc->text);
      auto t = l.tokenize();
      Parser p(std::move(t));
      auto m = p.parseModule();

      asts_[uri] = std::move(m.declarations);
    }

    // Type check all declarations
    TypeInference typer(TypeEnv::builtins());

    // Infer types for each declaration
    for (const auto& decl : asts_[uri]) {
      try {
        typer.inferDecl(*decl);
      } catch (const SolisError& e) {
        Diagnostic diag;
        // Extract location from error if available
        if (e.location().line > 0) {
          diag.range.start.line = e.location().line - 1;
          diag.range.start.character = e.location().column - 1;
          diag.range.end.line = e.location().endLine - 1;
          diag.range.end.character = e.location().endColumn - 1;
        } else {
          diag.range = {{0, 0}, {0, 10}};
        }

        diag.message = e.title() + ": " + e.explanation();
        diag.severity = 1;

        std::vector<Diagnostic> diagnostics = getDiagnostics(uri);
        diagnostics.push_back(diag);
        publishDiagnostics(uri, diagnostics);
        // Continue analyzing remaining declarations
      } catch (const std::exception& e) {
      }
    }
    typers_[uri] = typer;

    // Index symbols for cross-file navigation
    try {
      symbolIndex_.indexFile(uri, asts_[uri], typer);
    } catch (const std::exception& e) {
      // Symbol indexing failed, continue without cross-file support
    }

    // Build definitions map for go-to-definition support (backward compatibility)
    DefinitionMap defs;
    for (const auto& decl : asts_[uri]) {
      if (auto* func = std::get_if<FunctionDecl>(&decl->node)) {
        if (func->location) {
          std::string name = func->name;
          if (typer.env().contains(name)) {
            try {
              auto scheme = typer.env().lookup(name);
              DefinitionLocation loc;
              loc.uri = uri;
              loc.range.start.line = func->location->line - 1;
              loc.range.start.character = func->location->column - 1;
              loc.range.end.line = func->location->endLine - 1;
              loc.range.end.character = func->location->endColumn - 1;
              loc.name = name;
              loc.type = scheme.instantiate();
              defs[name] = loc;
            } catch (const std::exception&) {
              // Type lookup failed, skip this definition
            }
          }
        }
      }
    }
    definitions_[uri] = defs;

    // Publish diagnostics including parser errors
    auto diagnostics = getDiagnostics(uri);

    publishDiagnostics(uri, diagnostics);

  } catch (const std::exception& e) {
    // Create diagnostic for analysis error
    Diagnostic diag;
    diag.range = {{0, 0}, {0, 10}};
    diag.message = e.what();
    diag.severity = 1;

    std::vector<Diagnostic> diagnostics = getDiagnostics(uri);
    diagnostics.push_back(diag);
    publishDiagnostics(uri, diagnostics);
  } catch (...) {
  }
}

void LanguageServer::publishDiagnostics(const std::string& uri,
                                        const std::vector<Diagnostic>& diagnostics) {
  std::string diagJson = "[";
  for (size_t i = 0; i < diagnostics.size(); i++) {
    if (i > 0)
      diagJson += ",";
    const auto& d = diagnostics[i];

    // Escape diagnostic message for JSON
    std::string escapedMessage;
    for (char c : d.message) {
      if (c == '"')
        escapedMessage += "\\\"";
      else if (c == '\\')
        escapedMessage += "\\\\";
      else if (c == '\b')
        escapedMessage += "\\b";
      else if (c == '\f')
        escapedMessage += "\\f";
      else if (c == '\n')
        escapedMessage += "\\n";
      else if (c == '\r')
        escapedMessage += "\\r";
      else if (c == '\t')
        escapedMessage += "\\t";
      else if (static_cast<unsigned char>(c) < 0x20) {
        // Skip other control characters
      } else
        escapedMessage += c;
    }

    diagJson += "{\"range\":{\"start\":{\"line\":" + std::to_string(d.range.start.line) +
                ",\"character\":" + std::to_string(d.range.start.character) + "}," +
                "\"end\":{\"line\":" + std::to_string(d.range.end.line) +
                ",\"character\":" + std::to_string(d.range.end.character) + "}}," +
                "\"severity\":" + std::to_string(d.severity) + "," + "\"message\":\"" +
                escapedMessage + "\"}";
  }
  diagJson += "]";

  std::string response = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                         "publishDiagnostics\",\"params\":{\"uri\":\"" +
                         uri + "\",\"diagnostics\":" + diagJson + "}}";

  std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
}

// Get diagnostics for document: lexer errors, parser errors, type errors
std::vector<Diagnostic> LanguageServer::getDiagnostics(const std::string& uri) {
  std::vector<Diagnostic> diagnostics;
  auto doc = getDocument(uri);
  if (!doc) {
    return diagnostics;
  }

  try {
    Lexer lexer(doc->text);
    auto tokens = lexer.tokenize();

    // Collect lexer errors
    for (const auto& token : tokens) {
      if (token.type == TokenType::Error) {
        Diagnostic diag;
        diag.range = {{static_cast<int>(token.line - 1), static_cast<int>(token.column - 1)},
                      {static_cast<int>(token.line - 1),
                       static_cast<int>(token.column + token.lexeme.length() - 1)}};
        diag.message = "Lexer error: " + token.lexeme;
        diag.severity = 1;
        diagnostics.push_back(diag);
      }
    }

    // Collect parser errors
    Parser parser(std::move(tokens));
    while (!parser.isAtEnd()) {
      try {
        parser.parseDeclaration();
      } catch (const std::exception& e) {
        Diagnostic diag;
        diag.range = {{0, 0}, {0, 10}};
        diag.message = std::string("Parse error: ") + e.what();
        diag.severity = 1;
        diagnostics.push_back(diag);
        break;
      }
    }

  } catch (const std::exception& e) {
    Diagnostic diag;
    diag.range = {{0, 0}, {0, 10}};
    diag.message = std::string("Error: ") + e.what();
    diag.severity = 1;
    diagnostics.push_back(diag);
  }
  return diagnostics;
}

// Get completion items at position
// Handles import statements, qualified names, and default completions
std::vector<CompletionItem> LanguageServer::getCompletions(const std::string& uri,
                                                           const Position& pos) {
  std::vector<CompletionItem> items;

  auto doc = getDocument(uri);
  if (!doc)
    return items;

  // Get text before cursor to detect context
  std::string line = doc->getLine(pos.line);
  if (line.empty() && pos.line >= 0) {
    // Line number might be out of bounds, use empty prefix
    line = "";
  }
  std::string prefix = line.substr(0, std::min(pos.character, (int)line.length()));

  // Handle import statement completions
  if (prefix.find("import ") != std::string::npos) {
    // Check for selective import context: "import Foo ("
    size_t parenPos = prefix.find('(');
    if (parenPos != std::string::npos) {
      // Suggest module exports for selective import
      // Extract module name
      std::string importPart = prefix.substr(prefix.find("import ") + 7,
                                             parenPos - (prefix.find("import ") + 7));
      // Trim whitespace
      importPart.erase(0, importPart.find_first_not_of(" \t"));
      importPart.erase(importPart.find_last_not_of(" \t(") + 1);

      // Parse module to extract exports
      std::string moduleName = importPart;
      std::vector<std::string> exports;

      // Try to find and parse the module file
      std::vector<std::string> searchPaths = {"std", "prelude", "."};
      std::string moduleFile;
      for (const auto& searchPath : searchPaths) {
        std::string candidate = searchPath + "/" + moduleName + ".solis";
        std::replace(candidate.begin() + searchPath.length() + 1, candidate.end(), '.', '/');
        if (std::filesystem::exists(candidate)) {
          moduleFile = candidate;
          break;
        }
      }

      if (!moduleFile.empty()) {
        // Extract exported symbols from module file
        try {
          std::ifstream file(moduleFile);
          std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

          // Extract export list from module declaration
          size_t modulePos = content.find("module ");
          if (modulePos != std::string::npos) {
            size_t parenStart = content.find('(', modulePos);
            size_t wherePos = content.find(" where", modulePos);

            if (parenStart != std::string::npos && parenStart < wherePos) {
              // Module has explicit export list
              size_t parenEnd = content.find(')', parenStart);
              std::string exportList = content.substr(parenStart + 1, parenEnd - parenStart - 1);

              // Parse comma-separated exports
              std::stringstream ss(exportList);
              std::string token;
              while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(" \t\n"));
                token.erase(token.find_last_not_of(" \t\n") + 1);
                if (!token.empty()) {
                  exports.push_back(token);
                }
              }
            } else {
              // No explicit export list: extract all top-level function definitions
              size_t pos = content.find("let ", wherePos);
              while (pos != std::string::npos) {
                size_t nameStart = pos + 4;
                size_t nameEnd = content.find_first_of(" \t=:", nameStart);
                std::string name = content.substr(nameStart, nameEnd - nameStart);
                exports.push_back(name);
                pos = content.find("\nlet ", pos + 1);
              }
            }
          }
        } catch (...) {
          // If parsing fails, no completions
        }
      }

      // Add exports as completion items
      for (const auto& name : exports) {
        CompletionItem item;
        item.label = name;
        item.kind = 3;  // Function
        item.detail = "Export from " + importPart;
        items.push_back(item);
      }
      return items;
    }

    // Suggest module names from filesystem
    std::vector<std::string> modules;

    // Discover modules from search paths
    std::vector<std::string> searchPaths = {"std", "prelude", "."};
    for (const auto& searchPath : searchPaths) {
      try {
        if (std::filesystem::exists(searchPath) && std::filesystem::is_directory(searchPath)) {
          for (const auto& entry : std::filesystem::recursive_directory_iterator(searchPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".solis") {
              // Convert file path to module name: std/Data/List.solis -> Data.List
              std::string modPath = entry.path().string();
              // Strip search path prefix
              if (modPath.find(searchPath + "/") == 0) {
                modPath = modPath.substr(searchPath.length() + 1);
              }
              // Strip .solis extension
              if (modPath.length() > 6 && modPath.substr(modPath.length() - 6) == ".solis") {
                modPath = modPath.substr(0, modPath.length() - 6);
              }
              // Convert path separators to module name dots
              std::replace(modPath.begin(), modPath.end(), '/', '.');
              modules.push_back(modPath);
            }
          }
        }
      } catch (...) {
        // Skip unreadable directories
      }
    }

    // Add discovered modules as completion items
    for (const auto& mod : modules) {
      CompletionItem item;
      item.label = mod;
      item.kind = 9;  // Module
      item.detail = "Module";
      items.push_back(item);
    }
    return items;
  }

  // Handle qualified name completions (Module.Symbol)
  size_t dotPos = prefix.find_last_of('.');
  if (dotPos != std::string::npos && dotPos > 0) {
    // Extract module qualifier
    size_t startPos = prefix.find_last_of(" \t\n(", dotPos);
    startPos = (startPos == std::string::npos) ? 0 : startPos + 1;
    std::string qualifier = prefix.substr(startPos, dotPos - startPos);

    // Find and parse the qualified module
    std::vector<std::string> searchPaths = {"std", "prelude", "."};
    std::string moduleFile;
    for (const auto& searchPath : searchPaths) {
      std::string candidate = searchPath + "/" + qualifier + ".solis";
      std::replace(candidate.begin() + searchPath.length() + 1, candidate.end(), '.', '/');
      if (std::filesystem::exists(candidate)) {
        moduleFile = candidate;
        break;
      }
    }

    if (!moduleFile.empty()) {
      try {
        std::ifstream file(moduleFile);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Extract exported function names from module
        size_t modulePos = content.find("module ");
        if (modulePos != std::string::npos) {
          size_t wherePos = content.find(" where", modulePos);
          size_t pos = content.find("let ", wherePos);

          while (pos != std::string::npos) {
            size_t nameStart = pos + 4;
            size_t nameEnd = content.find_first_of(" \t=:", nameStart);
            if (nameEnd != std::string::npos) {
              std::string name = content.substr(nameStart, nameEnd - nameStart);

              CompletionItem item;
              item.label = name;
              item.kind = 3;  // Function
              item.detail = "From " + qualifier;
              items.push_back(item);
            }
            pos = content.find("\nlet ", pos + 1);
          }
        }
      } catch (...) {
        // Module parsing failed
      }
      return items;
    }
  }

  // Default completions: keywords and available symbols
  std::vector<std::string> keywords = {"let",
                                       "match",
                                       "if",
                                       "else",
                                       "then",
                                       "type",
                                       "data",
                                       "module",
                                       "import",
                                       "infix",
                                       "infixl",
                                       "infixr",
                                       "do",
                                       "in",
                                       "where",
                                       "case",
                                       "of"};

  for (const auto& kw : keywords) {
    CompletionItem item;
    item.label = kw;
    item.kind = 14;  // Keyword
    item.detail = "Keyword";
    items.push_back(item);
  }

  // Add user-defined functions and builtins
  // Use symbol index for better completion coverage
  std::string completionPrefix = "";
  size_t lastSpace = prefix.find_last_of(" \t\n");
  if (lastSpace != std::string::npos && lastSpace + 1 < prefix.length()) {
    completionPrefix = prefix.substr(lastSpace + 1);
  }

  auto symbolCompletions = symbolIndex_.getSymbolsForCompletion(uri, completionPrefix);
  for (const auto& symbol : symbolCompletions) {
    CompletionItem item;
    item.label = symbol.name;
    item.kind = static_cast<int>(symbol.kind);
    if (symbol.type) {
      item.detail = typeToString(symbol.type);
    } else {
      item.detail = "";
    }
    items.push_back(item);
  }

  auto typerIt = typers_.find(uri);
  if (typerIt != typers_.end()) {
    // Add user-defined functions from definitions map (backward compatibility)
    auto defIt = definitions_.find(uri);
    if (defIt != definitions_.end()) {
      for (const auto& [name, def] : defIt->second) {
        // Check if already added from symbol index
        bool alreadyAdded = false;
        for (const auto& item : items) {
          if (item.label == name) {
            alreadyAdded = true;
            break;
          }
        }
        if (!alreadyAdded) {
          CompletionItem item;
          item.label = name;
          item.kind = 3;  // Function
          item.detail = typeToString(def.type);
          items.push_back(item);
        }
      }
    }

    // Add builtin functions not in definitions
    std::vector<std::string> builtins = {"map",
                                         "filter",
                                         "foldl",
                                         "foldr",
                                         "head",
                                         "tail",
                                         "reverse",
                                         "length",
                                         "sum",
                                         "product",
                                         "max",
                                         "min",
                                         "abs",
                                         "show",
                                         "print"};

    for (const auto& name : builtins) {
      bool alreadyAdded = false;
      for (const auto& item : items) {
        if (item.label == name) {
          alreadyAdded = true;
          break;
        }
      }

      if (!alreadyAdded) {
        CompletionItem item;
        item.label = name;
        item.kind = 3;  // Function
        item.detail = "<builtin>";
        items.push_back(item);
      }
    }
  }

  return items;
}

// Get hover information at position
// Returns type information and definition location
Hover LanguageServer::getHover(const std::string& uri, const Position& pos) {
  Hover hover;
  hover.range = {pos, pos};
  hover.contents = "";

  auto doc = getDocument(uri);
  if (!doc)
    return hover;

  std::string word = getIdentifierAtPosition(doc->text, pos);
  if (word.empty())
    return hover;

  // Get type from typer environment
  auto typerIt = typers_.find(uri);
  if (typerIt != typers_.end() && typerIt->second.env().contains(word)) {
    try {
      auto scheme = typerIt->second.env().lookup(word);
      auto type = scheme.instantiate();

      std::string hoverText = "```solis\n" + word + " :: " + typeToString(type) + "\n```\n";

      // Append definition location if available
      auto defsIt = definitions_.find(uri);
      if (defsIt != definitions_.end()) {
        auto defIt = defsIt->second.find(word);
        if (defIt != defsIt->second.end()) {
          hoverText += "\n---\n**Defined in:** ";
          hoverText += defIt->second.uri + ":" + std::to_string(defIt->second.range.start.line + 1);
        }
      }

      hover.contents = hoverText;
    } catch (const std::exception&) {
      // Type lookup failed, return empty hover
    }
  }

  return hover;
}

// Get definition location for symbol at position
// Uses symbol index for cross-file navigation
Location LanguageServer::getDefinition(const std::string& uri, const Position& pos) {
  Location loc;
  loc.uri = uri;
  loc.range = {{0, 0}, {0, 0}};

  auto doc = getDocument(uri);
  if (!doc)
    return loc;

  std::string ident = getIdentifierAtPosition(doc->text, pos);
  if (ident.empty())
    return loc;

  // Use symbol index for cross-file definition lookup
  auto* symbolLoc = symbolIndex_.findDefinition(ident, uri);
  if (symbolLoc) {
    loc.uri = symbolLoc->uri;
    loc.range = symbolLoc->range;
    return loc;
  }

  // Fallback to local definitions map
  auto it = definitions_.find(uri);
  if (it != definitions_.end()) {
    auto defIt = it->second.find(ident);
    if (defIt != it->second.end()) {
      loc.uri = defIt->second.uri;
      loc.range = defIt->second.range;
      return loc;
    }
  }

  return loc;
}

// Get all references to symbol at position
// Uses symbol index for cross-file reference finding
std::vector<Location> LanguageServer::getReferences(const std::string& uri, const Position& pos) {
  std::vector<Location> locations;

  auto doc = getDocument(uri);
  if (!doc)
    return locations;

  std::string ident = getIdentifierAtPosition(doc->text, pos);
  if (ident.empty())
    return locations;

  // Use symbol index to find all references
  auto refs = symbolIndex_.findReferences(ident, uri);
  for (const auto& ref : refs) {
    Location loc;
    loc.uri = ref.uri;
    loc.range = ref.range;
    locations.push_back(loc);
  }

  return locations;
}

// Extract identifier at position from source text
// Handles whitespace and identifier boundaries
std::string LanguageServer::getIdentifierAtPosition(const std::string& text, const Position& pos) {
  size_t offset = 0;
  int line = 0;
  int col = 0;

  // Convert position to byte offset
  for (size_t i = 0; i < text.length(); i++) {
    if (line == pos.line && col == pos.character) {
      offset = i;
      break;
    }
    if (text[i] == '\n') {
      line++;
      col = 0;
    } else {
      col++;
    }
  }

  // Extract identifier at offset
  if (offset >= text.length())
    return "";

  // If position is on whitespace, check preceding character
  if (!isalnum(text[offset]) && text[offset] != '_') {
    if (offset > 0 && (isalnum(text[offset - 1]) || text[offset - 1] == '_')) {
      offset--;
    } else {
      return "";
    }
  }

  size_t start = offset;
  while (start > 0 && (isalnum(text[start - 1]) || text[start - 1] == '_')) {
    start--;
  }

  size_t end = offset;
  while (end < text.length() && (isalnum(text[end]) || text[end] == '_')) {
    end++;
  }

  return text.substr(start, end - start);
}

// Get inlay hints (type annotations) for range
std::vector<LanguageServer::InlayHint> LanguageServer::getInlayHints(const std::string& uri,
                                                                     const Range& range) {
  std::vector<InlayHint> hints;

  auto it = definitions_.find(uri);
  if (it == definitions_.end())
    return hints;

  // Generate type hints for functions in range
  for (const auto& [name, def] : it->second) {
    if (def.range.start.line >= range.start.line && def.range.end.line <= range.end.line) {
      InlayHint hint;
      hint.position = def.range.end;
      hint.label = ": " + typeToString(def.type);
      hint.kind = 1;
      hint.paddingLeft = true;
      hint.paddingRight = false;
      hints.push_back(hint);
    }
  }

  return hints;
}

// Get signature help for function call at position
LanguageServer::SignatureHelp LanguageServer::getSignatureHelp(const std::string& uri,
                                                               const Position& pos) {
  SignatureHelp help;
  help.activeSignature = 0;
  help.activeParameter = 0;

  auto doc = getDocument(uri);
  if (!doc)
    return help;

  auto [funcName, paramIndex] = getFunctionCallContext(doc->text, pos);
  if (funcName.empty())
    return help;

  // Look up function type
  auto it = typers_.find(uri);
  if (it != typers_.end()) {
    if (it->second.env().contains(funcName)) {
      try {
        auto scheme = it->second.env().lookup(funcName);
        auto type = scheme.instantiate();

        SignatureInformation sig;
        sig.label = funcName + " :: " + typeToString(type);

        help.signatures.push_back(sig);
        help.activeParameter = paramIndex;
      } catch (const std::exception&) {
        // Type lookup failed
      }
    }
  }

  return help;
}

// Get code actions for range
// Provides quick fixes: add type annotations, auto-import suggestions
std::vector<LanguageServer::CodeAction> LanguageServer::getCodeActions(const std::string& uri,
                                                                       const Range& range) {
  std::vector<CodeAction> actions;

  auto doc = getDocument(uri);
  if (!doc) {
    return actions;
  }

  auto it = asts_.find(uri);
  if (it == asts_.end()) {
    return actions;
  }

  // Action: Add type annotations for unannotated functions
  for (const auto& decl : it->second) {
    if (auto* func = std::get_if<FunctionDecl>(&decl->node)) {
      if (!func->location) {
        continue;
      }

      int startLine = func->location->line - 1;
      int endLine = func->location->endLine - 1;

      if (startLine <= range.end.line && endLine >= range.start.line) {
        if (!func->typeAnnotation) {
          auto typerIt = typers_.find(uri);
          if (typerIt != typers_.end()) {
            if (typerIt->second.env().contains(func->name)) {
              try {
                auto scheme = typerIt->second.env().lookup(func->name);
                std::string typeStr = scheme.toString();

                std::string lineText = doc->getLine(startLine);
                size_t equalsPos = lineText.find('=');

                if (equalsPos != std::string::npos) {
                  CodeAction action;
                  action.title = "Add type annotation for '" + func->name + "'";
                  action.kind = "quickfix";

                  TextEdit edit;
                  edit.range.start.line = startLine;
                  edit.range.start.character = static_cast<int>(equalsPos);
                  edit.range.end.line = startLine;
                  edit.range.end.character = static_cast<int>(equalsPos);

                  if (equalsPos > 0 && lineText[equalsPos - 1] == ' ') {
                    edit.newText = ": " + typeStr + " ";
                  } else {
                    edit.newText = " : " + typeStr + " ";
                  }

                  action.edits.push_back(edit);
                  actions.push_back(action);
                }
              } catch (const std::exception&) {
                // Type lookup failed, skip this action
              }
            }
          }
        }
      }
    }
  }

  // Action: Auto-import suggestions for undefined symbols
  auto diagnostics = getDiagnostics(uri);
  for (const auto& diag : diagnostics) {
    // Check if diagnostic overlaps with requested range
    if (diag.range.start.line <= range.end.line && diag.range.end.line >= range.start.line) {
      // Detect undefined variable errors
      if (diag.message.find("Undefined") != std::string::npos ||
          diag.message.find("undefined") != std::string::npos ||
          diag.message.find("not found") != std::string::npos) {
        // Extract symbol name from diagnostic range
        std::string line = doc->getLine(diag.range.start.line);
        if (static_cast<size_t>(diag.range.start.character) < line.length() &&
            static_cast<size_t>(diag.range.end.character) <= line.length()) {
          std::string symbol = line.substr(diag.range.start.character,
                                           diag.range.end.character - diag.range.start.character);

          // Suggest imports for common undefined symbols
          // Common symbol to module mappings
          std::map<std::string, std::string> commonImports = {{"map", "Data.List"},
                                                              {"filter", "Data.List"},
                                                              {"foldl", "Data.List"},
                                                              {"lookup", "Data.Map"},
                                                              {"insert", "Data.Map"}};

          if (commonImports.count(symbol)) {
            CodeAction action;
            action.title = "Import '" + symbol + "' from " + commonImports[symbol];
            action.kind = "quickfix";

            TextEdit edit;
            edit.range.start.line = 0;
            edit.range.start.character = 0;
            edit.range.end.line = 0;
            edit.range.end.character = 0;
            edit.newText = "import " + commonImports[symbol] + " (" + symbol + ")\n";

            action.edits.push_back(edit);
            actions.push_back(action);
          }
        }
      }
    }
  }

  return actions;
}

// Extract function name and parameter index from call context at position
std::pair<std::string, int> LanguageServer::getFunctionCallContext(const std::string& text,
                                                                   const Position& pos) {
  size_t offset = 0;
  int line = 0;
  int col = 0;

  // Convert position to byte offset
  for (size_t i = 0; i < text.length(); i++) {
    if (line == pos.line && col == pos.character) {
      offset = i;
      break;
    }
    if (text[i] == '\n') {
      line++;
      col = 0;
    } else {
      col++;
    }
  }

  if (offset >= text.length())
    return {"", 0};

  // Backtrack to find function call opening parenthesis
  int balance = 0;
  int paramIndex = 0;
  size_t i = offset;

  while (i > 0) {
    i--;
    char c = text[i];

    if (c == ')') {
      balance++;
    } else if (c == '(') {
      if (balance > 0) {
        balance--;
      } else {
        // Found function call opening parenthesis
        // Extract function name preceding it
        size_t nameEnd = i;
        while (nameEnd > 0 && isspace(text[nameEnd - 1])) {
          nameEnd--;
        }

        size_t nameStart = nameEnd;
        while (nameStart > 0 && (isalnum(text[nameStart - 1]) || text[nameStart - 1] == '_')) {
          nameStart--;
        }

        if (nameStart < nameEnd) {
          return {text.substr(nameStart, nameEnd - nameStart), paramIndex};
        }
        return {"", 0};
      }
    } else if (c == ',' && balance == 0) {
      paramIndex++;
    }
  }

  return {"", 0};
}

// Main LSP message loop
// Reads JSON-RPC messages from stdin and dispatches to handlers
void LanguageServer::run() {
  while (true) {
    // Read LSP message headers
    std::string line;
    int contentLength = 0;

    while (std::getline(std::cin, line)) {
      // Handle CRLF or LF
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      if (line.empty()) {
        break;  // End of headers
      }

      if (line.rfind("Content-Length: ", 0) == 0) {
        try {
          contentLength = std::stoi(line.substr(16));
        } catch (const std::exception& e) {
        }
      }
    }

    if (std::cin.eof()) {
      break;
    }

    if (contentLength == 0)
      continue;

    // Read message body
    std::vector<char> buffer(contentLength);
    std::cin.read(buffer.data(), contentLength);
    std::string content(buffer.begin(), buffer.end());

    // Extract request ID from JSON
    int id = 0;
    size_t idPos = content.find("\"id\":");
    if (idPos != std::string::npos) {
      size_t endPos = content.find_first_of(",}", idPos);
      try {
        id = std::stoi(content.substr(idPos + 5, endPos - (idPos + 5)));
      } catch (...) {
      }
    }

    // Helper to extract string value with whitespace tolerance
    auto getString = [&](const std::string& key) -> std::string {
      // Find key
      size_t keyPos = content.find("\"" + key + "\"");
      if (keyPos == std::string::npos)
        return "";

      // Find colon after key
      size_t colonPos = content.find(':', keyPos + key.length() + 2);
      if (colonPos == std::string::npos)
        return "";

      // Find opening quote after colon
      size_t startQuote = content.find('"', colonPos + 1);
      if (startQuote == std::string::npos)
        return "";

      size_t endQuote = startQuote + 1;
      while (endQuote < content.length()) {
        if (content[endQuote] == '"' &&
            (endQuote == startQuote + 1 || content[endQuote - 1] != '\\'))
          break;
        endQuote++;
      }

      if (endQuote >= content.length())
        return "";
      return content.substr(startQuote + 1, endQuote - (startQuote + 1));
    };

    // Helper to extract int value (nested in position)
    auto getInt = [&](const std::string& key) -> int {
      size_t pos = content.find("\"" + key + "\"");
      if (pos == std::string::npos)
        return 0;

      size_t colonPos = content.find(':', pos + key.length() + 2);
      if (colonPos == std::string::npos)
        return 0;

      size_t startVal = content.find_first_not_of(" \t\r\n", colonPos + 1);
      if (startVal == std::string::npos)
        return 0;

      size_t endVal = content.find_first_of(",}", startVal);
      try {
        return std::stoi(content.substr(startVal, endVal - startVal));
      } catch (...) {
        return 0;
      }
    };

    std::string method = getString("method");

    // Dispatch to method handlers
    if (method == "initialize") {
      std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                             ",\"result\":{\"capabilities\":{"
                             "\"textDocumentSync\":1,"
                             "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                             "\"hoverProvider\":true,"
                             "\"definitionProvider\":true,"
                             "\"codeActionProvider\":true,"
                             "\"inlayHintProvider\":true,"
                             "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}"
                             "}}}";
      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "textDocument/didOpen") {
      std::string uri = getString("uri");

      // Extract document text from JSON
      size_t textPos = content.find("\"text\"");
      if (textPos != std::string::npos) {
        size_t colonPos = content.find(':', textPos);
        size_t startQuote = content.find('"', colonPos + 1);

        if (startQuote != std::string::npos) {
          std::string text;
          // Unescape JSON string
          for (size_t i = startQuote + 1; i < content.length(); i++) {
            if (content[i] == '"' && (i == startQuote + 1 || content[i - 1] != '\\'))
              break;
            if (content[i] == '\\') {
              i++;
              if (i >= content.length())
                break;
              if (content[i] == 'n')
                text += '\n';
              else if (content[i] == 'r')
                text += '\r';
              else if (content[i] == 't')
                text += '\t';
              else if (content[i] == '"')
                text += '"';
              else if (content[i] == '\\')
                text += '\\';
              else
                text += content[i];
            } else {
              text += content[i];
            }
          }
          didOpen(uri, text, 1);
        }
      }
    } else if (method == "textDocument/didChange") {
      std::string uri = getString("uri");

      // Extract text from contentChanges
      size_t textPos = content.find("\"text\"");
      if (textPos != std::string::npos) {
        size_t colonPos = content.find(':', textPos);
        size_t startQuote = content.find('"', colonPos + 1);

        if (startQuote != std::string::npos) {
          std::string text;
          for (size_t i = startQuote + 1; i < content.length(); i++) {
            if (content[i] == '"' && (i == startQuote + 1 || content[i - 1] != '\\'))
              break;
            if (content[i] == '\\') {
              i++;
              if (i >= content.length())
                break;
              if (content[i] == 'n')
                text += '\n';
              else if (content[i] == 'r')
                text += '\r';
              else if (content[i] == 't')
                text += '\t';
              else if (content[i] == '"')
                text += '"';
              else if (content[i] == '\\')
                text += '\\';
              else
                text += content[i];
            } else {
              text += content[i];
            }
          }
          didChange(uri, text, 2);
        }
      }
    } else if (method == "textDocument/completion") {
      std::string uri = getString("uri");
      int line = getInt("line");
      int character = getInt("character");

      auto items = getCompletions(uri, {line, character});

      std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":[";
      for (size_t i = 0; i < items.size(); i++) {
        if (i > 0)
          response += ",";
        response += "{\"label\":\"" + items[i].label +
                    "\",\"kind\":" + std::to_string(static_cast<int>(items[i].kind)) +
                    ",\"detail\":\"" + items[i].detail + "\"}";
      }
      response += "]}";

      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "textDocument/hover") {
      std::string uri = getString("uri");
      int line = getInt("line");
      int character = getInt("character");

      auto hover = getHover(uri, {line, character});

      std::string response;
      if (!hover.contents.empty()) {
        // Escape markdown content
        std::string escapedContent;
        for (char c : hover.contents) {
          if (c == '"')
            escapedContent += "\\\"";
          else if (c == '\n')
            escapedContent += "\\n";
          else if (c == '\r')
            escapedContent += "\\r";
          else if (c == '\t')
            escapedContent += "\\t";
          else if (c == '\\')
            escapedContent += "\\\\";
          else
            escapedContent += c;
        }

        response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                   ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"" +
                   escapedContent + "\"}}}";
      } else {
        response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":null}";
      }

      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "textDocument/definition") {
      std::string uri = getString("uri");
      int line = getInt("line");
      int character = getInt("character");

      auto loc = getDefinition(uri, {line, character});

      std::string response;
      if (!loc.uri.empty()) {
        response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":{\"uri\":\"" +
                   loc.uri +
                   "\",\"range\":{\"start\":{\"line\":" + std::to_string(loc.range.start.line) +
                   ",\"character\":" + std::to_string(loc.range.start.character) +
                   "},\"end\":{\"line\":" + std::to_string(loc.range.end.line) +
                   ",\"character\":" + std::to_string(loc.range.end.character) + "}}}}";
      } else {
        response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":null}";
      }

      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "textDocument/codeAction") {
      std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                             ",\"result\":[]}";
      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "shutdown") {
      std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                             ",\"result\":null}";
      std::cout << "Content-Length: " << response.length() << "\r\n\r\n" << response << std::flush;
    } else if (method == "exit") {
      break;
    }
  }
}

}  // namespace lsp
}  // namespace solis
