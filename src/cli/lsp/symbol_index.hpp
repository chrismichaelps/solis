// Solis Programming Language - LSP Symbol Index
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Cross-file symbol indexing for goto-definition, find-references, and rename

#pragma once

#include "parser/ast.hpp"
#include "type/typer.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace solis {
namespace lsp {

// Position and Range definitions
struct Position {
  int line;
  int character;
};

struct Range {
  Position start;
  Position end;
};

// Symbol location in source code
struct SymbolLocation {
  std::string uri;
  Range range;
  std::string name;
  InferTypePtr type;

  SymbolLocation(const std::string& uri,
                 const Range& range,
                 const std::string& name,
                 InferTypePtr type = nullptr)
      : uri(uri)
      , range(range)
      , name(name)
      , type(type) {}
};

// Symbol kind classification
enum class SymbolKind {
  Function,
  Variable,
  Parameter,
  Type,
  Constructor,
  Field,
  Module,
  Trait,
  TypeVariable
};

// Symbol information for indexing
struct SymbolInfo {
  std::string name;
  SymbolKind kind;
  SymbolLocation definition;
  std::vector<SymbolLocation> references;
  InferTypePtr type;
  std::string documentation;

  SymbolInfo(const std::string& name, SymbolKind kind, const SymbolLocation& def)
      : name(name)
      , kind(kind)
      , definition(def)
      , type(nullptr) {}
};

// Dependency tracking between files
struct FileDependency {
  std::string uri;
  std::set<std::string> imports;     // Files this file imports
  std::set<std::string> importedBy;  // Files that import this file
  std::set<std::string> exports;     // Symbols exported by this file
  size_t lastModified;               // Timestamp for staleness detection

  FileDependency()
      : lastModified(0) {}
};

// Symbol index for entire workspace
// Tracks all symbols across all files for fast lookup
class SymbolIndex {
public:
  SymbolIndex();

  // Index a file and extract all symbols
  void indexFile(const std::string& uri,
                 const std::vector<DeclPtr>& declarations,
                 const TypeInference& typer);

  // Find symbol definition by name
  SymbolLocation* findDefinition(const std::string& name, const std::string& currentUri);

  // Find all references to a symbol
  std::vector<SymbolLocation> findReferences(const std::string& name, const std::string& uri);

  // Find symbol at specific position
  SymbolInfo* findSymbolAtPosition(const std::string& uri, const Position& pos);

  // Get all symbols in a file (for document symbols)
  std::vector<SymbolInfo> getDocumentSymbols(const std::string& uri);

  // Get all symbols for completion
  std::vector<SymbolInfo> getSymbolsForCompletion(const std::string& uri,
                                                  const std::string& prefix);

  // Dependency tracking
  void registerImport(const std::string& fromUri, const std::string& toUri);
  std::set<std::string> getDependentFiles(const std::string& uri);
  std::set<std::string> getDependencies(const std::string& uri);

  // Invalidation for incremental updates
  void invalidateFile(const std::string& uri);
  void clearFile(const std::string& uri);

  // Workspace operations
  void clearWorkspace();
  size_t getTotalSymbols() const;
  size_t getFileCount() const;

private:
  // Symbol storage: name -> list of symbols (handles overloading)
  std::map<std::string, std::vector<SymbolInfo>> symbols_;

  // URI-based symbol lookup for fast file-specific queries
  std::map<std::string, std::vector<SymbolInfo*>> fileSymbols_;

  // Dependency graph for incremental compilation
  std::map<std::string, FileDependency> dependencies_;

  // Helper methods
  void indexDeclaration(const Decl& decl, const std::string& uri, const TypeInference& typer);
  void indexFunctionDecl(const FunctionDecl& func, const std::string& uri, InferTypePtr type);
  void indexTypeDecl(const TypeDecl& typeDecl, const std::string& uri);
  void indexModuleDecl(const ModuleDecl& moduleDecl, const std::string& uri);
  void indexTraitDecl(const TraitDecl& traitDecl, const std::string& uri);

  // Extract references from expressions
  void extractReferencesFromExpr(const Expr& expr,
                                 const std::string& uri,
                                 std::vector<SymbolLocation>& refs);

  // Range intersection for position queries
  bool rangeContainsPosition(const Range& range, const Position& pos) const;
};

}  // namespace lsp
}  // namespace solis
