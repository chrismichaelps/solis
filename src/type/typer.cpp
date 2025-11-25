// Solis Programming Language - Type Inference Engine
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "type/typer.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>

namespace solis {

template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

int typeVarCounter = 0;

// Type Utilities

InferTypePtr freshTyVar(const std::string& hint = "") {
  int id = typeVarCounter++;
  auto ty = std::make_shared<InferType>();

  std::string name;
  if (hint.empty()) {
    char c = 'a' + (id % 26);
    name = std::string(1, c);
    if (id >= 26)
      name += std::to_string(id / 26);
  } else {
    name = hint;
  }

  ty->node = InferTyVar{id, name};
  return ty;
}

// TypeInference member just calls global
InferTypePtr TypeInference::freshTyVar(const std::string& hint) {
  return ::solis::freshTyVar(hint);
}

std::set<int> freeTypeVars(const InferTypePtr& type) {
  if (!type)
    return {};
  return std::visit(overload{[](const InferTyVar& v) -> std::set<int> { return {v.id}; },
                             [](const InferTyCon& c) -> std::set<int> {
                               std::set<int> result;
                               for (const auto& arg : c.args) {
                                 auto vars = freeTypeVars(arg);
                                 result.insert(vars.begin(), vars.end());
                               }
                               return result;
                             },
                             [](const InferTyFun& f) -> std::set<int> {
                               auto vars = freeTypeVars(f.from);
                               auto toVars = freeTypeVars(f.to);
                               vars.insert(toVars.begin(), toVars.end());
                               return vars;
                             },
                             [](const InferTyQual& q) -> std::set<int> {
                               auto vars = freeTypeVars(q.type);
                               for (const auto& c : q.constraints) {
                                 auto cVars = freeTypeVars(c.type);
                                 vars.insert(cVars.begin(), cVars.end());
                               }
                               return vars;
                             },
                             [](const InferTyForall& f) -> std::set<int> {
                               auto vars = freeTypeVars(f.body);
                               for (int var : f.quantified) {
                                 vars.erase(var);
                               }
                               return vars;
                             }},
                    type->node);
}

// Forward declaration
static void collectVarsInOrder(const InferTypePtr& type,
                               std::vector<int>& vars,
                               std::set<int>& seen);

// Map structural constraints to typeclass names
static std::string constraintsToTypeclasses(const std::vector<Constraint>& constraints) {
  if (constraints.empty())
    return "";

  // Group constraints by variable
  std::map<std::string, std::set<std::string>> varConstraints;
  for (const auto& c : constraints) {
    // Extract the variable name from the constraint type
    if (auto* tyVar = std::get_if<InferTyVar>(&c.type->node)) {
      varConstraints[tyVar->name].insert(c.name);
    }
  }

  std::vector<std::string> typeclasses;
  for (const auto& [varName, ops] : varConstraints) {
    // Map common constraint patterns to typeclass names
    bool hasEq = ops.count("==") > 0;
    bool hasLt = ops.count("<") > 0;
    bool hasGt = ops.count(">") > 0;
    bool hasPlus = ops.count("+") > 0;
    bool hasMinus = ops.count("-") > 0;
    bool hasTimes = ops.count("*") > 0;
    bool hasDiv = ops.count("/") > 0;

    // Determine typeclass based on operations
    if ((hasLt || hasGt) && hasEq) {
      typeclasses.push_back("Ord " + varName);
    } else if (hasPlus || hasMinus || hasTimes || hasDiv) {
      typeclasses.push_back("Num " + varName);
    } else if (hasEq) {
      typeclasses.push_back("Eq " + varName);
    } else {
      // Unknown pattern - fall back to showing raw constraints
      for (const auto& op : ops) {
        typeclasses.push_back(op + " " + varName);
      }
    }
  }

  if (typeclasses.empty())
    return "";

  std::string result;
  for (size_t i = 0; i < typeclasses.size(); ++i) {
    if (i > 0)
      result += ", ";
    result += typeclasses[i];
  }
  return result;
}

// Apply canonical naming to type for display
static InferTypePtr canonicalizeTypeVars(const InferTypePtr& type) {
  if (!type)
    return type;

  // Collect all type variables in order of appearance
  std::vector<int> varsInOrder;
  std::set<int> seen;
  collectVarsInOrder(type, varsInOrder, seen);

  // Create mapping to clean names: a, b, c, ...
  std::map<int, InferTypePtr> renaming;
  int idx = 0;
  for (int varId : varsInOrder) {
    std::string newName;
    if (idx < 26) {
      newName = std::string(1, 'a' + idx);
    } else {
      newName = std::string(1, 'a' + (idx % 26)) + std::to_string(idx / 26);
    }
    auto newVar = std::make_shared<InferType>();
    newVar->node = InferTyVar{varId, newName};
    renaming[varId] = newVar;
    idx++;
  }

  // Apply renaming
  return Substitution{renaming}.apply(type);
}

std::string typeToString(const InferTypePtr& type) {
  if (!type)
    return "?";

  // Apply canonical naming for clean display
  InferTypePtr displayType = canonicalizeTypeVars(type);

  return std::visit(overload{[](const InferTyVar& v) -> std::string { return v.name; },
                             [](const InferTyCon& c) -> std::string {
                               if (c.args.empty())
                                 return c.name;
                               if (c.name == "List" && c.args.size() == 1) {
                                 return "[" + typeToString(c.args[0]) + "]";
                               }
                               std::string result = c.name;
                               for (const auto& arg : c.args) {
                                 result += " " + typeToString(arg);
                               }
                               return result;
                             },
                             [](const InferTyFun& f) -> std::string {
                               std::string fromStr = typeToString(f.from);
                               if (std::holds_alternative<InferTyFun>(f.from->node)) {
                                 fromStr = "(" + fromStr + ")";
                               }
                               return fromStr + " -> " + typeToString(f.to);
                             },
                             [](const InferTyQual& q) -> std::string {
                               // Map constraints to typeclass names
                               std::string typeclasses = constraintsToTypeclasses(q.constraints);

                               if (typeclasses.empty()) {
                                 return typeToString(q.type);
                               }

                               return typeclasses + " => " + typeToString(q.type);
                             },
                             [](const InferTyForall& f) -> std::string {
                               std::string result = "forall";
                               // Generate clean variable names (a, b, c, ...) for quantified vars
                               std::vector<int> sortedVars(f.quantified.begin(),
                                                           f.quantified.end());
                               std::sort(sortedVars.begin(), sortedVars.end());
                               for (size_t i = 0; i < sortedVars.size(); ++i) {
                                 if (i < 26) {
                                   result += " " + std::string(1, 'a' + i);
                                 } else {
                                   result += " " + std::string(1, 'a' + (i % 26)) +
                                             std::to_string(i / 26);
                                 }
                               }
                               result += ". " + typeToString(f.body);
                               return result;
                             }},
                    displayType->node);
}

InferTypePtr tyInt() {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"Int", {}};
  return ty;
}

