// Solis Programming Language - Error Handling
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "error/errors.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace solis {

// Format type mismatch details
std::string TypeMismatchDetails::format() const {
  std::ostringstream oss;

  oss << "\n  " << errors::CYAN << "Expected: " << errors::RESET << errors::BOLD << expectedType
      << errors::RESET << "\n";
  oss << "  " << errors::CYAN << "Actual:   " << errors::RESET << errors::BOLD << actualType
      << errors::RESET << "\n";

  if (!location.empty()) {
    oss << "\n  " << errors::DIM << "Location: " << location << errors::RESET << "\n";
  }

  if (!differences.empty()) {
    oss << "\n  " << errors::RED << "[WARNING] Differences:" << errors::RESET << "\n";
    for (const auto& diff : differences) {
      oss << "    " << errors::DIM << "•" << errors::RESET << " " << diff << "\n";
    }
  }

  return oss.str();
}

// Format inference chain
std::string InferenceChain::format() const {
  if (steps.empty())
    return "";

  std::ostringstream oss;

  for (size_t i = 0; i < steps.size(); ++i) {
    const auto& step = steps[i];

    // Indentation for chain effect
    if (i > 0) {
      oss << "  " << errors::DIM << "  -> " << errors::RESET;
    } else {
      oss << "  " << errors::BOLD;
    }

    // Type
    oss << errors::CYAN << step.typeString << errors::RESET;

    // Reason
    if (!step.reason.empty()) {
      oss << " " << errors::DIM << "(" << step.reason;
      if (!step.context.empty()) {
        oss << " '" << step.context << "'";
      }
      if (step.location.line > 0) {
        oss << " at line " << step.location.line;
      }
      oss << ")" << errors::RESET;
    }

    oss << "\n";
  }

  return oss.str();
}

namespace errors {

// ANSI color codes
const char* RED = "\033[31m";
const char* YELLOW = "\033[33m";
const char* GREEN = "\033[32m";
const char* CYAN = "\033[36m";
const char* MAGENTA = "\033[35m";
const char* BOLD = "\033[1m";
const char* DIM = "\033[2m";
const char* RESET = "\033[0m";

// Compute Levenshtein distance between two strings
int levenshteinDistance(const std::string& s1, const std::string& s2) {
  const size_t len1 = s1.size(), len2 = s2.size();
  std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));

  for (size_t i = 0; i <= len1; ++i)
    d[i][0] = i;
  for (size_t j = 0; j <= len2; ++j)
    d[0][j] = j;

  for (size_t i = 1; i <= len1; ++i) {
    for (size_t j = 1; j <= len2; ++j) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      d[i][j] = std::min({
          d[i - 1][j] + 1,        // deletion
          d[i][j - 1] + 1,        // insertion
          d[i - 1][j - 1] + cost  // substitution
      });
    }
  }

  return d[len1][len2];
}

std::vector<std::string> findSimilarNames(const std::string& target,
                                          const std::vector<std::string>& candidates,
                                          int maxDistance) {
  std::vector<std::pair<int, std::string>> scored;

  for (const auto& candidate : candidates) {
    int dist = levenshteinDistance(target, candidate);
    if (dist <= maxDistance) {
      scored.emplace_back(dist, candidate);
    }
  }

  // Sort by distance (closest first)
  std::sort(scored.begin(), scored.end());

  std::vector<std::string> result;
  for (const auto& [dist, name] : scored) {
    result.push_back(name);
  }

  return result;
}

std::string extractSourceContext(const std::string& source, int targetLine, int contextLines) {
  std::istringstream iss(source);
  std::string line;
  std::vector<std::string> lines;

  while (std::getline(iss, line)) {
    lines.push_back(line);
  }

  std::ostringstream result;
  int startLine = std::max(1, targetLine - contextLines);
  int endLine = std::min(static_cast<int>(lines.size()), targetLine + contextLines);

  for (int i = startLine; i <= endLine; ++i) {
    result << i << " │ " << lines[i - 1] << "\n";
  }

  return result.str();
}

