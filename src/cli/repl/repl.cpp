// Solis Programming Language - REPL (Read-Eval-Print Loop)
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/repl/repl.hpp"

#include "error/errors.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// ANSI color codes (static to avoid linker errors)
static const char* RED = "\033[31m";
static const char* YELLOW = "\033[33m";
static const char* GREEN = "\033[32m";
static const char* CYAN = "\033[36m";
static const char* MAGENTA = "\033[35m";
static const char* BOLD = "\033[1m";
static const char* RESET = "\033[0m";

#include "cli/compiler/compiler.hpp"

namespace solis {

// REPLContext Implementation

std::vector<std::string> REPLContext::getBindings() const {
  return interp_.getBindingNames();
}

InferTypePtr REPLContext::getType(const std::string& name) {
  if (typer.env().contains(name)) {
    return typer.env().lookup(name).instantiate();
  }
  return nullptr;
}

// HistoryManager Implementation

HistoryManager::HistoryManager(size_t maxSize)
    : maxSize_(maxSize)
    , currentIndex_(0) {
  // Set history file path
#ifdef _WIN32
  const char* home = getenv("USERPROFILE");
#else
  const char* home = getenv("HOME");
#endif
  if (home) {
    historyFile_ = std::string(home) + "/.solis_history";
  } else {
    historyFile_ = ".solis_history";
  }

  load();
}

HistoryManager::~HistoryManager() {
  save();
}

void HistoryManager::add(const std::string& entry) {
  // Don't add empty entries or duplicates of last entry
  if (entry.empty() || (!history_.empty() && history_.back() == entry)) {
    return;
  }

  history_.push_back(entry);

  // Maintain max size
  if (history_.size() > maxSize_) {
    history_.pop_front();
  }

  currentIndex_ = history_.size();
}

std::string HistoryManager::getPrevious() {
  if (history_.empty() || currentIndex_ == 0) {
    return "";
  }
  currentIndex_--;
  return history_[currentIndex_];
}

std::string HistoryManager::getNext() {
  if (history_.empty() || currentIndex_ >= history_.size() - 1) {
    currentIndex_ = history_.size();
    return "";
  }
  currentIndex_++;
  return history_[currentIndex_];
}

void HistoryManager::reset() {
  currentIndex_ = history_.size();
}

void HistoryManager::save() {
  // Try to open file for writing, create if doesn't exist
  std::ofstream file(historyFile_, std::ios::out | std::ios::trunc);
  if (!file) {
    // If file exists but can't write, try to fix permissions
    if (access(historyFile_.c_str(), F_OK) == 0) {
      chmod(historyFile_.c_str(), 0600);
      file.open(historyFile_, std::ios::out | std::ios::trunc);
    }
    if (!file) {
      return;  // Silently fail - not critical
    }
  }

  // Save last 1000 entries (or maxSize_)
  size_t startIdx = history_.size() > maxSize_ ? history_.size() - maxSize_ : 0;
  for (size_t i = startIdx; i < history_.size(); ++i) {
    file << history_[i] << "\n"
         << "---\n";
    // Use separator for multi-line entries
  }
}

void HistoryManager::load() {
  std::ifstream file(historyFile_);
  if (!file) {
    return;  // File doesn't exist yet, that's ok
  }

  std::string line, entry;
  while (std::getline(file, line)) {
    if (line == "---") {
      if (!entry.empty()) {
        history_.push_back(entry);
        entry.clear();
      }
    } else {
      if (!entry.empty()) {
        entry += "\n";
      }
      entry += line;
    }
  }

  // Add last entry if exists
  if (!entry.empty()) {
    history_.push_back(entry);
  }

  currentIndex_ = history_.size();
}

// CompletionEngine Implementation

CompletionEngine::CompletionEngine() {}

void CompletionEngine::setCommandNames(const std::vector<std::string>& commands) {
  commands_ = commands;
}

void CompletionEngine::setBindingsProvider(std::function<std::vector<std::string>()> provider) {
  getBindings_ = provider;
}

std::vector<std::string> CompletionEngine::complete(const std::string& input,
                                                    [[maybe_unused]] REPLContext& ctx) {
  if (input.empty()) {
    return {};
  }

  // If starts with ':', complete command names
  if (input[0] == ':') {
    std::string cmdPrefix = input.substr(1);
    return filterMatches(cmdPrefix, commands_);
  }

  // Otherwise, complete bindings and keywords
  std::vector<std::string> candidates;

  // Add keywords
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
  candidates.insert(candidates.end(), keywords.begin(), keywords.end());

  if (getBindings_) {
    auto bindings = getBindings_();
    candidates.insert(candidates.end(), bindings.begin(), bindings.end());
  }

  return filterMatches(input, candidates);
}

std::vector<std::string> CompletionEngine::filterMatches(
    const std::string& prefix, const std::vector<std::string>& candidates) {
  std::vector<std::string> matches;
  for (const auto& candidate : candidates) {
    if (candidate.size() >= prefix.size() && candidate.substr(0, prefix.size()) == prefix) {
      matches.push_back(candidate);
    }
  }
  return matches;
}

// CommandRegistry Implementation

void CommandRegistry::registerCommand(std::shared_ptr<REPLCommand> cmd) {
  std::string name = cmd->name();
  commands_[name] = cmd;

  // Register aliases
  for (const auto& alias : cmd->aliases()) {
    aliases_[alias] = name;
  }
}

std::shared_ptr<REPLCommand> CommandRegistry::getCommand(const std::string& name) const {
  // Try direct lookup
  auto it = commands_.find(name);
  if (it != commands_.end()) {
    return it->second;
  }

  // Try alias lookup
  auto aliasIt = aliases_.find(name);
  if (aliasIt != aliases_.end()) {
    return commands_.at(aliasIt->second);
  }

  return nullptr;
}

std::vector<std::string> CommandRegistry::getAllCommandNames() const {
  std::vector<std::string> names;
  for (const auto& [name, _] : commands_) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::pair<std::string, std::string>> CommandRegistry::getAllCommands() const {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& [name, cmd] : commands_) {
    result.push_back({name, cmd->description()});
  }
  return result;
}

bool CommandRegistry::hasCommand(const std::string& name) const {
  return commands_.count(name) > 0 || aliases_.count(name) > 0;
}

// REPL Implementation

REPL::REPL(Interpreter& interp)
    : ctx_(interp)
    , registry_()
    , history_()
    , completion_()
    , running_(false) {}

#include "utils/linenoise.h"

// Global pointer to REPL instance for callback
static REPL* g_replInstance = nullptr;

// Bridge function for linenoise completion
void completionHook(const char* buf, linenoiseCompletions* lc) {
  if (!g_replInstance)
    return;

  std::string input(buf);
  // Get completions from our engine
  auto matches = g_replInstance->getCompletions(input);

  for (const auto& match : matches) {
    linenoiseAddCompletion(lc, match.c_str());
  }
}

// Bridge function for linenoise hints (inline suggestions)
char* hintsHook(const char* buf, int* color, int* bold) {
  if (!g_replInstance)
    return nullptr;

  std::string input(buf);
  if (input.empty())
    return nullptr;

  // Get completions from our engine
  auto matches = g_replInstance->getCompletions(input);

  // Single match that extends input: show as inline hint
  if (matches.size() == 1 && matches[0].find(input) == 0 && matches[0] != input) {
    *color = 90;  // Gray color
    *bold = 0;    // Not bold
    std::string hint = matches[0].substr(input.length());
    return strdup(hint.c_str());
  }

  // If multiple matches, show the common prefix if it's longer than input
  if (matches.size() > 1) {
    std::string commonPrefix = matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
      size_t j = 0;
      while (j < commonPrefix.length() && j < matches[i].length() &&
             commonPrefix[j] == matches[i][j]) {
        ++j;
      }
      commonPrefix = commonPrefix.substr(0, j);
    }

    if (commonPrefix.length() > input.length()) {
      *color = 90;  // Gray color
      *bold = 0;    // Not bold
      std::string hint = commonPrefix.substr(input.length());
      return strdup(hint.c_str());
    }
  }

