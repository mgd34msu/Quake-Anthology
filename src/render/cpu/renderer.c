#include "internal.h"
#include <limits.h>
#include "../save_fields.h"
#include "qa/display.h"
#include "qa/q3_source_scene_bank.h"

struct qa_cpu_surface_ticket {
  qa_cpu_renderer *renderer;
  cpu_framebuffer next, retired_display, retired_opacity;
  qa_output_domains retired_domains;
  uint8_t *output, *retired_output;
  qa_cpu_options original;
  qa_cpu_present present;
  void *context;
  uint32_t width, height;
  uint8_t gamma[256], original_gamma[256];
  float gamma_value, original_gamma_value;
  cpu_framebuffer original_display, original_opacity;
  cpu_target *original_targets;
  const qa_scene_image *original_bound[2];
  cpu_vertex *original_vertices;
  size_t original_vertex_capacity;
  uint8_t *original_output;
  bool original_gamma_enabled;
  bool gamma_enabled, resized, prepared, published;
};

static bool cpu_surface_idle(const qa_cpu_renderer *renderer, qa_error *error) {
  if (renderer && !renderer->surface_ticket && !renderer->controls.ticket && !renderer->controls.source.entered) return true;
  qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CPU renderer has a retained settings ticket");
  return false;
}

qa_render_controls *qa_cpu_render_controls(qa_cpu_renderer *renderer) {
  return renderer ? &renderer->controls : NULL;
}
bool qa_cpu_render_controls_current(const qa_render_controls *controls) {
  const qa_cpu_renderer *renderer = controls ? controls->owner.cpu : NULL;
  return renderer && controls->backend == QA_RENDER_CONTROLS_CPU &&
      &renderer->controls == controls && !renderer->destroy_pending &&
      !renderer->executing && !renderer->presenting && !renderer->capturing &&
      !renderer->opacity_active && !renderer->opacity_parent;
}
void qa_cpu_render_controls_close(qa_render_controls *controls) {
  qa_cpu_renderer *renderer = controls->owner.cpu;
  if (renderer->destroy_pending) qa_cpu_destroy(renderer);
}
bool qa_cpu_source_scratch_current(const qa_render_controls *controls) {
  const qa_cpu_renderer *renderer = controls ? controls->owner.cpu : NULL;
  return renderer && controls->backend == QA_RENDER_CONTROLS_CPU &&
      &renderer->controls == controls && !renderer->destroy_pending &&
      !renderer->executing && !renderer->presenting && !renderer->capturing &&
      !renderer->surface_ticket && (!renderer->opacity_active ||
       (controls->source.entered && controls->source.issuing));
}

