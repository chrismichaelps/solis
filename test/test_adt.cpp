#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerADTTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Algebraic Data Types");

  suite->addTest("Simple ADT constructor", []() {
    std::string code = R"(
      data Maybe a = Just a | Nothing
      
      let x = Just 42 in
      match x {
        Just n => n,
        Nothing => 0
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto mod = parser.parseModule();
    Interpreter interp;
    for (auto& decl : mod.declarations) {
      interp.eval(*decl);
    }
    // Evaluate the let expression
    auto letCode = R"(
      let x = Just 42 in
      match x {
        Just n => n,
        Nothing => 0
      }
    )";
    Lexer lexer2(letCode);
    Parser parser2(lexer2.tokenize());
    auto expr = parser2.parseExpression();
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(42, intVal->value);
  });

  suite->addTest("ADT with multiple constructors", []() {
    std::string code = R"(
      data Color = Red | Green | Blue
      
      let myColor = Green in
      match myColor {
        Red => 1,
        Green => 2,
        Blue => 3
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto mod = parser.parseModule();
    Interpreter interp;
    for (auto& decl : mod.declarations) {
      interp.eval(*decl);
    }
    auto letCode = R"(
      let myColor = Green in
      match myColor {
        Red => 1,
        Green => 2,
        Blue => 3
      }
    )";
    Lexer lexer2(letCode);
    Parser parser2(lexer2.tokenize());
    auto expr = parser2.parseExpression();
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(2, intVal->value);
  });

  suite->addTest("Recursive ADT - List", []() {
    std::string code = R"(
      data List a = Nil | Cons a (List a)
      
      let myList = Cons 1 (Cons 2 Nil) in
      match myList {
        Nil => 0,
        Cons x xs => x
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto mod = parser.parseModule();
    Interpreter interp;
    for (auto& decl : mod.declarations) {
      interp.eval(*decl);
    }
    auto letCode = R"(
      let myList = Cons 1 (Cons 2 Nil) in
      match myList {
        Nil => 0,
        Cons x xs => x
      }
    )";
    Lexer lexer2(letCode);
    Parser parser2(lexer2.tokenize());
    auto expr = parser2.parseExpression();
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(1, intVal->value);
  });

  suite->addTest("ADT with multiple fields", []() {
    std::string code = R"(
      data Person = MakePerson String Int
      
      let bob = MakePerson "Bob" 30 in
      match bob {
        MakePerson name age => age
      }
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto mod = parser.parseModule();
    Interpreter interp;
    for (auto& decl : mod.declarations) {
      interp.eval(*decl);
    }
    auto letCode = R"(
      let bob = MakePerson "Bob" 30 in
      match bob {
        MakePerson name age => age
      }
    )";
    Lexer lexer2(letCode);
    Parser parser2(lexer2.tokenize());
    auto expr = parser2.parseExpression();
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(30, intVal->value);
  });

  runner.addSuite(suite);
}

