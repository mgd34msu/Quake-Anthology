/* See include/qa/ui/common/legacy/runtime.h. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/core/commands/text.h"
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/legacy/borders.h"

#define FR(x) ((double)(float)(x))
/* Menu scripts nest through open/close/transition; the original recursed without a bound. */
#define MAX_SCRIPT_DEPTH 64
#define MAX_OPEN_MENUS 16
#define SCROLLBAR_SIZE 16
#define SLIDER_WIDTH 96
#define SLIDER_THUMB_WIDTH 12
#define DOUBLE_CLICK_DELAY 300
#define PULSE_DIVISOR 75
#define BLINK_DIVISOR 200

typedef struct item_state {
  qa_ui_item_definition *def;
} item_state;

typedef struct menu_state {
  qa_ui_menu_definition *def;
  qa_ui_captured_menu handle;
} menu_state;

typedef struct picture_entry {
  char *key;
  bool has;
  qa_picture_asset picture;
} picture_entry;

typedef struct sound_entry {
  char *key;
  bool has;
  qa_pcm_sound sound;
} sound_entry;

typedef struct model_entry {
  char *key;
  qa_q3_pres_scene_model model;
} model_entry;

typedef struct cinematic_entry {
  char *key;
  qa_ui_cinematic_asset asset;
} cinematic_entry;

/* TS cvarScriptTokens entry: one cvar script's tokens, parsed once. A NULL
   token is the TS null (String_Alloc failed). */
typedef struct cvar_script_tokens {
  char *script;
  char **tokens;
  size_t count;
} cvar_script_tokens;

typedef enum capture_kind {
  CAPTURE_IDLE,
  CAPTURE_LIST_AUTO,
  CAPTURE_LIST_THUMB,
  CAPTURE_SLIDER_THUMB
} capture_kind;

typedef struct capture_state {
  capture_kind kind;
  item_state *item;
  int32_t key;
  double x_start;
  double y_start;
} capture_state;

struct qa_ui_runtime {
  qa_ui_runtime_options options;
  qa_ui_menu_definitions definitions;
  menu_state **menus;
  size_t menu_len, menu_capacity;
  int64_t active_menu_count;
  picture_entry *pictures;
  size_t picture_count, picture_capacity;
  sound_entry *sounds;
  size_t sound_count, sound_capacity;
  model_entry *models;
  size_t model_count, model_capacity;
  cinematic_entry *cinematics;
  size_t cinematic_count, cinematic_capacity;
  menu_state **open_stack;
  size_t open_count, open_capacity;
  item_state **item_arena;
  size_t arena_count, arena_capacity;
  /* allocationOffset -> item_state slot map */
  double *slot_offsets;
  item_state **slot_items;
  size_t slot_count, slot_capacity;
  size_t item_allocation_point;
  bool disposed;
  double display_cursor_x, display_cursor_y;
  int32_t real_time;
  int32_t last_list_click_time;
  capture_state capture;
  cvar_script_tokens *cvar_scripts; /* TS cvarScriptTokens Map */
  size_t cvar_script_count, cvar_script_capacity;
  int64_t script_depth;             /* TS scriptDepth */
  int32_t next_scroll_time, next_scroll_adjust_time, scroll_adjust_value;
  bool debug;
  qa_common_parse_state *source_parser;
  qa_common_parse_state owned_parser;
  /* setbackground's shader references: window memory borrows them, so the
   * runtime owns each until a later setbackground replaces it or destroy. */
  qa_ui_shader_reference **backgrounds;
  size_t background_count, background_capacity;
};

typedef qa_ui_runtime runtime_t;

