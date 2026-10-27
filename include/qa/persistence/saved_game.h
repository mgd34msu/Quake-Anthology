/*
 * Port of src/persistence/saved-game.ts: signature dispatch between
 * the shared QTSAVE image and original Quake (Q1) source saves.
 * C accommodations:
 *  - TS async read/write (Bun.file) are synchronous; they return
 *    false on I/O failure (the TS promise rejects) or OOM.
 *  - The decoded Q1 payload is owned (qa_q1_save_data_free via
 *    qa_saved_game_free); a shared image borrows per save_image.h.
 *  - BLOCKED: the shared ("QT") branch needs decodeSaveImage /
 *    encodeSaveImage from save-image.ts, which have no C port yet
 *    (see save_image.h). qa_decode_saved_game reproduces that
 *    codec's signature, envelope and schemaVersion checks exactly
 *    (malformed shared saves abort with the same SaveFormatError
 *    and never fall back to source parsing), then aborts with a
 *    C-only "not yet ported" SaveFormatError until the codec lands;
 *    qa_write_saved_game aborts the same way for a shared save.
 *    The Q1 source branch is complete.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SAVED_GAME_H
#define QA_PERSISTENCE_SAVED_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/session.h"
#include "qa/persistence/q1.h"

/* TS SavedGame union on kind: "shared" | "q1-source". */
typedef enum qa_saved_game_kind {
  QA_SAVED_GAME_SHARED, QA_SAVED_GAME_Q1_SOURCE
} qa_saved_game_kind;
typedef struct qa_saved_game {
  qa_saved_game_kind kind;
  qa_save_image image;   /* shared */
  qa_q1_save_data data;  /* q1-source; owned */
} qa_saved_game;

/* False only on OOM; malformed saves abort as the TS throws. */
bool qa_decode_saved_game(const uint8_t *bytes, size_t length, qa_saved_game *out);
/* TS readSavedGame (synchronous). False on I/O failure or OOM. */
bool qa_read_saved_game(const char *path, qa_saved_game *out);
/* TS writeSavedGame (synchronous). False only on I/O failure or OOM;
 * contained-path RangeErrors abort via qa_write_contained_save. */
bool qa_write_saved_game(const char *directory, const char *path,
                         const qa_saved_game *save);
void qa_saved_game_free(qa_saved_game *save);

#endif
