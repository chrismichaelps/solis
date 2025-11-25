// Solis Programming Language - Lexer Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "runtime/bigint.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace solis {

using LiteralValue = std::variant<std::monostate, int64_t, double, std::string, BigInt>;

enum class TokenType {
  // Literals
  Integer,
  BigIntLiteral,  // e.g., 123n
  Float,
  String,
  Char,
  BoolTrue,
  BoolFalse,

  // Identifiers
  Identifier,
  Constructor,

  // Keywords
  Let,
  Type,
  Data,
  Match,
  If,
  Then,
  Else,
  Module,
  Where,
  Import,
  Export,
  Qualified,
  Hiding,
  As,
  Trait,
  Impl,
  Forall,
  Exists,
  In,
  Do,

  // Operators
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Equal,
  EqualEqual,
  BangEqual,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  AmpAmp,
  PipePipe,
  Bang,
  PlusPlus,
  Cons,
  Arrow,
  LeftArrow,
  FatArrow,
  Pipe,
  Dot,
  Colon,
  At,
  Backslash,
  PipeRight,

  // Delimiters
  LeftParen,
  RightParen,
  LeftBracket,
  RightBracket,
  LeftBrace,
  RightBrace,
  Comma,
  Semicolon,

  // Special
  Eof,
  Error
};

struct Token {
  TokenType type;
  std::string lexeme;
  LiteralValue literal;
  size_t line;
  size_t column;  // Column position in the line (1-indexed)

  Token(TokenType type, std::string lexeme = "", size_t line = 0, size_t column = 0)
      : type(type)
      , lexeme(std::move(lexeme))
      , literal(std::monostate{})
      , line(line)
      , column(column) {}

  Token(TokenType type, std::string lexeme, LiteralValue literal, size_t line, size_t column)
      : type(type)
      , lexeme(std::move(lexeme))
      , literal(std::move(literal))
      , line(line)
      , column(column) {}
};

class Lexer {
public:
  explicit Lexer(std::string_view source);

  std::vector<Token> tokenize();

private:
  Token nextToken();

  char advance();
  char peek() const;
  char peekNext() const;
  bool match(char expected);
  bool isAtEnd() const;

  void skipWhitespace();
  void skipLineComment();
  void skipBlockComment();

  Token makeToken(TokenType type);
  Token makeToken(TokenType type, int64_t value);
  Token makeToken(TokenType type, double value);
  Token makeToken(TokenType type, const std::string& value);
  Token makeToken(TokenType type, const BigInt& value);
  Token errorToken(std::string message);

  Token number();
  Token string();
  Token charLiteral();
  Token identifier();

  TokenType identifierType();
  TokenType checkKeyword(size_t start, size_t length, std::string_view rest, TokenType type);

  std::string_view source_;
  size_t current_;
  size_t start_;
  size_t line_;
  size_t column_;
  size_t startColumn_;
};

}  // namespace solis
