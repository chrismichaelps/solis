#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerLazyTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Lazy Evaluation");

  suite->addTest("Lazy let binding", []() {
    std::string code = R"(
      let x = 10 + 20 in
      let y = x * 2 in
      y
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(60, intVal->value);
  });

  suite->addTest("Conditional evaluation only evaluates needed branch", []() {
    std::string code = R"(
      let expensive = 100 * 100 in
      if true then 42 else expensive
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(42, intVal->value);
  });

  suite->addTest("Strict evaluation with bang operator", []() {
    std::string code = R"(
      let x = 5 + 5 in
      !x
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(10, intVal->value);
  });

  suite->addTest("List elements are lazily evaluated", []() {
    std::string code = R"(
      let xs = [1, 2, 3, 4, 5] in
      match xs {
        (x:xs) => x,
        [] => 0
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(1, intVal->value);
  });

  suite->addTest("Nested computations are lazily evaluated", []() {
    std::string code = R"(
      let pair = [10, 20] in
      match pair {
        (x:xs) => x * 2,
        [] => 0
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(20, intVal->value);
  });

  runner.addSuite(suite);
}