InferTypePtr tyBigInt() {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"BigInt", {}};
  return ty;
}

InferTypePtr tyFloat() {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"Float", {}};
  return ty;
}

InferTypePtr tyString() {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"String", {}};
  return ty;
}

InferTypePtr tyBool() {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"Bool", {}};
  return ty;
}

InferTypePtr tyList(InferTypePtr elem) {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyCon{"List", {elem}};
  return ty;
}

InferTypePtr tyFun(InferTypePtr from, InferTypePtr to) {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyFun{from, to};
  return ty;
}

InferTypePtr tyFun(std::vector<InferTypePtr> args, InferTypePtr ret) {
  if (args.empty())
    return ret;
  InferTypePtr result = ret;
  for (auto it = args.rbegin(); it != args.rend(); ++it) {
    result = tyFun(*it, result);
  }
  return result;
}

InferTypePtr tyQual(std::vector<Constraint> constraints, InferTypePtr type) {
  auto ty = std::make_shared<InferType>();
  ty->node = InferTyQual{std::move(constraints), type};
  return ty;
}

// Forward declaration
InferTypePtr fromASTType(const Type& astType);

// Overload for raw pointers (from shared_ptr.get())
InferTypePtr fromASTType(const Type* astType) {
  if (!astType)
    return freshTyVar();
  return fromASTType(*astType);
}

// Main fromASTType function
InferTypePtr fromASTType(const Type& astType) {
  return std::visit(overload{[](const TyVar& v) -> InferTypePtr {
                               // Named type variables become concrete types when parsed
                               // treat as fresh variables
                               return freshTyVar(v.name);
                             },
                             [](const TyCon& c) -> InferTypePtr {
                               std::vector<InferTypePtr> inferArgs;
                               // c.args are already shared_ptr<Type>, convert each
                               for (const auto& arg : c.args) {
                                 // Recursively convert
                                 inferArgs.push_back(fromASTType(arg.get()));
                               }
                               auto ty = std::make_shared<InferType>();
                               ty->node = InferTyCon{c.name, inferArgs};
                               return ty;
                             },
                             [](const TyArr& arr) -> InferTypePtr {
                               return tyFun(fromASTType(arr.from.get()), fromASTType(arr.to.get()));
                             },
                             [](const TyForall& f) -> InferTypePtr {
                               // Explicit forall handling for rank-1 polymorphism
                               // Type variables quantified at top-level let-bindings
                               // Generalization occurs automatically via Hindley-Milner algorithm
                               // This design supports parametric polymorphism without requiring
                               // explicit forall annotations in source code
                               return fromASTType(f.body.get());
                             },
                             [](const auto&) -> InferTypePtr { return freshTyVar(); }},
                    astType.node);
}

// Convert InferType to AST Type (for display/storage)
TypePtr toASTType(const InferTypePtr& inferType) {
  if (!inferType)
    return nullptr;

  return std::visit(overload{[](const InferTyVar& v) -> TypePtr {
                               return std::make_unique<Type>(Type{TyVar{-1, v.name}});
                             },
                             [](const InferTyCon& c) -> TypePtr {
                               return std::make_unique<Type>(Type{TyCon{c.name, {}}});
                             },
                             [](const InferTyFun& f) -> TypePtr {
                               TyArr arr;
                               arr.from = toASTType(f.from);
                               arr.to = toASTType(f.to);
                               return std::make_unique<Type>(Type{std::move(arr)});
                             },
                             [](const InferTyQual& q) -> TypePtr {
                               // ignore constraints in AST conversion
                               return toASTType(q.type);
                             },
                             [](const InferTyForall& f) -> TypePtr {
                               TyForall astForall;
                               // Map IDs to names?
                               astForall.body = toASTType(f.body);
                               return std::make_unique<Type>(Type{std::move(astForall)});
                             }},
                    inferType->node);
}

// Substitution Implementation

Substitution::Substitution(int var, InferTypePtr type) {
  subst_[var] = std::move(type);
}

InferTypePtr Substitution::apply(const InferTypePtr& type) const {
  if (!type)
    return nullptr;

  return std::visit(overload{[&](const InferTyVar& v) -> InferTypePtr {
                               auto it = subst_.find(v.id);
                               if (it != subst_.end())
                                 return it->second;
                               return type;
                             },
                             [&](const InferTyCon& c) -> InferTypePtr {
                               std::vector<InferTypePtr> newArgs;
                               for (const auto& arg : c.args) {
                                 newArgs.push_back(apply(arg));
                               }
                               auto result = std::make_shared<InferType>();
                               result->node = InferTyCon{c.name, newArgs};
                               return result;
                             },
                             [&](const InferTyFun& f) -> InferTypePtr {
                               return tyFun(apply(f.from), apply(f.to));
                             },
                             [&](const InferTyQual& q) -> InferTypePtr {
                               std::vector<Constraint> newConstraints;
                               for (const auto& c : q.constraints) {
                                 newConstraints.push_back({c.name, apply(c.type)});
                               }
                               return tyQual(std::move(newConstraints), apply(q.type));
                             },
                             [&](const InferTyForall& f) -> InferTypePtr {
                               // Avoid capturing bound variables
                               // Naive implementation: just apply to body if vars not in
                               // subst Correct implementation requires alpha-renaming if
                               // collision
                               return std::make_shared<InferType>(
                                   InferType{InferTyForall{f.quantified, apply(f.body)}});
                             }},
                    type->node);
}

