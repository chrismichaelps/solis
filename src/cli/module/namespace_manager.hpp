// Solis Programming Language - Namespace Manager
// Author: Chris M. Perez
// License: MIT License (see LICENSE file)

#pragma once

#include "parser/ast.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace solis {

// Forward declaration
struct Value;
using ValuePtr = std::shared_ptr<Value>;

/// Manages symbol visibility and qualified name resolution.
/// Supports qualified imports, selective imports, hiding clauses, and conflict detection.
class NamespaceManager {
public:
  /// Symbol information in namespace
  struct Symbol {
    std::string name;
    std::string moduleName;
    ValuePtr value;
    bool isExported;
  };

  /// Import configuration
  struct ImportConfig {
    std::string moduleName;
    bool qualified;
    std::optional<std::string> alias;
    std::vector<std::string> importList;  // Empty = import all
    std::vector<std::string> hideList;    // Empty = hide nothing
  };

  NamespaceManager() = default;

  /// Register an import with symbols from module
  ///
  /// @param import Import declaration
  /// @param symbols Available symbols from module
  /// @throws runtime_error if selective import references non-existent symbol
  void addImport(const ImportDecl& import, const std::vector<Symbol>& symbols);

  /// Lookup unqualified name
  /// @return Symbol if found uniquely, nullopt if not found or ambiguous
  std::optional<Symbol> lookup(const std::string& name) const;

  /// Lookup qualified name (e.g., "Foo.bar" or "F.bar")
  /// @param qualifier Module name or alias
  /// @param name Symbol name
  /// @return Symbol if found, nullopt otherwise
  std::optional<Symbol> lookupQualified(const std::string& qualifier,
                                        const std::string& name) const;

  /// Check if a name is ambiguous (imported from multiple modules)
  bool isAmbiguous(const std::string& name) const;

  /// Get modules that export a symbol
  std::vector<std::string> getModulesExporting(const std::string& name) const;

  /// Register module symbols in catalog for import suggestions
  void registerModuleCatalog(const std::string& moduleName, const std::vector<Symbol>& symbols);

  /// Suggest imports for undefined symbol
  std::vector<std::string> suggestImportsFor(const std::string& symbolName) const;

  /// Scan search paths and populate catalog with all available modules
  /// @param searchPaths Directories to scan for .solis files
  void scanAvailableModules(const std::vector<std::string>& searchPaths);

  /// Print namespace contents for debugging
  void dump() const;

private:
  /// Qualified symbol table: qualifier =>(name =>Symbol)
  std::map<std::string, std::map<std::string, Symbol>> qualifiedSymbols_;

  /// Unqualified symbol table: name =>[Symbol...] (multiple = ambiguous)
  std::map<std::string, std::vector<Symbol>> unqualifiedSymbols_;

  /// Module catalog for import suggestions: moduleName =>(symbolName =>Symbol)
  std::map<std::string, std::map<std::string, Symbol>> moduleCatalog_;

  /// Check if symbol passes import/hide filters
  bool shouldImportSymbol(const std::string& symbolName,
                          const std::vector<std::string>& importList,
                          const std::vector<std::string>& hideList) const;
};

}  // namespace solis
