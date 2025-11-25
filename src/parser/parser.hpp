// Solis Programming Language - Parser Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "parser/ast.hpp"
#include "parser/lexer.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace solis {

class ParseError : public std::runtime_error {
public:
  ParseError(const std::string& message, size_t line, size_t column)
      : std::runtime_error(message)
      , line_(line)
      , column_(column) {}

  size_t line() const { return line_; }
  size_t column() const { return column_; }

private:
  size_t line_;
  size_t column_;
};

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  Module parseModule();
  DeclPtr parseDeclaration();
  ExprPtr parseExpression();

  // Helper for tests
  static ExprPtr parseExpressionFromSource(const std::string& source);

  bool isAtEnd() const;

private:
  // Token management
  const Token& current() const;
  const Token& previous() const;
  const Token& peek(size_t offset = 1) const;
  const Token& peekNext() const;
  bool check(TokenType type) const;
  bool match(TokenType type);
  bool match(const std::initializer_list<TokenType>& types);
  Token consume(TokenType type, const std::string& message);
  Token advance();

  // Error handling
  [[noreturn]] void error(const std::string& message);
  void synchronize();

  // Parsing methods
  ExprPtr parseAtom();
  ExprPtr parseAppExpr();
  ExprPtr parseInfixExpr();
  ExprPtr parseNonBlockExpr();  // Parse expressions but don't consume braces as blocks
  ExprPtr parseLetExpr();
  ExprPtr parseMatchExpr();
  ExprPtr parseIfExpr();
  ExprPtr parseLambdaExpr();
  ExprPtr parseBracedExpr(bool isDoBlock = false);
  ExprPtr parseBlockStatement();
  ExprPtr parseList();

  PatternPtr parsePattern();
  PatternPtr parsePrimaryPattern();

  TypePtr parseType();
  TypePtr parseTypeAtom();
  TypePtr parseAppType();
  TypePtr parseFunctionType();
  TypePtr parseEffectType();

  DeclPtr parseFunctionDecl();
  DeclPtr parseTypeDecl();
  DeclPtr parseTraitDecl();
  DeclPtr parseImplDecl();
  ModuleDecl parseModuleDecl();
  ImportDecl parseImportDecl();

  // Helpers
  int getPrecedence(TokenType type) const;

  std::vector<Token> tokens_;
  size_t current_;
};

}  // namespace solis
