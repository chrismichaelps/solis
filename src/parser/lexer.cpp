// Solis Programming Language - Lexical Analyzer
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "parser/lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace solis {

Lexer::Lexer(std::string_view source)
    : source_(source)
    , current_(0)
    , start_(0)
    , line_(1)
    , column_(1)
    , startColumn_(1) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (!isAtEnd()) {
    start_ = current_;
    startColumn_ = column_;
    Token token = nextToken();
    if (token.type != TokenType::Error) {
      tokens.push_back(std::move(token));
    }
  }
  tokens.push_back(Token(TokenType::Eof, "", line_, column_));
  return tokens;
}

Token Lexer::nextToken() {
  skipWhitespace();

  if (isAtEnd()) {
    return makeToken(TokenType::Eof);
  }

  start_ = current_;
  startColumn_ = column_;
  char c = advance();

  // Numbers
  if (std::isdigit(c)) {
    return number();
  }

  // Identifiers and keywords
  if (std::isalpha(c) || c == '_') {
    return identifier();
  }

  // String literals
  if (c == '"') {
    return string();
  }

  // Char literals
  if (c == '\'') {
    return charLiteral();
  }

  // Operators and delimiters
  switch (c) {
  case '(':
    return makeToken(TokenType::LeftParen);
  case ')':
    return makeToken(TokenType::RightParen);
  case '[':
    return makeToken(TokenType::LeftBracket);
  case ']':
    return makeToken(TokenType::RightBracket);
  case '{':
    return makeToken(TokenType::LeftBrace);
  case '}':
    return makeToken(TokenType::RightBrace);
  case ',':
    return makeToken(TokenType::Comma);
  case ';':
    return makeToken(TokenType::Semicolon);
  case '@':
    return makeToken(TokenType::At);
  case '\\':
    return makeToken(TokenType::Backslash);

  case '+':
    if (match('+'))
      return makeToken(TokenType::PlusPlus);
    return makeToken(TokenType::Plus);

  case '-':
    if (match('>'))
      return makeToken(TokenType::Arrow);
    return makeToken(TokenType::Minus);

  case '*':
    return makeToken(TokenType::Star);
  case '/':
    return makeToken(TokenType::Slash);
  case '%':
    return makeToken(TokenType::Percent);

  case '=':
    if (match('='))
      return makeToken(TokenType::EqualEqual);
    if (match('>'))
      return makeToken(TokenType::FatArrow);
    return makeToken(TokenType::Equal);

  case '!':
    if (match('='))
      return makeToken(TokenType::BangEqual);
    return makeToken(TokenType::Bang);

  case '<':
    if (match('-'))
      return makeToken(TokenType::LeftArrow);
    if (match('='))
      return makeToken(TokenType::LessEqual);
    return makeToken(TokenType::Less);

  case '>':
    if (match('='))
      return makeToken(TokenType::GreaterEqual);
    return makeToken(TokenType::Greater);

  case '&':
    if (match('&'))
      return makeToken(TokenType::AmpAmp);
    return errorToken("Expected '&' after '&'");

  case '|':
    if (match('|'))
      return makeToken(TokenType::PipePipe);
    if (match('>'))
      return makeToken(TokenType::PipeRight);
    return makeToken(TokenType::Pipe);

  case ':':
    if (match(':'))
      return makeToken(TokenType::Cons);
    return makeToken(TokenType::Colon);

  case '.':
    return makeToken(TokenType::Dot);
  }

  return errorToken("Unexpected character");
}

char Lexer::advance() {
  column_++;
  return source_[current_++];
}

char Lexer::peek() const {
  if (isAtEnd())
    return '\0';
  return source_[current_];
}

char Lexer::peekNext() const {
  if (current_ + 1 >= source_.size())
    return '\0';
  return source_[current_ + 1];
}

bool Lexer::match(char expected) {
  if (isAtEnd())
    return false;
  if (source_[current_] != expected)
    return false;
  advance();
  return true;
}

bool Lexer::isAtEnd() const {
  return current_ >= source_.size();
}

