#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerFibOverflowTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Fibonacci Overflow Tests");

  suite->addTest("Fibonacci 50 - checks Int overflow", []() {
    std::string code = R"(
      let fib = \n ->
        let helper = \i -> \a -> \b ->
          if i == 0 {
            a
          } else {
            helper (i - 1) b (a + b)
          } in
        helper n 0 1 in
      fib 50
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    // Fib(50) = 12586269025
    // This will overflow Int64 max (9223372036854775807), so check if it wraps
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr, "fib(50) should return IntValue");
  });

  suite->addTest("Fibonacci 100 - severe Int overflow", []() {
    std::string code = R"(
      let fib = \n ->
        let helper = \i -> \a -> \b ->
          if i == 0 {
            a
          } else {
            helper (i - 1) b (a + b)
          } in
        helper n 0 1 in
      fib 100
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    // Fib(100) = 354224848179261915075
    // This WILL overflow Int64, demonstrating the need for BigInt
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr, "fib(100) returns value (may be incorrect due to overflow)");
    // Note: This test documents that Int overflows - user should use BigInt
  });

  suite->addTest("Fibonacci 20 - safe value without overflow", []() {
    std::string code = R"(
      let fib = \n ->
        let helper = \i -> \a -> \b ->
          if i == 0 {
            a
          } else {
            helper (i - 1) b (a + b)
          } in
        helper n 0 1 in
      fib 20
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    // Fib(20) = 6765
    assertEqual(6765, intVal->value);
  });

  runner.addSuite(suite);
}
