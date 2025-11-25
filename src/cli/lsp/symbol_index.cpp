// Solis Programming Language - LSP Symbol Index Implementation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/lsp/symbol_index.hpp"

#include "cli/lsp/lsp.hpp"

#include <algorithm>
#include <chrono>

namespace solis {
namespace lsp {

SymbolIndex::SymbolIndex() {}

// Index entire file and extract all symbols
void SymbolIndex::indexFile(const std::string& uri,
                            const std::vector<DeclPtr>& declarations,
                            const TypeInference& typer) {
  // Clear previous symbols for this file
  clearFile(uri);

  // Update file timestamp
  dependencies_[uri].uri = uri;
  dependencies_[uri].lastModified = std::chrono::system_clock::now().time_since_epoch().count();

  // Index each declaration
  for (const auto& decl : declarations) {
    if (decl) {
      indexDeclaration(*decl, uri, typer);
    }
  }
}

// Index single declaration
void SymbolIndex::indexDeclaration(const Decl& decl,
                                   const std::string& uri,
                                   const TypeInference& typer) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, FunctionDecl>) {
          // Get type from typer if available
          InferTypePtr type = nullptr;
          if (typer.env().contains(node.name)) {
            try {
              auto scheme = typer.env().lookup(node.name);
              type = scheme.type;
            } catch (const std::exception&) {
              // Type lookup failed, continue without type
              type = nullptr;
            }
          }
          indexFunctionDecl(node, uri, type);
        } else if constexpr (std::is_same_v<T, TypeDecl>) {
          indexTypeDecl(node, uri);
        } else if constexpr (std::is_same_v<T, ModuleDecl>) {
          indexModuleDecl(node, uri);
        } else if constexpr (std::is_same_v<T, TraitDecl>) {
          indexTraitDecl(node, uri);
        }
      },
      decl.node);
}

// Index function declaration
void SymbolIndex::indexFunctionDecl(const FunctionDecl& func,
                                    const std::string& uri,
                                    InferTypePtr type) {
  // Create symbol location (approximation - need actual line info from parser)
  Range range{{0, 0}, {0, 0}};
  SymbolLocation location(uri, range, func.name, type);

  // Create symbol info
  SymbolInfo info(func.name, SymbolKind::Function, location);
  info.type = type;

  // Extract references from function body
  if (func.body) {
    extractReferencesFromExpr(*func.body, uri, info.references);
  }

  // Store symbol
  symbols_[func.name].push_back(info);
  fileSymbols_[uri].push_back(&symbols_[func.name].back());

  // Mark as export
  dependencies_[uri].exports.insert(func.name);
}

// Index type declaration
void SymbolIndex::indexTypeDecl(const TypeDecl& typeDecl, const std::string& uri) {
  Range range{{0, 0}, {0, 0}};
  SymbolLocation location(uri, range, typeDecl.name);

  SymbolInfo info(typeDecl.name, SymbolKind::Type, location);
  symbols_[typeDecl.name].push_back(info);
  fileSymbols_[uri].push_back(&symbols_[typeDecl.name].back());

  dependencies_[uri].exports.insert(typeDecl.name);

  // Index constructors if ADT
  if (auto* adt = std::get_if<std::vector<std::pair<std::string, std::vector<TypePtr>>>>(
          &typeDecl.rhs)) {
    for (const auto& [ctorName, args] : *adt) {
      SymbolLocation ctorLoc(uri, range, ctorName);
      SymbolInfo ctorInfo(ctorName, SymbolKind::Constructor, ctorLoc);
      symbols_[ctorName].push_back(ctorInfo);
      fileSymbols_[uri].push_back(&symbols_[ctorName].back());
      dependencies_[uri].exports.insert(ctorName);
    }
  }
}

// Index module declaration
void SymbolIndex::indexModuleDecl(const ModuleDecl& moduleDecl, const std::string& uri) {
  Range range{{0, 0}, {0, 0}};
  SymbolLocation location(uri, range, moduleDecl.name);

  SymbolInfo info(moduleDecl.name, SymbolKind::Module, location);
  symbols_[moduleDecl.name].push_back(info);
  fileSymbols_[uri].push_back(&symbols_[moduleDecl.name].back());
}

