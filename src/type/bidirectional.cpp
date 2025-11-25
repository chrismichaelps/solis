// Solis Programming Language - Bidirectional Type Checking Implementation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "type/bidirectional.hpp"

namespace solis {

// Helper for std::visit
template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

// Forward declarations
extern InferTypePtr freshTyVar(const std::string& hint);
extern Substitution unify(const InferTypePtr& t1, const InferTypePtr& t2);

InferResult BiDirectionalInference::inferWithContext(const Expr& expr, const BiDirContext& ctx) {
  if (ctx.mode == InferMode::Check && ctx.expectedType) {
    return check(expr, *ctx.expectedType);
  }
  return synthesize(expr);
}

InferResult BiDirectionalInference::synthesize(const Expr& expr) {
  return std::visit(overload{[&](const Lit&) { return synthLit(expr); },
                             [&](const Var&) { return synthVar(expr); },
                             [&](const Lambda&) { return synthLambda(expr); },
                             [&](const App& app) { return synthApp(app); },
                             [&](const BinOp&) { return synthBinOp(expr); },
                             [&](const If&) { return synthIf(expr); },
                             [&](const Let&) { return synthLet(expr); },
                             [&](const List&) { return synthList(expr); },
                             [&](const Match&) { return synthMatch(expr); },
                             [&](const Block&) { return synthBlock(expr); },
                             [&](const auto&) -> InferResult { return underlying_.infer(expr); }},
                    expr.node);
}

InferResult BiDirectionalInference::check(const Expr& expr, const InferTypePtr& expected) {
  if (auto* lam = std::get_if<Lambda>(&expr.node)) {
    return checkLambda(*lam, expected);
  }
  if (auto* app = std::get_if<App>(&expr.node)) {
    return checkApp(*app, expected);
  }
  if (auto* ifExpr = std::get_if<If>(&expr.node)) {
    return checkIf(*ifExpr, expected);
  }

  auto result = synthesize(expr);
  if (!subsumes(result.type, expected)) {
    auto s = unify(result.type, expected);
    return InferResult{s.compose(result.subst), s.apply(result.type), result.constraints};
  }
  return result;
}

// Delegate synthesis to underlying
InferResult BiDirectionalInference::synthLit(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthVar(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthLambda(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthApp(const App& app) {
  auto funcResult = synthesize(*app.func);
  auto argResult = synthesize(*app.arg);

  auto resultType = freshTyVar("result");
  auto funcType = argResult.subst.apply(funcResult.type);
  auto expectedType = tyFun(argResult.type, resultType);

  auto unifySubst = unify(funcType, expectedType);
  auto finalSubst = unifySubst.compose(argResult.subst).compose(funcResult.subst);

  std::vector<Constraint> finalConstraints;
  for (const auto& c : funcResult.constraints) {
    finalConstraints.push_back({c.name, unifySubst.compose(argResult.subst).apply(c.type)});
  }
  for (const auto& c : argResult.constraints) {
    finalConstraints.push_back({c.name, unifySubst.apply(c.type)});
  }

  return InferResult{finalSubst, unifySubst.apply(resultType), finalConstraints};
}

InferResult BiDirectionalInference::synthIf(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthLet(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthList(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthMatch(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthBlock(const Expr& expr) {
  return underlying_.infer(expr);
}

InferResult BiDirectionalInference::synthBinOp(const Expr& expr) {
  return underlying_.infer(expr);
}

// Checking with expected type propagation
InferResult BiDirectionalInference::checkLambda(const Lambda& lam, const InferTypePtr& expected) {
  if (auto* funTy = std::get_if<InferTyFun>(&expected->node)) {
    if (lam.params.size() == 1) {
      auto paramType = funTy->from;
      auto resultType = funTy->to;

      TypeEnv oldEnv = env();
      const Pattern& pattern = *lam.params[0];

      if (auto* varPat = std::get_if<VarPat>(&pattern.node)) {
        env().extend(varPat->name, paramType);
        auto bodyResult = check(*lam.body, resultType);
        underlying_.setEnv(oldEnv);
        return bodyResult;
      }
    }
  }

  // Fallback: just return expected (limitation until we fix AST copying)
  return InferResult{Substitution{}, expected, {}};
}

InferResult BiDirectionalInference::checkApp(const App& app, const InferTypePtr& expected) {
  auto argType = freshTyVar("arg");
  auto funcType = tyFun(argType, expected);

  auto funcResult = check(*app.func, funcType);
  auto argExpectedType = funcResult.subst.apply(argType);
  underlying_.setEnv(env().apply(funcResult.subst));
  auto argResult = check(*app.arg, argExpectedType);

  auto finalSubst = argResult.subst.compose(funcResult.subst);

  std::vector<Constraint> finalConstraints;
  for (const auto& c : funcResult.constraints) {
    finalConstraints.push_back({c.name, argResult.subst.apply(c.type)});
  }
  finalConstraints.insert(finalConstraints.end(),
                          argResult.constraints.begin(),
                          argResult.constraints.end());

  return InferResult{finalSubst, finalSubst.apply(expected), finalConstraints};
}

InferResult BiDirectionalInference::checkIf(const If& ifExpr, const InferTypePtr& expected) {
  auto condResult = check(*ifExpr.cond, tyBool());

  underlying_.setEnv(env().apply(condResult.subst));
  auto thenResult = check(*ifExpr.thenBranch, condResult.subst.apply(expected));

  underlying_.setEnv(env().apply(thenResult.subst));
  auto elseResult = check(*ifExpr.elseBranch, thenResult.subst.apply(expected));

  auto finalSubst = elseResult.subst.compose(thenResult.subst).compose(condResult.subst);

  std::vector<Constraint> constraints;
  auto s_then_else = elseResult.subst.compose(thenResult.subst);

  for (const auto& c : condResult.constraints) {
    constraints.push_back({c.name, s_then_else.apply(c.type)});
  }
  for (const auto& c : thenResult.constraints) {
    constraints.push_back({c.name, elseResult.subst.apply(c.type)});
  }
  constraints.insert(constraints.end(),
                     elseResult.constraints.begin(),
                     elseResult.constraints.end());

  return InferResult{finalSubst, finalSubst.apply(expected), constraints};
}

bool BiDirectionalInference::subsumes(const InferTypePtr& inferred, const InferTypePtr& expected) {
  try {
    unify(inferred, expected);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace solis
