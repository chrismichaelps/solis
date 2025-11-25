#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerFunctionTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Functions and Closures");

  suite->addTest("Function definition and call", []() {
    std::string code = R"(
      let add = \x -> \y -> x + y in
      add 5 3
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(8, intVal->value);
  });

  suite->addTest("Closure captures outer scope", []() {
    std::string code = R"(
      let makeAdder = \x -> \y -> x + y in
      let add5 = makeAdder 5 in
      add5 3
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(8, intVal->value);
  });

  suite->addTest("Curried function application", []() {
    std::string code = R"(
      let multiply = \x -> \y -> x * y in
      let double = multiply 2 in
      double 7
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(14, intVal->value);
  });

  suite->addTest("Higher-order function composition", []() {
    std::string code = R"(
      let compose = \f -> \g -> \x -> f (g x) in
      let add1 = \x -> x + 1 in
      let mul2 = \x -> x * 2 in
      let add1ThenMul2 = compose mul2 add1 in
      add1ThenMul2 5
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(12, intVal->value);
  });

  suite->addTest("Fibonacci recursive function", []() {
    std::string code = R"(
      let fib = \n -> match n {
        0 => 0,
        1 => 1,
        n => fib (n - 1) + fib (n - 2)
      } in
      fib 7
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(13, intVal->value);
  });

  suite->addTest("Factorial recursive function", []() {
    std::string code = R"(
      let fact = \n -> if n <= 1 then 1 else n * fact (n - 1) in
      fact 5
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(120, intVal->value);
  });

  runner.addSuite(suite);
}
