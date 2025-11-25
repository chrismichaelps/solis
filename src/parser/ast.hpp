// Solis Programming Language - AST Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "runtime/bigint.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace solis {

// Source code location tracking for LSP features
struct SourceLocation {
  int line;       // 1-indexed
  int column;     // 1-indexed
  int endLine;    // 1-indexed
  int endColumn;  // 1-indexed

  SourceLocation()
      : line(0)
      , column(0)
      , endLine(0)
      , endColumn(0) {}
  SourceLocation(int l, int c, int el, int ec)
      : line(l)
      , column(c)
      , endLine(el)
      , endColumn(ec) {}
};

// Forward declarations
struct Expr;
struct Type;
struct Pattern;
struct Decl;

using ExprPtr = std::unique_ptr<Expr>;
using TypePtr = std::unique_ptr<Type>;
using PatternPtr = std::unique_ptr<Pattern>;
using DeclPtr = std::unique_ptr<Decl>;

// Expressions
struct Var {
  std::string name;
};

struct Lit {
  std::variant<int64_t, double, std::string, bool, BigInt> value;
};

struct Lambda {
  std::vector<PatternPtr> params;
  ExprPtr body;
};

struct App {
  ExprPtr func;
  ExprPtr arg;
};

struct Let {
  bool isRecursive = false;  // For recursive let-binding support
  PatternPtr pattern;
  ExprPtr value;
  ExprPtr body;
};

struct Match {
  ExprPtr scrutinee;
  std::vector<std::pair<PatternPtr, ExprPtr>> arms;
};

struct If {
  ExprPtr cond;
  ExprPtr thenBranch;
  ExprPtr elseBranch;
};

struct BinOp {
  std::string op;
  ExprPtr left;
  ExprPtr right;
};

struct List {
  std::vector<ExprPtr> elements;
};

struct Record {
  std::map<std::string, ExprPtr> fields;
};

struct RecordAccess {
  ExprPtr record;
  std::string field;
};

struct RecordUpdate {
  ExprPtr record;
  std::map<std::string, ExprPtr> updates;
};

struct Block {
  std::vector<ExprPtr> stmts;
  bool isDoBlock = false;  // Track if this block was preceded by 'do' keyword
};

struct Strict {
  ExprPtr expr;
};

// Monadic bind: x <- action
struct Bind {
  PatternPtr pattern;  // Pattern to match result against
  ExprPtr value;       // Expression to evaluate (the action)
  ExprPtr body;        // Rest of the computation
};

struct Expr {
  std::variant<Var,
               Lit,
               Lambda,
               App,
               Let,
               Match,
               If,
               BinOp,
               List,
               Record,
               RecordAccess,
               RecordUpdate,
               Block,
               Strict,
               Bind>
      node;
};

// Patterns
struct VarPat {
  std::string name;
};

struct LitPat {
  std::variant<int64_t, double, std::string, bool, BigInt> value;
};

struct ConsPat {
  std::string constructor;
  std::vector<PatternPtr> args;
};

struct ListPat {
  std::vector<PatternPtr> elements;
};

struct RecordPat {
  std::map<std::string, PatternPtr> fields;
};

struct WildcardPat {};

struct Pattern {
  std::variant<VarPat, LitPat, ConsPat, ListPat, RecordPat, WildcardPat> node;
};

// Types
struct TyVar {
  int id = -1;  // Unique ID for unification (-1 = named variable)
  std::string name;
};


struct TyCon {
  std::string name;
  std::vector<std::shared_ptr<Type>> args;  // For List a, Map k v, etc.
};


struct TyApp {
  TypePtr func;
  TypePtr arg;
};

struct TyArr {
  TypePtr from;
  TypePtr to;
};

struct TyEffect {
  std::vector<std::string> effects;
  TypePtr resultType;
};

struct TyRecord {
  std::map<std::string, TypePtr> fields;
  std::optional<std::string> rowVar;
};

struct TyForall {
  std::vector<std::string> vars;
  TypePtr body;
};

struct TyExists {
  std::vector<std::string> vars;
  TypePtr body;
};

struct Type {
  std::variant<TyVar, TyCon, TyApp, TyArr, TyEffect, TyRecord, TyForall, TyExists> node;
};

// Declarations
struct FunctionDecl {
  std::string name;
  std::optional<TypePtr> typeAnnotation;
  std::vector<PatternPtr> params;
  ExprPtr body;
  std::optional<SourceLocation> location;  // For LSP go-to-definition
};

struct TypeDecl {
  std::string name;
  std::vector<std::string> params;
  std::variant<std::vector<std::pair<std::string, std::vector<TypePtr>>>,  // ADT
                                                                           // constructors
               std::map<std::string, TypePtr>,                             // Record fields
               TypePtr                                                     // Type alias
               >
      rhs;
};

struct ModuleDecl {
  std::string name;
  std::vector<std::string> exports;
};

struct ImportDecl {
  std::string moduleName;
  bool qualified;
  std::optional<std::string> alias;
  std::vector<std::string> imports;  // Selective imports: import Foo (bar, baz)
  std::vector<std::string> hiding;   // Hiding imports: import Foo hiding (bar)
};

struct TraitDecl {
  std::string name;
  std::vector<std::string> typeParams;
  std::vector<std::pair<std::string, TypePtr>> methods;  // method name -> type signature
};

struct ImplDecl {
  std::optional<std::string> traitName;
  TypePtr type;
  std::vector<FunctionDecl> methods;
};

struct Decl {
  std::variant<FunctionDecl, TypeDecl, ModuleDecl, ImportDecl, TraitDecl, ImplDecl> node;
};

// Module (top-level)
struct Module {
  std::optional<ModuleDecl> moduleDecl;
  std::vector<ImportDecl> imports;
  std::vector<DeclPtr> declarations;
};

// Pretty printing
std::string prettyPrint(const Expr& expr);
std::string prettyPrint(const Pattern& pat);
std::string prettyPrint(const Type& type);
std::string prettyPrint(const Decl& decl);
std::string prettyPrint(const Module& mod);

}  // namespace solis
