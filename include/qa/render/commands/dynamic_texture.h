/* Port of src/render/commands/dynamic-texture.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_COMMANDS_DYNAMIC_TEXTURE_H
#define QA_RENDER_COMMANDS_DYNAMIC_TEXTURE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
typedef void (*qa_image_operation_apply_fn)(void *ctx, qa_image_resource_operation operation);
typedef struct qa_texture_resolver qa_texture_resolver;
struct qa_texture_resolver { void *impl; qa_image_operation_apply_fn apply; void *apply_ctx; };
bool qa_texture_resolver_init(qa_texture_resolver *resolver, qa_image_operation_apply_fn apply, void *apply_ctx);
void qa_texture_resolver_destroy(qa_texture_resolver *resolver);
qa_texture_binding qa_texture_resolver_resolve(qa_texture_resolver *resolver, qa_texture_binding binding);
/* TS resolveDrawTextures returns the same batch object when unchanged;
 * C writes the resolved batch to *out and returns false when unchanged
 * (caller keeps using the input). */
bool qa_resolve_draw_textures(qa_draw_batch batch, qa_image_operation_apply_fn apply, void *apply_ctx, qa_draw_batch *out);
#endif
