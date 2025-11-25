#include "../src/parser/lexer.hpp"
#include "../test/test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerLexerTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Lexer Tests");

  suite->addTest("Tokenize integers", []() {
    Lexer lexer("42");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size(), "Should have integer + EOF");
    assertEqual(TokenType::Integer, tokens[0].type);
    assertEqual(42LL, std::get<int64_t>(tokens[0].literal));
  });

  suite->addTest("Tokenize floats", []() {
    Lexer lexer("3.14");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size());
    assertEqual(TokenType::Float, tokens[0].type);
  });

  suite->addTest("Tokenize strings", []() {
    Lexer lexer("\"hello\"");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size());
    assertEqual(TokenType::String, tokens[0].type);
    assertEqual("hello", std::get<std::string>(tokens[0].literal));
  });

  suite->addTest("Tokenize identifiers", []() {
    Lexer lexer("foo bar_baz");
    auto tokens = lexer.tokenize();
    assertEqual(3, (int)tokens.size());  // foo, bar_baz, EOF
    assertEqual(TokenType::Identifier, tokens[0].type);
    assertEqual("foo", tokens[0].lexeme);
    assertEqual(TokenType::Identifier, tokens[1].type);
    assertEqual("bar_baz", tokens[1].lexeme);
  });

  suite->addTest("Tokenize keywords", []() {
    Lexer lexer("let match if else");
    auto tokens = lexer.tokenize();
    assertEqual(5, (int)tokens.size());
    assertEqual(TokenType::Let, tokens[0].type);
    assertEqual(TokenType::Match, tokens[1].type);
    assertEqual(TokenType::If, tokens[2].type);
    assertEqual(TokenType::Else, tokens[3].type);
  });

  suite->addTest("Tokenize operators", []() {
    Lexer lexer("+ - * / == != <= >= ++ ::");
    auto tokens = lexer.tokenize();
    assertEqual(11, (int)tokens.size());  // 10 operators + EOF
    assertEqual(TokenType::Plus, tokens[0].type);
    assertEqual(TokenType::Minus, tokens[1].type);
    assertEqual(TokenType::Star, tokens[2].type);
    assertEqual(TokenType::Slash, tokens[3].type);
    assertEqual(TokenType::EqualEqual, tokens[4].type);
    assertEqual(TokenType::BangEqual, tokens[5].type);
    assertEqual(TokenType::LessEqual, tokens[6].type);
    assertEqual(TokenType::GreaterEqual, tokens[7].type);
    assertEqual(TokenType::PlusPlus, tokens[8].type);
    assertEqual(TokenType::Cons, tokens[9].type);
  });

  suite->addTest("Tokenize arrows", []() {
    Lexer lexer("-> =>");
    auto tokens = lexer.tokenize();
    assertEqual(3, (int)tokens.size());
    assertEqual(TokenType::Arrow, tokens[0].type);
    assertEqual(TokenType::FatArrow, tokens[1].type);
  });

  suite->addTest("Tokenize delimiters", []() {
    Lexer lexer("( ) [ ] { } , ;");
    auto tokens = lexer.tokenize();
    assertEqual(9, (int)tokens.size());
    assertEqual(TokenType::LeftParen, tokens[0].type);
    assertEqual(TokenType::RightParen, tokens[1].type);
    assertEqual(TokenType::LeftBracket, tokens[2].type);
    assertEqual(TokenType::RightBracket, tokens[3].type);
    assertEqual(TokenType::LeftBrace, tokens[4].type);
    assertEqual(TokenType::RightBrace, tokens[5].type);
    assertEqual(TokenType::Comma, tokens[6].type);
    assertEqual(TokenType::Semicolon, tokens[7].type);
  });

  suite->addTest("Skip line comments", []() {
    Lexer lexer("// This is a comment\n42");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size());
    assertEqual(TokenType::Integer, tokens[0].type);
  });

  suite->addTest("Skip block comments", []() {
    Lexer lexer("/* comment */ 42");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size());
    assertEqual(TokenType::Integer, tokens[0].type);
  });

  suite->addTest("Handle escape sequences in strings", []() {
    Lexer lexer("\"hello\\nworld\"");
    auto tokens = lexer.tokenize();
    assertEqual(2, (int)tokens.size());
    assertEqual(TokenType::String, tokens[0].type);
    assertEqual("hello\nworld", std::get<std::string>(tokens[0].literal));
  });

  runner.addSuite(suite);
}
