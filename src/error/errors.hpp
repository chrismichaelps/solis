// Solis Programming Language - Error Types Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "parser/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace solis {

// Source location in a file
// SourceLocation is now defined in ast.hpp

// Inference step for tracking type derivation
struct InferenceStep {
  std::string reason;       // "from literal", "from function call", "from parameter", etc.
  std::string typeString;   // String representation of the type
  SourceLocation location;  // Where this inference happened
  std::string context;      // Additional context (variable name, function name, etc.)

  InferenceStep() = default;

  InferenceStep(std::string r,
                std::string t,
                SourceLocation loc = SourceLocation(),
                std::string ctx = "")
      : reason(std::move(r))
      , typeString(std::move(t))
      , location(loc)
      , context(std::move(ctx)) {}
};

// Inference chain for showing how a type was derived
struct InferenceChain {
  std::vector<InferenceStep> steps;

  void addStep(InferenceStep step) { steps.push_back(std::move(step)); }

  void addStep(std::string reason,
               std::string type,
               SourceLocation loc = SourceLocation(),
               std::string context = "") {
    steps.emplace_back(std::move(reason), std::move(type), loc, std::move(context));
  }

  // Format for display
  std::string format() const;
};

// Suggested fix for an error
struct ErrorSuggestion {
  std::string description;
  std::string code;
  std::optional<SourceLocation> location;

  ErrorSuggestion(std::string desc,
                  std::string c = "",
                  std::optional<SourceLocation> loc = std::nullopt)
      : description(std::move(desc))
      , code(std::move(c))
      , location(loc) {}
};

// Error categories
enum class ErrorCategory {
  TypeError,
  SyntaxError,
  NameError,
  PatternMatchError,
  EvaluationError
};

// Detailed type mismatch information
struct TypeMismatchDetails {
  std::string expectedType;
  std::string actualType;
  std::string location;                  // "parameter", "return type", "list element", etc.
  std::vector<std::string> differences;  // Specific points of divergence

  TypeMismatchDetails() = default;

  TypeMismatchDetails(std::string exp, std::string act, std::string loc = "")
      : expectedType(std::move(exp))
      , actualType(std::move(act))
      , location(std::move(loc)) {}

  void addDifference(std::string diff) { differences.push_back(std::move(diff)); }

  // Format for display
  std::string format() const;
};

// Rich error with context and suggestions
class SolisError : public std::exception {
private:
  ErrorCategory category_;
  std::string title_;
  std::string explanation_;
  std::string sourceCode_;
  SourceLocation location_;
  std::vector<ErrorSuggestion> suggestions_;
  std::vector<std::string> relatedInfo_;
  std::optional<TypeMismatchDetails> typeMismatch_;
  std::optional<InferenceChain> expectedChain_;  // How expected type was inferred
  std::optional<InferenceChain> actualChain_;    // How actual type was inferred
  mutable std::string cachedMessage_;

public:
  SolisError(ErrorCategory cat, std::string title, SourceLocation loc = SourceLocation())
      : category_(cat)
      , title_(std::move(title))
      , location_(loc) {}

  // Setters for builder pattern
  SolisError& setExplanation(std::string expl) {
    explanation_ = std::move(expl);
    return *this;
  }

  SolisError& setSourceCode(std::string source) {
    sourceCode_ = std::move(source);
    return *this;
  }

  SolisError& addSuggestion(std::string desc, std::string code = "") {
    suggestions_.emplace_back(std::move(desc), std::move(code));
    return *this;
  }

  SolisError& addRelatedInfo(std::string info) {
    relatedInfo_.push_back(std::move(info));
    return *this;
  }

  SolisError& setTypeMismatch(TypeMismatchDetails details) {
    typeMismatch_ = std::move(details);
    return *this;
  }

  SolisError& setExpectedChain(InferenceChain chain) {
    expectedChain_ = std::move(chain);
    return *this;
  }

  SolisError& setActualChain(InferenceChain chain) {
    actualChain_ = std::move(chain);
    return *this;
  }

  // Exception interface
  const char* what() const noexcept override;

  // Display formatted error
  std::string display() const;

  // JSON output for IDE/LSP integration
  std::string toJSON() const;

  // Getters
  ErrorCategory category() const { return category_; }
  const std::string& title() const { return title_; }
  const std::string& explanation() const { return explanation_; }
  const SourceLocation& location() const { return location_; }
};

// Error collector for gathering multiple errors
class ErrorCollector {
private:
  std::vector<SolisError> errors_;
  std::vector<SolisError> warnings_;
  bool stopOnFirstError_ = false;

public:
  ErrorCollector(bool stopOnFirst = false)
      : stopOnFirstError_(stopOnFirst) {}

  // Add error (throws if stopOnFirstError is true)
  void addError(SolisError error) {
    if (stopOnFirstError_ && !errors_.empty()) {
      throw errors_.back();
    }
    errors_.push_back(std::move(error));
    if (stopOnFirstError_) {
      throw errors_.back();
    }
  }

  // Add warning (never throws)
  void addWarning(SolisError warning) { warnings_.push_back(std::move(warning)); }

  // Check if any errors
  bool hasErrors() const { return !errors_.empty(); }
  bool hasWarnings() const { return !warnings_.empty(); }

  // Get counts
  size_t errorCount() const { return errors_.size(); }
  size_t warningCount() const { return warnings_.size(); }

  // Get errors/warnings
  const std::vector<SolisError>& errors() const { return errors_; }
  const std::vector<SolisError>& warnings() const { return warnings_; }

  // Display all errors and warnings
  void displayAll() const;

  // JSON output for IDE/LSP
  std::string toJSON() const;

  // Clear all
  void clear() {
    errors_.clear();
    warnings_.clear();
  }

  // Throw first error if any
  void throwIfErrors() const {
    if (!errors_.empty()) {
      throw errors_.front();
    }
  }
};

// Utility functions
namespace errors {

// Find similar names using Levenshtein distance
std::vector<std::string> findSimilarNames(const std::string& target,
                                          const std::vector<std::string>& candidates,
                                          int maxDistance = 2);

// Extract source context around a location
std::string extractSourceContext(const std::string& source, int line, int contextLines = 2);

// Smart suggestion generators
namespace suggestions {

// Suggest type conversions
std::vector<std::string> suggestTypeConversions(const std::string& fromType,
                                                const std::string& toType);

// Suggest when pattern will never match
std::vector<std::string> suggestPatternFix(const std::string& patternType,
                                           const std::string& scrutineeType);

// Suggest when function application is wrong
std::vector<std::string> suggestFunctionApplication(const std::string& functionType,
                                                    int expectedParams,
                                                    int actualParams);

// Suggest missing import
std::vector<std::string> suggestImport(const std::string& undefinedName);

}  // namespace suggestions

// ANSI color codes
extern const char* RED;
extern const char* YELLOW;
extern const char* GREEN;
extern const char* CYAN;
extern const char* MAGENTA;
extern const char* BOLD;
extern const char* DIM;
extern const char* RESET;

}  // namespace errors

}  // namespace solis
