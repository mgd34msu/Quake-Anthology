/* Port of src/content/item-icon.ts. GPL-2.0-or-later. */
#include "qa/content/item_icon.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mounts/paths.h"

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader value = field(reader, name);
  const char *text = qa_save_reader_string(&value);
  qa_save_reader_destroy(&value);
  return text;
}
static char *normalized_field(const qa_save_reader *reader, const char *name) {
  return qa_normalize_resource_path(field_string(reader, name));
}
static const char *read_kind(const qa_save_reader *reader) {
  static const char *const kinds[] = {"image", "wad-picture", "shader"};
  qa_save_reader value = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&value, kinds, 3);
  qa_save_reader_destroy(&value);
  return kind;
}
static bool valid_lump(const char *lump) {
  size_t length = strlen(lump);
  return length != 0 && length <= 16; /* a C string carries no NUL */
}

void qa_content_item_icon_free(qa_source_item_icon_declaration *value) {
  free((void *)(const void *)value->path);
  value->path = NULL;
  free((void *)(const void *)value->lump);
  value->lump = NULL;
  free((void *)(const void *)value->name);
  value->name = NULL;
  value->kind = NULL;
}
void qa_content_weapon_hud_icon_free(qa_weapon_hud_icon *value) {
  free((void *)(const void *)value->resource.path);
  value->resource.path = NULL;
  free((void *)(const void *)value->lump);
  value->lump = NULL;
  free((void *)(const void *)value->name);
  value->name = NULL;
}

bool qa_read_item_icon_declaration(const qa_save_reader *reader,
                                   qa_source_item_icon_declaration *out) {
  memset(out, 0, sizeof *out);
  const char *kind = read_kind(reader);
  out->kind = kind;
  if (strcmp(kind, "shader") == 0) {
    out->name = normalized_field(reader, "name");
    return out->name != NULL;
  }
  out->path = normalized_field(reader, "path");
  if (out->path == NULL) return false;
  if (strcmp(kind, "image") == 0) return true;
  const char *lump = field_string(reader, "lump");
  if (!valid_lump(lump))
    qa_save_reader_fail(reader, "Item icon requires a valid WAD lump name");
  out->lump = copy_text(lump);
  if (out->lump == NULL) {
    qa_content_item_icon_free(out);
    return false;
  }
  return true;
}

qa_weapon_hud_icon qa_resolve_item_icon(qa_source_item_icon_declaration icon,
                                        qa_content_id content) {
  qa_weapon_hud_icon out;
  memset(&out, 0, sizeof out);
  if (strcmp(icon.kind, "shader") == 0) {
    out.kind = QA_WEAPON_HUD_ICON_SHADER;
    out.content = content;
    out.name = icon.name;
    return out;
  }
  out.resource.content = content;
  out.resource.path = icon.path;
  if (strcmp(icon.kind, "image") == 0) {
    out.kind = QA_WEAPON_HUD_ICON_IMAGE;
    return out;
  }
  out.kind = QA_WEAPON_HUD_ICON_WAD_PICTURE;
  out.lump = icon.lump;
  return out;
}

bool qa_read_source_item_icon(const qa_save_reader *reader,
                              qa_content_id content, qa_weapon_hud_icon *out) {
  memset(out, 0, sizeof *out);
  const char *kind = read_kind(reader);
  bool is_shader = strcmp(kind, "shader") == 0;
  qa_save_reader resource_storage;
  const qa_save_reader *resource = reader;
  if (!is_shader) {
    resource_storage = field(reader, "resource");
    resource = &resource_storage;
  }
  const char *owner = field_string(resource, "content");
  if (strcmp(owner, content) != 0) {
    if (!is_shader) qa_save_reader_destroy(&resource_storage);
    qa_save_reader_fail(reader, "Item icon belongs to another content source");
  }
  if (is_shader) {
    out->kind = QA_WEAPON_HUD_ICON_SHADER;
    out->content = content;
    out->name = normalized_field(reader, "name");
    return out->name != NULL;
  }
  char *path = normalized_field(resource, "path");
  qa_save_reader_destroy(&resource_storage);
  if (path == NULL) return false;
  out->resource.content = content;
  out->resource.path = path;
  if (strcmp(kind, "image") == 0) {
    out->kind = QA_WEAPON_HUD_ICON_IMAGE;
    return true;
  }
  const char *lump = field_string(reader, "lump");
  if (!valid_lump(lump))
    qa_save_reader_fail(reader, "Item icon requires a valid WAD lump name");
  out->kind = QA_WEAPON_HUD_ICON_WAD_PICTURE;
  out->lump = copy_text(lump);
  if (out->lump == NULL) {
    qa_content_weapon_hud_icon_free(out);
    return false;
  }
  return true;
}
