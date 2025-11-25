<div style="display: flex; align-items: center; gap: 20px;">
  <img src="../public/logo/file_icon.png" alt="Solis Logo" style="background-color: #000; padding: 10px; border-radius: 5px;">
  <h1>Solis Language Documentation</h1>
</div>

This directory contains documentation and examples for the Solis programming language, organized by concept.

## Examples by Concept

The `examples/` directory contains example code demonstrating various language features:

- **[functions.solis](examples/functions.solis)** - Function definitions, lambdas, currying, and composition
- **[pattern_matching.solis](examples/pattern_matching.solis)** - Pattern matching on lists, integers, and nested structures
- **[lists.solis](examples/lists.solis)** - List construction, operations, and manipulation
- **[records.solis](examples/records.solis)** - Record types, field access, and updates
- **[algebraic_data_types.solis](examples/algebraic_data_types.solis)** - Defining and using ADTs
- **[lazy_evaluation.solis](examples/lazy_evaluation.solis)** - Lazy evaluation and infinite data structures
- **[do_notation.solis](examples/do_notation.solis)** - Do notation for sequential computations
- **[type_inference.solis](examples/type_inference.solis)** - How type inference works
- **[bigint.solis](examples/bigint.solis)** - Arbitrary-precision integer arithmetic
- **[higher_order.solis](examples/higher_order.solis)** - Higher-order functions and function composition

## Running Examples

To run any example file:

```bash
./solis run doc/examples/functions.solis
```

Or use the REPL to experiment:

```bash
./solis repl
```

Then load and evaluate expressions interactively.

## Language Reference

For detailed language specification and syntax, see the language reference documentation (coming soon).
