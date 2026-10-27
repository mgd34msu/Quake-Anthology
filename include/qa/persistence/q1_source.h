/*
 * Port of src/persistence/q1-source.ts: original Quake save
 * capture/restore against a staged QuakeC source.
 * TS QuakeCSource (owner: src/app/bootstrap/simulation/
 * quakec-source.ts) is a large app-bootstrap class with no C port
 * yet, so the members q1-source.ts reads are mirrored here as one
 * flattened fn + ctx table (TS member paths named per field); the
 * app port reconciles the names. Stable construction-time options
 * are values; live getters (timeSeconds, loading, entity count)
 * stay callbacks. Capture produces a fully owned qa_q1_save_data
 * (qa_q1_save_data_free). Both functions return false only on
 * allocation failure; plain TS Errors abort printing "Error: ..."
 * (the provider_ownership precedent).
 * Accommodations: the comment /\s/ replacement folds ASCII
 * whitespace bytes (JS \s also matches Unicode spaces; save
 * comments are byte text here). emit is specialized to the single
 * lightstyle event q1-source.ts emits.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q1_SOURCE_H
#define QA_PERSISTENCE_Q1_SOURCE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/persistence/q1.h"
#include "qa/persistence/q1_quakec.h"

/* Mirrored TS QuakeCSource (see header comment). */
typedef struct qa_quakec_source {
  void *ctx;
  qa_qc_machine machine;          /* source.machine */
  const char *kind;               /* source.kind: "netquake" | "quakeworld" */
  double max_clients;             /* source.options.maxClients */
  const char *mode;               /* source.options.mode */
  double skill;                   /* source.options.skill */
  const char *map_requested_path; /* source.options.recipe.map.geometry.requestedPath */
  double reserved_client_slots;   /* source.reservedClientSlots */
  qa_content_id owner_content;    /* source.prepared.execution.owner.content */
  void (*checkpoint)(void *ctx);  /* source.checkpoint(); result discarded */
  double (*time_seconds)(void *ctx); /* source.timeSeconds */
  bool (*loading)(void *ctx);        /* source.loading */
  /* source.options.events.lightStyle(index); NULL or "" falls back
   * to "m" (the TS ||). */
  const char *(*light_style)(void *ctx, size_t index);
  /* source.options.events.emit(owner_content, {kind:"q1", event:
   * {kind:"lightstyle", style, pattern}}). */
  void (*emit_lightstyle)(void *ctx, qa_content_id content, size_t style,
                          const char *pattern);
  size_t (*entity_count)(void *ctx); /* source.entities.count */
  void (*set_entity_count)(void *ctx, size_t count); /* source.entities.setCount */
  /* source.slots.at(slot); false for the TS null result. */
  bool (*slot_actor)(void *ctx, size_t slot, qa_owned_actor *out);
  /* source.slots.options.storage.read(slot).free */
  bool (*slot_is_free)(void *ctx, size_t slot);
  /* source.slots.options.storage.clearFreed(slot, now) */
  void (*clear_freed_slot)(void *ctx, size_t slot, qa_source_time now);
  /* source.slots.bindExisting(slot, definition) */
  qa_owned_actor (*bind_existing_slot)(void *ctx, size_t slot,
                                       const char *definition);
  /* source.slots.options.storage.initialize(slot, actor) */
  void (*initialize_slot)(void *ctx, size_t slot, qa_owned_actor actor);
  /* source.options.physics.bodies.unlink(actor) */
  void (*unlink_body)(void *ctx, qa_owned_actor actor);
  /* source.options.actors.release(actor) */
  void (*release_actor)(void *ctx, qa_owned_actor actor);
  /* source.worldHost.link(slot) */
  void (*link_slot)(void *ctx, size_t slot);
} qa_quakec_source;

/** The TS finish closure of restoreQ1SourceSave: fn + ctx. */
typedef struct qa_q1_source_finish {
  void *ctx;
  void (*fn)(void *ctx, qa_q1_save_header header);
} qa_q1_source_finish;

/* `game_directories` NULL spells a version 5 format;
 * `extension_text` NULL spells the TS "" default argument. */
bool qa_capture_q1_source_save(qa_quakec_source *source, int format_version,
                               const char *game_directories, const char *comment,
                               const double *spawn_parameters,
                               size_t spawn_parameter_count,
                               const char *extension_text,
                               qa_q1_save_data *out);
/* Called only on a staged, precached map with its one local client
 * already bound. */
bool qa_restore_q1_source_save(qa_quakec_source *source,
                               const qa_q1_save_data *save,
                               qa_q1_source_finish finish,
                               qa_q1_unknown_save_fields *out);

#endif
