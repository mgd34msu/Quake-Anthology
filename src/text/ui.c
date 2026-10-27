/* Port of src/text/ui.ts. GPL-2.0-or-later. */
#include "qa/text/ui.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

void qa_ui_text_renderer_init(qa_ui_text_renderer *renderer, qa_seat_id seat) {
  memset(renderer, 0, sizeof *renderer);
  renderer->seat = seat;
}

void qa_ui_text_renderer_destroy(qa_ui_text_renderer *renderer) {
  qa_ui_text_renderer_clear(renderer);
  free(renderer->fonts);
  memset(renderer, 0, sizeof *renderer);
}

void qa_ui_text_renderer_clear(qa_ui_text_renderer *renderer) {
  for (size_t i = 0; i < renderer->font_count; i++) {
    free(renderer->fonts[i].resource);
  }
  renderer->font_count = 0;
}

bool qa_ui_text_renderer_bind(qa_ui_text_renderer *renderer,
                              qa_resource_id resource,
                              qa_text_font_selection font) {
  for (size_t i = 0; i < renderer->font_count; i++) {
    if (strcmp(renderer->fonts[i].resource, resource) == 0) {
      renderer->fonts[i].font = font;
      return true;
    }
  }
  if (renderer->font_count == renderer->font_capacity) {
    size_t grown =
        renderer->font_capacity == 0 ? 8 : renderer->font_capacity * 2;
    qa_ui_font_entry *fonts =
        (qa_ui_font_entry *)realloc(renderer->fonts, grown * sizeof *fonts);
    if (fonts == NULL) return false;
    renderer->fonts = fonts;
    renderer->font_capacity = grown;
  }
  char *copy = dup_text(resource);
  if (copy == NULL) return false;
  renderer->fonts[renderer->font_count].resource = copy;
  renderer->fonts[renderer->font_count].font = font;
  renderer->font_count++;
  return true;
}

void qa_ui_text_renderer_release(qa_ui_text_renderer *renderer,
                                 qa_resource_id resource) {
  for (size_t i = 0; i < renderer->font_count; i++) {
    if (strcmp(renderer->fonts[i].resource, resource) != 0) continue;
    free(renderer->fonts[i].resource);
    memmove(&renderer->fonts[i], &renderer->fonts[i + 1],
            (renderer->font_count - i - 1) * sizeof *renderer->fonts);
    renderer->font_count--;
    return;
  }
}

bool qa_ui_text_renderer_draw(qa_ui_text_renderer *renderer,
                              const qa_ui_draw_context *context,
                              const qa_ui_draw_command *command,
                              const qa_draw2d *draw, qa_text_layout *out) {
  if (!qa_same_seat(renderer->seat, context->binding.seat) ||
      !qa_same_seat(renderer->seat, draw->commands.seat)) {
    qa_error("UI text belongs to a different seat");
  }
  const qa_text_font_selection *font = NULL;
  for (size_t i = 0; i < renderer->font_count; i++) {
    if (strcmp(renderer->fonts[i].resource, command->font) == 0) {
      font = &renderer->fonts[i].font;
    }
  }
  if (font == NULL) qa_error("UI font has not been registered for this seat");
  qa_text_layout_options options = {0};
  options.text = command->text != NULL ? command->text : "";
  options.text_length = strlen(options.text);
  options.font = *font;
  options.scale = command->scale;
  options.color = command->color;
  if (!qa_layout_text(&options, out)) return false;
  double offset = command->align != NULL &&
                          strcmp(command->align, "center") == 0
                      ? out->width / 2
                      : command->align != NULL &&
                                strcmp(command->align, "right") == 0
                            ? out->width
                            : 0;
  qa_vec2 origin = {command->origin.x - offset, command->origin.y};
  qa_draw_text_layout(draw, out, origin, command->shadow ? 1 : 0);
  return true;
}
