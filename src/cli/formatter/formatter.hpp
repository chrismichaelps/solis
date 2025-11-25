// Solis Programming Language - Formatter Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "parser/ast.hpp"

#include <string>

namespace solis {

// Configuration for code formatting
struct FormatConfig {
  // Indentation: 2 spaces (K&R style)
  int indentSize = 2;
  int maxLineWidth = 80;

  // Brace style: K&R (opening brace on same line)
  // if () {} not if ()\n{}
  enum class BraceStyle {
    KAndR,
    Allman
  };
  BraceStyle braceStyle = BraceStyle::KAndR;

  // Spacing preferences
  bool spaceBeforeParen = false;
  bool spaceAroundOperators = true;
  bool alignPatternArrows = true;
  
  // Formatting modes
  enum class FormattingMode {
    Full,     // Format everything (default)
    Minimal   // Only format structure (indentation, braces), preserve spacing
  };
  FormattingMode mode = FormattingMode::Full;

  // Comma preferences
  enum class TrailingComma {
    Never,
    Always,
    WhereValid
  };
  TrailingComma trailingCommas = TrailingComma::WhereValid;

  static FormatConfig defaults() { return FormatConfig{}; }
};

// AST-based code formatter
class Formatter {
public:
  explicit Formatter(const FormatConfig& config = FormatConfig::defaults());

  // Format entire source code
  std::string format(const std::string& source);

  // Format individual AST nodes
  std::string formatExpr(const Expr& expr, int indentLevel = 0);
  std::string formatDecl(const Decl& decl, int indentLevel = 0);
  std::string formatPattern(const Pattern& pattern);

private:
  FormatConfig config_;

  // Helper methods
  std::string indent(int level) const;
  std::string indentStr() const;
  bool shouldBreakLine(const std::string& current, const std::string& addition) const;

  // Format specific node types
  std::string formatLambda(const Lambda& lam, int indentLevel);
  std::string formatApp(const App& app, int indentLevel);
  std::string formatLet(const Let& let, int indentLevel);
  std::string formatMatch(const Match& match, int indentLevel);
  std::string formatIf(const If& ifExpr, int indentLevel);
  std::string formatBinOp(const BinOp& op, int indentLevel);
  std::string formatList(const List& list, int indentLevel);
  std::string formatRecord(const Record& rec, int indentLevel);
  std::string formatRecordAccess(const RecordAccess& access, int indentLevel);
  std::string formatRecordUpdate(const RecordUpdate& update, int indentLevel);
  std::string formatBlock(const Block& block, int indentLevel);

  std::string formatFunctionDecl(const FunctionDecl& func, int indentLevel);
};

}  // namespace solis
