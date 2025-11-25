// Solis Programming Language - Module Resolver
// Author: Chris M. Perez
// License: MIT License (see LICENSE file)

#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace solis {

/// Resolves module names to file paths with circular import detection.
/// Converts hierarchical names (Data.List) to paths (Data/List.solis).
class ModuleResolver {
public:
  /// Module search path configuration
  struct Config {
    std::string stdLibPath;
    std::string preludePath;
    std::vector<std::string> additionalPaths;

    Config()
        : stdLibPath("src/solis/std")
        , preludePath("src/solis/prelude") {}
  };

  explicit ModuleResolver(Config config = Config());

  /// Resolve module name to canonical file path
  /// Search order: stdlib =>current dir =>additional paths =>prelude
  ///
  /// @param moduleName Module name (e.g., "Data.List" or "MathUtils")
  /// @param currentDir Directory for relative imports
  /// @return Canonical path if found, nullopt otherwise
  std::optional<std::string> resolveModule(const std::string& moduleName,
                                           const std::string& currentDir = ".");

  /// Check if a module has been loaded (prevents circular imports)
  bool isLoaded(const std::string& moduleName) const;

  /// Mark a module as loaded
  void markLoaded(const std::string& moduleName);

  /// Clear loaded module state (useful for REPL :reload)
  void clearLoadedModules();

  /// Get all configured search paths in priority order
  std::vector<std::string> getSearchPaths(const std::string& currentDir) const;

  /// Convert module name to relative file path
  /// Example: "Data.List" =>"Data/List.solis"
  static std::string moduleNameToPath(const std::string& moduleName);

private:
  Config config_;
  std::set<std::string> loadedModules_;

  /// Try to find file in a specific base path
  std::optional<std::string> tryFindModule(const std::string& basePath,
                                           const std::string& relPath) const;
};

}  // namespace solis
