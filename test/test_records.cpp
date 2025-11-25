#include "../src/cli/interpreter/interpreter.hpp"
#include "../src/parser/lexer.hpp"
#include "../src/parser/parser.hpp"
#include "test_framework.hpp"

using namespace solis;
using namespace solis::test;

void registerRecordTests(solis::test::TestRunner& runner) {
  auto* suite = new TestSuite("Record Operations");

  suite->addTest("Record creation and field access", []() {
    std::string code = R"(
      let person = { name = "Alice", age = 30 } in
      person.age
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);

    if (auto* intVal = std::get_if<IntValue>(&result->data)) {
      assertEqual(30, intVal->value);
    } else if (std::get_if<ThunkValue>(&result->data)) {
      throw std::runtime_error("Got ThunkValue instead of IntValue - field not forced");
    } else if (std::get_if<RecordValue>(&result->data)) {
      throw std::runtime_error("Got RecordValue instead of IntValue - field access didn't work");
    } else {
      throw std::runtime_error(
          "Got unexpected type (variant index: " + std::to_string(result->data.index()) + ")");
    }
  });

  suite->addTest("Record update syntax", []() {
    std::string code = R"(
      let person = { name = "Alice", age = 30 } in
      let updated = { person | age = 31 } in
      updated.age
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);

    if (auto* intVal = std::get_if<IntValue>(&result->data)) {
      assertEqual(31, intVal->value);
    } else if (std::get_if<ThunkValue>(&result->data)) {
      throw std::runtime_error("Got ThunkValue instead of IntValue - field not forced");
    } else if (std::get_if<RecordValue>(&result->data)) {
      throw std::runtime_error("Got RecordValue instead of IntValue - field access didn't work");
    } else {
      throw std::runtime_error(
          "Got unexpected type (variant index: " + std::to_string(result->data.index()) + ")");
    }
  });

  suite->addTest("Nested record creation and access", []() {
    std::string code = R"(
      let address = { street = "Main St", city = "NYC" } in
      let person = { name = "Bob", address = address } in
      person.address
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* recordVal = std::get_if<RecordValue>(&result->data);
    assertTrue(recordVal != nullptr);
    assertTrue(recordVal->fields.count("street") > 0);
  });

  suite->addTest("Deeply nested record field access", []() {
    std::string code = R"(
      let address = { street = "Main St", number = 123 } in
      let person = { name = "Alice", address = address } in
      person.address.number
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(123, intVal->value);
  });

  suite->addTest("Record with multiple field types", []() {
    std::string code = R"(
      let mixed = { id = 42, name = "Test", active = true, score = 3.14 } in
      mixed.id
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* intVal = std::get_if<IntValue>(&result->data);
    assertTrue(intVal != nullptr);
    assertEqual(42, intVal->value);
  });

  suite->addTest("Nested record update", []() {
    std::string code = R"(
      let address = { street = "Main", number = 10 } in
      let person = { name = "Bob", address = address } in
      let updated = { person | name = "Robert" } in
      updated.name
    )";
    Lexer lexer(code);
    Parser parser(lexer.tokenize());
    auto expr = parser.parseExpression();
    Interpreter interp;
    auto result = interp.eval(*expr);
    auto* strVal = std::get_if<StringValue>(&result->data);
    assertTrue(strVal != nullptr);
    assertEqual("Robert", strVal->value);
  });

  runner.addSuite(suite);
}