void Lexer::skipWhitespace() {
  while (!isAtEnd()) {
    char c = peek();
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance();
      break;
    case '\n':
      line_++;
      column_ = 0;  // Will be incremented to 1 on advance
      advance();
      break;
    case '/':
      if (peekNext() == '/') {
        skipLineComment();
      } else if (peekNext() == '*') {
        skipBlockComment();
      } else {
        return;
      }
      break;
    case '-':
      if (peekNext() == '-') {
        skipLineComment();
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

void Lexer::skipLineComment() {
  while (!isAtEnd() && peek() != '\n') {
    advance();
  }
}

void Lexer::skipBlockComment() {
  advance();  // consume '/'
  advance();  // consume '*'

  while (!isAtEnd()) {
    if (peek() == '*' && peekNext() == '/') {
      advance();  // consume '*'
      advance();  // consume '/'
      break;
    }
    if (peek() == '\n') {
      line_++;
      column_ = 0;
    }
    advance();
  }
}

Token Lexer::number() {
  size_t start = current_ - 1;

  while (std::isdigit(peek()))
    advance();

  // Check for BigInt suffix 'n'
  if (peek() == 'n') {
    advance();                                                        // consume 'n'
    std::string numStr(source_.substr(start, current_ - start - 1));  // exclude 'n'
    BigInt value(numStr);
    return makeToken(TokenType::BigIntLiteral, value);
  }

  // Check for decimal point
  if (peek() == '.' && std::isdigit(peekNext())) {
    advance();  // consume '.'
    while (std::isdigit(peek()))
      advance();

    // Check for exponent
    if (peek() == 'e' || peek() == 'E') {
      advance();
      if (peek() == '+' || peek() == '-') {
        advance();
      }
      while (std::isdigit(peek())) {
        advance();
      }
    }

    std::string numStr(source_.substr(start, current_ - start));
    double value = std::stod(numStr);
    return makeToken(TokenType::Float, value);
  }

  // Regular integer
  std::string numStr(source_.substr(start, current_ - start));
  int64_t value = std::stoll(numStr);
  return makeToken(TokenType::Integer, value);
}

Token Lexer::string() {
  std::string value;

  while (!isAtEnd() && peek() != '"') {
    if (peek() == '\\') {
      advance();
      if (isAtEnd())
        break;
      char escaped = advance();
      switch (escaped) {
      case 'n':
        value += '\n';
        break;
      case 't':
        value += '\t';
        break;
      case 'r':
        value += '\r';
        break;
      case '\\':
        value += '\\';
        break;
      case '"':
        value += '"';
        break;
      default:
        value += escaped;
        break;
      }
    } else {
      if (peek() == '\n') {
        line_++;
        column_ = 0;
      }
      value += advance();
    }
  }

  if (isAtEnd()) {
    return errorToken("Unterminated string");
  }

  advance();  // closing "

  return makeToken(TokenType::String, value);
}

Token Lexer::charLiteral() {
  if (isAtEnd()) {
    return errorToken("Unterminated char literal");
  }

  char value;
  if (peek() == '\\') {
    advance();
    if (isAtEnd())
      return errorToken("Unterminated char literal");
    char escaped = advance();
    switch (escaped) {
    case 'n':
      value = '\n';
      break;
    case 't':
      value = '\t';
      break;
    case 'r':
      value = '\r';
      break;
    case '\\':
      value = '\\';
      break;
    case '\'':
      value = '\'';
      break;
    default:
      value = escaped;
      break;
    }
  } else {
    value = advance();
  }

  if (isAtEnd() || peek() != '\'') {
    return errorToken("Unterminated char literal");
  }

  advance();  // closing '

  return makeToken(TokenType::Char, std::string(1, value));
}

Token Lexer::identifier() {
  while (std::isalnum(peek()) || peek() == '_' || peek() == '\'') {
    advance();
  }

  return makeToken(identifierType());
}

TokenType Lexer::identifierType() {
  std::string_view lexeme = source_.substr(start_, current_ - start_);

  // Check for keywords
  static const std::unordered_map<std::string_view, TokenType> keywords = {
      {"let", TokenType::Let},       {"type", TokenType::Type},
      {"data", TokenType::Data},     {"match", TokenType::Match},
      {"if", TokenType::If},         {"then", TokenType::Then},
      {"else", TokenType::Else},     {"module", TokenType::Module},
      {"where", TokenType::Where},   {"import", TokenType::Import},
      {"export", TokenType::Export}, {"qualified", TokenType::Qualified},
      {"hiding", TokenType::Hiding}, {"as", TokenType::As},
      {"trait", TokenType::Trait},   {"impl", TokenType::Impl},
      {"forall", TokenType::Forall}, {"exists", TokenType::Exists},
      {"in", TokenType::In},         {"do", TokenType::Do},
      {"true", TokenType::BoolTrue}, {"false", TokenType::BoolFalse}};

  auto it = keywords.find(lexeme);
  if (it != keywords.end()) {
    return it->second;
  }

  // Check if it's a constructor (starts with uppercase)
  if (std::isupper(source_[start_])) {
    return TokenType::Constructor;
  }

  return TokenType::Identifier;
}

Token Lexer::makeToken(TokenType type) {
  size_t length = current_ - start_;
  std::string lexeme(source_.substr(start_, length));
  return Token{type, lexeme, std::monostate{}, line_, column_};
}

Token Lexer::makeToken(TokenType type, int64_t value) {
  size_t length = current_ - start_;
  std::string lexeme(source_.substr(start_, length));
  return Token{type, lexeme, value, line_, column_};
}

Token Lexer::makeToken(TokenType type, double value) {
  size_t length = current_ - start_;
  std::string lexeme(source_.substr(start_, length));
  return Token{type, lexeme, value, line_, column_};
}

Token Lexer::makeToken(TokenType type, const std::string& value) {
  size_t length = current_ - start_;
  std::string lexeme(source_.substr(start_, length));
  return Token{type, lexeme, value, line_, column_};
}

Token Lexer::makeToken(TokenType type, const BigInt& value) {
  size_t length = current_ - start_;
  std::string lexeme(source_.substr(start_, length));
  return Token{type, lexeme, value, line_, column_};
}

Token Lexer::errorToken(std::string message) {
  return Token(TokenType::Error, std::move(message), line_, startColumn_);
}

}  // namespace solis
