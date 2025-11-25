#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerNumericTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Numeric Types");

  suite->addTest("BigInt literal support", []() {
    std::string code = "123456789012345678901234567890n";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* bigIntVal = std::get_if<BigIntValue>(&result->data);
    assertTrue(bigIntVal != nullptr);
  });

  suite->addTest("BigInt addition", []() {
    std::string code = "100000000000000000000n + 200000000000000000000n";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* bigIntVal = std::get_if<BigIntValue>(&result->data);
    assertTrue(bigIntVal != nullptr);
  });

  suite->addTest("BigInt multiplication", []() {
    std::string code = "999999999999999999n * 888888888888888888n";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* bigIntVal = std::get_if<BigIntValue>(&result->data);
    assertTrue(bigIntVal != nullptr);
  });

  suite->addTest("BigInt subtraction", []() {
    std::string code = "500000000000000000000n - 100000000000000000000n";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* bigIntVal = std::get_if<BigIntValue>(&result->data);
    assertTrue(bigIntVal != nullptr);
  });

  suite->addTest("Mixed integer and BigInt in expression", []() {
    std::string code = "let x = 42 in let y = 100000000000000000000n in x + 0";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(42, intVal->value);
  });

  suite->addTest("Float arithmetic addition", []() {
    std::string code = "3.14 + 2.86";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* floatVal = std::get_if<FloatValue>(&result->data);
    assertTrue(floatVal != nullptr);
  });

  suite->addTest("Integer division", []() {
    std::string code = "10 / 2";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(5, intVal->value);
  });

  suite->addTest("Modulo operation", []() {
    std::string code = "17 % 5";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(2, intVal->value);
  });

  runner.addSuite(suite);
}
