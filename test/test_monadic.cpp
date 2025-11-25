#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerMonadicTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Monadic Operations");

  suite->addTest("Do notation with sequential let bindings", []() {
    std::string code = R"(
      do {
        let x = 5;
        let y = 10;
        x + y
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(15, intVal->value);
  });

  suite->addTest("Do notation with multiple bindings", []() {
    std::string code = R"(
      do {
        let x = 10;
        let y = 20;
        let z = 30;
        x + y + z
      }
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

  suite->addTest("Nested do blocks", []() {
    std::string code = R"(
      do {
        let x = do {
          let a = 5;
          a * 2
        };
        x + 10
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

  suite->addTest("Do notation with expressions", []() {
    std::string code = R"(
      do {
        let x = 5;
        let double = \n -> n * 2;
        double x
      }
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

  runner.addSuite(suite);
}
