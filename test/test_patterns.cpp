#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

void registerPatternMatchingTests(solis::test::TestRunner& runner) {
  using namespace solis::test;
  auto* suite = new TestSuite("Pattern Matching Tests");
  // Test: Nested cons pattern (tail extraction)
  suite->addTest("Nested cons pattern (tail extraction)", []() {
    std::string tailCode = R"(
            match [1, 2, 3] {
                (x:xs) => match xs {
                    (y:ys) => y,
                    [] => 0
                }
            }
        )";

    solis::Lexer lexer(tailCode);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue for tail test");
    assertEqual(2, intVal->value);
  });
  // Test: Cons pattern (x:xs)
  suite->addTest("Match cons pattern (x:xs)", []() {
    std::string code = R"(
            match [10, 20, 30] {
                (x:xs) => x
            }
        )";

    solis::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue");
    assertEqual(10, intVal->value);
  });

  // Test: Empty list pattern
  suite->addTest("Match empty list", []() {
    std::string code = R"(
            match [] {
                [] => 42
            }
        )";

    solis::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue");
    assertEqual(42, intVal->value);
  });
  // Test: Recursive sum with cons pattern
  suite->addTest("Recursive sum with cons pattern", []() {
    std::string code = R"(
            let sum = \xs -> match xs {
                [] => 0,
                (x:xs) => x + sum xs
            } in sum [1, 2, 3, 4, 5]
        )";

    solis::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue for recursive sum");
    assertEqual(15, intVal->value);
  });

  // Test: Non-empty list with wildcard
  suite->addTest("Match non-empty list with wildcard", []() {
    std::string code = R"(
            match [1, 2, 3] {
                [] => 0,
                _ => 99
            }
        )";

    solis::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue");
    assertEqual(99, intVal->value);
  });

  // Test: List pattern with specific elements
  suite->addTest("Match list with specific elements", []() {
    std::string code = R"(
            match [1, 2] {
                [x, y] => x + y
            }
        )";

    solis::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    solis::Parser parser(std::move(tokens));
    auto expr = parser.parseExpression();

    solis::Interpreter interp;
    auto result = interp.eval(*expr);

    auto* intVal = std::get_if<solis::IntValue>(&result->data);
    assertTrue(intVal != nullptr, "Expected IntValue");
    assertEqual(3, intVal->value);
  });

  // ... (rest of tests)

  runner.addSuite(suite);
}
