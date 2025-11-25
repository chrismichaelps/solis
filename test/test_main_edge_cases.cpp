// Solis Programming Language - Main Function Edge Cases Tests
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/interpreter/interpreter.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "test_framework.hpp"

#include <sstream>

using namespace solis;
using namespace solis::test;

void registerMainEdgeCaseTests(TestRunner& runner) {
  auto* suite = new TestSuite("Main Function Edge Cases");

  // Test 1: Simple value as main
  suite->addTest("Simple value as main", []() {
    std::string source = R"(
      let main = 42
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 42) {
        throw std::runtime_error("Expected 42, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 2: Do block as main
  suite->addTest("Do block as main", []() {
    std::string source = R"(
      let main = do {
        let x = 10;
        let y = 20;
        x + y
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 30) {
        throw std::runtime_error("Expected 30, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 3: Main referencing another do block
  suite->addTest("Main referencing another do block", []() {
    std::string source = R"(
      let compute = do {
        let a = 5;
        let b = 10;
        let c = a * b;
        c + 15
      }
      let main = compute
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 65) {
        throw std::runtime_error("Expected 65, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 4: Nested do blocks in main
  suite->addTest("Nested do blocks in main", []() {
    std::string source = R"(
      let main = do {
        let inner = do {
          let x = 10;
          x * 2
        };
        inner + 5
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 25) {
        throw std::runtime_error("Expected 25, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 5: Do block with single expression
  suite->addTest("Do block with single expression", []() {
    std::string source = R"(
      let main = do {
        42
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 42) {
        throw std::runtime_error("Expected 42, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 6: Main with complex expression (right-associative operators)
  suite->addTest("Main with complex expression", []() {
    std::string source = R"(
      let x = 10
      let y = 20
      let main = x * y + 5
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      // Binary operators are right-associative: x * y + 5 = x * (y + 5) = 10 * 25 = 250
      if (iv->value != 250) {
        throw std::runtime_error("Expected 250, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 7: Do block with multiple bindings using previous values
  suite->addTest("Do block with chained bindings", []() {
    std::string source = R"(
      let main = do {
        let a = 5;
        let b = a + 3;
        let c = b * 2;
        let d = c - 1;
        d
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 15) {
        throw std::runtime_error("Expected 15, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 8: Main as lambda result
  suite->addTest("Main as lambda application", []() {
    std::string source = R"(
      let f = \x -> x * 2
      let main = f 21
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 42) {
        throw std::runtime_error("Expected 42, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 9: Main with conditional in do block
  suite->addTest("Do block with conditional", []() {
    std::string source = R"(
      let main = do {
        let x = 10;
        let y = if x > 5 { 100 } else { 0 };
        y + x
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 110) {
        throw std::runtime_error("Expected 110, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 10: Main with list construction in do block
  suite->addTest("Do block with list construction", []() {
    std::string source = R"(
      let main = do {
        let x = 1;
        let y = 2;
        let z = 3;
        let list = x:y:z:[];
        list
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* lv = std::get_if<ListValue>(&result->data)) {
      if (lv->elements.size() != 3) {
        throw std::runtime_error("Expected list of length 3");
      }
    } else {
      throw std::runtime_error("Expected ListValue");
    }
  });

  // Test 11: Main with record construction in do block
  suite->addTest("Do block with record construction", []() {
    std::string source = R"(
      let main = do {
        let name = "test";
        let value = 42;
        { name = name, value = value }
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* rv = std::get_if<RecordValue>(&result->data)) {
      if (rv->fields.size() != 2) {
        throw std::runtime_error("Expected record with 2 fields");
      }
      if (rv->fields.find("name") == rv->fields.end() ||
          rv->fields.find("value") == rv->fields.end()) {
        throw std::runtime_error("Expected fields 'name' and 'value'");
      }
    } else {
      throw std::runtime_error("Expected RecordValue");
    }
  });

  // Test 12: Multiple do blocks referenced by main
  suite->addTest("Main referencing multiple do blocks", []() {
    std::string source = R"(
      let compute1 = do {
        let x = 10;
        x * 2
      }
      let compute2 = do {
        let y = 5;
        y + 3
      }
      let main = compute1 + compute2
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 28) {
        throw std::runtime_error("Expected 28, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 13: Do block with function call
  suite->addTest("Do block with function call", []() {
    std::string source = R"(
      let double x = x * 2
      let main = do {
        let x = 21;
        double x
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 42) {
        throw std::runtime_error("Expected 42, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 14: Do block with match expression
  suite->addTest("Do block with match expression", []() {
    std::string source = R"(
      let main = do {
        let list = 1:2:3:[];
        match list {
          [] => 0,
          :: x xs => x
        }
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 1) {
        throw std::runtime_error("Expected 1, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  // Test 15: Do block with shadowing
  suite->addTest("Do block with variable shadowing", []() {
    std::string source = R"(
      let x = 100
      let main = do {
        let x = 10;
        let y = x + 5;
        let x = 20;
        x + y
      }
    )";

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    Interpreter interp;
    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    auto mainExpr = std::make_unique<Expr>(Expr{Var{"main"}});
    auto result = interp.eval(*mainExpr);

    if (auto* iv = std::get_if<IntValue>(&result->data)) {
      if (iv->value != 35) {
        throw std::runtime_error("Expected 35, got " + std::to_string(iv->value));
      }
    } else {
      throw std::runtime_error("Expected IntValue");
    }
  });

  runner.addSuite(suite);
}
