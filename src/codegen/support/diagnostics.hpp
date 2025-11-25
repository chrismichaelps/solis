// Solis Programming Language - Codegen Diagnostics
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace solis {

// Diagnostic severity levels
enum class DiagnosticLevel {
  Note,
  Warning,
  Error,
  Fatal
};

// Extended source location for diagnostics (includes filename)
struct DiagnosticLocation {
  std::string filename;
  int line = 0;
  int column = 0;

  DiagnosticLocation() = default;
  DiagnosticLocation(const std::string& file, int l, int c)
      : filename(file)
      , line(l)
      , column(c) {}
};

// Single diagnostic message
struct Diagnostic {
  DiagnosticLevel level;
  std::string message;
  DiagnosticLocation location;

  Diagnostic(DiagnosticLevel lvl, const std::string& msg)
      : level(lvl)
      , message(msg) {}

  Diagnostic(DiagnosticLevel lvl, const std::string& msg, const DiagnosticLocation& loc)
      : level(lvl)
      , message(msg)
      , location(loc) {}
};

// Professional diagnostic engine replaces throw runtime_error
class DiagnosticEngine {
private:
  std::vector<Diagnostic> diagnostics_;
  int errorCount_ = 0;
  int warningCount_ = 0;
  bool treatWarningsAsErrors_ = false;

public:
  DiagnosticEngine() = default;

  // Emit diagnostics
  void emitNote(const std::string& msg) {
    diagnostics_.emplace_back(DiagnosticLevel::Note, msg);
    report(diagnostics_.back());
  }

  void emitWarning(const std::string& msg) {
    warningCount_++;
    diagnostics_.emplace_back(DiagnosticLevel::Warning, msg);
    report(diagnostics_.back());

    if (treatWarningsAsErrors_) {
      errorCount_++;
    }
  }

  void emitError(const std::string& msg) {
    errorCount_++;
    diagnostics_.emplace_back(DiagnosticLevel::Error, msg);
    report(diagnostics_.back());
  }

  void emitFatal(const std::string& msg) {
    errorCount_++;
    diagnostics_.emplace_back(DiagnosticLevel::Fatal, msg);
    report(diagnostics_.back());
  }

  // With source locations
  void emitWarning(const std::string& msg, const DiagnosticLocation& loc) {
    warningCount_++;
    diagnostics_.emplace_back(DiagnosticLevel::Warning, msg, loc);
    report(diagnostics_.back());

    if (treatWarningsAsErrors_) {
      errorCount_++;
    }
  }

  void emitError(const std::string& msg, const DiagnosticLocation& loc) {
    errorCount_++;
    diagnostics_.emplace_back(DiagnosticLevel::Error, msg, loc);
    report(diagnostics_.back());
  }

  // Query state
  bool hasErrors() const { return errorCount_ > 0; }
  bool hasWarnings() const { return warningCount_ > 0; }
  int getErrorCount() const { return errorCount_; }
  int getWarningCount() const { return warningCount_; }

  // Configuration
  void setTreatWarningsAsErrors(bool enable) { treatWarningsAsErrors_ = enable; }

  // Get all diagnostics
  const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics_; }

  // Clear diagnostics
  void clear() {
    diagnostics_.clear();
    errorCount_ = 0;
    warningCount_ = 0;
  }

private:
  void report(const Diagnostic& diag) {
    std::ostream& out = (diag.level == DiagnosticLevel::Error ||
                         diag.level == DiagnosticLevel::Fatal)
                            ? std::cerr
                            : std::cout;

    // Format: filename:line:col: level: message
    if (!diag.location.filename.empty()) {
      out << diag.location.filename << ":" << diag.location.line << ":" << diag.location.column
          << ": ";
    }

    switch (diag.level) {
    case DiagnosticLevel::Note:
      out << "note: ";
      break;
    case DiagnosticLevel::Warning:
      out << "warning: ";
      break;
    case DiagnosticLevel::Error:
      out << "error: ";
      break;
    case DiagnosticLevel::Fatal:
      out << "fatal error: ";
      break;
    }

    out << diag.message << std::endl;
  }
};

}  // namespace solis