static bool dimensions(uint32_t width, uint32_t height, size_t *count,
                       qa_error *error) {
  if (!width || !height || width > INT32_MAX || height > INT32_MAX ||
      (size_t)width > SIZE_MAX / height ||
      (size_t)width * height > SIZE_MAX / sizeof(double)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU framebuffer dimensions");
    return false;
  }
  *count = (size_t)width * height;
  return true;
}
static void buffer_destroy(cpu_framebuffer *buffer) {
  free(buffer->color);
  free(buffer->depth);
  free(buffer->stencil);
  memset(buffer, 0, sizeof(*buffer));
}
static bool buffer_create(cpu_framebuffer *out, uint32_t width, uint32_t height,
                          bool depth_only, const qa_cpu_renderer *renderer,
                          qa_error *error) {
  size_t count;
  if (!dimensions(width, height, &count, error))
    return false;
  cpu_framebuffer next = {.width = width,
                          .height = height,
                          .depth_only = depth_only,
                          .alpha = renderer->options.alpha_bits != 0};
  next.depth = malloc(count * sizeof(*next.depth));
  if (!depth_only)
    next.color = calloc(count, 4);
  if (renderer->options.stencil_bits)
    next.stencil = calloc(count, sizeof(*next.stencil));
  if (!next.depth || (!depth_only && !next.color) ||
      (renderer->options.stencil_bits && !next.stencil)) {
    buffer_destroy(&next);
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU framebuffer");
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    next.depth[i] = 1;
    if (next.color && !renderer->options.alpha_bits)
      next.color[i * 4 + 3] = 255;
  }
  *out = next;
  return true;
}
void qa_cpu_options_default(qa_cpu_options *options) {
  if (options)
    *options = (qa_cpu_options){.width = 640,
                                .height = 480,
                                .subpixel_bits = 8,
                                .stencil_bits = 8,
                                .alpha_bits = 8};
}
qa_cpu_renderer *qa_cpu_create(const qa_cpu_options *options, qa_error *error) {
  qa_cpu_options defaults;
  qa_cpu_options_default(&defaults);
  if (!options)
    options = &defaults;
  if (options->subpixel_bits < 4 || options->subpixel_bits > 16 ||
      options->stencil_bits > 32 ||
      (options->alpha_bits != 0 && options->alpha_bits != 8)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU subpixel/stencil/alpha precision");
    return NULL;
  }
  qa_cpu_renderer *renderer = calloc(1, sizeof(*renderer));
  if (!renderer) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU renderer");
    return NULL;
  }
  renderer->options = *options;
  qa_render_controls_init_cpu(&renderer->controls, renderer);
  qa_scene_state_default(&renderer->pipeline);
  renderer->clear_depth = 1;
  renderer->gamma_value = 1;
  for (size_t i=0;i<256;++i) renderer->gamma[i]=(uint8_t)i;
  renderer->stencil_maximum =
      options->stencil_bits == 32
          ? UINT32_MAX
          : (uint32_t)((UINT64_C(1) << options->stencil_bits) - 1);
  if (!qa_cpu_resize(renderer, options->width, options->height, error)) {
    qa_cpu_destroy(renderer);
    return NULL;
  }
  return renderer;
}
void qa_cpu_destroy(qa_cpu_renderer *renderer) {
  if (!renderer)
    return;
  if (renderer->surface_ticket || renderer->controls.ticket || renderer->controls.source.entered) { renderer->destroy_pending=true; return; }
  material_source_release(&renderer->controls.source);
  for (size_t i = 0; i < 2; ++i)
    qa_scene_image_release(renderer->bound[i]);
  while (renderer->targets) {
    cpu_target *target = renderer->targets;
    renderer->targets = target->next;
    qa_scene_image_release(target->image);
    buffer_destroy(&target->framebuffer);
    free(target);
  }
  buffer_destroy(&renderer->display);
  buffer_destroy(&renderer->opacity);
  free(renderer->output);
  free(renderer->vertices);
  qa_output_domains_destroy(&renderer->output_domains);
  free(renderer);
}
bool qa_cpu_capabilities_read(const qa_cpu_renderer *renderer, qa_cpu_capabilities *out, qa_error *error)
{
  if (!renderer || !out || renderer->destroy_pending || !renderer->display.color ||
      !renderer->display.depth || !renderer->display.width || !renderer->display.height) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CPU capabilities require the actual retained framebuffer");
    return false;
  }
  *out = (qa_cpu_capabilities){.color_bits = 3 * 8,
    .alpha_bits = renderer->display.alpha ? 8 : 0,
    .depth_bits = sizeof(*renderer->display.depth) * CHAR_BIT,
    .stencil_bits = renderer->options.stencil_bits};
  return true;
}
bool qa_cpu_resize(qa_cpu_renderer *renderer, uint32_t width, uint32_t height,
                   qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || (renderer->opacity_active && renderer->opacity_value != 1)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Cannot resize an active CPU opacity scope");
    return false;
  }
  if (renderer->display.width == width && renderer->display.height == height)
    return true;
  cpu_framebuffer next = {0};
  if (!buffer_create(&next, width, height, false, renderer, error))
    return false;
  uint8_t *output = malloc((size_t)width * height * 4);
  if (!output) {
    buffer_destroy(&next);
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Allocating CPU presentation pixels");
    return false;
  }
  bool current_display =
      !renderer->current || renderer->current == &renderer->display;
  buffer_destroy(&renderer->display);
  buffer_destroy(&renderer->opacity);
  free(renderer->output);
  renderer->display = next;
  renderer->output = output;
  renderer->options.width = width;
  renderer->options.height = height;
  if (current_display)
    renderer->current = &renderer->display;
  renderer->view.viewport = (qa_scene_rect){0, 0, width, height};
  renderer->view.depth = 1;
  qa_output_domains_destroy(&renderer->output_domains);
  return true;
}
const cpu_framebuffer *cpu_target_find(const qa_cpu_renderer *renderer,
                                       const qa_scene_image *image) {
  if (!image)
    return NULL;
  for (const cpu_target *target = renderer->targets; target;
       target = target->next)
    if (target->image->identity == image->identity &&
        target->image->revision == image->revision)
      return &target->framebuffer;
  return NULL;
}
static bool select_target(qa_cpu_renderer *renderer,
                          const qa_scene_image *image, qa_error *error) {
  if (renderer->opacity_active && renderer->opacity_value != 1) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Render target changed inside opacity scope");
    return false;
  }
  if (!image) {
    renderer->current = &renderer->display;
    return true;
  }
  for (cpu_target *target = renderer->targets; target; target = target->next)
    if (target->image->identity == image->identity &&
        target->image->revision == image->revision) {
      renderer->current = &target->framebuffer;
      return true;
    }
  if (!image->levels || !image->level_count ||
      (unsigned)image->kind > QA_SCENE_DEPTH32F) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU render target image");
    return false;
  }
  const qa_scene_image_level *level = &image->levels[0];
  size_t count;
  if (!dimensions(level->width, level->height, &count, error))
    return false;
  if (!level->pixels || level->bytes < count * 4) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU render target image has truncated pixels");
    return false;
  }
  cpu_target *target = calloc(1, sizeof(*target));
  if (!target) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU render target");
    return false;
  }
  if (!buffer_create(&target->framebuffer, level->width, level->height,
                     image->kind == QA_SCENE_DEPTH32F, renderer, error)) {
    free(target);
    return false;
  }
  target->framebuffer.alpha = image->kind == QA_SCENE_RGBA8;
  if (image->kind == QA_SCENE_DEPTH32F) {
    const uint8_t *source = level->pixels;
    for (size_t i = 0; i < count; ++i) {
      float depth;
      memcpy(&depth, source + i * sizeof(depth), sizeof(depth));
      size_t destination =
          (size_t)(level->height - 1 - i / level->width) * level->width +
          i % level->width;
      target->framebuffer.depth[destination] =
          isfinite(depth) ? cpu_clamp(depth) : 1;
    }
  } else {
    const uint8_t *source = level->pixels;
    for (size_t y = 0; y < level->height; ++y) {
      uint8_t *destination = target->framebuffer.color +
                             (level->height - 1 - y) * level->width * 4;
      memcpy(destination, source + y * level->width * 4,
             (size_t)level->width * 4);
      if (!target->framebuffer.alpha)
        for (size_t x = 0; x < level->width; ++x)
          destination[x * 4 + 3] = 255;
    }
  }
  qa_scene_image_retain(image);
  target->image = image;
  target->next = renderer->targets;
  renderer->targets = target;
  renderer->current = &target->framebuffer;
  return true;
}
static bool update_image(qa_cpu_renderer *renderer, const qa_scene_image *image,
                         qa_error *error) {
  if (!image) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU image update requires an image");
    return false;
  }
  if (!cpu_image_valid(image, error))
    return false;
  for (cpu_target *target = renderer->targets; target; target = target->next)
    if (target->image->identity == image->identity &&
        target->image->revision != image->revision &&
        (renderer->current == &target->framebuffer ||
         renderer->opacity_parent == &target->framebuffer)) {
      if (!select_target(renderer, image, error))
        return false;
      break;
    }
  for (size_t i = 0; i < 2; ++i)
    if (renderer->bound[i] && renderer->bound[i]->identity == image->identity &&
        renderer->bound[i] != image) {
      qa_scene_image_retain(image);
      qa_scene_image_release(renderer->bound[i]);
      renderer->bound[i] = image;
    }
  return true;
}
static void clear_view(qa_cpu_renderer *renderer, const qa_scene_view *view) {
  cpu_framebuffer *buffer = renderer->current;
  int64_t right = (int64_t)view->viewport.x + view->viewport.width;
  int64_t bottom = (int64_t)view->viewport.y + view->viewport.height;
  int64_t x0 = view->viewport.x > 0 ? view->viewport.x : 0;
  int64_t y0 = view->viewport.y > 0 ? view->viewport.y : 0;
  int64_t x1 = right < buffer->width ? right : buffer->width;
  int64_t y1 = bottom < buffer->height ? bottom : buffer->height;
  uint8_t color[4] = {cpu_byte(view->color.x), cpu_byte(view->color.y),
                      cpu_byte(view->color.z),
                      buffer->alpha ? cpu_byte(view->color.w) : 255};
  for (int64_t y = y0; y < y1; ++y)
    for (int64_t x = x0; x < x1; ++x) {
      size_t index = (size_t)y * buffer->width + (size_t)x;
      if (view->clear_color && buffer->color)
        memcpy(buffer->color + index * 4, color, 4);
      if (view->clear_depth)
        buffer->depth[index] = cpu_clamp(view->depth);
      if (view->clear_stencil && buffer->stencil)
        buffer->stencil[index] = 0;
    }
}
static bool begin_opacity(qa_cpu_renderer *renderer, float opacity,
                          qa_error *error) {
  if (renderer->opacity_active || !isfinite(opacity) || opacity < 0 ||
      opacity > 1) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid or nested CPU opacity scope");
    return false;
  }
  cpu_framebuffer *parent = renderer->current;
  if (opacity > 0 && opacity < 1) {
    if (renderer->opacity.width != parent->width ||
        renderer->opacity.height != parent->height ||
        renderer->opacity.depth_only != parent->depth_only) {
      cpu_framebuffer next = {0};
      if (!buffer_create(&next, parent->width, parent->height,
                         parent->depth_only, renderer, error))
        return false;
      buffer_destroy(&renderer->opacity);
      renderer->opacity = next;
    }
    size_t count = (size_t)parent->width * parent->height;
    renderer->opacity.alpha = parent->alpha;
    if (parent->color)
      memcpy(renderer->opacity.color, parent->color, count * 4);
    memcpy(renderer->opacity.depth, parent->depth,
           count * sizeof(*parent->depth));
    if (parent->stencil)
      memcpy(renderer->opacity.stencil, parent->stencil,
             count * sizeof(*parent->stencil));
    renderer->current = &renderer->opacity;
  }
  renderer->opacity_parent = opacity == 1 ? NULL : parent;
  renderer->opacity_viewport = renderer->view.viewport;
  renderer->opacity_value = opacity;
  renderer->opacity_active = true;
  renderer->opacity_skip = opacity == 0;
  return true;
}
static bool end_opacity(qa_cpu_renderer *renderer, qa_error *error) {
  if (!renderer->opacity_active) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU opacity end without a begin");
    return false;
  }
  cpu_framebuffer *parent = renderer->opacity_parent;
  if (renderer->opacity_value > 0 && renderer->opacity_value < 1 &&
      parent->color) {
    qa_scene_rect rect = renderer->opacity_viewport;
    int64_t x0 = rect.x > 0 ? rect.x : 0, y0 = rect.y > 0 ? rect.y : 0;
    int64_t right = (int64_t)rect.x + rect.width,
            bottom = (int64_t)rect.y + rect.height;
    int64_t x1 = right < parent->width ? right : parent->width;
    int64_t y1 = bottom < parent->height ? bottom : parent->height;
    double alpha = renderer->opacity_value;
    for (int64_t y = y0; y < y1; ++y)
      for (int64_t x = x0; x < x1; ++x) {
        size_t index = ((size_t)y * parent->width + (size_t)x) * 4;
        for (size_t c = 0; c < 4; ++c)
          parent->color[index + c] =
              (uint8_t)floor(parent->color[index + c] * (1 - alpha) +
                             renderer->opacity.color[index + c] * alpha + 0.5);
        if (!parent->alpha)
          parent->color[index + 3] = 255;
      }
  }
  if (renderer->opacity_value != 1)
    renderer->current = parent;
  renderer->opacity_parent = NULL;
  renderer->opacity_active = false;
  renderer->opacity_skip = false;
  return true;
}
bool qa_cpu_set_gamma(qa_cpu_renderer *renderer, float gamma, qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || !isfinite(gamma) || gamma < 0.5f || gamma > 3) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU gamma must be within 0.5..3");
    return false;
  }
  renderer->gamma_enabled = gamma != 1;
  for (size_t i = 0; i < 256; ++i)
    renderer->gamma[i] =
        gamma == 1 ? (uint8_t)i : cpu_byte(pow((float)i / 255, 1.0f / gamma));
  renderer->gamma_value=gamma;
  return true;
}
bool qa_cpu_gamma_read(const qa_cpu_renderer *renderer,float *out,qa_error *error)
{
  if (!renderer || !out || renderer->destroy_pending || !isfinite(renderer->gamma_value) ||
      renderer->gamma_value<.5f || renderer->gamma_value>3) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU brightness observation requires its actual live renderer"); return false;
  }
  *out=renderer->gamma_value; return true;
}
bool qa_cpu_output_domain_read(const qa_cpu_renderer *renderer,qa_scene_rect rect,bool *out,qa_error *error)
{
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!qa_cpu_render_controls_current(&renderer->controls)) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Output domain read requires its returned CPU renderer"); return false;
  }
  if (renderer->current!=&renderer->display || rect.x<0 || rect.y<0 ||
      (uint64_t)(uint32_t)rect.x+rect.width>renderer->display.width ||
      (uint64_t)(uint32_t)rect.y+rect.height>renderer->display.height) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Output domain read requires its actual CPU display region"); return false;
  }
  return qa_output_domains_rect_read(&renderer->output_domains,rect,QA_DRAW_BACK,out,error);
}
qa_bytes qa_cpu_pixels(qa_cpu_renderer *renderer) {
  if (!renderer || renderer->surface_ticket)
    return (qa_bytes){0};
  size_t count = (size_t)renderer->display.width * renderer->display.height;
  bool native_gamma = renderer->options.present == qa_display_present_cpu &&
      qa_display_gamma_applied_is(renderer->options.present_context);
  if (!renderer->gamma_enabled || native_gamma)
    return (qa_bytes){renderer->display.color, count * 4};
  for (size_t i = 0; i < count; ++i) {
    for (size_t c = 0; c < 3; ++c)
      renderer->output[i * 4 + c] = qa_output_domains_source(&renderer->output_domains,
          (uint32_t)(i % renderer->display.width), (uint32_t)(i / renderer->display.width), QA_DRAW_BACK)
          ? renderer->display.color[i * 4 + c] : renderer->gamma[renderer->display.color[i * 4 + c]];
    renderer->output[i * 4 + 3] = renderer->display.color[i * 4 + 3];
  }
  return (qa_bytes){renderer->output, count * 4};
}
bool qa_cpu_capture(qa_cpu_renderer *renderer, qa_buffer *out,
                    qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || !out) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU capture destination");
    return false;
  }
  qa_bytes pixels = qa_cpu_pixels(renderer);
  uint8_t *copy = malloc(pixels.size);
  if (!copy) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU capture");
    return false;
  }
  memcpy(copy, pixels.data, pixels.size);
  *out = (qa_buffer){copy, pixels.size};
  return true;
}
static bool cpu_present_frame(qa_cpu_renderer *renderer, qa_error *error) {
  if (!renderer || renderer->presenting || renderer->capturing || (renderer->opacity_active && renderer->opacity_value != 1)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Cannot present an active CPU opacity scope");
    return false;
  }
  renderer->presenting=true;
  bool ok = !renderer->options.present ||
         renderer->options.present(
             renderer->options.present_context, qa_cpu_pixels(renderer),
             renderer->display.width, renderer->display.height, error);
  renderer->presenting=false;
  if (ok) {
    renderer->controls.source.projection_2d = false;
    renderer->controls.source.entity_count = renderer->controls.source.first_scene_entity = 0;
    renderer->controls.source.submitted_light_count = renderer->controls.source.first_scene_light = 0;
    qa_q3_source_scene_bank_frame(renderer->controls.source.scene_bank);
  }
  return ok;
}
bool qa_cpu_present_frame(qa_cpu_renderer *renderer, qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  qa_scene_frame frame;
  qa_scene_frame_init(&frame,renderer->options.owner);
  frame.source_backend=true;
  bool ok=qa_material_source_swap_end(&renderer->controls.source,&frame,error);
  bool skip=frame.source_skip_backend;
  qa_scene_frame_destroy(&frame);
  return ok && (skip || cpu_present_frame(renderer,error));
}
bool qa_cpu_read_depth(const qa_cpu_renderer *renderer, uint32_t x, uint32_t y,
                       float *out, qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || !out || x >= renderer->display.width ||
      y >= renderer->display.height) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU depth read is outside display framebuffer");
    return false;
  }
  *out =
      (float)
          renderer->display.depth[(size_t)(renderer->display.height - 1 - y) *
                                      renderer->display.width +
                                  x];
  return true;
}
bool qa_cpu_set_overdraw(qa_cpu_renderer *renderer, bool enabled,
                         qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || (enabled && !renderer->options.stencil_bits)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU overdraw requires stencil storage");
    return false;
  }
  renderer->overdraw = enabled;
  return true;
}
bool qa_cpu_read_overdraw(const qa_cpu_renderer *renderer, uint8_t *destination,
                          size_t bytes, qa_error *error) {
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || !destination || !renderer->display.stencil) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU overdraw read requires stencil storage");
    return false;
  }
  size_t stride = ((size_t)renderer->display.width + 3) & ~(size_t)3;
  if (stride > SIZE_MAX / renderer->display.height ||
      bytes <
          stride * (renderer->display.height - 1) + renderer->display.width) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU overdraw destination is truncated");
    return false;
  }
  for (size_t y = 0; y < renderer->display.height; ++y) {
    const uint32_t *row =
        renderer->display.stencil +
        (renderer->display.height - 1 - y) * renderer->display.width;
    for (size_t x = 0; x < renderer->display.width; ++x)
      destination[y * stride + x] = (uint8_t)row[x];
  }
  return true;
}
static bool cpu_execute_range(qa_cpu_renderer *renderer, const qa_scene_frame *frame,
                    size_t first, bool begin, bool finish, qa_error *error) {
  if (!renderer || !frame || frame->owner != renderer->options.owner ||
      (frame->command_count && !frame->commands) || first > frame->command_count ||
      (begin && renderer->opacity_active)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU frame or renderer owner");
    return false;
  }
  if (frame->source_backend && frame->source_skip_backend) return true;
  if (begin && frame->source_backend && frame->source_clear_draw_buffer) {
    qa_scene_view clear = renderer->view;
    clear.clear_color = renderer->pipeline.color_write;
    clear.clear_depth = renderer->pipeline.depth_write;
    clear.clear_stencil = false;
    clear.color = (qa_scene_vec4){1, 0, 0.5f, 1};
    clear.depth = renderer->clear_depth;
    clear_view(renderer, &clear);
    if (renderer->controls.source.issuing && renderer->controls.source.frame == frame)
      renderer->controls.source.frame->source_clear_draw_buffer = false;
  }
  /* Versions no longer retained by any scene can release their target storage.
   */
  cpu_target **link = begin ? &renderer->targets : NULL;
  while (link && *link) {
    cpu_target *target = *link;
    if (target->image->references == 1 &&
        renderer->current != &target->framebuffer) {
      *link = target->next;
      qa_scene_image_release(target->image);
      buffer_destroy(&target->framebuffer);
      free(target);
    } else
      link = &target->next;
  }
  for (size_t i = first; i < frame->command_count; ++i) {
    const qa_scene_command *command = &frame->commands[i];
    bool ok = true;
    switch (command->kind) {
    case QA_SCENE_COMMAND_VIEW:
      if (!command->data.view.viewport.width ||
          !command->data.view.viewport.height ||
          !isfinite(command->data.view.depth) ||
          (command->data.view.clear_color &&
           (!isfinite(command->data.view.color.x) ||
            !isfinite(command->data.view.color.y) ||
            !isfinite(command->data.view.color.z) ||
            !isfinite(command->data.view.color.w))) ||
          (command->data.view.clip_enabled &&
           (!qa_vec_finite(command->data.view.clip_plane.normal) ||
            !isfinite(command->data.view.clip_plane.distance)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, i, "Invalid CPU view");
        ok = false;
        break;
      }
      renderer->view = command->data.view;
      if (!renderer->opacity_skip) {
        if (frame->source_backend && renderer->view.clear_depth)
          qa_render_source_state_bits(&renderer->pipeline,true,false);
        if (renderer->view.clear_color) renderer->pipeline.color_write = true;
        if (renderer->view.clear_depth) {
          renderer->pipeline.depth_write = true;
          renderer->clear_depth = (float)cpu_clamp(renderer->view.depth);
        }
        clear_view(renderer, &renderer->view);
      }
      break;
    case QA_SCENE_COMMAND_DRAW:
      if (renderer->preblend_gamma &&
          (command->data.draw.lighting!=QA_LIGHT_VERTEX || command->data.draw.shadow_atlas)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,i,"Generic overlay gamma requires an unlit primitive");
        ok=false;
      } else if (!renderer->opacity_skip)
        ok = cpu_draw(renderer, &command->data.draw, error);
      break;
    case QA_SCENE_COMMAND_TARGET:
      ok = select_target(renderer, command->data.target.image, error);
      break;
    case QA_SCENE_COMMAND_IMAGE:
      ok = update_image(renderer, command->data.image, error);
      break;
    case QA_SCENE_COMMAND_OUTPUT_DOMAIN:
      if (renderer->current != &renderer->display || renderer->opacity_active) {
        qa_error_set(error, QA_ERROR_ARGUMENT, i, "Output color domain requires the actual CPU display target");
        ok = false;
      } else ok = qa_output_domains_assign(&renderer->output_domains, command->data.output_domain.rect,
          QA_DRAW_BACK, command->data.output_domain.source, renderer->display.width, renderer->display.height, error);
      if (ok) renderer->preblend_gamma=false;
      break;
    case QA_SCENE_COMMAND_PREBLEND_GAMMA:
      if (renderer->current != &renderer->display || renderer->opacity_active) {
        qa_error_set(error, QA_ERROR_ARGUMENT, i, "Generic overlay gamma requires the actual CPU display target");
        ok = false;
      } else renderer->preblend_gamma = command->data.preblend_gamma.enabled &&
          !(renderer->options.present == qa_display_present_cpu &&
            qa_display_gamma_applied_is(renderer->options.present_context));
      break;
    case QA_SCENE_COMMAND_OPACITY_BEGIN:
      ok = begin_opacity(renderer, command->data.opacity.value, error);
      break;
    case QA_SCENE_COMMAND_OPACITY_END:
      ok = end_opacity(renderer, error);
      break;
    case QA_SCENE_COMMAND_FOG:
      if (!renderer->opacity_skip)
        ok = cpu_depth_fog(renderer, &command->data.fog.fog,
                           &command->data.fog.view, error);
      break;
    case QA_SCENE_COMMAND_DRAW_BUFFER:
      if (command->data.draw_buffer.buffer != QA_DRAW_FRONT &&
          command->data.draw_buffer.buffer != QA_DRAW_BACK) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, i,
                     "CPU presentation does not provide stereo draw buffers");
        ok = false;
        break;
      }
      if (command->data.draw_buffer.clear && !renderer->opacity_skip) {
        qa_scene_view clear = renderer->view;
        clear.clear_color = !frame->source_backend || renderer->pipeline.color_write;
        clear.clear_depth = !frame->source_backend || renderer->pipeline.depth_write;
        clear.clear_stencil = false;
        clear.color = (qa_scene_vec4){1, 0, 0.5f, 1};
        clear.depth = frame->source_backend ? renderer->clear_depth : 1;
        if (!frame->source_backend) {
          renderer->pipeline.color_write = renderer->pipeline.depth_write = true;
          renderer->clear_depth = 1;
        }
        clear_view(renderer, &clear);
      }
      break;
    case QA_SCENE_COMMAND_SWAP:
      ok = cpu_present_frame(renderer, error);
      break;
    default:
      qa_error_set(error, QA_ERROR_ARGUMENT, i, "Unknown CPU render command");
      ok = false;
      break;
    }
    if (!ok) {
      if (renderer->opacity_active) {
        if (renderer->opacity_value != 1)
          renderer->current = renderer->opacity_parent;
        renderer->opacity_parent = NULL;
        renderer->opacity_active = renderer->opacity_skip = false;
      }
      return false;
    }
  }
  if (finish && renderer->opacity_active) {
    if (renderer->opacity_value != 1)
      renderer->current = renderer->opacity_parent;
    renderer->opacity_parent = NULL;
    renderer->opacity_active = renderer->opacity_skip = false;
    qa_error_set(error, QA_ERROR_ARGUMENT, frame->command_count,
                 "CPU frame ended inside opacity scope");
    return false;
  }
  return true;
}
bool qa_cpu_source_execute_prefix(qa_render_controls *controls, const qa_scene_frame *frame,
    size_t first, bool begin, bool finish, qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket ||
      !controls->source.entered || !controls->source.issuing || controls->source.frame != frame) {
    qa_error_set(error, QA_ERROR_ARGUMENT, first, "CPU Source issue lost its actual renderer/frame owner");
    return false;
  }
  qa_cpu_renderer *renderer = controls->owner.cpu;
  renderer->executing = true;
  bool ok = cpu_execute_range(renderer, frame, first, begin, finish, error);
  renderer->executing = false;
  return ok;
}
bool qa_cpu_source_depth_range(qa_render_controls *controls,float near_depth,float far_depth,
    qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || !isfinite(near_depth) || !isfinite(far_depth) ||
      near_depth<0 || near_depth>1 || far_depth<0 || far_depth>1) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source depth range lost its actual CPU issue owner");
    return false;
  }
  controls->owner.cpu->pipeline.depth_near=near_depth;
  controls->owner.cpu->pipeline.depth_far=far_depth;
  return true;
}
bool qa_cpu_source_polygon_offset(qa_render_controls *controls,bool enabled,float factor,float units,
    qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || !isfinite(factor) || !isfinite(units)) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source polygon offset lost its actual CPU issue owner");
    return false;
  }
  qa_scene_state *state=&controls->owner.cpu->pipeline;
  state->polygon_offset=enabled;
  if (enabled) { state->offset_factor=factor; state->offset_units=units; }
  return true;
}
bool qa_cpu_source_cull(qa_render_controls *controls,qa_scene_cull cull,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || (unsigned)cull>QA_CULL_BACK) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source cull lost its actual CPU issue owner");
    return false;
  }
  controls->owner.cpu->pipeline.cull=cull;
  return true;
}
bool qa_cpu_source_texture_bind(qa_render_controls *controls,const qa_scene_image *image,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || !cpu_image_valid(image,error)) {
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source texture binding lost its actual CPU issue owner");
    return false;
  }
  const qa_scene_image **binding=controls->owner.cpu->bound+controls->attributes.texture_unit;
  if (*binding!=image) {
    qa_scene_image_retain(image);
    qa_scene_image_release(*binding);
    *binding=image;
  }
  return true;
}
bool qa_cpu_source_stage_state(qa_render_controls *controls,const qa_scene_state *state,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || !state) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source GL_State lost its actual CPU issue owner");
    return false;
  }
  qa_render_source_stage_state(&controls->owner.cpu->pipeline,state);
  return true;
}
bool qa_cpu_source_view_read(qa_render_controls *controls,qa_scene_view *out,qa_error *error)
{
  if (!out || !qa_cpu_source_scratch_current(controls) || controls->ticket) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source view lost its actual CPU owner"); return false;
  }
  *out=controls->owner.cpu->view; return true;
}
bool qa_cpu_execute(qa_cpu_renderer *renderer,const qa_scene_frame *frame,qa_error *error)
{
  if (renderer && renderer->controls.source.entered)
    return qa_material_source_frame_end(&renderer->controls.source, (qa_scene_frame *)frame, true, error);
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || renderer->executing || renderer->presenting || renderer->capturing) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU renderer is absent or executing"); return false;
  }
  renderer->executing=true;
  bool ok=cpu_execute_range(renderer,frame,0,true,true,error);
  renderer->executing=false; return ok;
}
static bool cpu_checkpoint_idle(const qa_cpu_renderer *renderer,qa_error *error)
{
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || renderer->executing || renderer->presenting || renderer->capturing ||
      renderer->opacity_active || renderer->opacity_parent) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU continuation requires its completed idle renderer owner"); return false;
  }
  return true;
}
bool qa_cpu_checkpoint_resources(const qa_cpu_renderer *renderer,qa_render_resource_visit_fn visit,void *context,qa_error *error)
{
  if (!visit || !cpu_checkpoint_idle(renderer,error)) return false;
  size_t ordinal=0;
  for (size_t i=0;i<2;++i,++ordinal)
    if (renderer->bound[i] && !visit(context,renderer->bound[i],NULL,ordinal,error)) return false;
  for (const cpu_target *target=renderer->targets;target;target=target->next,++ordinal)
    if (!visit(context,target->image,NULL,ordinal,error)) return false;
  if (renderer->controls.source.lightmap &&
      !visit(context,renderer->controls.source.lightmap,NULL,ordinal,error)) return false;
  return true;
}
static bool cpu_saved_buffer(qa_source_save_io *io,qa_cpu_renderer *renderer,cpu_framebuffer *buffer)
{
  bool reading=io->direction==QA_SOURCE_SAVE_READ,present=buffer->width!=0;
  if (!qa_source_save_bool(io,&present)) return false;
  if (!present) return reading || (!buffer->height && !buffer->color && !buffer->depth && !buffer->stencil);
  uint32_t width=buffer->width,height=buffer->height; bool depth_only=buffer->depth_only,alpha=buffer->alpha;
  if (!qa_source_save_u32(io,&width) || !qa_source_save_u32(io,&height) ||
      !qa_source_save_bool(io,&depth_only) || !qa_source_save_bool(io,&alpha)) return false;
  size_t count=0;
  if (!dimensions(width,height,&count,io->error)) return false;
  if (reading) {
    if (!buffer_create(buffer,width,height,depth_only,renderer,io->error)) return false;
    buffer->alpha=alpha;
  }
  if (!buffer->depth || (!depth_only && !buffer->color) ||
      ((buffer->stencil!=NULL)!=(renderer->options.stencil_bits!=0))) return false;
  if (!depth_only && !qa_source_save_bytes(io,buffer->color,count*4)) return false;
  for (size_t i=0;i<count;++i)
    if (!qa_source_save_f64(io,buffer->depth+i) || !isfinite(buffer->depth[i])) return false;
  for (size_t i=0;buffer->stencil && i<count;++i)
    if (!qa_source_save_u32(io,buffer->stencil+i) || buffer->stencil[i]>renderer->stencil_maximum) return false;
  return true;
}
static bool cpu_saved_fields(qa_source_save_io *io,qa_cpu_renderer *renderer,const qa_render_checkpoint_refs *refs,
    const qa_cpu_options *installed)
{
  bool reading=io->direction==QA_SOURCE_SAVE_READ;
  uint8_t magic[4]={'Q','C','P','U'}; uint32_t version=12;
  bool presenter=reading?false:renderer->options.present!=NULL;
  if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QCPU",4) ||
      !qa_source_save_u32(io,&version) || version<4 || version>12 ||
      !qa_render_controls_saved_fields(io,&renderer->controls,version,refs) ||
      !qa_source_save_u32(io,&renderer->options.width) || !qa_source_save_u32(io,&renderer->options.height) ||
      !qa_source_save_u8(io,&renderer->options.subpixel_bits) || !qa_source_save_u8(io,&renderer->options.stencil_bits) ||
      !qa_source_save_u8(io,&renderer->options.alpha_bits) || !qa_source_save_u64(io,&renderer->options.owner) ||
      !qa_source_save_bool(io,&presenter) || renderer->options.subpixel_bits<4 || renderer->options.subpixel_bits>16 ||
      renderer->options.stencil_bits>32 || (renderer->options.alpha_bits!=0 && renderer->options.alpha_bits!=8)) return false;
  if (reading) {
    if (!installed || installed->width!=renderer->options.width || installed->height!=renderer->options.height ||
        installed->owner!=renderer->options.owner || installed->subpixel_bits!=renderer->options.subpixel_bits ||
        installed->stencil_bits!=renderer->options.stencil_bits || installed->alpha_bits!=renderer->options.alpha_bits ||
        (installed->present!=NULL)!=presenter) return false;
    renderer->options.present=installed->present; renderer->options.present_context=installed->present_context;
    renderer->stencil_maximum=renderer->options.stencil_bits==32?UINT32_MAX:
        (uint32_t)((UINT64_C(1)<<renderer->options.stencil_bits)-1);
  }
  if (!cpu_saved_buffer(io,renderer,&renderer->display) || renderer->display.depth_only ||
      renderer->display.alpha!=(renderer->options.alpha_bits!=0) ||
      renderer->display.width!=renderer->options.width || renderer->display.height!=renderer->options.height ||
      !cpu_saved_buffer(io,renderer,&renderer->opacity)) return false;
  if (version>=5 && !qa_output_domains_codec(io,&renderer->output_domains,
      renderer->display.width,renderer->display.height)) return false;
  if (version>=6) {
    if (!render_save_pipeline(io,&renderer->pipeline) ||
        !qa_source_save_f32(io,&renderer->clear_depth) || !isfinite(renderer->clear_depth) ||
        renderer->clear_depth<0 || renderer->clear_depth>1) return false;
  } else if (reading) {
    qa_scene_state_default(&renderer->pipeline);
    renderer->clear_depth=1;
  }
  if (version>=11) {
    if (!qa_source_save_bool(io,&renderer->preblend_gamma)) return false;
  } else if (reading) renderer->preblend_gamma=false;
  size_t count=0; uint64_t current=0;
  if (!reading) {
    if (renderer->current==&renderer->display) current=1;
    for (cpu_target *target=renderer->targets;target;target=target->next) {
      ++count; if (renderer->current==&target->framebuffer) current=count+1;
    }
  }
  if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(cpu_target)) ||
      !qa_source_save_u64(io,&current) || !current || current>count+1) return false;
  cpu_target *target=renderer->targets,**tail=&renderer->targets;
  for (size_t i=0;i<count;++i) {
    if (reading) {
      target=calloc(1,sizeof(*target)); if (!target) { qa_error_set(io->error,QA_ERROR_MEMORY,0,"Restoring CPU retained target row"); return false; }
      *tail=target; tail=&target->next;
    }
    if (!target || !render_save_image(io,refs,&target->image) || !target->image || !cpu_image_valid(target->image,io->error) ||
        !cpu_saved_buffer(io,renderer,&target->framebuffer) || target->framebuffer.width!=target->image->levels[0].width ||
        target->framebuffer.height!=target->image->levels[0].height ||
        target->framebuffer.depth_only!=(target->image->kind==QA_SCENE_DEPTH32F) ||
        target->framebuffer.alpha!=(target->image->kind==QA_SCENE_RGBA8)) return false;
    for (cpu_target *prior=renderer->targets;prior!=target;prior=prior->next)
      if (prior->image==target->image || (prior->image->identity==target->image->identity &&
          prior->image->revision==target->image->revision)) return false;
    if (reading && current==i+2) renderer->current=&target->framebuffer;
    target=target->next;
  }
  if (reading && current==1) renderer->current=&renderer->display;
  if (!render_save_view(io,&renderer->view) || !render_save_rect(io,&renderer->opacity_viewport) ||
      !qa_source_save_bool(io,&renderer->opacity_skip) || !qa_source_save_f32(io,&renderer->opacity_value) ||
      !isfinite(renderer->opacity_value) || !qa_source_save_bool(io,&renderer->gamma_enabled) ||
      !qa_source_save_f32(io,&renderer->gamma_value) || !isfinite(renderer->gamma_value) ||
      renderer->gamma_value<.5f || renderer->gamma_value>3 ||
      renderer->gamma_enabled!=(renderer->gamma_value!=1) ||
      !qa_source_save_bool(io,&renderer->overdraw) || !qa_source_save_bytes(io,renderer->gamma,256) ||
      !qa_source_save_count(io,&renderer->vertex_capacity,SIZE_MAX/sizeof(cpu_vertex))) return false;
  for (size_t i=0;i<2;++i) if (!render_save_image(io,refs,renderer->bound+i)) return false;
  if (reading) {
    size_t pixels=(size_t)renderer->options.width*renderer->options.height;
    renderer->output=malloc(pixels*4);
    if (renderer->vertex_capacity) renderer->vertices=malloc(renderer->vertex_capacity*sizeof(cpu_vertex));
    if (!renderer->output || (renderer->vertex_capacity && !renderer->vertices)) {
      qa_error_set(io->error,QA_ERROR_MEMORY,0,"Restoring CPU presentation/transform storage"); return false;
    }
  }
  return true;
}
bool qa_cpu_checkpoint(const qa_cpu_renderer *renderer,const qa_render_checkpoint_refs *refs,qa_buffer *out,qa_error *error)
{
  if (!out || out->data || out->size || !cpu_checkpoint_idle(renderer,error)) return false;
  qa_cpu_renderer state=*renderer; qa_source_save_io io={0};
  if (renderer->current==&renderer->display) state.current=&state.display;
  ((qa_cpu_renderer *)renderer)->capturing=true;
  bool ok=qa_source_save_writer(&io,NULL,error) && cpu_saved_fields(&io,&state,refs,NULL) && qa_source_save_finish(&io,out);
  qa_source_save_dispose(&io); ((qa_cpu_renderer *)renderer)->capturing=false;
  if (!ok && (!error || error->code==QA_OK)) qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid actual CPU renderer continuation");
  return ok;
}
bool qa_cpu_restore(qa_bytes bytes,const qa_cpu_options *options,const qa_render_checkpoint_refs *refs,qa_cpu_renderer **out,qa_error *error)
{
  if (!out || *out || !options) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU restore requires detached owner output/options"); return false; }
  qa_cpu_renderer *renderer=calloc(1,sizeof(*renderer));
  if (!renderer) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating detached CPU renderer"); return false; }
  qa_render_controls_init_cpu(&renderer->controls, renderer);
  qa_source_save_io io={0};
  bool ok=qa_source_save_reader(&io,NULL,bytes,error) && cpu_saved_fields(&io,renderer,refs,options) && qa_source_save_finish(&io,NULL);
  qa_source_save_dispose(&io);
  if (!ok) {
    qa_cpu_destroy(renderer);
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid saved CPU renderer continuation");
    return false;
  }
  *out=renderer; return true;
}

