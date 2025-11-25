// Solis Programming Language - Code Formatter
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/formatter/formatter.hpp"

#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <sstream>

namespace solis {

Formatter::Formatter(const FormatConfig& config)
    : config_(config) {}

std::string Formatter::indent(int level) const {
  return std::string(level * config_.indentSize, ' ');
}

std::string Formatter::indentStr() const {
  return std::string(config_.indentSize, ' ');
}

bool Formatter::shouldBreakLine(const std::string& current, const std::string& addition) const {
  return (current.length() + addition.length()) > static_cast<size_t>(config_.maxLineWidth);
}

std::string Formatter::format(const std::string& source) {
  // Parse the source
  Lexer lexer(source);
  auto tokens = lexer.tokenize();
  Parser parser(std::move(tokens));

  std::ostringstream result;

  // Parse and format all declarations
  while (!parser.isAtEnd()) {
    auto decl = parser.parseDeclaration();
    result << formatDecl(*decl, 0);
    result << "\n\n";  // Double newline between declarations
  }

  return result.str();
}

std::string Formatter::formatDecl(const Decl& decl, int indentLevel) {
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDecl>) {
          return formatFunctionDecl(node, indentLevel);
        } else {
          // TypeDecl, ModuleDecl, ImportDecl use basic pretty printing
          return prettyPrint(decl);
        }
      },
      decl.node);
}

std::string Formatter::formatFunctionDecl(const FunctionDecl& func, int indentLevel) {
  std::ostringstream os;
  os << indent(indentLevel) << "let " << func.name;

  // Format parameters
  for (const auto& param : func.params) {
    os << " " << formatPattern(*param);
  }

  os << " = ";

  // Check if body is a Block (including do-blocks) - keep on same line
  bool isBlockBody = std::holds_alternative<Block>(func.body->node);

  // Format function body
  std::string body = formatExpr(*func.body, indentLevel);

  // Blocks always stay on same line; short bodies stay inline
  if (isBlockBody || (body.length() < 60 && body.find('\n') == std::string::npos)) {
    os << body;
  } else {
    // Multi-line body
    os << "\n" << indent(indentLevel + 1) << body;
  }

  return os.str();
}

std::string Formatter::formatPattern(const Pattern& pattern) {
  return prettyPrint(pattern);
}

std::string Formatter::formatExpr(const Expr& expr, int indentLevel) {
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, Var>) {
          return node.name;
        } else if constexpr (std::is_same_v<T, Lit>) {
          return std::visit(
              [](const auto& val) -> std::string {
                using V = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<V, int64_t>) {
                  return std::to_string(val);
                } else if constexpr (std::is_same_v<V, double>) {
                  return std::to_string(val);
                } else if constexpr (std::is_same_v<V, std::string>) {
                  return "\"" + val + "\"";
                } else if constexpr (std::is_same_v<V, bool>) {
                  return val ? "true" : "false";
                } else if constexpr (std::is_same_v<V, BigInt>) {
                  return val.toString() + "n";
                }
                return "";
              },
              node.value);
        } else if constexpr (std::is_same_v<T, Lambda>) {
          return formatLambda(node, indentLevel);
        } else if constexpr (std::is_same_v<T, App>) {
          return formatApp(node, indentLevel);
        } else if constexpr (std::is_same_v<T, Let>) {
          return formatLet(node, indentLevel);
        } else if constexpr (std::is_same_v<T, Match>) {
          return formatMatch(node, indentLevel);
        } else if constexpr (std::is_same_v<T, If>) {
          return formatIf(node, indentLevel);
        } else if constexpr (std::is_same_v<T, BinOp>) {
          return formatBinOp(node, indentLevel);
        } else if constexpr (std::is_same_v<T, List>) {
          return formatList(node, indentLevel);
        } else if constexpr (std::is_same_v<T, Record>) {
          return formatRecord(node, indentLevel);
        } else if constexpr (std::is_same_v<T, RecordAccess>) {
          return formatRecordAccess(node, indentLevel);
        } else if constexpr (std::is_same_v<T, RecordUpdate>) {
          return formatRecordUpdate(node, indentLevel);
        } else if constexpr (std::is_same_v<T, Block>) {
          return formatBlock(node, indentLevel);
        } else if constexpr (std::is_same_v<T, Strict>) {
          return "!" + formatExpr(*node.expr, indentLevel);
        }
        return "";
      },
      expr.node);
}

std::string Formatter::formatLambda(const Lambda& lam, int indentLevel) {
  std::ostringstream os;
  os << "\\";
  for (size_t i = 0; i < lam.params.size(); ++i) {
    if (i > 0)
      os << " ";
    os << formatPattern(*lam.params[i]);
  }
  os << " -> " << formatExpr(*lam.body, indentLevel);
  return os.str();
}

std::string Formatter::formatApp(const App& app, int indentLevel) {
  std::string func = formatExpr(*app.func, indentLevel);
  std::string arg = formatExpr(*app.arg, indentLevel);

  if (config_.spaceBeforeParen) {
    return func + " (" + arg + ")";
  } else {
    return func + " " + arg;
  }
}

std::string Formatter::formatLet(const Let& let, int indentLevel) {
  std::ostringstream os;
  os << "let " << formatPattern(*let.pattern) << " = " << formatExpr(*let.value, indentLevel);

  // Check if there's a body (let-in)
  if (let.body) {
    os << " in\n" << indent(indentLevel) << formatExpr(*let.body, indentLevel);
  }

  return os.str();
}

