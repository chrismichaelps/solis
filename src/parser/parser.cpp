// Solis Programming Language - Parser Implementation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "parser/parser.hpp"

#include "parser/ast.hpp"
#include "parser/lexer.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace solis {

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens))
    , current_(0) {}

const Token& Parser::current() const {
  return tokens_[current_];
}

const Token& Parser::peekNext() const {
  if (current_ + 1 >= tokens_.size()) {
    return tokens_.back();  // Return EOF if at end
  }
  return tokens_[current_ + 1];
}

const Token& Parser::previous() const {
  return tokens_[current_ - 1];
}

const Token& Parser::peek(size_t offset) const {
  size_t idx = current_ + offset;
  if (idx >= tokens_.size()) {
    return tokens_.back();
  }
  return tokens_[idx];
}

bool Parser::check(TokenType type) const {
  if (isAtEnd())
    return false;
  return current().type == type;
}

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::match(const std::initializer_list<TokenType>& types) {
  for (auto type : types) {
    if (check(type)) {
      advance();
      return true;
    }
  }
  return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
  if (check(type)) {
    return advance();
  }
  error(message);
}

Token Parser::advance() {
  if (!isAtEnd()) {
    current_++;
  }
  return previous();
}

bool Parser::isAtEnd() const {
  return current().type == TokenType::Eof;
}

void Parser::error(const std::string& message) {
  const Token& token = current();
  std::ostringstream oss;
  oss << "Parse error at line " << token.line << ", column " << token.column << ": " << message
      << " (got '" << token.lexeme << "')";
  throw ParseError(oss.str(), token.line, token.column);
}

void Parser::synchronize() {
  advance();

  while (!isAtEnd()) {
    if (previous().type == TokenType::Semicolon)
      return;

    switch (current().type) {
    case TokenType::Let:
    case TokenType::Type:
    case TokenType::Module:
    case TokenType::Import:
      return;
    default:;  // continue
    }
    advance();
  }
}

Module Parser::parseModule() {
  Module mod;

  // Optional module declaration
  if (check(TokenType::Module)) {
    mod.moduleDecl = parseModuleDecl();
  }

  // Imports
  while (check(TokenType::Import)) {
    mod.imports.push_back(parseImportDecl());
  }

  // Declarations
  while (!isAtEnd()) {
    try {
      mod.declarations.push_back(parseDeclaration());
    } catch (const ParseError& e) {
      // Error recovery
      synchronize();
    }
  }

  return mod;
}

