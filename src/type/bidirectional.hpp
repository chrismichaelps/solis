// Solis Programming Language - Bidirectional Type Checking
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "parser/ast.hpp"
#include "type/typer.hpp"

#include <optional>

namespace solis {

// Forward declarations for global type constructors
InferTypePtr tyInt();
InferTypePtr tyBigInt();  // BigInt type constructor
InferTypePtr tyFloat();

// Inference Mode
enum class InferMode {
  Synthesize,  // ⇒ infer type from expression
  Check        // ⇐ check against expected type
};

// Context for bidirectional inference
struct BiDirContext {
  InferMode mode;
  std::optional<InferTypePtr> expectedType;

  BiDirContext()
      : mode(InferMode::Synthesize)
      , expectedType(std::nullopt) {}
  BiDirContext(InferMode m)
      : mode(m)
      , expectedType(std::nullopt) {}
  BiDirContext(InferMode m, InferTypePtr exp)
      : mode(m)
      , expectedType(std::move(exp)) {}
};

// Bidirectional Type Inference Engine
class BiDirectionalInference {
private:
  TypeInference& underlying_;  // Delegate to existing Damas-Milner
  bool enabled_;

  // Core bidirectional inference
  InferResult synthesize(const Expr& expr);
  InferResult check(const Expr& expr, const InferTypePtr& expected);

  // Per-expression synthesis
  InferResult synthLit(const Expr& expr);
  InferResult synthVar(const Expr& expr);
  InferResult synthLambda(const Expr& expr);
  InferResult synthApp(const App& app);
  InferResult synthIf(const Expr& expr);
  InferResult synthLet(const Expr& expr);
  InferResult synthList(const Expr& expr);
  InferResult synthMatch(const Expr& expr);
  InferResult synthBlock(const Expr& expr);
  InferResult synthBinOp(const Expr& expr);

  // Per-expression checking
  InferResult checkLambda(const Lambda& lam, const InferTypePtr& expected);
  InferResult checkApp(const App& app, const InferTypePtr& expected);
  InferResult checkIf(const If& ifExpr, const InferTypePtr& expected);

  // Subsumption checking (τ' ≤ τ)
  bool subsumes(const InferTypePtr& inferred, const InferTypePtr& expected);

public:
  BiDirectionalInference(TypeInference& underlying, bool enabled = true)
      : underlying_(underlying)
      , enabled_(enabled) {}

  // Main entry point
  InferResult inferWithContext(const Expr& expr, const BiDirContext& ctx);

  // Public API (compatible with TypeInference)
  InferResult infer(const Expr& expr) {
    if (!enabled_) {
      return underlying_.infer(expr);
    }
    return synthesize(expr);
  }

  InferResult inferWithExpected(const Expr& expr, const InferTypePtr& expected) {
    if (!enabled_) {
      return underlying_.infer(expr);
    }
    return check(expr, expected);
  }

  void setEnabled(bool enabled) { enabled_ = enabled; }
  bool isEnabled() const { return enabled_; }

  TypeEnv& env() { return underlying_.env(); }
  const TypeEnv& env() const { return underlying_.env(); }
};

}  // namespace solis