  return nullptr;
}

void REPL::initialize() {
  registerBuiltinCommands();

  // Set up completion with command names
  completion_.setCommandNames(registry_.getAllCommandNames());

  // Set up completion with bindings provider
  completion_.setBindingsProvider([this]() { return ctx_.getBindings(); });

  // Set up linenoise
  g_replInstance = this;
  linenoiseSetCompletionCallback(completionHook);
  linenoiseSetHintsCallback(hintsHook);
  linenoiseSetFreeHintsCallback(free);

// Load history
#ifdef _WIN32
  const char* home = getenv("USERPROFILE");
#else
  const char* home = getenv("HOME");
#endif
  std::string historyFile = ".solis_history";
  if (home) {
    historyFile = std::string(home) + "/.solis_history";
  }
  linenoiseHistoryLoad(historyFile.c_str());
}

std::string REPL::readInput() {
  std::string accumulated;
  char* line;

  while (true) {
    std::string prompt;
    if (accumulated.empty()) {
      prompt = "solis> ";
    } else {
      prompt = " ...> ";
    }

    line = linenoise(prompt.c_str());

    if (line == NULL) {
      // EOF or error
      if (accumulated.empty()) {
        return "";
      }
      break;
    }

    std::string lineStr(line);
    free(line);  // linenoise allocates with malloc

    // Add to history if not empty
    if (!lineStr.empty()) {
      linenoiseHistoryAdd(lineStr.c_str());

// Save history incrementally
#ifdef _WIN32
      const char* home = getenv("USERPROFILE");
#else
      const char* home = getenv("HOME");
#endif
      std::string historyFile = ".solis_history";
      if (home) {
        historyFile = std::string(home) + "/.solis_history";
      }
      linenoiseHistorySave(historyFile.c_str());
    }

    accumulated += lineStr + "\n";

    // Check if input is complete
    if (isBalanced(accumulated)) {
      break;
    }
  }

  return accumulated;
}

