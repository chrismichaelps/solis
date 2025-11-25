#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerListTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("List Operations");

  suite->addTest("List map operation", []() {
    std::string code = R"(
      let map = \f -> \xs -> match xs {
        [] => [],
        (x:xs) => f x : map f xs
      } in
      map (\x -> x * 2) [1, 2, 3]
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* listVal = std::get_if<ListValue>(&result->data);
    assertTrue(listVal != nullptr);
    assertEqual(3, (int)listVal->elements.size());
    assertEqual(2, std::get<IntValue>(listVal->elements[0]->data).value);
    assertEqual(4, std::get<IntValue>(listVal->elements[1]->data).value);
    assertEqual(6, std::get<IntValue>(listVal->elements[2]->data).value);
  });

  suite->addTest("List filter operation", []() {
    std::string code = R"(
      let filter = \p -> \xs -> match xs {
        [] => [],
        (x:xs) => if p x then x : filter p xs else filter p xs
      } in
      filter (\x -> x > 2) [1, 2, 3, 4, 5]
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* listVal = std::get_if<ListValue>(&result->data);
    assertTrue(listVal != nullptr);
    assertEqual(3, (int)listVal->elements.size());
  });

  suite->addTest("List foldl operation", []() {
    std::string code = R"(
      let foldl = \f -> \acc -> \xs -> match xs {
        [] => acc,
        (x:xs) => foldl f (f acc x) xs
      } in
      foldl (\acc -> \x -> acc + x) 0 [1, 2, 3, 4]
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

  suite->addTest("List concatenation", []() {
    std::string code = R"(
      [1, 2] ++ [3, 4]
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* listVal = std::get_if<ListValue>(&result->data);
    assertTrue(listVal != nullptr);
    assertEqual(4, (int)listVal->elements.size());
  });

  suite->addTest("List comprehension with map and filter", []() {
    std::string code = R"(
      let map = \f -> \xs -> match xs {
        [] => [],
        (x:xs) => f x : map f xs
      } in
      let filter = \p -> \xs -> match xs {
        [] => [],
        (x:xs) => if p x then x : filter p xs else filter p xs
      } in
      filter (\x -> x > 5) (map (\x -> x * 2) [1, 2, 3, 4, 5])
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* listVal = std::get_if<ListValue>(&result->data);
    assertTrue(listVal != nullptr);
    assertEqual(3, (int)listVal->elements.size());
  });

  suite->addTest("Nested pattern matching with lists", []() {
    std::string code = R"(
      match [[1, 2], [3, 4], [5]] {
        ((x:y:xs):ys) => x + y,
        _ => 0
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(3, intVal->value);
  });

  runner.addSuite(suite);
}
