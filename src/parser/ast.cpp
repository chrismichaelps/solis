// Solis Programming Language - Abstract Syntax Tree
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "parser/ast.hpp"

#include <sstream>

namespace solis {

// Helper to visit variants
template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

std::string prettyPrint(const Expr& expr) {
  return std::visit(
      overload{
          [](const Var& v) { return v.name; },
          [](const Lit& l) -> std::string {
            return std::visit(overload{[](int64_t i) { return std::to_string(i); },
                                       [](double d) { return std::to_string(d); },
                                       [](const std::string& s) { return "\"" + s + "\""; },
                                       [](bool b) -> std::string { return b ? "true" : "false"; },
                                       [](const BigInt& bi) { return bi.toString() + "n"; }},
                              l.value);
          },
          [](const Lambda& lam) -> std::string {
            std::ostringstream oss;
            oss << "\\";
            for (size_t i = 0; i < lam.params.size(); ++i) {
              if (i > 0)
                oss << " ";
              oss << prettyPrint(*lam.params[i]);
            }
            oss << " -> " << prettyPrint(*lam.body);
            return oss.str();
          },
          [](const App& app) {
            return "(" + prettyPrint(*app.func) + " " + prettyPrint(*app.arg) + ")";
          },
          [](const Let& let) {
            return "let " + prettyPrint(*let.pattern) + " = " + prettyPrint(*let.value) + "; " +
                   prettyPrint(*let.body);
          },
          [](const Match& m) -> std::string {
            std::ostringstream oss;
            oss << "match " << prettyPrint(*m.scrutinee) << " { ";
            for (size_t i = 0; i < m.arms.size(); ++i) {
              if (i > 0)
                oss << ", ";
              oss << prettyPrint(*m.arms[i].first) << " => " << prettyPrint(*m.arms[i].second);
            }
            oss << " }";
            return oss.str();
          },
          [](const If& ifExpr) {
            return "if " + prettyPrint(*ifExpr.cond) + " { " + prettyPrint(*ifExpr.thenBranch) +
                   " } else { " + prettyPrint(*ifExpr.elseBranch) + " }";
          },
          [](const BinOp& op) {
            return "(" + prettyPrint(*op.left) + " " + op.op + " " + prettyPrint(*op.right) + ")";
          },
          [](const List& list) -> std::string {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < list.elements.size(); ++i) {
              if (i > 0)
                oss << ", ";
              oss << prettyPrint(*list.elements[i]);
            }
            oss << "]";
            return oss.str();
          },
          [](const Record& rec) -> std::string {
            std::ostringstream oss;
            oss << "{ ";
            bool first = true;
            for (const auto& [name, expr] : rec.fields) {
              if (!first)
                oss << ", ";
              first = false;
              oss << name << ": " << prettyPrint(*expr);
            }
            oss << " }";
            return oss.str();
          },
          [](const RecordAccess& acc) { return prettyPrint(*acc.record) + "." + acc.field; },
          [](const RecordUpdate& upd) -> std::string {
            std::ostringstream oss;
            oss << "{ " << prettyPrint(*upd.record) << " | ";
            bool first = true;
            for (const auto& [name, expr] : upd.updates) {
              if (!first)
                oss << ", ";
              first = false;
              oss << name << " = " << prettyPrint(*expr);
            }
            oss << " }";
            return oss.str();
          },
          [](const Bind& b) { return prettyPrint(*b.pattern) + " <- " + prettyPrint(*b.value); },
          [](const Block& block) -> std::string {
            std::ostringstream oss;
            oss << "{ ";
            for (size_t i = 0; i < block.stmts.size(); ++i) {
              if (i > 0)
                oss << "; ";
              oss << prettyPrint(*block.stmts[i]);
            }
            oss << " }";
            return oss.str();
          },
          [](const Strict& s) { return "!" + prettyPrint(*s.expr); }},
      expr.node);
}

std::string prettyPrint(const Pattern& pat) {
  return std::visit(
      overload{[](const VarPat& v) { return v.name; },
               [](const LitPat& l) -> std::string {
                 return std::visit(
                     overload{[](int64_t i) -> std::string { return std::to_string(i); },
                              [](double d) -> std::string { return std::to_string(d); },
                              [](const std::string& s) -> std::string { return "\"" + s + "\""; },
                              [](bool b) -> std::string { return b ? "true" : "false"; },
                              [](const BigInt& bi) { return bi.toString() + "n"; }},
                     l.value);
               },
               [](const ConsPat& c) -> std::string {
                 std::ostringstream oss;
                 oss << c.constructor;
                 for (const auto& arg : c.args) {
                   oss << " " << prettyPrint(*arg);
                 }
                 return oss.str();
               },
               [](const ListPat& list) -> std::string {
                 std::ostringstream oss;
                 oss << "[";
                 for (size_t i = 0; i < list.elements.size(); ++i) {
                   if (i > 0)
                     oss << ", ";
                   oss << prettyPrint(*list.elements[i]);
                 }
                 oss << "]";
                 return oss.str();
               },
               [](const RecordPat& rec) -> std::string {
                 std::ostringstream oss;
                 oss << "{ ";
                 bool first = true;
                 for (const auto& [name, pat] : rec.fields) {
                   if (!first)
                     oss << ", ";
                   first = false;
                   oss << name << ": " << prettyPrint(*pat);
                 }
                 oss << " }";
                 return oss.str();
               },
               [](const WildcardPat&) { return std::string("_"); }},
      pat.node);
}

