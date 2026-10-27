/*
 * Port of src/persistence/q2-classic-guest.ts: the classic native
 * (Windows DLL) original save payload and the callback file overlay
 * that captures/restores the DLL's own game.ssv/level.sav bytes.
 * Ownership: decoded saves borrow strings/bytes from `root` (owned;
 * entry arrays are malloc'd); captured saves own their byte copies
 * and entry arrays but borrow identity/entry strings. Either form is
 * released with qa_q2_classic_original_save_free. The TS api/abi
 * literals ("q2-classic-game"/3, "windows-i386") are constants of the
 * format here: encoders always write them, decoders require them.
 * TS `throw new Error(msg)` aborts printing "Error: msg" (the
 * qa_range_error precedent); RangeError keeps qa_range_error.
 * Async *Loading variants are synchronous in C (PORTING) and share
 * the synchronous implementation. File handles stay valid (but
 * retired) until the overlay is destroyed, mirroring the TS test
 * that a retained handle fails loudly instead of dangling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_CLASSIC_GUEST_H
#define QA_PERSISTENCE_Q2_CLASSIC_GUEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_classic_original_identity {
  qa_module_identity module;
  const char *map;
} qa_q2_classic_original_identity;

typedef struct qa_q2_classic_configstring_entry {
  double index;
  const char *value;
} qa_q2_classic_configstring_entry;
typedef struct qa_q2_classic_portal_entry {
  double portal;
  bool open;
} qa_q2_classic_portal_entry;
typedef struct qa_q2_classic_level_state {
  qa_q2_classic_configstring_entry *configstrings;
  size_t configstring_count;
  qa_q2_classic_portal_entry *portals;
  size_t portal_count;
} qa_q2_classic_level_state;

typedef struct qa_q2_classic_visited_level {
  qa_q2_classic_level_state state;
  double version; /* TS literal 1 */
  const char *map;
  qa_byte_view level;
} qa_q2_classic_visited_level;

typedef struct qa_q2_classic_original_server_state {
  qa_q2_classic_level_state state;
  qa_byte_view cvars;
} qa_q2_classic_original_server_state;

typedef struct qa_q2_classic_original_save {
  qa_module_identity module;
  const char *map;
  bool autosave;
  qa_q2_classic_original_server_state server;
  qa_byte_view game;
  qa_byte_view level;
  qa_q2_classic_visited_level *visited_levels;
  size_t visited_level_count;
  qa_save_value *root; /* owned when decoded; NULL when captured/built */
} qa_q2_classic_original_save;

/* Returns false only on OOM. out->bytes.bytes is malloc'd (caller
 * frees); out->provider borrows save->module.id and out->schema is a
 * string literal. */
bool qa_encode_q2_classic_original_save(const qa_q2_classic_original_save *save,
                                        qa_provider_checkpoint *out);
/* `record` borrows its bytes. False only on OOM. */
bool qa_decode_q2_classic_original_save(const qa_provider_checkpoint *record,
                                        qa_q2_classic_original_identity expected,
                                        qa_q2_classic_original_save *out);
void qa_q2_classic_original_save_free(qa_q2_classic_original_save *save);

/* TS WindowsFile table. Field-for-field identical to qa_windows_file
 * (qa/guest/runtime/windows/contracts.h); that header cannot be mixed
 * with qa/contracts/execution.h in one translation unit (duplicate
 * GuestAddress spellings), so the overlay exposes the same shape
 * under a local name and guest-side TUs cast between them. */
typedef struct qa_q2_classic_file {
  void *ctx;
  size_t (*read)(void *ctx, size_t offset, uint8_t *out, size_t length);
  int64_t (*write)(void *ctx, size_t offset, const uint8_t *bytes, size_t len);
  int64_t (*size)(void *ctx);
  void (*truncate)(void *ctx, size_t length);
  void (*flush)(void *ctx);
  void (*close)(void *ctx);
} qa_q2_classic_file;
typedef qa_q2_classic_file *(*qa_q2_classic_open_file_fn)(void *ctx, const char *path,
                                                         bool read, bool write, int creation);
typedef void (*qa_q2_classic_server_state_fn)(void *ctx,
                                              qa_q2_classic_original_server_state *out);
typedef void (*qa_q2_classic_write_fn)(void *ctx, const char *game_path,
                                       const char *level_path, bool autosave);
typedef void (*qa_q2_classic_read_fn)(void *ctx, const char *game_path,
                                      const char *level_path);
typedef void (*qa_q2_classic_level_write_fn)(void *ctx, const char *level_path);
typedef void *(*qa_q2_classic_level_read_fn)(void *ctx, const char *level_path);

typedef struct qa_classic_original_save_files qa_classic_original_save_files;

/* NULL fallback means ordinary paths open as TS null. NULL on OOM. */
qa_classic_original_save_files *qa_classic_original_save_files_create(
    qa_q2_classic_open_file_fn fallback, void *fallback_ctx);
void qa_classic_original_save_files_destroy(qa_classic_original_save_files *files);
/* Matches TS ClassicOriginalSaveFiles.openFile. Outside the reserved
 * root the fallback (or NULL) answers; otherwise overlay rules. */
qa_q2_classic_file *qa_classic_original_save_files_open_file(
    qa_classic_original_save_files *files, const char *path, bool read, bool write,
    int creation);

/* False only on OOM; protocol violations abort as the TS throws. */
bool qa_classic_original_save_files_capture(
    qa_classic_original_save_files *files, qa_q2_classic_original_identity identity,
    qa_q2_classic_server_state_fn server_state, void *server_ctx,
    qa_q2_classic_write_fn write, void *write_ctx, bool autosave,
    qa_q2_classic_original_save *out);
void qa_classic_original_save_files_restore(
    qa_classic_original_save_files *files, const qa_q2_classic_original_save *save,
    qa_q2_classic_original_identity expected, qa_q2_classic_read_fn read, void *read_ctx);
/* TS async restoreLoading; synchronous in C. */
void qa_classic_original_save_files_restore_loading(
    qa_classic_original_save_files *files, const qa_q2_classic_original_save *save,
    qa_q2_classic_original_identity expected, qa_q2_classic_read_fn read, void *read_ctx);
/* Malloc'd level bytes in *out_bytes; false only on OOM. */
bool qa_classic_original_save_files_capture_travel_level(
    qa_classic_original_save_files *files, qa_q2_classic_level_write_fn write,
    void *write_ctx, uint8_t **out_bytes, size_t *out_length);
bool qa_classic_original_save_files_capture_travel_level_loading(
    qa_classic_original_save_files *files, qa_q2_classic_level_write_fn write,
    void *write_ctx, uint8_t **out_bytes, size_t *out_length);
/* Returns the callback's own result (TS withTravelLevel<T>). */
void *qa_classic_original_save_files_with_travel_level(
    qa_classic_original_save_files *files, const uint8_t *bytes, size_t length,
    qa_q2_classic_level_read_fn read, void *read_ctx);
void *qa_classic_original_save_files_with_travel_level_loading(
    qa_classic_original_save_files *files, const uint8_t *bytes, size_t length,
    qa_q2_classic_level_read_fn read, void *read_ctx);

#endif
