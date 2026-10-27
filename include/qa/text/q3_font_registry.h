/*
 * Port of src/text/q3-font-registry.ts: RendererFontRegistry, the Q3
 * RE_RegisterFont cache over fontImage_<size>.dat records, with
 * FreeType generation, checkpoint capture/restore, and close.
 * C accommodations:
 *  - The async methods are synchronous (draw2d convention); the TS
 *    registration promise chain collapses to a direct call, so the
 *    pending count measures callback reentrancy depth instead of a
 *    queue. Promises from services likewise resolve synchronously;
 *    their bool returns are false only on OOM.
 *  - RegisteredFont returns are borrowed pointers into registry-owned
 *    entries, valid until qa_q3_renderer_font_registry_close/destroy.
 *    A NULL return is TS null (or OOM, which TS cannot report).
 *  - The TS `destination?: () => Uint8Array` closure is a producer
 *    fn + ctx returning a mutable byte view; a NULL fn is TS
 *    undefined. TS `path: string | null` is a NULL const char *.
 *  - TS MaterialPicture is qa_material_picture (draw2d.h). Glyph
 *    pictures point into a registry-owned pool of qa_picture_asset
 *    material variants; their name strings stay borrowed from the
 *    registering services, as in the TS.
 *  - Checkpoint capture returns an owned qa_save_value tree (NULL
 *    only on OOM) freed with qa_save_value_free; restore borrows the
 *    tree and reads it with SaveReader path "client-fonts".
 *  - Names are NUL-terminated bytes; the cached-font lookup's
 *    toLowerCase comparison is ASCII-only (font names are latin1
 *    record bytes, where JS Unicode lowering can differ).
 *  - FreeType goes through qa/platform/freetype.h (the port of the
 *    TS platform/freetype.ts this file imports): the registry owns
 *    one qa_freetype_font_library once opened, and the checkpoint
 *    carries its qa_freetype_init_kind in the TS string form.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_Q3_FONT_REGISTRY_H
#define QA_TEXT_Q3_FONT_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/platform/freetype.h"
#include "qa/text/draw2d.h"
#include "qa/text/q3_font.h"

/* TS FontGenerationServices. register_image/write_file receive byte
 * views valid for the call. register_image false is OOM only. */
typedef struct qa_q3_font_generation_services {
  void *ctx;
  bool (*register_image)(void *ctx, const char *name, qa_byte_view rgba,
                         qa_material_picture *out);
  bool (*save_font_data)(void *ctx);
  void (*write_file)(void *ctx, const char *name, qa_byte_view bytes);
} qa_q3_font_generation_services;

/* TS constructor's registerPicture closure. False is OOM only. */
typedef bool (*qa_q3_font_register_picture_fn)(void *ctx, const char *path,
                                               qa_material_picture *out);
typedef void (*qa_q3_font_sync_render_thread_fn)(void *ctx);

/* TS `destination: () => Uint8Array`; the view is mutable and must
 * stay valid while the registry writes the record through it. */
typedef struct qa_q3_font_record_view {
  uint8_t *bytes;
  size_t length;
} qa_q3_font_record_view;
typedef qa_q3_font_record_view (*qa_q3_font_destination_fn)(void *ctx);

typedef struct qa_q3_renderer_font_entry {
  qa_q3_registered_font font;
  uint8_t *record; /* owned QA_Q3_FONT_RECORD_BYTES copy */
} qa_q3_renderer_font_entry;

typedef struct qa_q3_renderer_generated_entry {
  char *name;     /* owned */
  uint8_t *rgba;  /* owned, rgba_length bytes */
  size_t rgba_length;
} qa_q3_renderer_generated_entry;

/* TS RendererFontRegistry. Font entries are individually allocated
 * so borrowed font pointers stay stable; at most six ever exist (the
 * TS cache bound enforced by registerFont/restoreCheckpoint). */
typedef struct qa_q3_renderer_font_registry {
  qa_font_file_reader reader;
  qa_q3_font_register_picture_fn register_picture;
  void *register_picture_ctx;
  qa_q3_font_sync_render_thread_fn sync_render_thread;
  void *sync_render_thread_ctx;
  bool has_generation; /* TS generation !== null */
  qa_q3_font_generation_services generation;
  qa_q3_renderer_font_entry *fonts[6];
  size_t font_count;
  int64_t pending;
  qa_q3_renderer_generated_entry *generated; /* owned array */
  size_t generated_count;
  size_t generated_capacity;
  bool has_freetype; /* TS freeType !== undefined */
  qa_freetype_init_kind freetype_kind;
  qa_freetype_font_library freetype; /* ready only */
  qa_retained_font_file *retained; /* owned array of reader files */
  size_t retained_count;
  size_t retained_capacity;
  qa_picture_asset **pictures; /* owned array of owned pictures */
  size_t picture_count;
  size_t picture_capacity;
  bool closed;
} qa_q3_renderer_font_registry;

/* `generation` NULL is the TS null default. */
void qa_q3_renderer_font_registry_init(
    qa_q3_renderer_font_registry *registry, qa_font_file_reader reader,
    qa_q3_font_register_picture_fn register_picture, void *register_picture_ctx,
    qa_q3_font_sync_render_thread_fn sync_render_thread,
    void *sync_render_thread_ctx,
    const qa_q3_font_generation_services *generation);

/* Owned checkpoint tree; NULL only on OOM. */
qa_save_value *qa_q3_renderer_font_registry_capture_checkpoint(
    qa_q3_renderer_font_registry *registry);
/* False only on OOM; malformed values abort via SaveReader. */
bool qa_q3_renderer_font_registry_restore_checkpoint(
    qa_q3_renderer_font_registry *registry, const qa_save_value *value);
/* Borrowed font, or NULL for TS null. `path` NULL is TS null;
 * `print` NULL is a noop; `destination` NULL is TS undefined. */
const qa_q3_registered_font *qa_q3_renderer_font_registry_register_font(
    qa_q3_renderer_font_registry *registry, const char *path, double point_size,
    qa_font_print_fn print, void *print_ctx,
    qa_q3_font_destination_fn destination, void *destination_ctx);
void qa_q3_renderer_font_registry_close(qa_q3_renderer_font_registry *registry);
/* close() plus freeing registry-owned storage. */
void qa_q3_renderer_font_registry_destroy(qa_q3_renderer_font_registry *registry);

#endif