static char *dupstr(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

static char *dupnstr(const char *text, size_t length) {
  char *copy = malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

static void opened(runtime_t *rt) {
  if (rt->disposed) qa_ui_script_plain_error("UI runtime is disposed");
}

static char ascii_fold_char(char c) {
  return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static char *ascii_lower(const char *text) {
  char *out = dupstr(text);
  if (out == NULL) return NULL;
  for (size_t i = 0; out[i] != '\0'; i++) out[i] = ascii_fold_char(out[i]);
  return out;
}

static bool equal_name(const char *left, const char *right) {
  if (left == NULL) return false;
  size_t i = 0;
  for (;;) {
    char a = ascii_fold_char(left[i]);
    char b = ascii_fold_char(right[i]);
    if (a != b) return false;
    if (a == '\0') return true;
    i++;
  }
}

/* Malloc'd resourceKey(path); path NULL is TS null. */
static char *resource_key(const char *path) {
  if (path == NULL) return dupstr("null");
  char *lower = ascii_lower(path);
  size_t length = strlen(lower) + 6;
  char *out = malloc(length);
  if (out != NULL) snprintf(out, length, "name:%s", lower);
  free(lower);
  return out;
}

static qa_rect copy_rect(qa_rect value) {
  qa_rect out = {FR(value.x), FR(value.y), FR(value.width), FR(value.height)};
  return out;
}

static qa_vec4 copy_color(qa_vec4 value) {
  qa_vec4 out = {FR(value.x), FR(value.y), FR(value.z), FR(value.w)};
  return out;
}

static bool rect_contains(qa_rect rect, double x, double y) {
  return x > rect.x && x < FR(rect.x + rect.width) && y > rect.y &&
         y < FR(rect.y + rect.height);
}

static bool is_mouse_key(int32_t key) {
  return key == QA_KEY_MOUSE1 || key == QA_KEY_MOUSE2 ||
         key == QA_KEY_MOUSE3;
}

static bool is_activate_key(int32_t key) {
  return is_mouse_key(key) || key == QA_KEY_ENTER;
}

static int32_t i32_add(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t i32_sub(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a - (uint32_t)b);
}

/* ---- resource maps ---- */

static void pictures_set(runtime_t *rt, const char *key, bool has,
                         const qa_picture_asset *picture) {
  for (size_t i = 0; i < rt->picture_count; i++) {
    if (strcmp(rt->pictures[i].key, key) == 0) {
      rt->pictures[i].has = has;
      if (has) rt->pictures[i].picture = *picture;
      return;
    }
  }
  if (rt->picture_count == rt->picture_capacity) {
    size_t capacity = rt->picture_capacity == 0 ? 16 : rt->picture_capacity * 2;
    picture_entry *grown = realloc(rt->pictures, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->pictures = grown;
    rt->picture_capacity = capacity;
  }
  picture_entry *entry = &rt->pictures[rt->picture_count++];
  entry->key = dupstr(key);
  entry->has = has;
  if (has) entry->picture = *picture;
}

static bool pictures_get(runtime_t *rt, const char *path,
                         qa_picture_asset *out) {
  char *key = resource_key(path);
  bool found = false;
  for (size_t i = 0; i < rt->picture_count; i++) {
    if (strcmp(rt->pictures[i].key, key) == 0) {
      found = rt->pictures[i].has;
      if (found) *out = rt->pictures[i].picture;
      break;
    }
  }
  free(key);
  return found;
}

static void sounds_set(runtime_t *rt, const char *key, bool has,
                       const qa_pcm_sound *sound) {
  for (size_t i = 0; i < rt->sound_count; i++) {
    if (strcmp(rt->sounds[i].key, key) == 0) {
      rt->sounds[i].has = has;
      if (has) rt->sounds[i].sound = *sound;
      return;
    }
  }
  if (rt->sound_count == rt->sound_capacity) {
    size_t capacity = rt->sound_capacity == 0 ? 16 : rt->sound_capacity * 2;
    sound_entry *grown = realloc(rt->sounds, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->sounds = grown;
    rt->sound_capacity = capacity;
  }
  sound_entry *entry = &rt->sounds[rt->sound_count++];
  entry->key = dupstr(key);
  entry->has = has;
  if (has) entry->sound = *sound;
}

static const qa_pcm_sound *sounds_get(runtime_t *rt, const char *path) {
  char *key = resource_key(path);
  const qa_pcm_sound *found = NULL;
  for (size_t i = 0; i < rt->sound_count; i++) {
    if (strcmp(rt->sounds[i].key, key) == 0) {
      if (rt->sounds[i].has) found = &rt->sounds[i].sound;
      break;
    }
  }
  free(key);
  return found;
}

static void models_set(runtime_t *rt, const char *key,
                       const qa_q3_pres_scene_model *model) {
  for (size_t i = 0; i < rt->model_count; i++) {
    if (strcmp(rt->models[i].key, key) == 0) {
      rt->models[i].model = *model;
      return;
    }
  }
  if (rt->model_count == rt->model_capacity) {
    size_t capacity = rt->model_capacity == 0 ? 8 : rt->model_capacity * 2;
    model_entry *grown = realloc(rt->models, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->models = grown;
    rt->model_capacity = capacity;
  }
  model_entry *entry = &rt->models[rt->model_count++];
  entry->key = dupstr(key);
  entry->model = *model;
}

static bool models_get(runtime_t *rt, const char *path,
                       qa_q3_pres_scene_model *out) {
  char *key = resource_key(path);
  bool found = false;
  for (size_t i = 0; i < rt->model_count; i++) {
    if (strcmp(rt->models[i].key, key) == 0) {
      *out = rt->models[i].model;
      found = true;
      break;
    }
  }
  free(key);
  return found;
}

static bool cinematics_get(runtime_t *rt, const char *path,
                           qa_ui_cinematic_asset *out) {
  for (size_t i = 0; i < rt->cinematic_count; i++) {
    if (strcmp(rt->cinematics[i].key, path) == 0) {
      *out = rt->cinematics[i].asset;
      return true;
    }
  }
  return false;
}

static void cinematics_set(runtime_t *rt, const char *path,
                           qa_ui_cinematic_asset asset) {
  for (size_t i = 0; i < rt->cinematic_count; i++) {
    if (strcmp(rt->cinematics[i].key, path) == 0) {
      rt->cinematics[i].asset = asset;
      return;
    }
  }
  if (rt->cinematic_count == rt->cinematic_capacity) {
    size_t capacity =
        rt->cinematic_capacity == 0 ? 8 : rt->cinematic_capacity * 2;
    cinematic_entry *grown =
        realloc(rt->cinematics, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->cinematics = grown;
    rt->cinematic_capacity = capacity;
  }
  cinematic_entry *entry = &rt->cinematics[rt->cinematic_count++];
  entry->key = dupstr(path);
  entry->asset = asset;
}

/* ---- cvars / commands ---- */

static void cvar_set_direct(runtime_t *rt, const char *name,
                            const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(rt->options.cvars, name, value, force, &snapshot))
    qa_cvar_read_free(&snapshot);
}

static void set_cvar(runtime_t *rt, const char *name, const char *value,
                     bool force) {
  char *text = name == NULL ? NULL : qa_source_command_text(name);
  bool invalid = text == NULL || strpbrk(text, "\\\";") != NULL;
  const char *key = invalid ? "BADNAME" : text;
  if (invalid && rt->options.print != NULL) {
    size_t length = strlen(text != NULL ? text : "(null)") + 32;
    char *message = malloc(length);
    if (message != NULL) {
      snprintf(message, length, "invalid cvar name string: %s\n",
               text != NULL ? text : "(null)");
      rt->options.print(rt->options.print_ctx, message);
      free(message);
    }
  }
  if (value == NULL) {
    qa_cvar_snapshot snapshot;
    if (qa_cvar_registry_reset(rt->options.cvars, key, force, &snapshot))
      qa_cvar_read_free(&snapshot);
  } else {
    cvar_set_direct(rt, key, value, force);
  }
  free(text);
}

static double cvar_value(runtime_t *rt, const char *name) {
  if (rt->options.cvar_value != NULL)
    return rt->options.cvar_value(rt->options.cvar_value_ctx, name);
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(rt->options.cvars, name, &snapshot)) return 0;
  double value = snapshot.numeric_value;
  qa_cvar_read_free(&snapshot);
  return value;
}

/* Malloc'd TS cvarBuffer: value truncated to 1023 chars. */
static char *cvar_buffer(runtime_t *rt, const char *name) {
  qa_cvar_snapshot snapshot;
  char *out;
  if (!qa_cvar_registry_get(rt->options.cvars, name, &snapshot))
    return dupstr("");
  size_t length = strlen(snapshot.value);
  if (length > 1023) length = 1023;
  out = dupnstr(snapshot.value, length);
  qa_cvar_read_free(&snapshot);
  return out;
}

static void runtime_print(runtime_t *rt, const char *text) {
  if (rt->options.print != NULL) rt->options.print(rt->options.print_ctx, text);
}

/* Malloc'd gameFormat with the TS default 32000-byte buffer. */
static char *game_format1(const char *format, qa_q3_game_format_arg arg) {
  return qa_q3_game_format(format, &arg, 1, 32000);
}


/* ---- menu/item states ---- */

static item_state *allocate_item(runtime_t *rt, qa_ui_item_definition *def);

static menu_state *make_menu(runtime_t *rt, qa_ui_menu_definition *def) {
  (void)rt;
  menu_state *menu = calloc(1, sizeof *menu);
  menu->def = def;
  menu->handle.definition = def;
  return menu;
}

static void populate_menu(menu_state *menu, qa_ui_menu_definition *def) {
  menu->def = def;
  menu->handle.definition = def;
}

static void push_menu_state(runtime_t *rt, menu_state *menu) {
  if (rt->menu_len == rt->menu_capacity) {
    size_t capacity = rt->menu_capacity == 0 ? 8 : rt->menu_capacity * 2;
    menu_state **grown = realloc(rt->menus, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->menus = grown;
    rt->menu_capacity = capacity;
  }
  rt->menus[rt->menu_len++] = menu;
}

static item_state *arena_push(runtime_t *rt, qa_ui_item_definition *def) {
  if (rt->arena_count == rt->arena_capacity) {
    size_t capacity = rt->arena_capacity == 0 ? 16 : rt->arena_capacity * 2;
    item_state **grown = realloc(rt->item_arena, capacity * sizeof *grown);
    if (grown == NULL) return NULL;
    rt->item_arena = grown;
    rt->arena_capacity = capacity;
  }
  item_state *item = calloc(1, sizeof *item);
  item->def = def;
  rt->item_arena[rt->arena_count++] = item;
  return item;
}

static item_state *allocate_item(runtime_t *rt, qa_ui_item_definition *def) {
  if (def->has_allocation_offset) {
    double offset = def->allocation_offset;
    for (size_t i = 0; i < rt->slot_count; i++) {
      if (rt->slot_offsets[i] == offset) {
        item_state *previous = rt->slot_items[i];
        if (previous->def != def) previous->def = def;
        return previous;
      }
    }
    item_state *item = arena_push(rt, def);
    if (rt->slot_count == rt->slot_capacity) {
      size_t capacity = rt->slot_capacity == 0 ? 16 : rt->slot_capacity * 2;
      rt->slot_offsets = realloc(rt->slot_offsets, capacity * sizeof(double));
      rt->slot_items =
          realloc(rt->slot_items, capacity * sizeof(item_state *));
      rt->slot_capacity = capacity;
    }
    rt->slot_offsets[rt->slot_count] = offset;
    rt->slot_items[rt->slot_count++] = item;
    return item;
  }
  for (size_t i = 0; i < rt->arena_count; i++) {
    if (rt->item_arena[i]->def == def) {
      if (rt->item_allocation_point < i + 1)
        rt->item_allocation_point = i + 1;
      return rt->item_arena[i];
    }
  }
  size_t allocation = rt->item_allocation_point++;
  if (allocation >= rt->arena_count) return arena_push(rt, def);
  item_state *previous = rt->item_arena[allocation];
  previous->def = def;
  return previous;
}

static item_state *menu_item_at(runtime_t *rt, menu_state *menu,
                                int64_t index) {
  qa_ui_item_definition *def = qa_ui_menu_item_at(menu->def, (double)index);
  return def == NULL ? NULL : allocate_item(rt, def);
}

/* TS menuItems(): item or the NULL-dereference error. */
static item_state *menu_item_required(runtime_t *rt, menu_state *menu,
                                      int64_t index) {
  item_state *item = menu_item_at(rt, menu, index);
  if (item == NULL)
    qa_ui_script_plain_error(
        "UI menu item dereference reached a NULL item pointer");
  return item;
}

#define MENU_ITEMS(rt, menu, var, index_var)                              \
  for (int64_t index_var = 0,                                             \
               _count_##index_var = qa_ui_menu_item_count((menu)->def);   \
       index_var < _count_##index_var; index_var++)                       \
    for (item_state *var = menu_item_required((rt), (menu), index_var);   \
         var != NULL; var = NULL)

static menu_state *live_menu_at(runtime_t *rt, int64_t index) {
  if (index < 0 || (size_t)index >= rt->menu_len ||
      rt->menus[index] == NULL)
    qa_ui_script_plain_error(
        "UI menu traversal reached an uninitialized static slot");
  return rt->menus[index];
}

static menu_state *captured_menu(runtime_t *rt, qa_ui_captured_menu *handle) {
  for (size_t i = 0; i < rt->menu_len; i++) {
    if (&rt->menus[i]->handle == handle) return rt->menus[i];
  }
  qa_ui_script_plain_error(
      "UI captured-menu handle does not belong to this runtime");
}

static menu_state *parent_menu(runtime_t *rt, item_state *item) {
  qa_ui_menu_definition *parent = qa_ui_item_parent(item->def);
  if (parent == NULL) return NULL;
  double index = qa_ui_menu_source_index(parent);
  if (index != trunc(index) || index < 0 || index >= (double)rt->menu_len ||
      rt->menus[(size_t)index] == NULL)
    qa_ui_script_plain_error("UI item belongs to a missing menu");
  return rt->menus[(size_t)index];
}

static menu_state *menu_for(runtime_t *rt, item_state *item) {
  menu_state *menu = parent_menu(rt, item);
  if (menu == NULL)
    qa_ui_script_plain_error("UI item dereferences a NULL parent menu");
  return menu;
}

static void set_item_screen_coords(item_state *item, double x, double y) {
  if (qa_ui_window_border(&item->def->window) != 0) {
    double border_size = qa_ui_window_border_size(&item->def->window);
    x = FR(x + border_size);
    y = FR(y + border_size);
  }
  qa_rect client = qa_ui_window_client_rect(&item->def->window);
  qa_rect rect = {FR(x + client.x), FR(y + client.y), client.width,
                  client.height};
  qa_ui_window_set_rect(&item->def->window, rect);
  qa_ui_item_set_text_rect_component(item->def, 2, 0);
  qa_ui_item_set_text_rect_component(item->def, 3, 0);
}

static void update_item_position(item_state *item) {
  qa_ui_menu_definition *parent = qa_ui_item_parent(item->def);
  if (parent == NULL) return;
  qa_rect rect = qa_ui_window_rect(&parent->window);
  double x = rect.x, y = rect.y;
  if (qa_ui_window_border(&parent->window) != 0) {
    double border_size = qa_ui_window_border_size(&parent->window);
    x = FR(x + border_size);
    y = FR(y + border_size);
  }
  set_item_screen_coords(item, x, y);
}

static bool matches_item(item_state *item, const char *name) {
  char *window_name = qa_ui_window_name(&item->def->window);
  bool match = equal_name(window_name, name);
  free(window_name);
  if (match) return true;
  char *group = qa_ui_window_group(&item->def->window);
  match = equal_name(group, name);
  free(group);
  return match;
}

/* ---- RuntimeScriptCursor ---- */

struct qa_ui_script_cursor {
  qa_common_parse_cursor cursor;
  qa_common_parse_state *parser;
  qa_ui_menu_memory_ownership memory;
  int64_t index;
};

static qa_ui_script_cursor *script_cursor_create(
    const char *text, qa_common_parse_state *parser,
    qa_ui_menu_memory_ownership memory) {
  qa_ui_script_cursor *cursor = calloc(1, sizeof *cursor);
  size_t length = strlen(text);
  if (length > 1023) length = 1023;
  qa_common_parse_cursor_init(&cursor->cursor, text, length, false);
  cursor->parser = parser;
  cursor->memory = memory;
  return cursor;
}

static void script_cursor_destroy(qa_ui_script_cursor *cursor) {
  qa_common_parse_cursor_destroy(&cursor->cursor);
  free(cursor);
}

static void lookahead_cursor(qa_ui_script_cursor *cursor,
                             qa_common_parse_cursor *out) {
  qa_common_parse_cursor_init(out, cursor->cursor.source,
                              cursor->cursor.length,
                              cursor->cursor.uninitialized_end);
  if (cursor->cursor.has_offset)
    qa_common_parse_cursor_set_offset(out, cursor->cursor.offset);
  else
    qa_common_parse_cursor_set_null(out);
}

double qa_ui_script_cursor_position(qa_ui_script_cursor *cursor) {
  return (double)cursor->index;
}

double qa_ui_script_cursor_remaining(qa_ui_script_cursor *cursor) {
  qa_common_parse_cursor lookahead;
  lookahead_cursor(cursor, &lookahead);
  qa_common_parse_state parser;
  qa_common_parse_state_init(&parser);
  int64_t count = 0;
  while (qa_common_parse_state_parse(&parser, &lookahead, false)[0] != '\0')
    count++;
  qa_common_parse_cursor_destroy(&lookahead);
  return (double)count;
}

static void script_token_out(qa_ui_script_cursor *cursor, const char *text,
                             qa_ui_menu_script_token *out) {
  out->text = dupstr(text);
  out->location.path = dupstr("<UI script>");
  out->location.line = cursor->parser->line;
  out->location.column = 1;
}

bool qa_ui_script_cursor_peek(qa_ui_script_cursor *cursor,
                              qa_ui_menu_script_token *out) {
  qa_common_parse_cursor lookahead;
  lookahead_cursor(cursor, &lookahead);
  qa_common_parse_state parser;
  qa_common_parse_state_init(&parser);
  const char *text = qa_common_parse_state_parse(&parser, &lookahead, false);
  bool found = text[0] != '\0';
  if (found) script_token_out(cursor, text, out);
  qa_common_parse_cursor_destroy(&lookahead);
  return found;
}

bool qa_ui_script_cursor_next(qa_ui_script_cursor *cursor,
                              qa_ui_menu_script_token *out) {
  const char *text =
      qa_common_parse_state_parse(cursor->parser, &cursor->cursor, false);
  if (text[0] == '\0') return false;
  cursor->index++;
  script_token_out(cursor, text, out);
  return true;
}

/* Malloc'd TS rawString; NULL is TS undefined. */
static char *script_cursor_raw_string(qa_ui_script_cursor *cursor) {
  qa_ui_menu_script_token token;
  if (!qa_ui_script_cursor_next(cursor, &token)) return NULL;
  if (token.text[0] == '\0') {
    qa_ui_menu_script_token_destroy(&token);
    return NULL;
  }
  char *out = token.text;
  token.text = NULL;
  qa_ui_menu_script_token_destroy(&token);
  return out;
}

qa_ui_script_string_kind qa_ui_script_cursor_string(
    qa_ui_script_cursor *cursor, char **out) {
  char *text = script_cursor_raw_string(cursor);
  *out = NULL;
  if (text == NULL) return QA_UI_SCRIPT_STRING_NONE;
  if (cursor->memory.kind == QA_UI_MENU_MEMORY_UNACCOUNTED) {
    *out = text;
    return QA_UI_SCRIPT_STRING_TEXT;
  }
  char *allocated =
      qa_ui_team_arena_memory_string_alloc(cursor->memory.memory, text);
  free(text);
  if (allocated == NULL) return QA_UI_SCRIPT_STRING_NULL;
  *out = allocated;
  return QA_UI_SCRIPT_STRING_TEXT;
}

/* Cursor string for internal commands (keeps the null channel). */
typedef struct cursor_string {
  qa_ui_script_string_kind kind;
  char *text;
} cursor_string;

static cursor_string cursor_string_read(qa_ui_script_cursor *cursor) {
  cursor_string out;
  out.kind = qa_ui_script_cursor_string(cursor, &out.text);
  return out;
}

static qa_rect parsed_rect(qa_ui_script_cursor *cursor, bool *ok) {
  qa_rect out = {0, 0, 0, 0};
  char *parts[4] = {NULL, NULL, NULL, NULL};
  *ok = false;
  for (int i = 0; i < 4; i++) {
    parts[i] = script_cursor_raw_string(cursor);
    if (parts[i] == NULL) goto done;
  }
  out.x = qa_game_atof(parts[0]);
  out.y = qa_game_atof(parts[1]);
  out.width = qa_game_atof(parts[2]);
  out.height = qa_game_atof(parts[3]);
  *ok = true;
done:
  for (int i = 0; i < 4; i++) free(parts[i]);
  return out;
}

/* ---- forward declarations ---- */

static void run_script(runtime_t *rt, menu_state *menu, item_state *item,
                       qa_ui_menu_script *script);
static bool handle_list_key(runtime_t *rt, item_state *item, int32_t key,
                            bool force);
static void paint_menu(runtime_t *rt, menu_state *menu,
                       const qa_draw2d *draw, bool force);
static bool item_passes_cvar(runtime_t *rt, item_state *item, int purpose);
static menu_state *focused_menu(runtime_t *rt);
static void close_cinematics(runtime_t *rt);

/* ---- source handles / window pictures ---- */

typedef struct handle_ctx {
  runtime_t *runtime;
  double handle;
} handle_ctx;

static const qa_ui_runtime_handles *source_handles(runtime_t *rt) {
  if (rt->options.resources.handles.kind == QA_UI_HANDLES_DIAGNOSTIC)
    qa_ui_script_plain_error(
        "Numeric UI resource use requires the actual source handle owners");
  return &rt->options.resources.handles;
}

static qa_picture_asset produce_handle_picture(void *ctx) {
  handle_ctx *storage = ctx;
  qa_picture_asset out;
  const qa_ui_runtime_handles *handles = source_handles(storage->runtime);
  if (handles->picture_for_handle(handles->ctx, storage->handle, &out))
    return out;
  return storage->runtime->options.zero_picture;
}

/* TS windowState.backgroundPath: undefined unless no numeric handle. */
static bool window_background_path(qa_ui_window_definition *window,
                                   const char **out_path,
                                   qa_ui_shader_reference **out_ref) {
  int32_t handle;
  if (qa_ui_window_background_handle(window, &handle)) return false;
  qa_ui_shader_reference *background = qa_ui_window_background(window);
  *out_ref = background;
  *out_path = background == NULL ? NULL : background->path;
  return background != NULL;
}

static bool window_picture(runtime_t *rt, qa_ui_window_definition *window,
                           qa_picture_asset *out) {
  int32_t handle;
  if (qa_ui_window_background_handle(window, &handle)) {
    if (handle == 0) return false;
    const qa_ui_runtime_handles *handles = source_handles(rt);
    return handles->picture_for_handle(handles->ctx, handle, out);
  }
  const char *path;
  qa_ui_shader_reference *ref;
  if (!window_background_path(window, &path, &ref)) return false;
  return pictures_get(rt, path, out);
}

static bool has_window_background(runtime_t *rt,
                                  qa_ui_window_definition *window) {
  int32_t handle;
  if (qa_ui_window_background_handle(window, &handle)) return handle != 0;
  qa_picture_asset picture;
  return window_picture(rt, window, &picture);
}

static qa_ui_picture_or_lazy window_background(
    runtime_t *rt, qa_ui_window_definition *window, handle_ctx *storage) {
  qa_ui_picture_or_lazy out;
  memset(&out, 0, sizeof out);
  int32_t handle;
  bool has_handle = qa_ui_window_background_handle(window, &handle);
  if (!has_handle || handle == 0) {
    qa_picture_asset picture;
    if (window_picture(rt, window, &picture)) {
      out.kind = QA_UI_PICTURE_VALUE;
      out.picture = picture;
    }
    return out;
  }
  storage->runtime = rt;
  storage->handle = handle;
  out.kind = QA_UI_PICTURE_LAZY;
  out.produce = produce_handle_picture;
  out.produce_ctx = storage;
  return out;
}

static qa_ui_picture_or_lazy background_or_zero(runtime_t *rt,
                                                qa_ui_window_definition *window,
                                                handle_ctx *storage) {
  qa_ui_picture_or_lazy out = window_background(rt, window, storage);
  if (out.kind == QA_UI_PICTURE_NONE) {
    out.kind = QA_UI_PICTURE_VALUE;
    out.picture = rt->options.zero_picture;
  }
  return out;
}

static qa_ui_picture_or_lazy widget_picture(runtime_t *rt,
                                            qa_picture_asset picture,
                                            handle_ctx *storage) {
  qa_ui_picture_or_lazy out;
  memset(&out, 0, sizeof out);
  const qa_ui_runtime_handles *handles = &rt->options.resources.handles;
  if (handles->kind == QA_UI_HANDLES_DIAGNOSTIC) {
    out.kind = QA_UI_PICTURE_VALUE;
    out.picture = picture;
    return out;
  }
  storage->runtime = rt;
  storage->handle = handles->picture_handle(handles->ctx, &picture);
  out.kind = QA_UI_PICTURE_LAZY;
  out.produce = produce_handle_picture;
  out.produce_ctx = storage;
  return out;
}

static void draw_pic(const qa_draw2d *draw, qa_rect rect,
                     qa_ui_picture_or_lazy picture) {
  if (picture.kind == QA_UI_PICTURE_LAZY)
    qa_draw2d_draw_handle_pic_lazy(draw, rect, picture.produce,
                                   picture.produce_ctx);
  else
    qa_draw2d_draw_handle_pic(draw, rect, &picture.picture);
}

/* ---- registration / cinematics caching ---- */

static void resolve_registration(runtime_t *rt, bool completed,
                                 const qa_ui_menu_registration_event *event) {
  const qa_ui_runtime_resources *resources = &rt->options.resources;
  const char *path = event->reference->path;
  char *key = resource_key(path);
  switch (event->kind) {
    case QA_UI_RESOURCE_FONT:
      if (!completed)
        resources->register_font(resources->ctx, path,
                                 event->reference->point_size);
      break;
    case QA_UI_RESOURCE_SHADER: {
      qa_picture_asset picture;
      bool has = completed
                     ? resources->registered_picture(resources->ctx, path,
                                                     &picture)
                     : resources->register_picture(resources->ctx, path,
                                                   &picture);
      pictures_set(rt, key, has, &picture);
      break;
    }
    case QA_UI_RESOURCE_SOUND: {
      qa_pcm_sound sound;
      bool has = completed
                     ? resources->registered_sound(resources->ctx, path,
                                                   &sound)
                     : resources->register_sound(resources->ctx, path, &sound);
      sounds_set(rt, key, has, &sound);
      break;
    }
    case QA_UI_RESOURCE_MODEL: {
      qa_q3_pres_scene_model model;
      bool has = true;
      if (completed) {
        has = resources->registered_model(resources->ctx, path, &model);
      } else {
        model = resources->register_model(resources->ctx, path);
      }
      if (!has)
        qa_ui_script_plain_error(
            "Completed UI model registration is missing %s",
            path != NULL ? path : "null");
      models_set(rt, key, &model);
      break;
    }
  }
  free(key);
}

static void cache_window_cinematic(runtime_t *rt,
                                   qa_ui_window_definition *window) {
  char *cinematic = qa_ui_window_cinematic(window);
  if (cinematic == NULL) return;
  qa_ui_cinematic_asset asset =
      rt->options.resources.prepare_cinematic(rt->options.resources.ctx,
                                              cinematic);
  cinematics_set(rt, cinematic, asset);
  free(cinematic);
}

static qa_ui_cinematic_asset cinematic_asset(runtime_t *rt, const char *path) {
  qa_ui_cinematic_asset asset;
  if (cinematics_get(rt, path, &asset)) return asset;
  asset = rt->options.resources.prepare_cinematic(rt->options.resources.ctx,
                                                  path);
  opened(rt);
  cinematics_set(rt, path, asset);
  return asset;
}

/* ---- creation ---- */

qa_ui_runtime *qa_ui_runtime_create(const qa_ui_runtime_options *options) {
  runtime_t *rt = calloc(1, sizeof *rt);
  if (rt == NULL) return NULL;
  rt->options = *options;
  rt->definitions = options->definitions;
  qa_common_parse_state_init(&rt->owned_parser);
  rt->source_parser =
      options->source_parser != NULL ? options->source_parser
                                     : &rt->owned_parser;
  rt->capture.kind = CAPTURE_IDLE;
  bool completed =
      options->definitions.registration_kind == QA_UI_REGISTRATION_COMPLETED;
  for (size_t i = 0; i < options->definitions.registration_event_count; i++)
    resolve_registration(rt, completed,
                         &options->definitions.registration_events[i]);
  for (size_t i = 0; i < options->definitions.menu_count; i++) {
    qa_ui_menu_definition *def = options->definitions.menus[i];
    cache_window_cinematic(rt, &def->window);
    int32_t item_count = qa_ui_menu_item_count(def);
    if (item_count > QA_MAX_UI_MENU_ITEMS)
      qa_range_error("UI menu item count exceeds the source array");
    for (int32_t j = 0; j < item_count; j++) {
      qa_ui_item_definition *item = qa_ui_menu_item_at(def, j);
      if (item == NULL)
        qa_ui_script_plain_error("UI menu dereferences a NULL item pointer");
      cache_window_cinematic(rt, &item->window);
    }
  }
  for (size_t i = 0; i < options->definitions.menu_count; i++)
    push_menu_state(rt, make_menu(rt, options->definitions.menus[i]));
  rt->active_menu_count = (int64_t)rt->menu_len;
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    int32_t item_count = qa_ui_menu_item_count(menu->def);
    for (int32_t j = 0; j < item_count; j++) (void)menu_item_at(rt, menu, j);
  }
  return rt;
}

void qa_ui_runtime_destroy(qa_ui_runtime *rt) {
  if (rt == NULL) return;
  for (size_t i = 0; i < rt->menu_len; i++) free(rt->menus[i]);
  free(rt->menus);
  for (size_t i = 0; i < rt->arena_count; i++) free(rt->item_arena[i]);
  free(rt->item_arena);
  free(rt->slot_offsets);
  free(rt->slot_items);
  free(rt->open_stack);
  for (size_t i = 0; i < rt->cvar_script_count; i++) {
    for (size_t j = 0; j < rt->cvar_scripts[i].count; j++)
      free(rt->cvar_scripts[i].tokens[j]);
    free(rt->cvar_scripts[i].tokens);
    free(rt->cvar_scripts[i].script);
  }
  free(rt->cvar_scripts);
  for (size_t i = 0; i < rt->picture_count; i++) free(rt->pictures[i].key);
  free(rt->pictures);
  for (size_t i = 0; i < rt->sound_count; i++) free(rt->sounds[i].key);
  free(rt->sounds);
  for (size_t i = 0; i < rt->model_count; i++) free(rt->models[i].key);
  free(rt->models);
  for (size_t i = 0; i < rt->cinematic_count; i++)
    free(rt->cinematics[i].key);
  free(rt->cinematics);
  for (size_t i = 0; i < rt->background_count; i++)
    qa_ui_resource_reference_destroy(rt->backgrounds[i]);
  free(rt->backgrounds);
  free(rt);
}

void qa_ui_runtime_retire(qa_ui_runtime *rt) {
  rt->disposed = true;
  rt->capture.kind = CAPTURE_IDLE;
}

void qa_ui_runtime_dispose(qa_ui_runtime *rt) {
  if (rt->disposed) return;
  close_cinematics(rt);
  qa_ui_runtime_retire(rt);
}

/* ---- definitions management ---- */

void qa_ui_runtime_reset_definitions(qa_ui_runtime *rt,
                                     qa_ui_runtime_reset reset) {
  opened(rt);
  rt->active_menu_count = 0;
  if (reset == QA_UI_RUNTIME_RESET_STRINGS) {
    rt->item_allocation_point = 0;
    rt->open_count = 0;
  }
}

void qa_ui_runtime_accept_menu_registration(
    qa_ui_runtime *rt, const qa_ui_menu_registration_event *event) {
  opened(rt);
  const qa_ui_runtime_resources *resources = &rt->options.resources;
  const char *path = event->reference->path;
  char *key = resource_key(path);
  switch (event->kind) {
    case QA_UI_RESOURCE_FONT:
      break;
    case QA_UI_RESOURCE_SHADER: {
      qa_picture_asset picture;
      bool has =
          resources->registered_picture(resources->ctx, path, &picture);
      pictures_set(rt, key, has, &picture);
      break;
    }
    case QA_UI_RESOURCE_SOUND: {
      qa_pcm_sound sound;
      bool has = resources->registered_sound(resources->ctx, path, &sound);
      sounds_set(rt, key, has, &sound);
      break;
    }
    case QA_UI_RESOURCE_MODEL: {
      qa_q3_pres_scene_model model;
      if (!resources->registered_model(resources->ctx, path, &model))
        qa_ui_script_plain_error(
            "Completed UI model registration is missing %s",
            path != NULL ? path : "null");
      models_set(rt, key, &model);
      break;
    }
  }
  free(key);
}

void qa_ui_runtime_publish_menu_asset(
    qa_ui_runtime *rt, const qa_ui_menu_asset_publication *event) {
  opened(rt);
  if (event->field == QA_UI_ASSET_CURSOR_STR) return;
  if (event->field == QA_UI_ASSET_FONT_REGISTERED) {
    rt->definitions.font_registered = event->boolean;
    return;
  }
  qa_ui_global_assets *assets = &rt->definitions.assets;
  switch (event->field) {
    case QA_UI_ASSET_TEXT_FONT: assets->text_font = event->reference; break;
    case QA_UI_ASSET_SMALL_FONT: assets->small_font = event->reference; break;
    case QA_UI_ASSET_BIG_FONT: assets->big_font = event->reference; break;
    case QA_UI_ASSET_CURSOR: assets->cursor = event->reference; break;
    case QA_UI_ASSET_GRADIENT_BAR:
      assets->gradient_bar = event->reference;
      break;
    case QA_UI_ASSET_MENU_ENTER_SOUND:
      assets->menu_enter_sound = event->reference;
      break;
    case QA_UI_ASSET_MENU_EXIT_SOUND:
      assets->menu_exit_sound = event->reference;
      break;
    case QA_UI_ASSET_MENU_BUZZ_SOUND:
      assets->menu_buzz_sound = event->reference;
      break;
    case QA_UI_ASSET_ITEM_FOCUS_SOUND:
      assets->item_focus_sound = event->reference;
      break;
    case QA_UI_ASSET_FADE_CLAMP: assets->fade_clamp = event->number; break;
    case QA_UI_ASSET_FADE_CYCLE: assets->fade_cycle = event->number; break;
    case QA_UI_ASSET_FADE_AMOUNT: assets->fade_amount = event->number; break;
    case QA_UI_ASSET_SHADOW_X: assets->shadow_x = event->number; break;
    case QA_UI_ASSET_SHADOW_Y: assets->shadow_y = event->number; break;
    case QA_UI_ASSET_SHADOW_FADE_CLAMP:
      assets->shadow_fade_clamp = event->number;
      break;
    case QA_UI_ASSET_SHADOW_COLOR_COMPONENT:
      if (event->component == 0) assets->shadow_color.x = event->number;
      else if (event->component == 1) assets->shadow_color.y = event->number;
      else if (event->component == 2) assets->shadow_color.z = event->number;
      else assets->shadow_color.w = event->number;
      break;
    case QA_UI_ASSET_FONT_REGISTERED:
    case QA_UI_ASSET_CURSOR_STR:
      break;
  }
}

void qa_ui_runtime_assert_menu_memory(
    qa_ui_runtime *rt, qa_ui_menu_memory_ownership next_memory) {
  opened(rt);
  qa_ui_menu_memory_ownership previous = rt->definitions.memory;
  if (previous.kind != next_memory.kind ||
      (previous.kind == QA_UI_MENU_MEMORY_QVM32 &&
       next_memory.kind == QA_UI_MENU_MEMORY_QVM32 &&
       previous.memory != next_memory.memory))
    qa_ui_script_plain_error(
        "UI reload cannot replace its source memory owner");
}

static void sync_definitions_menus(runtime_t *rt) {
  /* TS rebuilds definitions.menus from the active menu states. */
  qa_ui_menu_definition **menus =
      malloc((size_t)(rt->active_menu_count > 0 ? rt->active_menu_count : 1) *
             sizeof *menus);
  for (int64_t i = 0; i < rt->active_menu_count; i++)
    menus[i] = rt->menus[i]->def;
  rt->definitions.menus = menus;
  rt->definitions.menu_count = (size_t)rt->active_menu_count;
}

void qa_ui_runtime_append_menu(qa_ui_runtime *rt,
                               qa_ui_menu_definition *definition,
                               qa_ui_menu_memory_ownership memory) {
  opened(rt);
  qa_ui_runtime_assert_menu_memory(rt, memory);
  if (rt->active_menu_count >= QA_MAX_UI_MENUS)
    qa_range_error(
        "UI menu publication exceeds the source static menu array");
  int64_t index = rt->active_menu_count;
  cache_window_cinematic(rt, &definition->window);
  opened(rt);
  int32_t item_count = qa_ui_menu_item_count(definition);
  if (item_count > QA_MAX_UI_MENU_ITEMS)
    qa_range_error("UI menu item count exceeds the source array");
  for (int32_t i = 0; i < item_count; i++) {
    qa_ui_item_definition *item = qa_ui_menu_item_at(definition, i);
    if (item == NULL)
      qa_ui_script_plain_error("UI menu dereferences a NULL item pointer");
    cache_window_cinematic(rt, &item->window);
    opened(rt);
  }
  if (rt->active_menu_count != index)
    qa_ui_script_plain_error(
        "UI menu publication was interrupted by another menu load");
  menu_state *menu;
  if ((size_t)index >= rt->menu_len) {
    menu = make_menu(rt, definition);
    push_menu_state(rt, menu);
  } else {
    menu = rt->menus[index];
    populate_menu(menu, definition);
  }
  if (memory.kind == QA_UI_MENU_MEMORY_UNACCOUNTED) {
    for (int32_t i = 0; i < item_count; i++) {
      qa_ui_item_definition *item = qa_ui_menu_item_at(definition, i);
      if (item == NULL)
        qa_ui_script_plain_error("UI menu dereferences a NULL item pointer");
      (void)allocate_item(rt, item);
    }
  }
  rt->active_menu_count++;
  sync_definitions_menus(rt);
}

void qa_ui_runtime_set_fonts(qa_ui_runtime *rt, const qa_q3_font_set *fonts) {
  rt->options.fonts = *fonts;
}

void qa_ui_runtime_set_widget_assets(qa_ui_runtime *rt,
                                     const qa_ui_widget_assets *assets) {
  rt->options.widget_assets = *assets;
}

void qa_ui_runtime_reload_definitions(
    qa_ui_runtime *rt, const qa_ui_menu_definitions *definitions) {
  opened(rt);
  qa_ui_runtime_assert_menu_memory(rt, definitions->memory);
  bool completed =
      definitions->registration_kind == QA_UI_REGISTRATION_COMPLETED;
  for (size_t i = 0; i < definitions->registration_event_count; i++)
    resolve_registration(rt, completed, &definitions->registration_events[i]);
  for (size_t i = 0; i < definitions->menu_count; i++) {
    qa_ui_menu_definition *def = definitions->menus[i];
    cache_window_cinematic(rt, &def->window);
    int32_t item_count = qa_ui_menu_item_count(def);
    if (item_count > QA_MAX_UI_MENU_ITEMS)
      qa_range_error("UI menu item count exceeds the source array");
    for (int32_t j = 0; j < item_count; j++) {
      qa_ui_item_definition *item = qa_ui_menu_item_at(def, j);
      if (item == NULL)
        qa_ui_script_plain_error("UI menu dereferences a NULL item pointer");
      cache_window_cinematic(rt, &item->window);
    }
  }
  rt->definitions = *definitions;
  for (size_t i = 0; i < definitions->menu_count; i++) {
    qa_ui_menu_definition *def = definitions->menus[i];
    if (def == NULL) continue;
    if (i >= rt->menu_len) {
      push_menu_state(rt, make_menu(rt, def));
    } else {
      populate_menu(rt->menus[i], def);
    }
    if (definitions->memory.kind == QA_UI_MENU_MEMORY_UNACCOUNTED) {
      int32_t item_count = qa_ui_menu_item_count(def);
      for (int32_t j = 0; j < item_count; j++) {
        qa_ui_item_definition *item = qa_ui_menu_item_at(def, j);
        if (item == NULL)
          qa_ui_script_plain_error(
              "UI menu dereferences a NULL item pointer");
        (void)allocate_item(rt, item);
      }
    }
  }
  rt->active_menu_count = (int64_t)definitions->menu_count;
}

/* ---- menu activation ---- */

static menu_state *find_menu(runtime_t *rt, const char *name) {
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    char *menu_name = qa_ui_window_name(&menu->def->window);
    bool match = equal_name(menu_name, name);
    free(menu_name);
    if (match) return menu;
  }
  return NULL;
}

static menu_state *focused_menu(runtime_t *rt) {
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    int32_t flags = qa_ui_window_flags(&menu->def->window);
    if ((flags & QA_UI_WINDOW_HAS_FOCUS) != 0 &&
        (flags & QA_UI_WINDOW_VISIBLE) != 0)
      return menu;
  }
  return NULL;
}

static void close_window_cinematic(runtime_t *rt,
                                   qa_ui_window_definition *window) {
  int32_t handle = qa_ui_window_cinematic_handle(window);
  if (handle >= 0) {
    rt->options.cinematics.stop(rt->options.cinematics.ctx, handle);
    qa_ui_window_set_cinematic_handle(window, -1);
  }
}

static void close_cinematics(runtime_t *rt) {
  for (int64_t mi = 0; mi < rt->active_menu_count; mi++) {
    menu_state *menu = live_menu_at(rt, mi);
    if (qa_ui_window_style(&menu->def->window) == 5)
      close_window_cinematic(rt, &menu->def->window);
    MENU_ITEMS(rt, menu, item, ii) {
      if (qa_ui_window_style(&item->def->window) == 5)
        close_window_cinematic(rt, &item->def->window);
      if (qa_ui_item_behavior(item->def) == QA_UI_BEHAVIOR_OWNER_DRAW)
        rt->options.owner_draw.close_cinematic(
            rt->options.owner_draw.ctx,
            -(double)qa_ui_window_owner_draw(&item->def->window));
    }
  }
}

static void start_background(runtime_t *rt, const char *path) {
  rt->options.audio.start_background(rt->options.audio.ctx, path);
  opened(rt);
}

static void play_local_sound(runtime_t *rt, const qa_pcm_sound *sound) {
  qa_ui_runtime_sound_arg arg;
  memset(&arg, 0, sizeof arg);
  if (sound == NULL) {
    arg.kind = QA_UI_SOUND_ARG_NONE;
  } else {
    arg.kind = QA_UI_SOUND_ARG_PCM;
    arg.sound = sound;
  }
  rt->options.audio.play_local(rt->options.audio.ctx, arg);
}

static void play_local_handle(runtime_t *rt, double handle) {
  qa_ui_runtime_sound_arg arg;
  memset(&arg, 0, sizeof arg);
  arg.kind = QA_UI_SOUND_ARG_HANDLE;
  arg.handle = handle;
  rt->options.audio.play_local(rt->options.audio.ctx, arg);
}

static void activate_menu(runtime_t *rt, menu_state *menu) {
  qa_ui_window_set_flags(&menu->def->window,
                         qa_ui_window_flags(&menu->def->window) |
                             QA_UI_WINDOW_HAS_FOCUS | QA_UI_WINDOW_VISIBLE);
  qa_ui_menu_script *on_open = qa_ui_menu_on_open(menu->def);
  if (on_open != NULL) run_script(rt, menu, NULL, on_open);
  char *loop = qa_ui_menu_sound_loop_path(menu->def);
  /* soundLoop is defined iff its stored path string is defined. */
  if (loop != NULL) start_background(rt, loop);
  free(loop);
  close_cinematics(rt);
}

static void close_menu(runtime_t *rt, menu_state *menu) {
  qa_ui_menu_script *on_close = qa_ui_menu_on_close(menu->def);
  if ((qa_ui_window_flags(&menu->def->window) & QA_UI_WINDOW_VISIBLE) != 0 &&
      on_close != NULL)
    run_script(rt, menu, NULL, on_close);
  qa_ui_window_set_flags(
      &menu->def->window,
      qa_ui_window_flags(&menu->def->window) &
          ~(QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_HAS_FOCUS));
}

static void open_stack_push(runtime_t *rt, menu_state *menu) {
  if (rt->open_count == rt->open_capacity) {
    size_t capacity = rt->open_capacity == 0 ? 8 : rt->open_capacity * 2;
    menu_state **grown = realloc(rt->open_stack, capacity * sizeof *grown);
    if (grown == NULL) return;
    rt->open_stack = grown;
    rt->open_capacity = capacity;
  }
  rt->open_stack[rt->open_count++] = menu;
}

static bool activate_impl(runtime_t *rt, const char *name,
                          qa_ui_runtime_name_fn name_fn, void *name_ctx,
                          bool is_null) {
  opened(rt);
  menu_state *focus = focused_menu(rt);
  menu_state *activated = NULL;
  for (int64_t mi = 0; mi < rt->active_menu_count; mi++) {
    menu_state *menu = live_menu_at(rt, mi);
    char *menu_name = qa_ui_window_name(&menu->def->window);
    const char *requested = NULL;
    char *requested_copy = NULL;
    if (menu_name != NULL && !is_null) {
      requested = name_fn != NULL ? name_fn(name_ctx) : name;
      requested_copy = dupstr(requested);
      requested = requested_copy;
    }
    if (requested != NULL && equal_name(menu_name, requested)) {
      activated = menu;
      activate_menu(rt, menu);
      if (focus != NULL && rt->open_count < MAX_OPEN_MENUS)
        open_stack_push(rt, focus);
    } else {
      qa_ui_window_set_flags(&menu->def->window,
                             qa_ui_window_flags(&menu->def->window) &
                                 ~QA_UI_WINDOW_HAS_FOCUS);
    }
    free(requested_copy);
    free(menu_name);
  }
  close_cinematics(rt);
  return activated != NULL;
}

bool qa_ui_runtime_activate(qa_ui_runtime *rt, const char *name) {
  return activate_impl(rt, name, NULL, NULL, false);
}
bool qa_ui_runtime_activate_null(qa_ui_runtime *rt) {
  return activate_impl(rt, NULL, NULL, NULL, true);
}
bool qa_ui_runtime_activate_with(qa_ui_runtime *rt,
                                 qa_ui_runtime_name_fn name, void *ctx) {
  return activate_impl(rt, NULL, name, ctx, false);
}

bool qa_ui_runtime_show(qa_ui_runtime *rt, const char *name) {
  opened(rt);
  menu_state *menu = find_menu(rt, name);
  if (menu == NULL) return false;
  activate_menu(rt, menu);
  return true;
}

bool qa_ui_runtime_close(qa_ui_runtime *rt, const char *name) {
  opened(rt);
  menu_state *menu = find_menu(rt, name);
  if (menu == NULL) return false;
  close_menu(rt, menu);
  return true;
}

void qa_ui_runtime_close_all(qa_ui_runtime *rt) {
  opened(rt);
  for (int64_t i = 0; i < rt->active_menu_count; i++)
    close_menu(rt, live_menu_at(rt, i));
}

bool qa_ui_runtime_any_full_screen_visible(qa_ui_runtime *rt) {
  opened(rt);
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    if ((qa_ui_window_flags(&menu->def->window) & QA_UI_WINDOW_VISIBLE) !=
            0 &&
        qa_ui_menu_full_screen(menu->def) != 0)
      return true;
  }
  return false;
}

double qa_ui_runtime_menu_count(qa_ui_runtime *rt) {
  opened(rt);
  return (double)rt->active_menu_count;
}

/* ---- script execution ---- */

typedef struct script_owner {
  menu_state *menu;
  item_state *item; /* NULL for menu scripts */
} script_owner;

static menu_state *script_menu(runtime_t *rt, script_owner owner) {
  return owner.item == NULL ? owner.menu : parent_menu(rt, owner.item);
}

static int64_t items_matching_group(runtime_t *rt, menu_state *menu,
                                    const char *name, bool name_is_null) {
  if (menu == NULL)
    qa_ui_script_plain_error(
        "Menu_ItemsMatchingGroup dereferences a NULL parent menu");
  int64_t count = 0;
  MENU_ITEMS(rt, menu, item, ii) {
    if (!name_is_null && matches_item(item, name)) count++;
  }
  return count;
}

static item_state *matching_item(runtime_t *rt, menu_state *menu,
                                 const char *name, bool name_is_null,
                                 int64_t match) {
  if (menu == NULL)
    qa_ui_script_plain_error(
        "Menu_GetMatchingItemByNumber dereferences a NULL parent menu");
  int64_t index = 0;
  MENU_ITEMS(rt, menu, item, ii) {
    if (name_is_null || !matches_item(item, name)) continue;
    if (index++ == match) return item;
  }
  return NULL;
}

/* Calls fn for each TS `matching()` item, in order. */
typedef void (*matching_fn)(void *ctx, item_state *item);
static void for_each_matching(runtime_t *rt, menu_state *menu,
                              const char *name, bool name_is_null,
                              matching_fn fn, void *ctx) {
  int64_t count = items_matching_group(rt, menu, name, name_is_null);
  for (int64_t match = 0; match < count; match++) {
    item_state *item = matching_item(rt, menu, name, name_is_null, match);
    if (item != NULL) fn(ctx, item);
  }
}

typedef struct show_ctx {
  runtime_t *rt;
  bool visible;
} show_ctx;

static void show_item(void *ctx, item_state *item) {
  show_ctx *show = ctx;
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if (show->visible) {
    qa_ui_window_set_flags(&item->def->window, flags | QA_UI_WINDOW_VISIBLE);
  } else {
    qa_ui_window_set_flags(&item->def->window, flags & ~QA_UI_WINDOW_VISIBLE);
    close_window_cinematic(show->rt, &item->def->window);
  }
}

static void script_show(runtime_t *rt, script_owner owner,
                        qa_ui_script_cursor *cursor, bool visible) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  show_ctx ctx = {rt, visible};
  for_each_matching(rt, script_menu(rt, owner), name.text,
                    name.kind == QA_UI_SCRIPT_STRING_NULL, show_item, &ctx);
  free(name.text);
}

typedef struct fade_ctx {
  bool fade_out;
} fade_ctx;

static void fade_item(void *ctx, item_state *item) {
  fade_ctx *fade = ctx;
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if (fade->fade_out) {
    flags |= QA_UI_WINDOW_FADING_OUT | QA_UI_WINDOW_VISIBLE;
    flags &= ~QA_UI_WINDOW_FADING_IN;
  } else {
    flags |= QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_FADING_IN;
    flags &= ~QA_UI_WINDOW_FADING_OUT;
  }
  qa_ui_window_set_flags(&item->def->window, flags);
}

static void script_fade(runtime_t *rt, script_owner owner,
                        qa_ui_script_cursor *cursor, bool fade_out) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  fade_ctx ctx = {fade_out};
  for_each_matching(rt, script_menu(rt, owner), name.text,
                    name.kind == QA_UI_SCRIPT_STRING_NULL, fade_item, &ctx);
  free(name.text);
}

static void script_set_color(item_state *item, qa_ui_script_cursor *cursor) {
  cursor_string target = cursor_string_read(cursor);
  if (target.kind != QA_UI_SCRIPT_STRING_TEXT) {
    free(target.text);
    return;
  }
  char *name = ascii_lower(target.text);
  free(target.text);
  int field; /* 0 back, 1 fore, 2 border */
  if (strcmp(name, "backcolor") == 0) field = 0;
  else if (strcmp(name, "forecolor") == 0) field = 1;
  else if (strcmp(name, "bordercolor") == 0) field = 2;
  else {
    free(name);
    return;
  }
  free(name);
  if (item != NULL) {
    int32_t flags = qa_ui_window_flags(&item->def->window);
    if (field == 0) flags |= QA_UI_WINDOW_BACK_COLOR_SET;
    else if (field == 1) flags |= QA_UI_WINDOW_FORE_COLOR_SET;
    qa_ui_window_set_flags(&item->def->window, flags);
  }
  for (int component = 0; component < 4; component++) {
    char *value = script_cursor_raw_string(cursor);
    if (value == NULL) return;
    if (item != NULL) {
      double number = qa_game_atof(value);
      if (field == 0)
        qa_ui_window_set_back_color_component(&item->def->window, component,
                                              number);
      else if (field == 1)
        qa_ui_window_set_fore_color_component(&item->def->window, component,
                                              number);
      else
        qa_ui_window_set_border_color_component(&item->def->window,
                                                component, number);
    }
    free(value);
  }
}

static void script_open(runtime_t *rt, qa_ui_script_cursor *cursor) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind != QA_UI_SCRIPT_STRING_NONE)
    (void)qa_ui_runtime_activate(rt, name.text);
  free(name.text);
}

