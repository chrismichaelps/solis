#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <sstream>

void registerPreludeTests(solis::test::TestRunner& runner) {
  using namespace solis::test;
  auto* suite = new TestSuite("Standard Library (Prelude) Tests");

  suite->addTest("Prelude: Basics (id, const, not)", []() {
    solis::Interpreter interp;

    // Load Prelude
    std::ifstream file("src/solis/prelude/prelude.solis");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    solis::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    // Debug: Print tokens
    /*
    for (const auto& t : tokens) {
        std::cout << "Token: '" << t.lexeme << "' (" << (int)t.type << ") line " << t.line << "\n";
    }
    */

    solis::Parser parser(std::move(tokens));

    std::vector<solis::DeclPtr> decls;
    while (!parser.isAtEnd()) {
      auto decl = parser.parseDeclaration();
      interp.eval(*decl);
      decls.push_back(std::move(decl));
    }

    // Test id
    auto res1 = interp.eval(*parser.parseExpressionFromSource("id 42"));
    assertEqual(42, std::get<solis::IntValue>(res1->data).value);

    // Test const
    auto res2 = interp.eval(*parser.parseExpressionFromSource("const 10 20"));
    assertEqual(10, std::get<solis::IntValue>(res2->data).value);

    // Test not
    auto res3 = interp.eval(*parser.parseExpressionFromSource("not true"));
    assertEqual(false, std::get<solis::BoolValue>(res3->data).value);
  });

  suite->addTest("Prelude: List Basics (length, null, head)", []() {
    solis::Interpreter interp;
    // Load Prelude
    std::ifstream file("src/solis/prelude/prelude.solis");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    solis::Lexer lexer(source);
    solis::Parser parser(lexer.tokenize());
    std::vector<solis::DeclPtr> decls;
    while (!parser.isAtEnd()) {
      auto decl = parser.parseDeclaration();
      interp.eval(*decl);
      decls.push_back(std::move(decl));
    }

    // Test length
    auto res1 = interp.eval(*parser.parseExpressionFromSource("length [1, 2, 3]"));
    assertEqual(3, std::get<solis::IntValue>(res1->data).value);

    // Test null
    auto res2 = interp.eval(*parser.parseExpressionFromSource("null []"));
    assertEqual(true, std::get<solis::BoolValue>(res2->data).value);

    // Test head
    auto res3 = interp.eval(*parser.parseExpressionFromSource("head [10, 20]"));
    assertEqual(10, std::get<solis::IntValue>(res3->data).value);
  });

  suite->addTest("Prelude: Higher Order (map, filter)", []() {
    solis::Interpreter interp;
    std::ifstream file("src/solis/prelude/prelude.solis");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    solis::Lexer lexer(source);
    solis::Parser parser(lexer.tokenize());

    std::vector<solis::DeclPtr> decls;
    while (!parser.isAtEnd()) {
      auto decl = parser.parseDeclaration();
      interp.eval(*decl);
      decls.push_back(std::move(decl));
    }

    // Test map
    auto res1 = interp.eval(*parser.parseExpressionFromSource("map (\\x -> x * 2) [1, 2, 3]"));
    auto list1 = std::get<solis::ListValue>(res1->data);
    assertEqual(3, (int)list1.elements.size());
    assertEqual(2, std::get<solis::IntValue>(list1.elements[0]->data).value);
    assertEqual(6, std::get<solis::IntValue>(list1.elements[2]->data).value);

    // Test filter
    auto res2 = interp.eval(*parser.parseExpressionFromSource("filter (\\x -> x > 1) [1, 2, 3]"));
    auto list2 = std::get<solis::ListValue>(res2->data);
    assertEqual(2, (int)list2.elements.size());
    assertEqual(2, std::get<solis::IntValue>(list2.elements[0]->data).value);
  });

  suite->addTest("Prelude: Folds (foldl, foldr)", []() {
    solis::Interpreter interp;
    std::ifstream file("src/solis/prelude/prelude.solis");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    solis::Lexer lexer(source);
    solis::Parser parser(lexer.tokenize());

    std::vector<solis::DeclPtr> decls;
    while (!parser.isAtEnd()) {
      auto decl = parser.parseDeclaration();
      interp.eval(*decl);
      decls.push_back(std::move(decl));
    }

    // Test foldl (sum)
    auto res1 = interp.eval(
        *parser.parseExpressionFromSource("foldl (\\acc -> \\x -> acc + x) 0 [1, 2, 3, 4]"));
    assertEqual(10, std::get<solis::IntValue>(res1->data).value);
  });

  runner.addSuite(suite);
}
