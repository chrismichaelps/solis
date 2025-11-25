// Solis Programming Language - Runtime Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include <cstddef>
#include <cstdint>

// Runtime system for Solis LLVM backend
// Provides memory management, lazy evaluation, and builtin operations

extern "C" {

// Memory Management (GC Integration)

// Allocate memory with garbage collection
void* solis_alloc(size_t size);

// Allocate atomic (non-pointer) memory
void* solis_alloc_atomic(size_t size);

// Lazy Evaluation (Thunks)

// Thunk structure
struct SolisThunk {
  void* (*compute)(void* env);
  void* env;
  void* cached;
  bool evaluated;
};

// Create a new thunk
void* solis_create_thunk(void* (*compute)(void*), void* env);

// Force evaluation of a thunk
void* solis_force(void* thunk);

// String Operations

// Concatenate two strings (GC-allocated)
char* solis_string_concat(char* s1, char* s2);

// String equality
bool solis_string_eq(char* s1, char* s2);

// String length
int64_t solis_string_length(char* str);

// List Operations

// Cons cell structure
struct SolisCons {
  void* head;
  void* tail;
};

// Create a cons cell
void* solis_cons(void* head, void* tail);

// Get list length
int64_t solis_list_length(void* list);
int64_t solis_length(void* list);  // Added by user

// Check if list is empty
bool solis_is_nil(void* list);

// Get head of list (unsafe, must check is_nil first)
void* solis_head(void* list);

// Get tail of list (unsafe, must check is_nil first)
void* solis_tail(void* list);

// I/O Operations

// Print string to stdout (returns Unit)
void* solis_print(char* str);

// Print string with newline
void solis_println(char* str);

// Read line from stdin
char* solis_read_line();

// Conversion Functions

// Integer to string
char* solis_int_to_string(int64_t n);

// Float to string
char* solis_float_to_string(double f);

// Bool to string
char* solis_bool_to_string(bool b);

// Pattern Matching Helpers

// ADT structure (tagged union)
struct SolisADT {
  int32_t tag;
  void* data[];
};

// Create ADT value
void* solis_create_adt(int32_t tag, int32_t num_fields, ...);

// Get ADT tag
int32_t solis_get_tag(void* adt);

// Extract field from ADT
void* solis_get_field(void* adt, int32_t index);

// Record Operations

// Record structure (field array)
struct SolisRecord {
  int32_t num_fields;
  void* fields[];
};

// Create record
void* solis_create_record(int32_t num_fields, ...);

// Get record field
void* solis_record_get(void* record, int32_t index);

// Update record field (creates new record)
void* solis_record_update(void* record, int32_t index, void* value);

// Closure Operations

// Closure structure
struct SolisClosure {
  void* fn_ptr;
  void* env;
};

// Create closure
void* solis_create_closure(void* fn_ptr, void* env);

// Apply closure
void* solis_apply_closure(void* closure, void* arg);

// multi-variable closures
void* solis_create_closure_multi(void* fn_ptr, int envSize, ...);
void* solis_closure_env_get(void* closure, int index);
void* solis_call_closure_multi(void* closure, void* arg);

// Error Handling

// Runtime error (terminates with message)
[[noreturn]] void solis_error(char* message);

// Pattern match failure
[[noreturn]] void solis_match_failure();

// Division by zero
[[noreturn]] void solis_division_by_zero();

}  // extern "C"
