/* Port of src/render/worker-protocol.ts. Threads share an address space, so the
 * wire form is a deep snapshot of the contract structs in a bump arena instead of
 * structuredClone output: frontend data can change right after encoding and the
 * receiver never sees it. Images and dynamic sources cross as wire identities.
 * TS deferred() becomes a `failure` string on the wire value, raised by the decoder
 * when that leaf is reached, so earlier leaves still execute. TS decoder type checks
 * on `unknown` have no counterpart where C types rule the mismatch out; the variant
 * strings and integers are still checked. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_PROTOCOL_H
#define QA_RENDER_WORKER_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/render/execution.h"
typedef struct qa_wire_arena qa_wire_arena;
qa_wire_arena *qa_wire_arena_create(void);
void qa_wire_arena_destroy(qa_wire_arena *arena);
/* Bytes snapshotted so far. */
size_t qa_wire_arena_used(const qa_wire_arena *arena);
void *qa_wire_arena_copy(qa_wire_arena *arena, const void *source, size_t size);
/* TS wire image descriptor {ordinal,width,height,name}. */
typedef struct qa_wire_image { double ordinal, width, height; const char *name; } qa_wire_image;
/* TS decodeImageLevel: validates an RGBA readback level. */
qa_image_level qa_decode_wire_image_level(qa_image_level level);
typedef struct qa_wire_binding { qa_texture_binding_kind kind; qa_wire_image image; double token; } qa_wire_binding;
typedef struct qa_wire_image_operation {
  const char *failure; /* TS {workerFailure}; NULL when encoded */
  qa_image_resource_operation operation; /* retained snapshot, frontend image identity */
  double acknowledgment;
  qa_wire_image image;
} qa_wire_image_operation;
typedef struct qa_wire_batch {
  const char *failure;
  qa_draw_batch batch; /* snapshot; images and bindings are decoded from the fields below */
  qa_wire_binding texture, second;
  bool has_atlas; qa_wire_image atlas;
  qa_q2_shadow_atlas decoded_atlas; /* receiver storage for batch.lighting.atlas */
} qa_wire_batch;
typedef struct qa_wire_operation {
  const char *failure;
  qa_render_operation operation; /* snapshot; batches are in the field below */
  qa_wire_image image; /* image or white_image */
  qa_wire_batch *batches; size_t batch_count;
} qa_wire_operation;
typedef struct qa_wire_view {
  qa_render_view_state state;
  qa_wire_operation *before_view; size_t before_view_count;
  qa_wire_operation *operations; size_t operation_count;
} qa_wire_view;
typedef struct qa_wire_command {
  const char *failure;
  qa_render_command_kind kind;
  const char *buffer; bool clear;
  qa_wire_image_operation operation;
  qa_vec4 color; qa_rect rect; qa_texture_rect uv; qa_wire_image image;
  qa_wire_view view;
  const double *captures; size_t capture_count;
} qa_wire_command;

/* Snapshot values without invoking dynamic sources or serializing frontend identities. */
typedef struct qa_wire_encoder qa_wire_encoder;
struct qa_wire_encoder { void *impl; qa_renderer_resource_owner owner; };
bool qa_wire_encoder_init(qa_wire_encoder *encoder, qa_renderer_resource_owner owner);
void qa_wire_encoder_destroy(qa_wire_encoder *encoder);
qa_renderer_image qa_wire_encoder_original_image(qa_wire_encoder *encoder, double ordinal);
qa_dynamic_image_source qa_wire_encoder_source(qa_wire_encoder *encoder, double token);
/* Caller must synchronize and retain tokens while direct prepared draws remain active. */
void qa_wire_encoder_clear_sources(qa_wire_encoder *encoder);
void qa_wire_encoder_clear(qa_wire_encoder *encoder);
/* The returned operation stays valid until the next acknowledge, clear or destroy.
 * Value structs have no object identity, so a TS `previous !== image` check compares
 * owner, dimensions and name here. */
qa_image_resource_operation qa_wire_encoder_acknowledge(qa_wire_encoder *encoder, double token);
qa_wire_image qa_wire_encoder_image(qa_wire_encoder *encoder, qa_renderer_image image);
qa_wire_binding qa_wire_encoder_binding(qa_wire_encoder *encoder, qa_texture_binding binding);
/* Owns its snapshot until acknowledged; the wire value must not outlive that. */
qa_wire_image_operation qa_wire_encoder_image_operation(qa_wire_encoder *encoder, qa_image_resource_operation operation);
qa_wire_batch qa_wire_encoder_batch(qa_wire_encoder *encoder, qa_wire_arena *arena, qa_draw_batch batch);
qa_wire_operation qa_wire_encoder_operation(qa_wire_encoder *encoder, qa_wire_arena *arena, qa_render_operation operation);
qa_wire_command qa_wire_encoder_command(qa_wire_encoder *encoder, qa_wire_arena *arena, qa_render_command command, const double *captures, size_t capture_count);

/* Reconstruct canonical receiver identities, decoding execution leaves only when reached. */
typedef qa_renderer_image (*qa_wire_reached_dynamic_fn)(void *ctx, double token, void (*apply)(void *apply_ctx, qa_image_resource_operation operation), void *apply_ctx);
typedef struct qa_wire_decoder qa_wire_decoder;
struct qa_wire_decoder { void *impl; qa_renderer_resource_owner owner; qa_wire_reached_dynamic_fn reached_dynamic; void *reached_ctx; qa_resolved_resource_reference palette_source; };
bool qa_wire_decoder_init(qa_wire_decoder *decoder, qa_renderer_resource_owner owner, qa_wire_reached_dynamic_fn reached_dynamic, void *reached_ctx, qa_resolved_resource_reference palette_source);
void qa_wire_decoder_destroy(qa_wire_decoder *decoder);
/* Caller must finish submitted work and all direct prepared draws before clearing. */
void qa_wire_decoder_clear_sources(qa_wire_decoder *decoder);
qa_renderer_image qa_wire_decoder_image(qa_wire_decoder *decoder, qa_wire_image wire);
qa_texture_binding qa_wire_decoder_binding(qa_wire_decoder *decoder, qa_wire_binding wire);
/* TS WeakMap keyed by decoded object. C values are matched on kind, image and data
 * pointers; a successful lookup consumes the entry, since the runtime asks only once. */
double qa_wire_decoder_acknowledgment(qa_wire_decoder *decoder, const qa_image_resource_operation *operation);
qa_image_resource_operation qa_wire_decoder_image_operation(qa_wire_decoder *decoder, qa_wire_image_operation wire);
/* Writes receiver storage inside the wire value; the receiver owns it at this point. */
qa_draw_batch qa_wire_decoder_batch(qa_wire_decoder *decoder, qa_wire_batch *batch);
/* Draw and object-opacity results carry no batches: decode wire->batches when reached. */
qa_execution_operation qa_wire_decoder_operation(qa_wire_decoder *decoder, const qa_wire_operation *operation);
/* TS viewState drops viewProjection, so a worker view never has one. */
qa_render_view_state qa_wire_decoder_view_state(qa_wire_decoder *decoder, const qa_wire_view *view);
/* Raises a command's deferred failure. For a view, only that check runs: execute the
 * leaves through wire->view. */
qa_execution_command qa_wire_decoder_command(qa_wire_decoder *decoder, const qa_wire_command *command);
#endif