static void script_conditional_open(runtime_t *rt,
                                    qa_ui_script_cursor *cursor) {
  cursor_string cvar = cursor_string_read(cursor);
  if (cvar.kind == QA_UI_SCRIPT_STRING_NONE) return;
  cursor_string first = cursor_string_read(cursor);
  if (first.kind == QA_UI_SCRIPT_STRING_NONE) {
    free(cvar.text);
    return;
  }
  cursor_string second = cursor_string_read(cursor);
  if (second.kind == QA_UI_SCRIPT_STRING_NONE) {
    free(cvar.text);
    free(first.text);
    return;
  }
  if (cvar.kind == QA_UI_SCRIPT_STRING_NULL)
    qa_ui_script_plain_error("Cvar_VariableValue hashes NULL in Cvar_FindVar");
  double value = cvar_value(rt, cvar.text);
  cursor_string *selected = value == 0 ? &second : &first;
  (void)qa_ui_runtime_activate(rt, selected->text);
  free(cvar.text);
  free(first.text);
  free(second.text);
}

static void script_close(runtime_t *rt, qa_ui_script_cursor *cursor) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind == QA_UI_SCRIPT_STRING_TEXT)
    (void)qa_ui_runtime_close(rt, name.text);
  free(name.text);
}

static void adopt_background(runtime_t *rt, qa_ui_resource_reference *previous,
                             qa_ui_shader_reference *reference) {
  for (size_t i = 0; previous != NULL && i < rt->background_count; i++)
    if (rt->backgrounds[i] == previous) {
      qa_ui_resource_reference_destroy(previous);
      rt->backgrounds[i] = rt->backgrounds[--rt->background_count];
      break;
    }
  if (reference == NULL) return;
  if (rt->background_count == rt->background_capacity) {
    size_t capacity = rt->background_capacity == 0 ? 8 : rt->background_capacity * 2;
    qa_ui_shader_reference **grown = realloc(rt->backgrounds, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("UI memory allocation failed");
    rt->backgrounds = grown;
    rt->background_capacity = capacity;
  }
  rt->backgrounds[rt->background_count++] = reference;
}
static void script_set_background(runtime_t *rt, item_state *item,
                                  qa_ui_script_cursor *cursor) {
  cursor_string path = cursor_string_read(cursor);
  if (path.kind == QA_UI_SCRIPT_STRING_NONE) return;
  qa_picture_asset background;
  bool has = rt->options.resources.register_picture(
      rt->options.resources.ctx, path.text, &background);
  if (item != NULL) {
    qa_ui_shader_reference *reference =
        qa_ui_shader_reference_create(path.text);
    const qa_ui_runtime_handles *handles = &rt->options.resources.handles;
    bool has_handle = false;
    int32_t handle = 0;
    if (handles->kind == QA_UI_HANDLES_SOURCE) {
      has_handle = true;
      handle = (int32_t)handles->picture_handle(
          handles->ctx, has ? &background : NULL);
    }
    qa_ui_resource_reference *previous =
        qa_ui_memory_allocation_get_resource(item->def->window.alloc, 176);
    qa_ui_window_set_background(&item->def->window, reference, has_handle,
                                handle);
    adopt_background(rt, previous, reference);
  }
  char *key = resource_key(path.text);
  pictures_set(rt, key, has, &background);
  free(key);
  free(path.text);
}

static void script_set_item_color(runtime_t *rt, script_owner owner,
                                  qa_ui_script_cursor *cursor) {
  cursor_string item_name = cursor_string_read(cursor);
  if (item_name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  cursor_string target = cursor_string_read(cursor);
  if (target.kind == QA_UI_SCRIPT_STRING_NONE) {
    free(item_name.text);
    return;
  }
  menu_state *menu = script_menu(rt, owner);
  int64_t count = items_matching_group(
      rt, menu, item_name.text, item_name.kind == QA_UI_SCRIPT_STRING_NULL);
  char *parts[4] = {NULL, NULL, NULL, NULL};
  for (int i = 0; i < 4; i++) {
    parts[i] = script_cursor_raw_string(cursor);
    if (parts[i] == NULL) goto done;
  }
  {
    qa_vec4 color = {qa_game_atof(parts[0]), qa_game_atof(parts[1]),
                     qa_game_atof(parts[2]), qa_game_atof(parts[3])};
    if (target.kind != QA_UI_SCRIPT_STRING_NULL) {
      char *name = ascii_lower(target.text);
      for (int64_t match = 0; match < count; match++) {
        item_state *item = matching_item(
            rt, menu, item_name.text,
            item_name.kind == QA_UI_SCRIPT_STRING_NULL, match);
        if (item == NULL) continue;
        if (strcmp(name, "backcolor") == 0) {
          qa_ui_window_set_back_color(&item->def->window, copy_color(color));
        } else if (strcmp(name, "forecolor") == 0) {
          qa_ui_window_set_flags(&item->def->window,
                                 qa_ui_window_flags(&item->def->window) |
                                     QA_UI_WINDOW_FORE_COLOR_SET);
          qa_ui_window_set_fore_color(&item->def->window, copy_color(color));
        } else if (strcmp(name, "bordercolor") == 0) {
          qa_ui_window_set_border_color(&item->def->window,
                                        copy_color(color));
        }
      }
      free(name);
    }
  }
done:
  for (int i = 0; i < 4; i++) free(parts[i]);
  free(item_name.text);
  free(target.text);
}

static void script_set_team_color(runtime_t *rt, item_state *item) {
  qa_vec4 color =
      rt->options.get_team_color(rt->options.get_team_color_ctx);
  if (item != NULL)
    qa_ui_window_set_back_color(&item->def->window, copy_color(color));
}

static item_state *clear_focus(runtime_t *rt, menu_state *menu) {
  if (menu == NULL) return NULL;
  item_state *previous = NULL;
  MENU_ITEMS(rt, menu, item, ii) {
    if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HAS_FOCUS) !=
        0)
      previous = item;
    qa_ui_window_set_flags(&item->def->window,
                           qa_ui_window_flags(&item->def->window) &
                               ~QA_UI_WINDOW_HAS_FOCUS);
    qa_ui_menu_script *leave = qa_ui_item_leave_focus(item->def);
    if (leave != NULL) run_script(rt, menu, item, leave);
  }
  return previous;
}

static void script_set_focus(runtime_t *rt, script_owner owner,
                             qa_ui_script_cursor *cursor) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind != QA_UI_SCRIPT_STRING_TEXT) {
    free(name.text);
    return;
  }
  menu_state *menu = script_menu(rt, owner);
  if (menu == NULL) {
    free(name.text);
    return;
  }
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    char *window_name = qa_ui_window_name(&candidate->def->window);
    bool match = equal_name(window_name, name.text);
    free(window_name);
    if (match) {
      found = candidate;
      break;
    }
  }
  free(name.text);
  if (found == NULL) return;
  int32_t flags = qa_ui_window_flags(&found->def->window);
  if ((flags & QA_UI_WINDOW_DECORATION) != 0 ||
      (flags & QA_UI_WINDOW_HAS_FOCUS) != 0)
    return;
  (void)clear_focus(rt, script_menu(rt, owner));
  qa_ui_window_set_flags(&found->def->window,
                         qa_ui_window_flags(&found->def->window) |
                             QA_UI_WINDOW_HAS_FOCUS);
  qa_ui_menu_script *on_focus = qa_ui_item_on_focus(found->def);
  if (on_focus != NULL) run_script(rt, menu, found, on_focus);
  qa_ui_sound_reference *global_ref = rt->definitions.assets.item_focus_sound;
  const qa_pcm_sound *global_sound =
      global_ref == NULL ? NULL : sounds_get(rt, global_ref->path);
  if (global_sound != NULL) play_local_sound(rt, global_sound);
}

static void script_set_cvar(runtime_t *rt, qa_ui_script_cursor *cursor,
                            const char *fixed_name) {
  cursor_string name;
  bool read_name = fixed_name == NULL;
  if (read_name) {
    name = cursor_string_read(cursor);
    if (name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  } else {
    name.kind = QA_UI_SCRIPT_STRING_TEXT;
    name.text = (char *)fixed_name;
  }
  cursor_string value = cursor_string_read(cursor);
  if (value.kind == QA_UI_SCRIPT_STRING_NONE) {
    if (read_name) free(name.text);
    return;
  }
  if (value.kind == QA_UI_SCRIPT_STRING_NULL)
    set_cvar(rt, name.text, NULL, true);
  else
    set_cvar(rt, name.text, value.text, true);
  if (read_name) free(name.text);
  free(value.text);
}

static void script_exec(runtime_t *rt, qa_ui_script_cursor *cursor) {
  (void)rt;
  cursor_string value = cursor_string_read(cursor);
  const bool present = value.kind != QA_UI_SCRIPT_STRING_NONE;
  free(value.text);
  if (present)
    qa_ui_script_plain_error("UI exec is unavailable in the cgame menu runtime");
}

static void script_play(runtime_t *rt, qa_ui_script_cursor *cursor) {
  cursor_string path = cursor_string_read(cursor);
  if (path.kind != QA_UI_SCRIPT_STRING_NONE) {
    qa_pcm_sound sound;
    bool has = rt->options.resources.register_sound(
        rt->options.resources.ctx, path.text, &sound);
    char *key = resource_key(path.text);
    sounds_set(rt, key, has, &sound);
    free(key);
    play_local_sound(rt, has ? sounds_get(rt, path.text) : NULL);
  }
  free(path.text);
}

static void script_play_looped(runtime_t *rt, qa_ui_script_cursor *cursor) {
  cursor_string path = cursor_string_read(cursor);
  if (path.kind == QA_UI_SCRIPT_STRING_NONE) return;
  rt->options.audio.stop_background(rt->options.audio.ctx);
  opened(rt);
  start_background(rt, path.text);
  free(path.text);
}

typedef struct transition_ctx {
  qa_rect from, to;
  int32_t time;
  double amount;
} transition_ctx;

static double transition_step(double start, double end, double amount) {
  int32_t difference = qa_qvm_float_to_int(FR(end - start));
  int32_t absolute = difference < 0
                         ? (int32_t)(~(uint32_t)difference + 1u)
                         : difference;
  return FR(FR((double)absolute) / amount);
}

static void transition_item(void *ctx, item_state *item) {
  transition_ctx *transition = ctx;
  qa_ui_window_set_flags(&item->def->window,
                         qa_ui_window_flags(&item->def->window) |
                             QA_UI_WINDOW_IN_TRANSITION | QA_UI_WINDOW_VISIBLE);
  qa_ui_window_set_offset_time(&item->def->window, transition->time);
  qa_ui_window_set_client_rect(&item->def->window,
                               copy_rect(transition->from));
  qa_ui_window_set_rect_effects(&item->def->window,
                                copy_rect(transition->to));
  qa_rect effects2 = {
      transition_step(transition->from.x, transition->to.x,
                      transition->amount),
      transition_step(transition->from.y, transition->to.y,
                      transition->amount),
      transition_step(transition->from.width, transition->to.width,
                      transition->amount),
      transition_step(transition->from.height, transition->to.height,
                      transition->amount)};
  qa_ui_window_set_rect_effects2(&item->def->window, effects2);
  update_item_position(item);
}

static void script_transition(runtime_t *rt, script_owner owner,
                              qa_ui_script_cursor *cursor) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  bool ok = false;
  qa_rect from = parsed_rect(cursor, &ok);
  if (!ok) {
    free(name.text);
    return;
  }
  qa_rect to = parsed_rect(cursor, &ok);
  if (!ok) {
    free(name.text);
    return;
  }
  char *time_text = script_cursor_raw_string(cursor);
  if (time_text == NULL) {
    free(name.text);
    return;
  }
  char *amount_text = script_cursor_raw_string(cursor);
  if (amount_text == NULL) {
    free(time_text);
    free(name.text);
    return;
  }
  transition_ctx ctx;
  ctx.from = from;
  ctx.to = to;
  ctx.time = qa_game_atoi(time_text);
  ctx.amount = qa_game_atof(amount_text);
  free(time_text);
  free(amount_text);
  for_each_matching(rt, script_menu(rt, owner), name.text,
                    name.kind == QA_UI_SCRIPT_STRING_NULL, transition_item,
                    &ctx);
  free(name.text);
}

typedef struct orbit_ctx {
  double x, y, cx, cy;
  int32_t time;
} orbit_ctx;

static void orbit_item(void *ctx, item_state *item) {
  orbit_ctx *orbit = ctx;
  qa_ui_window_set_flags(&item->def->window,
                         qa_ui_window_flags(&item->def->window) |
                             QA_UI_WINDOW_ORBITING | QA_UI_WINDOW_VISIBLE);
  qa_ui_window_set_offset_time(&item->def->window, orbit->time);
  qa_rect effects = qa_ui_window_rect_effects(&item->def->window);
  effects.x = orbit->cx;
  effects.y = orbit->cy;
  qa_ui_window_set_rect_effects(&item->def->window, effects);
  qa_rect client = qa_ui_window_client_rect(&item->def->window);
  client.x = orbit->x;
  client.y = orbit->y;
  qa_ui_window_set_client_rect(&item->def->window, client);
  update_item_position(item);
}

static void script_orbit(runtime_t *rt, script_owner owner,
                         qa_ui_script_cursor *cursor) {
  cursor_string name = cursor_string_read(cursor);
  if (name.kind == QA_UI_SCRIPT_STRING_NONE) return;
  char *parts[5] = {NULL, NULL, NULL, NULL, NULL};
  for (int i = 0; i < 5; i++) {
    parts[i] = script_cursor_raw_string(cursor);
    if (parts[i] == NULL) goto done;
  }
  {
    orbit_ctx ctx;
    ctx.x = qa_game_atof(parts[0]);
    ctx.y = qa_game_atof(parts[1]);
    ctx.cx = qa_game_atof(parts[2]);
    ctx.cy = qa_game_atof(parts[3]);
    ctx.time = qa_game_atoi(parts[4]);
    for_each_matching(rt, script_menu(rt, owner), name.text,
                      name.kind == QA_UI_SCRIPT_STRING_NULL, orbit_item,
                      &ctx);
  }
done:
  for (int i = 0; i < 5; i++) free(parts[i]);
  free(name.text);
}

