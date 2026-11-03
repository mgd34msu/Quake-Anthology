/* Port of src/ui/common/captions.ts. GPL-2.0-or-later. */
#include "qa/ui/common/captions.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

static bool is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v'
      || ch == '\f';
}

typedef struct lines { char **items; size_t count, capacity; } lines;

static bool push_line(lines *list, char *owned) {
  if (owned == NULL) return false;
  if (list->count == list->capacity) {
    size_t cap = list->capacity == 0 ? 8 : list->capacity * 2;
    char **items = realloc(list->items, cap * sizeof *items);
    if (items == NULL) { free(owned); return false; }
    list->items = items;
    list->capacity = cap;
  }
  list->items[list->count++] = owned;
  return true;
}

static char *dup_range(const char *first, const char *last) {
  size_t length = (size_t)(last - first);
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, first, length);
  out[length] = '\0';
  return out;
}

static char *trim_end_dup(const char *text) {
  size_t length = strlen(text);
  while (length > 0 && is_space(text[length - 1])) length--;
  return dup_range(text, text + length);
}

static bool blank(const char *text) {
  for (; *text != '\0'; text++) if (!is_space(*text)) return false;
  return true;
}

static char *append_str(char *owned, const char *extra) {
  size_t a = strlen(owned), b = strlen(extra);
  char *out = realloc(owned, a + b + 1);
  if (out == NULL) { free(owned); return NULL; }
  memcpy(out + a, extra, b + 1);
  return out;
}

void qa_ui_caption_commands_free(qa_ui_caption_commands *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->owned_count; i++) free(result->owned[i]);
  free(result->owned);
  free(result->commands);
  result->commands = NULL;
  result->owned = NULL;
  result->count = 0;
  result->owned_count = 0;
}

