// Solis Programming Language - Compiler Driver
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include <string>

namespace solis {

// Compile a Solis source file to a native executable
// Returns 0 on success, non-zero on failure
int compileFile(const std::string& filename);

}  // namespace solis