static bool run_shared_command(runtime_t *rt, script_owner owner,
                               const char *command,
                               qa_ui_script_cursor *cursor) {
  if (strcmp(command, "fadein") == 0) {
    script_fade(rt, owner, cursor, false);
  } else if (strcmp(command, "fadeout") == 0) {
    script_fade(rt, owner, cursor, true);
  } else if (strcmp(command, "show") == 0) {
    script_show(rt, owner, cursor, true);
  } else if (strcmp(command, "hide") == 0) {
    script_show(rt, owner, cursor, false);
  } else if (strcmp(command, "setcolor") == 0) {
    script_set_color(owner.item, cursor);
  } else if (strcmp(command, "open") == 0) {
    script_open(rt, cursor);
  } else if (strcmp(command, "conditionalopen") == 0) {
    script_conditional_open(rt, cursor);
  } else if (strcmp(command, "close") == 0) {
    script_close(rt, cursor);
  } else if (strcmp(command, "setasset") == 0) {
    cursor_string dropped = cursor_string_read(cursor);
    free(dropped.text);
  } else if (strcmp(command, "setbackground") == 0) {
    script_set_background(rt, owner.item, cursor);
  } else if (strcmp(command, "setitemcolor") == 0) {
    script_set_item_color(rt, owner, cursor);
  } else if (strcmp(command, "setteamcolor") == 0) {
    script_set_team_color(rt, owner.item);
  } else if (strcmp(command, "setfocus") == 0) {
    script_set_focus(rt, owner, cursor);
  } else if (strcmp(command, "setplayermodel") == 0) {
    script_set_cvar(rt, cursor, "team_model");
  } else if (strcmp(command, "setplayerhead") == 0) {
    script_set_cvar(rt, cursor, "team_headmodel");
  } else if (strcmp(command, "transition") == 0) {
    script_transition(rt, owner, cursor);
  } else if (strcmp(command, "setcvar") == 0) {
    script_set_cvar(rt, cursor, NULL);
  } else if (strcmp(command, "exec") == 0) {
    script_exec(rt, cursor);
  } else if (strcmp(command, "play") == 0) {
    script_play(rt, cursor);
  } else if (strcmp(command, "playlooped") == 0) {
    script_play_looped(rt, cursor);
  } else if (strcmp(command, "orbit") == 0) {
    script_orbit(rt, owner, cursor);
  } else {
    return false;
  }
  return true;
}

static void run_script(runtime_t *rt, menu_state *menu, item_state *item,
                       qa_ui_menu_script *script) {
  char *text = qa_ui_menu_script_text(script);
  if (text == NULL || text[0] == '\0') {
    free(text);
    return;
  }
  /* Menu scripts nest through open/close/transition; the original recursed
     without a bound. */
  if (rt->script_depth >= MAX_SCRIPT_DEPTH) {
    free(text);
    runtime_print(rt, "menu script nesting limit reached\n");
    return;
  }
  rt->script_depth++;
  script_owner owner = {menu, item};
  qa_ui_script_cursor *cursor =
      script_cursor_create(text, rt->source_parser, rt->definitions.memory);
  free(text);
  for (;;) {
    cursor_string command = cursor_string_read(cursor);
    if (command.kind == QA_UI_SCRIPT_STRING_NONE) break;
    if (command.kind == QA_UI_SCRIPT_STRING_NULL) {
      script_cursor_destroy(cursor);
      qa_ui_script_plain_error(
          "Item_RunScript dereferences NULL command after String_Alloc");
    }
    if (strcmp(command.text, ";") == 0) {
      free(command.text);
      continue;
    }
    char *lower = ascii_lower(command.text);
    free(command.text);
    bool handled = run_shared_command(rt, owner, lower, cursor);
    free(lower);
    if (!handled) {
      menu_state *owner_menu = script_menu(rt, owner);
      char *menu_name = owner_menu == NULL
                            ? NULL
                            : qa_ui_window_name(&owner_menu->def->window);
      char *item_name =
          item == NULL ? NULL : qa_ui_window_name(&item->def->window);
      qa_ui_external_script_context context = {menu_name, item_name};
      rt->options.external_script.run(rt->options.external_script.ctx,
                                      cursor, context);
      free(menu_name);
      free(item_name);
    }
  }
  script_cursor_destroy(cursor);
  rt->script_depth--;
}

void qa_ui_runtime_run_menu_script(qa_ui_runtime *rt, const char *menu_name,
                                   qa_ui_menu_script *script) {
  opened(rt);
  menu_state *menu = find_menu(rt, menu_name);
  if (menu == NULL)
    qa_ui_script_plain_error("Unknown UI menu %s", menu_name);
  run_script(rt, menu, NULL, script);
}

void qa_ui_runtime_run_item_script(qa_ui_runtime *rt, const char *menu_name,
                                   const char *item_name,
                                   qa_ui_menu_script *script) {
  opened(rt);
  menu_state *menu = find_menu(rt, menu_name);
  if (menu == NULL)
    qa_ui_script_plain_error("Unknown UI menu %s", menu_name);
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    char *window_name = qa_ui_window_name(&candidate->def->window);
    bool match = equal_name(window_name, item_name);
    free(window_name);
    if (match) {
      found = candidate;
      break;
    }
  }
  if (found == NULL)
    qa_ui_script_plain_error("Unknown UI item %s in %s", item_name,
                             menu_name);
  run_script(rt, menu, found, script);
}

/* ---- cvar gating ---- */

/* purpose: 1 = enable, 4 = show (TS flag values). */
static bool item_passes_cvar(runtime_t *rt, item_state *item, int purpose) {
  int32_t flag = purpose;
  int32_t flags = qa_ui_item_cvar_flags(item->def);
  if ((flags & (flag | flag << 1)) == 0) return true;
  qa_ui_menu_script *script = qa_ui_item_cvar_script(item->def);
  char *cvar_test = qa_ui_item_cvar_test(item->def);
  if (script == NULL || cvar_test == NULL || cvar_test[0] == '\0') {
    free(cvar_test);
    return true;
  }
  char *script_text = qa_ui_menu_script_text(script);
  if (script_text == NULL || script_text[0] == '\0') {
    free(script_text);
    free(cvar_test);
    return true;
  }
  char *current = cvar_buffer(rt, cvar_test);
  free(cvar_test);
  /* TS cvarScriptTokens: each script text is tokenized once per runtime. */
  cvar_script_tokens *entry = NULL;
  for (size_t i = 0; i < rt->cvar_script_count; i++)
    if (strcmp(rt->cvar_scripts[i].script, script_text) == 0) {
      entry = &rt->cvar_scripts[i];
      break;
    }
  if (entry == NULL) {
    cvar_script_tokens built = {script_text, NULL, 0};
    size_t capacity = 0;
    qa_ui_script_cursor *cursor =
        script_cursor_create(script_text, rt->source_parser,
                             rt->definitions.memory);
    for (;;) {
      cursor_string value = cursor_string_read(cursor);
      if (value.kind == QA_UI_SCRIPT_STRING_NONE) break;
      if (built.count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        char **grown = realloc(built.tokens, capacity * sizeof *grown);
        if (grown == NULL) qa_range_error("UI runtime allocation failed");
        built.tokens = grown;
      }
      built.tokens[built.count++] =
          value.kind == QA_UI_SCRIPT_STRING_NULL ? NULL : value.text;
    }
    script_cursor_destroy(cursor);
    if (rt->cvar_script_count == rt->cvar_script_capacity) {
      size_t grown_capacity =
          rt->cvar_script_capacity == 0 ? 8 : rt->cvar_script_capacity * 2;
      cvar_script_tokens *grown =
          realloc(rt->cvar_scripts, grown_capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("UI runtime allocation failed");
      rt->cvar_scripts = grown;
      rt->cvar_script_capacity = grown_capacity;
    }
    rt->cvar_scripts[rt->cvar_script_count] = built;
    entry = &rt->cvar_scripts[rt->cvar_script_count++];
  } else {
    free(script_text);
  }
  bool result = (flags & flag) == 0;
  for (size_t i = 0; i < entry->count; i++) {
    const char *value = entry->tokens[i];
    if (value == NULL) {
      free(current);
      qa_ui_script_plain_error(
          "Item_EnableShowViaCvar dereferences NULL token after String_Alloc");
    }
    if (strcmp(value, ";") != 0 && equal_name(value, current)) {
      result = (flags & flag) != 0;
      break;
    }
  }
  free(current);
  return result;
}

/* ---- pointer / focus ---- */

static qa_rect corrected_text_rect(item_state *item) {
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  if (text_rect.width == 0) return copy_rect(text_rect);
  qa_rect out = text_rect;
  out.y = FR(text_rect.y - text_rect.height);
  return out;
}

static int32_t list_maximum(runtime_t *rt, item_state *item);
static int32_t list_thumb_position(runtime_t *rt, item_state *item);

static int32_t list_hit(runtime_t *rt, item_state *item, double x, double y) {
  (void)rt->options.feeder.count(rt->options.feeder.ctx,
                                  qa_ui_item_special(item->def));
  qa_rect window_rect = qa_ui_window_rect(&item->def->window);
  qa_rect rect;
  if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HORIZONTAL) !=
      0) {
    rect.x = window_rect.x;
    rect.y = FR(FR(window_rect.y + window_rect.height) - SCROLLBAR_SIZE);
    rect.width = SCROLLBAR_SIZE;
    rect.height = SCROLLBAR_SIZE;
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_LEFT_ARROW;
    rect.x = FR(FR(window_rect.x + window_rect.width) - SCROLLBAR_SIZE);
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_RIGHT_ARROW;
    int32_t thumb = list_thumb_position(rt, item);
    rect.x = thumb;
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_THUMB;
    rect.x = FR(window_rect.x + SCROLLBAR_SIZE);
    rect.width = FR(FR((double)thumb) - FR(window_rect.x + SCROLLBAR_SIZE));
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_PAGE_UP;
    rect.x = FR(FR((double)thumb) + SCROLLBAR_SIZE);
    rect.width = FR(FR(window_rect.x + window_rect.width) - SCROLLBAR_SIZE);
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_PAGE_DOWN;
  } else {
    rect.x = FR(FR(window_rect.x + window_rect.width) - SCROLLBAR_SIZE);
    rect.y = window_rect.y;
    rect.width = SCROLLBAR_SIZE;
    rect.height = SCROLLBAR_SIZE;
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_LEFT_ARROW;
    rect.y = FR(FR(window_rect.y + window_rect.height) - SCROLLBAR_SIZE);
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_RIGHT_ARROW;
    int32_t thumb = list_thumb_position(rt, item);
    rect.y = thumb;
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_THUMB;
    rect.y = FR(window_rect.y + SCROLLBAR_SIZE);
    rect.height = FR(FR((double)thumb) - FR(window_rect.y + SCROLLBAR_SIZE));
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_PAGE_UP;
    rect.y = FR(FR((double)thumb) + SCROLLBAR_SIZE);
    rect.height = FR(FR(window_rect.y + window_rect.height) - SCROLLBAR_SIZE);
    if (rect_contains(rect, x, y)) return QA_UI_WINDOW_LIST_PAGE_DOWN;
  }
  return 0;
}

static void list_mouse_enter(runtime_t *rt, item_state *item, double x,
                             double y) {
  qa_ui_list_box_definition list;
  bool has_list = qa_ui_item_list_data(item->def, &list);
  int32_t flags = qa_ui_window_flags(&item->def->window);
  flags &= ~(QA_UI_WINDOW_LIST_LEFT_ARROW | QA_UI_WINDOW_LIST_RIGHT_ARROW |
             QA_UI_WINDOW_LIST_THUMB | QA_UI_WINDOW_LIST_PAGE_UP |
             QA_UI_WINDOW_LIST_PAGE_DOWN);
  flags |= list_hit(rt, item, x, y);
  qa_ui_window_set_flags(&item->def->window, flags);
  const int32_t controls = QA_UI_WINDOW_LIST_LEFT_ARROW |
                           QA_UI_WINDOW_LIST_RIGHT_ARROW |
                           QA_UI_WINDOW_LIST_THUMB | QA_UI_WINDOW_LIST_PAGE_UP |
                           QA_UI_WINDOW_LIST_PAGE_DOWN;
  if ((flags & controls) != 0) return;
  if (!has_list)
    qa_ui_script_plain_error(
        "Item_ListBox_MouseEnter dereferences NULL list data");
  qa_rect window_rect = qa_ui_window_rect(&item->def->window);
  if ((flags & QA_UI_WINDOW_HORIZONTAL) != 0) {
    if (qa_ui_list_element_style(&list) != 1) return;
    qa_rect rect = {window_rect.x, window_rect.y,
                    FR(window_rect.width - qa_ui_list_draw_padding(&list)),
                    FR(window_rect.height - SCROLLBAR_SIZE)};
    if (rect_contains(rect, x, y)) {
      int32_t position =
          i32_add(qa_qvm_float_to_int(
                      FR(FR(x - rect.x) / qa_ui_list_element_width(&list))),
                  qa_ui_list_start_position(&list));
      int32_t end = qa_ui_list_end_position(&list);
      qa_ui_list_set_cursor_position(&list,
                                     position < end ? position : end);
    }
  } else {
    qa_rect rect = {window_rect.x, window_rect.y,
                    FR(window_rect.width - SCROLLBAR_SIZE),
                    FR(window_rect.height - qa_ui_list_draw_padding(&list))};
    if (rect_contains(rect, x, y)) {
      int32_t position = i32_add(
          qa_qvm_float_to_int(FR(FR(FR(y - 2) - rect.y) /
                                 qa_ui_list_element_height(&list))),
          qa_ui_list_start_position(&list));
      int32_t end = qa_ui_list_end_position(&list);
      qa_ui_list_set_cursor_position(&list,
                                     position < end ? position : end);
    }
  }
}

static void mouse_enter(runtime_t *rt, menu_state *menu, item_state *item,
                        double x, double y) {
  if (!item_passes_cvar(rt, item, 1) || !item_passes_cvar(rt, item, 4))
    return;
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  qa_rect corrected = text_rect;
  corrected.y = FR(text_rect.y - text_rect.height);
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if (rect_contains(corrected, x, y)) {
    if ((flags & QA_UI_WINDOW_MOUSE_OVER_TEXT) == 0) {
      qa_ui_menu_script *script = qa_ui_item_mouse_enter_text(item->def);
      if (script != NULL) run_script(rt, menu, item, script);
      flags = qa_ui_window_flags(&item->def->window) |
              QA_UI_WINDOW_MOUSE_OVER_TEXT;
      qa_ui_window_set_flags(&item->def->window, flags);
    }
    if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_MOUSE_OVER) ==
        0) {
      qa_ui_menu_script *script = qa_ui_item_mouse_enter(item->def);
      if (script != NULL) run_script(rt, menu, item, script);
      qa_ui_window_set_flags(&item->def->window,
                             qa_ui_window_flags(&item->def->window) |
                                 QA_UI_WINDOW_MOUSE_OVER);
    }
  } else {
    if ((flags & QA_UI_WINDOW_MOUSE_OVER_TEXT) != 0) {
      qa_ui_menu_script *script = qa_ui_item_mouse_exit_text(item->def);
      if (script != NULL) run_script(rt, menu, item, script);
      qa_ui_window_set_flags(&item->def->window,
                             qa_ui_window_flags(&item->def->window) &
                                 ~QA_UI_WINDOW_MOUSE_OVER_TEXT);
    }
    if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_MOUSE_OVER) ==
        0) {
      qa_ui_menu_script *script = qa_ui_item_mouse_enter(item->def);
      if (script != NULL) run_script(rt, menu, item, script);
      qa_ui_window_set_flags(&item->def->window,
                             qa_ui_window_flags(&item->def->window) |
                                 QA_UI_WINDOW_MOUSE_OVER);
    }
    if (qa_ui_item_type(item->def) == 6) list_mouse_enter(rt, item, x, y);
  }
}

static void mouse_leave(runtime_t *rt, menu_state *menu, item_state *item) {
  if ((qa_ui_window_flags(&item->def->window) &
       QA_UI_WINDOW_MOUSE_OVER_TEXT) != 0) {
    qa_ui_menu_script *script = qa_ui_item_mouse_exit_text(item->def);
    if (script != NULL) run_script(rt, menu, item, script);
    qa_ui_window_set_flags(&item->def->window,
                           qa_ui_window_flags(&item->def->window) &
                               ~QA_UI_WINDOW_MOUSE_OVER_TEXT);
  }
  qa_ui_menu_script *script = qa_ui_item_mouse_exit(item->def);
  if (script != NULL) run_script(rt, menu, item, script);
  qa_ui_window_set_flags(&item->def->window,
                         qa_ui_window_flags(&item->def->window) &
                             ~(QA_UI_WINDOW_LIST_RIGHT_ARROW |
                               QA_UI_WINDOW_LIST_LEFT_ARROW));
}

static bool set_item_focus(runtime_t *rt, menu_state *menu, item_state *item,
                           double x, double y) {
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if ((flags & (QA_UI_WINDOW_DECORATION | QA_UI_WINDOW_HAS_FOCUS)) != 0 ||
      (flags & QA_UI_WINDOW_VISIBLE) == 0)
    return false;
  menu_state *parent = parent_menu(rt, item);
  if (!item_passes_cvar(rt, item, 1) || !item_passes_cvar(rt, item, 4))
    return false;
  item_state *old_focus = clear_focus(rt, parent_menu(rt, item));
  bool play_sound = false;
  if (qa_ui_item_behavior(item->def) == QA_UI_BEHAVIOR_TEXT) {
    if (rect_contains(corrected_text_rect(item), x, y)) {
      qa_ui_window_set_flags(&item->def->window,
                             qa_ui_window_flags(&item->def->window) |
                                 QA_UI_WINDOW_HAS_FOCUS);
      play_sound = true;
    } else if (old_focus != NULL) {
      qa_ui_window_set_flags(&old_focus->def->window,
                             qa_ui_window_flags(&old_focus->def->window) |
                                 QA_UI_WINDOW_HAS_FOCUS);
      qa_ui_menu_script *script = qa_ui_item_on_focus(old_focus->def);
      if (script != NULL) run_script(rt, menu, old_focus, script);
    }
  } else {
    qa_ui_window_set_flags(&item->def->window,
                           qa_ui_window_flags(&item->def->window) |
                               QA_UI_WINDOW_HAS_FOCUS);
    qa_ui_menu_script *script = qa_ui_item_on_focus(item->def);
    if (script != NULL) run_script(rt, menu, item, script);
    play_sound = true;
  }
  if (play_sound) {
    int32_t handle;
    bool has_handle = qa_ui_item_focus_sound_handle(item->def, &handle);
    if (has_handle && handle != 0) {
      play_local_handle(rt, handle);
    } else {
      const qa_pcm_sound *item_sound = NULL;
      if (!has_handle) {
        qa_ui_sound_reference *focus = qa_ui_item_focus_sound(item->def);
        if (focus != NULL) item_sound = sounds_get(rt, focus->path);
      }
      qa_ui_sound_reference *global_ref =
          rt->definitions.assets.item_focus_sound;
      const qa_pcm_sound *global_sound =
          global_ref == NULL ? NULL : sounds_get(rt, global_ref->path);
      play_local_sound(rt, item_sound != NULL ? item_sound : global_sound);
    }
  }
  if (parent == NULL)
    qa_ui_script_plain_error(
        "Item_SetFocus dereferences a NULL parent menu at itemCount");
  int32_t count = qa_ui_menu_item_count(parent->def);
  for (int32_t i = 0; i < count; i++) {
    if (menu_item_at(rt, parent, i) == item) {
      qa_ui_menu_set_cursor_item(parent->def, i);
      break;
    }
  }
  return true;
}

static void mouse_move_menu(runtime_t *rt, menu_state *menu, double x,
                            double y);

static item_state *focus_relative(runtime_t *rt, menu_state *menu,
                                  int direction) {
  int32_t original = qa_ui_menu_cursor_item(menu->def);
  bool wrapped = false;
  int32_t item_count = qa_ui_menu_item_count(menu->def);
  if (direction < 0 && qa_ui_menu_cursor_item(menu->def) < 0) {
    qa_ui_menu_set_cursor_item(menu->def, i32_sub(item_count, 1));
    wrapped = true;
  } else if (direction > 0 && qa_ui_menu_cursor_item(menu->def) == -1) {
    qa_ui_menu_set_cursor_item(menu->def, 0);
    wrapped = true;
  }
  while (direction < 0 ? qa_ui_menu_cursor_item(menu->def) > -1
                       : qa_ui_menu_cursor_item(menu->def) <
                             qa_ui_menu_item_count(menu->def)) {
    qa_ui_menu_set_cursor_item(
        menu->def, i32_add(qa_ui_menu_cursor_item(menu->def), direction));
    int32_t current = qa_ui_menu_cursor_item(menu->def);
    int32_t count = qa_ui_menu_item_count(menu->def);
    if ((direction < 0 ? current < 0 : current >= count) && !wrapped) {
      wrapped = true;
      qa_ui_menu_set_cursor_item(menu->def,
                                 direction < 0 ? i32_sub(count, 1) : 0);
    }
    /* Already wrapped once and nothing took focus. */
    if (qa_ui_menu_cursor_item(menu->def) < 0 ||
        qa_ui_menu_cursor_item(menu->def) >= qa_ui_menu_item_count(menu->def))
      break;
    item_state *item =
        menu_item_at(rt, menu, qa_ui_menu_cursor_item(menu->def));
    if (item != NULL &&
        set_item_focus(rt, menu, item, rt->display_cursor_x,
                       rt->display_cursor_y)) {
      qa_rect rect = qa_ui_window_rect(&item->def->window);
      mouse_move_menu(rt, menu, FR(rect.x + 1), FR(rect.y + 1));
      return menu_item_at(rt, menu, qa_ui_menu_cursor_item(menu->def));
    }
  }
  qa_ui_menu_set_cursor_item(menu->def, original);
  return NULL;
}

static void mouse_move_menu(runtime_t *rt, menu_state *menu, double x,
                            double y) {
  if ((qa_ui_window_flags(&menu->def->window) &
       (QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_FORCED)) == 0)
    return;
  if (rt->capture.kind != CAPTURE_IDLE) return;
  bool focus_set = false;
  for (int pass = 0; pass < 2; pass++) {
    MENU_ITEMS(rt, menu, item, ii) {
      int32_t flags = qa_ui_window_flags(&item->def->window);
      if ((flags & (QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_FORCED)) == 0)
        continue;
      if (!item_passes_cvar(rt, item, 1) || !item_passes_cvar(rt, item, 4))
        continue;
      qa_rect rect = qa_ui_window_rect(&item->def->window);
      if (rect_contains(rect, x, y)) {
        if (pass != 1) continue;
        if (qa_ui_item_behavior(item->def) == QA_UI_BEHAVIOR_TEXT) {
          char *text = qa_ui_item_text(item->def);
          bool has_text = text != NULL;
          free(text);
          if (has_text && !rect_contains(corrected_text_rect(item), x, y))
            continue;
        }
        flags = qa_ui_window_flags(&item->def->window);
        if ((flags & QA_UI_WINDOW_VISIBLE) != 0 &&
            (flags & QA_UI_WINDOW_FADING_OUT) == 0) {
          mouse_enter(rt, menu, item, x, y);
          if (!focus_set) focus_set = set_item_focus(rt, menu, item, x, y);
        }
      } else if ((flags & QA_UI_WINDOW_MOUSE_OVER) != 0) {
        mouse_leave(rt, menu, item);
        qa_ui_runtime_set_item_mouse_over(rt, item->def, false);
      }
    }
  }
}

bool qa_ui_runtime_pointer_move(qa_ui_runtime *rt, double x, double y) {
  opened(rt);
  if (!isfinite(x) || !isfinite(y))
    qa_range_error("UI pointer coordinates must be finite");
  double pointer_x = FR(x), pointer_y = FR(y);
  menu_state *focused = focused_menu(rt);
  if (focused != NULL &&
      (qa_ui_window_flags(&focused->def->window) & QA_UI_WINDOW_POPUP) != 0) {
    mouse_move_menu(rt, focused, pointer_x, pointer_y);
    return true;
  }
  for (int64_t i = 0; i < rt->active_menu_count; i++)
    mouse_move_menu(rt, live_menu_at(rt, i), pointer_x, pointer_y);
  return true;
}

void qa_ui_runtime_set_display_cursor(qa_ui_runtime *rt, double x, double y) {
  opened(rt);
  if (!isfinite(x) || !isfinite(y))
    qa_range_error("UI display cursor coordinates must be finite");
  rt->display_cursor_x = FR(x);
  rt->display_cursor_y = FR(y);
}

void qa_ui_runtime_set_display_time(qa_ui_runtime *rt, double time) {
  opened(rt);
  if (time != trunc(time) || time < -2147483648.0 || time > 2147483647.0)
    qa_range_error(
        "UI time must be a signed 32-bit millisecond value");
  rt->real_time = (int32_t)time;
}

/* ---- list / slider geometry ---- */

