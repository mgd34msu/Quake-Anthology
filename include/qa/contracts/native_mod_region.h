/*
 * Port of src/contracts/native-mod-region.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_NATIVE_MOD_REGION_H
#define QA_CONTRACTS_NATIVE_MOD_REGION_H

#include <stddef.h>

#include "qa/contracts/native_mod_callbacks.h"

/* TS NativeModRegionRegister: "rax"|"rcx"|"rdx"|"rbx"|"rbp"|"rsi"|
 * "rdi"|"r8"|"r9"|"r10"|"r11"|"r12"|"r13"|"r14"|"r15" (exact strings). */
typedef const char *qa_native_mod_region_register;
/* TS NativeModRegionStorage = NativeModScalar | "pointer". */
typedef const char *qa_native_mod_region_storage;

/* TS NativeModRegionLocation union on kind: "register" | "simd" | "stack". */
typedef enum qa_native_mod_region_location_kind {
  QA_NATIVE_MOD_LOCATION_REGISTER, QA_NATIVE_MOD_LOCATION_SIMD, QA_NATIVE_MOD_LOCATION_STACK
} qa_native_mod_region_location_kind;
typedef struct qa_native_mod_region_location {
  qa_native_mod_region_location_kind kind;
  qa_native_mod_region_register reg; /* register */
  double index;                      /* simd */
  double offset;                     /* simd/stack */
  qa_native_mod_region_storage storage;
} qa_native_mod_region_location;

typedef struct qa_native_mod_region_input {
  qa_native_mod_region_location target;
  qa_native_mod_value value;
} qa_native_mod_region_input;

typedef struct qa_native_mod_region_frame {
  double entry, exit, stack_bytes, argument_bytes;
} qa_native_mod_region_frame;

/* TS abi is the literal "source-region". */
typedef struct qa_native_mod_protection_region {
  qa_native_mod_source_call call;
  qa_native_mod_region_frame frame;
  double entry, join;
  const qa_native_mod_region_input *inputs;
  size_t input_count;
  qa_native_mod_region_location result;
} qa_native_mod_protection_region;

#endif
