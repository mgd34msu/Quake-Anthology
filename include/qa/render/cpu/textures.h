/* Port of src/render/cpu/textures.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_TEXTURES_H
#define QA_RENDER_CPU_TEXTURES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/render/cpu/triangle_kernel.h"
typedef struct qa_cpu_images qa_cpu_images;
bool qa_cpu_images_init(qa_cpu_images *images, qa_renderer_resource_owner owner);
void qa_cpu_images_destroy(qa_cpu_images *images);
void qa_cpu_images_validate(qa_cpu_images *images, qa_renderer_image image);
qa_depth_image_level qa_cpu_images_depth_image(qa_cpu_images *images, qa_renderer_image image);
void qa_cpu_images_apply(qa_cpu_images *images, qa_image_resource_operation operation);
void qa_cpu_images_bind(qa_cpu_images *images, double unit, qa_texture_binding operation);
/* Result levels point into caller storage (TS returns a fresh array). */
qa_bound_texture qa_cpu_images_bound(qa_cpu_images *images, double unit, qa_texture_storage *level_storage, size_t level_capacity);
void qa_cpu_images_clear(qa_cpu_images *images);
struct qa_cpu_images { void *impl; qa_renderer_resource_owner owner; };
#endif