// Index trait declaration
void SymbolIndex::indexTraitDecl(const TraitDecl& traitDecl, const std::string& uri) {
  Range range{{0, 0}, {0, 0}};
  SymbolLocation location(uri, range, traitDecl.name);

  SymbolInfo info(traitDecl.name, SymbolKind::Trait, location);
  symbols_[traitDecl.name].push_back(info);
  fileSymbols_[uri].push_back(&symbols_[traitDecl.name].back());

  dependencies_[uri].exports.insert(traitDecl.name);
}

// Extract references from expression tree
void SymbolIndex::extractReferencesFromExpr(const Expr& expr,
                                            const std::string& uri,
                                            std::vector<SymbolLocation>& refs) {
  try {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, Var>) {
            Range range{{0, 0}, {0, 0}};
            refs.emplace_back(uri, range, node.name);
          } else if constexpr (std::is_same_v<T, App>) {
            if (node.func) {
              extractReferencesFromExpr(*node.func, uri, refs);
            }
            if (node.arg) {
              extractReferencesFromExpr(*node.arg, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, Lambda>) {
            if (node.body) {
              extractReferencesFromExpr(*node.body, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, Let>) {
            if (node.value) {
              extractReferencesFromExpr(*node.value, uri, refs);
            }
            if (node.body) {
              extractReferencesFromExpr(*node.body, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, If>) {
            if (node.cond) {
              extractReferencesFromExpr(*node.cond, uri, refs);
            }
            if (node.thenBranch) {
              extractReferencesFromExpr(*node.thenBranch, uri, refs);
            }
            if (node.elseBranch) {
              extractReferencesFromExpr(*node.elseBranch, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, Match>) {
            if (node.scrutinee) {
              extractReferencesFromExpr(*node.scrutinee, uri, refs);
            }
            for (const auto& [pattern, armExpr] : node.arms) {
              if (armExpr) {
                extractReferencesFromExpr(*armExpr, uri, refs);
              }
            }
          } else if constexpr (std::is_same_v<T, BinOp>) {
            if (node.left) {
              extractReferencesFromExpr(*node.left, uri, refs);
            }
            if (node.right) {
              extractReferencesFromExpr(*node.right, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, List>) {
            for (const auto& elem : node.elements) {
              if (elem) {
                extractReferencesFromExpr(*elem, uri, refs);
              }
            }
          } else if constexpr (std::is_same_v<T, Record>) {
            for (const auto& [name, fieldExpr] : node.fields) {
              if (fieldExpr) {
                extractReferencesFromExpr(*fieldExpr, uri, refs);
              }
            }
          } else if constexpr (std::is_same_v<T, RecordAccess>) {
            if (node.record) {
              extractReferencesFromExpr(*node.record, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, RecordUpdate>) {
            if (node.record) {
              extractReferencesFromExpr(*node.record, uri, refs);
            }
            for (const auto& [name, updateExpr] : node.updates) {
              if (updateExpr) {
                extractReferencesFromExpr(*updateExpr, uri, refs);
              }
            }
          } else if constexpr (std::is_same_v<T, Block>) {
            for (const auto& stmt : node.stmts) {
              if (stmt) {
                extractReferencesFromExpr(*stmt, uri, refs);
              }
            }
          } else if constexpr (std::is_same_v<T, Strict>) {
            if (node.expr) {
              extractReferencesFromExpr(*node.expr, uri, refs);
            }
          } else if constexpr (std::is_same_v<T, Bind>) {
            if (node.value) {
              extractReferencesFromExpr(*node.value, uri, refs);
            }
            if (node.body) {
              extractReferencesFromExpr(*node.body, uri, refs);
            }
          }
        },
        expr.node);
  } catch (const std::exception&) {
    // Ignore errors during reference extraction
  }
}

// Find definition of symbol
SymbolLocation* SymbolIndex::findDefinition(const std::string& name,
                                            const std::string& currentUri) {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return nullptr;
  }

  // Prefer definition in current file
  for (auto& info : it->second) {
    if (info.definition.uri == currentUri) {
      return &info.definition;
    }
  }

  // Otherwise return first definition
  if (!it->second.empty()) {
    return &it->second[0].definition;
  }

  return nullptr;
}

// Find all references to a symbol
std::vector<SymbolLocation> SymbolIndex::findReferences(const std::string& name,
                                                        const std::string& uri) {
  std::vector<SymbolLocation> allRefs;

  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return allRefs;
  }

  // Collect references from all symbol instances
  for (const auto& info : it->second) {
    // Add definition as a reference
    allRefs.push_back(info.definition);

    // Add all other references
    allRefs.insert(allRefs.end(), info.references.begin(), info.references.end());
  }

  return allRefs;
}

// Find symbol at specific position
SymbolInfo* SymbolIndex::findSymbolAtPosition(const std::string& uri, const Position& pos) {
  auto it = fileSymbols_.find(uri);
  if (it == fileSymbols_.end()) {
    return nullptr;
  }

  for (auto* symbol : it->second) {
    if (rangeContainsPosition(symbol->definition.range, pos)) {
      return symbol;
    }
  }

  return nullptr;
}

// Get all symbols in a file
std::vector<SymbolInfo> SymbolIndex::getDocumentSymbols(const std::string& uri) {
  std::vector<SymbolInfo> result;

  auto it = fileSymbols_.find(uri);
  if (it != fileSymbols_.end()) {
    for (auto* symbol : it->second) {
      result.push_back(*symbol);
    }
  }

  return result;
}

// Get symbols for completion with prefix filtering
std::vector<SymbolInfo> SymbolIndex::getSymbolsForCompletion(const std::string& uri,
                                                             const std::string& prefix) {
  std::vector<SymbolInfo> result;

  for (const auto& [name, infos] : symbols_) {
    if (prefix.empty() || name.find(prefix) == 0) {
      if (!infos.empty()) {
        result.push_back(infos[0]);
      }
    }
  }

  return result;
}

// Register import dependency
void SymbolIndex::registerImport(const std::string& fromUri, const std::string& toUri) {
  dependencies_[fromUri].imports.insert(toUri);
  dependencies_[toUri].importedBy.insert(fromUri);
}

// Get files that depend on this file
std::set<std::string> SymbolIndex::getDependentFiles(const std::string& uri) {
  auto it = dependencies_.find(uri);
  if (it != dependencies_.end()) {
    return it->second.importedBy;
  }
  return {};
}

// Get files that this file depends on
std::set<std::string> SymbolIndex::getDependencies(const std::string& uri) {
  auto it = dependencies_.find(uri);
  if (it != dependencies_.end()) {
    return it->second.imports;
  }
  return {};
}

// Invalidate file for incremental recompilation
void SymbolIndex::invalidateFile(const std::string& uri) {
  clearFile(uri);

  // Mark dependent files as needing reanalysis
  auto dependents = getDependentFiles(uri);
  for (const auto& dep : dependents) {
    dependencies_[dep].lastModified = 0;
  }
}

// Clear all symbols from a file
void SymbolIndex::clearFile(const std::string& uri) {
  // Remove from file-specific lookup
  auto fileIt = fileSymbols_.find(uri);
  if (fileIt != fileSymbols_.end()) {
    for (auto* symbol : fileIt->second) {
      // Remove from global symbol map
      auto symbolIt = symbols_.find(symbol->name);
      if (symbolIt != symbols_.end()) {
        auto& infos = symbolIt->second;
        infos.erase(std::remove_if(infos.begin(),
                                   infos.end(),
                                   [&](const SymbolInfo& info) {
                                     return info.definition.uri == uri;
                                   }),
                    infos.end());

        // Remove symbol entry if empty
        if (infos.empty()) {
          symbols_.erase(symbolIt);
        }
      }
    }
    fileSymbols_.erase(fileIt);
  }

  // Clear exports
  dependencies_[uri].exports.clear();
}

// Clear entire workspace
void SymbolIndex::clearWorkspace() {
  symbols_.clear();
  fileSymbols_.clear();
  dependencies_.clear();
}

// Get total symbol count
size_t SymbolIndex::getTotalSymbols() const {
  size_t count = 0;
  for (const auto& [name, infos] : symbols_) {
    count += infos.size();
  }
  return count;
}

// Get file count
size_t SymbolIndex::getFileCount() const {
  return fileSymbols_.size();
}

// Check if range contains position
bool SymbolIndex::rangeContainsPosition(const Range& range, const Position& pos) const {
  if (pos.line < range.start.line || pos.line > range.end.line) {
    return false;
  }

  if (pos.line == range.start.line && pos.character < range.start.character) {
    return false;
  }

  if (pos.line == range.end.line && pos.character > range.end.character) {
    return false;
  }

  return true;
}

}  // namespace lsp
}  // namespace solis