// Expose completion engine for the hook
std::vector<std::string> REPL::getCompletions(const std::string& input) {
  return completion_.complete(input, ctx_);
}

bool REPL::isBalanced(const std::string& input) {
  int braces = 0, brackets = 0, parens = 0;
  bool inString = false;

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];

    // Handle strings
    if (c == '"' && (i == 0 || input[i - 1] != '\\')) {
      inString = !inString;
      continue;
    }

    if (inString)
      continue;

    // Handle comments
    if (i + 1 < input.size() && input[i] == '-' && input[i + 1] == '-') {
      while (i < input.size() && input[i] != '\n')
        ++i;
      continue;
    }

    // Count delimiters
    if (c == '{')
      braces++;
    else if (c == '}')
      braces--;
    else if (c == '[')
      brackets++;
    else if (c == ']')
      brackets--;
    else if (c == '(')
      parens++;
    else if (c == ')')
      parens--;
  }

  return braces == 0 && brackets == 0 && parens == 0;
}

// Built-in Commands Implementation

void QuitCommand::execute(const std::string&, REPLContext&) {
  std::cout << "Goodbye!" << std::endl;
  repl_.stop();
}

void HelpCommand::execute(const std::string&, REPLContext&) {
  std::cout << "\n" << BOLD << "Solis REPL Commands:" << RESET << "\n";

  auto commands = registry_.getAllCommands();
  // Sort by name
  std::sort(commands.begin(), commands.end());

  for (const auto& [name, desc] : commands) {
    auto cmd = registry_.getCommand(name);
    std::cout << "  " << GREEN << ":" << name << RESET;

    // Show aliases
    auto aliases = cmd->aliases();
    if (!aliases.empty()) {
      std::cout << " (";
      for (size_t i = 0; i < aliases.size(); ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << ":" << aliases[i];
      }
      std::cout << ")";
    }

    std::cout << "\n    " << desc << "\n";
  }

  std::cout << "\n" << BOLD << "Examples:" << RESET << "\n";
  std::cout << "  let x = 42\n";
  std::cout << "  let add x y = x + y\n";
  std::cout << "  :type map\n";
  std::cout << "  :info map\n";
  std::cout << "  :browse\n" << std::endl;
}

void TypeCommand::execute(const std::string& args, REPLContext& ctx) {
  if (args.empty()) {
    std::cerr << YELLOW << "Usage:" << RESET << " :type EXPRESSION" << std::endl;
    return;
  }

  try {
    Lexer lexer(args);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto expr = parser.parseExpression();

    // Special case: if it's just a variable, show its TypeScheme directly
    if (auto* var = std::get_if<Var>(&expr->node)) {
      if (ctx.typer.env().contains(var->name)) {
        auto scheme = ctx.typer.env().lookup(var->name);
        std::cout << args << " :: " << CYAN << scheme.toString() << RESET << std::endl;
        return;
      }
    }

    // Otherwise, infer the type
    TypeInference typer(ctx.typer.env());
    auto result = typer.infer(*expr);

    // Apply substitution and display
    auto finalType = result.subst.apply(result.type);
    if (!result.constraints.empty()) {
      finalType = tyQual(result.constraints, finalType);
    }

    std::cout << args << " :: " << CYAN << typeToString(finalType) << RESET << std::endl;
  } catch (const SolisError& e) {
    std::cerr << e.display();
  } catch (const std::exception& e) {
    std::cerr << RED << "Error:" << RESET << " " << e.what() << std::endl;
  }
}