Substitution Substitution::compose(const Substitution& other) const {
  std::map<int, InferTypePtr> result = other.subst_;
  for (auto& [var, type] : result) {
    type = apply(type);
  }
  for (const auto& [var, type] : subst_) {
    if (result.find(var) == result.end()) {
      result[var] = type;
    }
  }
  return Substitution{result};
}

std::string Substitution::toString() const {
  if (subst_.empty())
    return "{}";
  std::ostringstream oss;
  oss << "{";
  bool first = true;
  for (const auto& [var, type] : subst_) {
    if (!first)
      oss << ", ";
    first = false;
    oss << "t" << var << " ↦ " << typeToString(type);
  }
  oss << "}";
  return oss.str();
}

// Type Scheme Implementation

InferTypePtr TypeScheme::instantiate() const {
  std::map<int, InferTypePtr> mapping;
  for (int var : quantified) {
    mapping[var] = freshTyVar();
  }
  auto instantiated = Substitution{mapping}.apply(type);

  // Instantiate nested foralls by unwrapping and substituting fresh variables
  // This supports rank-N polymorphism by recursively instantiating quantifiers
  // Ensures each instantiation produces distinct type variables
  if (auto* forall = std::get_if<InferTyForall>(&instantiated->node)) {
    std::map<int, InferTypePtr> innerMapping;
    for (int var : forall->quantified) {
      innerMapping[var] = freshTyVar();
    }
    return Substitution{innerMapping}.apply(forall->body);
  }

  return instantiated;
}

std::set<int> TypeScheme::freeVars() const {
  auto vars = freeTypeVars(type);
  for (int var : quantified) {
    vars.erase(var);
  }
  return vars;
}

// Helper to collect type variables in order of appearance
static void collectVarsInOrder(const InferTypePtr& type,
                               std::vector<int>& vars,
                               std::set<int>& seen) {
  if (!type)
    return;

  std::visit(overload{[&](const InferTyVar& v) {
                        // Use the ID field directly
                        if (seen.find(v.id) == seen.end()) {
                          vars.push_back(v.id);
                          seen.insert(v.id);
                        }
                      },
                      [&](const InferTyCon& c) {
                        for (const auto& arg : c.args) {
                          collectVarsInOrder(arg, vars, seen);
                        }
                      },
                      [&](const InferTyFun& f) {
                        collectVarsInOrder(f.from, vars, seen);
                        collectVarsInOrder(f.to, vars, seen);
                      },
                      [&](const InferTyQual& q) {
                        for (const auto& c : q.constraints) {
                          collectVarsInOrder(c.type, vars, seen);
                        }
                        collectVarsInOrder(q.type, vars, seen);
                      },
                      [&](const InferTyForall& f) { collectVarsInOrder(f.body, vars, seen); }},
             type->node);
}

std::string TypeScheme::toString() const {
  if (quantified.empty())
    return typeToString(type);

  // Collect variables in order of appearance
  std::vector<int> varsInOrder;
  std::set<int> seen;
  collectVarsInOrder(type, varsInOrder, seen);

  // Create canonical renaming: map old IDs to a, b, c, ...
  std::map<int, InferTypePtr> renaming;
  int idx = 0;
  for (int varId : varsInOrder) {
    if (quantified.find(varId) != quantified.end()) {
      std::string newName;
      if (idx < 26) {
        newName = std::string(1, 'a' + idx);
      } else {
        newName = std::string(1, 'a' + (idx % 26)) + std::to_string(idx / 26);
      }
      auto newVar = std::make_shared<InferType>();
      newVar->node = InferTyVar{varId, newName};  // Preserve original ID
      renaming[varId] = newVar;
      idx++;
    }
  }

  // Apply renaming to produce clean type variable names
  InferTypePtr renamedType = Substitution{renaming}.apply(type);

  // Omit explicit forall quantifiers in output for readability
  // Type signatures display as `a -> b -> c` instead of `forall a b c. a -> b -> c`
  // Quantification is implicit at let-bindings via Hindley-Milner generalization
  return typeToString(renamedType);
}

// Type Environment Implementation

TypeScheme TypeEnv::lookup(const std::string& name) const {
  auto it = bindings_.find(name);
  if (it != bindings_.end())
    return it->second;

  std::vector<std::string> candidates;
  for (const auto& [n, _] : bindings_) {
    candidates.push_back(n);
  }

  auto similar = errors::findSimilarNames(name, candidates, 2);
  auto error = SolisError(ErrorCategory::NameError, "Undefined variable: " + name)
                   .setExplanation("'" + name + "' is not defined.");
  if (!similar.empty()) {
    error.addSuggestion("Did you mean '" + similar[0] + "'?");
  }
  throw error;
}

bool TypeEnv::contains(const std::string& name) const {
  return bindings_.find(name) != bindings_.end();
}

void TypeEnv::extend(const std::string& name, const TypeScheme& scheme) {
  bindings_[name] = scheme;
}

void TypeEnv::extend(const std::string& name, const InferTypePtr& type) {
  bindings_[name] = TypeScheme{type};
}

void TypeEnv::remove(const std::string& name) {
  bindings_.erase(name);
}

std::set<int> TypeEnv::freeVars() const {
  std::set<int> result;
  for (const auto& [_, scheme] : bindings_) {
    auto vars = scheme.freeVars();
    result.insert(vars.begin(), vars.end());
  }
  return result;
}

