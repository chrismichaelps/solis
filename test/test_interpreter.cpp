#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "../test/test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerInterpreterTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Interpreter Tests");

  suite->addTest("Evaluate integer literal", []() {
    Interpreter interp;
    Lexer lexer("42");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("42", interp.valueToString(result));
  });

  suite->addTest("Evaluate string literal", []() {
    Interpreter interp;
    Lexer lexer("\"hello\"");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("hello", interp.valueToString(result));
  });

  suite->addTest("Evaluate boolean literal", []() {
    Interpreter interp;
    Lexer lexer("true");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("true", interp.valueToString(result));
  });

  suite->addTest("Evaluate addition", []() {
    Interpreter interp;
    Lexer lexer("2 + 3");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("5", interp.valueToString(result));
  });

  suite->addTest("Evaluate subtraction", []() {
    Interpreter interp;
    Lexer lexer("10 - 3");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("7", interp.valueToString(result));
  });

  suite->addTest("Evaluate multiplication", []() {
    Interpreter interp;
    Lexer lexer("4 * 5");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("20", interp.valueToString(result));
  });

  suite->addTest("Evaluate division", []() {
    Interpreter interp;
    Lexer lexer("20 / 4");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("5", interp.valueToString(result));
  });

  suite->addTest("Evaluate equality", []() {
    Interpreter interp;
    Lexer lexer("5 == 5");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("true", interp.valueToString(result));
  });

  suite->addTest("Evaluate less than", []() {
    Interpreter interp;
    Lexer lexer("3 < 5");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("true", interp.valueToString(result));
  });

  suite->addTest("Evaluate string concatenation", []() {
    Interpreter interp;
    Lexer lexer("\"hello\" ++ \" world\"");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("hello world", interp.valueToString(result));
  });

  suite->addTest("Evaluate let binding", []() {
    Interpreter interp;
    Lexer lexer("let x = 42; x");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("42", interp.valueToString(result));
  });

  suite->addTest("Evaluate if true", []() {
    Interpreter interp;
    Lexer lexer("if true { 1 } else { 0 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("1", interp.valueToString(result));
  });

  suite->addTest("Evaluate if false", []() {
    Interpreter interp;
    Lexer lexer("if false { 1 } else { 0 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("0", interp.valueToString(result));
  });

  suite->addTest("Evaluate match with literal", []() {
    Interpreter interp;
    Lexer lexer("match 0 { 0 => 1, _ => 2 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("1", interp.valueToString(result));
  });

  suite->addTest("Evaluate match with wildcard", []() {
    Interpreter interp;
    Lexer lexer("match 5 { 0 => 1, _ => 2 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    auto result = interp.eval(*expr);
    assertEqual("2", interp.valueToString(result));
  });

  runner.addSuite(suite);
}
