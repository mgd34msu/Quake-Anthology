/*
 * Port of src/compat/qvm/registry.ts.
 * Mirrored type (TS owner src/compat/qvm/interpreter.ts, parallel
 * slice): qa_qvmy_interpreter mirrors the QvmInterpreter surface this
 * module reads (codeLength, instructionPointersLength, memory length,
 * debugEnabled, symbols.printProfile via the landed symbols port).
 * Registration methods become functions taking the registration;
 * reserve returns a registry-owned slot registration by value whose
 * identity is (registry, slot, generation).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_REGISTRY_H
#define QA_COMPAT_QVM_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/symbols.h"

/* TS QvmExecutionProfile: kind "release" | "debug" (exact strings). */
typedef struct qa_qvm_execution_profile {
  const char *kind;
  int32_t trace; /* 0|1|2 when debug */
  int64_t break_function;
} qa_qvm_execution_profile;

typedef struct qa_qvmy_interpreter {
  void *ctx; /* TS-owner interpreter object */
  int64_t code_length;
  int64_t instruction_pointers_length;
  int64_t memory_length;
  bool debug_enabled;
  qa_qvm_symbols *symbols; /* borrowed */
} qa_qvmy_interpreter;

/* TS VmBinding: kind "initializing"|"interpreted"|"typescript"|"freed". */
typedef struct qa_qvm_vm_binding {
  const char *kind;
  const qa_qvmy_interpreter *interpreter; /* when interpreted */
} qa_qvm_vm_binding;

typedef void (*qa_qvm_registry_print_fn)(void *ctx, const char *text);
typedef qa_qvm_execution_profile (*qa_qvm_registry_profile_fn)(void *ctx);

typedef struct qa_qvm_vm_registry qa_qvm_vm_registry;

/* TS VmRegistration. */
typedef struct qa_qvm_vm_registration {
  char name[64];
  qa_qvm_vm_registry *registry;
  size_t slot;
  uint64_t generation;
} qa_qvm_vm_registration;

struct qa_qvm_vm_registry {
  qa_qvm_registry_print_fn print;
  void *print_ctx;
  qa_qvm_registry_profile_fn execution_profile;
  void *profile_ctx;
  void *slots; /* opaque, 3 slots */
  bool has_last_called;
  size_t last_called_slot;
  uint64_t last_called_generation;
  int64_t debug_level;
};

void qa_qvm_vm_registry_init(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print,
                             void *print_ctx, qa_qvm_registry_profile_fn execution_profile,
                             void *profile_ctx);
void qa_qvm_vm_registry_destroy(qa_qvm_vm_registry *registry);
void qa_qvm_vm_registry_debug(qa_qvm_vm_registry *registry, int64_t level);
qa_qvm_vm_registration qa_qvm_vm_registry_reserve(qa_qvm_vm_registry *registry, const char *name);
void qa_qvm_vm_registry_clear(qa_qvm_vm_registry *registry);
void qa_qvm_vm_registry_print_info(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print, void *print_ctx);
void qa_qvm_vm_registry_print_profile(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print, void *print_ctx);

qa_qvm_vm_binding qa_qvm_vm_registration_get_binding(const qa_qvm_vm_registration *registration);
qa_qvm_execution_profile qa_qvm_vm_registration_execution_profile(const qa_qvm_vm_registration *registration);
void qa_qvm_vm_registration_bind_data(const qa_qvm_vm_registration *registration, size_t memory_length);
void qa_qvm_vm_registration_bind_instruction_pointers_length(const qa_qvm_vm_registration *registration, int64_t length);
void qa_qvm_vm_registration_bind_code_length(const qa_qvm_vm_registration *registration, int64_t length);
void qa_qvm_vm_registration_bind_interpreter(const qa_qvm_vm_registration *registration, const qa_qvmy_interpreter *interpreter);
void qa_qvm_vm_registration_bind_typescript(const qa_qvm_vm_registration *registration);
void qa_qvm_vm_registration_called(const qa_qvm_vm_registration *registration);
void qa_qvm_vm_registration_debug(const qa_qvm_vm_registration *registration, int64_t level);
void qa_qvm_vm_registration_print(const qa_qvm_vm_registration *registration, const char *text);
void qa_qvm_vm_registration_print_call(const qa_qvm_vm_registration *registration, int64_t callnum);
void qa_qvm_vm_registration_free(const qa_qvm_vm_registration *registration);

#endif