static int32_t list_maximum(runtime_t *rt, item_state *item) {
  qa_ui_list_box_definition list;
  bool has_list = qa_ui_item_list_data(item->def, &list);
  double count = rt->options.feeder.count(rt->options.feeder.ctx,
                                          qa_ui_item_special(item->def));
  bool horizontal = (qa_ui_window_flags(&item->def->window) &
                     QA_UI_WINDOW_HORIZONTAL) != 0;
  if (!has_list)
    qa_ui_script_plain_error(
        "Item_ListBox_MaxScroll dereferences NULL list data");
  double element_size = horizontal ? qa_ui_list_element_width(&list)
                                   : qa_ui_list_element_height(&list);
  if (count != trunc(count) || count < -2147483648.0 || count > 2147483647.0)
    qa_range_error("UI feeder count must be a signed 32-bit integer");
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  double extent = horizontal ? rect.width : rect.height;
  int32_t value =
      qa_qvm_float_to_int(FR(FR(FR(count) - FR(extent / element_size)) + 1));
  return value > 0 ? value : 0;
}

static int32_t list_thumb_position(runtime_t *rt, item_state *item) {
  qa_ui_list_box_definition list;
  bool has_list = qa_ui_item_list_data(item->def, &list);
  int32_t maximum = list_maximum(rt, item);
  bool horizontal = (qa_ui_window_flags(&item->def->window) &
                     QA_UI_WINDOW_HORIZONTAL) != 0;
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  double size = FR(FR((horizontal ? rect.width : rect.height) -
                      SCROLLBAR_SIZE * 2) -
                   2);
  double step =
      maximum > 0 ? FR(FR(size - SCROLLBAR_SIZE) / FR((double)maximum)) : 0;
  if (!has_list)
    qa_ui_script_plain_error(
        "Item_ListBox_ThumbPosition dereferences NULL list data");
  double base = horizontal ? rect.x : rect.y;
  return qa_qvm_float_to_int(
      FR(FR(FR(base + 1) + SCROLLBAR_SIZE) +
         FR(step * FR((double)qa_ui_list_start_position(&list)))));
}

static int32_t list_thumb_draw_position(runtime_t *rt, item_state *item) {
  if (rt->capture.kind != CAPTURE_IDLE && rt->capture.item == item) {
    bool horizontal = (qa_ui_window_flags(&item->def->window) &
                       QA_UI_WINDOW_HORIZONTAL) != 0;
    qa_rect rect = qa_ui_window_rect(&item->def->window);
    double start = horizontal ? rect.x : rect.y;
    double extent = horizontal ? rect.width : rect.height;
    int32_t minimum = qa_qvm_float_to_int(FR(FR(start + SCROLLBAR_SIZE) + 1));
    int32_t maximum = qa_qvm_float_to_int(
        FR(FR(FR(start + extent) - 2 * SCROLLBAR_SIZE) - 1));
    double cursor = horizontal ? rt->display_cursor_x : rt->display_cursor_y;
    if (cursor >= FR(FR((double)minimum) + SCROLLBAR_SIZE / 2) &&
        cursor <= FR(FR((double)maximum) + SCROLLBAR_SIZE / 2))
      return qa_qvm_float_to_int(FR(cursor - SCROLLBAR_SIZE / 2));
  }
  return list_thumb_position(rt, item);
}

static void select_list(runtime_t *rt, item_state *item,
                        qa_ui_list_box_definition *list) {
  qa_ui_item_set_cursor_position(item->def,
                                 qa_ui_list_cursor_position(list));
  rt->options.feeder.select(rt->options.feeder.ctx,
                            qa_ui_item_special(item->def),
                            qa_ui_item_cursor_position(item->def));
  opened(rt);
}

static bool handle_list_key(runtime_t *rt, item_state *item, int32_t key,
                            bool force) {
  qa_ui_list_box_definition list;
  bool has_list = qa_ui_item_list_data(item->def, &list);
  double count_d = rt->options.feeder.count(rt->options.feeder.ctx,
                                            qa_ui_item_special(item->def));
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  if (!force &&
      (!rect_contains(rect, rt->display_cursor_x, rt->display_cursor_y) ||
       (qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HAS_FOCUS) ==
           0))
    return false;
  int32_t maximum = list_maximum(rt, item);
  int32_t count = (int32_t)count_d; /* validated by list_maximum */
  bool horizontal = (qa_ui_window_flags(&item->def->window) &
                     QA_UI_WINDOW_HORIZONTAL) != 0;
  if (!has_list)
    qa_ui_script_plain_error(
        "Item_ListBox_HandleKey dereferences NULL list data");
  int32_t view = qa_qvm_float_to_int(
      FR(horizontal ? rect.width / qa_ui_list_element_width(&list)
                    : rect.height / qa_ui_list_element_height(&list)));
  bool backward = horizontal ? key == QA_KEY_LEFT || key == QA_KEY_KEYPAD_LEFT
                             : key == QA_KEY_UP || key == QA_KEY_KEYPAD_UP;
  bool forward = horizontal
                     ? key == QA_KEY_RIGHT || key == QA_KEY_KEYPAD_RIGHT
                     : key == QA_KEY_DOWN || key == QA_KEY_KEYPAD_DOWN;
  bool not_selectable = qa_ui_list_not_selectable(&list);
  if (backward) {
    if (!not_selectable) {
      qa_ui_list_set_cursor_position(
          &list, i32_sub(qa_ui_list_cursor_position(&list), 1));
      if (qa_ui_list_cursor_position(&list) < 0)
        qa_ui_list_set_cursor_position(&list, 0);
      if (qa_ui_list_cursor_position(&list) < qa_ui_list_start_position(&list))
        qa_ui_list_set_start_position(&list,
                                      qa_ui_list_cursor_position(&list));
      if (qa_ui_list_cursor_position(&list) >=
          i32_add(qa_ui_list_start_position(&list), view))
        qa_ui_list_set_start_position(
            &list,
            i32_add(i32_sub(qa_ui_list_cursor_position(&list), view), 1));
      select_list(rt, item, &list);
    } else {
      qa_ui_list_set_start_position(
          &list, i32_sub(qa_ui_list_start_position(&list), 1));
      if (qa_ui_list_start_position(&list) < 0)
        qa_ui_list_set_start_position(&list, 0);
    }
    return true;
  }
  if (forward) {
    if (!not_selectable) {
      qa_ui_list_set_cursor_position(
          &list, i32_add(qa_ui_list_cursor_position(&list), 1));
      if (qa_ui_list_cursor_position(&list) < qa_ui_list_start_position(&list))
        qa_ui_list_set_start_position(&list,
                                      qa_ui_list_cursor_position(&list));
      if (qa_ui_list_cursor_position(&list) >= count)
        qa_ui_list_set_cursor_position(&list, i32_sub(count, 1));
      if (qa_ui_list_cursor_position(&list) >=
          i32_add(qa_ui_list_start_position(&list), view))
        qa_ui_list_set_start_position(
            &list,
            i32_add(i32_sub(qa_ui_list_cursor_position(&list), view), 1));
      select_list(rt, item, &list);
    } else {
      qa_ui_list_set_start_position(
          &list, i32_add(qa_ui_list_start_position(&list), 1));
      int32_t limit = horizontal ? i32_sub(count, 1) : maximum;
      if (qa_ui_list_start_position(&list) > limit)
        qa_ui_list_set_start_position(&list, limit);
    }
    return true;
  }
  if (key == QA_KEY_MOUSE1 || key == QA_KEY_MOUSE2) {
    int32_t flags = qa_ui_window_flags(&item->def->window);
    if ((flags & QA_UI_WINDOW_LIST_LEFT_ARROW) != 0) {
      qa_ui_list_set_start_position(
          &list, i32_sub(qa_ui_list_start_position(&list), 1));
      if (qa_ui_list_start_position(&list) < 0)
        qa_ui_list_set_start_position(&list, 0);
    } else if ((flags & QA_UI_WINDOW_LIST_RIGHT_ARROW) != 0) {
      qa_ui_list_set_start_position(
          &list, i32_add(qa_ui_list_start_position(&list), 1));
      if (qa_ui_list_start_position(&list) > maximum)
        qa_ui_list_set_start_position(&list, maximum);
    } else if ((flags & QA_UI_WINDOW_LIST_PAGE_UP) != 0) {
      qa_ui_list_set_start_position(
          &list, i32_sub(qa_ui_list_start_position(&list), view));
      if (qa_ui_list_start_position(&list) < 0)
        qa_ui_list_set_start_position(&list, 0);
    } else if ((flags & QA_UI_WINDOW_LIST_PAGE_DOWN) != 0) {
      qa_ui_list_set_start_position(
          &list, i32_add(qa_ui_list_start_position(&list), view));
      if (qa_ui_list_start_position(&list) > maximum)
        qa_ui_list_set_start_position(&list, maximum);
    } else if ((flags & QA_UI_WINDOW_LIST_THUMB) == 0) {
      qa_ui_menu_script *double_click = qa_ui_list_double_click(&list);
      if (rt->real_time < rt->last_list_click_time && double_click != NULL)
        run_script(rt, menu_for(rt, item), item, double_click);
      rt->last_list_click_time = i32_add(rt->real_time, DOUBLE_CLICK_DELAY);
      if (qa_ui_item_cursor_position(item->def) !=
          qa_ui_list_cursor_position(&list))
        select_list(rt, item, &list);
    }
    return true;
  }
  if (key == QA_KEY_HOME || key == QA_KEY_KEYPAD_HOME) {
    qa_ui_list_set_start_position(&list, 0);
    return true;
  }
  if (key == QA_KEY_END || key == QA_KEY_KEYPAD_END) {
    qa_ui_list_set_start_position(&list, maximum);
    return true;
  }
  bool page_up = key == QA_KEY_PAGE_UP || key == QA_KEY_KEYPAD_PAGE_UP;
  bool page_down = key == QA_KEY_PAGE_DOWN || key == QA_KEY_KEYPAD_PAGE_DOWN;
  if (page_up || page_down) {
    int32_t amount = page_up ? -view : view;
    if (!not_selectable) {
      qa_ui_list_set_cursor_position(
          &list, i32_add(qa_ui_list_cursor_position(&list), amount));
      if (page_up && qa_ui_list_cursor_position(&list) < 0)
        qa_ui_list_set_cursor_position(&list, 0);
      if (qa_ui_list_cursor_position(&list) < qa_ui_list_start_position(&list))
        qa_ui_list_set_start_position(&list,
                                      qa_ui_list_cursor_position(&list));
      if (page_down && qa_ui_list_cursor_position(&list) >= count)
        qa_ui_list_set_cursor_position(&list, i32_sub(count, 1));
      if (qa_ui_list_cursor_position(&list) >=
          i32_add(qa_ui_list_start_position(&list), view))
        qa_ui_list_set_start_position(
            &list,
            i32_add(i32_sub(qa_ui_list_cursor_position(&list), view), 1));
      select_list(rt, item, &list);
    } else {
      qa_ui_list_set_start_position(
          &list, i32_add(qa_ui_list_start_position(&list), amount));
      if (page_up && qa_ui_list_start_position(&list) < 0)
        qa_ui_list_set_start_position(&list, 0);
      if (page_down && qa_ui_list_start_position(&list) > maximum)
        qa_ui_list_set_start_position(&list, maximum);
    }
    return true;
  }
  return false;
}

static double slider_x(item_state *item) {
  char *text = qa_ui_item_text(item->def);
  bool has_text = text != NULL;
  free(text);
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  if (!has_text) return rect.x;
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  return FR(FR(text_rect.x + text_rect.width) + 8);
}

static double slider_thumb_position(runtime_t *rt, item_state *item) {
  qa_ui_edit_field_definition edit;
  bool has_edit = qa_ui_item_edit_data(item->def, &edit);
  double x = slider_x(item);
  char *cvar = qa_ui_item_cvar(item->def);
  if (!has_edit && cvar != NULL) {
    free(cvar);
    return x;
  }
  if (cvar == NULL)
    qa_ui_script_plain_error(
        "Item_Slider_ThumbPosition hashes NULL cvar in Cvar_FindVar");
  double value = cvar_value(rt, cvar);
  free(cvar);
  if (!has_edit)
    qa_ui_script_plain_error(
        "Item_Slider_ThumbPosition dereferences NULL edit data");
  double minimum = qa_ui_edit_minimum(&edit);
  double maximum = qa_ui_edit_maximum(&edit);
  if (value < minimum) value = minimum;
  else if (value > maximum) value = maximum;
  double range = FR(maximum - minimum);
  value = FR(value - minimum);
  value = FR(value / range);
  value = FR(value * SLIDER_WIDTH);
  return FR(x + value);
}

static bool handle_slider_key(runtime_t *rt, item_state *item, int32_t key) {
  char *cvar = qa_ui_item_cvar(item->def);
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  /* Left/Right step a focused slider by a twentieth of its range. */
  if (cvar != NULL &&
      (qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HAS_FOCUS) !=
          0 &&
      (key == QA_KEY_LEFT || key == QA_KEY_RIGHT)) {
    qa_ui_edit_field_definition edit;
    if (qa_ui_item_edit_data(item->def, &edit) &&
        qa_ui_edit_maximum(&edit) > qa_ui_edit_minimum(&edit)) {
      const double minimum = qa_ui_edit_minimum(&edit),
                   maximum = qa_ui_edit_maximum(&edit);
      const double step = FR(FR(maximum - minimum) / 20);
      const double value =
          FR(cvar_value(rt, cvar) + (key == QA_KEY_RIGHT ? step : -step));
      char *text = game_format1(
          "%f", qa_q3_game_format_number(
                    qa_js_max(minimum, qa_js_min(maximum, value))));
      cvar_set_direct(rt, cvar, text, true);
      free(text);
      free(cvar);
      return true;
    }
  }
  if (cvar != NULL &&
      (qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HAS_FOCUS) !=
          0 &&
      rect_contains(rect, rt->display_cursor_x, rt->display_cursor_y) &&
      is_activate_key(key)) {
    qa_ui_edit_field_definition edit;
    if (qa_ui_item_edit_data(item->def, &edit)) {
      double x = slider_x(item);
      qa_rect test = rect;
      test.x = FR(x - SLIDER_THUMB_WIDTH / 2);
      test.width = FR(SLIDER_WIDTH + SLIDER_THUMB_WIDTH / 2);
      if (rect_contains(test, rt->display_cursor_x, rt->display_cursor_y)) {
        double work = FR(rt->display_cursor_x - x);
        double value = FR(FR(FR(work / SLIDER_WIDTH) *
                             FR(qa_ui_edit_maximum(&edit) -
                                qa_ui_edit_minimum(&edit))) +
                          qa_ui_edit_minimum(&edit));
        char *text =
            game_format1("%f", qa_q3_game_format_number(value));
        cvar_set_direct(rt, cvar, text, true);
        free(text);
        free(cvar);
        return true;
      }
    }
  }
  free(cvar);
  runtime_print(rt, "slider handle key exit\n");
  return false;
}

static void start_capture(runtime_t *rt, item_state *item, int32_t key) {
  qa_ui_item_behavior_kind kind = qa_ui_item_behavior(item->def);
  if (kind == QA_UI_BEHAVIOR_LIST_BOX || kind == QA_UI_BEHAVIOR_EDIT_FIELD ||
      kind == QA_UI_BEHAVIOR_NUMERIC_FIELD) {
    int32_t flags =
        list_hit(rt, item, rt->display_cursor_x, rt->display_cursor_y);
    if ((flags & (QA_UI_WINDOW_LIST_LEFT_ARROW |
                  QA_UI_WINDOW_LIST_RIGHT_ARROW)) != 0) {
      rt->next_scroll_time = i32_add(rt->real_time, 500);
      rt->next_scroll_adjust_time = i32_add(rt->real_time, 150);
      rt->scroll_adjust_value = 500;
      rt->capture.kind = CAPTURE_LIST_AUTO;
      rt->capture.item = item;
      rt->capture.key = key;
      rt->capture.x_start = rt->display_cursor_x;
      rt->capture.y_start = rt->display_cursor_y;
    } else if ((flags & QA_UI_WINDOW_LIST_THUMB) != 0) {
      rt->capture.kind = CAPTURE_LIST_THUMB;
      rt->capture.item = item;
      rt->capture.key = key;
      rt->capture.x_start = rt->display_cursor_x;
      rt->capture.y_start = rt->display_cursor_y;
    }
  } else if (kind == QA_UI_BEHAVIOR_SLIDER) {
    double thumb = slider_thumb_position(rt, item);
    qa_rect rect = qa_ui_window_rect(&item->def->window);
    qa_rect test = {FR(thumb - SLIDER_THUMB_WIDTH / 2), FR(rect.y - 2),
                    SLIDER_THUMB_WIDTH, 20};
    if (rect_contains(test, rt->display_cursor_x, rt->display_cursor_y)) {
      rt->capture.kind = CAPTURE_SLIDER_THUMB;
      rt->capture.item = item;
      rt->capture.key = key;
      rt->capture.x_start = rt->display_cursor_x;
      rt->capture.y_start = rt->display_cursor_y;
    }
  }
}

/* ---- item key handlers ---- */

/* id requires the pointer over a widget; Enter on the focused widget also
   activates it so keyboard and pad players can operate it. */
static bool activates_at_pointer(runtime_t *rt, qa_rect rect, int32_t key) {
  return is_activate_key(key) &&
         (key == QA_KEY_ENTER ||
          rect_contains(rect, rt->display_cursor_x, rt->display_cursor_y));
}

static bool handle_yes_no_key(runtime_t *rt, item_state *item, int32_t key) {
  char *cvar = qa_ui_item_cvar(item->def);
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  bool ok = cvar != NULL &&
            (qa_ui_window_flags(&item->def->window) &
             QA_UI_WINDOW_HAS_FOCUS) != 0 &&
            activates_at_pointer(rt, rect, key);
  if (!ok) {
    free(cvar);
    return false;
  }
  double value = cvar_value(rt, cvar);
  cvar_set_direct(rt, cvar, value == 0 ? "1" : "0", true);
  free(cvar);
  return true;
}

static bool handle_multi_key(runtime_t *rt, item_state *item, int32_t key) {
  char *cvar = qa_ui_item_cvar(item->def);
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  bool ok = qa_ui_item_behavior(item->def) == QA_UI_BEHAVIOR_MULTI &&
            cvar != NULL &&
            (qa_ui_window_flags(&item->def->window) &
             QA_UI_WINDOW_HAS_FOCUS) != 0 &&
            activates_at_pointer(rt, rect, key);
  if (!ok) {
    free(cvar);
    return false;
  }
  qa_ui_multi_definition multi;
  if (!qa_ui_item_multi_data(item->def, &multi)) {
    free(cvar);
    return false;
  }
  int32_t count = qa_ui_multi_count(&multi);
  int32_t current = 0;
  if (qa_ui_multi_string_definition(&multi)) {
    char *value = cvar_buffer(rt, cvar);
    for (int32_t i = 0; i < count; i++) {
      char *candidate = qa_ui_multi_string_value(&multi, i);
      bool match = equal_name(candidate, value);
      free(candidate);
      if (match) {
        current = i;
        break;
      }
    }
    free(value);
  } else {
    double value = cvar_value(rt, cvar);
    for (int32_t i = 0; i < count; i++) {
      if (qa_ui_multi_number_value(&multi, i) == value) {
        current = i;
        break;
      }
    }
  }
  current = i32_add(current, 1);
  if (current < 0 || current >= count) current = 0;
  if (qa_ui_multi_string_definition(&multi)) {
    char *value = qa_ui_multi_string_value(&multi, current);
    if (value == NULL) set_cvar(rt, cvar, NULL, true);
    else set_cvar(rt, cvar, value, true);
    free(value);
  } else {
    double value = qa_ui_multi_number_value(&multi, current);
    int32_t integer = qa_qvm_float_to_int(value);
    char *text = FR((double)integer) == value
                     ? game_format1("%i", qa_q3_game_format_number(integer))
                     : game_format1("%f", qa_q3_game_format_number(value));
    cvar_set_direct(rt, cvar, text, true);
    free(text);
  }
  free(cvar);
  return true;
}

static bool handle_item_key(runtime_t *rt, item_state *item, int32_t key,
                            bool down) {
  if (rt->capture.kind != CAPTURE_IDLE) {
    rt->capture.kind = CAPTURE_IDLE;
    rt->capture.item = NULL;
  } else if (down && is_mouse_key(key)) {
    start_capture(rt, item, key);
  }
  if (!down) return false;
  switch (qa_ui_item_behavior(item->def)) {
    case QA_UI_BEHAVIOR_LIST_BOX:
      return handle_list_key(rt, item, key, false);
    case QA_UI_BEHAVIOR_YES_NO:
      return handle_yes_no_key(rt, item, key);
    case QA_UI_BEHAVIOR_MULTI:
      return handle_multi_key(rt, item, key);
    case QA_UI_BEHAVIOR_OWNER_DRAW: {
      qa_ui_owner_draw_key_result result =
          rt->options.owner_draw.handle_key(
              rt->options.owner_draw.ctx,
              qa_ui_window_owner_draw(&item->def->window),
              qa_ui_window_owner_draw_flags(&item->def->window),
              qa_ui_item_special(item->def), key);
      opened(rt);
      qa_ui_item_set_special(item->def, FR(result.special));
      return result.handled;
    }
    case QA_UI_BEHAVIOR_BIND:
      return false;
    case QA_UI_BEHAVIOR_SLIDER:
      return handle_slider_key(rt, item, key);
    default:
      return false;
  }
}

static bool menu_over_active_item(runtime_t *rt, menu_state *menu, double x,
                                  double y) {
  int32_t menu_flags = qa_ui_window_flags(&menu->def->window);
  qa_rect menu_rect = qa_ui_window_rect(&menu->def->window);
  if ((menu_flags & (QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_FORCED)) == 0 ||
      !rect_contains(menu_rect, x, y))
    return false;
  bool found = false;
  MENU_ITEMS(rt, menu, item, ii) {
    int32_t flags = qa_ui_window_flags(&item->def->window);
    if ((flags & (QA_UI_WINDOW_VISIBLE | QA_UI_WINDOW_FORCED)) == 0 ||
        (flags & QA_UI_WINDOW_DECORATION) != 0)
      continue;
    qa_rect rect = qa_ui_window_rect(&item->def->window);
    if (!rect_contains(rect, x, y)) continue;
    if (qa_ui_item_behavior(item->def) != QA_UI_BEHAVIOR_TEXT) {
      found = true;
      break;
    }
    char *text = qa_ui_item_text(item->def);
    bool has_text = text != NULL;
    free(text);
    if (!has_text || rect_contains(corrected_text_rect(item), x, y)) {
      found = true;
      break;
    }
  }
  return found;
}

static bool handle_menu_key(runtime_t *rt, menu_state *menu, int32_t key,
                            bool down);

static void handle_out_of_bounds(runtime_t *rt, menu_state *menu,
                                 int32_t key, bool down) {
  if (down && (qa_ui_window_flags(&menu->def->window) &
               QA_UI_WINDOW_OUT_OF_BOUNDS_CLICK) != 0)
    close_menu(rt, menu);
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *candidate = live_menu_at(rt, i);
    if (!menu_over_active_item(rt, candidate, rt->display_cursor_x,
                               rt->display_cursor_y))
      continue;
    close_menu(rt, menu);
    activate_menu(rt, candidate);
    mouse_move_menu(rt, candidate, rt->display_cursor_x,
                    rt->display_cursor_y);
    (void)handle_menu_key(rt, candidate, key, down);
  }
  close_cinematics(rt);
}