static bool cpu_surface_prepare(qa_cpu_renderer *renderer,uint32_t width,uint32_t height,float gamma,
    qa_cpu_present present,void *context,bool present_candidate,qa_cpu_surface_ticket **out,qa_error *error)
{
  size_t count=0;
  if (!out || *out || !cpu_checkpoint_idle(renderer,error) || renderer->destroy_pending ||
      renderer->current!=&renderer->display || renderer->opacity_active ||
      !isfinite(gamma) || gamma<0.5f || gamma>3 || present!=renderer->options.present ||
      !dimensions(width,height,&count,error)) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU surface preparation requires an idle display owner and valid settings");
    return false;
  }
  qa_cpu_surface_ticket *ticket=calloc(1,sizeof(*ticket));
  if (!ticket) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating CPU surface ticket"); return false; }
  ticket->renderer=renderer; ticket->original=renderer->options;
  ticket->original_display=renderer->display; ticket->original_opacity=renderer->opacity;
  ticket->original_targets=renderer->targets; ticket->original_vertices=renderer->vertices;
  ticket->original_vertex_capacity=renderer->vertex_capacity;
  for (size_t i=0;i<2;++i) ticket->original_bound[i]=renderer->bound[i];
  ticket->original_output=renderer->output;
  ticket->original_gamma_value=renderer->gamma_value; ticket->original_gamma_enabled=renderer->gamma_enabled;
  memcpy(ticket->original_gamma,renderer->gamma,256); ticket->gamma_value=gamma;
  ticket->width=width; ticket->height=height; ticket->present=present; ticket->context=context;
  ticket->resized=width!=renderer->display.width || height!=renderer->display.height;
  ticket->gamma_enabled=gamma!=1;
  renderer->surface_ticket=ticket; *out=ticket;
  if (ticket->resized && !buffer_create(&ticket->next,width,height,false,renderer,error)) return false;
  ticket->output=malloc(count*4);
  if (!ticket->output) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating prepared CPU presentation"); return false; }
  for (size_t i=0;i<256;++i)
    ticket->gamma[i]=gamma==1?(uint8_t)i:cpu_byte(pow((float)i/255,1.0f/gamma));
  const uint8_t *pixels=ticket->resized?ticket->next.color:renderer->display.color;
  for (size_t i=0;i<count;++i) {
    for (size_t c=0;c<3;++c) ticket->output[i*4+c]=ticket->gamma[pixels[i*4+c]];
    ticket->output[i*4+3]=pixels[i*4+3];
  }
  if (present_candidate && present && !present(context,(qa_bytes){ticket->output,count*4},width,height,error)) return false;
  ticket->prepared=true;
  return qa_cpu_surface_ready(ticket,error);
}

