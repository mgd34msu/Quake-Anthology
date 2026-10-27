/* Port of src/render/gl/textures.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_TEXTURES_H
#define QA_RENDER_GL_TEXTURES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/platform/gl.h"
typedef void (*qa_gl_pixel_store_fn)(void *ctx);
/* direction: "pack"|"unpack". */
void qa_gl_with_pixel_store(qa_gl *gl, const char *direction, qa_gl_pixel_store_fn operation, void *operation_ctx);
typedef struct qa_gl_texture_info { uint32_t name; bool mipmap; double width, height; qa_render_image_kind content_kind; } qa_gl_texture_info;
typedef struct qa_gl_textures qa_gl_textures;
struct qa_gl_textures { void *impl; };
bool qa_gl_textures_init(qa_gl_textures *textures, qa_gl *gl, qa_renderer_resource_owner owner, double maximum);
qa_gl_texture_info qa_gl_textures_registered(qa_gl_textures *textures, qa_renderer_image image);
void qa_gl_textures_bind(qa_gl_textures *textures, qa_texture_binding binding);
void qa_gl_textures_apply(qa_gl_textures *textures, qa_image_resource_operation operation);
void qa_gl_textures_close(qa_gl_textures *textures);
#endif
