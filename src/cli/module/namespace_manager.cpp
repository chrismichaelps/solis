// Solis Programming Language - Namespace Manager Implementation
// Author: Chris M. Perez
// License: MIT License (see LICENSE file)

#include "cli/module/namespace_manager.hpp"

#include "parser/lexer.hpp"
#include "parser/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace solis {

// Determine if symbol should be imported based on import/hide lists
// Returns true if symbol passes filtering rules
bool NamespaceManager::shouldImportSymbol(const std::string& symbolName,
                                          const std::vector<std::string>& importList,
                                          const std::vector<std::string>& hideList) const {
  // If hide list is specified, exclude those symbols
  if (!hideList.empty()) {
    return std::find(hideList.begin(), hideList.end(), symbolName) == hideList.end();
  }

  // If import list is specified, include only those symbols
  if (!importList.empty()) {
    return std::find(importList.begin(), importList.end(), symbolName) != importList.end();
  }

  // Otherwise, import everything
  return true;
}

// Register import and add symbols to qualified/unqualified namespaces
// Validates selective imports and handles qualified-only imports
void NamespaceManager::addImport(const ImportDecl& import, const std::vector<Symbol>& symbols) {
  std::string qualifier = import.alias.value_or(import.moduleName);

  // Validate selective imports - all names must exist
  if (!import.imports.empty()) {
    for (const auto& importName : import.imports) {
      auto it = std::find_if(symbols.begin(), symbols.end(), [&](const Symbol& s) {
        return s.name == importName;
      });
      if (it == symbols.end()) {
        throw std::runtime_error("Cannot import '" + importName + "' from module '" +
                                 import.moduleName + "': symbol not found");
      }
    }
  }

  for (const auto& symbol : symbols) {
    // Check if symbol should be imported based on import/hide lists
    if (!shouldImportSymbol(symbol.name, import.imports, import.hiding)) {
      continue;
    }

    // Add to qualified namespace (always available for qualified imports)
    qualifiedSymbols_[qualifier][symbol.name] = symbol;

    // Add to unqualified namespace if not a qualified-only import
    if (!import.qualified) {
      unqualifiedSymbols_[symbol.name].push_back(symbol);
    }
  }
}

// Look up unqualified symbol in namespace
// Returns first match or nullopt if not found
// Ambiguous symbols return first match with warning
std::optional<NamespaceManager::Symbol> NamespaceManager::lookup(const std::string& name) const {
  auto it = unqualifiedSymbols_.find(name);

  if (it == unqualifiedSymbols_.end() || it->second.empty()) {
    return std::nullopt;
  }

  // If multiple modules export the same symbol unqualified, it's ambiguous
  if (it->second.size() > 1) {
    // Return first with warning - caller can detect ambiguity via isAmbiguous()
    std::cerr << "Warning: Ambiguous symbol '" << name << "' imported from multiple modules"
              << std::endl;
  }

  return it->second[0];
}

// Look up qualified symbol (Module.Symbol)
// Returns symbol if found in specified module, nullopt otherwise
std::optional<NamespaceManager::Symbol> NamespaceManager::lookupQualified(
    const std::string& qualifier, const std::string& name) const {
  auto modIt = qualifiedSymbols_.find(qualifier);
  if (modIt == qualifiedSymbols_.end()) {
    return std::nullopt;
  }

  auto symIt = modIt->second.find(name);
  if (symIt == modIt->second.end()) {
    return std::nullopt;
  }

  return symIt->second;
}

// Check if unqualified symbol is ambiguous (imported from multiple modules)
bool NamespaceManager::isAmbiguous(const std::string& name) const {
  auto it = unqualifiedSymbols_.find(name);
  return it != unqualifiedSymbols_.end() && it->second.size() > 1;
}

// Get list of modules that export the specified symbol
std::vector<std::string> NamespaceManager::getModulesExporting(const std::string& name) const {
  std::vector<std::string> modules;

  auto it = unqualifiedSymbols_.find(name);
  if (it != unqualifiedSymbols_.end()) {
    for (const auto& symbol : it->second) {
      modules.push_back(symbol.moduleName);
    }
  }

  return modules;
}

