// Solis Programming Language - Interpreter Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "parser/ast.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace solis {

// Forward declarations for module system
class ModuleResolver;
class NamespaceManager;

// Runtime values
struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct IntValue {
  int64_t value;
};
struct FloatValue {
  double value;
};
struct StringValue {
  std::string value;
};
struct BoolValue {
  bool value;
};
struct ListValue {
  std::vector<ValuePtr> elements;
};
// Thunk: lazy computation for forward references
struct ThunkValue {
  std::function<ValuePtr()> computation;
  mutable std::optional<ValuePtr> cached;

  ValuePtr force() const {
    if (!cached) {
      cached = computation();
    }
    return *cached;
  }
};

struct FunctionValue {
  std::function<ValuePtr(ValuePtr)> func;
};
struct RecordValue {
  std::map<std::string, ValuePtr> fields;
};
struct ConstructorValue {
  std::string name;
  std::vector<ValuePtr> args;
};
struct BigIntValue {
  BigInt value;
};

struct Value {
  std::variant<IntValue,
               FloatValue,
               StringValue,
               BoolValue,
               ListValue,
               FunctionValue,
               ThunkValue,
               RecordValue,
               ConstructorValue,
               BigIntValue>
      data;
};

// Environment for variable bindings
using Environment = std::map<std::string, ValuePtr>;

class Interpreter {
public:
  Interpreter();

  ValuePtr eval(const Expr& expr);
  ValuePtr eval(const Expr& expr, Environment& env);
  void eval(const Decl& decl);
  void evalAndStore(DeclPtr decl);  // Takes ownership and stores AST

  void addBinding(const std::string& name, ValuePtr value);
  std::string valueToString(const ValuePtr& value);

  // Access bindings (for REPL commands)
  std::vector<std::string> getBindingNames() const;
  bool hasBinding(const std::string& name) const;
  ValuePtr getBinding(const std::string& name) const;

  // Access declarations (for on-demand type inference)
  const std::vector<DeclPtr>& getDeclarations() const { return declarations_; }

  // Module system integration
  void setModuleResolver(std::shared_ptr<ModuleResolver> resolver);
  void setNamespaceManager(std::shared_ptr<NamespaceManager> nsManager);
  std::shared_ptr<ModuleResolver> getModuleResolver() const { return moduleResolver_; }
  std::shared_ptr<NamespaceManager> getNamespaceManager() const { return namespaceManager_; }

  // Set current directory for module resolution
  void setCurrentDirectory(const std::string& dir) { currentDirectory_ = dir; }
  std::string getCurrentDirectory() const { return currentDirectory_; }

private:
  ValuePtr force(const ValuePtr& value);
  ValuePtr evalBinOp(const std::string& op, const ValuePtr& left, const ValuePtr& right);
  bool matchPattern(const Pattern& pattern, const ValuePtr& value, Environment& env);
  void evalDeclAtIndex(size_t declIndex);  // Evaluate a declaration by its index

  Environment globalEnv_;
  std::vector<DeclPtr> declarations_;  // Store declarations to keep AST alive
  std::set<std::string> loadedModules_;

  // Module system components
  std::shared_ptr<ModuleResolver> moduleResolver_;
  std::shared_ptr<NamespaceManager> namespaceManager_;
  std::string currentDirectory_;  // Current directory for relative imports
};

}  // namespace solis