TypeEnv TypeEnv::apply(const Substitution& subst) const {
  std::map<std::string, TypeScheme> newBindings;
  for (const auto& [name, scheme] : bindings_) {
    newBindings[name] = TypeScheme{scheme.quantified, subst.apply(scheme.type)};
  }
  return TypeEnv{newBindings};
}

TypeScheme TypeEnv::generalize(const InferTypePtr& type) const {
  auto typeVars = freeTypeVars(type);
  auto envVars = freeVars();
  std::set<int> quantified;
  for (int var : typeVars) {
    if (envVars.find(var) == envVars.end()) {
      quantified.insert(var);
    }
  }
  return TypeScheme{quantified, type};
}

TypeEnv TypeEnv::builtins() {
  TypeEnv env;

  // IO
  auto printVar = freshTyVar("a");
  env.extend("print",
             TypeScheme{{std::get<InferTyVar>(printVar->node).id}, tyFun(printVar, tyBool())});

  auto showVar = freshTyVar("a");
  env.extend("show",
             TypeScheme{{std::get<InferTyVar>(showVar->node).id}, tyFun(showVar, tyString())});

  // List ops - map, filter, foldl are defined in prelude now!

  // String ops
  env.extend("concat", tyFun(tyString(), tyFun(tyString(), tyString())));
  env.extend("words", tyFun(tyString(), tyList(tyString())));
  env.extend("unwords", tyFun(tyList(tyString()), tyString()));
  env.extend("lines", tyFun(tyString(), tyList(tyString())));
  env.extend("unlines", tyFun(tyList(tyString()), tyString()));

  // Missing string ops
  env.extend("contains", tyFun(tyString(), tyFun(tyString(), tyBool())));
  env.extend("endsWith", tyFun(tyString(), tyFun(tyString(), tyBool())));
  env.extend("startsWith", tyFun(tyString(), tyFun(tyString(), tyBool())));
  env.extend("split", tyFun(tyString(), tyFun(tyString(), tyList(tyString()))));
  env.extend("trim", tyFun(tyString(), tyString()));

  // Constants
  env.extend("true", tyBool());
  env.extend("false", tyBool());

  // List predicates - defined in prelude

  // Math predicates
  env.extend("even", tyFun(tyInt(), tyBool()));
  env.extend("odd", tyFun(tyInt(), tyBool()));
  env.extend("abs", tyFun(tyInt(), tyInt()));
  env.extend("sqrt", tyFun(tyInt(), tyInt()));      // Simplified - actually returns float
  env.extend("truncate", tyFun(tyInt(), tyInt()));  // Simplified
  env.extend("square", tyFun(tyInt(), tyInt()));
  env.extend("gcd", tyFun(tyInt(), tyFun(tyInt(), tyInt())));
  env.extend("lcm", tyFun(tyInt(), tyFun(tyInt(), tyInt())));
  env.extend("max", tyFun(tyInt(), tyFun(tyInt(), tyInt())));
  env.extend("min", tyFun(tyInt(), tyFun(tyInt(), tyInt())));

  // List generation - defined in prelude

  // More list functions - defined in prelude

  // File I/O primitives
  env.extend("readFile", tyFun(tyString(), tyString()));
  env.extend("writeFile", tyFun(tyString(), tyFun(tyString(), tyBool())));
  env.extend("appendFile", tyFun(tyString(), tyFun(tyString(), tyBool())));
  env.extend("fileExists", tyFun(tyString(), tyBool()));
  env.extend("deleteFile", tyFun(tyString(), tyBool()));

  return env;
}

// Helper to report or collect errors
void TypeInference::reportError(SolisError error) {
  if (errorCollector_) {
    errorCollector_->addError(std::move(error));
  } else {
    throw error;
  }
}

// Forward declaration of global unify
Substitution unify(const InferTypePtr& t1, const InferTypePtr& t2);

// TypeInference member: fresh type variable

// Unification

bool occurs(int var, const InferTypePtr& type) {
  return freeTypeVars(type).count(var) > 0;
}

Substitution unify(const InferTypePtr& t1, const InferTypePtr& t2) {
  if (!t1 || !t2)
    return Substitution{};

  // Handle same type variable unification (a ~ a) - always succeeds with empty
  // substitution
  if (auto* v1 = std::get_if<InferTyVar>(&t1->node)) {
    if (auto* v2 = std::get_if<InferTyVar>(&t2->node)) {
      if (v1->id == v2->id) {
        // Same variable - no substitution needed
        return Substitution{};
      }
    }

    // Different types - check for cycles
    if (occurs(v1->id, t2)) {
      throw SolisError(ErrorCategory::TypeError, "Infinite type")
          .setExplanation("Cannot construct: " + typeToString(t1) + " ~ " + typeToString(t2));
    }
    return Substitution{v1->id, t2};
  }

  if (auto* v2 = std::get_if<InferTyVar>(&t2->node)) {
    // t1 is not a TyVar (checked above), so no need to check same-variable case
    if (occurs(v2->id, t1)) {
      throw SolisError(ErrorCategory::TypeError, "Infinite type")
          .setExplanation("Cannot construct: " + typeToString(t2) + " ~ " + typeToString(t1));
    }
    return Substitution{v2->id, t1};
  }

  if (auto* f1 = std::get_if<InferTyFun>(&t1->node)) {
    if (auto* f2 = std::get_if<InferTyFun>(&t2->node)) {
      auto s1 = unify(f1->from, f2->from);
      auto s2 = unify(s1.apply(f1->to), s1.apply(f2->to));
      return s2.compose(s1);
    }
  }

  if (auto* c1 = std::get_if<InferTyCon>(&t1->node)) {
    if (auto* c2 = std::get_if<InferTyCon>(&t2->node)) {
      if (c1->name != c2->name || c1->args.size() != c2->args.size()) {
        throw SolisError(ErrorCategory::TypeError, "Type mismatch")
            .setExplanation("Cannot unify " + typeToString(t1) + " with " + typeToString(t2));
      }
      Substitution subst;
      for (size_t i = 0; i < c1->args.size(); ++i) {
        auto s = unify(subst.apply(c1->args[i]), subst.apply(c2->args[i]));
        subst = s.compose(subst);
      }
      return subst;
    }
  }

  throw SolisError(ErrorCategory::TypeError, "Type mismatch")
      .setExplanation("Cannot unify " + typeToString(t1) + " with " + typeToString(t2));
}

