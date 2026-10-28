/* Port of src/app/bootstrap/q2-native-hud.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_native_hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct picture_entry { char *id; qa_picture_asset asset; char *name; } picture_entry;
typedef struct id_entry { char *name; char *id; } id_entry;
struct qa_app_q2_native_hud {
  picture_entry *pictures; size_t picture_count, picture_capacity;
  id_entry *ids; size_t id_count, id_capacity;
  char *content;
  double revision;
  qa_app_native_q2_hud_rendering environment;
  bool has_environment;
  qa_native_q2_hud_table table;
  qa_native_q2_hud_operations prepared;
  bool has_prepared;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_q2_native_hud *qa_app_q2_native_hud_create(void) {
  qa_app_q2_native_hud *hud = calloc(1, sizeof *hud);
  if (hud == NULL) qa_range_error("Out of memory");
  qa_native_q2_hud_table_init(&hud->table);
  return hud;
}
void qa_app_q2_native_hud_clear(qa_app_q2_native_hud *hud) {
  qa_native_q2_hud_table_destroy(&hud->table);
  qa_native_q2_hud_table_init(&hud->table);
  if (hud->has_prepared) { qa_native_q2_hud_operations_free(&hud->prepared); hud->has_prepared = false; }
  hud->has_environment = false;
  hud->revision++;
  free(hud->content); hud->content = NULL;
  for (size_t i = 0; i < hud->id_count; i++) { free(hud->ids[i].name); free(hud->ids[i].id); }
  hud->id_count = 0;
  for (size_t i = 0; i < hud->picture_count; i++) { free(hud->pictures[i].id); free(hud->pictures[i].name); }
  hud->picture_count = 0;
}
void qa_app_q2_native_hud_destroy(qa_app_q2_native_hud *hud) {
  if (hud == NULL) return;
  qa_app_q2_native_hud_clear(hud);
  qa_native_q2_hud_table_destroy(&hud->table);
  free(hud->pictures); free(hud->ids);
  free(hud);
}
const qa_picture_asset *qa_app_q2_native_hud_picture(qa_app_q2_native_hud *hud, qa_resource_id id) {
  for (size_t i = 0; i < hud->picture_count; i++)
    if (strcmp(hud->pictures[i].id, id) == 0) return &hud->pictures[i].asset;
  return NULL;
}
static const char *id_for(qa_app_q2_native_hud *hud, const char *name) {
  for (size_t i = 0; i < hud->id_count; i++)
    if (strcmp(hud->ids[i].name, name) == 0) return hud->ids[i].id;
  return NULL;
}
static void set_picture(qa_app_q2_native_hud *hud, const char *name, const char *id,
    const char *path, qa_renderer_image image) {
  if (hud->id_count == hud->id_capacity) {
    size_t cap = hud->id_capacity == 0 ? 16 : hud->id_capacity * 2;
    id_entry *next = realloc(hud->ids, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    hud->ids = next; hud->id_capacity = cap;
  }
  hud->ids[hud->id_count].name = dup_text(name);
  hud->ids[hud->id_count].id = dup_text(id);
  hud->id_count++;
  if (hud->picture_count == hud->picture_capacity) {
    size_t cap = hud->picture_capacity == 0 ? 16 : hud->picture_capacity * 2;
    picture_entry *next = realloc(hud->pictures, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    hud->pictures = next; hud->picture_capacity = cap;
  }
  picture_entry *entry = &hud->pictures[hud->picture_count++];
  entry->id = dup_text(id);
  entry->name = dup_text(path);
  memset(&entry->asset, 0, sizeof entry->asset);
  entry->asset.kind = QA_PICTURE_IMAGE;
  entry->asset.image.name = entry->name;
  entry->asset.image.image = image;
}
void qa_app_q2_native_hud_prepare(qa_app_q2_native_hud *hud, qa_content_id content,
    const qa_app_q2_hud_assets *assets, const qa_native_q2_hud_frame *frame,
    qa_ui_draw_context context, double scale, const char *mode,
    qa_app_q2_hud_assert_fn assert_current, void *assert_ctx,
    const qa_native_q2_hud_arsenal *arsenal, const qa_app_native_q2_hud_rendering *environment) {
  if (hud->content == NULL || strcmp(hud->content, content) != 0) {
    qa_app_q2_native_hud_clear(hud);
    hud->content = dup_text(content);
  }
  double revision = hud->revision;
  if (environment != NULL) {
    hud->environment = *environment;
    hud->environment.environment.table = &hud->table;
    hud->has_environment = true;
  } else hud->has_environment = false;
  if (assert_current != NULL) assert_current(assert_ctx);
  if (revision != hud->revision) qa_range_error("Native HUD media is retired");
  qa_rect area = context.binding.safe_area;
  qa_native_q2_hud_operations ops = {0};
  qa_native_q2_hud_environment *env = hud->has_environment ? &hud->environment.environment : NULL;
  if (!qa_q2_native_hud_operations(frame, area.width / scale, area.height / scale, NULL, NULL, mode, arsenal, env, &ops))
    qa_range_error("Out of memory");
  if (hud->has_environment) {
    if (hud->has_prepared) qa_native_q2_hud_operations_free(&hud->prepared);
    hud->prepared = ops;
    hud->has_prepared = true;
  }
  /* Collect names: conchars, field_3, num/anum digits + minus, op pics. */
  const char *names[256];
  size_t name_count = 0;
  char number_names[22][24];
  names[name_count++] = "conchars";
  names[name_count++] = "field_3";
  size_t number_at = 0;
  for (size_t p = 0; p < 2; p++) {
    for (int digit = 0; digit < 10; digit++) {
      snprintf(number_names[number_at], 24, "%s_%d", p == 0 ? "num" : "anum", digit);
      names[name_count++] = number_names[number_at++];
    }
    snprintf(number_names[number_at], 24, "%s_minus", p == 0 ? "num" : "anum");
    names[name_count++] = number_names[number_at++];
  }
  for (size_t i = 0; i < ops.count && name_count < 256; i++) {
    if (ops.operations[i].kind == QA_Q2_HUD_OP_PICTURE || ops.operations[i].kind == QA_Q2_HUD_OP_SIZED_PICTURE) {
      bool seen = false;
      for (size_t n = 0; n < name_count; n++) if (strcmp(names[n], ops.operations[i].name) == 0) seen = true;
      if (!seen) names[name_count++] = ops.operations[i].name;
    }
  }
  for (size_t i = 0; i < name_count; i++) {
    const char *name = names[i];
    if (id_for(hud, name) != NULL) continue;
    char path[1024];
    if (name[0] == '/' || name[0] == '\\') snprintf(path, sizeof path, "%s", name + 1);
    else snprintf(path, sizeof path, "pics/%s.pcx", name);
    qa_renderer_image image;
    bool loaded = assets->load_texture(assets->ctx, content, path, &image);
    if (assert_current != NULL) assert_current(assert_ctx);
    if (revision != hud->revision) qa_range_error("Native HUD media is retired");
    if (!loaded && strncmp(path, "players/", 8) == 0)
      loaded = assets->load_texture(assets->ctx, content, "players/male/grunt_i.pcx", &image);
    if (assert_current != NULL) assert_current(assert_ctx);
    if (revision != hud->revision) qa_range_error("Native HUD media is retired");
    if (!loaded) image = assets->missing_image(assets->ctx, content);
    char id[2048];
    snprintf(id, sizeof id, "resource:q2-native-hud:%s/%s", content, path);
    set_picture(hud, name, id, path, image);
  }
  if (!hud->has_environment) qa_native_q2_hud_operations_free(&ops);
}
static void push_command(qa_ui_draw_command **out, size_t *count, size_t *capacity, qa_ui_draw_command command) {
  if (*count == *capacity) {
    *capacity = *capacity == 0 ? 16 : *capacity * 2;
    qa_ui_draw_command *next = realloc(*out, *capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    *out = next;
  }
  (*out)[(*count)++] = command;
}
size_t qa_app_q2_native_hud_commands(qa_app_q2_native_hud *hud, const qa_native_q2_hud_frame *frame,
    qa_ui_draw_context context, double scale, qa_q2_hud_binding_fn binding, void *binding_ctx,
    const char *mode, const qa_native_q2_hud_arsenal *arsenal, qa_ui_draw_command **out) {
  qa_rect area = context.binding.safe_area;
  qa_ui_draw_command *commands = NULL;
  size_t count = 0, capacity = 0;
  qa_ui_draw_command clip = {0};
  clip.kind = QA_UI_DRAW_CLIP; clip.rect = area; clip.has_clip_rect = true;
  push_command(&commands, &count, &capacity, clip);
  const char *font = id_for(hud, "conchars");
  qa_native_q2_hud_operations fresh = {0};
  const qa_native_q2_hud_operations *ops = &hud->prepared;
  if (!hud->has_prepared) {
    qa_native_q2_hud_environment *env = hud->has_environment ? &hud->environment.environment : NULL;
    if (!qa_q2_native_hud_operations(frame, area.width / scale, area.height / scale, binding, binding_ctx, mode, arsenal, env, &fresh))
      qa_range_error("Out of memory");
    ops = &fresh;
  }
  for (size_t i = 0; i < ops->count; i++) {
    const qa_native_q2_hud_operation *op = &ops->operations[i];
    qa_ui_draw_command command = {0};
    if (op->kind == QA_Q2_HUD_OP_FILL) {
      command.kind = QA_UI_DRAW_FILL;
      command.rect.x = area.x + op->x * scale; command.rect.y = area.y + op->y * scale;
      command.rect.width = op->width * scale; command.rect.height = op->height * scale;
      command.color = op->color;
      push_command(&commands, &count, &capacity, command);
    } else if (op->kind == QA_Q2_HUD_OP_FONT_TEXT) {
      if (!hud->has_environment) qa_range_error("Native HUD font was not prepared");
      command.kind = QA_UI_DRAW_TEXT;
      command.origin.x = area.x + op->x * scale; command.origin.y = area.y + op->y * scale;
      command.text = op->text;
      command.font = hud->environment.font;
      command.scale = hud->environment.font_scale * scale;
      command.color = op->alternate ? (qa_vec4){112.0 / 255, 1, 52.0 / 255, 1} : (qa_vec4){1, 1, 1, 1};
      command.align = "left"; command.shadow = true;
      push_command(&commands, &count, &capacity, command);
    } else if (op->kind == QA_Q2_HUD_OP_ARSENAL_PICTURE) {
      double width = 24 * (op->aspect < 1 ? op->aspect : 1);
      double height = 24 / (op->aspect > 1 ? op->aspect : 1);
      command.kind = QA_UI_DRAW_IMAGE;
      command.resource = op->resource;
      command.rect.x = area.x + (op->x + (24 - width) / 2) * scale;
      command.rect.y = area.y + (op->y + (24 - height) / 2) * scale;
      command.rect.width = width * scale; command.rect.height = height * scale;
      command.tex_coords[0].x = 0; command.tex_coords[0].y = 0;
      command.tex_coords[1].x = 1; command.tex_coords[1].y = 1;
      command.color = (qa_vec4){1, 1, 1, 1};
      push_command(&commands, &count, &capacity, command);
    } else if (op->kind == QA_Q2_HUD_OP_PICTURE || op->kind == QA_Q2_HUD_OP_SIZED_PICTURE) {
      const char *id = id_for(hud, op->name);
      const qa_picture_asset *picture = id != NULL ? qa_app_q2_native_hud_picture(hud, id) : NULL;
      if (id == NULL || picture == NULL || picture->kind != QA_PICTURE_IMAGE) continue;
      command.kind = QA_UI_DRAW_IMAGE;
      command.resource = id;
      double x_offset = op->kind == QA_Q2_HUD_OP_PICTURE && op->has_anchor_before
          ? picture->image.image.width + 2 : 0;
      command.rect.x = area.x + (op->x - x_offset) * scale;
      command.rect.y = area.y + op->y * scale;
      command.rect.width = (op->kind == QA_Q2_HUD_OP_SIZED_PICTURE ? op->width : picture->image.image.width) * scale;
      command.rect.height = (op->kind == QA_Q2_HUD_OP_SIZED_PICTURE ? op->height : picture->image.image.height) * scale;
      command.tex_coords[1].x = 1; command.tex_coords[1].y = 1;
      command.color = (qa_vec4){1, 1, 1, 1};
      push_command(&commands, &count, &capacity, command);
    } else if (font != NULL) {
      /* TS text op: per-byte conchars glyphs. TextEncoder bytes are
       * UTF-8; op.text bytes are read as-is (ASCII HUD text). */
      size_t length = strlen(op->text);
      for (size_t index = 0; index < length; index++) {
        unsigned int byte = (unsigned char)op->text[index];
        unsigned int alternate = op->alternate ? 128 : 0;
        unsigned int code = ((op->has_xor && op->xor_) ? (byte ^ alternate) : (byte | alternate)) & 255;
        if ((code & 127) == 32) continue;
        double column = (double)(code & 15), row = (double)(code >> 4);
        qa_ui_draw_command glyph = {0};
        glyph.kind = QA_UI_DRAW_IMAGE;
        glyph.resource = font;
        glyph.rect.x = area.x + (op->x + (double)index * 8) * scale;
        glyph.rect.y = area.y + op->y * scale;
        glyph.rect.width = 8 * scale; glyph.rect.height = 8 * scale;
        glyph.tex_coords[0].x = column / 16; glyph.tex_coords[0].y = row / 16;
        glyph.tex_coords[1].x = (column + 1) / 16; glyph.tex_coords[1].y = (row + 1) / 16;
        glyph.color = (qa_vec4){1, 1, 1, 1};
        if (op->has_shadow && op->shadow) {
          qa_ui_draw_command shadow = glyph;
          shadow.rect.x += scale; shadow.rect.y += scale;
          shadow.color = (qa_vec4){0, 0, 0, 1};
          push_command(&commands, &count, &capacity, shadow);
        }
        push_command(&commands, &count, &capacity, glyph);
      }
    }
  }
  if (!hud->has_prepared) qa_native_q2_hud_operations_free(&fresh);
  qa_ui_draw_command unclip = {0};
  unclip.kind = QA_UI_DRAW_CLIP; unclip.has_clip_rect = false;
  push_command(&commands, &count, &capacity, unclip);
  *out = commands;
  return count;
}
