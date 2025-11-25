# Solis Programming Language - Build System
# Copyright (c) 2025 Chris M. Perez
# Licensed under the MIT License

CXX = g++

# LLVM Configuration
# Try to find llvm-config, preferring LLVM 21
LLVM_CONFIG = $(shell which llvm-config 2>/dev/null || find /opt/homebrew/Cellar/llvm/21* /opt/homebrew/Cellar/llvm@21* /usr/local/Cellar/llvm/21* -name llvm-config -type f 2>/dev/null | head -1 || find /opt/homebrew /usr/local -name llvm-config -type f 2>/dev/null | head -1 || echo "")
LLVM_CXXFLAGS_RAW = $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null || echo "-I/opt/homebrew/Cellar/llvm/21.1.3_1/include -std=c++17 -stdlib=libc++ -funwind-tables -DEXPERIMENTAL_KEY_INSTRUCTIONS -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS")
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --libs core 2>/dev/null || echo "-L/opt/homebrew/Cellar/llvm/21.1.3_1/lib -Wl,-search_paths_first -Wl,-headerpad_max_install_names -lLLVM-21 -Wl,-rpath,/opt/homebrew/Cellar/llvm/21.1.3_1/lib")

# Filter out -fno-exceptions from LLVM flags and add our own -fexceptions
LLVM_CXXFLAGS = $(filter-out -fno-exceptions,$(LLVM_CXXFLAGS_RAW))

# Base flags with exceptions enabled
CXXFLAGS = -std=c++23 -Wall -Wextra -O2 -fexceptions -Isrc
CXXFLAGS += $(LLVM_CXXFLAGS)

# GMP Configuration (try common locations)
GMP_INCLUDE = $(shell pkg-config --cflags gmp 2>/dev/null || echo "-I/opt/homebrew/Cellar/gmp/6.3.0/include -I/opt/homebrew/include -I/usr/local/include")
GMP_LIB = $(shell pkg-config --libs gmp 2>/dev/null || echo "-L/opt/homebrew/Cellar/gmp/6.3.0/lib -L/opt/homebrew/lib -L/usr/local/lib -lgmp")

CXXFLAGS += $(GMP_INCLUDE)

# Linker flags - dynamic linking (default on macOS/Linux)
# Using -l flags links dynamically against shared libraries (.dylib/.so)
LDFLAGS = $(LLVM_LDFLAGS) $(GMP_LIB)

# Source files
PARSER_SOURCES = src/parser/lexer.cpp src/parser/parser.cpp src/parser/ast.cpp
CLI_SOURCES = src/cli/interpreter/interpreter.cpp src/cli/module/module_resolver.cpp src/cli/module/namespace_manager.cpp src/cli/compiler/compiler.cpp
ERROR_SOURCES = src/error/errors.cpp
TYPE_SOURCES = src/type/typer.cpp src/type/bidirectional.cpp
REPL_SOURCES = src/cli/repl/repl.cpp
UTILS_SOURCES = src/utils/linenoise.cpp
LSP_SOURCES = src/cli/lsp/lsp.cpp src/cli/lsp/symbol_index.cpp
FORMATTER_SOURCES = src/cli/formatter/formatter.cpp
RUNTIME_SOURCES = src/runtime/bigint.cpp
CODEGEN_SOURCES = src/codegen/codegen.cpp \
                  src/codegen/runtime/runtime_init.cpp \
                  src/codegen/backend/target_backend.cpp \
                  src/codegen/ir/types.cpp \
                  src/codegen/ir/helpers.cpp \
                  src/codegen/ir/expr_gen.cpp \
                  src/codegen/ir/closure.cpp \
                  src/codegen/ir/decl_gen.cpp \
                  src/codegen/gc/gc_helpers.cpp

