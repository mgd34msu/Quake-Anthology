#include "internal.h"
#include <limits.h>

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
  free(renderer);
}
bool qa_cpu_resize(qa_cpu_renderer *renderer, uint32_t width, uint32_t height,
                   qa_error *error) {
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
  if (!renderer || !isfinite(gamma) || gamma < 0.5f || gamma > 3) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "CPU gamma must be within 0.5..3");
    return false;
  }
  renderer->gamma_enabled = gamma != 1;
  for (size_t i = 0; i < 256; ++i)
    renderer->gamma[i] =
        gamma == 1 ? (uint8_t)i : cpu_byte(pow((float)i / 255, 1.0f / gamma));
  return true;
}
qa_bytes qa_cpu_pixels(qa_cpu_renderer *renderer) {
  if (!renderer)
    return (qa_bytes){0};
  size_t count = (size_t)renderer->display.width * renderer->display.height;
  if (!renderer->gamma_enabled)
    return (qa_bytes){renderer->display.color, count * 4};
  for (size_t i = 0; i < count; ++i) {
    for (size_t c = 0; c < 3; ++c)
      renderer->output[i * 4 + c] =
          renderer->gamma[renderer->display.color[i * 4 + c]];
    renderer->output[i * 4 + 3] = renderer->display.color[i * 4 + 3];
  }
  return (qa_bytes){renderer->output, count * 4};
}
bool qa_cpu_capture(qa_cpu_renderer *renderer, qa_buffer *out,
                    qa_error *error) {
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
bool qa_cpu_present_frame(qa_cpu_renderer *renderer, qa_error *error) {
  if (!renderer || (renderer->opacity_active && renderer->opacity_value != 1)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Cannot present an active CPU opacity scope");
    return false;
  }
  return !renderer->options.present ||
         renderer->options.present(
             renderer->options.present_context, qa_cpu_pixels(renderer),
             renderer->display.width, renderer->display.height, error);
}
bool qa_cpu_read_depth(const qa_cpu_renderer *renderer, uint32_t x, uint32_t y,
                       float *out, qa_error *error) {
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
bool qa_cpu_execute(qa_cpu_renderer *renderer, const qa_scene_frame *frame,
                    qa_error *error) {
  if (!renderer || !frame || frame->owner != renderer->options.owner ||
      (frame->command_count && !frame->commands) || renderer->opacity_active) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid CPU frame or renderer owner");
    return false;
  }
  /* Versions no longer retained by any scene can release their target storage.
   */
  cpu_target **link = &renderer->targets;
  while (*link) {
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
  for (size_t i = 0; i < frame->command_count; ++i) {
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
      if (!renderer->opacity_skip)
        clear_view(renderer, &renderer->view);
      break;
    case QA_SCENE_COMMAND_DRAW:
      if (!renderer->opacity_skip)
        ok = cpu_draw(renderer, &command->data.draw, error);
      break;
    case QA_SCENE_COMMAND_TARGET:
      ok = select_target(renderer, command->data.target.image, error);
      break;
    case QA_SCENE_COMMAND_IMAGE:
      ok = update_image(renderer, command->data.image, error);
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
        clear.clear_color = true;
        clear.clear_depth = true;
        clear.clear_stencil = false;
        clear.color = (qa_scene_vec4){1, 0, 0.5f, 1};
        clear.depth = 1;
        clear_view(renderer, &clear);
      }
      break;
    case QA_SCENE_COMMAND_SWAP:
      ok = qa_cpu_present_frame(renderer, error);
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
  if (renderer->opacity_active) {
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