// Type Inference Implementation

InferResult TypeInference::inferLit(const Lit& lit) {
  InferTypePtr type = std::visit(overload{[](int64_t) { return tyInt(); },
                                          [](double) { return tyFloat(); },
                                          [](const std::string&) { return tyString(); },
                                          [](bool) { return tyBool(); },
                                          [](const BigInt&) {
                                            return tyBigInt();
                                          }},  // Distinct BigInt type!
                                 lit.value);
  return InferResult{Substitution{}, type};
}

InferResult TypeInference::inferVar(const Var& var) {
  auto scheme = env_.lookup(var.name);
  auto instantiated = scheme.instantiate();

  std::vector<Constraint> constraints;
  InferTypePtr type = instantiated;

  if (auto* q = std::get_if<InferTyQual>(&instantiated->node)) {
    constraints = q->constraints;
    type = q->type;
  }

  return InferResult{Substitution{}, type, constraints};
}

InferResult TypeInference::inferLambda(const Lambda& lam) {
  TypeEnv newEnv = env_;
  std::vector<InferTypePtr> paramTypes;

  // Handle all parameters
  for (const auto& paramPtr : lam.params) {
    auto paramType = freshTyVar();
    paramTypes.push_back(paramType);

    const Pattern& pattern = *paramPtr;

    if (auto* varPat = std::get_if<VarPat>(&pattern.node)) {
      // Simple variable pattern
      newEnv.extend(varPat->name, paramType);
    } else if (auto* consPat = std::get_if<ConsPat>(&pattern.node)) {
      // Cons pattern: x:xs where consPat->constructor == ":" and args = [x, xs]
      if (consPat->constructor == "::" && consPat->args.size() == 2) {
        // Type must be [a] for some a
        auto elemType = freshTyVar("elem");
        auto listType = tyList(elemType);

        // Unify paramType with [elemType]
        // Direct node assignment works for fresh type variables
        // Full unification with substitution requires additional infrastructure
        paramType->node = listType->node;

        // Bind first arg (head) to elemType
        if (auto* headVar = std::get_if<VarPat>(&consPat->args[0]->node)) {
          newEnv.extend(headVar->name, elemType);
        }

        // Bind second arg (tail) to [elemType]
        if (auto* tailVar = std::get_if<VarPat>(&consPat->args[1]->node)) {
          newEnv.extend(tailVar->name, listType);
        }
      } else {
        // Other constructors not supported in lambda yet
      }
    } else if (std::get_if<WildcardPat>(&pattern.node)) {
      // Wildcard - just ignore
    } else {
      // Complex patterns not yet supported
      // reportError(SolisError(ErrorCategory::TypeError, "Complex patterns not
      // yet supported in lambda"));
    }
  }

  // Infer body in extended environment
  TypeInference bodyInfer{newEnv};
  auto bodyResult = bodyInfer.infer(*lam.body);

  // Construct result type: p1 -> p2 -> ... -> body
  auto resultType = bodyResult.type;

  // Apply substitution to param types and build function type from right to
  // left
  for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it) {
    resultType = tyFun(bodyResult.subst.apply(*it), resultType);
  }

  return InferResult{bodyResult.subst, resultType, bodyResult.constraints};
}

InferResult TypeInference::inferApp(const App& app) {
  auto funcResult = infer(*app.func);
  env_ = env_.apply(funcResult.subst);
  auto argResult = infer(*app.arg);

  auto resultType = freshTyVar();
  auto funcType = argResult.subst.apply(funcResult.type);
  auto expectedType = tyFun(argResult.type, resultType);

  auto unifySubst = unify(funcType, expectedType);
  auto finalSubst = unifySubst.compose(argResult.subst).compose(funcResult.subst);

  // Merge constraints
  std::vector<Constraint> finalConstraints;
  auto s2 = argResult.subst;
  auto s3 = unifySubst;

  for (const auto& c : funcResult.constraints) {
    finalConstraints.push_back({c.name, s3.apply(s2.apply(c.type))});
  }
  for (const auto& c : argResult.constraints) {
    finalConstraints.push_back({c.name, s3.apply(c.type)});
  }

  return InferResult{finalSubst, unifySubst.apply(resultType), finalConstraints};
}

