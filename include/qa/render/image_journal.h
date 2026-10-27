/* Port of src/render/image-journal.ts. Snapshots own malloc'd pixel
 * copies (TS .slice()); journal storage is freed by destroy/clear.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_IMAGE_JOURNAL_H
#define QA_RENDER_IMAGE_JOURNAL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
typedef struct qa_render_image_journal qa_render_image_journal;
struct qa_render_image_journal { void *impl; };
bool qa_render_image_journal_init(qa_render_image_journal *journal);
void qa_render_image_journal_destroy(qa_render_image_journal *journal);
typedef struct qa_journal_backend { void *ctx; void (*apply_image_resource)(void *ctx, qa_image_resource_operation operation); } qa_journal_backend;
void qa_render_image_journal_replay(qa_render_image_journal *journal, qa_journal_backend backend);
void qa_render_image_journal_record(qa_render_image_journal *journal, qa_image_resource_operation operation);
typedef struct qa_journal_image_description { double ordinal; const char *name; double width, height; qa_render_image_kind encoding; double mip_levels; } qa_journal_image_description;
size_t qa_render_image_journal_describe(qa_render_image_journal *journal, qa_journal_image_description *out, size_t capacity);
void qa_render_image_journal_clear(qa_render_image_journal *journal);
#endif
