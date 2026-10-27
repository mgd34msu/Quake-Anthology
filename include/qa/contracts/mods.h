/*
 * Port of src/contracts/mods.ts: mod selections, descriptions,
 * identities, and checkpoints. Runtime helpers live in mods.c.
 * TS throws plain Error for invalid selections; the C port aborts
 * with the identical message. String builders return malloc'd
 * strings (caller frees), NULL only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MODS_H
#define QA_CONTRACTS_MODS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/session.h"

typedef struct qa_mod_selection {
  const char *product;
  const char *id;
} qa_mod_selection;

/* availability: {kind:"available"} | {kind:"unavailable",reason}.
 * purpose: "game-type" | "addition" (exact strings). */
typedef struct qa_mod_description {
  qa_mod_selection selection;
  qa_provider_reference source;
  const char *title;
  const char *source_title;
  const char *purpose;
  const qa_mod_selection *requires;
  size_t requires_count;
  const qa_mod_selection *conflicts;
  size_t conflicts_count;
  bool is_available;
  const char *unavailable_reason;
} qa_mod_description;

/* TS declaration union: ModCallbackDeclaration |
 * QvmModCallbackDeclaration | NativeModDeclaration. The C form
 * carries its runtime kind ("quakec"|"qvm"|"native", the TS
 * declarations' `runtime` strings) in session/runtime fields; the
 * union is discriminated by `runtime`. */
typedef enum qa_mod_declaration_runtime {
  QA_MOD_RUNTIME_QUAKEC, QA_MOD_RUNTIME_QVM, QA_MOD_RUNTIME_NATIVE
} qa_mod_declaration_runtime;
typedef struct qa_mod_declaration {
  qa_mod_declaration_runtime runtime;
  union {
    const qa_mod_callback_declaration *quakec;
    const qa_qvm_mod_callback_declaration *qvm;
    const qa_native_mod_declaration *native;
  } as;
} qa_mod_declaration;

struct qa_resolved_gameplay_mod {
  qa_mod_selection selection;
  qa_provider_reference source;
  const char *title;
  const char *source_title;
  const qa_mod_selection *requires;
  size_t requires_count;
  const qa_mod_selection *conflicts;
  size_t conflicts_count;
  qa_mod_declaration declaration;
  qa_content_digest declaration_digest;
};

/* TS modInstanceProvider: `mod:${encodeURIComponent(key)}`. */
char *qa_mod_instance_provider(qa_mod_selection selection);

typedef struct qa_mod_identity {
  qa_mod_selection selection;
  qa_provider_reference source;
  qa_content_digest declaration_digest;
  const qa_module_identity *modules;
  size_t module_count;
  /* TS Omit<ProviderCheckpoint, "bytes"> rows. */
  const qa_provider_checkpoint_ref *providers;
  size_t provider_count;
} qa_mod_identity;

typedef struct qa_mod_private_checkpoint {
  const qa_guest_checkpoint *guests;
  size_t guest_count;
  const qa_provider_checkpoint *providers;
  size_t provider_count;
} qa_mod_private_checkpoint;

typedef struct qa_mod_checkpoint {
  qa_mod_identity identity;
  qa_mod_private_checkpoint state;
} qa_mod_checkpoint;

/* TS version is the literal 1. */
struct qa_mod_session_checkpoint {
  int64_t version;
  const qa_mod_checkpoint *mods;
  size_t mod_count;
};

/* state may be null in TS (NULL here). */
typedef struct qa_mod_travel_entry {
  qa_mod_identity identity;
  const qa_mod_private_checkpoint *state;
} qa_mod_travel_entry;
typedef struct qa_mod_travel_checkpoint {
  int64_t version; /* TS literal 1 */
  const qa_mod_travel_entry *mods;
  size_t mod_count;
} qa_mod_travel_checkpoint;

/* "product/id"; aborts when either component fails the TS patterns. */
char *qa_mod_selection_key(qa_mod_selection selection);
/* Parses "PRODUCT/COMPONENT_ID". On success out->product / out->id
 * are malloc'd (release with qa_mod_selection_free). Returns false
 * only on allocation failure; malformed input aborts. */
bool qa_read_mod_selection(const char *value, qa_mod_selection *out);
void qa_mod_selection_free(qa_mod_selection *selection);

bool qa_same_mod_identity(const qa_mod_identity *left, const qa_mod_identity *right);

#endif