std::string prettyPrint(const Type& type) {
  return std::visit(overload{[](const TyVar& v) { return v.name; },
                             [](const TyCon& c) { return c.name; },
                             [](const TyApp& app) {
                               return "(" + prettyPrint(*app.func) + " " + prettyPrint(*app.arg) +
                                      ")";
                             },
                             [](const TyArr& arr) {
                               return prettyPrint(*arr.from) + " -> " + prettyPrint(*arr.to);
                             },
                             [](const TyEffect& eff) -> std::string {
                               std::ostringstream oss;
                               oss << "Eff [";
                               for (size_t i = 0; i < eff.effects.size(); ++i) {
                                 if (i > 0)
                                   oss << ", ";
                                 oss << eff.effects[i];
                               }
                               oss << "] " << prettyPrint(*eff.resultType);
                               return oss.str();
                             },
                             [](const TyRecord& rec) -> std::string {
                               std::ostringstream oss;
                               oss << "{ ";
                               bool first = true;
                               for (const auto& [name, type] : rec.fields) {
                                 if (!first)
                                   oss << ", ";
                                 first = false;
                                 oss << name << ": " << prettyPrint(*type);
                               }
                               if (rec.rowVar) {
                                 oss << " | " << *rec.rowVar;
                               }
                               oss << " }";
                               return oss.str();
                             },
                             [](const TyForall& fa) -> std::string {
                               std::ostringstream oss;
                               oss << "forall ";
                               for (size_t i = 0; i < fa.vars.size(); ++i) {
                                 if (i > 0)
                                   oss << " ";
                                 oss << fa.vars[i];
                               }
                               oss << ". " << prettyPrint(*fa.body);
                               return oss.str();
                             },
                             [](const TyExists& ex) -> std::string {
                               std::ostringstream oss;
                               oss << "exists ";
                               for (size_t i = 0; i < ex.vars.size(); ++i) {
                                 if (i > 0)
                                   oss << " ";
                                 oss << ex.vars[i];
                               }
                               oss << ". " << prettyPrint(*ex.body);
                               return oss.str();
                             }},
                    type.node);
}

std::string prettyPrint(const Decl& decl) {
  return std::visit(overload{[](const FunctionDecl& func) -> std::string {
                               std::ostringstream oss;
                               if (func.typeAnnotation) {
                                 oss << func.name << " :: " << prettyPrint(**func.typeAnnotation)
                                     << "\n";
                               }
                               oss << "let " << func.name;
                               for (const auto& param : func.params) {
                                 oss << " " << prettyPrint(*param);
                               }
                               oss << " = " << prettyPrint(*func.body);
                               return oss.str();
                             },
                             [](const TypeDecl& type) -> std::string {
                               std::ostringstream oss;
                               oss << "type " << type.name;
                               for (const auto& param : type.params) {
                                 oss << " " << param;
                               }
                               oss << " = ...";  // Simplified
                               return oss.str();
                             },
                             [](const ModuleDecl& mod) { return "module " + mod.name + " where"; },
                             [](const ImportDecl& imp) { return "import " + imp.moduleName; },
                             [](const TraitDecl& trait) -> std::string {
                               std::ostringstream oss;
                               oss << "trait " << trait.name;
                               for (const auto& param : trait.typeParams) {
                                 oss << " " << param;
                               }
                               oss << " where ...";
                               return oss.str();
                             },
                             [](const ImplDecl& impl) -> std::string {
                               std::ostringstream oss;
                               oss << "impl ";
                               if (impl.traitName) {
                                 oss << *impl.traitName << " ";
                               }
                               oss << "...";
                               return oss.str();
                             }},
                    decl.node);
}

std::string prettyPrint(const Module& mod) {
  std::ostringstream oss;

  if (mod.moduleDecl) {
    const ModuleDecl& moduleDeclRef = *mod.moduleDecl;
    oss << "module " << moduleDeclRef.name << " where\n\n";
  }

  for (const auto& imp : mod.imports) {
    oss << "import " << imp.moduleName << "\n";
  }

  if (!mod.imports.empty()) {
    oss << "\n";
  }

  for (const auto& decl : mod.declarations) {
    oss << prettyPrint(*decl) << "\n\n";
  }

  return oss.str();
}

}  // namespace solis