std::string Formatter::formatMatch(const Match& match, int indentLevel) {
  std::ostringstream os;

  // Apply brace style
  if (config_.braceStyle == FormatConfig::BraceStyle::KAndR) {
    os << "match " << formatExpr(*match.scrutinee, indentLevel) << " {\n";
  } else {
    os << "match " << formatExpr(*match.scrutinee, indentLevel) << "\n";
    os << indent(indentLevel) << "{\n";
  }

  for (size_t i = 0; i < match.arms.size(); ++i) {
    const auto& [pattern, expr] = match.arms[i];
    os << indent(indentLevel + 1) << formatPattern(*pattern);

    if (config_.alignPatternArrows) {
      os << " => ";
    } else {
      os << " => ";
    }

    os << formatExpr(*expr, indentLevel + 1);

    if (i < match.arms.size() - 1) {
      os << ",";
    }
    os << "\n";
  }

  os << indent(indentLevel) << "}";
  return os.str();
}

std::string Formatter::formatIf(const If& ifExpr, int indentLevel) {
  std::ostringstream os;

  // K&R style: if () {
  // Allman style: if ()\n{
  if (config_.braceStyle == FormatConfig::BraceStyle::KAndR) {
    os << "if " << formatExpr(*ifExpr.cond, indentLevel) << " {\n";
    os << indent(indentLevel + 1) << formatExpr(*ifExpr.thenBranch, indentLevel + 1) << "\n";
    os << indent(indentLevel) << "} else {\n";
    os << indent(indentLevel + 1) << formatExpr(*ifExpr.elseBranch, indentLevel + 1) << "\n";
    os << indent(indentLevel) << "}";
  } else {
    os << "if " << formatExpr(*ifExpr.cond, indentLevel) << "\n";
    os << indent(indentLevel) << "{\n";
    os << indent(indentLevel + 1) << formatExpr(*ifExpr.thenBranch, indentLevel + 1) << "\n";
    os << indent(indentLevel) << "}\n";
    os << indent(indentLevel) << "else\n";
    os << indent(indentLevel) << "{\n";
    os << indent(indentLevel + 1) << formatExpr(*ifExpr.elseBranch, indentLevel + 1) << "\n";
    os << indent(indentLevel) << "}";
  }

  return os.str();
}

std::string Formatter::formatBinOp(const BinOp& op, int indentLevel) {
  std::string left = formatExpr(*op.left, indentLevel);
  std::string right = formatExpr(*op.right, indentLevel);

  if (config_.spaceAroundOperators) {
    return left + " " + op.op + " " + right;
  } else {
    return left + op.op + right;
  }
}

std::string Formatter::formatList(const List& list, int indentLevel) {
  if (list.elements.empty()) {
    return "[]";
  }

  // Attempt inline formatting first
  std::ostringstream inline_os;
  inline_os << "[";
  for (size_t i = 0; i < list.elements.size(); ++i) {
    if (i > 0)
      inline_os << ", ";
    inline_os << formatExpr(*list.elements[i], indentLevel);
  }
  inline_os << "]";

  std::string inline_str = inline_os.str();
  if (inline_str.length() < 60) {
    return inline_str;
  }

  // Multi-line formatting for long lists
  std::ostringstream os;
  os << "[\n";
  for (size_t i = 0; i < list.elements.size(); ++i) {
    os << indent(indentLevel + 1) << formatExpr(*list.elements[i], indentLevel + 1);
    if (i < list.elements.size() - 1) {
      os << ",";
    }
    os << "\n";
  }
  os << indent(indentLevel) << "]";
  return os.str();
}

std::string Formatter::formatRecord(const Record& rec, int indentLevel) {
  if (rec.fields.empty()) {
    return "{}";
  }

  // Attempt inline formatting first
  std::ostringstream inline_os;
  inline_os << "{ ";
  bool first = true;
  for (const auto& [name, expr] : rec.fields) {
    if (!first)
      inline_os << ", ";
    first = false;
    inline_os << name << " = " << formatExpr(*expr, indentLevel);
  }
  inline_os << " }";

  std::string inline_str = inline_os.str();
  if (inline_str.length() < 60 && rec.fields.size() <= 2) {
    return inline_str;
  }

  // Multi-line formatting for large records
  std::ostringstream os;
  os << "{\n";
  size_t i = 0;
  for (const auto& [name, expr] : rec.fields) {
    os << indent(indentLevel + 1) << name << " = " << formatExpr(*expr, indentLevel + 1);
    if (i < rec.fields.size() - 1) {
      os << ",";
    }
    os << "\n";
    i++;
  }
  os << indent(indentLevel) << "}";
  return os.str();
}

std::string Formatter::formatRecordAccess(const RecordAccess& access, int indentLevel) {
  return formatExpr(*access.record, indentLevel) + "." + access.field;
}

std::string Formatter::formatRecordUpdate(const RecordUpdate& update, int indentLevel) {
  std::ostringstream os;
  os << "{ " << formatExpr(*update.record, indentLevel) << " | ";

  bool first = true;
  for (const auto& [name, expr] : update.updates) {
    if (!first)
      os << ", ";
    first = false;
    os << name << " = " << formatExpr(*expr, indentLevel);
  }

  os << " }";
  return os.str();
}

std::string Formatter::formatBlock(const Block& block, int indentLevel) {
  std::ostringstream os;

  // Format block with statements separated by semicolons
  if (block.isDoBlock) {
    os << "do {\n";
  } else {
    os << "{\n";
  }
  for (size_t i = 0; i < block.stmts.size(); ++i) {
    os << indent(indentLevel + 1) << formatExpr(*block.stmts[i], indentLevel + 1);
    if (i < block.stmts.size() - 1) {
      os << ";";
    }
    os << "\n";
  }
  os << indent(indentLevel) << "}";
  return os.str();
}

}  // namespace solis