static bool handle_menu_key(runtime_t *rt, menu_state *menu, int32_t key,
                            bool down) {
  qa_rect menu_rect = qa_ui_window_rect(&menu->def->window);
  if (down &&
      (qa_ui_window_flags(&menu->def->window) & QA_UI_WINDOW_POPUP) == 0 &&
      !rect_contains(menu_rect, rt->display_cursor_x,
                     rt->display_cursor_y) &&
      is_mouse_key(key)) {
    handle_out_of_bounds(rt, menu, key, down);
    return true;
  }
  item_state *item = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    if ((qa_ui_window_flags(&candidate->def->window) &
         QA_UI_WINDOW_HAS_FOCUS) != 0)
      item = candidate;
  }
  if (item != NULL && handle_item_key(rt, item, key, down)) {
    qa_ui_menu_script *action = qa_ui_item_action(item->def);
    if (action != NULL) run_script(rt, menu, item, action);
    return true;
  }
  if (!down) return false;
  if (key == QA_KEY_F11) {
    if (cvar_value(rt, "developer") != 0) rt->debug = !rt->debug;
    return true;
  }
  if (key == QA_KEY_UP || key == QA_KEY_KEYPAD_UP) {
    (void)focus_relative(rt, menu, -1);
    return true;
  }
  if (key == QA_KEY_DOWN || key == QA_KEY_KEYPAD_DOWN || key == QA_KEY_TAB) {
    (void)focus_relative(rt, menu, 1);
    return true;
  }
  if (key == QA_KEY_ESCAPE) {
    qa_ui_menu_script *on_escape = qa_ui_menu_on_escape(menu->def);
    if (on_escape != NULL)
      run_script(rt, menu, NULL, on_escape);
    return true;
  }
  if ((key == QA_KEY_MOUSE1 || key == QA_KEY_MOUSE2) && item != NULL) {
    qa_ui_item_behavior_kind kind = qa_ui_item_behavior(item->def);
    qa_rect rect = qa_ui_window_rect(&item->def->window);
    if (kind == QA_UI_BEHAVIOR_TEXT) {
      qa_ui_menu_script *action = qa_ui_item_action(item->def);
      if (rect_contains(corrected_text_rect(item), rt->display_cursor_x,
                        rt->display_cursor_y) &&
          action != NULL)
        run_script(rt, menu, item, action);
    } else if (kind == QA_UI_BEHAVIOR_EDIT_FIELD ||
               kind == QA_UI_BEHAVIOR_NUMERIC_FIELD) {
      /* Edit fields are inert in the cgame menu runtime: no key-binding or
         overstrike host exists. */
    } else {
      qa_ui_menu_script *action = qa_ui_item_action(item->def);
      if (rect_contains(rect, rt->display_cursor_x, rt->display_cursor_y) &&
          action != NULL)
        run_script(rt, menu, item, action);
    }
    return true;
  }
  if ((key == QA_KEY_ENTER || key == QA_KEY_KEYPAD_ENTER) && item != NULL) {
    qa_ui_item_behavior_kind kind = qa_ui_item_behavior(item->def);
    if (kind != QA_UI_BEHAVIOR_EDIT_FIELD &&
        kind != QA_UI_BEHAVIOR_NUMERIC_FIELD) {
      qa_ui_menu_script *action = qa_ui_item_action(item->def);
      if (action != NULL) run_script(rt, menu, item, action);
    }
    return true;
  }
  return false;
}

static menu_state *menu_at(runtime_t *rt, double x, double y) {
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    if (rect_contains(qa_ui_window_rect(&menu->def->window), x, y))
      return menu;
  }
  return NULL;
}

static int32_t validate_key_event(qa_ui_key_event event) {
  if (event.code != trunc(event.code) || event.code < 0 ||
      event.code > 0x7fff)
    qa_range_error("UI key code must be an integer in 0..32767");
  if (event.kind == QA_UI_KEY_EVENT_CHARACTER && event.code > 255)
    qa_range_error("UI character code must fit one byte");
  return (int32_t)event.code;
}

bool qa_ui_runtime_handle_key(qa_ui_runtime *rt, qa_ui_key_event event,
                              double x, double y) {
  opened(rt);
  int32_t key = validate_key_event(event);
  if (!isfinite(x) || !isfinite(y))
    qa_range_error("UI key coordinates must be finite");
  menu_state *menu = menu_at(rt, FR(x), FR(y));
  if (menu == NULL) menu = focused_menu(rt);
  if (menu == NULL) return false;
  bool is_char = event.kind == QA_UI_KEY_EVENT_CHARACTER;
  return handle_menu_key(rt, menu,
                         is_char ? key | QA_KEY_CHAR_FLAG : key,
                         is_char || event.down);
}

qa_ui_captured_menu *qa_ui_runtime_focused_menu_handle(qa_ui_runtime *rt) {
  opened(rt);
  menu_state *menu = focused_menu(rt);
  return menu == NULL ? NULL : &menu->handle;
}

bool qa_ui_runtime_handle_captured_key(qa_ui_runtime *rt,
                                       qa_ui_captured_menu *handle,
                                       qa_ui_key_event event) {
  opened(rt);
  menu_state *menu = captured_menu(rt, handle);
  int32_t key = validate_key_event(event);
  bool is_char = event.kind == QA_UI_KEY_EVENT_CHARACTER;
  return handle_menu_key(rt, menu,
                         is_char ? key | QA_KEY_CHAR_FLAG : key,
                         is_char || event.down);
}

qa_ui_captured_menu *qa_ui_runtime_menu_handle(qa_ui_runtime *rt,
                                               const char *name) {
  opened(rt);
  menu_state *menu = find_menu(rt, name);
  return menu == NULL ? NULL : &menu->handle;
}

qa_ui_captured_menu *qa_ui_runtime_capture_menu(qa_ui_runtime *rt, double x,
                                                double y) {
  opened(rt);
  if (!isfinite(x) || !isfinite(y))
    qa_range_error("UI capture coordinates must be finite");
  menu_state *menu = menu_at(rt, FR(x), FR(y));
  return menu == NULL ? NULL : &menu->handle;
}

qa_ui_item_definition *qa_ui_runtime_hit_test_menu(
    qa_ui_runtime *rt, qa_ui_captured_menu *handle, double x, double y) {
  opened(rt);
  menu_state *menu = captured_menu(rt, handle);
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, item, ii) {
    if (rect_contains(qa_ui_window_rect(&item->def->window), FR(x), FR(y))) {
      found = item;
      break;
    }
  }
  return found == NULL ? NULL : found->def;
}

void qa_ui_runtime_set_item_mouse_over(qa_ui_runtime *rt,
                                       qa_ui_item_definition *item,
                                       bool focused) {
  opened(rt);
  if (item == NULL) return;
  int32_t flags = qa_ui_window_flags(&item->window);
  if (focused) flags |= QA_UI_WINDOW_MOUSE_OVER;
  else flags &= ~QA_UI_WINDOW_MOUSE_OVER;
  qa_ui_window_set_flags(&item->window, flags);
}

void qa_ui_runtime_move_captured_menu(qa_ui_runtime *rt,
                                      qa_ui_captured_menu *handle,
                                      double delta_x, double delta_y) {
  opened(rt);
  if (!isfinite(delta_x) || !isfinite(delta_y))
    qa_range_error("UI captured-menu delta must be finite");
  menu_state *menu = captured_menu(rt, handle);
  qa_rect rect = qa_ui_window_rect(&menu->def->window);
  rect.x = FR(rect.x + FR(delta_x));
  rect.y = FR(rect.y + FR(delta_y));
  qa_ui_window_set_rect(&menu->def->window, rect);
  double x = rect.x, y = rect.y;
  if (qa_ui_window_border(&menu->def->window) != 0) {
    double border_size = qa_ui_window_border_size(&menu->def->window);
    x = FR(x + border_size);
    y = FR(y + border_size);
  }
  int32_t count = qa_ui_menu_item_count(menu->def);
  for (int32_t i = 0; i < count; i++) {
    item_state *item = menu_item_at(rt, menu, i);
    if (item != NULL) set_item_screen_coords(item, x, y);
  }
}

static void select_feeder(runtime_t *rt, menu_state *menu, double feeder,
                          double index) {
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    if (qa_ui_item_special(candidate->def) == FR(feeder)) {
      found = candidate;
      break;
    }
  }
  if (found == NULL) return;
  if (index == 0) {
    qa_ui_list_box_definition list;
    if (!qa_ui_item_list_data(found->def, &list))
      qa_ui_script_plain_error(
          "Menu_SetFeederSelection dereferences NULL list data");
    qa_ui_list_set_cursor_position(&list, 0);
    qa_ui_list_set_start_position(&list, 0);
  }
  qa_ui_item_set_cursor_position(found->def, qa_int32(index));
  rt->options.feeder.select(rt->options.feeder.ctx,
                            qa_ui_item_special(found->def),
                            qa_ui_item_cursor_position(found->def));
  opened(rt);
}

void qa_ui_runtime_set_feeder_selection(qa_ui_runtime *rt, double feeder,
                                        double index,
                                        const char *menu_name) {
  opened(rt);
  if (!isfinite(feeder) || index != trunc(index))
    qa_range_error("Invalid feeder selection");
  menu_state *menu =
      menu_name == NULL ? focused_menu(rt) : find_menu(rt, menu_name);
  if (menu == NULL) return;
  select_feeder(rt, menu, feeder, index);
}

void qa_ui_runtime_set_captured_feeder_selection(
    qa_ui_runtime *rt, qa_ui_captured_menu *handle, double feeder,
    double index) {
  opened(rt);
  if (!isfinite(feeder) || index != trunc(index))
    qa_range_error("Invalid feeder selection");
  select_feeder(rt, captured_menu(rt, handle), feeder, index);
}

void qa_ui_runtime_scroll_feeder(qa_ui_runtime *rt, double feeder, bool down,
                                 const char *menu_name) {
  opened(rt);
  menu_state *menu =
      menu_name == NULL ? focused_menu(rt) : find_menu(rt, menu_name);
  if (menu == NULL) return;
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    if (qa_ui_item_special(candidate->def) == FR(feeder)) {
      found = candidate;
      break;
    }
  }
  if (found != NULL)
    (void)handle_list_key(rt, found, down ? QA_KEY_DOWN : QA_KEY_UP, true);
}

void qa_ui_runtime_scroll_captured_feeder(qa_ui_runtime *rt,
                                          qa_ui_captured_menu *handle,
                                          double feeder, bool down) {
  opened(rt);
  menu_state *menu = captured_menu(rt, handle);
  item_state *found = NULL;
  MENU_ITEMS(rt, menu, candidate, ii) {
    if (qa_ui_item_special(candidate->def) == FR(feeder)) {
      found = candidate;
      break;
    }
  }
  if (found != NULL)
    (void)handle_list_key(rt, found, down ? QA_KEY_DOWN : QA_KEY_UP, true);
}

qa_ui_cursor_type qa_ui_runtime_cursor_type(qa_ui_runtime *rt, double x,
                                            double y) {
  opened(rt);
  for (int64_t i = 0; i < rt->active_menu_count; i++) {
    menu_state *menu = rt->menus[i];
    qa_rect rect = qa_ui_window_rect(&menu->def->window);
    qa_rect corner = {FR(rect.x - 3), FR(rect.y - 3), 7, 7};
    if (rect_contains(corner, x, y)) return QA_UI_CURSOR_SIZER;
  }
  return QA_UI_CURSOR_ARROW;
}

static void run_capture(runtime_t *rt) {
  capture_state *capture = &rt->capture;
  if (capture->kind == CAPTURE_IDLE) return;
  if (capture->kind == CAPTURE_SLIDER_THUMB) {
    qa_ui_edit_field_definition edit;
    bool has_edit = qa_ui_item_edit_data(capture->item->def, &edit);
    double x = slider_x(capture->item);
    double upper = FR(x + SLIDER_WIDTH);
    double cursor = rt->display_cursor_x;
    if (cursor < x) cursor = x;
    if (cursor > upper) cursor = upper;
    if (!has_edit)
      qa_ui_script_plain_error(
          "Scroll_Slider_ThumbFunc dereferences NULL edit data");
    double value = FR(FR(FR(FR(cursor - x) / SLIDER_WIDTH) *
                         FR(qa_ui_edit_maximum(&edit) -
                            qa_ui_edit_minimum(&edit))) +
                      qa_ui_edit_minimum(&edit));
    char *text = game_format1("%f", qa_q3_game_format_number(value));
    char *cvar = qa_ui_item_cvar(capture->item->def);
    set_cvar(rt, cvar, text, true);
    free(cvar);
    free(text);
    return;
  }
  if (capture->kind == CAPTURE_LIST_THUMB) {
    qa_ui_list_box_definition list;
    bool has_list = qa_ui_item_list_data(capture->item->def, &list);
    bool horizontal = (qa_ui_window_flags(&capture->item->def->window) &
                       QA_UI_WINDOW_HORIZONTAL) != 0;
    qa_rect rect = qa_ui_window_rect(&capture->item->def->window);
    if (horizontal) {
      if (rt->display_cursor_x != capture->x_start) {
        double width = FR(FR(rect.width - SCROLLBAR_SIZE * 2) - 2);
        double start = FR(FR(rect.x + SCROLLBAR_SIZE) + 1);
        int32_t maximum = list_maximum(rt, capture->item);
        double denominator = FR(width - SCROLLBAR_SIZE);
        if (!has_list)
          qa_ui_script_plain_error(
              "Scroll_ListBox_ThumbFunc dereferences NULL list data");
        int32_t position = qa_qvm_float_to_int(FR(
            FR(FR(FR(rt->display_cursor_x - start) - SCROLLBAR_SIZE / 2) *
               FR((double)maximum)) /
            denominator));
        if (position < 0) position = 0;
        if (position > maximum) position = maximum;
        qa_ui_list_set_start_position(&list, position);
        capture->x_start = rt->display_cursor_x;
      }
    } else if (rt->display_cursor_y != capture->y_start) {
      double height = FR(FR(rect.height - SCROLLBAR_SIZE * 2) - 2);
      double start = FR(FR(rect.y + SCROLLBAR_SIZE) + 1);
      int32_t maximum = list_maximum(rt, capture->item);
      double denominator = FR(height - SCROLLBAR_SIZE);
      if (!has_list)
        qa_ui_script_plain_error(
            "Scroll_ListBox_ThumbFunc dereferences NULL list data");
      int32_t position = qa_qvm_float_to_int(FR(
          FR(FR(FR(rt->display_cursor_y - start) - SCROLLBAR_SIZE / 2) *
             FR((double)maximum)) /
          denominator));
      if (position < 0) position = 0;
      if (position > maximum) position = maximum;
      qa_ui_list_set_start_position(&list, position);
      capture->y_start = rt->display_cursor_y;
    }
  }
  if (rt->real_time > rt->next_scroll_time) {
    (void)handle_list_key(rt, capture->item, capture->key, false);
    rt->next_scroll_time = i32_add(rt->real_time, rt->scroll_adjust_value);
  }
  if (rt->real_time > rt->next_scroll_adjust_time) {
    rt->next_scroll_adjust_time = i32_add(rt->real_time, 150);
    if (rt->scroll_adjust_value > 20) rt->scroll_adjust_value -= 40;
  }
}

/* ---- painting ---- */

static void fade(runtime_t *rt, qa_ui_window_definition *window, bool fore,
                 double clamp, double cycle, bool clear_flags,
                 double amount) {
  int32_t flags = qa_ui_window_flags(window);
  if ((flags & (QA_UI_WINDOW_FADING_OUT | QA_UI_WINDOW_FADING_IN)) == 0 ||
      rt->real_time <= qa_ui_window_next_time(window))
    return;
  qa_ui_window_set_next_time(window,
                             i32_add(rt->real_time, qa_int32(cycle)));
  qa_vec4 color = fore ? qa_ui_window_fore_color(window)
                       : qa_ui_window_back_color(window);
  if ((flags & QA_UI_WINDOW_FADING_OUT) != 0) {
    color.w = FR(color.w - amount);
    if (fore) qa_ui_window_set_fore_color(window, color);
    else qa_ui_window_set_back_color(window, color);
    if (clear_flags && color.w <= 0)
      qa_ui_window_set_flags(
          window, qa_ui_window_flags(window) &
                      ~(QA_UI_WINDOW_FADING_OUT | QA_UI_WINDOW_VISIBLE));
  } else {
    color.w = FR(color.w + amount);
    if (color.w >= clamp) {
      color.w = FR(clamp);
      if (fore) qa_ui_window_set_fore_color(window, color);
      else qa_ui_window_set_back_color(window, color);
      if (clear_flags)
        qa_ui_window_set_flags(window, qa_ui_window_flags(window) &
                                           ~QA_UI_WINDOW_FADING_IN);
    } else {
      if (fore) qa_ui_window_set_fore_color(window, color);
      else qa_ui_window_set_back_color(window, color);
    }
  }
}

static void draw_rect_borders(runtime_t *rt, const qa_draw2d *draw,
                              qa_rect rect, double size, qa_vec4 color) {
  qa_ui_draw_cg_rect(draw, rect, size, color,
                     &rt->options.widget_assets.white_shader);
}

static void gradient(runtime_t *rt, const qa_draw2d *draw, qa_rect rect,
                     qa_vec4 color) {
  (void)rt;
  qa_draw2d_set_color(draw, &color);
  qa_draw2d_draw_handle_pic(draw, rect,
                            &rt->options.widget_assets.gradient_bar);
  qa_draw2d_set_color(draw, NULL);
}

static void paint_cinematic(runtime_t *rt, qa_ui_window_definition *window,
                            qa_ui_window_definition *definition, qa_rect rect,
                            const qa_draw2d *draw) {
  if (qa_ui_window_cinematic_handle(window) == -1) {
    char *path = qa_ui_window_cinematic(definition);
    if (path == NULL)
      qa_ui_script_plain_error("cinematic window has no cinematic path");
    qa_ui_cinematic_asset asset = cinematic_asset(rt, path);
    opened(rt);
    qa_ui_cinematic_instance instance;
    bool has = rt->options.cinematics.play(rt->options.cinematics.ctx, asset,
                                            rect, &instance);
    qa_ui_window_set_cinematic_handle(
        window, has ? (int32_t)instance.handle_index : -2);
    free(path);
  }
  int32_t handle = qa_ui_window_cinematic_handle(window);
  if (handle >= 0) {
    rt->options.cinematics.run(rt->options.cinematics.ctx, handle,
                               rt->real_time);
    rt->options.cinematics.draw(rt->options.cinematics.ctx, handle, rect,
                                draw);
  }
}

static void paint_window(runtime_t *rt, qa_ui_window_definition *window,
                         qa_ui_window_definition *definition,
                         double fade_amount, double fade_clamp,
                         double fade_cycle, const qa_draw2d *draw) {
  if (rt->debug) {
    qa_vec4 white = {1, 1, 1, 1};
    draw_rect_borders(rt, draw, qa_ui_window_rect(window), 1, white);
  }
  int32_t style = qa_ui_window_style(definition);
  int32_t border = qa_ui_window_border(definition);
  if (style == 0 && border == 0) return;
  qa_rect fill = copy_rect(qa_ui_window_rect(window));
  double border_size = qa_ui_window_border_size(definition);
  if (border != 0) {
    fill.x = FR(fill.x + border_size);
    fill.y = FR(fill.y + border_size);
    fill.width = FR(fill.width - FR(border_size + 1));
    fill.height = FR(fill.height - FR(border_size + 1));
  }
  bool has_team_color = false;
  qa_vec4 team_color = {0, 0, 0, 0};
  handle_ctx storage;
  if (style == 1) {
    if (has_window_background(rt, window)) {
      fade(rt, window, false, fade_clamp, fade_cycle, true, fade_amount);
      qa_vec4 back = qa_ui_window_back_color(window);
      qa_draw2d_set_color(draw, &back);
      draw_pic(draw, fill, background_or_zero(rt, window, &storage));
      qa_draw2d_set_color(draw, NULL);
    } else {
      qa_draw2d_fill_rect(draw, fill, qa_ui_window_back_color(window),
                          &rt->options.widget_assets.white_shader);
    }
  } else if (style == 2) {
    gradient(rt, draw, fill, qa_ui_window_back_color(window));
  } else if (style == 3) {
    if ((qa_ui_window_flags(window) & QA_UI_WINDOW_FORE_COLOR_SET) != 0) {
      qa_vec4 fore = qa_ui_window_fore_color(window);
      qa_draw2d_set_color(draw, &fore);
    }
    draw_pic(draw, fill, background_or_zero(rt, window, &storage));
    qa_draw2d_set_color(draw, NULL);
  } else if (style == 4) {
    team_color = copy_color(
        rt->options.get_team_color(rt->options.get_team_color_ctx));
    has_team_color = true;
    qa_draw2d_fill_rect(draw, fill, team_color,
                        &rt->options.widget_assets.white_shader);
  } else if (style == 5) {
    paint_cinematic(rt, window, definition, fill, draw);
    opened(rt);
  }
  qa_rect rect = qa_ui_window_rect(window);
  if (border == 1) {
    if (style == 4 && has_team_color) {
      qa_vec4 color =
          team_color.x > 0 ? (qa_vec4){1, 0.5, 0.5, 1} : (qa_vec4){0.5, 0.5, 1, 1};
      draw_rect_borders(rt, draw, rect, border_size, color);
    } else {
      draw_rect_borders(rt, draw, rect, border_size,
                        qa_ui_window_border_color(window));
    }
  } else if (border == 2) {
    qa_vec4 color = qa_ui_window_border_color(window);
    qa_draw2d_set_color(draw, &color);
    qa_ui_draw_cg_top_bottom(draw, rect, border_size,
                             &rt->options.widget_assets.white_shader);
    qa_draw2d_set_color(draw, NULL);
  } else if (border == 3) {
    qa_vec4 color = qa_ui_window_border_color(window);
    qa_draw2d_set_color(draw, &color);
    qa_ui_draw_cg_sides(draw, rect, border_size,
                        &rt->options.widget_assets.white_shader);
    qa_draw2d_set_color(draw, NULL);
  } else if (border == 4) {
    qa_rect top = rect;
    top.height = border_size;
    gradient(rt, draw, top, qa_ui_window_border_color(window));
    qa_rect bottom = rect;
    bottom.y = FR(FR(rect.y + rect.height) - 1);
    bottom.height = border_size;
    gradient(rt, draw, bottom, qa_ui_window_border_color(window));
  }
}

static qa_vec4 lerp_color(runtime_t *rt, qa_vec4 color, qa_vec4 low_light) {
  int32_t steps = rt->real_time / PULSE_DIVISOR; /* truncates toward 0 */
  double amount = FR(0.5 + FR(0.5 * FR(sin(FR((double)steps)))));
  double components[4];
  double from[4] = {color.x, color.y, color.z, color.w};
  double to[4] = {low_light.x, low_light.y, low_light.z, low_light.w};
  for (int i = 0; i < 4; i++) {
    double value = FR(from[i] + FR(amount * FR(to[i] - from[i])));
    components[i] = value < 0 ? 0 : value > 1 ? 1 : value;
  }
  qa_vec4 out = {components[0], components[1], components[2], components[3]};
  return out;
}

static qa_vec4 pulse(runtime_t *rt, qa_vec4 color) {
  qa_vec4 low = {FR(FR(0.8) * color.x), FR(FR(0.8) * color.y),
                 FR(FR(0.8) * color.z), FR(FR(0.8) * color.w)};
  return lerp_color(rt, color, low);
}

static void text_paint(runtime_t *rt, const qa_draw2d *draw, double x,
                       double y, double scale, qa_vec4 color,
                       const char *text, double adjust, double limit,
                       int32_t style) {
  qa_q3_text_paint_options options;
  options.x = x;
  options.y = y;
  options.scale = scale;
  options.color = color;
  options.text = text;
  options.adjust = adjust;
  options.limit = limit;
  options.style = style;
  qa_q3_text_paint(draw, &rt->options.fonts, &options);
}

static double text_width_of(runtime_t *rt, const char *text, double scale) {
  return qa_q3_text_width(&rt->options.fonts, text, scale, 0);
}

