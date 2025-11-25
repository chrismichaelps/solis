// Solis Programming Language - Type System Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "error/errors.hpp"
#include "parser/ast.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace solis {

// Inference Types (separate from AST types, using shared_ptr)

struct InferType;
using InferTypePtr = std::shared_ptr<InferType>;

struct InferTyVar {
  int id;
  std::string name;
};

struct InferTyCon {
  std::string name;
  std::vector<InferTypePtr> args;
};

struct InferTyFun {
  InferTypePtr from;
  InferTypePtr to;
};

struct Constraint {
  std::string name;
  InferTypePtr type;
};

struct InferTyQual {
  std::vector<Constraint> constraints;
  InferTypePtr type;
};

struct InferTyForall {
  std::vector<int> quantified;
  InferTypePtr body;
};

struct InferType {
  std::variant<InferTyVar, InferTyCon, InferTyFun, InferTyQual, InferTyForall> node;
};

// Type Utilities

// Get free type variables
std::set<int> freeTypeVars(const InferTypePtr& type);

// Pretty print
std::string typeToString(const InferTypePtr& type);

// Construct common types
InferTypePtr tyInt();
InferTypePtr tyFloat();
InferTypePtr tyString();
InferTypePtr tyBool();
InferTypePtr tyList(InferTypePtr elem);
InferTypePtr tyFun(InferTypePtr from, InferTypePtr to);
InferTypePtr tyFun(std::vector<InferTypePtr> args, InferTypePtr ret);
InferTypePtr tyQual(std::vector<Constraint> constraints, InferTypePtr type);

// Convert between AST types and inference types
InferTypePtr fromASTType(const TypePtr& astType);
TypePtr toASTType(const InferTypePtr& inferType);

// Substitution

class Substitution {
private:
  std::map<int, InferTypePtr> subst_;

public:
  Substitution() = default;
  Substitution(int var, InferTypePtr type);
  Substitution(std::map<int, InferTypePtr> s)
      : subst_(std::move(s)) {}

  InferTypePtr apply(const InferTypePtr& type) const;
  Substitution compose(const Substitution& other) const;

  bool empty() const { return subst_.empty(); }
  const std::map<int, InferTypePtr>& map() const { return subst_; }
  std::string toString() const;
};

// Type Schemes

struct TypeScheme {
  std::set<int> quantified;
  InferTypePtr type;

  TypeScheme()
      : type(nullptr) {}  // Default constructor
  TypeScheme(InferTypePtr t)
      : type(std::move(t)) {}
  TypeScheme(std::set<int> q, InferTypePtr t)
      : quantified(std::move(q))
      , type(std::move(t)) {}


  InferTypePtr instantiate() const;
  std::set<int> freeVars() const;
  std::string toString() const;
};

// Type Environment

class TypeEnv {
private:
  std::map<std::string, TypeScheme> bindings_;

public:
  TypeEnv() = default;
  TypeEnv(std::map<std::string, TypeScheme> b)
      : bindings_(std::move(b)) {}

  TypeScheme lookup(const std::string& name) const;
  bool contains(const std::string& name) const;

  void extend(const std::string& name, const TypeScheme& scheme);
  void extend(const std::string& name, const InferTypePtr& type);
  void remove(const std::string& name);

  std::set<int> freeVars() const;
  TypeEnv apply(const Substitution& subst) const;
  TypeScheme generalize(const InferTypePtr& type) const;

  static TypeEnv builtins();
};

// Type Inference

struct InferResult {
  Substitution subst;
  InferTypePtr type;
  std::vector<Constraint> constraints;

  InferResult(Substitution s, InferTypePtr t, std::vector<Constraint> c = {})
      : subst(std::move(s))
      , type(std::move(t))
      , constraints(std::move(c)) {}
};

class TypeInference {
private:
  TypeEnv env_;
  ErrorCollector* errorCollector_ = nullptr;  // Optional error collector

  // Fresh type variable generation
  InferTypePtr freshTyVar(const std::string& hint = "");

  // Note: unification uses the global unify() function, not a member

  InferResult inferLit(const Lit& lit);
  InferResult inferVar(const Var& var);
  InferResult inferLambda(const Lambda& lam);
  InferResult inferApp(const App& app);
  InferResult inferBinOp(const BinOp& op);
  InferResult inferIf(const If& ifExpr);
  InferResult inferLet(const Let& let);
  InferResult inferList(const List& list);
  InferResult inferMatch(const Match& match);
  InferResult inferBlock(const Block& block);
  InferResult inferRecord(const Record& rec);
  InferResult inferRecordAccess(const RecordAccess& access);

public:
  bool occursCheck(int id, InferTypePtr type);

  // Helper to report or collect errors
  void reportError(SolisError error);

public:
  TypeInference(const TypeEnv& env = TypeEnv(), ErrorCollector* collector = nullptr)
      : env_(env)
      , errorCollector_(collector) {}

  // Set error collector
  void setErrorCollector(ErrorCollector* collector) { errorCollector_ = collector; }

  InferResult infer(const Expr& expr);
  InferResult inferDecl(const Decl& decl);

  const TypeEnv& env() const { return env_; }
  TypeEnv& env() { return env_; }
  void setEnv(const TypeEnv& env) { env_ = env; }
};

}  // namespace solis
