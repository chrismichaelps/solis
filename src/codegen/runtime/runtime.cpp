// Solis Programming Language - Runtime System
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/runtime/runtime.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Memory allocation using malloc
// Production use requires Boehm GC integration for automatic garbage collection

extern "C" {

// Memory Management

void* solis_alloc(size_t size) {
  void* ptr = malloc(size);
  if (!ptr) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  return ptr;
}

void* solis_alloc_atomic(size_t size) {
  // same as regular alloc
  // With Boehm GC, this would use GC_MALLOC_ATOMIC
  return solis_alloc(size);
}

// Lazy Evaluation (Thunks)

void* solis_create_thunk(void* (*compute)(void*), void* env) {
  SolisThunk* thunk = (SolisThunk*)solis_alloc(sizeof(SolisThunk));
  thunk->compute = compute;
  thunk->env = env;
  thunk->cached = nullptr;
  thunk->evaluated = false;
  return thunk;
}

void* solis_force(void* thunk_ptr) {
  if (!thunk_ptr) {
    return nullptr;
  }

  SolisThunk* thunk = (SolisThunk*)thunk_ptr;

  if (!thunk->evaluated) {
    thunk->cached = thunk->compute(thunk->env);
    thunk->evaluated = true;
  }

  return thunk->cached;
}

// String Operations

char* solis_string_concat(char* s1, char* s2) {
  if (!s1 || !s2) {
    return nullptr;
  }

  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  char* result = (char*)solis_alloc_atomic(len1 + len2 + 1);

  strcpy(result, s1);
  strcat(result, s2);

  return result;
}

bool solis_string_eq(char* s1, char* s2) {
  if (s1 == s2) {
    return true;
  }
  if (!s1 || !s2) {
    return false;
  }
  return strcmp(s1, s2) == 0;
}

int64_t solis_string_length(char* str) {
  return str ? strlen(str) : 0;
}

// List Operations

void* solis_cons(void* head, void* tail) {
  SolisCons* cons = (SolisCons*)solis_alloc(sizeof(SolisCons));
  cons->head = head;
  cons->tail = tail;
  return cons;
}

int64_t solis_list_length(void* list) {
  int64_t len = 0;
  SolisCons* current = (SolisCons*)list;

  while (current != nullptr) {
    len++;
    current = (SolisCons*)current->tail;
  }

  return len;
}

bool solis_is_nil(void* list) {
  return list == nullptr;
}

void* solis_head(void* list) {
  if (!list) {
    solis_error((char*)"head of empty list");
  }
  return ((SolisCons*)list)->head;
}

void* solis_tail(void* list) {
  if (!list) {
    solis_error((char*)"tail of empty list");
  }
  return ((SolisCons*)list)->tail;
}

// Alias for solis_list_length
int64_t solis_length(void* list) {
  return solis_list_length(list);
}

// I/O Operations

void* solis_print(char* str) {
  printf("%s\n", str);
  return nullptr;  // Return Unit (null)
}

void solis_println(char* str) {
  if (str) {
    printf("%s\n", str);
  } else {
    printf("\n");
  }
  fflush(stdout);
}

char* solis_read_line() {
  char buffer[4096];
  if (fgets(buffer, sizeof(buffer), stdin)) {
    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }

    // Copy to GC-allocated memory
    char* result = (char*)solis_alloc_atomic(strlen(buffer) + 1);
    strcpy(result, buffer);
    return result;
  }
  return nullptr;
}

// Conversion Functions

char* solis_int_to_string(int64_t n) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lld", (long long)n);

  char* result = (char*)solis_alloc_atomic(strlen(buffer) + 1);
  strcpy(result, buffer);
  return result;
}

char* solis_float_to_string(double f) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%g", f);

  char* result = (char*)solis_alloc_atomic(strlen(buffer) + 1);
  strcpy(result, buffer);
  return result;
}

char* solis_bool_to_string(bool b) {
  const char* str = b ? "true" : "false";
  char* result = (char*)solis_alloc_atomic(strlen(str) + 1);
  strcpy(result, str);
  return result;
}

// Pattern Matching Helpers

void* solis_create_adt(int32_t tag, int32_t num_fields, ...) {
  size_t size = sizeof(SolisADT) + num_fields * sizeof(void*);
  SolisADT* adt = (SolisADT*)solis_alloc(size);
  adt->tag = tag;

  va_list args;
  va_start(args, num_fields);
  for (int32_t i = 0; i < num_fields; i++) {
    adt->data[i] = va_arg(args, void*);
  }
  va_end(args);

  return adt;
}