static void text_extents(runtime_t *rt, item_state *item, const char *text) {
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  qa_ui_item_behavior_kind kind = qa_ui_item_behavior(item->def);
  int32_t alignment = qa_ui_item_text_alignment(item->def);
  if (qa_qvm_float_to_int(text_rect.width) != 0 &&
      !(kind == QA_UI_BEHAVIOR_OWNER_DRAW && alignment == 1))
    return;
  double scale = qa_ui_item_text_scale(item->def);
  char *item_text = qa_ui_item_text(item->def);
  double original_width = text_width_of(rt, item_text != NULL ? item_text : "",
                                        scale);
  free(item_text);
  if (kind == QA_UI_BEHAVIOR_OWNER_DRAW &&
      (alignment == 1 || alignment == 2)) {
    original_width = (double)qa_int32(
        original_width +
        rt->options.owner_draw.width(
            rt->options.owner_draw.ctx,
            qa_ui_window_owner_draw(&item->def->window), scale));
  } else if (kind == QA_UI_BEHAVIOR_EDIT_FIELD && alignment == 1) {
    char *cvar = qa_ui_item_cvar(item->def);
    if (cvar != NULL) {
      char *buffer = cvar_buffer(rt, cvar);
      if (strlen(buffer) > 255) buffer[255] = '\0';
      original_width = (double)qa_int32(original_width +
                                        text_width_of(rt, buffer, scale));
      free(buffer);
    }
    free(cvar);
  }
  double width = text_width_of(rt, text, scale);
  double height = qa_q3_text_height(&rt->options.fonts, text, scale, 0);
  qa_ui_item_set_text_rect_component(item->def, 2, FR(width));
  qa_ui_item_set_text_rect_component(item->def, 3, FR(height));
  double align_x = qa_ui_item_text_align_x(item->def);
  double align_y = qa_ui_item_text_align_y(item->def);
  qa_ui_item_set_text_rect_component(item->def, 0, align_x);
  qa_ui_item_set_text_rect_component(item->def, 1, align_y);
  if (alignment == 2) {
    qa_ui_item_set_text_rect_component(item->def, 0,
                                       FR(align_x - original_width));
  } else if (alignment == 1) {
    qa_ui_item_set_text_rect_component(
        item->def, 0, FR(align_x - trunc(original_width / 2)));
  }
  qa_rect updated = qa_ui_item_text_rect(item->def);
  if (qa_ui_window_border(&item->def->window) != 0) {
    double border_size = qa_ui_window_border_size(&item->def->window);
    qa_ui_item_set_text_rect_component(item->def, 0,
                                       FR(updated.x + border_size));
    qa_ui_item_set_text_rect_component(item->def, 1,
                                       FR(updated.y + border_size));
    updated = qa_ui_item_text_rect(item->def);
  }
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  qa_ui_item_set_text_rect_component(item->def, 0, FR(updated.x + rect.x));
  qa_ui_item_set_text_rect_component(item->def, 1, FR(updated.y + rect.y));
}

static qa_vec4 item_text_color(runtime_t *rt, item_state *item) {
  menu_state *menu = menu_for(rt, item);
  fade(rt, &item->def->window, true, qa_ui_menu_fade_clamp(menu->def),
       qa_ui_menu_fade_cycle(menu->def), true,
       qa_ui_menu_fade_amount(menu->def));
  qa_vec4 color = copy_color(qa_ui_window_fore_color(&item->def->window));
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if ((flags & QA_UI_WINDOW_HAS_FOCUS) != 0) {
    color = pulse(rt, qa_ui_menu_focus_color(menu->def));
  } else if (qa_ui_item_text_style(item->def) == 1 &&
             ((rt->real_time / BLINK_DIVISOR) & 1) == 0) {
    color = pulse(rt, qa_ui_window_fore_color(&item->def->window));
  }
  if (!item_passes_cvar(rt, item, 1))
    return copy_color(qa_ui_menu_disable_color(menu->def));
  return color;
}

static qa_vec4 value_color(runtime_t *rt, item_state *item) {
  if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HAS_FOCUS) != 0)
    return pulse(rt, qa_ui_menu_focus_color(menu_for(rt, item)->def));
  return copy_color(qa_ui_window_fore_color(&item->def->window));
}

/* Malloc'd TS textValue; NULL is TS undefined. */
static char *text_value(runtime_t *rt, item_state *item) {
  char *text = qa_ui_item_text(item->def);
  if (text != NULL) return text;
  char *cvar = qa_ui_item_cvar(item->def);
  if (cvar == NULL) return NULL;
  char *out = cvar_buffer(rt, cvar);
  free(cvar);
  return out;
}

static void paint_auto_wrapped(runtime_t *rt, item_state *item,
                               const qa_draw2d *draw, const char *text,
                               qa_vec4 color) {
  char buffer[1025];
  buffer[0] = '\0';
  int64_t length = 0, position = 0;
  double width = 0;
  int64_t line_break = 0, next_line = 0;
  double line_width = 0;
  double y = qa_ui_item_text_align_y(item->def);
  double scale = qa_ui_item_text_scale(item->def);
  for (;;) {
    char character = text[position];
    if (character == ' ' || character == '\t' || character == '\n' ||
        character == '\0') {
      line_break = length;
      next_line = position + 1;
      line_width = width;
    }
    width = text_width_of(rt, buffer, scale);
    qa_rect window_rect = qa_ui_window_rect(&item->def->window);
    if ((line_break != 0 && width > window_rect.width) ||
        character == '\n' || character == '\0') {
      if (length != 0) {
        qa_rect text_rect = qa_ui_item_text_rect(item->def);
        double x = text_rect.x;
        int32_t alignment = qa_ui_item_text_alignment(item->def);
        double align_x = qa_ui_item_text_align_x(item->def);
        if (alignment == 0) x = align_x;
        else if (alignment == 2) x = FR(align_x - line_width);
        else if (alignment == 1) x = FR(align_x - trunc(line_width / 2));
        double baseline = y;
        if (qa_ui_window_border(&item->def->window) != 0) {
          double border_size = qa_ui_window_border_size(&item->def->window);
          x = FR(x + border_size);
          baseline = FR(baseline + border_size);
        }
        qa_ui_item_set_text_rect_component(item->def, 0,
                                           FR(x + window_rect.x));
        qa_ui_item_set_text_rect_component(item->def, 1,
                                           FR(baseline + window_rect.y));
        buffer[line_break] = '\0';
        qa_rect placed = qa_ui_item_text_rect(item->def);
        text_paint(rt, draw, placed.x, placed.y, scale, color, buffer, 0, 0,
                   qa_ui_item_text_style(item->def));
      }
      if (character == '\0') return;
      qa_rect text_rect = qa_ui_item_text_rect(item->def);
      y = FR(y + FR((double)i32_add(
                      qa_qvm_float_to_int(text_rect.height), 5)));
      position = next_line;
      length = 0;
      line_break = 0;
      line_width = 0;
    } else {
      if (length >= 1023)
        qa_range_error(
            "Item_Text_AutoWrapped_Paint writes outside local char[1024]");
      buffer[length] = character;
      length++;
      buffer[length] = '\0';
      position++;
    }
  }
}

static void paint_text(runtime_t *rt, item_state *item,
                       const qa_draw2d *draw) {
  char *text = text_value(rt, item);
  if (text == NULL) return;
  qa_vec4 color;
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if ((flags & (QA_UI_WINDOW_WRAPPED | QA_UI_WINDOW_AUTO_WRAPPED)) != 0) {
    if (text[0] == '\0') {
      free(text);
      return;
    }
    color = item_text_color(rt, item);
    text_extents(rt, item, text);
  } else {
    text_extents(rt, item, text);
    if (text[0] == '\0') {
      free(text);
      return;
    }
    color = item_text_color(rt, item);
  }
  double scale = qa_ui_item_text_scale(item->def);
  int32_t style = qa_ui_item_text_style(item->def);
  if ((flags & QA_UI_WINDOW_WRAPPED) != 0) {
    qa_rect text_rect = qa_ui_item_text_rect(item->def);
    double y = text_rect.y;
    /* Split on '\r' like TS text.split("\r"). */
    const char *line = text;
    for (;;) {
      const char *next = strchr(line, '\r');
      size_t line_length = next == NULL ? strlen(line) : (size_t)(next - line);
      if (next != NULL && line_length >= 1024) {
        free(text);
        qa_range_error(
            "Item_Text_Wrapped_Paint writes outside local char[1024]");
      }
      char *segment = dupnstr(line, line_length);
      text_paint(rt, draw, text_rect.x, y, scale, color, segment, 0, 0,
                 style);
      free(segment);
      qa_rect current = qa_ui_item_text_rect(item->def);
      y = FR(y + FR((double)i32_add(qa_qvm_float_to_int(current.height), 5)));
      if (next == NULL) break;
      line = next + 1;
    }
    free(text);
    return;
  }
  if ((flags & QA_UI_WINDOW_AUTO_WRAPPED) != 0) {
    paint_auto_wrapped(rt, item, draw, text, color);
    free(text);
    return;
  }
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  text_paint(rt, draw, text_rect.x, text_rect.y, scale, color, text, 0, 0,
             style);
  free(text);
}

static void paint_text_field(runtime_t *rt, item_state *item,
                             const qa_draw2d *draw) {
  qa_ui_edit_field_definition edit;
  bool has_edit = qa_ui_item_edit_data(item->def, &edit);
  paint_text(rt, item, draw);
  char *cvar = qa_ui_item_cvar(item->def);
  char *value = cvar == NULL ? dupstr("") : cvar_buffer(rt, cvar);
  free(cvar);
  qa_vec4 color = value_color(rt, item);
  char *item_text = qa_ui_item_text(item->def);
  double offset =
      item_text != NULL && item_text[0] != '\0' ? 8 : 0;
  free(item_text);
  if (!has_edit) {
    free(value);
    qa_ui_script_plain_error(
        "Item_TextField_Paint dereferences NULL edit data");
  }
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  int32_t paint_offset = qa_ui_edit_paint_offset(&edit);
  const char *shown = value;
  if (paint_offset > 0) {
    size_t length = strlen(value);
    shown = (size_t)paint_offset <= length ? value + paint_offset
                                            : value + length;
  }
  qa_q3_text_paint_options options;
  options.x = FR(FR(text_rect.x + text_rect.width) + offset);
  options.y = text_rect.y;
  options.scale = qa_ui_item_text_scale(item->def);
  options.color = color;
  options.text = shown;
  options.adjust = 0;
  options.limit = qa_ui_edit_max_paint_chars(&edit);
  options.style = qa_ui_item_text_style(item->def);
  qa_q3_text_paint(draw, &rt->options.fonts, &options);
  free(value);
}

static void paint_yes_no(runtime_t *rt, item_state *item,
                         const qa_draw2d *draw) {
  char *cvar = qa_ui_item_cvar(item->def);
  double value = cvar == NULL ? 0 : cvar_value(rt, cvar);
  free(cvar);
  qa_vec4 color = value_color(rt, item);
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  double x = text_rect.x;
  char *item_text = qa_ui_item_text(item->def);
  if (item_text != NULL) {
    paint_text(rt, item, draw);
    text_rect = qa_ui_item_text_rect(item->def);
    x = FR(FR(text_rect.x + text_rect.width) + 8);
  }
  free(item_text);
  text_paint(rt, draw, x, text_rect.y, qa_ui_item_text_scale(item->def),
             color, value != 0 ? "Yes" : "No", 0, 0,
             qa_ui_item_text_style(item->def));
}

/* Malloc'd TS multiSetting; NULL is TS undefined label. */
static char *multi_setting(runtime_t *rt, item_state *item, bool *has) {
  *has = true;
  char *cvar = qa_ui_item_cvar(item->def);
  qa_ui_multi_definition multi;
  bool has_multi = qa_ui_item_multi_data(item->def, &multi);
  if (cvar == NULL || qa_ui_item_behavior(item->def) != QA_UI_BEHAVIOR_MULTI ||
      !has_multi) {
    free(cvar);
    return dupstr("");
  }
  int32_t count = qa_ui_multi_count(&multi);
  char *result = NULL;
  bool matched = false;
  if (qa_ui_multi_string_definition(&multi)) {
    char *current = cvar_buffer(rt, cvar);
    for (int32_t i = 0; i < count; i++) {
      char *candidate = qa_ui_multi_string_value(&multi, i);
      bool match = equal_name(candidate, current);
      free(candidate);
      if (match) {
        matched = true;
        result = qa_ui_multi_label(&multi, i);
        break;
      }
    }
    free(current);
  } else {
    double current = cvar_value(rt, cvar);
    for (int32_t i = 0; i < count; i++) {
      if (qa_ui_multi_number_value(&multi, i) == current) {
        matched = true;
        result = qa_ui_multi_label(&multi, i);
        break;
      }
    }
  }
  free(cvar);
  if (!matched) return dupstr("");
  if (result == NULL) *has = false; /* matched label is TS undefined */
  return result;
}

static void paint_multi(runtime_t *rt, item_state *item,
                        const qa_draw2d *draw) {
  qa_vec4 color = value_color(rt, item);
  bool has = true;
  char *text = multi_setting(rt, item, &has);
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  double x = text_rect.x;
  char *item_text = qa_ui_item_text(item->def);
  if (item_text != NULL) {
    paint_text(rt, item, draw);
    text_rect = qa_ui_item_text_rect(item->def);
    x = FR(FR(text_rect.x + text_rect.width) + 8);
  }
  free(item_text);
  if (!has) {
    free(text);
    return; /* TS: text === undefined */
  }
  text_paint(rt, draw, x, text_rect.y, qa_ui_item_text_scale(item->def),
             color, text, 0, 0, qa_ui_item_text_style(item->def));
  free(text);
}

static void paint_bind(runtime_t *rt, item_state *item,
                       const qa_draw2d *draw) {
  qa_ui_edit_field_definition edit;
  bool has_edit = qa_ui_item_edit_data(item->def, &edit);
  int32_t max_chars = has_edit ? qa_ui_edit_max_paint_chars(&edit) : 0;
  char *cvar = qa_ui_item_cvar(item->def);
  if (cvar != NULL) (void)cvar_value(rt, cvar);
  free(cvar);
  qa_vec4 color = value_color(rt, item);
  qa_rect text_rect = qa_ui_item_text_rect(item->def);
  double x = text_rect.x;
  /* "FIXME" is id's literal in Item_Bind_Paint (ui_shared.c) for a bind item
     without text; it is not a leftover. */
  char *text = dupstr("FIXME");
  char *item_text = qa_ui_item_text(item->def);
  if (item_text != NULL) {
    paint_text(rt, item, draw);
    free(text);
    text = dupstr("???");
    text_rect = qa_ui_item_text_rect(item->def);
    x = FR(FR(text_rect.x + text_rect.width) + 8);
  }
  free(item_text);
  text_paint(rt, draw, x, text_rect.y, qa_ui_item_text_scale(item->def),
             color, text, 0, max_chars, qa_ui_item_text_style(item->def));
  free(text);
}

static void paint_slider(runtime_t *rt, item_state *item,
                         const qa_draw2d *draw) {
  char *cvar = qa_ui_item_cvar(item->def);
  if (cvar != NULL) (void)cvar_value(rt, cvar);
  free(cvar);
  qa_vec4 color = value_color(rt, item);
  qa_rect window_rect = qa_ui_window_rect(&item->def->window);
  double y = window_rect.y;
  double x;
  char *item_text = qa_ui_item_text(item->def);
  if (item_text != NULL) {
    paint_text(rt, item, draw);
    qa_rect text_rect = qa_ui_item_text_rect(item->def);
    x = FR(FR(text_rect.x + text_rect.width) + 8);
  } else {
    x = window_rect.x;
  }
  free(item_text);
  qa_draw2d_set_color(draw, &color);
  handle_ctx storage1, storage2;
  qa_rect bar = {x, y, SLIDER_WIDTH, 16};
  draw_pic(draw, bar,
           widget_picture(rt, rt->options.widget_assets.slider_bar,
                          &storage1));
  x = slider_thumb_position(rt, item);
  qa_rect thumb = {FR(x - SLIDER_THUMB_WIDTH / 2), FR(y - 2),
                   SLIDER_THUMB_WIDTH, 20};
  draw_pic(draw, thumb,
           widget_picture(rt, rt->options.widget_assets.slider_thumb,
                          &storage2));
}

static void paint_model(runtime_t *rt, item_state *item,
                        const qa_draw2d *draw) {
  qa_ui_model_definition model_data;
  if (!qa_ui_item_model_data(item->def, &model_data)) return;
  int32_t rotation_speed = qa_ui_model_rotation_speed(&model_data);
  if (rotation_speed != 0 &&
      rt->real_time > qa_ui_window_next_time(&item->def->window)) {
    qa_ui_window_set_next_time(&item->def->window,
                               i32_add(rt->real_time, rotation_speed));
    int32_t angle =
        i32_add(qa_ui_model_angle(&model_data), 1) % 360;
    qa_ui_model_set_angle(&model_data, angle);
  }
  int32_t handle;
  bool has_handle = qa_ui_item_asset_handle(item->def, &handle);
  qa_q3_pres_scene_model model;
  bool has_model = false;
  const char *asset_path = NULL;
  if (!has_handle) {
    qa_ui_resource_reference *asset = qa_ui_item_asset(item->def);
    if (asset != NULL && asset->kind == QA_UI_RESOURCE_MODEL) {
      asset_path = asset->path;
      has_model = models_get(rt, asset->path, &model);
    }
  } else if (handle == 0 &&
             rt->options.resources.handles.kind ==
                 QA_UI_HANDLES_DIAGNOSTIC) {
    model = qa_q3_pres_default_model;
    has_model = true;
  } else {
    const qa_ui_runtime_handles *handles = source_handles(rt);
    model = handles->model_for_handle(handles->ctx, handle);
    has_model = true;
  }
  if (!has_model)
    qa_ui_script_plain_error(
        "UI model %s was not resolved before runtime use",
        asset_path != NULL ? asset_path : "NULL");
  qa_rect rect = qa_ui_window_rect(&item->def->window);
  qa_ui_model_paint_request request;
  request.draw = draw;
  request.model = model;
  request.rect.x = FR(rect.x + 1);
  request.rect.y = FR(rect.y + 1);
  request.rect.width = FR(rect.width - 2);
  request.rect.height = FR(rect.height - 2);
  request.time = rt->real_time;
  request.angle = qa_ui_model_angle(&model_data);
  request.field_of_view_x = qa_ui_model_field_of_view_x(&model_data);
  request.field_of_view_y = qa_ui_model_field_of_view_y(&model_data);
  rt->options.paint_model(rt->options.paint_model_ctx, &request);
}

static void paint_owner_draw(runtime_t *rt, item_state *item,
                             const qa_draw2d *draw) {
  menu_state *menu = menu_for(rt, item);
  fade(rt, &item->def->window, true, qa_ui_menu_fade_clamp(menu->def),
       qa_ui_menu_fade_cycle(menu->def), true,
       qa_ui_menu_fade_amount(menu->def));
  qa_vec4 color = copy_color(qa_ui_window_fore_color(&item->def->window));
  size_t range_count = qa_ui_item_color_range_count(item->def);
  if (range_count > 0) {
    double value = rt->options.owner_draw.value(
        rt->options.owner_draw.ctx,
        qa_ui_window_owner_draw(&item->def->window));
    for (size_t i = 0; i < range_count; i++) {
      qa_ui_color_range range = qa_ui_item_color_range_at(item->def, i);
      if (value >= range.low && value <= range.high) {
        color = copy_color(range.color);
        break;
      }
    }
  }
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if ((flags & QA_UI_WINDOW_HAS_FOCUS) != 0) {
    color = pulse(rt, qa_ui_menu_focus_color(menu->def));
  } else if (qa_ui_item_text_style(item->def) == 1 &&
             ((rt->real_time / BLINK_DIVISOR) & 1) == 0) {
    color = pulse(rt, qa_ui_window_fore_color(&item->def->window));
  }
  if (!item_passes_cvar(rt, item, 1))
    color = copy_color(qa_ui_menu_disable_color(menu->def));
  qa_rect rect = copy_rect(qa_ui_window_rect(&item->def->window));
  double text_x = qa_ui_item_text_align_x(item->def);
  char *item_text = qa_ui_item_text(item->def);
  if (item_text != NULL) {
    paint_text(rt, item, draw);
    qa_rect text_rect = qa_ui_item_text_rect(item->def);
    rect.x = FR(FR(text_rect.x + text_rect.width) +
                (item_text[0] != '\0' ? 8 : 0));
    text_x = 0;
  }
  free(item_text);
  handle_ctx storage;
  qa_ui_owner_draw_paint_request request;
  request.draw = draw;
  request.rect = rect;
  request.text_x = text_x;
  request.text_y = qa_ui_item_text_align_y(item->def);
  request.owner_draw = qa_ui_window_owner_draw(&item->def->window);
  request.owner_draw_flags =
      qa_ui_window_owner_draw_flags(&item->def->window);
  request.alignment = qa_ui_item_alignment(item->def);
  request.special = qa_ui_item_special(item->def);
  request.text_scale = qa_ui_item_text_scale(item->def);
  request.color = color;
  request.background = window_background(rt, &item->def->window, &storage);
  request.text_style = qa_ui_item_text_style(item->def);
  rt->options.owner_draw.paint(rt->options.owner_draw.ctx, &request);
}