void InfoCommand::execute(const std::string& args, REPLContext& ctx) {
  if (args.empty()) {
    std::cerr << YELLOW << "Usage:" << RESET << " :info IDENTIFIER" << std::endl;
    return;
  }

  std::string name = args;
  // Trim whitespace
  name.erase(0, name.find_first_not_of(" \t"));
  name.erase(name.find_last_not_of(" \t") + 1);

  // Check if binding exists
  if (!ctx.interp().hasBinding(name)) {
    std::cerr << RED << "Error:" << RESET << " Binding '" << name << "' not found.\n";
    std::cerr << "  Use " << GREEN << ":browse" << RESET << " to see all bindings." << std::endl;
    return;
  }

  try {
    // Get the binding
    auto binding = ctx.interp().getBinding(name);

    std::cout << BOLD << CYAN << name << RESET << "\n";

    // Try to show type
    try {
      auto type = ctx.getType(name);
      if (type) {
        std::cout << "  Type: " << MAGENTA << typeToString(type) << RESET << "\n";
      }
    } catch (...) {
      // Type inference failed, skip
    }

    // Try to show value
    try {
      std::string valueStr = ctx.interp().valueToString(binding);
      std::cout << "  Value: " << GREEN << valueStr << RESET << "\n";
    } catch (...) {
      std::cout << "  Value: " << YELLOW << "<not evaluable>" << RESET << "\n";
    }

    std::cout << "  Defined in: " << YELLOW << "<REPL or Prelude>" << RESET << "\n";
    std::cout << std::endl;
  } catch (const std::exception& e) {
    std::cerr << RED << "Error:" << RESET << " " << e.what() << std::endl;
  }
}

void BrowseCommand::execute([[maybe_unused]] const std::string& args, REPLContext& ctx) {
  std::cout << BOLD << "Bindings in scope:" << RESET << "\n\n";

  auto bindings = ctx.getBindings();

  if (bindings.empty()) {
    std::cout << "  (No user-defined bindings)\n";
    std::cout << "  Try defining something with " << GREEN << "let" << RESET << "\n\n";
    return;
  }

  // Get stored declarations from interpreter to infer types
  [[maybe_unused]] auto& interp = ctx.interp();

  for (const auto& name : bindings) {
    std::cout << "  " << BOLD << CYAN << name << RESET << " :: ";

    try {
      // First check if type is already in typer environment
      auto type = ctx.getType(name);
      if (type) {
        std::cout << MAGENTA << typeToString(type) << RESET << "\n";
        continue;
      }

      // If not in typer env, try to infer it dynamically
      // Handle prelude functions not type-checked during load
      // just show <unknown> - implementing full dynamic inference
      // would require accessing the stored AST declarations
      std::cout << errors::DIM << "<unknown>" << errors::RESET << "\n";

    } catch (...) {
      std::cout << errors::DIM << "<unknown>" << errors::RESET << "\n";
    }
  }

  std::cout << "\n"
            << YELLOW << "Total: " << bindings.size() << " binding(s)" << RESET << "\n"
            << std::endl;
}

void KindCommand::execute(const std::string& args, [[maybe_unused]] REPLContext& ctx) {
  if (args.empty()) {
    std::cerr << YELLOW << "Usage:" << RESET << " :kind TYPE" << std::endl;
    return;
  }

  std::string typeName = args;
  typeName.erase(0, typeName.find_first_not_of(" \t"));
  typeName.erase(typeName.find_last_not_of(" \t") + 1);

  // Built-in kinds (simplified - in real system would parse type expressions)
  std::map<std::string, std::string> builtinKinds = {
      {"Int", "*"},
      {"Float", "*"},
      {"String", "*"},
      {"Bool", "*"},
      {"List", "* -> *"},
      {"Maybe", "* -> *"},
      {"Either", "* -> * -> *"},
  };

  auto it = builtinKinds.find(typeName);
  if (it != builtinKinds.end()) {
    std::cout << CYAN << typeName << RESET << " :: " << BOLD << it->second << RESET << std::endl;
  } else {
    std::cerr << YELLOW << "Warning:" << RESET << " Kind inference not yet fully implemented\n";
    std::cout << CYAN << typeName << RESET << " :: " << MAGENTA << "?" << RESET << std::endl;
  }
}