int32_t solis_get_tag(void* adt) {
  if (!adt) {
    solis_error((char*)"get_tag on null ADT");
  }
  return ((SolisADT*)adt)->tag;
}

void* solis_get_field(void* adt, int32_t index) {
  if (!adt) {
    solis_error((char*)"get_field on null ADT");
  }
  return ((SolisADT*)adt)->data[index];
}

// Record Operations

void* solis_create_record(int32_t num_fields, ...) {
  size_t size = sizeof(SolisRecord) + num_fields * sizeof(void*);
  SolisRecord* record = (SolisRecord*)solis_alloc(size);
  record->num_fields = num_fields;

  va_list args;
  va_start(args, num_fields);
  for (int32_t i = 0; i < num_fields; i++) {
    record->fields[i] = va_arg(args, void*);
  }
  va_end(args);

  return record;
}

void* solis_record_get(void* record_ptr, int32_t index) {
  if (!record_ptr) {
    solis_error((char*)"record_get on null record");
  }

  SolisRecord* record = (SolisRecord*)record_ptr;
  if (index < 0 || index >= record->num_fields) {
    solis_error((char*)"record field index out of bounds");
  }

  return record->fields[index];
}

void* solis_record_update(void* record_ptr, int32_t index, void* value) {
  if (!record_ptr) {
    solis_error((char*)"record_update on null record");
  }

  SolisRecord* old_record = (SolisRecord*)record_ptr;
  if (index < 0 || index >= old_record->num_fields) {
    solis_error((char*)"record field index out of bounds");
  }

  // Create new record with updated field
  size_t size = sizeof(SolisRecord) + old_record->num_fields * sizeof(void*);
  SolisRecord* new_record = (SolisRecord*)solis_alloc(size);
  new_record->num_fields = old_record->num_fields;

  // Copy all fields
  for (int32_t i = 0; i < old_record->num_fields; i++) {
    new_record->fields[i] = old_record->fields[i];
  }

  // Update the specified field
  new_record->fields[index] = value;

  return new_record;
}

// Closure Operations

void* solis_create_closure(void* fn_ptr, void* env) {
  SolisClosure* closure = (SolisClosure*)solis_alloc(sizeof(SolisClosure));
  closure->fn_ptr = fn_ptr;
  closure->env = env;
  return closure;
}

void* solis_apply_closure(void* closure_ptr, void* arg) {
  if (!closure_ptr) {
    solis_error((char*)"apply_closure on null closure");
  }

  SolisClosure* closure = (SolisClosure*)closure_ptr;

  // Cast function pointer and call
  typedef void* (*ClosureFn)(void*, void*);
  ClosureFn fn = (ClosureFn)closure->fn_ptr;

  return fn(closure->env, arg);
}

// closure with multiple captured variables
void* solis_create_closure_multi(void* fn_ptr, int envSize, ...) {
  // Allocate closure with inline environment array
  size_t size = sizeof(SolisClosure) + envSize * sizeof(void*);
  void** closure_mem = (void**)solis_alloc(size);

  // First element is function pointer
  closure_mem[0] = fn_ptr;

  // Rest are environment variables
  va_list args;
  va_start(args, envSize);
  for (int i = 0; i < envSize; i++) {
    closure_mem[i + 1] = va_arg(args, void*);
  }
  va_end(args);

  return closure_mem;
}

// Get environment variable from multi-closure
void* solis_closure_env_get(void* closure, int index) {
  if (!closure) {
    solis_error((char*)"closure_env_get on null closure");
  }
  void** closure_mem = (void**)closure;
  // Index + 1 because index 0 is function pointer
  return closure_mem[index + 1];
}

// Call multi-closure
void* solis_call_closure_multi(void* closure, void* arg) {
  if (!closure) {
    solis_error((char*)"call_closure_multi on null closure");
  }

  void** closure_mem = (void**)closure;
  void* fn_ptr = closure_mem[0];

  // Function signature: ptr fn(ptr closure, ptr arg)
  typedef void* (*ClosureFn)(void*, void*);
  ClosureFn fn = (ClosureFn)fn_ptr;

  return fn(closure, arg);
}

// Error Handling

[[noreturn]] void solis_error(char* message) {
  fprintf(stderr, "Runtime error: %s\n", message);
  exit(1);
}

[[noreturn]] void solis_match_failure() {
  fprintf(stderr, "Runtime error: Non-exhaustive pattern match\n");
  exit(1);
}

[[noreturn]] void solis_division_by_zero() {
  fprintf(stderr, "Runtime error: Division by zero\n");
  exit(1);
}

}  // extern "C"