# Object files
PARSER_OBJECTS = $(PARSER_SOURCES:src/%.cpp=build/%.o)
CLI_OBJECTS = $(CLI_SOURCES:src/%.cpp=build/%.o)
ERROR_OBJECTS = $(ERROR_SOURCES:src/%.cpp=build/%.o)
TYPE_OBJECTS = $(TYPE_SOURCES:src/%.cpp=build/%.o)
REPL_OBJECTS = $(REPL_SOURCES:src/%.cpp=build/%.o)
UTILS_OBJECTS = $(UTILS_SOURCES:src/%.cpp=build/%.o)
LSP_OBJECTS = $(LSP_SOURCES:src/%.cpp=build/%.o)
RUNTIME_OBJECTS = $(RUNTIME_SOURCES:src/%.cpp=build/%.o)
FORMATTER_OBJECTS = $(FORMATTER_SOURCES:src/%.cpp=build/%.o)
CODEGEN_OBJECTS = $(CODEGEN_SOURCES:src/%.cpp=build/%.o)

COMMON_OBJECTS = $(PARSER_OBJECTS) $(CLI_OBJECTS) $(ERROR_OBJECTS) $(TYPE_OBJECTS) $(REPL_OBJECTS) $(UTILS_OBJECTS) $(LSP_OBJECTS) $(FORMATTER_OBJECTS) $(RUNTIME_OBJECTS) $(CODEGEN_OBJECTS)

# Main targets
all: solis solis-lsp solisfmt

solis: build/cli/main.o $(COMMON_OBJECTS)
	@echo "Linking solis (dynamic linking)..."
	$(CXX) $(CXXFLAGS) -fexceptions -o $@ $^ $(LDFLAGS)
	@echo "Solis compiler built successfully!"

solis-lsp: build/cli/lsp/solis_lsp_main.o $(COMMON_OBJECTS)
	@echo "Linking solis-lsp (dynamic linking)..."
	$(CXX) $(CXXFLAGS) -fexceptions -o $@ $^ $(LDFLAGS)
	@echo "Solis LSP server built successfully!"

solisfmt: build/cli/formatter/solisfmt_main.o $(COMMON_OBJECTS)
	@echo "Linking solisfmt (dynamic linking)..."
	$(CXX) $(CXXFLAGS) -fexceptions -o $@ $^ $(LDFLAGS)
	@echo "Solis formatter built successfully!"

# Pattern rule for object files
build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fexceptions -c $< -o $@

# Clean
clean:
	rm -rf build solis solis-lsp solisfmt
	@echo "Build artifacts removed"

# Install (optional)
install: solis solis-lsp solisfmt
	@echo "Installing solis, solis-lsp, and solisfmt to /usr/local/bin..."
	cp solis /usr/local/bin/ 2>/dev/null || sudo cp solis /usr/local/bin/
	cp solis-lsp /usr/local/bin/ 2>/dev/null || sudo cp solis-lsp /usr/local/bin/
	cp solisfmt /usr/local/bin/ 2>/dev/null || sudo cp solisfmt /usr/local/bin/
	@echo "Installation complete!"

# Test sources
TEST_SOURCES = test/test_main.cpp \
               test/test_lexer.cpp \
               test/test_parser.cpp \
               test/test_interpreter.cpp \
               test/test_patterns.cpp \
               test/test_prelude.cpp \
               test/test_functions.cpp \
               test/test_lists.cpp \
               test/test_records.cpp \
               test/test_monadic.cpp \
               test/test_numeric.cpp \
               test/test_strings.cpp \
               test/test_adt.cpp \
               test/test_lazy.cpp \
               test/test_fib_overflow.cpp \
               test/test_main_edge_cases.cpp \
               test/test_examples.cpp

TEST_OBJECTS = $(TEST_SOURCES:test/%.cpp=build/test/%.o)

# Test executable
test: build/test/test_main $(COMMON_OBJECTS)
	@echo "Running tests..."
	./build/test/test_main

build/test/test_main: $(TEST_OBJECTS) $(COMMON_OBJECTS)
	@echo "Linking test executable..."
	@mkdir -p build/test
	$(CXX) $(CXXFLAGS) -fexceptions -o $@ $^ $(LDFLAGS)
	@echo "Test executable built successfully!"

# Pattern rule for test object files
build/test/%.o: test/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fexceptions -Itest -c $< -o $@

# Code formatting
CLANG_FORMAT = clang-format
format:
	@echo "Formatting C++ code..."
	@find src -name '*.cpp' -o -name '*.hpp' | xargs $(CLANG_FORMAT) -i 2>/dev/null || echo "clang-format not found, skipping"
	@echo "Formatting complete"

.PHONY: all clean install format test solis solis-lsp solisfmt
