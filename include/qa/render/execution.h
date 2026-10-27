/* Port of src/render/execution.ts. TS Iterable batches/operations become
 * pointer+count. GPL-2.0-or-later. */
#ifndef QA_RENDER_EXECUTION_H
#define QA_RENDER_EXECUTION_H
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS ExecutionOperation: RenderOperation with draw/object-opacity
 * batches as iterables (already pointer+count in contracts). */
typedef qa_render_operation qa_execution_operation;
typedef struct qa_execution_view {
  qa_render_view_state state;
  const qa_execution_operation *before_view; size_t before_view_count;
  const qa_execution_operation *operations; size_t operation_count;
} qa_execution_view;
typedef enum qa_execution_command_kind { QA_EXEC_COMMAND_DRAW_BUFFER, QA_EXEC_COMMAND_IMAGE_RESOURCE, QA_EXEC_COMMAND_SET_COLOR, QA_EXEC_COMMAND_STRETCH_PIC, QA_EXEC_COMMAND_VIEW, QA_EXEC_COMMAND_SWAP_BUFFERS } qa_execution_command_kind;
typedef struct qa_execution_command {
  qa_execution_command_kind kind;
  qa_renderer_draw_buffer buffer; bool clear;
  qa_image_resource_operation operation;
  qa_vec4 color; qa_rect rect; qa_texture_rect uv; qa_renderer_image image;
  qa_execution_view view;
  const double *captures; size_t capture_count;
} qa_execution_command;
typedef struct qa_execution_services { void *ctx; void (*image_applied)(void *ctx, qa_image_resource_operation operation); void (*swap)(void *ctx, const double *captures, size_t capture_count); } qa_execution_services;
typedef struct qa_render_executor { qa_renderer_backend backend; qa_execution_services services; qa_vec4 color; } qa_render_executor;
void qa_render_executor_init(qa_render_executor *executor, qa_renderer_backend backend, qa_execution_services services);
void qa_render_executor_replace_backend(qa_render_executor *executor, qa_renderer_backend backend);
void qa_render_executor_image(qa_render_executor *executor, qa_image_resource_operation operation);
void qa_render_executor_execute(qa_render_executor *executor, qa_execution_command command);
#endif
