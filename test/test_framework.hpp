#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace solis::test {

// Test result
struct TestResult {
  std::string name;
  bool passed;
  std::string message;
};

// Test suite
class TestSuite {
public:
  TestSuite(const std::string& name)
      : name_(name) {}

  void addTest(const std::string& testName, std::function<void()> test) {
    tests_.push_back({testName, test});
  }

  std::vector<TestResult> run() {
    std::vector<TestResult> results;

    for (const auto& [name, test] : tests_) {
      try {
        test();
        results.push_back({name, true, ""});
      } catch (const std::exception& e) {
        results.push_back({name, false, e.what()});
      } catch (...) {
        results.push_back({name, false, "Unknown exception"});
      }
    }

    return results;
  }

  const std::string& name() const { return name_; }

private:
  std::string name_;
  std::vector<std::pair<std::string, std::function<void()>>> tests_;
};

// Assertion helpers
inline void assertTrue(bool condition, const std::string& message = "Assertion failed") {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void assertFalse(bool condition, const std::string& message = "Assertion failed") {
  assertTrue(!condition, message);
}

inline void assertEqual(const std::string& expected,
                        const std::string& actual,
                        const std::string& message = "") {
  if (expected != actual) {
    throw std::runtime_error(message.empty() ? "Expected: '" + expected + "', got: '" + actual + "'"
                                             : message);
  }
}

inline void assertEqual(int expected, int actual, const std::string& message = "") {
  if (expected != actual) {
    throw std::runtime_error(message.empty() ? "Expected: " + std::to_string(expected) +
                                                   ", got: " + std::to_string(actual)
                                             : message);
  }
}

template <typename T>
inline void assertEqual(T expected, T actual, const std::string& message = "") {
  if (expected != actual) {
    throw std::runtime_error(message.empty() ? "Values not equal" : message);
  }
}

inline void assertNotNull(const void* ptr, const std::string& message = "Pointer is null") {
  assertTrue(ptr != nullptr, message);
}

// Test runner
class TestRunner {
public:
  void addSuite(TestSuite* suite) { suites_.push_back(suite); }

  int runAll() {
    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;

    std::cout << "\n=== Running Solis Compiler Tests ===\n\n";

    for (auto* suite : suites_) {
      std::cout << "Suite: " << suite->name() << "\n";
      auto results = suite->run();

      for (const auto& result : results) {
        totalTests++;
        if (result.passed) {
          passedTests++;
          std::cout << "  [PASS] " << result.name << "\n";
        } else {
          failedTests++;
          std::cout << "  [FAIL] " << result.name << "\n";
          std::cout << "    Error: " << result.message << "\n";
        }
      }
      std::cout << "\n";
    }

    std::cout << "=== Test Summary ===\n";
    std::cout << "Total:  " << totalTests << "\n";
    std::cout << "Passed: " << passedTests << "\n";
    std::cout << "Failed: " << failedTests << "\n";
    std::cout << "\n";

    return failedTests == 0 ? 0 : 1;
  }

private:
  std::vector<TestSuite*> suites_;
};

}  // namespace solis::test
