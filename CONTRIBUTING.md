<div style="display: flex; align-items: center; gap: 20px;">
  <img src="public/logo/file_icon.png" alt="Solis Logo" style="background-color: #000; padding: 10px; border-radius: 5px;">
  <h1>Contributing to Solis</h1>
</div>

Thank you for your interest in contributing to Solis! This document provides guidelines and instructions for contributing to the project.

## Getting Started

1. **Fork the repository** and clone your fork locally
2. **Set up the development environment**:
   ```bash
   # Install dependencies (see README.md for details)
   brew install gmp llvm@21  # macOS
   # or
   sudo apt-get install libgmp-dev llvm-21-dev  # Linux
   
   # Build the project
   make
   ```

3. **Run the test suite** to ensure everything works:
   ```bash
   make test
   ```

## Development Workflow

1. Create a new branch for your changes:
   ```bash
   git checkout -b feature/your-feature-name
   # or
   git checkout -b fix/your-bug-fix
   ```

2. Make your changes following the coding standards (see below)

3. Ensure all tests pass:
   ```bash
   make test
   ```

4. Commit your changes with clear, descriptive commit messages

5. Push to your fork and create a pull request

## Coding Standards

### Code Style

- Follow existing code style and conventions
- Use meaningful variable and function names
- Add comments for complex logic
- Follow the `.agent` rules for documentation (see `.agent/` directory)

### C++ Standards

- Use C++23 features where appropriate
- Prefer modern C++ idioms (smart pointers, RAII, etc.)
- Keep functions focused and modular
- Document public APIs

### Testing

- Add tests for new features
- Ensure all existing tests pass
- Write tests that are clear and maintainable
- Test edge cases and error conditions

## Areas for Contribution

### Language Features

- Type system improvements
- New language constructs
- Standard library functions
- Performance optimizations

### Tooling

- REPL enhancements
- Formatter improvements
- LSP server features
- Documentation

### Infrastructure

- Build system improvements
- CI/CD setup
- Documentation
- Examples and tutorials

## Reporting Issues

When reporting bugs or requesting features:

1. Check if the issue already exists
2. Use clear, descriptive titles
3. Provide steps to reproduce (for bugs)
4. Include relevant code examples
5. Specify your environment (OS, compiler version, etc.)

## Pull Request Process

1. Update documentation if needed
2. Add tests for new functionality
3. Ensure all tests pass (`make test`)
4. Update CHANGELOG.md if applicable
5. Write clear commit messages
6. Reference any related issues

## Code Review

All submissions require review. We aim to:

- Respond to pull requests within a reasonable time
- Provide constructive feedback
- Help contributors improve their code
- Maintain code quality and consistency

## Questions?

If you have questions about contributing, feel free to:

- Open an issue with the `question` label
- Check existing documentation
- Review the codebase for examples

Thank you for contributing to Solis!