void LoadCommand::execute(const std::string& args, REPLContext& ctx) {
  if (args.empty()) {
    std::cerr << YELLOW << "Usage:" << RESET << " :load FILE" << std::endl;
    return;
  }

  std::string filename = args;
  filename.erase(0, filename.find_first_not_of(" \t"));
  filename.erase(filename.find_last_not_of(" \t") + 1);

  std::ifstream file(filename);
  if (!file) {
    std::cerr << RED << "Error:" << RESET << " Could not open file: " << filename << std::endl;
    return;
  }

  ctx.lastLoadedFile = filename;

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  try {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));

    auto module = parser.parseModule();

    for (auto& decl : module.declarations) {
      ctx.interp().evalAndStore(std::move(decl));
    }

    std::cout << GREEN << "Loaded " << filename << RESET << std::endl;
  } catch (const std::exception& e) {
    std::cerr << RED << "Error loading " << filename << ":" << RESET << " " << e.what()
              << std::endl;
  }
}

void ReloadCommand::execute(const std::string&, REPLContext& ctx) {
  if (ctx.lastLoadedFile.empty()) {
    std::cerr << YELLOW << "Warning:" << RESET
              << " No file has been loaded yet. Use :load FILE first." << std::endl;
    return;
  }

  std::string filename = ctx.lastLoadedFile;
  LoadCommand loadCmd;
  loadCmd.execute(filename, ctx);
}

void ClearCommand::execute(const std::string&, [[maybe_unused]] REPLContext& ctx) {
  // Clear command implementation
  // Current limitation: interpreter reference cannot be replaced
  // Full state reset requires REPL restart

  std::cout << YELLOW << "Clearing REPL state..." << RESET << std::endl;

  std::cout << YELLOW << "Limitation:" << RESET
            << " Full clear requires restarting REPL. Use :reload "
               "to reload last file.\n";
  std::cout << "To fully reset, exit with " << GREEN << ":quit" << RESET << " and restart."
            << std::endl;
}

void CompileCommand::execute(const std::string& args, [[maybe_unused]] REPLContext& ctx) {
  if (args.empty()) {
    std::cerr << YELLOW << "Usage:" << RESET << " :compile FILE" << std::endl;
    return;
  }

  std::string filename = args;
  filename.erase(0, filename.find_first_not_of(" \t"));
  filename.erase(filename.find_last_not_of(" \t") + 1);

  compileFile(filename);
}

void REPL::registerBuiltinCommands() {
  // Register all built-in commands
  registry_.registerCommand(std::make_shared<HelpCommand>(registry_));
  registry_.registerCommand(std::make_shared<QuitCommand>(*this));
  registry_.registerCommand(std::make_shared<TypeCommand>());
  registry_.registerCommand(std::make_shared<InfoCommand>());
  registry_.registerCommand(std::make_shared<BrowseCommand>());
  registry_.registerCommand(std::make_shared<LoadCommand>());
  registry_.registerCommand(std::make_shared<ReloadCommand>());
  registry_.registerCommand(std::make_shared<ClearCommand>());
  registry_.registerCommand(std::make_shared<CompileCommand>());
  registry_.registerCommand(std::make_shared<KindCommand>());
}

void REPL::run() {
  printBanner();
  printWelcome();
  running_ = true;

  while (running_) {
    std::string input = readInput();

    if (input.empty()) {
      // EOF
      break;
    }

    processLine(input);
  }
}

void REPL::printBanner() {
  // Simple banner (prelude loaded info will be shown separately if needed)
}

void REPL::printWelcome() {
  std::cout << CYAN << R"(
   _____  ____  _      _____  _____
  / ____|/ __ \| |    |_   _|/ ____|
 | (___ | |  | | |      | | | (___
  \___ \| |  | | |      | |  \___ \
  ____) | |__| | |____ _| |_ ____) |
 |_____/ \____/|______|_____|_____/
)" << RESET << std::endl;

  std::cout << BOLD << "Solis REPL" << RESET << " v0.1" << std::endl;
  std::cout << "Type " << GREEN << ":help" << RESET << " for commands, " << GREEN << ":quit"
            << RESET << " to exit.\n"
            << std::endl;
}

