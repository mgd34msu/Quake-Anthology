#include "internal.h"
#include <limits.h>
#include <stdio.h>
#include <SDL_timer.h>
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
  if (renderer && !renderer->surface_ticket && !renderer->controls.ticket && !renderer->controls.image_ticket && !renderer->controls.source.entered) return true;
  qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CPU renderer has a retained settings ticket");
  return false;
}
static cpu_source_image *cpu_source_object(qa_cpu_renderer *,const qa_scene_image *);

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
  if (!cpu_texture_components_init(renderer, error)) {
    qa_cpu_destroy(renderer);
    return NULL;
  }
  cpu_raster_pool_create(renderer);
  return renderer;
}
void qa_cpu_destroy(qa_cpu_renderer *renderer) {
  if (!renderer)
    return;
  if (renderer->surface_ticket || renderer->controls.ticket || renderer->controls.image_ticket || renderer->controls.source.entered) { renderer->destroy_pending=true; return; }
  cpu_raster_pool_destroy(renderer);
  material_source_release(&renderer->controls.source);
  qa_render_source_texture_release(&renderer->controls.zero_texture);
  for (size_t i = 0; i < 2; ++i)
    qa_scene_image_release(renderer->bound[i]);
  for (uint32_t i=0;i<renderer->source_image_count;++i) {
    qa_render_source_texture_release(&renderer->source_images[i].texture);
    qa_scene_image_release(renderer->source_images[i].image);
    qa_scene_resources_destroy(renderer->source_images[i].owner);
  }
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
  if (image->source_q3 && cpu_source_object(renderer,image)) return true;
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
  if (view->clear_color) renderer->clear_color=view->color;
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
  if (renderer->source_frame) {
    qa_render_controls *controls=&renderer->controls;
    if (controls->frame_values.show_images && !qa_cpu_source_image_grid(controls,controls->frame_values.show_images,error)) return false;
    if (renderer->overdraw && renderer->display.stencil)
      for (size_t i=0;i<(size_t)renderer->display.width*renderer->display.height;++i)
        controls->counters.overdraw+=(uint8_t)renderer->display.stencil[i];
    qa_render_source_report(controls,renderer->display.width,renderer->display.height);
  }
  renderer->presenting=true;
  bool ok = !renderer->options.present ||
         renderer->options.present(
             renderer->options.present_context, qa_cpu_pixels(renderer),
             renderer->display.width, renderer->display.height, error);
  renderer->presenting=false;
  if (ok) {
    renderer->source_frame = false;
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
  frame.source_backend=renderer->source_frame;
  bool ok=qa_material_source_swap_end(&renderer->controls.source,&frame,error);
  bool skip=frame.source_backend && frame.source_skip_backend;
  qa_scene_frame_destroy(&frame);
  if (ok && skip) qa_render_source_report(&renderer->controls,renderer->display.width,renderer->display.height);
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
  if (frame->source_backend) renderer->source_frame=true;
  if (frame->source_backend && frame->source_skip_backend) return true;
  if (begin && frame->source_backend && frame->source_clear_draw_buffer) {
    qa_scene_view clear = renderer->view;
    clear.clear_color = renderer->pipeline.color_write;
    clear.clear_depth = renderer->pipeline.depth_write;
    clear.clear_stencil = false;
    clear.color = (qa_scene_vec4){1, 0, 0.5f, 1};
    renderer->clear_color=clear.color;
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
    if (command->kind != QA_SCENE_COMMAND_DRAW) cpu_raster_flush(renderer);
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
        if (frame->source_backend && renderer->view.clear_depth) {
          if (renderer->controls.frame_values.finish==0 || renderer->controls.frame_values.finish==1)
            renderer->controls.finish_called=true;
          if (renderer->overdraw) renderer->view.clear_stencil=true;
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
        ok = cpu_draw_queued(renderer, &command->data.draw, error);
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
    renderer->clear_color=clear.color;
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
      cpu_raster_flush(renderer);
      if (renderer->opacity_active) {
        if (renderer->opacity_value != 1)
          renderer->current = renderer->opacity_parent;
        renderer->opacity_parent = NULL;
        renderer->opacity_active = renderer->opacity_skip = false;
      }
      return false;
    }
  }
  cpu_raster_flush(renderer);
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
static cpu_source_image *cpu_source_object(qa_cpu_renderer *,const qa_scene_image *);
static void cpu_source_bind(qa_cpu_renderer *renderer,const qa_scene_image *image)
{
  cpu_source_image *object=cpu_source_object(renderer,image);
  if (object) image=object->image;
  qa_render_source_image_used(&renderer->controls,image);
  uint32_t unit=renderer->controls.attributes.texture_unit;
  const qa_scene_image **binding=renderer->bound+unit;
  if (*binding!=image) {
    qa_scene_image_retain(image); qa_scene_image_release(*binding); *binding=image;
    renderer->controls.attributes.actual_empty[unit]=image==NULL;
  }
}
size_t qa_cpu_source_images_metadata_count(const qa_render_controls *controls)
{ return controls->owner.cpu->source_image_count; }
const qa_scene_image *qa_cpu_source_image_metadata_at(const qa_render_controls *controls,size_t ordinal)
{ return controls->owner.cpu->source_images[ordinal].image; }
size_t qa_cpu_source_texture_metadata_count(const qa_render_controls *controls)
{
  size_t count=controls->zero_texture.count;
  const qa_cpu_renderer *renderer=controls->owner.cpu;
  for (uint32_t i=0;i<renderer->source_image_count;++i) count+=renderer->source_images[i].texture.count;
  return count;
}
const qa_scene_image *qa_cpu_source_texture_metadata_at(const qa_render_controls *controls,size_t ordinal)
{
  if (ordinal<controls->zero_texture.count) return controls->zero_texture.images[ordinal];
  ordinal-=controls->zero_texture.count;
  const qa_cpu_renderer *renderer=controls->owner.cpu;
  for (uint32_t i=0;i<renderer->source_image_count;++i) {
    const qa_render_source_texture *texture=&renderer->source_images[i].texture;
    if (ordinal<texture->count) return texture->images[ordinal];
    ordinal-=texture->count;
  }
  return NULL;
}
qa_scene_filter qa_cpu_source_image_sampling(const qa_render_controls *controls,const qa_scene_image *image,
    bool *magnification_linear)
{
  const qa_cpu_renderer *renderer=controls->owner.cpu;
  const qa_render_source_texture *texture=NULL;
  if (image==&controls->zero_texture.view) texture=&controls->zero_texture;
  else for (uint32_t i=0;i<renderer->source_image_count;++i)
    if (image==&renderer->source_images[i].texture.view || renderer->source_images[i].image==image) {
      texture=&renderer->source_images[i].texture;
      break;
    }
  if (magnification_linear) *magnification_linear=texture?texture->magnification_linear:
      image->filter==QA_SCENE_LINEAR || image->filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
      image->filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
  if (texture) return image==&texture->view?image->filter:texture->filter;
  return image->source_mipmap?controls->source_filter:image->filter;
}
static cpu_source_image *cpu_source_object(qa_cpu_renderer *renderer,const qa_scene_image *image)
{
  for (uint32_t i=0;i<renderer->source_image_count;++i)
    if (renderer->source_images[i].image==image) return renderer->source_images+i;
  qa_scene_resources *owner=image && image->source_q3?qa_scene_image_resource_owner(image):NULL;
  if (owner) for (uint32_t i=renderer->source_image_count;i>0;--i)
    if (renderer->source_images[i-1].owner==owner && renderer->source_images[i-1].image->identity==image->identity)
      return renderer->source_images+i-1;
  return NULL;
}
bool qa_cpu_source_texture_upload(qa_render_controls *controls,const qa_scene_image *slot,
    const qa_scene_image *image,const qa_scene_image *binding,bool redefine,bool dirty,qa_error *error)
{
  qa_cpu_renderer *renderer=controls->owner.cpu;
  cpu_source_image *registered=cpu_source_object(renderer,slot),*selected=cpu_source_object(renderer,binding);
  qa_scene_resources *owner=qa_scene_image_resource_owner(image);
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || controls->image_ticket ||
      !registered || registered->image!=slot || !selected || !image || !owner || owner!=registered->owner ||
      image->identity!=slot->identity || (image->kind!=QA_SCENE_RGBA8 && image->kind!=QA_SCENE_RGB8) || image->level_count!=1 ||
      !cpu_image_valid(image,error)) {
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source cinematic upload lost its actual CPU scratch object");
    return false;
  }
  cpu_source_bind(renderer,selected->image);
  if (!dirty && !redefine) return true;
  uint32_t unit=controls->attributes.texture_unit;
  qa_render_source_texture *texture=controls->attributes.actual_empty[unit]?&controls->zero_texture:&selected->texture;
  if (redefine) {
    if (!qa_render_source_texture_level(texture,0,image,owner,QA_SCENE_RGB8,QA_Q3_TEXTURE_RGB8,error)) return false;
    qa_render_source_texture_filter(texture,QA_SCENE_LINEAR); texture->wrap=QA_SCENE_CLAMP;
  } else {
    bool updated=false;
    if (!qa_render_source_texture_subimage(texture,image,&updated,error)) return false;
  }
  if (texture==&selected->texture) selected->filter=texture->filter;
  return true;
}
const qa_scene_image *qa_cpu_source_texture_image(qa_render_controls *controls,uint32_t unit,const qa_scene_image *image)
{
  if (controls->attributes.actual_empty[unit]) return qa_render_source_texture_view(&controls->zero_texture);
  cpu_source_image *row=cpu_source_object(controls->owner.cpu,image);
  return row?qa_render_source_texture_view(&row->texture):image;
}
bool qa_cpu_source_texture_filter_apply(qa_render_controls *controls,bool no_bind,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || controls->source.entered) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source filter lost its real idle CPU image owner"); return false;
  }
  qa_cpu_renderer *renderer=controls->owner.cpu;
  const qa_scene_image *dlight=NULL;
  if (no_bind && !qa_render_controls_source_dlight_read(controls,&dlight,error)) return false;
  uint32_t unit=controls->attributes.texture_unit;
  for (uint32_t i=0;i<renderer->source_image_count;++i) {
    cpu_source_image *row=renderer->source_images+i;
    if (row->image->source_mipmap) {
      cpu_source_image *selected=dlight?cpu_source_object(renderer,dlight):row;
      cpu_source_bind(renderer,selected->image);
      if (!controls->attributes.actual_empty[unit]) {
        selected->filter=controls->source_filter;
        qa_render_source_texture_filter(&selected->texture,controls->source_filter);
      } else qa_render_source_texture_filter(&controls->zero_texture,controls->source_filter);
    }
  }
  return true;
}
bool qa_cpu_source_image_admit(qa_render_controls *controls,const qa_scene_image *image,const qa_scene_image *binding,
    uint32_t unit,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !image || !image->source_q3 || unit>1 ||
      !cpu_image_valid(image,error)) {
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source image admission lost its actual CPU recipient");
    return false;
  }
  qa_cpu_renderer *renderer=controls->owner.cpu;
  for (uint32_t i=0;i<renderer->source_image_count;++i)
    if (renderer->source_images[i].image==image) return true;
  if (renderer->source_image_count>=CPU_SOURCE_IMAGES_QA) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"MAX_DRAWIMAGES hit in actual Source CPU image admission"); return false;
  }
  qa_scene_resources *owner=qa_scene_image_resource_owner(image);
  if (!owner || !qa_scene_resources_retain(owner,error)) {
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source CPU image lost its actual bank owner");
    return false;
  }
  if (binding!=image && !cpu_source_object(renderer,binding)) {
    qa_scene_resources_destroy(owner);
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source no-bind upload lost its genuinely admitted CPU target"); return false;
  }
  qa_scene_image_retain(image);
  cpu_source_image *requested=renderer->source_images+renderer->source_image_count++;
  *requested=(cpu_source_image){.image=image,.owner=owner,
      .filter=image->source_mipmap?controls->source_filter:image->filter};
  qa_render_source_texture_init(&requested->texture);
  controls->attributes.texture_unit=unit;
  cpu_source_bind(renderer,binding);
  cpu_source_image *selected=cpu_source_object(renderer,binding);
  qa_render_source_texture *texture=controls->attributes.actual_empty[unit]?&controls->zero_texture:&selected->texture;
  if (!qa_render_source_texture_upload(texture,image,owner,requested->filter,error)) return false;
  if (texture==&selected->texture) selected->filter=texture->filter;
  controls->attributes.actual_empty[unit]=true;
  if (unit==1) controls->attributes.texture_unit=0;
  return true;
}
typedef struct cpu_source_prepared_image {
  cpu_source_image row;
  const qa_scene_resource_policy *policy;
  qa_scene_resources *destination;
  uint64_t creation;
  size_t policy_ordinal;
  uint32_t unit;
} cpu_source_prepared_image;
typedef struct cpu_source_prepared_bank {
  const qa_scene_resource_policy *policy;
  size_t count;
} cpu_source_prepared_bank;
typedef struct cpu_source_prepared_update {
  cpu_source_image *original;
  qa_render_source_texture texture;
} cpu_source_prepared_update;
struct qa_cpu_source_images_ticket {
  qa_cpu_renderer *renderer;
  cpu_source_prepared_image *rows;
  size_t count;
  cpu_source_prepared_bank *banks;
  size_t bank_count;
  const qa_scene_image *bound[2];
  qa_render_source_attributes attributes;
  qa_scene_filter filter;
  qa_render_source_texture zero_texture;
  cpu_source_prepared_update *updates;
  size_t update_count;
  const qa_scene_image *final_bound[2];
  qa_render_source_attributes final_attributes;
  uint32_t original_count;
  bool prepared,published,restart;
};
static int cpu_source_prepared_order(const void *a,const void *b)
{
  const cpu_source_prepared_image *first=a,*second=b;
  return first->creation<second->creation?-1:first->creation>second->creation;
}
bool qa_cpu_source_images_prepare(qa_render_controls *controls,qa_scene_resource_policy *const *banks,size_t count,
    bool no_bind,const qa_render_source_restart_values *restart_values,
    qa_cpu_source_images_ticket **out,qa_error *error)
{
  bool restart=restart_values!=NULL;
  qa_scene_filter filter=restart?restart_values->filter:controls->source_filter;
  if (!out || *out || !qa_cpu_render_controls_current(controls) || controls->source.entered) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared Source images require their actual idle CPU owner"); return false;
  }
  qa_cpu_renderer *renderer=controls->owner.cpu;
  qa_cpu_source_images_ticket *ticket=calloc(1,sizeof(*ticket));
  if (!ticket) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining prepared Source CPU images"); return false; }
  *out=ticket; ticket->renderer=renderer; ticket->original_count=renderer->source_image_count;
  ticket->restart=restart;
  ticket->bound[0]=renderer->bound[0]; ticket->bound[1]=renderer->bound[1];
  ticket->attributes=controls->attributes; ticket->filter=controls->source_filter;
  ticket->final_bound[0]=restart?NULL:renderer->bound[0]; ticket->final_bound[1]=restart?NULL:renderer->bound[1];
  if (restart) {
    qa_render_source_attributes_init(&ticket->final_attributes);
    qa_render_source_texture_init(&ticket->zero_texture);
  } else {
    ticket->final_attributes=controls->attributes;
    if (!qa_render_source_texture_clone(&ticket->zero_texture,&controls->zero_texture,error)) return false;
  }
  ticket->banks=count?calloc(count,sizeof(*ticket->banks)):NULL;
  if (count && !ticket->banks) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining Source CPU image bank children"); return false; }
  ticket->bank_count=count;
  size_t total=0;
  for (size_t i=0;i<count;++i) {
    size_t images=0;
    if (!qa_scene_resource_policy_source_image_count(banks[i],&images,error)) return false;
    ticket->banks[i]=(cpu_source_prepared_bank){banks[i],images};
    if (images>CPU_SOURCE_IMAGES_QA-total || total+images>CPU_SOURCE_IMAGES_QA-(restart?0:ticket->original_count)) {
      qa_error_set(error,QA_ERROR_ARGUMENT,0,"MAX_DRAWIMAGES hit preparing actual Source CPU images"); return false;
    }
    total+=images;
  }
  ticket->rows=total?calloc(total,sizeof(*ticket->rows)):NULL;
  ticket->updates=total?calloc(total,sizeof(*ticket->updates)):NULL;
  if (total && (!ticket->rows || !ticket->updates)) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining Source CPU image creation order"); return false; }
  for (size_t i=0;i<count;++i) for (size_t ordinal=0;ordinal<ticket->banks[i].count;++ordinal) {
    cpu_source_prepared_image *row=ticket->rows+ticket->count;
    const qa_scene_image *image=NULL;
    row->policy=banks[i]; row->policy_ordinal=ordinal;
    row->destination=qa_scene_resource_policy_destination(banks[i]);
    qa_scene_resources *owner=qa_scene_resource_policy_source(banks[i]);
    if (!qa_scene_resource_policy_source_image_at(banks[i],ordinal,&image,&row->creation,error) ||
        !owner || !row->destination || !image || !image->source_q3 || image->source_texture_unit>1 ||
        qa_scene_image_resource_owner(image)!=row->destination || !cpu_image_valid(image,error) ||
        !qa_scene_resources_retain(owner,error)) return false;
    qa_scene_image_retain(image);
    row->row=(cpu_source_image){.image=image,.owner=owner,.filter=image->source_mipmap?filter:image->filter};
    qa_render_source_texture_init(&row->row.texture);
    row->unit=image->source_texture_unit; ++ticket->count;
  }
  if (ticket->count>1) qsort(ticket->rows,ticket->count,sizeof(*ticket->rows),cpu_source_prepared_order);
  for (size_t i=1;i<ticket->count;++i)
    if (ticket->rows[i-1].creation==ticket->rows[i].creation) {
      qa_error_set(error,QA_ERROR_ARGUMENT,i,"Source CPU image roster duplicates an actual constructor"); return false;
    }
  for (size_t i=0;i<ticket->count;++i) {
    cpu_source_prepared_image *row=ticket->rows+i;
    cpu_source_image *selected=&row->row;
    bool old=false;
    if (no_bind) {
      for (size_t j=i;j>0;--j)
        if (ticket->rows[j-1].row.image->source_dlight) {
          selected=&ticket->rows[j-1].row; break;
        }
      if (selected==&row->row && !restart)
        for (uint32_t j=renderer->source_image_count;j>0;--j)
          if (renderer->source_images[j-1].image->source_dlight) {
            selected=renderer->source_images+j-1; old=true; break;
          }
    }
    uint32_t unit=row->unit;
    if (ticket->final_bound[unit]!=selected->image) {
      ticket->final_bound[unit]=selected->image; ticket->final_attributes.actual_empty[unit]=false;
    }
    qa_render_source_texture *texture=&ticket->zero_texture;
    if (!ticket->final_attributes.actual_empty[unit]) {
      texture=&selected->texture;
      if (old) {
        size_t j=0;
        for (;j<ticket->update_count;++j) if (ticket->updates[j].original==selected) break;
        if (j==ticket->update_count) {
          ticket->updates[j].original=selected;
          if (!qa_render_source_texture_clone(&ticket->updates[j].texture,&selected->texture,error)) return false;
          ++ticket->update_count;
        }
        texture=&ticket->updates[j].texture;
      }
    }
    if (!qa_render_source_texture_upload(texture,row->row.image,row->row.owner,row->row.filter,error)) return false;
    ticket->final_attributes.actual_empty[unit]=true;
    ticket->final_attributes.texture_unit=unit==1?0:unit;
    if (row->row.image->source_after_upload_border)
      ticket->zero_texture.border=ticket->final_attributes.zero_border=row->row.image->source_upload_border;
  }
  ticket->prepared=true; return true;
}
bool qa_cpu_source_images_ready_is(const qa_cpu_source_images_ticket *ticket)
{
  const qa_cpu_renderer *renderer=ticket?ticket->renderer:NULL;
  if (!renderer || !ticket->prepared || ticket->published || !qa_cpu_render_controls_current(&renderer->controls) ||
      renderer->source_image_count!=ticket->original_count || renderer->bound[0]!=ticket->bound[0] ||
      renderer->bound[1]!=ticket->bound[1] || renderer->controls.source_filter!=ticket->filter ||
      memcmp(&renderer->controls.attributes,&ticket->attributes,sizeof(ticket->attributes))) return false;
  for (size_t i=0;i<ticket->bank_count;++i) {
    size_t count=0;
    if (!qa_scene_resource_policy_source_image_count(ticket->banks[i].policy,&count,NULL) || count!=ticket->banks[i].count) return false;
  }
  for (size_t i=0;i<ticket->count;++i) {
    const cpu_source_prepared_image *row=ticket->rows+i;
    const qa_scene_image *image=NULL; uint64_t creation=0;
    if (!qa_scene_resource_policy_source_image_at(row->policy,row->policy_ordinal,&image,&creation,NULL) ||
        image!=row->row.image || creation!=row->creation || image->source_texture_unit!=row->unit ||
        qa_scene_image_resource_owner(image)!=row->destination || qa_scene_resource_policy_source(row->policy)!=row->row.owner) return false;
  }
  return true;
}
void qa_cpu_source_images_publish(qa_cpu_source_images_ticket *ticket)
{
  if (!ticket || !ticket->prepared || ticket->published) return;
  qa_cpu_renderer *renderer=ticket->renderer;
  if (ticket->restart) {
    for (uint32_t i=0;i<renderer->source_image_count;++i) {
      qa_render_source_texture_release(&renderer->source_images[i].texture);
      qa_scene_image_release(renderer->source_images[i].image); qa_scene_resources_destroy(renderer->source_images[i].owner);
      renderer->source_images[i]=(cpu_source_image){0};
    }
    renderer->source_image_count=0;
    renderer->pipeline.blend_source=QA_BLEND_ONE;
    renderer->pipeline.blend_destination=QA_BLEND_ZERO;
    renderer->pipeline.depth_test=QA_DEPTH_DISABLED;
    renderer->pipeline.depth_write=true;
    renderer->pipeline.cull=QA_CULL_NONE;
    renderer->controls.source_cull_type=QA_CULL_FRONT;
    renderer->controls.source_cull_valid=true;
    renderer->pipeline.wireframe=false;
    renderer->clear_depth=1;
    renderer->view=(qa_scene_view){.viewport=renderer->view.viewport,.depth=1};
    renderer->preblend_gamma=renderer->source_frame=false;
  }
  for (size_t i=0;i<ticket->update_count;++i) {
    cpu_source_prepared_update *update=ticket->updates+i;
    qa_render_source_texture_release(&update->original->texture);
    update->original->texture=update->texture; update->original->filter=update->texture.filter;
    qa_render_source_texture_init(&update->texture);
  }
  for (size_t i=0;i<ticket->count;++i) {
    cpu_source_prepared_image *row=ticket->rows+i;
    renderer->source_images[renderer->source_image_count++]=row->row;
    row->row=(cpu_source_image){0};
  }
  for (uint32_t unit=0;unit<2;++unit) {
    qa_scene_image_retain(ticket->final_bound[unit]); qa_scene_image_release(renderer->bound[unit]);
    renderer->bound[unit]=ticket->final_bound[unit];
  }
  qa_render_source_texture_release(&renderer->controls.zero_texture);
  renderer->controls.zero_texture=ticket->zero_texture; qa_render_source_texture_init(&ticket->zero_texture);
  renderer->controls.attributes=ticket->final_attributes;
  ticket->published=true;
}
bool qa_cpu_source_images_finish(qa_cpu_source_images_ticket **out,qa_error *error)
{
  if (!out || !*out) return true;
  qa_cpu_source_images_ticket *ticket=*out;
  if (!ticket->published) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source CPU image finish requires publication"); return false; }
  free(ticket->updates); free(ticket->rows); free(ticket->banks); free(ticket); *out=NULL; return true;
}
bool qa_cpu_source_images_abort(qa_cpu_source_images_ticket **out,qa_error *error)
{
  if (!out || !*out) return true;
  qa_cpu_source_images_ticket *ticket=*out;
  if (ticket->published) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source CPU image abort requires an unpublished child"); return false; }
  for (size_t i=0;i<ticket->count;++i) {
    qa_render_source_texture_release(&ticket->rows[i].row.texture);
    qa_scene_image_release(ticket->rows[i].row.image);
    qa_scene_resources_destroy(ticket->rows[i].row.owner);
  }
  for (size_t i=0;i<ticket->update_count;++i) qa_render_source_texture_release(&ticket->updates[i].texture);
  qa_render_source_texture_release(&ticket->zero_texture);
  free(ticket->updates); free(ticket->rows); free(ticket->banks); free(ticket); *out=NULL; return true;
}
bool qa_cpu_source_texture_bind(qa_render_controls *controls,const qa_scene_image *image,qa_error *error)
{
  if (!qa_cpu_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
      !controls->source.issuing || !cpu_image_valid(image,error)) {
    if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source texture binding lost its actual CPU issue owner");
    return false;
  }
  cpu_source_bind(controls->owner.cpu,image);
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
static bool cpu_settings_idle(const qa_cpu_renderer *renderer,qa_error *error)
{
  if (!cpu_surface_idle(renderer,error)) return false;
  if (!renderer || renderer->executing || renderer->presenting || renderer->capturing ||
      renderer->opacity_active || renderer->opacity_parent) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"CPU settings require their completed idle renderer owner"); return false;
  }
  return true;
}
static bool cpu_surface_prepare(qa_cpu_renderer *renderer,uint32_t width,uint32_t height,float gamma,
    qa_cpu_present present,void *context,bool present_candidate,qa_cpu_surface_ticket **out,qa_error *error)
{
  size_t count=0;
  if (!out || *out || !cpu_settings_idle(renderer,error) || renderer->destroy_pending ||
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

bool qa_cpu_source_overdraw(qa_render_controls *controls,bool enabled,qa_error *error)
{
  qa_cpu_renderer *renderer=controls->owner.cpu;
  if (enabled && renderer->options.stencil_bits<4) {
    qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Source overdraw requires four stencil bits"); return false;
  }
  renderer->overdraw=enabled; return true;
}
bool qa_cpu_source_image_grid(qa_render_controls *controls,int32_t mode,qa_error *error)
{
  qa_cpu_renderer *renderer=controls->owner.cpu;
  if (renderer->current!=&renderer->display || renderer->opacity_active) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source image grid requires its physical display target"); return false;
  }
  qa_scene_rect target={0,0,renderer->display.width,renderer->display.height};
  if (!qa_output_domains_assign(&renderer->output_domains,target,QA_DRAW_BACK,true,target.width,target.height,error)) return false;
  renderer->source_frame=true; renderer->preblend_gamma=false;
  if (!controls->source.projection_2d) {
    qa_render_source_state_bits(&renderer->pipeline,false,true);
    renderer->pipeline.depth_test=QA_DEPTH_DISABLED; renderer->pipeline.cull=QA_CULL_NONE;
    renderer->view.viewport=target; renderer->view.clip_enabled=false; controls->source.projection_2d=true;
    controls->source.picture_milliseconds=controls->frame_values.milliseconds;
  }
  qa_scene_view clear=renderer->view;
  clear.clear_color=renderer->pipeline.color_write; clear.clear_depth=clear.clear_stencil=false;
  clear.color=renderer->clear_color;
  clear_view(renderer,&clear);
  uint64_t start=SDL_GetTicks64();
  qa_scene_frame frame; qa_scene_frame_init(&frame,renderer->options.owner);
  bool ok=true;
  for (uint32_t i=0;ok && i<renderer->source_image_count;++i) {
    const cpu_source_image *entry=renderer->source_images+i;
    float w=(float)(target.width/20),h=(float)(target.height/15);
    float x=(float)(i%20)*w,y=(float)(i/20)*h;
    if (mode==2 && entry->texture.count) {
      w*=(float)entry->texture.levels[0].width/512;
      h*=(float)entry->texture.levels[0].height/512;
    }
    const qa_scene_image *binding=entry->image;
    if (controls->frame_values.no_bind)
      for (uint32_t j=renderer->source_image_count;j>0;--j)
        if (renderer->source_images[j-1].image->source_dlight) { binding=renderer->source_images[j-1].image; break; }
    cpu_source_bind(renderer,binding);
    size_t first=frame.command_count;
    ok=qa_scene_frame_picture_f(&frame,binding,target,(qa_scene_rect_f){x,y,w,h},
      (qa_scene_vec4){0,0,1,1},controls->attributes.color,error);
    for (size_t c=first;ok && c<frame.command_count;++c) {
      if (frame.commands[c].kind!=QA_SCENE_COMMAND_DRAW) continue;
      qa_scene_draw *draw=&frame.commands[c].data.draw;
      draw->source_direct=QA_SOURCE_DIRECT_IMAGE_GRID;
      ok=cpu_draw(renderer,draw,error);
    }
  }
  if (ok && renderer->source_image_count) {
    controls->attributes.coordinates[0]=(qa_scene_vec2){0,1}; controls->attributes.coordinates_known[0]=true;
  }
  qa_scene_frame_destroy(&frame);
  if (ok && controls->source_print) {
    char text[100]; snprintf(text,sizeof(text),"%llu msec to draw all images\n",(unsigned long long)(SDL_GetTicks64()-start));
    controls->source_print(controls->source_print_context,text);
  }
  return ok;
}