// Register module in catalog for smart import suggestions
void NamespaceManager::registerModuleCatalog(const std::string& moduleName,
                                             const std::vector<Symbol>& symbols) {
  for (const auto& symbol : symbols) {
    moduleCatalog_[moduleName][symbol.name] = symbol;
  }
}

// Suggest modules that export the specified symbol
// Used for error messages and auto-import suggestions
std::vector<std::string> NamespaceManager::suggestImportsFor(const std::string& symbolName) const {
  std::vector<std::string> suggestions;

  for (const auto& [moduleName, symbols] : moduleCatalog_) {
    if (symbols.count(symbolName) > 0) {
      suggestions.push_back(moduleName);
    }
  }

  return suggestions;
}

// Scan search paths for .solis files and build module catalog
// Extracts module declarations and exported symbols for import suggestions
void NamespaceManager::scanAvailableModules(const std::vector<std::string>& searchPaths) {
  for (const auto& basePath : searchPaths) {
    if (!std::filesystem::exists(basePath) || !std::filesystem::is_directory(basePath)) {
      continue;
    }

    // Recursively scan for .solis files

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             basePath, std::filesystem::directory_options::skip_permission_denied)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".solis") {
        continue;
      }


      try {
        // Read the file
        std::ifstream file(entry.path());
        if (!file.is_open())
          continue;

        std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        // Parse to extract module declaration
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        auto module = parser.parseModule();

        // Skip files without module declarations
        if (!module.moduleDecl)
          continue;

        std::string moduleName = module.moduleDecl->name;
        std::vector<Symbol> symbols;

        // Extract function declarations as potential exports
        for (const auto& decl : module.declarations) {
          if (auto* funcDecl = std::get_if<FunctionDecl>(&decl->node)) {
            // Check if exported (if export list exists)
            bool isExported = true;
            if (!module.moduleDecl->exports.empty()) {
              auto& exports = module.moduleDecl->exports;
              isExported = std::find(exports.begin(), exports.end(), funcDecl->name) !=
                           exports.end();
            }

            if (isExported) {
              Symbol symbol;
              symbol.name = funcDecl->name;
              symbol.moduleName = moduleName;
              symbol.value = nullptr;  // No runtime value yet
              symbol.isExported = true;
              symbols.push_back(symbol);
            }
          }
        }

        // Add to catalog
        if (!symbols.empty()) {
          for (const auto& symbol : symbols) {
            moduleCatalog_[moduleName][symbol.name] = symbol;
          }
        }

      } catch (const std::exception& e) {
        // Skip files that fail to parse
        // std::cerr << "Warning: Could not parse " << entry.path()
        // << ": " << e.what() << std::endl;
      }
    }
  }

  // Report catalog contents
  for (const auto& [modName, symbols] : moduleCatalog_) {
    if (modName == "MathUtils" || modName == "StringUtils") {
      for ([[maybe_unused]] const auto& [symName, _] : symbols) {
      }
    }
  }
}

// Debug: dump namespace manager state to stdout
void NamespaceManager::dump() const {
  std::cout << "\n=== Namespace Manager State ===" << std::endl;

  std::cout << "\nQualified symbols:" << std::endl;
  for (const auto& [qualifier, symbols] : qualifiedSymbols_) {
    std::cout << "  " << qualifier << ":" << std::endl;
    for (const auto& [name, _] : symbols) {
      std::cout << "    - " << name << std::endl;
    }
  }

  std::cout << "\nUnqualified symbols:" << std::endl;
  for (const auto& [name, symbols] : unqualifiedSymbols_) {
    std::cout << "  " << name << " (" << symbols.size() << " source(s)";
    if (symbols.size() > 1) {
      std::cout << " - AMBIGUOUS";
    }
    std::cout << ")" << std::endl;
  }

  std::cout << "=== End Namespace ===" << std::endl;
}

}  // namespace solis
