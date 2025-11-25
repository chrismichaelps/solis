// Solis Programming Language - LSP Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "cli/interpreter/interpreter.hpp"
#include "cli/lsp/symbol_index.hpp"
#include "cli/module/module_resolver.hpp"
#include "cli/module/namespace_manager.hpp"
#include "parser/ast.hpp"
#include "type/typer.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace solis {
namespace lsp {

// Position and Range are defined in symbol_index.hpp (included above)
// Using those definitions here

struct Location {
  std::string uri;
  Range range;
};

struct Diagnostic {
  Range range;
  std::string message;
  int severity;  // 1=Error, 2=Warning, 3=Info, 4=Hint
};

struct CompletionItem {
  std::string label;
  std::string detail;
  std::string documentation;
  int kind;  // LSP symbol kind: 3=Function, 6=Variable, 12=Class, 13=Interface
};

struct Hover {
  std::string contents;
  Range range;
};

// Document management
class Document {
public:
  std::string uri;
  std::string text;
  int version;

  Document(const std::string& uri, const std::string& text, int version)
      : uri(uri)
      , text(text)
      , version(version) {}

  void update(const std::string& newText, int newVersion) {
    text = newText;
    version = newVersion;
  }

  Position offsetToPosition(size_t offset) const;
  size_t positionToOffset(const Position& pos) const;
  std::string getLine(int line) const;
};

// LSP Server
class LanguageServer {
public:
  LanguageServer();

  // Document lifecycle
  void didOpen(const std::string& uri, const std::string& text, int version);
  void didChange(const std::string& uri, const std::string& text, int version);
  void didClose(const std::string& uri);

  // Language features
  std::vector<Diagnostic> getDiagnostics(const std::string& uri);
  std::vector<CompletionItem> getCompletions(const std::string& uri, const Position& pos);
  Hover getHover(const std::string& uri, const Position& pos);
  Location getDefinition(const std::string& uri, const Position& pos);
  std::vector<Location> getReferences(const std::string& uri, const Position& pos);

  // Advanced features
  struct DefinitionLocation {
    std::string uri;
    Range range;
    std::string name;
    InferTypePtr type;
  };

  struct InlayHint {
    Position position;
    std::string label;
    int kind;  // 1=Type, 2=Parameter
    bool paddingLeft;
    bool paddingRight;
  };

  struct ParameterInfo {
    std::string label;
    std::string documentation;
  };

  struct SignatureInformation {
    std::string label;
    std::string documentation;
    std::vector<ParameterInfo> parameters;
    int activeParameter;
  };

  struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    int activeSignature;
    int activeParameter;
  };

  struct TextEdit {
    Range range;
    std::string newText;
  };

  struct CodeAction {
    std::string title;
    std::string kind;
    std::vector<TextEdit> edits;
  };

  std::vector<InlayHint> getInlayHints(const std::string& uri, const Range& range);
  SignatureHelp getSignatureHelp(const std::string& uri, const Position& pos);
  std::vector<CodeAction> getCodeActions(const std::string& uri, const Range& range);

  // Run the LSP server loop
  void run();

  // Publish diagnostics to client
  void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);

private:
  std::map<std::string, std::shared_ptr<Document>> documents_;
  std::map<std::string, std::shared_ptr<Interpreter>> interpreters_;
  std::map<std::string, TypeInference> typers_;
  std::map<std::string, std::vector<DeclPtr>> asts_;

  // Module system integration
  std::shared_ptr<NamespaceManager> namespaceManager_;
  std::shared_ptr<ModuleResolver> moduleResolver_;

  // Definition tracking
  using DefinitionMap = std::map<std::string, DefinitionLocation>;
  std::map<std::string, DefinitionMap> definitions_;

  // Symbol index for cross-file navigation and incremental compilation
  SymbolIndex symbolIndex_;

  // Helper methods
  std::shared_ptr<Document> getDocument(const std::string& uri);
  void analyzeDocument(const std::string& uri);
  std::string getIdentifierAtPosition(const std::string& text, const Position& pos);
  std::pair<std::string, int> getFunctionCallContext(const std::string& text, const Position& pos);
};

}  // namespace lsp
}  // namespace solis
