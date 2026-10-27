/*
 * Port of src/persistence/q1-quakec.ts: capture/restore of original
 * Quake saves against a QuakeC machine.
 * TS QcMachine (owner: src/compat/qc/machine.ts), its entity store
 * (src/compat/qc/memory.ts) and the pair codecs (src/compat/qc/
 * save.ts) have no C port yet, so the surface q1-quakec.ts uses is
 * mirrored here as an fn + ctx table; the compat port reconciles
 * the names. Pair records are qa_q1_text_record (q1.h already
 * carries TS QcTextPair as qa_q1_text_pair): save_* callbacks
 * return malloc'd records whose ownership moves into the produced
 * save/result, released with qa_q1_save_data_free /
 * qa_q1_unknown_save_fields_free. The apply_* `retained` record is
 * freed by restore after each entity (the TS relies on GC).
 * qa_capture_q1_quakec_save takes ownership of the header's
 * strings/arrays on success (the TS spread shares them); on
 * failure the caller still owns the header. The header handed to
 * host finish borrows the save's strings/arrays.
 * Functions return false only on allocation failure; the TS
 * SaveFormatError aborts via qa_save_format_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q1_QUAKEC_H
#define QA_PERSISTENCE_Q1_QUAKEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/q1.h"

/* TS Q1SaveHeader = Omit<Q1SaveData, "globals" | "entities">. */
typedef struct qa_q1_save_header {
  int format_version;       /* 5 or 6 */
  char *game_directories;   /* version 6 only, NULL for 5 */
  char *comment;
  double *spawn_parameters;
  size_t spawn_parameter_count;
  double skill;
  char *map;
  double time;
  char **light_styles;
  size_t light_style_count;
  char *extension_text;
  size_t extension_length;
} qa_q1_save_header;

/* TS QcEntityParseResult (src/compat/qc/save.ts). */
typedef struct qa_qc_entity_parse_result {
  bool empty;
  qa_q1_text_record unknown;  /* owned */
  qa_q1_text_record retained; /* owned */
} qa_qc_entity_parse_result;

/* Mirrored TS QcMachine + src/compat/qc/save.ts (see header). */
typedef struct qa_qc_machine {
  void *ctx;
  /* Called for the TS reentry check; the snapshot is discarded. */
  void (*snapshot)(void *ctx);
  size_t (*entity_count)(void *ctx);
  size_t (*entity_capacity)(void *ctx);
  void (*set_entity_count)(void *ctx, size_t count);
  /* Borrowed mutable field bytes of entities.at(slot).bytes. */
  uint8_t *(*entity_bytes)(void *ctx, size_t slot, size_t *out_length);
  qa_q1_text_record (*save_global_pairs)(void *ctx);
  qa_q1_text_record (*save_entity_pairs)(void *ctx, size_t slot, bool is_free);
  qa_q1_text_record (*apply_global_pairs)(void *ctx,
                                          const qa_q1_text_record *pairs);
  qa_qc_entity_parse_result (*apply_entity_pairs)(
      void *ctx, size_t slot, const qa_q1_text_record *pairs);
} qa_qc_machine;

/** TS Q1QuakeCSaveHost. */
typedef struct qa_q1_quakec_save_host {
  void *ctx;
  /* Unlink old entities and prepare the original host's free flags
   * before record loading. */
  void (*begin)(void *ctx, size_t entity_count);
  /* Update host metadata and link a non-free edict without
   * executing its spawn function. */
  void (*entity)(void *ctx, size_t slot, bool is_free);
  /* Restore server time, spawn parameters, light styles and other
   * header-owned state. */
  void (*finish)(void *ctx, qa_q1_save_header header);
} qa_q1_quakec_save_host;

/** TS Q1UnknownSaveFields. */
typedef struct qa_q1_unknown_entity_fields {
  size_t slot;
  qa_q1_text_record fields; /* owned */
} qa_q1_unknown_entity_fields;
typedef struct qa_q1_unknown_save_fields {
  qa_q1_text_record globals; /* owned */
  qa_q1_unknown_entity_fields *entities; /* owned */
  size_t entity_count;
} qa_q1_unknown_save_fields;
void qa_q1_unknown_save_fields_free(qa_q1_unknown_save_fields *fields);

/** The TS isFree closure: fn + ctx. */
typedef struct qa_q1_quakec_is_free {
  void *ctx;
  bool (*fn)(void *ctx, size_t slot);
} qa_q1_quakec_is_free;

bool qa_capture_q1_quakec_save(qa_qc_machine *machine, qa_q1_save_header header,
                               qa_q1_quakec_is_free is_free,
                               qa_q1_save_data *out);
/* Apply after loading the matching progs and map baseline, as
 * Host_Loadgame_f does. */
bool qa_restore_q1_quakec_save(qa_qc_machine *machine,
                               const qa_q1_save_data *save,
                               qa_q1_quakec_save_host host,
                               qa_q1_unknown_save_fields *out);

#endif