InferResult TypeInference::inferBinOp(const BinOp& op) {
  auto leftResult = infer(*op.left);
  env_ = env_.apply(leftResult.subst);
  auto rightResult = infer(*op.right);

  auto subst = rightResult.subst.compose(leftResult.subst);
  auto leftType = subst.apply(leftResult.type);
  auto rightType = subst.apply(rightResult.type);

  // Merge constraints
  std::vector<Constraint> constraints;
  for (const auto& c : leftResult.constraints) {
    constraints.push_back({c.name, rightResult.subst.apply(c.type)});
  }
  constraints.insert(constraints.end(),
                     rightResult.constraints.begin(),
                     rightResult.constraints.end());

  Substitution unifySubst;
  InferTypePtr resultType;

  if (op.op == "+" || op.op == "-" || op.op == "*" || op.op == "/" || op.op == "%") {
    auto s = unify(leftType, rightType);
    unifySubst = s;
    resultType = s.apply(leftType);

    // Generate constraint: op : T -> T -> T
    auto methodType = tyFun(resultType, tyFun(resultType, resultType));
    constraints.push_back({op.op, methodType});
  } else if (op.op == "==" || op.op == "!=" || op.op == "<" || op.op == ">" || op.op == "<=" ||
             op.op == ">=") {
    auto s = unify(leftType, rightType);
    unifySubst = s;
    resultType = tyBool();

    // Generate constraint: op : T -> T -> Bool
    auto argType = s.apply(leftType);
    auto methodType = tyFun(argType, tyFun(argType, tyBool()));
    constraints.push_back({op.op, methodType});
  } else if (op.op == "++") {
    auto s1 = unify(leftType, tyString());
    auto s2 = unify(s1.apply(rightType), tyString());
    unifySubst = s2.compose(s1);
    resultType = tyString();
  } else if (op.op == "&&" || op.op == "||") {
    auto s1 = unify(leftType, tyBool());
    auto s2 = unify(s1.apply(rightType), tyBool());
    unifySubst = s2.compose(s1);
    resultType = tyBool();
  } else if (op.op == "::" || op.op == ":") {
    auto elemTy = leftType;
    auto listTy = rightType;
    auto s = unify(listTy, tyList(elemTy));
    unifySubst = s;
    resultType = s.apply(listTy);
  } else {
    reportError(SolisError(ErrorCategory::TypeError, "Unknown operator: " + op.op));
    return InferResult{Substitution{}, freshTyVar()};
  }

  auto finalSubst = unifySubst.compose(subst);

  // Apply unification subst to constraints
  std::vector<Constraint> finalConstraints;
  for (const auto& c : constraints) {
    finalConstraints.push_back({c.name, unifySubst.apply(c.type)});
  }

  return InferResult{finalSubst, resultType, finalConstraints};
}

InferResult TypeInference::inferIf(const If& ifExpr) {
  auto condResult = infer(*ifExpr.cond);
  auto s1 = unify(condResult.type, tyBool());
  auto subst = s1.compose(condResult.subst);

  env_ = env_.apply(subst);
  auto thenResult = infer(*ifExpr.thenBranch);
  subst = thenResult.subst.compose(subst);

  env_ = env_.apply(thenResult.subst);
  auto elseResult = infer(*ifExpr.elseBranch);

  auto s2 = unify(thenResult.type, elseResult.type);
  auto finalSubst = s2.compose(elseResult.subst).compose(subst);

  // Merge constraints
  std::vector<Constraint> constraints;

  auto s_cond = s2.compose(elseResult.subst).compose(thenResult.subst).compose(s1);
  auto s_then = s2.compose(elseResult.subst);
  auto s_else = s2;

  for (const auto& c : condResult.constraints)
    constraints.push_back({c.name, s_cond.apply(c.type)});
  for (const auto& c : thenResult.constraints)
    constraints.push_back({c.name, s_then.apply(c.type)});
  for (const auto& c : elseResult.constraints)
    constraints.push_back({c.name, s_else.apply(c.type)});

  return InferResult{finalSubst, s2.apply(elseResult.type), constraints};
}

// Helper to generalize type with constraints
static TypeScheme generalizeWithConstraints(const TypeEnv& env,
                                            InferTypePtr type,
                                            const std::vector<Constraint>& constraints) {
  auto envVars = env.freeVars();
  auto typeVars = freeTypeVars(type);
  for (const auto& c : constraints) {
    auto cv = freeTypeVars(c.type);
    typeVars.insert(cv.begin(), cv.end());
  }

  std::set<int> quantified;
  std::set_difference(typeVars.begin(),
                      typeVars.end(),
                      envVars.begin(),
                      envVars.end(),
                      std::inserter(quantified, quantified.begin()));

  std::vector<Constraint> c_gen;
  for (const auto& c : constraints) {
    auto cv = freeTypeVars(c.type);
    bool dependsOnEnv = false;
    for (int v : cv) {
      if (envVars.count(v)) {
        dependsOnEnv = true;
        break;
      }
    }
    if (!dependsOnEnv) {
      c_gen.push_back(c);
    }
  }

  InferTypePtr schemeType = type;
  if (!c_gen.empty()) {
    schemeType = tyQual(c_gen, schemeType);
  }
  return TypeScheme(quantified, schemeType);
}

static std::vector<Constraint> extractOuterConstraints(const TypeEnv& env,
                                                       const std::vector<Constraint>& constraints) {
  std::vector<Constraint> outer;
  auto envVars = env.freeVars();
  for (const auto& c : constraints) {
    auto cv = freeTypeVars(c.type);
    bool dependsOnEnv = false;
    for (int v : cv) {
      if (envVars.count(v)) {
        dependsOnEnv = true;
        break;
      }
    }
    if (dependsOnEnv) {
      outer.push_back(c);
    }
  }
  return outer;
}

