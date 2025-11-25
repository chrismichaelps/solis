#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerExampleTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Example Programs Tests");

  // Example 1: Simple calculator functions
  suite->addTest("Example: Calculator functions", []() {
    std::string code = R"(
      let add = \x -> \y -> x + y in
      let subtract = \x -> \y -> x - y in
      let multiply = \x -> \y -> x * y in
      let divide = \x -> \y -> x / y in
      add (multiply 3 4) (divide 10 2)
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(17, intVal->value);  // 12 + 5
  });

  // Example 2: List utilities
  suite->addTest("Example: List utilities", []() {
    std::string code = R"(
      let length = \xs -> match xs {
        [] => 0,
        (x:xs) => 1 + length xs
      } in
      let reverse = \xs -> match xs {
        [] => [],
        (x:xs) => reverse xs ++ [x]
      } in
      length (reverse [1, 2, 3, 4])
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(4, intVal->value);
  });

  // Example 3: Working with records
  suite->addTest("Example: Record operations", []() {
    std::string code = R"(
      let createPerson = \name -> \age -> { name = name, age = age } in
      let getAge = \person -> person.age in
      let birthday = \person -> { person | age = person.age + 1 } in
      let alice = createPerson "Alice" 25 in
      getAge (birthday alice)
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(26, intVal->value);
  });

  // Example 4: Functional pipeline
  suite->addTest("Example: Functional pipeline", []() {
    std::string code = R"(
      let map = \f -> \xs -> match xs {
        [] => [],
        (x:xs) => f x : map f xs
      } in
      let filter = \p -> \xs -> match xs {
        [] => [],
        (x:xs) => if p x then x : filter p xs else filter p xs
      } in
      let sum = \xs -> match xs {
        [] => 0,
        (x:xs) => x + sum xs
      } in
      sum (filter (\x -> x > 3) (map (\x -> x * 2) [1, 2, 3, 4, 5]))
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(28, intVal->value);  // map: [2,4,6,8,10], filter(>3): [4,6,8,10], sum: 28
  });

  // Example 5: Recursive data structures
  suite->addTest("Example: Tree-like operations", []() {
    std::string code = R"(
      let flatten = \xs -> match xs {
        [] => [],
        (x:xs) => match x {
          [] => flatten xs,
          (y:ys) => y : flatten (ys : xs)
        }
      } in
      let length = \xs -> match xs {
        [] => 0,
        (x:xs) => 1 + length xs
      } in
      length (flatten [[1, 2], [3], [4, 5, 6]])
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(6, intVal->value);
  });

  // Example 6: Currying and partial application
  suite->addTest("Example: Currying", []() {
    std::string code = R"(
      let add = \x -> \y -> x + y in
      let add10 = add 10 in
      let add20 = add 20 in
      add10 (add20 5)
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(35, intVal->value);  // 10 + (20 + 5)
  });

  // Example 7: Complex pattern matching
  suite->addTest("Example: Complex pattern matching", []() {
    std::string code = R"(
      let findMax = \xs -> match xs {
        [] => 0,
        [x] => x,
        (x:y:xs) => if x > y then findMax (x : xs) else findMax (y : xs)
      } in
      findMax [3, 7, 2, 9, 1]
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(9, intVal->value);
  });

  // Example 8: Do notation for sequential operations
  suite->addTest("Example: Do notation", []() {
    std::string code = R"(
      do {
        let x = 10;
        let y = 20;
        let z = x + y;
        z * 2
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(60, intVal->value);  // (10 + 20) * 2
  });

  // Example 9: Lambda expressions
  suite->addTest("Example: Lambda expressions", []() {
    std::string code = R"(
      (\x -> \y -> x + y) 15 25
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(40, intVal->value);
  });

  // Example 10: Higher-order functions
  suite->addTest("Example: Higher-order functions", []() {
    std::string code = R"(
      let applyTwice = \f -> \x -> f (f x) in
      let square = \x -> x * x in
      applyTwice square 3
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(81, intVal->value);  // (3^2)^2 = 9^2 = 81
  });

  runner.addSuite(suite);
}