bool qa_cpu_surface_prepare(qa_cpu_renderer *renderer,uint32_t width,uint32_t height,float gamma,
    qa_cpu_present present,void *context,qa_cpu_surface_ticket **out,qa_error *error)
{ return cpu_surface_prepare(renderer,width,height,gamma,present,context,true,out,error); }

bool qa_cpu_gamma_prepare(qa_cpu_renderer *renderer,float gamma,qa_cpu_present present,void *context,
    qa_cpu_surface_ticket **out,qa_error *error)
{
  if (!renderer || renderer->options.present!=present || renderer->options.present_context!=context) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU gamma preparation requires its actual renderer and presentation endpoint"); return false;
  }
  return cpu_surface_prepare(renderer,renderer->options.width,renderer->options.height,gamma,
      renderer->options.present,renderer->options.present_context,false,out,error);
}

bool qa_cpu_surface_refresh(qa_cpu_surface_ticket *ticket,qa_error *error)
{
  if (!qa_cpu_surface_ready(ticket,error)) return false;
  qa_cpu_renderer *renderer=ticket->renderer;
  const uint8_t *pixels=ticket->resized?ticket->next.color:renderer->display.color;
  bool native=ticket->present==qa_display_present_cpu && qa_display_gamma_applied_is(ticket->context);
  size_t count=(size_t)ticket->width*ticket->height;
  for (size_t i=0;i<count;++i) {
    bool direct=native || (!ticket->resized && qa_output_domains_source(&renderer->output_domains,
        (uint32_t)(i%ticket->width),(uint32_t)(i/ticket->width),QA_DRAW_BACK));
    for (size_t c=0;c<3;++c) ticket->output[i*4+c]=direct?pixels[i*4+c]:ticket->gamma[pixels[i*4+c]];
    ticket->output[i*4+3]=pixels[i*4+3];
  }
  if (ticket->present && !ticket->present(ticket->context,(qa_bytes){ticket->output,count*4},
      ticket->width,ticket->height,error)) return false;
  return qa_cpu_surface_ready(ticket,error);
}
static bool cpu_buffer_same(const cpu_framebuffer *a,const cpu_framebuffer *b)
{
  return a->width==b->width && a->height==b->height && a->depth_only==b->depth_only &&
      a->alpha==b->alpha && a->color==b->color && a->depth==b->depth && a->stencil==b->stencil;
}
bool qa_cpu_surface_ready_is(const qa_cpu_surface_ticket *ticket)
{
  const qa_cpu_renderer *renderer=ticket?ticket->renderer:NULL;
  if (!renderer || renderer->surface_ticket!=ticket || renderer->destroy_pending || !ticket->prepared ||
      ticket->published || renderer->executing || renderer->presenting || renderer->capturing ||
      renderer->opacity_active || renderer->current!=&renderer->display ||
      renderer->options.owner!=ticket->original.owner || renderer->options.width!=ticket->original.width ||
      renderer->options.height!=ticket->original.height || renderer->options.present!=ticket->original.present ||
      renderer->options.present_context!=ticket->original.present_context || !ticket->output ||
      renderer->options.subpixel_bits!=ticket->original.subpixel_bits ||
      renderer->options.stencil_bits!=ticket->original.stencil_bits || renderer->options.alpha_bits!=ticket->original.alpha_bits ||
      !cpu_buffer_same(&renderer->display,&ticket->original_display) ||
      !cpu_buffer_same(&renderer->opacity,&ticket->original_opacity) || renderer->opacity_parent ||
      renderer->targets!=ticket->original_targets || renderer->vertices!=ticket->original_vertices ||
      renderer->vertex_capacity!=ticket->original_vertex_capacity ||
      renderer->bound[0]!=ticket->original_bound[0] || renderer->bound[1]!=ticket->original_bound[1] ||
      renderer->output!=ticket->original_output ||
      renderer->gamma_value!=ticket->original_gamma_value || renderer->gamma_enabled!=ticket->original_gamma_enabled ||
      memcmp(renderer->gamma,ticket->original_gamma,256) ||
      (ticket->resized && (!ticket->next.color || !ticket->next.depth || ticket->next.depth_only ||
          ticket->next.width!=ticket->width || ticket->next.height!=ticket->height ||
          ticket->next.alpha!=(renderer->options.alpha_bits!=0) ||
          (ticket->next.stencil!=NULL)!=(renderer->options.stencil_bits!=0)))) return false;
  return true;
}
bool qa_cpu_surface_ready(const qa_cpu_surface_ticket *ticket,qa_error *error)
{
  if (qa_cpu_surface_ready_is(ticket)) return true;
  qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU surface ticket is not prepared/current"); return false;
}