bool qa_ui_get_caption_commands(const qa_active_caption *captions,
                                size_t caption_count, qa_rect area,
                                qa_resource_id font, double scale,
                                qa_ui_measure_text_fn measure, void *ctx,
                                double cap_top, double cap_height,
                                qa_ui_caption_commands *out) {
  memset(out, 0, sizeof *out);
  if (caption_count == 0 || area.width < 16 || area.height < 12) return true;
  const double padding = 4, width = area.width - padding * 2;
  double line_height = ceil(cap_height * scale) + 4;
  if (line_height + padding * 2 > area.height) return true;
  lines list = { NULL, 0, 0 };
  bool ok = true;
  for (size_t ci = 0; ok && ci < caption_count; ci++) {
    const qa_active_caption *caption = &captions[ci];
    /* `${speaker === null ? "" : `${speaker}: `}${text}` */
    size_t need = strlen(caption->localized_text) + 3
      + (caption->localized_speaker != NULL
           ? strlen(caption->localized_speaker) : 0);
    char *text = malloc(need);
    if (text == NULL) { ok = false; break; }
    text[0] = '\0';
    if (caption->localized_speaker != NULL) {
      strcat(text, caption->localized_speaker);
      strcat(text, ": ");
    }
    strcat(text, caption->localized_text);
    /* Paragraphs split on '\n' (JS split keeps empty pieces). */
    const char *paragraph = text;
    for (;;) {
      const char *nl = strchr(paragraph, '\n');
      const char *pend = nl != NULL ? nl : paragraph + strlen(paragraph);
      char *line = dup_range("", "");
      if (line == NULL) ok = false;
      /* JS split(/(\s+)/u): alternating word / separator pieces. */
      const char *p = paragraph;
      while (ok && p <= pend) {
        const char *piece_first = p;
        bool sep = p < pend && is_space(*p);
        if (p < pend) {
          if (sep) while (p < pend && is_space(*p)) p++;
          else while (p < pend && !is_space(*p)) p++;
        }
        char *piece = dup_range(piece_first, p);
        if (piece == NULL) { ok = false; break; }
        char *candidate = dup_range(line, line + strlen(line));
        if (candidate != NULL) candidate = append_str(candidate, piece);
        if (candidate == NULL) { free(piece); ok = false; break; }
        if (measure(ctx, candidate, scale) <= width) {
          free(line);
          line = candidate;
          free(piece);
        } else {
          free(candidate);
          if (!blank(line)) {
            char *trimmed = trim_end_dup(line);
            if (!push_line(&list, trimmed)) ok = false;
            free(line);
            line = dup_range("", "");
            if (line == NULL) { free(piece); ok = false; break; }
          }
          /* for (const character of word.trimStart()) */
          const char *word = piece;
          while (is_space(*word)) word++;
          const unsigned char *ch = (const unsigned char *)word;
          while (ok && *ch != 0) {
            const unsigned char *next = ch + 1;
            while ((*next & 0xc0) == 0x80) next++;
            char *one = dup_range((const char *)ch, (const char *)next);
            if (one == NULL) { ok = false; break; }
            if (line[0] != '\0') {
              char *joined = dup_range(line, line + strlen(line));
              if (joined != NULL) joined = append_str(joined, one);
              if (joined == NULL) { free(one); ok = false; break; }
              if (measure(ctx, joined, scale) > width) {
                if (!push_line(&list, line)) { free(joined); free(one); ok = false; break; }
                line = dup_range("", "");
                if (line == NULL) { free(joined); free(one); ok = false; break; }
              }
              free(joined);
            }
            line = append_str(line, one);
            free(one);
            if (line == NULL) { ok = false; break; }
            ch = next;
          }
          free(piece);
        }
        if (p == pend) break;
      }
      if (ok) {
        char *trimmed = trim_end_dup(line != NULL ? line : "");
        if (!push_line(&list, trimmed)) ok = false;
      }
      free(line);
      if (nl == NULL || !ok) break;
      paragraph = nl + 1;
    }
    free(text);
  }
  if (!ok) {
    for (size_t i = 0; i < list.count; i++) free(list.items[i]);
    free(list.items);
    return false;
  }
  double max_lines = qa_js_max(1, floor((area.height - padding * 2) / line_height));
  /* lines.slice(-maxLines): NaN (slice(-NaN) is slice(0)) and Infinity keep every line. */
  size_t visible_count = isnan(max_lines) || (double)list.count <= max_lines
    ? list.count : (size_t)max_lines;
  size_t first = list.count - visible_count;
  double height = (double)visible_count * line_height + padding * 2;
  double y = area.y + area.height - height;
  out->commands = malloc((visible_count + 1) * sizeof *out->commands);
  if (out->commands == NULL) {
    for (size_t i = 0; i < list.count; i++) free(list.items[i]);
    free(list.items);
    return false;
  }
  qa_ui_draw_command *fill = &out->commands[out->count++];
  memset(fill, 0, sizeof *fill);
  fill->kind = QA_UI_DRAW_FILL;
  fill->rect.x = area.x;
  fill->rect.y = y;
  fill->rect.width = area.width;
  fill->rect.height = height;
  fill->color.x = 0; fill->color.y = 0; fill->color.z = 0; fill->color.w = 0.92;
  for (size_t i = 0; i < visible_count; i++) {
    qa_ui_draw_command *cmd = &out->commands[out->count++];
    memset(cmd, 0, sizeof *cmd);
    cmd->kind = QA_UI_DRAW_TEXT;
    cmd->text = list.items[first + i];
    cmd->font = font;
    cmd->scale = scale;
    cmd->origin.x = area.x + area.width / 2;
    cmd->origin.y = y + padding + (double)i * line_height - cap_top * scale;
    cmd->align = "center";
    cmd->color.x = 1; cmd->color.y = 1; cmd->color.z = 1; cmd->color.w = 1;
    cmd->shadow = true;
  }
  /* Hand every line (visible or not) to the result's owned list;
   * hidden lines stay allocated until free, as the TS array does. */
  out->owned = list.items;
  out->owned_count = list.count;
  return true;
}
