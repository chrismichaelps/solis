<div style="display: flex; align-items: center; gap: 20px;">
  <img src="public/logo/file_icon.png" alt="Solis Logo" style="background-color: #000; padding: 10px; border-radius: 5px;">
  <h1>Solis Programming Language</h1>
</div>

**Author:** Chris M. Perez  
**Status:** Academic Research Prototype

> > Disclaimer — Transparency About AI Usage
> > The initial drafts of Solis—including documentation, prototypes, and parts of the compiler—were created with the help of AI tools following my prompts and design instructions. I do not claim to have written every line manually; instead, I guided the process, refined the output, and made the architectural and conceptual decisions. This repository represents an AI-assisted research prototype, not a fully hand-coded language implementation.

Solis is a functional programming language inspired by Haskell, combining Hindley-Milner type inference with bidirectional type checking concepts from Lean. The language features pattern matching, call-by-need evaluation, and monadic do-notation. The implementation includes an interactive REPL and dual backends: an interpreter and an LLVM-based code generator.

## Why Solis?

Solis draws inspiration from Haskell's lazy evaluation, monadic do-notation, and pattern matching, while incorporating bidirectional type checking concepts from Lean and Agda. The type system uses Hindley-Milner inference (Damas-Milner algorithm) with constraint generation and unification as the foundation, extended with bidirectional checking modes (synthesis and checking) for improved type inference in certain contexts.

The type system infers types across function boundaries without requiring explicit annotations while maintaining static type safety. Type schemes with universal quantification enable polymorphic functions. The bidirectional extension allows checking expressions against expected types, improving inference for lambda expressions and applications.

The compiler architecture separates parsing, type checking, and code generation into modular components sharing a common AST representation. This design allows the interpreter and LLVM backend to share the same frontend, enabling rapid iteration during development and optimized native code generation.

Lazy evaluation follows Haskell's call-by-need semantics, implemented via thunks with memoization. The `ThunkValue` structure caches computed results, enabling infinite data structures. The expression-oriented syntax with pattern matching supports algebraic data types, records, and monadic do-notation similar to Haskell's monad syntax.

## Vision

Solis is an academic research project exploring type system implementation and compiler construction. The goal is to bridge theoretical foundations with practical implementation, producing a language suitable for real-world development.

The compiler architecture emphasizes modularity: lexer, parser, type checker, and code generators are separate components sharing a common AST representation. The LLVM backend generates optimized native code with runtime support for thunks, closures, and garbage collection. The interpreter enables rapid iteration during development with the same type system and language semantics.

## Roadmap

The goal is to evolve Solis from an academic research project into a practical language capable of building real-world software.

See [ROADMAP.md](ROADMAP.md) for detailed plans.

## Documentation

Language documentation and examples are in the `doc/` directory. The `doc/examples/` folder contains runnable examples organized by concept: functions, pattern matching, lists, records, algebraic data types, lazy evaluation, do notation, type inference, BigInt, and higher-order functions.

The Solis compiler provides several tools:

- `solis` - Main compiler/interpreter with REPL support
- `solis-lsp` - Language Server Protocol implementation for IDE integration
- `solisfmt` - Code formatter for consistent code style

## Interactive REPL

Solis includes an interactive REPL (Read-Eval-Print Loop) for rapid development and experimentation. Start the REPL with:

```bash
./solis repl
# or simply
./solis
```

### REPL Commands

The REPL provides built-in commands prefixed with `:`:

- `:help` (or `:h`, `:?`) - Show available commands
- `:type EXPRESSION` (or `:t`) - Display the type of an expression
- `:info IDENTIFIER` (or `:i`) - Show information about a binding
- `:browse [MODULE]` (or `:b`) - List all bindings in scope
- `:kind TYPE` (or `:k`) - Show the kind of a type
- `:load FILE` (or `:l`) - Load and execute a Solis file
- `:reload` - Reload the last loaded file
- `:clear` - Clear REPL state (limited, restart recommended for full reset)
- `:compile FILE` - Compile a file to LLVM IR or native code
- `:quit` (or `:q`, `:exit`) - Exit the REPL

### REPL Features

- **History**: Command history is saved to `~/.solis_history` and persists across sessions
- **Tab completion**: Automatic completion for identifiers and commands
- **Multi-line input**: Automatically handles multi-line expressions (braces, brackets, parentheses)
- **Type inference**: Real-time type checking and inference feedback
- **Prelude loaded**: Standard library functions are available by default

### Configuration

**REPL Configuration:**

- History file: `~/.solis_history`
- Prelude: Automatically loaded on startup
- Module search paths: Configurable via `ModuleResolver` when using the compiler programmatically

**Formatter Configuration:**

The `solisfmt` tool supports configuration options:

```bash
# Format files in-place (default)
./solisfmt file.solis

# Print formatted output to stdout
./solisfmt --stdout file.solis

# Minimal formatting (preserve operator spacing)
./solisfmt --minimal file.solis

# Quiet mode (suppress output messages)
./solisfmt --quiet file.solis
```

Formatter options include:

- Indentation size (default: 2 spaces)
- Max line width (default: 80)
- Brace style (K&R or Allman)
- Operator spacing (configurable)
- Pattern arrow alignment
- Trailing comma handling

## Installation

- Download a pre-built binary (when available)
- Build from source (see below)

### VS Code Extension (Local Development Only)

A local VS Code extension is available for syntax highlighting and language icon support. **This extension is not production-ready and must be installed locally from source.**

To install locally:

```bash
cd vscode-solis
npm install
code --install-extension .
```

The extension provides:

- Syntax highlighting for `.solis` files
- Language icon for Solis files in the file explorer

**Note:** This extension is not published to the VS Code marketplace and is intended for local development use only.

## Building from Source

Ensure you have the required dependencies:

- C++ compiler with C++23 support (g++ or clang++)
- GMP development libraries (BigInt support)
- LLVM 21.x development libraries (code generation)

### macOS

```bash
brew install gmp llvm@21
```

### Linux

```bash
sudo apt-get install libgmp-dev llvm-21-dev
# or on Fedora/RHEL:
sudo dnf install gmp-devel llvm21-devel
```

Then it is the standard Make build process:

```bash
make
```

This produces three executables:

- `solis` - Main compiler/interpreter
- `solis-lsp` - LSP server
- `solisfmt` - Code formatter

You can build them individually:

```bash
make solis      # Main compiler/interpreter
make solis-lsp  # LSP server
make solisfmt   # Code formatter
```

To install to a system location:

```bash
make install
```

This installs to `/usr/local/bin` by default. You can customize the installation prefix:

```bash
make install PREFIX=/custom/path
```

To clean build artifacts:

```bash
make clean
```

## Building from Source without LLVM

If you only need the interpreter without code generation capabilities, you can build without LLVM:

```bash
make solis NO_LLVM=1
```

This produces a `solis` executable that can run Solis programs via the interpreter but cannot generate native code.

## Requirements

### System Dependencies

- **C++ Compiler**: g++ or clang++ with C++23 support
- **GMP**: GNU Multiple Precision Arithmetic Library (for BigInt support)
- **LLVM**: Version 21.x (optional, for code generation)

### Build System

Standalone Makefile with automatic detection of LLVM and GMP installations. Searches common installation paths including Homebrew on macOS.

## Contributing

Solis is an academic research project. We welcome bug reports, feature suggestions, and contributions that align with the project's educational and research goals.

When contributing:

- Follow the existing code style and conventions
- Ensure all tests pass: `make test`
- Update documentation for new features
- Keep the focus on type system research and compiler implementation

## License

MIT License - Copyright (c) 2025 Chris M. Perez