void qa_cpu_surface_publish(qa_cpu_surface_ticket *ticket)
{
  if (!ticket || !ticket->prepared || ticket->published) return;
  qa_cpu_renderer *renderer=ticket->renderer;
  if (ticket->resized) {
    ticket->retired_domains=renderer->output_domains; renderer->output_domains=(qa_output_domains){0};
    ticket->retired_display=renderer->display; ticket->retired_opacity=renderer->opacity;
    renderer->display=ticket->next; ticket->next=(cpu_framebuffer){0};
    renderer->opacity=(cpu_framebuffer){0}; renderer->current=&renderer->display;
    renderer->view.viewport=(qa_scene_rect){0,0,ticket->width,ticket->height}; renderer->view.depth=1;
  }
  ticket->retired_output=renderer->output; renderer->output=ticket->output; ticket->output=NULL;
  memcpy(renderer->gamma,ticket->gamma,256); renderer->gamma_enabled=ticket->gamma_enabled;
  renderer->gamma_value=ticket->gamma_value;
  renderer->options.width=ticket->width; renderer->options.height=ticket->height;
  renderer->options.present=ticket->present; renderer->options.present_context=ticket->context;
  ticket->published=true;
}

static void cpu_surface_release(qa_cpu_surface_ticket **out)
{
  qa_cpu_surface_ticket *ticket=*out; qa_cpu_renderer *renderer=ticket->renderer;
  buffer_destroy(&ticket->next); buffer_destroy(&ticket->retired_display); buffer_destroy(&ticket->retired_opacity);
  free(ticket->output); free(ticket->retired_output);
  qa_output_domains_destroy(&ticket->retired_domains);
  renderer->surface_ticket=NULL; free(ticket); *out=NULL;
  if (renderer->destroy_pending) qa_cpu_destroy(renderer);
}

bool qa_cpu_surface_abort(qa_cpu_surface_ticket **out,qa_error *error)
{
  if (!out || (*out && ((*out)->published || (*out)->renderer->surface_ticket!=*out))) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU surface abort requires its retained unpublished ticket"); return false;
  }
  if (!*out) return true;
  cpu_surface_release(out); return true;
}

bool qa_cpu_surface_retire(qa_cpu_surface_ticket **out,qa_error *error)
{
  if (!out || (*out && (!(*out)->published || (*out)->renderer->surface_ticket!=*out))) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU surface retirement requires its published ticket"); return false;
  }
  if (*out) cpu_surface_release(out);
  return true;
}