// Smart suggestion generators
namespace suggestions {

std::vector<std::string> suggestTypeConversions(const std::string& fromType,
                                                const std::string& toType) {
  std::vector<std::string> hints;

  // String <-> Int
  if (fromType == "String" && toType == "Int") {
    hints.push_back("Convert String to Int using 'parseInt'");
    hints.push_back("Example: parseInt \"123\"");
  } else if (fromType == "Int" && toType == "String") {
    hints.push_back("Convert Int to String using 'toString'");
    hints.push_back("Example: toString 42");
  }

  // String <-> Bool
  else if (fromType == "String" && toType == "Bool") {
    hints.push_back("Convert String to Bool by comparison");
    hints.push_back("Example: str == \"true\"");
  } else if (fromType == "Bool" && toType == "String") {
    hints.push_back("Use 'if' expression to convert Bool to String");
    hints.push_back("Example: if condition then \"yes\" else \"no\"");
  }

  // Int <-> Bool
  else if (fromType == "Int" && toType == "Bool") {
    hints.push_back("Compare Int with 0");
    hints.push_back("Example: n > 0  or  n /= 0");
  } else if (fromType == "Bool" && toType == "Int") {
    hints.push_back("Use 'if' expression to convert Bool to Int");
    hints.push_back("Example: if condition then 1 else 0");
  }

  // List operations
  else if (fromType.find("[") == 0 && toType.find("[") != 0) {
    hints.push_back("Did you mean to get an element from the list?");
    hints.push_back("Use pattern matching or indexing");
  } else if (fromType.find("[") != 0 && toType.find("[") == 0) {
    hints.push_back("Wrap single value in a list");
    hints.push_back("Example: [value]");
  }

  // Function types
  else if (fromType.find("->") != std::string::npos || toType.find("->") != std::string::npos) {
    hints.push_back("Function type mismatch");
    hints.push_back("Check parameter and return types match expected signature");
  }

  return hints;
}

std::vector<std::string> suggestPatternFix(const std::string& patternType,
                                           const std::string& scrutineeType) {
  std::vector<std::string> hints;

  if (patternType != scrutineeType) {
    hints.push_back("Pattern type '" + patternType + "' will never match scrutinee type '" +
                    scrutineeType + "'");
    hints.push_back("This pattern is unreachable");
  }

  return hints;
}

std::vector<std::string> suggestFunctionApplication(const std::string& functionType,
                                                    int expectedParams,
                                                    int actualParams) {
  std::vector<std::string> hints;

  if (actualParams < expectedParams) {
    hints.push_back("Function expects " + std::to_string(expectedParams) +
                    " parameter(s) but got " + std::to_string(actualParams));
    hints.push_back("Did you forget to pass " + std::to_string(expectedParams - actualParams) +
                    " argument(s)?");
  } else if (actualParams > expectedParams) {
    hints.push_back("Too many arguments provided");
    hints.push_back("Function type: " + functionType);
    hints.push_back("Did you mean to call a different function?");
  }

  return hints;
}

std::vector<std::string> suggestImport(const std::string& undefinedName) {
  std::vector<std::string> hints;

  // Common prelude functions
  std::vector<std::string> preludeFunctions = {"map",
                                               "filter",
                                               "fold",
                                               "length",
                                               "head",
                                               "tail",
                                               "reverse",
                                               "concat",
                                               "take",
                                               "drop",
                                               "zip"};

  for (const auto& func : preludeFunctions) {
    if (func == undefinedName) {
      hints.push_back("'" + undefinedName + "' is in the Prelude");
      hints.push_back("Did you forget to import the Prelude?");
      hints.push_back("Add: import Prelude");
      return hints;
    }
  }

  hints.push_back("'" + undefinedName + "' is not defined");
  hints.push_back("Check spelling or import the module containing it");

  return hints;
}

}  // namespace suggestions

}  // namespace errors

const char* SolisError::what() const noexcept {
  if (cachedMessage_.empty()) {
    cachedMessage_ = display();
  }
  return cachedMessage_.c_str();
}

