// Solis Programming Language - Module Resolver Implementation
// Author: Chris M. Perez
// License: MIT License (see LICENSE file)

#include "cli/module/module_resolver.hpp"

#include <algorithm>
#include <iostream>

namespace solis {

ModuleResolver::ModuleResolver(Config config)
    : config_(std::move(config)) {}

// Convert module name to relative file path
// Example: "Data.List" -> "Data/List.solis"
std::string ModuleResolver::moduleNameToPath(const std::string& moduleName) {
  std::string path = moduleName;
  std::replace(path.begin(), path.end(), '.', '/');
  path += ".solis";
  return path;
}

// Attempt to find module file in specified base path
// Returns canonical path if found, nullopt otherwise
std::optional<std::string> ModuleResolver::tryFindModule(const std::string& basePath,
                                                         const std::string& relPath) const {
  std::filesystem::path fullPath;

  if (basePath.empty() || basePath == ".") {
    fullPath = relPath;
  } else {
    fullPath = std::filesystem::path(basePath) / relPath;
  }

  try {
    if (std::filesystem::exists(fullPath)) {
      return std::filesystem::canonical(fullPath).string();
    }
  } catch (const std::filesystem::filesystem_error&) {
    // Path doesn't exist or can't be canonicalized
  }

  return std::nullopt;
}

// Resolve module name to canonical file path
// Searches configured paths in priority order
std::optional<std::string> ModuleResolver::resolveModule(const std::string& moduleName,
                                                         const std::string& currentDir) {
  std::string relPath = moduleNameToPath(moduleName);

  // Build search paths in priority order
  auto searchPaths = getSearchPaths(currentDir);

  for (const auto& basePath : searchPaths) {
    auto result = tryFindModule(basePath, relPath);
    if (result) {
      return result;
    }
  }

  return std::nullopt;
}

// Get all configured search paths in priority order
// Priority: stdlib > current dir > working dir > additional paths > prelude
std::vector<std::string> ModuleResolver::getSearchPaths(const std::string& currentDir) const {
  std::vector<std::string> paths;

  // 1. Standard library (highest priority for std modules)
  if (!config_.stdLibPath.empty()) {
    paths.push_back(config_.stdLibPath);
  }

  // 2. Current directory (for local imports)
  if (!currentDir.empty()) {
    paths.push_back(currentDir);
  }

  // 3. Current working directory
  paths.push_back(".");

  // 4. Additional configured paths
  paths.insert(paths.end(), config_.additionalPaths.begin(), config_.additionalPaths.end());

  // 5. Prelude (lowest priority, backward compatibility)
  if (!config_.preludePath.empty()) {
    paths.push_back(config_.preludePath);
  }

  return paths;
}

// Check if module has been loaded (prevents circular imports)
bool ModuleResolver::isLoaded(const std::string& moduleName) const {
  return loadedModules_.count(moduleName) > 0;
}

// Mark module as loaded to prevent circular imports
void ModuleResolver::markLoaded(const std::string& moduleName) {
  loadedModules_.insert(moduleName);
}

// Clear loaded module state (useful for REPL :reload)
void ModuleResolver::clearLoadedModules() {
  loadedModules_.clear();
}

}  // namespace solis
