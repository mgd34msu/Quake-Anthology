/*
 * Port of src/contracts/original-pickups.ts (types only): original
 * pickup offers, rules, and admission shapes. Method interfaces
 * become function tables + ctx. TS generics ModPickupRule<Call> /
 * OriginalPickupOperation<Call> keep the call payload as an opaque
 * pointer plus elem size (the concrete call type belongs to the
 * instantiating module). runSource<Result> may return a Promise in
 * TS; the C form is synchronous and returns an opaque result pointer.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_ORIGINAL_PICKUPS_H
#define QA_CONTRACTS_ORIGINAL_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"

/* TS PickupResource union on kind: "protection"(channel) |
 * "inventory"(item). PickupWrite is the protection variant plus
 * "inventory" with fields "count"|"capacity"|"count-and-capacity".
 * One struct serves both (fields unused for resources). */
typedef enum qa_pickup_resource_kind {
  QA_PICKUP_RESOURCE_PROTECTION, QA_PICKUP_RESOURCE_INVENTORY
} qa_pickup_resource_kind;
typedef struct qa_pickup_resource {
  qa_pickup_resource_kind kind;
  qa_protection_channel channel; /* protection */
  qa_item_id item;               /* inventory */
} qa_pickup_resource;
typedef struct qa_pickup_write {
  qa_pickup_resource_kind kind;
  qa_protection_channel channel; /* protection */
  qa_item_id item;               /* inventory */
  const char *fields; /* inventory: "count"|"capacity"|"count-and-capacity" */
} qa_pickup_write;
/* TS PickupWrites is a nonempty readonly tuple: pointer + count. */
typedef struct qa_pickup_writes {
  const qa_pickup_write *values;
  size_t count;
} qa_pickup_writes;

/* TS PickupCount: {kind:"default"} | {kind:"override",amount}. */
typedef struct qa_pickup_count {
  bool is_override;
  double amount;
} qa_pickup_count;

/* TS PickupCargoEntry.kind: "counter" | "weapon" (exact strings). */
typedef struct qa_pickup_cargo_entry {
  const char *kind;
  qa_item_id item;
  double count;
} qa_pickup_cargo_entry;

typedef struct qa_original_pickup_offer {
  qa_actor_id recipient;
  qa_actor_id pickup;
  qa_provider_id source;
  qa_item_id item;
  const qa_pickup_resource *default_resource; /* nullable */
  qa_pickup_count count;
  bool dropped;
  qa_source_time time;
  const qa_pickup_cargo_entry *cargo; /* optional */
  size_t cargo_count;
  const char *grant; /* optional: "map-coupled" | "source-effect" */
} qa_original_pickup_offer;

/* TS OriginalPickupDecision = "accepted" | "refused". */
typedef enum qa_original_pickup_decision {
  QA_ORIGINAL_PICKUP_ACCEPTED, QA_ORIGINAL_PICKUP_REFUSED
} qa_original_pickup_decision;
/* TS OriginalPickupOutcome = OriginalPickupDecision | "stale". */
typedef enum qa_original_pickup_outcome {
  QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED = QA_ORIGINAL_PICKUP_ACCEPTED,
  QA_ORIGINAL_PICKUP_OUTCOME_REFUSED = QA_ORIGINAL_PICKUP_REFUSED,
  QA_ORIGINAL_PICKUP_OUTCOME_STALE
} qa_original_pickup_outcome;

typedef struct qa_original_pickup_execution qa_original_pickup_execution;
typedef struct qa_original_pickup_rule {
  void *ctx;
  const char *id;
  const qa_item_id *offered;
  size_t offered_count;
  qa_pickup_writes writes;
  qa_original_pickup_decision (*take)(void *ctx, qa_original_pickup_offer offer,
                                      qa_original_pickup_execution execution);
} qa_original_pickup_rule;

/** TS OriginalPickupExecution extends ProtectionObserver. */
struct qa_original_pickup_execution {
  qa_protection_observer observer;
  qa_pickup_writes writes;
  void *current_ctx;
  bool (*current)(void *current_ctx);
};

typedef struct qa_current_original_pickup {
  void *ctx;
  qa_provider_id owner;
  qa_original_pickup_rule operation;
  qa_original_pickup_rule captured;
  qa_pickup_write write;
  bool (*current)(void *ctx);
} qa_current_original_pickup;

typedef struct qa_original_pickup_resolution {
  const qa_current_original_pickup *matches;
  size_t match_count;
  bool blocks_primary;
} qa_original_pickup_resolution;

/** TS OriginalPickupContinuation; eligible() is optional (nullable). */
typedef struct qa_original_pickup_continuation {
  void *ctx;
  bool (*eligible)(void *ctx);
  bool (*original)(void *ctx);
  void (*complete)(void *ctx, bool taken);
} qa_original_pickup_continuation;

/* TS SourcePickupSelection: {kind:"original"|"blocked"|"stale"} |
 * {kind:"replacement", current(), grant()}. */
typedef enum qa_source_pickup_selection_kind {
  QA_SOURCE_PICKUP_ORIGINAL, QA_SOURCE_PICKUP_BLOCKED,
  QA_SOURCE_PICKUP_STALE, QA_SOURCE_PICKUP_REPLACEMENT
} qa_source_pickup_selection_kind;
typedef struct qa_source_pickup_selection {
  qa_source_pickup_selection_kind kind;
  void *ctx; /* replacement only */
  bool (*current)(void *ctx);
  qa_original_pickup_outcome (*grant)(void *ctx);
} qa_source_pickup_selection;

/** TS SourcePickupLifetime; the remove closure is fn ptr + ctx. */
typedef struct qa_source_pickup_lifetime {
  void *ctx;
  void (*consume_pickup)(void *ctx, void (*remove)(void *remove_ctx), void *remove_ctx);
} qa_source_pickup_lifetime;

/** TS OriginalPickupAdmission. */
typedef struct qa_original_pickup_admission {
  void *ctx;
  void *(*run_source)(void *ctx, qa_original_pickup_offer offer,
                      void *(*execute)(void *execute_ctx,
                                       qa_source_pickup_selection selection,
                                       qa_source_pickup_lifetime lifetime),
                      void *execute_ctx);
  qa_original_pickup_outcome (*touch)(void *ctx, qa_original_pickup_offer offer,
                                      qa_original_pickup_continuation continuation);
} qa_original_pickup_admission;

/* TS OriginalPickupOperation<Call>: {kind:"boolean-grant",grant} |
 * {kind:"gate-then-grant",gate,grant,grantAccepts}. */
typedef struct qa_original_pickup_operation {
  bool is_gate_then_grant;
  const void *gate;  /* Call, gate-then-grant only */
  const void *grant; /* Call */
  size_t call_elem_size;
  const char *grant_accepts; /* "nonzero" | "always" */
} qa_original_pickup_operation;

/* TS ModPickupRule<Call>. */
typedef struct qa_mod_pickup_rule {
  const char *id;
  qa_pickup_writes writes;
  const qa_item_id *offered;
  size_t offered_count;
  qa_original_pickup_operation operation;
} qa_mod_pickup_rule;

#endif
