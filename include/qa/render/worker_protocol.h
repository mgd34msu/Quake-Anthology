/* Port of src/render/worker-protocol.ts. Direct/pumped (PORTING.md):
 * TS `unknown` wire records become the contract structs themselves;
 * encode validates owner/variant strings and assigns dynamic-source
 * tokens and image acknowledgments exactly as the TS, decode
 * re-canonicalizes image identities. No serialization bytes exist.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_PROTOCOL_H
#define QA_RENDER_WORKER_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/render/execution.h"
/* TS wire image descriptor {ordinal,width,height,name}. */
typedef struct qa_wire_image { double ordinal, width, height; const char *name; } qa_wire_image;
/* TS decodeImageLevel: validates an RGBA readback level. */
qa_image_level qa_decode_wire_image_level(qa_image_level level);
typedef struct qa_wire_binding { qa_texture_binding_kind kind; qa_wire_image image; double token; } qa_wire_binding;
typedef struct qa_wire_image_operation { qa_image_resource_operation operation; double acknowledgment; } qa_wire_image_operation;
typedef struct qa_wire_encoder qa_wire_encoder;
struct qa_wire_encoder { void *impl; qa_renderer_resource_owner owner; };
bool qa_wire_encoder_init(qa_wire_encoder *encoder, qa_renderer_resource_owner owner);
void qa_wire_encoder_destroy(qa_wire_encoder *encoder);
qa_renderer_image qa_wire_encoder_original_image(qa_wire_encoder *encoder, double ordinal);
qa_dynamic_image_source qa_wire_encoder_source(qa_wire_encoder *encoder, double token);
void qa_wire_encoder_clear_sources(qa_wire_encoder *encoder);
void qa_wire_encoder_clear(qa_wire_encoder *encoder);
qa_image_resource_operation qa_wire_encoder_acknowledge(qa_wire_encoder *encoder, double token);
qa_wire_image qa_wire_encoder_image(qa_wire_encoder *encoder, qa_renderer_image image);
qa_wire_binding qa_wire_encoder_binding(qa_wire_encoder *encoder, qa_texture_binding binding);
qa_wire_image_operation qa_wire_encoder_image_operation(qa_wire_encoder *encoder, qa_image_resource_operation operation);
/* Encoded batches/operations/commands are validated copies whose image
 * references are canonicalized through the encoder image map; dynamic
 * bindings carry tokens in the returned wire binding side channel is
 * unnecessary in-process, so the batch keeps its source (documented). */
qa_draw_batch qa_wire_encoder_batch(qa_wire_encoder *encoder, qa_draw_batch batch);
qa_render_operation qa_wire_encoder_operation(qa_wire_encoder *encoder, qa_render_operation operation);
qa_execution_command qa_wire_encoder_command(qa_wire_encoder *encoder, qa_render_command command, const double *captures, size_t capture_count, qa_execution_view *view_storage, qa_execution_operation *operation_storage, size_t operation_capacity);
typedef qa_renderer_image (*qa_wire_reached_dynamic_fn)(void *ctx, double token, void (*apply)(void *apply_ctx, qa_image_resource_operation operation), void *apply_ctx);
typedef struct qa_wire_decoder qa_wire_decoder;
struct qa_wire_decoder { void *impl; qa_renderer_resource_owner owner; qa_wire_reached_dynamic_fn reached_dynamic; void *reached_ctx; qa_resolved_resource_reference palette_source; };
bool qa_wire_decoder_init(qa_wire_decoder *decoder, qa_renderer_resource_owner owner, qa_wire_reached_dynamic_fn reached_dynamic, void *reached_ctx, qa_resolved_resource_reference palette_source);
void qa_wire_decoder_destroy(qa_wire_decoder *decoder);
void qa_wire_decoder_clear_sources(qa_wire_decoder *decoder);
qa_renderer_image qa_wire_decoder_image(qa_wire_decoder *decoder, qa_wire_image wire);
qa_texture_binding qa_wire_decoder_binding(qa_wire_decoder *decoder, qa_wire_binding wire);
double qa_wire_decoder_acknowledgment(qa_wire_decoder *decoder, const qa_image_resource_operation *operation);
qa_image_resource_operation qa_wire_decoder_image_operation(qa_wire_decoder *decoder, qa_wire_image_operation wire);
qa_draw_batch qa_wire_decoder_batch(qa_wire_decoder *decoder, qa_draw_batch batch);
qa_execution_operation qa_wire_decoder_operation(qa_wire_decoder *decoder, qa_render_operation operation, qa_draw_batch *batch_storage, size_t batch_capacity);
qa_execution_command qa_wire_decoder_command(qa_wire_decoder *decoder, qa_execution_command command, qa_execution_operation *operation_storage, size_t operation_capacity, qa_draw_batch *batch_storage, size_t batch_capacity);
#endif
