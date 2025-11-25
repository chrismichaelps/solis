#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerStringTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("String Operations");

  suite->addTest("String literal", []() {
    std::string code = R"("Hello, World!")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
    assertEqual("Hello, World!", strVal->value);
  });

  suite->addTest("String concatenation", []() {
    std::string code = R"("Hello, " ++ "World!")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
    assertEqual("Hello, World!", strVal->value);
  });

  suite->addTest("String in let binding", []() {
    std::string code = R"(
      let greeting = "Hello" in
      let name = "Alice" in
      greeting ++ ", " ++ name
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
    assertEqual("Hello, Alice", strVal->value);
  });

  suite->addTest("String equality", []() {
    std::string code = R"("test" == "test")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* boolVal = std::get_if<BoolValue>(&result->data);
    assertTrue(boolVal != nullptr);
    assertEqual(true, boolVal->value);
  });

  suite->addTest("String inequality", []() {
    std::string code = R"("hello" == "world")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* boolVal = std::get_if<BoolValue>(&result->data);
    assertTrue(boolVal != nullptr);
    assertEqual(false, boolVal->value);
  });

  suite->addTest("Empty string", []() {
    std::string code = R"("")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
    assertEqual("", strVal->value);
  });

  suite->addTest("String with special characters", []() {
    std::string code = R"("Line1\nLine2\tTabbed")";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
  });

  runner.addSuite(suite);
}

