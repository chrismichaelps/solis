#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "../test/test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerParserTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Parser Tests");

  suite->addTest("Parse integer literal", []() {
    Lexer lexer("42");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<Lit>(expr->node));
  });

  suite->addTest("Parse variable", []() {
    Lexer lexer("foo");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<Var>(expr->node));
    assertEqual("foo", std::get<Var>(expr->node).name);
  });

  suite->addTest("Parse let binding", []() {
    Lexer lexer("let x = 42; x");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<Let>(expr->node));
  });

  suite->addTest("Parse if expression", []() {
    Lexer lexer("if true { 1 } else { 0 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<If>(expr->node));
  });

  suite->addTest("Parse match expression", []() {
    Lexer lexer("match x { 0 => 1, _ => 2 }");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<Match>(expr->node));
  });

  suite->addTest("Parse lambda", []() {
    Lexer lexer("\\x -> x + 1");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<Lambda>(expr->node));
  });

  suite->addTest("Parse function application", []() {
    Lexer lexer("f x");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<App>(expr->node));
  });

  suite->addTest("Parse binary operator", []() {
    Lexer lexer("1 + 2");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<BinOp>(expr->node));
  });

  suite->addTest("Parse list literal", []() {
    Lexer lexer("[1, 2, 3]");
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    assertNotNull(expr.get());
    assertTrue(std::holds_alternative<List>(expr->node));
    auto& list = std::get<List>(expr->node);
    assertEqual(3, (int)list.elements.size());
  });

  suite->addTest("Parse function declaration", []() {
    Lexer lexer("let add x y = x + y");
    Parser parser(lexer.tokenize());
    auto decl = parser.parseDeclaration();
    assertNotNull(decl.get());
    assertTrue(std::holds_alternative<FunctionDecl>(decl->node));
    auto& func = std::get<FunctionDecl>(decl->node);
    assertEqual("add", func.name);
    assertEqual(2, (int)func.params.size());
  });

  suite->addTest("Parse type declaration (ADT)", []() {
    Lexer lexer("type Maybe a = Nothing | Just a");
    Parser parser(lexer.tokenize());
    auto decl = parser.parseDeclaration();
    assertNotNull(decl.get());
    assertTrue(std::holds_alternative<TypeDecl>(decl->node));
  });

  runner.addSuite(suite);
}
