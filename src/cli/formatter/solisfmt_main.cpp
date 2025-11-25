// Solis Programming Language - Formatter Entry Point
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/formatter/formatter.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace solis;

// Print command-line usage information for the formatter
void printUsage(const char* progName) {
  std::cout << "Usage: " << progName << " [OPTIONS] [FILES...]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --stdout           Format to stdout instead of in-place\n";
  std::cout << "  --check            Check if files are formatted (exit 1 if not)\n";
  std::cout << "  --minimal          Minimal formatting (only structure, preserve spacing)\n";
  std::cout << "  -q, --quiet        Suppress output messages\n";
  std::cout << "  -h, --help         Show this help message\n";
  std::cout << "  --version          Show version\n\n";
  std::cout << "Examples:\n";
  std::cout << "  " << progName << " file.solis          # Format in-place (default)\n";
  std::cout << "  " << progName << " --minimal file.solis # Less aggressive formatting\n";
  std::cout << "  " << progName << " --stdout file.solis # Format to stdout\n";
  std::cout << "  " << progName << " --check src/        # Check all files\n";
}

std::string readFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) {
    throw std::runtime_error("Could not open file: " + filename);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Write string content to a file
// Throws on write failure
void writeFile(const std::string& filename, const std::string& content) {
  std::ofstream file(filename);
  if (!file) {
    throw std::runtime_error("Could not write file: " + filename);
  }
  file << content;
}

// Main entry point for Solis code formatter
// Formats Solis source files according to configured style rules
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  bool toStdout = false;
  bool checkOnly = false;
  bool quiet = false;
  bool minimal = false;
  std::vector<std::string> files;

  // Parse arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      return 0;
    } else if (arg == "--version") {
      std::cout << "solisfmt v0.1.0" << std::endl;
      return 0;
    } else if (arg == "--stdout") {
      toStdout = true;
    } else if (arg == "--check") {
      checkOnly = true;
    } else if (arg == "--minimal") {
      minimal = true;
    } else if (arg == "-q" || arg == "--quiet") {
      quiet = true;
    } else if (arg[0] != '-') {
      files.push_back(arg);
    } else {
      std::cerr << "Unknown option: " << arg << std::endl;
      return 1;
    }
  }

  if (files.empty()) {
    std::cerr << "Error: No files specified" << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  FormatConfig config = FormatConfig::defaults();
  if (minimal) {
    config.mode = FormatConfig::FormattingMode::Minimal;
    config.spaceAroundOperators = false;
  }
  
  Formatter formatter(config);
  bool anyChanges = false;

  for (const auto& filename : files) {
    try {
      std::string source = readFile(filename);
      std::string formatted = formatter.format(source);

      if (checkOnly) {
        if (source != formatted) {
          std::cout << filename << ": needs formatting" << std::endl;
          anyChanges = true;
        }
      } else if (toStdout) {
        // Output formatted code to stdout
        std::cout << formatted;
      } else {
        // Default: format in-place
        if (source != formatted) {
          writeFile(filename, formatted);
          if (!quiet) {
            std::cout << "Formatted: " << filename << std::endl;
          }
        } else {
          if (!quiet) {
            std::cout << "Already formatted: " << filename << std::endl;
          }
        }
      }

    } catch (const std::exception& e) {
      std::cerr << "Error formatting " << filename << ": " << e.what() << std::endl;
      return 1;
    }
  }

  if (checkOnly && anyChanges) {
    return 1;  // Exit with error if any files need formatting
  }

  return 0;
}