std::string SolisError::display() const {
  std::ostringstream oss;

  // Error icon and title
  oss << "\n" << errors::RED << errors::BOLD << "";

  switch (category_) {
  case ErrorCategory::TypeError:
    oss << "Type Error";
    break;
  case ErrorCategory::SyntaxError:
    oss << "Syntax Error";
    break;
  case ErrorCategory::NameError:
    oss << "Name Error";
    break;
  case ErrorCategory::PatternMatchError:
    oss << "Pattern Match Error";
    break;
  case ErrorCategory::EvaluationError:
    oss << "Evaluation Error";
    break;
  }

  oss << errors::RESET << ": " << title_ << "\n\n";

  // Location
  if (location_.line > 0) {
    oss << "  " << errors::DIM;
    oss << location_.line << ":" << location_.column;
    oss << errors::RESET << "\n\n";
  }

  // Source context
  if (!sourceCode_.empty() && location_.line > 0) {
    std::istringstream iss(sourceCode_);
    std::string line;
    int currentLine = 0;
    int startLine = std::max(1, location_.line - 2);
    int endLine = location_.line + 2;

    while (std::getline(iss, line)) {
      currentLine++;

      if (currentLine >= startLine && currentLine <= endLine) {
        // Line number
        oss << "  " << errors::DIM;
        if (currentLine < 10)
          oss << " ";
        oss << currentLine << " │ " << errors::RESET;

        // Line content
        if (currentLine == location_.line) {
          oss << errors::BOLD << line << errors::RESET << "\n";

          // Caret marker
          oss << "     │ ";
          for (int i = 0; i < location_.column - 1; ++i) {
            oss << " ";
          }
          oss << errors::RED << errors::BOLD;
          for (int i = 0; i < std::max(1, location_.endColumn - location_.column); ++i) {
            oss << "^";
          }
          oss << errors::RESET << "\n";
        } else {
          oss << line << "\n";
        }
      }
    }

    oss << "\n";
  }

  // Explanation
  if (!explanation_.empty()) {
    oss << "  " << errors::CYAN << " " << errors::RESET << explanation_ << "\n\n";
  }

  // Type mismatch details
  if (typeMismatch_.has_value()) {
    oss << typeMismatch_->format() << "\n";
  }

  // Inference chains - show how types were derived
  if (expectedChain_.has_value() || actualChain_.has_value()) {
    oss << "  " << errors::MAGENTA << "📊 Type Derivation:" << errors::RESET << "\n\n";

    if (expectedChain_.has_value()) {
      oss << "  " << errors::DIM << "Expected type derived from:" << errors::RESET << "\n";
      oss << expectedChain_->format() << "\n";
    }

    if (actualChain_.has_value()) {
      oss << "  " << errors::DIM << "Actual type derived from:" << errors::RESET << "\n";
      oss << actualChain_->format() << "\n";
    }
  }

  // Suggestions
  if (!suggestions_.empty()) {
    oss << "  " << errors::GREEN << "Suggestions:" << errors::RESET << "\n\n";

    for (size_t i = 0; i < suggestions_.size(); ++i) {
      oss << "  " << (i + 1) << ". " << suggestions_[i].description << "\n";

      if (!suggestions_[i].code.empty()) {
        oss << "\n     " << errors::DIM << suggestions_[i].code << errors::RESET << "\n";
      }

      oss << "\n";
    }
  }

  // Related info
  if (!relatedInfo_.empty()) {
    oss << "  " << errors::YELLOW << "ℹ️  Related:" << errors::RESET << "\n";
    for (const auto& info : relatedInfo_) {
      oss << "      • " << info << "\n";
    }
    oss << "\n";
  }

  return oss.str();
}

std::string SolisError::toJSON() const {
  std::ostringstream json;
  json << "{\n";
  json << "  \"severity\": \"error\",\n";
  json << "  \"message\": \"" << title_ << "\",\n";
  if (location_.line > 0) {
    json << "  \"range\": {\n";
    json << "    \"start\": {\"line\": " << (location_.line - 1)
         << ", \"character\": " << (location_.column - 1) << "},\n";
    json << "    \"end\": {\"line\": "
         << (location_.endLine > 0 ? location_.endLine - 1 : location_.line - 1)
         << ", \"character\": "
         << (location_.endColumn > 0 ? location_.endColumn - 1 : location_.column - 1) << "}\n";
    json << "  },\n";
  }
  json << "  \"source\": \"solis\"\n";
  json << "}";
  return json.str();
}

void ErrorCollector::displayAll() const {
  if (errors_.empty() && warnings_.empty()) {
    return;
  }

  std::cout << "\n";

  // Display all errors
  for (size_t i = 0; i < errors_.size(); ++i) {
    std::cout << errors_[i].display();

    // Separator between errors (except after last)
    if (i < errors_.size() - 1) {
      std::cout << errors::DIM << "────────────────────────────────────\n\n" << errors::RESET;
    }
  }

  // Display all warnings
  if (!warnings_.empty()) {
    if (!errors_.empty()) {
      std::cout << "\n"
                << errors::DIM << "════════════════════════════════════\n\n"
                << errors::RESET;
    }

    for (size_t i = 0; i < warnings_.size(); ++i) {
      std::cout << warnings_[i].display();

      if (i < warnings_.size() - 1) {
        std::cout << errors::DIM << "────────────────────────────────────\n\n" << errors::RESET;
      }
    }
  }

  // Summary
  std::cout << "\n" << errors::BOLD;
  if (errorCount() > 0) {
    std::cout << errors::RED << "[ERROR] " << errorCount() << " error"
              << (errorCount() == 1 ? "" : "s");
  }
  if (warningCount() > 0) {
    if (errorCount() > 0)
      std::cout << ", ";
    std::cout << errors::YELLOW << "[WARNING] " << warningCount() << " warning"
              << (warningCount() == 1 ? "" : "s");
  }
  std::cout << errors::RESET << "\n\n";
}

std::string ErrorCollector::toJSON() const {
  std::ostringstream json;

  json << "{\n";
  json << "  \"diagnostics\": [\n";

  // All errors
  for (size_t i = 0; i < errors_.size(); ++i) {
    json << "    " << errors_[i].toJSON();
    if (i < errors_.size() - 1 || !warnings_.empty()) {
      json << ",";
    }
    json << "\n";
  }

  // All warnings (with severity "warning")
  for (size_t i = 0; i < warnings_.size(); ++i) {
    // Warnings would need their JSON modified to say "warning" severity
    json << "    " << warnings_[i].toJSON();
    if (i < warnings_.size() - 1) {
      json << ",";
    }
    json << "\n";
  }

  json << "  ]\n";
  json << "}";

  return json.str();
}

}  // namespace solis