ModuleDecl Parser::parseModuleDecl() {
  consume(TokenType::Module, "Expected 'module'");

  ModuleDecl decl;
  decl.name = consume(TokenType::Constructor, "Expected module name").lexeme;

  while (match(TokenType::Dot)) {
    decl.name += ".";
    decl.name += consume(TokenType::Constructor, "Expected module name part").lexeme;
  }

  // Optional export list: module Foo (exportA, exportB) where
  if (match(TokenType::LeftParen)) {
    if (!check(TokenType::RightParen)) {
      do {
        Token exportName = consume(TokenType::Identifier, "Expected export name");
        decl.exports.push_back(exportName.lexeme);
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "Expected ')' after export list");
  }

  consume(TokenType::Where, "Expected 'where' after module name");

  return decl;
}

ImportDecl Parser::parseImportDecl() {
  consume(TokenType::Import, "Expected 'import'");

  ImportDecl decl;
  decl.qualified = match(TokenType::Qualified);

  // Parse module name
  if (check(TokenType::Constructor)) {
    decl.moduleName = consume(TokenType::Constructor, "Expected module name").lexeme;
  } else {
    decl.moduleName = consume(TokenType::Identifier, "Expected module name").lexeme;
  }

  // Handle hierarchical module names (e.g., Data.List)
  while (match(TokenType::Dot)) {
    decl.moduleName += ".";
    if (check(TokenType::Constructor)) {
      decl.moduleName += consume(TokenType::Constructor, "Expected module name part").lexeme;
    } else {
      decl.moduleName += consume(TokenType::Identifier, "Expected module name part").lexeme;
    }
  }

  // Parse 'as Alias'
  if (match(TokenType::As)) {
    decl.alias = consume(TokenType::Constructor, "Expected alias name").lexeme;
  }

  // Parse hiding clause: import Foo hiding (bar, baz)
  if (match(TokenType::Hiding)) {
    consume(TokenType::LeftParen, "Expected '(' after 'hiding'");
    if (!check(TokenType::RightParen)) {
      do {
        Token symbol = consume(TokenType::Identifier, "Expected identifier in hiding list");
        decl.hiding.push_back(symbol.lexeme);
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "Expected ')' after hiding list");
  }
  // Parse selective import: import Foo (bar, baz)
  // Note: Can't have both selective and hiding
  else if (match(TokenType::LeftParen)) {
    if (!check(TokenType::RightParen)) {
      do {
        Token symbol = consume(TokenType::Identifier, "Expected identifier in import list");
        decl.imports.push_back(symbol.lexeme);
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "Expected ')' after import list");
  }

  return decl;
}

DeclPtr Parser::parseDeclaration() {
  if (check(TokenType::Type) || check(TokenType::Data)) {
    return parseTypeDecl();
  } else if (check(TokenType::Let)) {
    return parseFunctionDecl();
  } else if (check(TokenType::Trait)) {
    return parseTraitDecl();
  } else if (check(TokenType::Impl)) {
    return parseImplDecl();
  } else if (check(TokenType::Export)) {
    // Handle export declarations (simplified)
    advance();
    // No handling for export keyword in parser
    return parseDeclaration();
  }

  error("Expected declaration");
}

DeclPtr Parser::parseFunctionDecl() {
  consume(TokenType::Let, "Expected 'let'");
  // Capture start location (the identifier)
  Token nameToken = consume(TokenType::Identifier, "Expected function name");
  std::string name = nameToken.lexeme;

  std::vector<PatternPtr> params;

  // Parse parameters - stop at = or :
  while (true) {
    // Stop conditions FIRST - check before trying to parse as pattern
    if (check(TokenType::Equal) || check(TokenType::Colon)) {
      break;
    }

    // Must be a valid pattern starter
    if (!(check(TokenType::Identifier) || check(TokenType::Constructor) ||
          check(TokenType::LeftParen) || check(TokenType::LeftBracket) ||
          check(TokenType::Integer) || check(TokenType::String) || check(TokenType::BoolTrue) ||
          check(TokenType::BoolFalse))) {
      break;
    }

    params.push_back(parsePattern());
  }

  std::optional<TypePtr> typeAnnotation;
  if (match(TokenType::Colon)) {
    typeAnnotation = parseType();
  }

  consume(TokenType::Equal, "Expected '=' after function signature");
  ExprPtr body = parseExpression();

  // Capture end location (from the last token of the body)
  Token endToken = previous();

  auto decl = std::make_unique<Decl>();
  decl->node = FunctionDecl{
      std::move(name), std::move(typeAnnotation), std::move(params), std::move(body), std::nullopt};

  // Set source location
  if (auto* funcDecl = std::get_if<FunctionDecl>(&decl->node)) {
    funcDecl->location = SourceLocation(static_cast<int>(nameToken.line),
                                        static_cast<int>(nameToken.column),
                                        static_cast<int>(endToken.line),
                                        static_cast<int>(endToken.column +
                                                         endToken.lexeme.length()));
  }

  return decl;
}

DeclPtr Parser::parseTypeDecl() {
  // Accept either 'type' or 'data' keyword
  if (!check(TokenType::Type) && !check(TokenType::Data)) {
    throw std::runtime_error("Expected 'type' or 'data'");
  }
  advance();  // Consume type/data token


  TypeDecl typeDecl;
  typeDecl.name = consume(TokenType::Constructor, "Expected type name").lexeme;

  // Type parameters
  while (check(TokenType::Identifier) || check(TokenType::LeftParen)) {
    if (match(TokenType::LeftParen)) {
      // Dependent type parameter: (n: Nat)
      std::string paramName = consume(TokenType::Identifier, "Expected parameter name").lexeme;
      consume(TokenType::Colon, "Expected ':' after parameter name");
      parseType();  // Parse kind (but we don't store it yet)
      consume(TokenType::RightParen, "Expected ')' after dependent parameter");
      typeDecl.params.push_back(paramName);
    } else {
      typeDecl.params.push_back(consume(TokenType::Identifier, "Expected type parameter").lexeme);
    }
  }

  consume(TokenType::Equal, "Expected '=' after type name");

  // Parse RHS
  if (check(TokenType::LeftBrace)) {
    // Record type
    advance();
    std::map<std::string, TypePtr> fields;

    if (!check(TokenType::RightBrace)) {
      do {
        std::string fieldName = consume(TokenType::Identifier, "Expected field name").lexeme;
        consume(TokenType::Colon, "Expected ':' after field name");
        fields[fieldName] = parseType();
      } while (match(TokenType::Comma));
    }

    consume(TokenType::RightBrace, "Expected '}' after record fields");
    typeDecl.rhs = std::move(fields);
  } else if (check(TokenType::Constructor)) {
    // ADT
    std::vector<std::pair<std::string, std::vector<TypePtr>>> constructors;

    do {
      std::string conName = consume(TokenType::Constructor, "Expected constructor").lexeme;
      std::vector<TypePtr> fields;

      while (!check(TokenType::Pipe) && !isAtEnd() && !check(TokenType::Let) &&
             !check(TokenType::Type) && !check(TokenType::Module)) {
        fields.push_back(parseTypeAtom());
      }

      constructors.push_back({conName, std::move(fields)});
    } while (match(TokenType::Pipe));

    typeDecl.rhs = std::move(constructors);
  } else {
    // Type alias
    typeDecl.rhs = parseType();
  }

  auto decl = std::make_unique<Decl>();
  decl->node = std::move(typeDecl);
  return decl;
}

ExprPtr Parser::parseExpression() {
  // Check for special expression forms first
  if (check(TokenType::Let)) {
    return parseLetExpr();
  }
  if (check(TokenType::Match)) {
    return parseMatchExpr();
  }
  if (check(TokenType::If)) {
    return parseIfExpr();
  }
  if (check(TokenType::Backslash)) {
    return parseLambdaExpr();
  }
  if (match(TokenType::Do)) {
    return parseBracedExpr(true);
  }

  // No special handling for '{' here; it will be handled in parseAtom

  // Otherwise parse as infix expression
  return parseInfixExpr();
}

ExprPtr Parser::parseNonBlockExpr() {
  // Like parseExpression but doesn't consume braces as blocks
  if (check(TokenType::Let)) {
    return parseLetExpr();
  }
  if (check(TokenType::Match)) {
    return parseMatchExpr();
  }
  if (check(TokenType::If)) {
    return parseIfExpr();
  }
  if (check(TokenType::Backslash)) {
    return parseLambdaExpr();
  }
  // NOTE: Don't check for LeftBrace here - that's the key difference!

  // Parse with stopAtBrace=true to prevent consuming braces as arguments
  ExprPtr expr = parseAtom();

  // Application parsing - but DON'T consume braces!
  while (!isAtEnd() &&
         (check(TokenType::Identifier) || check(TokenType::Constructor) ||
          check(TokenType::Integer) || check(TokenType::Float) || check(TokenType::String) ||
          check(TokenType::BoolTrue) || check(TokenType::BoolFalse) ||
          check(TokenType::LeftParen) || check(TokenType::LeftBracket) || check(TokenType::Bang))) {
    ExprPtr arg = parseAtom();
    App app;
    app.func = std::move(expr);
    app.arg = std::move(arg);
    expr = std::make_unique<Expr>(Expr{std::move(app)});
  }

  // Handle binary operators (simplified)
  if (this->match({TokenType::Plus,
                   TokenType::Minus,
                   TokenType::Star,
                   TokenType::Slash,
                   TokenType::Percent,
                   TokenType::EqualEqual,
                   TokenType::BangEqual,
                   TokenType::Less,
                   TokenType::Greater,
                   TokenType::LessEqual,
                   TokenType::GreaterEqual,
                   TokenType::AmpAmp,
                   TokenType::PipePipe,
                   TokenType::PlusPlus,
                   TokenType::Cons,
                   TokenType::Colon,
                   TokenType::PipeRight})) {
    std::string opLexeme = previous().lexeme;  // Save operator before recursive call!
    // Recursively parse right side (also stopping at braces)
    ExprPtr right = parseNonBlockExpr();
    BinOp binOp;
    binOp.op = opLexeme;
    binOp.left = std::move(expr);
    binOp.right = std::move(right);
    return std::make_unique<Expr>(Expr{std::move(binOp)});
  }

  return expr;
}

ExprPtr Parser::parseLetExpr() {
  consume(TokenType::Let, "Expected 'let'");

  Let let;
  let.pattern = parsePattern();
  consume(TokenType::Equal, "Expected '=' after pattern");
  let.value = parseExpression();

  if (match(TokenType::Semicolon)) {
    let.body = parseExpression();
  } else if (match(TokenType::In)) {
    let.body = parseExpression();
  } else {
    // Just the let binding, return unit (simplified)
    let.body = std::make_unique<Expr>(Expr{Lit{true}});
  }

  return std::make_unique<Expr>(Expr{std::move(let)});
}

ExprPtr Parser::parseMatchExpr() {
  consume(TokenType::Match, "Expected 'match'");

  Match match;
  match.scrutinee = parseNonBlockExpr();  // Use parseNonBlockExpr for scrutinee!

  consume(TokenType::LeftBrace, "Expected '{' after match scrutinee");

  if (!check(TokenType::RightBrace)) {
    do {
      PatternPtr pat = parsePattern();
      consume(TokenType::FatArrow, "Expected '=>' after pattern");
      ExprPtr expr = parseExpression();
      match.arms.push_back({std::move(pat), std::move(expr)});
    } while (this->match(TokenType::Comma));
  }

  consume(TokenType::RightBrace, "Expected '}' after match arms");

  return std::make_unique<Expr>(Expr{std::move(match)});
}

ExprPtr Parser::parseIfExpr() {
  consume(TokenType::If, "Expected 'if'");

  If ifExpr;
  ifExpr.cond = parseNonBlockExpr();

  // Support both syntaxes: if-then-else AND if {} else {}
  if (check(TokenType::Then)) {
    // Functional style: if cond then expr else expr
    consume(TokenType::Then, "Expected 'then'");
    ifExpr.thenBranch = parseNonBlockExpr();
    consume(TokenType::Else, "Expected 'else' after then branch");
    ifExpr.elseBranch = parseExpression();
  } else if (check(TokenType::LeftBrace)) {
    // C-style: if cond { expr } else { expr }
    consume(TokenType::LeftBrace, "Expected '{' or 'then' after if condition");
    ifExpr.thenBranch = parseExpression();
    consume(TokenType::RightBrace, "Expected '}' after then branch");
    consume(TokenType::Else, "Expected 'else' after then branch");
    consume(TokenType::LeftBrace, "Expected '{' after else");
    ifExpr.elseBranch = parseExpression();
    consume(TokenType::RightBrace, "Expected '}' after else branch");
  } else {
    error("Expected 'then' or '{' after if condition");
  }

  return std::make_unique<Expr>(Expr{std::move(ifExpr)});
}

ExprPtr Parser::parseLambdaExpr() {
  consume(TokenType::Backslash, "Expected '\\'");

  Lambda lambda;
  do {
    lambda.params.push_back(parsePattern());
  } while (!check(TokenType::Arrow));

  consume(TokenType::Arrow, "Expected '->' after lambda parameters");
  lambda.body = parseExpression();

  return std::make_unique<Expr>(Expr{std::move(lambda)});
}

ExprPtr Parser::parseBracedExpr(bool isDoBlock) {
  consume(TokenType::LeftBrace, "Expected '{'");

  // Empty record/block?
  if (match(TokenType::RightBrace)) {
    return std::make_unique<Expr>(Expr{Record{}});
  }

  // Check for Let explicitly to avoid consuming the rest of the block
  // If it starts with Let, it MUST be a block (do notation)
  if (check(TokenType::Let)) {
    Block block;
    block.isDoBlock = isDoBlock;
    block.stmts.push_back(parseBlockStatement());

    while (match(TokenType::Semicolon)) {
      if (check(TokenType::RightBrace))
        break;
      block.stmts.push_back(parseBlockStatement());
    }
    consume(TokenType::RightBrace, "Expected '}'");
    return std::make_unique<Expr>(Expr{std::move(block)});
  }

  // Parse first expression, but don't consume operators that might be <-
  // Use parseNonBlockExpr to stop early
  ExprPtr firstExpr = parseNonBlockExpr();

  // Check for Record Literal (Identifier = ...)
  if (check(TokenType::Equal)) {
    if (auto* var = std::get_if<Var>(&firstExpr->node)) {
      std::string fieldName = var->name;
      consume(TokenType::Equal, "Expected '='");

      Record record;
      record.fields[fieldName] = parseExpression();

      while (match(TokenType::Comma)) {
        std::string name = consume(TokenType::Identifier, "Expected field name").lexeme;
        consume(TokenType::Equal, "Expected '='");
        record.fields[name] = parseExpression();
      }

      consume(TokenType::RightBrace, "Expected '}'");
      return std::make_unique<Expr>(Expr{std::move(record)});
    } else {
      error("Expected field name before '=' in record literal");
    }
  }

  // Check for Record Update (Expr | ...)
  if (match(TokenType::Pipe)) {
    RecordUpdate update;
    update.record = std::move(firstExpr);

    do {
      std::string name = consume(TokenType::Identifier, "Expected field name").lexeme;
      consume(TokenType::Equal, "Expected '='");
      update.updates[name] = parseExpression();
    } while (match(TokenType::Comma));

    consume(TokenType::RightBrace, "Expected '}'");
    return std::make_unique<Expr>(Expr{std::move(update)});
  }

  // Check for Bind (pattern <- expr)
  // Check if firstExpr is a pattern and followed by <-
  if (match(TokenType::LeftArrow)) {
    // Try to convert firstExpr to a pattern
    // Currently only VarPat is supported
    PatternPtr pattern;
    if (auto* var = std::get_if<Var>(&firstExpr->node)) {
      VarPat vp;
      vp.name = var->name;
      pattern = std::make_unique<Pattern>(Pattern{vp});
    } else {
      error("Only variable patterns supported in bind");
    }

    // Parse the action expression
    ExprPtr action = parseExpression();

    // Parse the rest of the block as the body
    ExprPtr body;
    if (match(TokenType::Semicolon)) {
      if (check(TokenType::RightBrace)) {
        // No body, just return unit
        body = std::make_unique<Expr>(Expr{Lit{true}});
      } else {
        // Parse rest as expression
        // Bind is usually monadic bind >>=
        // Currently assumes it is an expression
        body = parseExpression();
      }
    } else {
      // No semicolon, body is unit
      body = std::make_unique<Expr>(Expr{Lit{true}});
    }

    consume(TokenType::RightBrace, "Expected '}'");

    Bind bind;
    bind.pattern = std::move(pattern);
    bind.value = std::move(action);
    bind.body = std::move(body);
    return std::make_unique<Expr>(Expr{std::move(bind)});
  }

  // Otherwise, it's a Block (Expr; Expr; ...)
  Block block;
  block.isDoBlock = isDoBlock;
  block.stmts.push_back(std::move(firstExpr));

  while (match(TokenType::Semicolon)) {
    if (check(TokenType::RightBrace))
      break;
    block.stmts.push_back(parseBlockStatement());
  }

  consume(TokenType::RightBrace, "Expected '}'");
  return std::make_unique<Expr>(Expr{std::move(block)});
}

ExprPtr Parser::parseBlockStatement() {
  if (match(TokenType::Let)) {
    PatternPtr pattern = parsePattern();
    consume(TokenType::Equal, "Expected '='");
    ExprPtr value = parseExpression();

    Let let;
    let.pattern = std::move(pattern);
    let.value = std::move(value);
    // Critical: Unit body so it doesn't consume the rest of the block
    let.body = std::make_unique<Expr>(Expr{Lit{true}});
    return std::make_unique<Expr>(Expr{std::move(let)});
  }
  return parseExpression();
}

ExprPtr Parser::parseInfixExpr() {
  return parseAppExpr();
}

ExprPtr Parser::parseAppExpr() {
  ExprPtr expr = parseAtom();

  // Handle postfix operators: function application and record field access
  while (!isAtEnd()) {
    // Record field access: expr.field
    if (match(TokenType::Dot)) {
      std::string fieldName = consume(TokenType::Identifier, "Expected field name after '.'").lexeme;
      RecordAccess access;
      access.record = std::move(expr);
      access.field = fieldName;
      expr = std::make_unique<Expr>(Expr{std::move(access)});
      continue;
    }
    
    // Function application
    if (check(TokenType::Identifier) || check(TokenType::Constructor) ||
        check(TokenType::Integer) || check(TokenType::Float) || check(TokenType::String) ||
        check(TokenType::BoolTrue) || check(TokenType::BoolFalse) ||
        check(TokenType::LeftParen) || check(TokenType::LeftBracket) ||
        check(TokenType::LeftBrace) || check(TokenType::Bang)) {
      ExprPtr arg = parseAtom();
      App app;
      app.func = std::move(expr);
      app.arg = std::move(arg);
      expr = std::make_unique<Expr>(Expr{std::move(app)});
      continue;
    }
    
    // No more postfix operators
    break;
  }

  // Handle binary operators (simplified)
  if (this->match({TokenType::Plus,
                   TokenType::Minus,
                   TokenType::Star,
                   TokenType::Slash,
                   TokenType::Percent,
                   TokenType::EqualEqual,
                   TokenType::BangEqual,
                   TokenType::Less,
                   TokenType::Greater,
                   TokenType::LessEqual,
                   TokenType::GreaterEqual,
                   TokenType::AmpAmp,
                   TokenType::PipePipe,
                   TokenType::PlusPlus,
                   TokenType::Cons,
                   TokenType::Colon,
                   TokenType::PipeRight})) {
    std::string opLexeme = previous().lexeme;  // Save operator before recursive call!
    ExprPtr right = parseAppExpr();
    BinOp binOp;
    binOp.op = opLexeme;
    binOp.left = std::move(expr);
    binOp.right = std::move(right);
    return std::make_unique<Expr>(Expr{std::move(binOp)});
  }

  return expr;
}

ExprPtr Parser::parseAtom() {
  // Handle negative number literals
  if (check(TokenType::Minus) && peekNext().type == TokenType::Integer) {
    advance();  // consume '-'
    auto value = std::get<int64_t>(advance().literal);
    return std::make_unique<Expr>(Expr{Lit{-value}});
  }

  // Handle negative BigInt literals
  if (check(TokenType::Minus) && peekNext().type == TokenType::BigIntLiteral) {
    advance();  // consume '-'
    auto value = std::get<BigInt>(advance().literal);
    return std::make_unique<Expr>(Expr{Lit{-value}});
  }

  if (check(TokenType::Minus) && peekNext().type == TokenType::Float) {
    advance();  // consume '-'
    auto value = std::get<double>(advance().literal);
    return std::make_unique<Expr>(Expr{Lit{-value}});
  }

  // Literals
  if (match(TokenType::Integer)) {
    auto value = std::get<int64_t>(previous().literal);
    return std::make_unique<Expr>(Expr{Lit{value}});
  }

  if (match(TokenType::BigIntLiteral)) {
    auto value = std::get<BigInt>(previous().literal);
    return std::make_unique<Expr>(Expr{Lit{value}});
  }

  if (match(TokenType::Float)) {
    auto value = std::get<double>(previous().literal);
    return std::make_unique<Expr>(Expr{Lit{value}});
  }

  if (match(TokenType::String)) {
    auto value = std::get<std::string>(previous().literal);
    return std::make_unique<Expr>(Expr{Lit{value}});
  }

  if (match(TokenType::BoolTrue)) {
    return std::make_unique<Expr>(Expr{Lit{true}});
  }

  if (match(TokenType::BoolFalse)) {
    return std::make_unique<Expr>(Expr{Lit{false}});
  }

  // Variables and constructors
  if (match({TokenType::Identifier, TokenType::Constructor})) {
    Var var;
    var.name = previous().lexeme;
    return std::make_unique<Expr>(Expr{std::move(var)});
  }

  // Parenthesized expression
  if (match(TokenType::LeftParen)) {
    ExprPtr expr = parseExpression();
    consume(TokenType::RightParen, "Expected ')' after expression");
    return expr;
  }

  // List literal
  if (check(TokenType::LeftBracket)) {
    return parseList();
  }

  // Record/Block
  if (check(TokenType::LeftBrace)) {
    return parseBracedExpr();
  }

  // Strictness
  if (match(TokenType::Bang)) {
    Strict strict;
    strict.expr = parseAtom();
    return std::make_unique<Expr>(Expr{std::move(strict)});
  }

  error("Expected expression");
}

ExprPtr Parser::parseList() {
  consume(TokenType::LeftBracket, "Expected '['");

  List list;
  if (!check(TokenType::RightBracket)) {
    do {
      list.elements.push_back(parseExpression());
    } while (match(TokenType::Comma));
  }

  consume(TokenType::RightBracket, "Expected ']' after list elements");

  return std::make_unique<Expr>(Expr{std::move(list)});
}

PatternPtr Parser::parsePattern() {
  PatternPtr left = parsePrimaryPattern();

  if (match(TokenType::Colon)) {
    ConsPat cons;
    cons.constructor = "::";
    cons.args.push_back(std::move(left));
    cons.args.push_back(parsePattern());
    return std::make_unique<Pattern>(Pattern{std::move(cons)});
  }

  return left;
}

PatternPtr Parser::parsePrimaryPattern() {
  // Wildcard
  if (match(TokenType::Identifier)) {
    if (previous().lexeme == "_") {
      return std::make_unique<Pattern>(Pattern{WildcardPat{}});
    }
    return std::make_unique<Pattern>(Pattern{VarPat{previous().lexeme}});
  }

  // Cons pattern (::)
  if (match(TokenType::Cons)) {
    ConsPat cons;
    cons.constructor = "::";

    // Arguments: :: pattern1 pattern2
    while (check(TokenType::Identifier) || check(TokenType::Constructor) ||
           check(TokenType::LeftParen) || check(TokenType::LeftBracket) || check(TokenType::Cons)) {
      cons.args.push_back(parsePrimaryPattern());
    }

    return std::make_unique<Pattern>(Pattern{std::move(cons)});
  }

  // Constructor pattern
  if (match(TokenType::Constructor)) {
    ConsPat cons;
    cons.constructor = previous().lexeme;

    // Arguments
    while (check(TokenType::Identifier) || check(TokenType::Constructor) ||
           check(TokenType::LeftParen) || check(TokenType::LeftBracket)) {
      cons.args.push_back(parsePrimaryPattern());
    }

    return std::make_unique<Pattern>(Pattern{std::move(cons)});
  }

  // Literal pattern
  if (match({TokenType::Integer,
             TokenType::Float,
             TokenType::String,
             TokenType::Char,
             TokenType::BoolTrue,
             TokenType::BoolFalse})) {
    auto literal = previous().literal;
    auto value = std::visit(
        [](auto&& arg) -> std::variant<int64_t, double, std::string, bool, BigInt> {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return (int64_t)0;
          } else {
            return arg;
          }
        },
        literal);
    return std::make_unique<Pattern>(Pattern{LitPat{std::move(value)}});
  }

  // Parenthesized pattern
  if (match(TokenType::LeftParen)) {
    PatternPtr pat = parsePattern();
    consume(TokenType::RightParen, "Expected ')' after pattern");
    return pat;
  }

  // List pattern
  if (match(TokenType::LeftBracket)) {
    ListPat list;

    if (!check(TokenType::RightBracket)) {
      do {
        list.elements.push_back(parsePattern());
      } while (match(TokenType::Comma));
    }

    consume(TokenType::RightBracket, "Expected ']' after list pattern");
    return std::make_unique<Pattern>(Pattern{std::move(list)});
  }

  // Record pattern
  if (match(TokenType::LeftBrace)) {
    RecordPat rp;
    if (!check(TokenType::RightBrace)) {
      do {
        std::string field = consume(TokenType::Identifier, "Expected field name").lexeme;
        consume(TokenType::Equal, "Expected '=' after field name");
        rp.fields[field] = parsePattern();
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightBrace, "Expected '}' after record pattern");
    return std::make_unique<Pattern>(Pattern{std::move(rp)});
  }

  error("Expected pattern");
}

TypePtr Parser::parseType() {
  if (match(TokenType::Forall)) {
    TyForall fa;
    while (match(TokenType::Identifier)) {
      fa.vars.push_back(previous().lexeme);
    }
    consume(TokenType::Dot, "Expected '.' after forall variables");
    fa.body = parseType();
    return std::make_unique<Type>(Type{std::move(fa)});
  }

  if (match(TokenType::Exists)) {
    TyExists ex;
    while (match(TokenType::Identifier)) {
      ex.vars.push_back(previous().lexeme);
    }
    consume(TokenType::Dot, "Expected '.' after exists variables");
    ex.body = parseType();
    return std::make_unique<Type>(Type{std::move(ex)});
  }

  return parseFunctionType();
}

TypePtr Parser::parseFunctionType() {
  TypePtr type = parseAppType();

  if (match(TokenType::Arrow)) {
    TyArr arr;
    arr.from = std::move(type);
    arr.to = parseFunctionType();  // Right-associative
    return std::make_unique<Type>(Type{std::move(arr)});
  }

  return type;
}

TypePtr Parser::parseAppType() {
  TypePtr type = parseTypeAtom();

  while (check(TokenType::Identifier) || check(TokenType::Constructor) ||
         check(TokenType::LeftParen) || check(TokenType::LeftBracket)) {
    TypePtr arg = parseTypeAtom();
    TyApp app;
    app.func = std::move(type);
    app.arg = std::move(arg);
    type = std::make_unique<Type>(Type{std::move(app)});
  }

  return type;
}

TypePtr Parser::parseTypeAtom() {
  if (match(TokenType::Identifier)) {
    return std::make_unique<Type>(Type{TyVar{-1, previous().lexeme}});
  }

  if (match(TokenType::Constructor)) {
    return std::make_unique<Type>(Type{TyCon{previous().lexeme, {}}});
  }

  if (match(TokenType::LeftParen)) {
    TypePtr type = parseType();
    consume(TokenType::RightParen, "Expected ')' after type");
    return type;
  }

  if (match(TokenType::LeftBracket)) {
    TypePtr elemType = parseType();
    consume(TokenType::RightBracket, "Expected ']' after list type");

    // List type is sugar for List a
    TyApp app;
    app.func = std::make_unique<Type>(Type{TyCon{"List", {}}});
    app.arg = std::move(elemType);
    return std::make_unique<Type>(Type{std::move(app)});
  }

  error("Expected type");
}

ExprPtr Parser::parseExpressionFromSource(const std::string& source) {
  Lexer lexer(source);
  auto tokens = lexer.tokenize();
  Parser parser(std::move(tokens));
  return parser.parseExpression();
}

// Trait declaration: trait Eq a where equals :: a -> a -> Bool
DeclPtr Parser::parseTraitDecl() {
  consume(TokenType::Trait, "Expected 'trait'");

  TraitDecl decl;
  decl.name = consume(TokenType::Constructor, "Expected trait name").lexeme;

  // Parse type parameters
  while (check(TokenType::Identifier)) {
    decl.typeParams.push_back(advance().lexeme);
  }

  consume(TokenType::Where, "Expected 'where' after trait name");

  // Parse method signatures
  // Simplified: methodName :: Type
  while (!isAtEnd() && !check(TokenType::Let) && !check(TokenType::Type) &&
         !check(TokenType::Trait) && !check(TokenType::Impl)) {
    if (check(TokenType::Identifier)) {
      std::string methodName = advance().lexeme;
      consume(TokenType::Colon, "Expected '::'");
      consume(TokenType::Colon, "Expected '::'");
      TypePtr methodType = parseType();
      decl.methods.push_back({methodName, std::move(methodType)});

      // Optional semicolon
      match(TokenType::Semicolon);
    } else {
      break;
    }
  }

  auto result = std::make_unique<Decl>();
  result->node = std::move(decl);
  return result;
}

// Impl declaration: impl Eq Int where equals = \a b -> a == b
DeclPtr Parser::parseImplDecl() {
  consume(TokenType::Impl, "Expected 'impl'");

  ImplDecl decl;
  TypePtr firstType = parseType();

  if (match(TokenType::LeftBrace)) {
    // Structural: impl Type { ... }
    decl.traitName = std::nullopt;
    decl.type = std::move(firstType);

    while (!check(TokenType::RightBrace) && !isAtEnd()) {
      if (check(TokenType::Let)) {
        DeclPtr methodDecl = parseFunctionDecl();
        if (auto* fd = std::get_if<FunctionDecl>(&methodDecl->node)) {
          decl.methods.push_back(std::move(*fd));
        }
      } else {
        error("Expected 'let' to start method definition in impl block");
      }
      match(TokenType::Semicolon);
    }
    consume(TokenType::RightBrace, "Expected '}' after impl block");
  } else {
    // Legacy: impl Trait Type where ...
    if (auto* tc = std::get_if<TyCon>(&firstType->node)) {
      decl.traitName = tc->name;
    } else {
      error("Expected trait name");
    }
    decl.type = parseType();
    consume(TokenType::Where, "Expected 'where'");

    while (!isAtEnd() && check(TokenType::Identifier)) {
      std::string methodName = advance().lexeme;
      consume(TokenType::Equal, "Expected '='");
      ExprPtr body = parseExpression();

      FunctionDecl method;
      method.name = methodName;
      method.body = std::move(body);
      decl.methods.push_back(std::move(method));

      match(TokenType::Semicolon);

      if (check(TokenType::Let) || check(TokenType::Type) || check(TokenType::Trait) ||
          check(TokenType::Impl)) {
        break;
      }
    }
  }

  auto result = std::make_unique<Decl>();
  result->node = std::move(decl);
  return result;
}

}  // namespace solis