InferResult TypeInference::inferLet(const Let& let) {
  Substitution totalSubst;
  TypeEnv currentEnv = env_;
  TypeEnv extendedEnv = currentEnv;
  const Pattern& pattern = *let.pattern;

  std::vector<Constraint> outerConstraints;

  if (auto* varPat = std::get_if<VarPat>(&pattern.node)) {
    std::string name = varPat->name;
    bool isRecursive = let.isRecursive;
    if (!isRecursive && std::get_if<Lambda>(&let.value->node)) {
      isRecursive = true;
    }

    if (isRecursive) {
      auto assumedType = freshTyVar("rec_" + name);
      extendedEnv.extend(name, assumedType);

      TypeInference recursiveInfer{extendedEnv};
      auto recValueResult = recursiveInfer.infer(*let.value);

      auto unifySubst = unify(assumedType, recValueResult.type);
      auto finalValueType = unifySubst.apply(recValueResult.type);
      auto finalSubst = unifySubst.compose(recValueResult.subst);

      extendedEnv = extendedEnv.apply(finalSubst);

      std::vector<Constraint> constraints;
      for (const auto& c : recValueResult.constraints) {
        constraints.push_back({c.name, finalSubst.apply(c.type)});
      }

      auto scheme = generalizeWithConstraints(currentEnv.apply(finalSubst),
                                              finalValueType,
                                              constraints);
      extendedEnv.extend(name, scheme);
      auto outer = extractOuterConstraints(currentEnv.apply(finalSubst), constraints);
      outerConstraints.insert(outerConstraints.end(), outer.begin(), outer.end());

      totalSubst = finalSubst;
    } else {
      auto valueResult = infer(*let.value);
      totalSubst = valueResult.subst;
      currentEnv = currentEnv.apply(totalSubst);
      extendedEnv = currentEnv;

      auto scheme = generalizeWithConstraints(currentEnv,
                                              valueResult.type,
                                              valueResult.constraints);
      extendedEnv.extend(name, scheme);
      auto outer = extractOuterConstraints(currentEnv, valueResult.constraints);
      outerConstraints.insert(outerConstraints.end(), outer.begin(), outer.end());
    }
  } else if (auto* consPat = std::get_if<ConsPat>(&pattern.node)) {
    auto valueResult = infer(*let.value);
    totalSubst = valueResult.subst;
    currentEnv = currentEnv.apply(totalSubst);
    extendedEnv = currentEnv;

    if (consPat->constructor == "::" && consPat->args.size() == 2) {
      auto elemType = freshTyVar("elem");
      auto listType = tyList(elemType);
      auto s = unify(valueResult.type, listType);
      elemType = s.apply(elemType);

      totalSubst = s.compose(totalSubst);
      extendedEnv = extendedEnv.apply(s);

      std::vector<Constraint> constraints;
      for (const auto& c : valueResult.constraints) {
        constraints.push_back({c.name, s.apply(c.type)});
      }

      if (auto* headVar = std::get_if<VarPat>(&consPat->args[0]->node)) {
        extendedEnv.extend(headVar->name,
                           generalizeWithConstraints(currentEnv, elemType, constraints));
      }
      if (auto* tailVar = std::get_if<VarPat>(&consPat->args[1]->node)) {
        extendedEnv.extend(tailVar->name,
                           generalizeWithConstraints(currentEnv, tyList(elemType), constraints));
      }
      auto outer = extractOuterConstraints(currentEnv, constraints);
      outerConstraints.insert(outerConstraints.end(), outer.begin(), outer.end());
    } else {
      reportError(SolisError(ErrorCategory::TypeError, "Unsupported constructor pattern in let"));
    }
  } else {
    auto valueResult = infer(*let.value);
    totalSubst = valueResult.subst;
    currentEnv = currentEnv.apply(totalSubst);
    extendedEnv = currentEnv;
    outerConstraints.insert(outerConstraints.end(),
                            valueResult.constraints.begin(),
                            valueResult.constraints.end());
  }

  TypeInference bodyInfer{extendedEnv};
  auto bodyResult = bodyInfer.infer(*let.body);

  auto finalSubst = bodyResult.subst.compose(totalSubst);

  std::vector<Constraint> finalConstraints;
  for (const auto& c : outerConstraints) {
    finalConstraints.push_back({c.name, bodyResult.subst.apply(c.type)});
  }
  finalConstraints.insert(finalConstraints.end(),
                          bodyResult.constraints.begin(),
                          bodyResult.constraints.end());

  return InferResult{finalSubst, bodyResult.type, finalConstraints};
}

InferResult TypeInference::inferList(const List& list) {
  if (list.elements.empty()) {
    return InferResult{Substitution{}, tyList(freshTyVar())};
  }

  auto firstResult = infer(*list.elements[0]);
  auto elemType = firstResult.type;
  auto subst = firstResult.subst;
  std::vector<Constraint> constraints = firstResult.constraints;

  for (size_t i = 1; i < list.elements.size(); ++i) {
    env_ = env_.apply(subst);
    auto elemResult = infer(*list.elements[i]);

    for (auto& c : constraints) {
      c.type = elemResult.subst.apply(c.type);
    }
    constraints.insert(constraints.end(),
                       elemResult.constraints.begin(),
                       elemResult.constraints.end());

    auto s = unify(subst.apply(elemType), elemResult.type);
    subst = s.compose(elemResult.subst).compose(subst);
    elemType = s.apply(elemType);

    for (auto& c : constraints) {
      c.type = s.apply(c.type);
    }
  }

  return InferResult{subst, tyList(elemType), constraints};
}

InferResult TypeInference::inferMatch(const Match& match) {
  auto scrutineeResult = infer(*match.scrutinee);
  auto scrutineeType = scrutineeResult.type;
  auto totalSubst = scrutineeResult.subst;
  std::vector<Constraint> constraints = scrutineeResult.constraints;

  auto resultType = freshTyVar("match_result");

  for (const auto& arm : match.arms) {
    const auto& pattern = *arm.first;
    const auto& expr = *arm.second;

    TypeEnv armEnv = env_.apply(totalSubst);

    InferTypePtr patType;
    if (auto* varPat = std::get_if<VarPat>(&pattern.node)) {
      patType = freshTyVar();
      armEnv.extend(varPat->name, patType);
    } else if (auto* litPat = std::get_if<LitPat>(&pattern.node)) {
      auto typeArg = std::visit(overload{[](int64_t) { return tyInt(); },
                                         [](double) { return tyFloat(); },
                                         [](const std::string&) { return tyString(); },
                                         [](bool) { return tyBool(); },
                                         [](const BigInt&) {
                                           return tyBigInt();
                                         }},  // Distinct type
                                litPat->value);
      patType = typeArg;
    } else if (std::get_if<WildcardPat>(&pattern.node)) {
      patType = freshTyVar();
    } else if (auto* listPat = std::get_if<ListPat>(&pattern.node)) {
      if (listPat->elements.empty()) {
        patType = tyList(freshTyVar());
      } else {
        patType = tyList(freshTyVar());
      }
    } else if (auto* consPat = std::get_if<ConsPat>(&pattern.node)) {
      if (consPat->constructor == "::" && consPat->args.size() == 2) {
        auto elemType = freshTyVar();
        patType = tyList(elemType);

        if (auto* headVar = std::get_if<VarPat>(&consPat->args[0]->node)) {
          armEnv.extend(headVar->name, elemType);
        }
        if (auto* tailVar = std::get_if<VarPat>(&consPat->args[1]->node)) {
          armEnv.extend(tailVar->name, patType);
        }
      } else {
        patType = freshTyVar();
        // Bind arguments to fresh type variables
        for (const auto& argPat : consPat->args) {
          if (auto* varPat = std::get_if<VarPat>(&argPat->node)) {
            armEnv.extend(varPat->name, freshTyVar());
          }
        }
      }
    } else {
      patType = freshTyVar();
    }

    auto s1 = unify(totalSubst.apply(scrutineeType), patType);
    totalSubst = s1.compose(totalSubst);

    for (auto& c : constraints) {
      c.type = s1.apply(c.type);
    }

    TypeInference armInfer{armEnv.apply(totalSubst)};
    auto armResult = armInfer.infer(expr);

    totalSubst = armResult.subst.compose(totalSubst);

    for (auto& c : constraints) {
      c.type = armResult.subst.apply(c.type);
    }
    constraints.insert(constraints.end(),
                       armResult.constraints.begin(),
                       armResult.constraints.end());

    auto s2 = unify(totalSubst.apply(resultType), armResult.type);
    totalSubst = s2.compose(totalSubst);

    for (auto& c : constraints) {
      c.type = s2.apply(c.type);
    }
  }

  return InferResult{totalSubst, totalSubst.apply(resultType), constraints};
}