static void paint_list(runtime_t *rt, item_state *item,
                       const qa_draw2d *draw) {
  qa_ui_list_box_definition list;
  bool has_list = qa_ui_item_list_data(item->def, &list);
  double special = qa_ui_item_special(item->def);
  double count = FR(rt->options.feeder.count(rt->options.feeder.ctx, special));
  qa_rect window_rect = qa_ui_window_rect(&item->def->window);
  double x, y, size;
  if ((qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_HORIZONTAL) !=
      0) {
    x = FR(window_rect.x + 1);
    y = FR(FR(FR(window_rect.y + window_rect.height) - SCROLLBAR_SIZE) - 1);
    qa_rect arrow = {x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
    qa_draw2d_draw_handle_pic(
        draw, arrow, &rt->options.widget_assets.scroll_bar_arrow_left);
    x = FR(x + SCROLLBAR_SIZE - 1);
    size = FR(window_rect.width - SCROLLBAR_SIZE * 2);
    qa_rect bar = {x, y, FR(size + 1), SCROLLBAR_SIZE};
    qa_draw2d_draw_handle_pic(draw, bar,
                              &rt->options.widget_assets.scroll_bar);
    x = FR(x + FR(size - 1));
    qa_rect arrow_right = {x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
    qa_draw2d_draw_handle_pic(
        draw, arrow_right,
        &rt->options.widget_assets.scroll_bar_arrow_right);
    double thumb = FR((double)list_thumb_draw_position(rt, item));
    double thumb_max = FR(FR(x - SCROLLBAR_SIZE) - 1);
    if (thumb > thumb_max) thumb = thumb_max;
    qa_rect thumb_rect = {thumb, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
    qa_draw2d_draw_handle_pic(draw, thumb_rect,
                              &rt->options.widget_assets.scroll_bar_thumb);
    if (!has_list)
      qa_ui_script_plain_error(
          "Item_ListBox_Paint dereferences NULL list data");
    qa_ui_list_set_end_position(&list, qa_ui_list_start_position(&list));
    size = FR(window_rect.width - 2);
    if (qa_ui_list_element_style(&list) != 1) return;
    x = FR(window_rect.x + 1);
    y = FR(window_rect.y + 1);
    for (double i = FR((double)qa_ui_list_start_position(&list)); i < count;
         i = FR(i + 1)) {
      qa_picture_asset picture;
      bool has_picture = rt->options.feeder.image(
          rt->options.feeder.ctx, special, qa_qvm_float_to_int(i), &picture);
      opened(rt);
      if (has_picture) {
        qa_rect cell = {FR(x + 1), FR(y + 1),
                        FR(qa_ui_list_element_width(&list) - 2),
                        FR(qa_ui_list_element_height(&list) - 2)};
        qa_draw2d_draw_handle_pic(draw, cell, &picture);
      }
      if (i == FR((double)qa_ui_item_cursor_position(item->def))) {
        qa_rect outline = {x, y, FR(qa_ui_list_element_width(&list) - 1),
                           FR(qa_ui_list_element_height(&list) - 1)};
        draw_rect_borders(
            rt, draw, outline,
            qa_ui_window_border_size(&item->def->window),
            qa_ui_window_border_color(&item->def->window));
      }
      size = FR(size - qa_ui_list_element_width(&list));
      if (size < qa_ui_list_element_width(&list)) {
        qa_ui_list_set_draw_padding(&list, qa_qvm_float_to_int(size));
        break;
      }
      x = FR(x + qa_ui_list_element_width(&list));
      qa_ui_list_set_end_position(
          &list, i32_add(qa_ui_list_end_position(&list), 1));
    }
    return;
  }
  x = FR(FR(FR(window_rect.x + window_rect.width) - SCROLLBAR_SIZE) - 1);
  y = FR(window_rect.y + 1);
  qa_rect arrow_up = {x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
  qa_draw2d_draw_handle_pic(draw, arrow_up,
                            &rt->options.widget_assets.scroll_bar_arrow_up);
  y = FR(y + SCROLLBAR_SIZE - 1);
  if (!has_list)
    qa_ui_script_plain_error(
        "Item_ListBox_Paint dereferences NULL list data");
  qa_ui_list_set_end_position(&list, qa_ui_list_start_position(&list));
  size = FR(window_rect.height - SCROLLBAR_SIZE * 2);
  qa_rect bar = {x, y, SCROLLBAR_SIZE, FR(size + 1)};
  qa_draw2d_draw_handle_pic(draw, bar, &rt->options.widget_assets.scroll_bar);
  y = FR(y + FR(size - 1));
  qa_rect arrow_down = {x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
  qa_draw2d_draw_handle_pic(
      draw, arrow_down, &rt->options.widget_assets.scroll_bar_arrow_down);
  double thumb = FR((double)list_thumb_draw_position(rt, item));
  double thumb_max = FR(FR(y - SCROLLBAR_SIZE) - 1);
  if (thumb > thumb_max) thumb = thumb_max;
  qa_rect thumb_rect = {x, thumb, SCROLLBAR_SIZE, SCROLLBAR_SIZE};
  qa_draw2d_draw_handle_pic(draw, thumb_rect,
                            &rt->options.widget_assets.scroll_bar_thumb);
  size = FR(window_rect.height - 2);
  x = FR(window_rect.x + 1);
  y = FR(window_rect.y + 1);
  bool image_style = qa_ui_list_element_style(&list) == 1;
  for (double i = FR((double)qa_ui_list_start_position(&list)); i < count;
       i = FR(i + 1)) {
    if (image_style) {
      qa_picture_asset picture;
      bool has_picture = rt->options.feeder.image(
          rt->options.feeder.ctx, special, qa_qvm_float_to_int(i), &picture);
      opened(rt);
      if (has_picture) {
        qa_rect cell = {FR(x + 1), FR(y + 1),
                        FR(qa_ui_list_element_width(&list) - 2),
                        FR(qa_ui_list_element_height(&list) - 2)};
        qa_draw2d_draw_handle_pic(draw, cell, &picture);
      }
      if (i == FR((double)qa_ui_item_cursor_position(item->def))) {
        qa_rect outline = {x, y, FR(qa_ui_list_element_width(&list) - 1),
                           FR(qa_ui_list_element_height(&list) - 1)};
        draw_rect_borders(
            rt, draw, outline,
            qa_ui_window_border_size(&item->def->window),
            qa_ui_window_border_color(&item->def->window));
      }
      qa_ui_list_set_end_position(
          &list, i32_add(qa_ui_list_end_position(&list), 1));
      size = FR(size - qa_ui_list_element_width(&list));
    } else {
      size_t column_count = qa_ui_list_column_count(&list);
      if (column_count > 0) {
        for (size_t column_index = 0; column_index < column_count;
             column_index++) {
          qa_ui_list_column column =
              qa_ui_list_column_at(&list, column_index);
          qa_ui_runtime_feeder_item entry;
          bool has_entry = rt->options.feeder.item(
              rt->options.feeder.ctx, special, qa_qvm_float_to_int(i),
              (double)column_index, &entry);
          opened(rt);
          if (!has_entry) continue;
          if (entry.has_picture) {
            qa_rect cell = {
                FR(FR(x + 4) + FR((double)column.position)),
                FR(FR(y - 1) + FR(qa_ui_list_element_height(&list) / 2)),
                FR((double)column.width), FR((double)column.width)};
            qa_draw2d_draw_handle_pic(draw, cell, &entry.picture);
          } else if (entry.text != NULL) {
            text_paint(rt, draw,
                       FR(FR(x + 4) + FR((double)column.position)),
                       FR(y + qa_ui_list_element_height(&list)),
                       qa_ui_item_text_scale(item->def),
                       qa_ui_window_fore_color(&item->def->window),
                       entry.text, 0, column.max_chars,
                       qa_ui_item_text_style(item->def));
          }
        }
      } else {
        qa_ui_runtime_feeder_item entry;
        bool has_entry = rt->options.feeder.item(
            rt->options.feeder.ctx, special, qa_qvm_float_to_int(i), 0,
            &entry);
        opened(rt);
        if (has_entry && !entry.has_picture && entry.text != NULL) {
          text_paint(rt, draw, FR(x + 4),
                     FR(y + qa_ui_list_element_height(&list)),
                     qa_ui_item_text_scale(item->def),
                     qa_ui_window_fore_color(&item->def->window), entry.text,
                     0, 0, qa_ui_item_text_style(item->def));
        }
      }
      if (i == FR((double)qa_ui_item_cursor_position(item->def))) {
        qa_rect fill = {FR(x + 2), FR(y + 2),
                        FR(FR(window_rect.width - SCROLLBAR_SIZE) - 4),
                        qa_ui_list_element_height(&list)};
        qa_draw2d_fill_rect(draw, fill,
                            qa_ui_window_outline_color(&item->def->window),
                            &rt->options.widget_assets.white_shader);
      }
      size = FR(size - qa_ui_list_element_height(&list));
    }
    bool exhausted = size < qa_ui_list_element_height(&list);
    if (exhausted) {
      qa_ui_list_set_draw_padding(
          &list, qa_qvm_float_to_int(
                     FR(qa_ui_list_element_height(&list) - size)));
      break;
    }
    if (!image_style)
      qa_ui_list_set_end_position(
          &list, i32_add(qa_ui_list_end_position(&list), 1));
    y = FR(y + qa_ui_list_element_height(&list));
  }
}

static double transition_value(double current, double target, double amount,
                               bool *done) {
  if (current == target) {
    *done = true;
    return current;
  }
  double next = current < target ? FR(current + amount) : FR(current - amount);
  if ((current < target && next > target) ||
      (current > target && next < target)) {
    *done = true;
    return target;
  }
  *done = false;
  return next;
}

static void advance_item_animation(runtime_t *rt, item_state *item) {
  int32_t flags = qa_ui_window_flags(&item->def->window);
  if ((flags & QA_UI_WINDOW_ORBITING) != 0 &&
      rt->real_time > qa_ui_window_next_time(&item->def->window)) {
    qa_ui_window_set_next_time(
        &item->def->window,
        i32_add(rt->real_time,
                qa_ui_window_offset_time(&item->def->window)));
    qa_rect client = qa_ui_window_client_rect(&item->def->window);
    qa_rect effects = qa_ui_window_rect_effects(&item->def->window);
    double half_width = FR(client.width / 2);
    double half_height = FR(client.height / 2);
    double rx = FR(FR(client.x + half_width) - effects.x);
    double ry = FR(FR(client.y + half_height) - effects.y);
    double angle = FR(3 * 3.141592653589793 / 180);
    double cosine = FR(cos(angle));
    double sine = FR(sin(angle));
    client.x = FR(FR(FR(FR(rx * cosine) - FR(ry * sine)) + effects.x) -
                  half_width);
    client.y = FR(FR(FR(FR(rx * sine) + FR(ry * cosine)) + effects.y) -
                  half_height);
    qa_ui_window_set_client_rect(&item->def->window, client);
    update_item_position(item);
  }
  flags = qa_ui_window_flags(&item->def->window);
  if ((flags & QA_UI_WINDOW_IN_TRANSITION) != 0 &&
      rt->real_time > qa_ui_window_next_time(&item->def->window)) {
    qa_ui_window_set_next_time(
        &item->def->window,
        i32_add(rt->real_time,
                qa_ui_window_offset_time(&item->def->window)));
    int done = 0;
    qa_rect client = qa_ui_window_client_rect(&item->def->window);
    qa_rect target = qa_ui_window_rect_effects(&item->def->window);
    qa_rect amounts = qa_ui_window_rect_effects2(&item->def->window);
    double *current[4] = {&client.x, &client.y, &client.width,
                          &client.height};
    double targets[4] = {target.x, target.y, target.width, target.height};
    double steps[4] = {amounts.x, amounts.y, amounts.width, amounts.height};
    for (int i = 0; i < 4; i++) {
      if (*current[i] == targets[i]) {
        done++;
        continue;
      }
      bool step_done = false;
      *current[i] = transition_value(*current[i], targets[i], steps[i],
                                     &step_done);
      if (step_done) done++;
    }
    qa_ui_window_set_client_rect(&item->def->window, client);
    update_item_position(item);
    if (done == 4)
      qa_ui_window_set_flags(&item->def->window,
                             qa_ui_window_flags(&item->def->window) &
                                 ~QA_UI_WINDOW_IN_TRANSITION);
  }
}

static void paint_item(runtime_t *rt, item_state *item,
                       const qa_draw2d *draw) {
  menu_state *menu = parent_menu(rt, item);
  advance_item_animation(rt, item);
  int32_t owner_draw_flags = qa_ui_window_owner_draw_flags(&item->def->window);
  if (owner_draw_flags != 0) {
    int32_t flags = qa_ui_window_flags(&item->def->window);
    if (rt->options.owner_draw.visible(rt->options.owner_draw.ctx,
                                       owner_draw_flags))
      flags |= QA_UI_WINDOW_VISIBLE;
    else
      flags &= ~QA_UI_WINDOW_VISIBLE;
    qa_ui_window_set_flags(&item->def->window, flags);
  }
  if (!item_passes_cvar(rt, item, 4) ||
      (qa_ui_window_flags(&item->def->window) & QA_UI_WINDOW_VISIBLE) == 0)
    return;
  if (menu == NULL)
    qa_ui_script_plain_error(
        "Item_Paint dereferences a NULL parent menu at fadeAmount");
  paint_window(rt, &item->def->window, &item->def->window,
               qa_ui_menu_fade_amount(menu->def),
               qa_ui_menu_fade_clamp(menu->def),
               qa_ui_menu_fade_cycle(menu->def), draw);
  opened(rt);
  if (rt->debug) {
    qa_vec4 green = {0, 1, 0, 1};
    draw_rect_borders(rt, draw, corrected_text_rect(item), 1, green);
  }
  switch (qa_ui_item_behavior(item->def)) {
    case QA_UI_BEHAVIOR_OWNER_DRAW: paint_owner_draw(rt, item, draw); break;
    case QA_UI_BEHAVIOR_TEXT:
    case QA_UI_BEHAVIOR_BUTTON: paint_text(rt, item, draw); break;
    case QA_UI_BEHAVIOR_EDIT_FIELD:
    case QA_UI_BEHAVIOR_NUMERIC_FIELD:
      paint_text_field(rt, item, draw);
      break;
    case QA_UI_BEHAVIOR_LIST_BOX: paint_list(rt, item, draw); break;
    case QA_UI_BEHAVIOR_MODEL: paint_model(rt, item, draw); break;
    case QA_UI_BEHAVIOR_YES_NO: paint_yes_no(rt, item, draw); break;
    case QA_UI_BEHAVIOR_MULTI: paint_multi(rt, item, draw); break;
    case QA_UI_BEHAVIOR_BIND: paint_bind(rt, item, draw); break;
    case QA_UI_BEHAVIOR_SLIDER: paint_slider(rt, item, draw); break;
    default: break;
  }
}

static void paint_menu(runtime_t *rt, menu_state *menu,
                       const qa_draw2d *draw, bool force) {
  if ((qa_ui_window_flags(&menu->def->window) & QA_UI_WINDOW_VISIBLE) == 0 &&
      !force)
    return;
  int32_t owner_draw_flags = qa_ui_window_owner_draw_flags(&menu->def->window);
  if (owner_draw_flags != 0 &&
      !rt->options.owner_draw.visible(rt->options.owner_draw.ctx,
                                      owner_draw_flags))
    return;
  if (force)
    qa_ui_window_set_flags(&menu->def->window,
                           qa_ui_window_flags(&menu->def->window) |
                               QA_UI_WINDOW_FORCED);
  if (qa_ui_menu_full_screen(menu->def) != 0) {
    handle_ctx storage;
    qa_rect full = {0, 0, 640, 480};
    draw_pic(draw, full,
             background_or_zero(rt, &menu->def->window, &storage));
  }
  paint_window(rt, &menu->def->window, &menu->def->window,
               qa_ui_menu_fade_amount(menu->def),
               qa_ui_menu_fade_clamp(menu->def),
               qa_ui_menu_fade_cycle(menu->def), draw);
  opened(rt);
  MENU_ITEMS(rt, menu, item, ii) { paint_item(rt, item, draw); }
  if (rt->debug) {
    qa_vec4 magenta = {1, 0, 1, 1};
    draw_rect_borders(rt, draw, qa_ui_window_rect(&menu->def->window), 1,
                      magenta);
  }
}

static void set_frame_time(runtime_t *rt, qa_ui_runtime_frame frame) {
  if (frame.frame_time != trunc(frame.frame_time) ||
      frame.frame_time < -2147483648.0 || frame.frame_time > 2147483647.0)
    qa_range_error("UI frameTime must be a signed 32-bit value");
  qa_ui_runtime_set_display_time(rt, frame.time);
}

void qa_ui_runtime_run_frame(qa_ui_runtime *rt, qa_ui_runtime_frame frame,
                         double frames_per_second) {
  opened(rt);
  set_frame_time(rt, frame);
  run_capture(rt);
  for (int64_t i = 0; i < rt->active_menu_count; i++)
    paint_menu(rt, live_menu_at(rt, i), frame.draw, false);
  if (rt->debug) {
    char *text = game_format1(
        "fps: %f", qa_q3_game_format_number(FR(frames_per_second)));
    qa_vec4 white = {1, 1, 1, 1};
    text_paint(rt, frame.draw, 5, 25, 0.5, white, text, 0, 0, 0);
    free(text);
  }
}

void qa_ui_runtime_cache_all(qa_ui_runtime *rt) {
  opened(rt);
  for (int64_t mi = 0; mi < rt->active_menu_count; mi++) {
    menu_state *menu = live_menu_at(rt, mi);
    /* menu window, then each item window */
    for (int pass = 0; pass < 2; pass++) {
      if (pass == 0) {
        char *path = qa_ui_window_cinematic(&menu->def->window);
        if (path != NULL) {
          qa_ui_cinematic_asset asset = cinematic_asset(rt, path);
          opened(rt);
          qa_rect zero = {0, 0, 0, 0};
          qa_ui_cinematic_instance instance;
          bool has = rt->options.cinematics.play(rt->options.cinematics.ctx,
                                                  asset, zero, &instance);
          opened(rt);
          rt->options.cinematics.stop(rt->options.cinematics.ctx,
                                      has ? instance.handle_index : -1);
          opened(rt);
          free(path);
        }
      } else {
        MENU_ITEMS(rt, menu, item, ii) {
          char *path = qa_ui_window_cinematic(&item->def->window);
          if (path == NULL) continue;
          qa_ui_cinematic_asset asset = cinematic_asset(rt, path);
          opened(rt);
          qa_rect zero = {0, 0, 0, 0};
          qa_ui_cinematic_instance instance;
          bool has = rt->options.cinematics.play(rt->options.cinematics.ctx,
                                                  asset, zero, &instance);
          opened(rt);
          rt->options.cinematics.stop(rt->options.cinematics.ctx,
                                      has ? instance.handle_index : -1);
          opened(rt);
          free(path);
        }
      }
    }
    char *loop = qa_ui_menu_sound_loop_path(menu->def);
    if (loop != NULL && loop[0] != '\0') {
      qa_pcm_sound sound;
      (void)rt->options.resources.register_sound(rt->options.resources.ctx,
                                                 loop, &sound);
      opened(rt);
    }
    free(loop);
  }
}

bool qa_ui_runtime_paint_named(qa_ui_runtime *rt, const char *name,
                               qa_ui_runtime_frame frame, bool force) {
  opened(rt);
  set_frame_time(rt, frame);
  menu_state *menu = find_menu(rt, name);
  if (menu == NULL) return false;
  paint_menu(rt, menu, frame.draw, force);
  return true;
}

void qa_ui_runtime_paint_captured(qa_ui_runtime *rt,
                                  qa_ui_captured_menu *handle,
                                  qa_ui_runtime_frame frame, bool force) {
  opened(rt);
  set_frame_time(rt, frame);
  paint_menu(rt, captured_menu(rt, handle), frame.draw, force);
}

bool qa_ui_runtime_clear_forced(qa_ui_runtime *rt, const char *name) {
  opened(rt);
  menu_state *menu = find_menu(rt, name);
  if (menu == NULL) return false;
  qa_ui_window_set_flags(&menu->def->window,
                         qa_ui_window_flags(&menu->def->window) &
                             ~QA_UI_WINDOW_FORCED);
  return true;
}

void qa_ui_runtime_clear_captured_forced(qa_ui_runtime *rt,
                                         qa_ui_captured_menu *handle) {
  opened(rt);
  menu_state *menu = captured_menu(rt, handle);
  qa_ui_window_set_flags(&menu->def->window,
                         qa_ui_window_flags(&menu->def->window) &
                             ~QA_UI_WINDOW_FORCED);
}

void qa_ui_runtime_paint_item_image(qa_ui_runtime *rt,
                                    qa_ui_item_definition *item,
                                    const qa_draw2d *draw) {
  opened(rt);
  if (item == NULL) return;
  qa_rect rect = qa_ui_window_rect(&item->window);
  int32_t handle;
  bool has_handle = qa_ui_item_asset_handle(item, &handle);
  qa_ui_picture_or_lazy picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_UI_PICTURE_VALUE;
  picture.picture = rt->options.zero_picture;
  handle_ctx storage;
  if (has_handle && handle != 0) {
    storage.runtime = rt;
    storage.handle = handle;
    picture.kind = QA_UI_PICTURE_LAZY;
    picture.produce = produce_handle_picture;
    picture.produce_ctx = &storage;
  } else if (!has_handle) {
    qa_ui_resource_reference *asset = qa_ui_item_asset(item);
    if (asset != NULL) {
      if (asset->kind != QA_UI_RESOURCE_SHADER) {
        qa_ui_script_plain_error(
            "Item_Image_Paint needs the source numeric handle for a model "
            "asset");
      }
      qa_picture_asset found;
      if (pictures_get(rt, asset->path, &found)) picture.picture = found;
    }
  }
  qa_rect cell = {FR(rect.x + 1), FR(rect.y + 1), FR(rect.width - 2),
                  FR(rect.height - 2)};
  draw_pic(draw, cell, picture);
}

/* ---- snapshot ---- */

static qa_ui_runtime_behavior_snapshot behavior_snapshot(item_state *item) {
  qa_ui_runtime_behavior_snapshot out;
  memset(&out, 0, sizeof out);
  out.kind = QA_UI_SNAPSHOT_OTHER;
  qa_ui_item_behavior_kind kind = qa_ui_item_behavior(item->def);
  if (kind == QA_UI_BEHAVIOR_LIST_BOX) {
    qa_ui_list_box_definition list =
        qa_ui_item_required_list_data(item->def);
    out.kind = QA_UI_SNAPSHOT_LIST_BOX;
    out.start_position = qa_ui_list_start_position(&list);
    out.end_position = qa_ui_list_end_position(&list);
    out.cursor_position = qa_ui_list_cursor_position(&list);
    out.draw_padding = qa_ui_list_draw_padding(&list);
    return out;
  }
  if (kind == QA_UI_BEHAVIOR_EDIT_FIELD ||
      kind == QA_UI_BEHAVIOR_NUMERIC_FIELD || kind == QA_UI_BEHAVIOR_SLIDER ||
      kind == QA_UI_BEHAVIOR_YES_NO || kind == QA_UI_BEHAVIOR_BIND) {
    qa_ui_edit_field_definition edit =
        qa_ui_item_required_edit_data(item->def);
    out.kind = QA_UI_SNAPSHOT_EDIT;
    out.paint_offset = qa_ui_edit_paint_offset(&edit);
    return out;
  }
  if (kind == QA_UI_BEHAVIOR_TEXT) {
    qa_ui_edit_field_definition edit;
    if (qa_ui_item_edit_data(item->def, &edit)) {
      out.kind = QA_UI_SNAPSHOT_EDIT;
      out.paint_offset = qa_ui_edit_paint_offset(&edit);
    }
  }
  return out;
}

qa_ui_runtime_snapshot qa_ui_runtime_take_snapshot(qa_ui_runtime *rt) {
  opened(rt);
  qa_ui_runtime_snapshot out;
  memset(&out, 0, sizeof out);
  menu_state *focused = focused_menu(rt);
  out.focused_menu =
      focused == NULL ? NULL : qa_ui_window_name(&focused->def->window);
  out.open_stack_count = rt->open_count;
  out.open_stack = calloc(rt->open_count > 0 ? rt->open_count : 1,
                          sizeof *out.open_stack);
  for (size_t i = 0; i < rt->open_count; i++)
    out.open_stack[i] = qa_ui_window_name(&rt->open_stack[i]->def->window);
  out.menu_count = (size_t)rt->active_menu_count;
  out.menus = calloc(out.menu_count > 0 ? out.menu_count : 1, sizeof *out.menus);
  for (int64_t mi = 0; mi < rt->active_menu_count; mi++) {
    menu_state *menu = rt->menus[mi];
    qa_ui_runtime_menu_snapshot *menu_out = &out.menus[mi];
    menu_out->name = qa_ui_window_name(&menu->def->window);
    menu_out->flags = qa_ui_window_flags(&menu->def->window);
    menu_out->rect = copy_rect(qa_ui_window_rect(&menu->def->window));
    menu_out->cursor_item = qa_ui_menu_cursor_item(menu->def);
    int32_t item_count = qa_ui_menu_item_count(menu->def);
    size_t count = item_count > 0 ? (size_t)item_count : 0;
    menu_out->item_count = count;
    menu_out->items =
        calloc(count > 0 ? count : 1, sizeof *menu_out->items);
    MENU_ITEMS(rt, menu, item, ii) {
      qa_ui_runtime_item_snapshot *item_out = &menu_out->items[ii];
      item_out->name = qa_ui_window_name(&item->def->window);
      item_out->group = qa_ui_window_group(&item->def->window);
      item_out->flags = qa_ui_window_flags(&item->def->window);
      item_out->rect = copy_rect(qa_ui_window_rect(&item->def->window));
      item_out->client_rect =
          copy_rect(qa_ui_window_client_rect(&item->def->window));
      item_out->fore_color =
          copy_color(qa_ui_window_fore_color(&item->def->window));
      item_out->back_color =
          copy_color(qa_ui_window_back_color(&item->def->window));
      item_out->border_color =
          copy_color(qa_ui_window_border_color(&item->def->window));
      item_out->has_background =
          window_picture(rt, &item->def->window, &item_out->background);
      item_out->cursor_position = qa_ui_item_cursor_position(item->def);
      item_out->special = qa_ui_item_special(item->def);
      item_out->enabled = item_passes_cvar(rt, item, 1);
      item_out->shown = item_passes_cvar(rt, item, 4);
      item_out->behavior = behavior_snapshot(item);
    }
  }
  return out;
}

void qa_ui_runtime_snapshot_destroy(qa_ui_runtime_snapshot *snapshot) {
  if (snapshot == NULL) return;
  free(snapshot->focused_menu);
  for (size_t i = 0; i < snapshot->open_stack_count; i++)
    free(snapshot->open_stack[i]);
  free(snapshot->open_stack);
  for (size_t i = 0; i < snapshot->menu_count; i++) {
    free(snapshot->menus[i].name);
    for (size_t j = 0; j < snapshot->menus[i].item_count; j++) {
      free(snapshot->menus[i].items[j].name);
      free(snapshot->menus[i].items[j].group);
    }
    free(snapshot->menus[i].items);
  }
  free(snapshot->menus);
  memset(snapshot, 0, sizeof *snapshot);
}
