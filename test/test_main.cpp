#include "test_framework.hpp"

// Forward declarations
void registerLexerTests(solis::test::TestRunner& runner);
void registerParserTests(solis::test::TestRunner& runner);
void registerInterpreterTests(solis::test::TestRunner& runner);
void registerPatternMatchingTests(solis::test::TestRunner& runner);
void registerPreludeTests(solis::test::TestRunner& runner);
void registerFunctionTests(solis::test::TestRunner& runner);
void registerListTests(solis::test::TestRunner& runner);
void registerRecordTests(solis::test::TestRunner& runner);
void registerMonadicTests(solis::test::TestRunner& runner);
void registerNumericTests(solis::test::TestRunner& runner);
void registerStringTests(solis::test::TestRunner& runner);
void registerADTTests(solis::test::TestRunner& runner);
void registerLazyTests(solis::test::TestRunner& runner);
void registerFibOverflowTests(solis::test::TestRunner& runner);
void registerMainEdgeCaseTests(solis::test::TestRunner& runner);
void registerExampleTests(solis::test::TestRunner& runner);

int main() {
  solis::test::TestRunner runner;

  // Add all test suites
  registerLexerTests(runner);
  registerParserTests(runner);
  registerInterpreterTests(runner);
  registerPatternMatchingTests(runner);
  registerPreludeTests(runner);
  registerFunctionTests(runner);
  registerListTests(runner);
  registerRecordTests(runner);
  registerMonadicTests(runner);
  registerNumericTests(runner);
  registerStringTests(runner);
  registerADTTests(runner);
  registerLazyTests(runner);
  registerFibOverflowTests(runner);
  registerMainEdgeCaseTests(runner);
  registerExampleTests(runner);

  // Run all tests
  return runner.runAll();
}
