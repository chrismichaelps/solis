// Solis Programming Language - REPL Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "cli/interpreter/interpreter.hpp"
#include "parser/ast.hpp"
#include "type/typer.hpp"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declare for linenoise callbacks
struct linenoiseCompletions;

namespace solis {

// Forward declarations
class REPLContext;
class REPL;

// REPL Command Base Class

class REPLCommand {
public:
  virtual ~REPLCommand() = default;

  // Command metadata
  virtual std::string name() const = 0;
  virtual std::vector<std::string> aliases() const { return {}; }
  virtual std::string description() const = 0;
  virtual std::string usage() const { return ":" + name(); }

  // Execute the command
  virtual void execute(const std::string& args, REPLContext& ctx) = 0;

  // For tab completion
  virtual std::vector<std::string> completions([[maybe_unused]] const std::string& partial,
                                               [[maybe_unused]] REPLContext& ctx) {
    return {};
  }
};

// REPL Context (Shared State)

class REPLContext {
private:
  Interpreter& interp_;

public:
  TypeInference typer;
  std::map<std::string, std::string> loadedFiles;
  std::string lastLoadedFile;
  bool echoInput = false;

  // Constructor - initialize typer with builtin types
  REPLContext(Interpreter& interp)
      : interp_(interp)
      , typer(TypeEnv::builtins()) {}

  Interpreter& interp() { return interp_; }
  TypeInference& typechecker() { return typer; }

  std::vector<std::string> getBindings() const;
  InferTypePtr getType(const std::string& name);
};

// History Manager

class HistoryManager {
private:
  std::deque<std::string> history_;
  size_t maxSize_;
  size_t currentIndex_;
  std::string historyFile_;

public:
  explicit HistoryManager(size_t maxSize = 1000);
  ~HistoryManager();

  void add(const std::string& entry);
  std::string getPrevious();
  std::string getNext();
  void reset();

  void save();
  void load();

  const std::deque<std::string>& getHistory() const { return history_; }
};

// Tab Completion Engine

class CompletionEngine {
private:
  std::vector<std::string> commands_;
  std::function<std::vector<std::string>()> getBindings_;

public:
  CompletionEngine();

  void setCommandNames(const std::vector<std::string>& commands);
  void setBindingsProvider(std::function<std::vector<std::string>()> provider);

  // Get completions for input
  std::vector<std::string> complete(const std::string& input, REPLContext& ctx);

private:
  std::vector<std::string> filterMatches(const std::string& prefix,
                                         const std::vector<std::string>& candidates);
};

// Command Registry

class CommandRegistry {
private:
  std::map<std::string, std::shared_ptr<REPLCommand>> commands_;
  std::map<std::string, std::string> aliases_;  // alias -> canonical name

public:
  void registerCommand(std::shared_ptr<REPLCommand> cmd);
  std::shared_ptr<REPLCommand> getCommand(const std::string& name) const;
  std::vector<std::string> getAllCommandNames() const;
  std::vector<std::pair<std::string, std::string>> getAllCommands() const;

  bool hasCommand(const std::string& name) const;
};

// REPL Main Class

class REPL {
private:
  REPLContext ctx_;
  CommandRegistry registry_;
  HistoryManager history_;
  CompletionEngine completion_;

  bool running_;

public:
  explicit REPL(Interpreter& interp);

  // Initialize REPL: register commands, load history, set up completion
  void initialize();

  // Run the REPL loop
  void run();

  // Process a single line of input
  void processLine(const std::string& input);

  // Print banner and welcome message
  void printBanner();
  void printWelcome();

  // Stop the REPL
  void stop() { running_ = false; }

  // Accessors
  REPLContext& context() { return ctx_; }
  CommandRegistry& registry() { return registry_; }

  // Expose completion for linenoise hook
  std::vector<std::string> getCompletions(const std::string& input);

  // Friend functions for linenoise callbacks
  friend void completionHook(const char* buf, linenoiseCompletions* lc);
  friend char* hintsHook(const char* buf, int* color, int* bold);

private:
  void registerBuiltinCommands();

  std::string readInput();
  bool isBalanced(const std::string& input);
};

// Built-in Commands (Declared here, defined in repl.cpp)

class QuitCommand : public REPLCommand {
  REPL& repl_;

public:
  explicit QuitCommand(REPL& repl)
      : repl_(repl) {}
  std::string name() const override { return "quit"; }
  std::vector<std::string> aliases() const override { return {"q", "exit"}; }
  std::string description() const override { return "Exit the REPL"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class HelpCommand : public REPLCommand {
  CommandRegistry& registry_;

public:
  explicit HelpCommand(CommandRegistry& registry)
      : registry_(registry) {}
  std::string name() const override { return "help"; }
  std::vector<std::string> aliases() const override { return {"h", "?"}; }
  std::string description() const override { return "Show help message"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class TypeCommand : public REPLCommand {
public:
  std::string name() const override { return "type"; }
  std::vector<std::string> aliases() const override { return {"t"}; }
  std::string description() const override { return "Show the type of an expression"; }
  std::string usage() const override { return ":type EXPRESSION"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class InfoCommand : public REPLCommand {
public:
  std::string name() const override { return "info"; }
  std::vector<std::string> aliases() const override { return {"i"}; }
  std::string description() const override { return "Show information about an identifier"; }
  std::string usage() const override { return ":info IDENTIFIER"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class BrowseCommand : public REPLCommand {
public:
  std::string name() const override { return "browse"; }
  std::vector<std::string> aliases() const override { return {"b"}; }
  std::string description() const override { return "List all bindings in scope"; }
  std::string usage() const override { return ":browse [MODULE]"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class KindCommand : public REPLCommand {
public:
  std::string name() const override { return "kind"; }
  std::vector<std::string> aliases() const override { return {"k"}; }
  std::string description() const override { return "Show the kind of a type"; }
  std::string usage() const override { return ":kind TYPE"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class LoadCommand : public REPLCommand {
public:
  std::string name() const override { return "load"; }
  std::vector<std::string> aliases() const override { return {"l"}; }
  std::string description() const override { return "Load and execute a file"; }
  std::string usage() const override { return ":load FILE"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class ReloadCommand : public REPLCommand {
public:
  std::string name() const override { return "reload"; }
  std::vector<std::string> aliases() const override { return {"r"}; }
  std::string description() const override { return "Reload the last loaded file"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class CompileCommand : public REPLCommand {
public:
  std::string name() const override { return "compile"; }
  std::vector<std::string> aliases() const override { return {"c"}; }
  std::string description() const override { return "Compile a file to native executable"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

class ClearCommand : public REPLCommand {
public:
  std::string name() const override { return "clear"; }
  std::vector<std::string> aliases() const override {
    return {};
  }  // Removed :c to avoid conflict with :compile
  std::string description() const override { return "Clear REPL state (reset interpreter)"; }
  void execute(const std::string& args, REPLContext& ctx) override;
};

}  // namespace solis