InferResult TypeInference::inferBlock(const Block& block) {
  Substitution subst;
  InferTypePtr type = tyBool();
  std::vector<Constraint> constraints;

  for (const auto& stmt : block.stmts) {
    env_ = env_.apply(subst);
    auto result = infer(*stmt);

    for (auto& c : constraints) {
      c.type = result.subst.apply(c.type);
    }
    constraints.insert(constraints.end(), result.constraints.begin(), result.constraints.end());

    subst = result.subst.compose(subst);
    type = result.type;
  }

  return InferResult{subst, type, constraints};
}

InferResult TypeInference::inferRecord(const Record&) {
  return InferResult{Substitution{}, freshTyVar()};
}

InferResult TypeInference::inferRecordAccess(const RecordAccess&) {
  return InferResult{Substitution{}, freshTyVar()};
}

InferResult TypeInference::infer(const Expr& expr) {
  return std::visit(overload{[&](const Lit& lit) { return inferLit(lit); },
                             [&](const Var& var) { return inferVar(var); },
                             [&](const Lambda& lam) { return inferLambda(lam); },
                             [&](const App& app) { return inferApp(app); },
                             [&](const BinOp& op) { return inferBinOp(op); },
                             [&](const If& ifExpr) { return inferIf(ifExpr); },
                             [&](const Let& let) { return inferLet(let); },
                             [&](const List& list) { return inferList(list); },
                             [&](const Match& match) { return inferMatch(match); },
                             [&](const Block& block) { return inferBlock(block); },
                             [&](const Record& rec) { return inferRecord(rec); },
                             [&](const RecordAccess& access) { return inferRecordAccess(access); },
                             [&](const auto&) -> InferResult {
                               return InferResult{Substitution{}, freshTyVar()};
                             }},
                    expr.node);
}

InferResult TypeInference::inferDecl(const Decl& decl) {
  if (auto* funcDecl = std::get_if<FunctionDecl>(&decl.node)) {
    // RECURSIVE FUNCTION SUPPORT
    // Pre-bind function name with fresh type variable
    auto assumedType = freshTyVar("rec_" + funcDecl->name);
    env_.extend(funcDecl->name, assumedType);

    // Handle function parameters
    TypeEnv newEnv = env_;
    std::vector<InferTypePtr> paramTypes;

    // Also bind function name in parameter environment for recursive calls
    newEnv.extend(funcDecl->name, assumedType);

    for (const auto& param : funcDecl->params) {
      std::string paramName;
      if (auto* varPat = std::get_if<VarPat>(&param->node)) {
        paramName = varPat->name;
      } else {
        paramName = "_";
      }

      auto paramType = freshTyVar();
      paramTypes.push_back(paramType);
      if (paramName != "_") {
        newEnv.extend(paramName, paramType);
      }
    }

    // Infer body in environment with self-reference
    TypeInference bodyInfer{newEnv};
    auto bodyResult = bodyInfer.infer(*funcDecl->body);

    // Construct the full function type: p1 -> p2 -> ... -> body
    auto fullType = bodyResult.type;
    for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it) {
      // Apply substitution to parameter types as we go back up
      fullType = tyFun(bodyResult.subst.apply(*it), fullType);
    }

    auto scheme = generalizeWithConstraints(env_, fullType, bodyResult.constraints);
    env_.extend(funcDecl->name, scheme);

    auto outer = extractOuterConstraints(env_, bodyResult.constraints);
    return InferResult{bodyResult.subst, fullType, outer};
  } else if (auto* typeDecl = std::get_if<TypeDecl>(&decl.node)) {
    // ADT constructor registration with proper function types
    if (auto* adt = std::get_if<std::vector<std::pair<std::string, std::vector<TypePtr>>>>(
            &typeDecl->rhs)) {
      // Create result type for the ADT
      auto resultType = std::make_shared<InferType>();
      resultType->node = InferTyCon{typeDecl->name, {}};

      for (const auto& [ctorName, args] : *adt) {
        if (args.empty()) {
          // Nullary constructor: direct value of result type
          env_.extend(ctorName, resultType);
        } else {
          // Multi-argument constructor: build curried function type
          // Constructor type: Arg1 -> Arg2 -> ... -> ResultType
          std::vector<InferTypePtr> argTypes;
          for (const auto& argType : args) {
            argTypes.push_back(fromASTType(argType.get()));
          }
          auto ctorType = tyFun(argTypes, resultType);
          env_.extend(ctorName, ctorType);
        }
      }
    }
    return InferResult{Substitution{}, freshTyVar()};
  }

  return InferResult{Substitution{}, freshTyVar()};
}

}  // namespace solis