void REPL::processLine(const std::string& input) {
  // Trim input
  std::string trimmed = input;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\n") + 1);

  if (trimmed.empty()) {
    return;
  }

  // Check if it's a command
  if (trimmed[0] == ':') {
    std::string cmdLine = trimmed.substr(1);
    size_t spacePos = cmdLine.find(' ');
    std::string cmdName, args;

    if (spacePos != std::string::npos) {
      cmdName = cmdLine.substr(0, spacePos);
      args = cmdLine.substr(spacePos + 1);
    } else {
      cmdName = cmdLine;
    }

    auto cmd = registry_.getCommand(cmdName);
    if (cmd) {
      try {
        cmd->execute(args, ctx_);
      } catch (const std::exception& e) {
        std::cerr << RED << "Command error:" << RESET << " " << e.what() << std::endl;
      }
    } else {
      std::cerr << RED << "Unknown command:" << RESET << " :" << cmdName << "\n";
      std::cout << "Type " << GREEN << ":help" << RESET << " for available commands." << std::endl;
    }
    return;
  }

  // Not a command - evaluate as expression
  try {
    Lexer lexer(trimmed);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));

    // Check if it's an import statement
    if (!trimmed.empty() && trimmed.substr(0, 6) == "import") {
      // Extract module name manually (parser.parseDeclaration doesn't handle
      // import)
      std::string moduleName = trimmed.substr(6);  // Skip "import"
      // Trim whitespace
      moduleName.erase(0, moduleName.find_first_not_of(" \t"));
      moduleName.erase(moduleName.find_last_not_of(" \t\n\r") + 1);

      // Create ImportDecl and evaluate it
      ImportDecl importDecl{moduleName,
                            false,
                            std::nullopt,
                            {},
                            {}};  // not qualified, no alias, no specific imports, no hiding
      Decl decl;
      decl.node = importDecl;
      ctx_.interp().eval(decl);
      std::cout << GREEN << "[OK]" << RESET << " Module imported" << std::endl;
      return;
    }

    // Check if it starts with 'let' keyword
    if (!trimmed.empty() && trimmed.substr(0, 3) == "let") {
      auto decl = parser.parseDeclaration();

      // Infer type and store in type environment
      try {
        TypeInference typer(ctx_.typer.env());
        typer.inferDecl(*decl);
        // Update the REPL's type environment with the new bindings
        ctx_.typer = typer;
      } catch (...) {
        // Type inference failed, but still evaluate
      }

      ctx_.interp().evalAndStore(std::move(decl));
      std::cout << GREEN << "[OK]" << RESET << std::endl;
    } else {
      // Parse as expression
      auto expr = parser.parseExpression();

      // Special case: if it's just a variable, show its TypeScheme directly
      if (auto* var = std::get_if<Var>(&expr->node)) {
        auto result = ctx_.interp().eval(*expr);

        // Try to look up the variable in the type environment
        if (ctx_.typer.env().contains(var->name)) {
          auto scheme = ctx_.typer.env().lookup(var->name);
          std::cout << ctx_.interp().valueToString(result) << " :: " << CYAN << scheme.toString()
                    << RESET << std::endl;
          return;
        }
      }

      // Normal inference for all other expressions
      auto result = ctx_.interp().eval(*expr);

      // Try to get type
      try {
        TypeInference typer(ctx_.typer.env());
        auto typeResult = typer.infer(*expr);

        // Filter out built-in operator constraints for cleaner display
        std::vector<Constraint> displayConstraints;
        std::set<std::string> builtinOps = {
            "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">="};

        for (const auto& c : typeResult.constraints) {
          if (builtinOps.find(c.name) == builtinOps.end()) {
            displayConstraints.push_back(c);
          }
        }

        // Wrap type with constraints if any non-builtin constraints exist
        InferTypePtr displayType = typeResult.type;
        if (!displayConstraints.empty()) {
          displayType = tyQual(displayConstraints, typeResult.type);
        }

        std::cout << ctx_.interp().valueToString(result) << " :: " << CYAN
                  << typeToString(displayType) << RESET << std::endl;
      } catch (...) {
        std::cout << ctx_.interp().valueToString(result) << std::endl;
      }
    }
  } catch (const SolisError& e) {
    std::cerr << e.display();
  } catch (const std::exception& e) {
    std::cerr << RED << "Error:" << RESET << " " << e.what() << std::endl;
  }
}

}  // namespace solis
