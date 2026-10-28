/* Port of src/app/bootstrap/network/unified-content.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_content.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/shared.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = xmalloc(n);
  memcpy(out, s, n);
  return out;
}
static qa_save_value *clone_value(const qa_save_value *value);
static void set_field(qa_save_value *object, const char *key, qa_save_value *child) {
  /* Replace semantics: drop an existing entry with the same key. */
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) == 0) {
      qa_save_value_free(object->entries[i].value);
      object->entries[i].value = child;
      return;
    }
  }
  if (!qa_save_value_set(object, key, child)) qa_range_error("out of memory");
}
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++)
        if (!qa_save_value_push(out, clone_value(value->items[i]))) { qa_save_value_free(out); return NULL; }
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        if (!qa_save_value_set(out, value->entries[i].key, clone_value(value->entries[i].value))) {
          qa_save_value_free(out); return NULL;
        }
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static qa_save_value *object_get(qa_save_value *object, const char *key) {
  return (qa_save_value *)qa_save_value_get(object, key);
}
static const char *text_of(const qa_save_value *value) {
  return value && value->kind == QA_SAVE_STRING ? value->text : NULL;
}
static char *encode_uri(const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  size_t length = strlen(text);
  char *out = xmalloc(length * 3 + 1);
  size_t used = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '.' || c == '!' || c == '~'
        || c == '*' || c == '\'' || c == '(' || c == ')';
    if (keep) out[used++] = (char)c;
    else { out[used++] = '%'; out[used++] = hex[c >> 4]; out[used++] = hex[c & 15]; }
  }
  out[used] = '\0';
  return out;
}
static void check_forward_slashes(const char *value) {
  char *normalized = qa_normalize_resource_path(value);
  bool same = normalized && strcmp(normalized, value) == 0;
  free(normalized);
  if (!same) qa_range_error("Unified paths must use forward slashes");
}
static void check_resource_path(const char *value) {
  bool ok = value[0] != '\0' && value[0] != '/' && !strchr(value, '\\');
  if (ok) {
    const char *cursor = value;
    for (;;) {
      const char *slash = strchr(cursor, '/');
      size_t part = slash ? (size_t)(slash - cursor) : strlen(cursor);
      if (part == 0 || (part == 1 && cursor[0] == '.')
          || (part == 2 && cursor[0] == '.' && cursor[1] == '.')) ok = false;
      if (!slash) break;
      cursor = slash + 1;
    }
  }
  if (!ok) qa_range_error("Expected a relative resource path: %s", value);
}
/* JSON.stringify helpers (string escaping + number spelling). */
typedef struct buffer { char *data; size_t length, capacity; } buffer;
static void buffer_add(buffer *b, const char *text) {
  size_t n = strlen(text);
  if (b->length + n + 1 > b->capacity) {
    b->capacity = (b->length + n + 1) * 2;
    b->data = realloc(b->data, b->capacity);
    if (!b->data) qa_range_error("out of memory");
  }
  memcpy(b->data + b->length, text, n + 1);
  b->length += n;
}
static void buffer_add_char(buffer *b, char c) {
  char text[2] = {c, '\0'};
  buffer_add(b, text);
}
static void json_escape(buffer *b, const char *text) {
  buffer_add_char(b, '"');
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    char escaped[8];
    switch (*p) {
      case '"': buffer_add(b, "\\\""); break;
      case '\\': buffer_add(b, "\\\\"); break;
      case '\b': buffer_add(b, "\\b"); break;
      case '\f': buffer_add(b, "\\f"); break;
      case '\n': buffer_add(b, "\\n"); break;
      case '\r': buffer_add(b, "\\r"); break;
      case '\t': buffer_add(b, "\\t"); break;
      default:
        if (*p < 0x20) {
          snprintf(escaped, sizeof escaped, "\\u%04x", *p);
          buffer_add(b, escaped);
        } else buffer_add_char(b, (char)*p);
        break;
    }
  }
  buffer_add_char(b, '"');
}
static void json_number(buffer *b, double value) {
  if (!isfinite(value)) qa_range_error("Composition identity contains a non-finite number");
  char text[40];
  if (value == 0 && signbit(value)) { buffer_add(b, "-0"); return; }
  if (value == trunc(value) && fabs(value) < 1e21) snprintf(text, sizeof text, "%.0f", value);
  else snprintf(text, sizeof text, "%.17g", value);
  buffer_add(b, text);
}
static int compare_entries(const void *a, const void *b) {
  return strcmp(((const qa_save_entry *)a)->key, ((const qa_save_entry *)b)->key);
}
static void canonical(buffer *b, const qa_save_value *value) {
  if (!value) qa_range_error("Composition identity contains a non-serializable value");
  switch (value->kind) {
    case QA_SAVE_NULL: buffer_add(b, "null"); return;
    case QA_SAVE_BOOLEAN: buffer_add(b, value->boolean ? "true" : "false"); return;
    case QA_SAVE_STRING: json_escape(b, value->text); return;
    case QA_SAVE_NUMBER: json_number(b, value->number); return;
    case QA_SAVE_ARRAY:
      buffer_add_char(b, '[');
      for (size_t i = 0; i < value->item_count; i++) {
        if (i) buffer_add_char(b, ',');
        canonical(b, value->items[i]);
      }
      buffer_add_char(b, ']');
      return;
    case QA_SAVE_OBJECT: {
      qa_save_entry *sorted = xmalloc(value->entry_count * sizeof *sorted);
      if (value->entry_count) memcpy(sorted, value->entries, value->entry_count * sizeof *sorted);
      qsort(sorted, value->entry_count, sizeof *sorted, compare_entries);
      buffer_add_char(b, '{');
      for (size_t i = 0; i < value->entry_count; i++) {
        if (i) buffer_add_char(b, ',');
        json_escape(b, sorted[i].key);
        buffer_add_char(b, ':');
        canonical(b, sorted[i].value);
      }
      buffer_add_char(b, '}');
      free(sorted);
      return;
    }
    default:
      qa_range_error("Composition identity contains a non-serializable value");
  }
}
static char *canonical_text(const qa_save_value *value) {
  buffer b = {0};
  canonical(&b, value);
  if (!b.data) { b.data = xmalloc(1); b.data[0] = '\0'; }
  return b.data;
}
static char *sha256_digest(const char *text) {
  char hex[65];
  qa_net_sha256_hex((const uint8_t *)text, strlen(text), hex);
  char *digest = qa_create_content_digest(hex);
  if (!digest) qa_range_error("out of memory");
  return digest;
}
qa_app_unified_resource_key qa_app_unified_resource_key_from_reference(
    const qa_resolved_resource_reference *reference) {
  const qa_archive_mount *archive = reference->provenance.archive_mount;
  const qa_loose_mount *loose = reference->provenance.loose_mount;
  qa_app_unified_resource_key key;
  key.content = archive ? archive->identity.content : loose->identity.content;
  key.path = reference->requested_path;
  key.digest = reference->digest;
  key.byte_length = (double)reference->byte_length;
  return key;
}
char *qa_app_unified_resource_id_from_key(const qa_app_unified_resource_key *key) {
  check_forward_slashes(key->path);
  if (key->byte_length != trunc(key->byte_length) || key->byte_length < 0
      || fabs(key->byte_length) > 9007199254740991.0)
    qa_range_error("Invalid unified resource length");
  buffer b = {0};
  buffer_add_char(&b, '[');
  json_escape(&b, key->content);
  buffer_add_char(&b, ',');
  json_escape(&b, key->path);
  buffer_add_char(&b, ',');
  json_escape(&b, key->digest);
  buffer_add_char(&b, ',');
  json_number(&b, key->byte_length);
  buffer_add_char(&b, ']');
  char hex[65];
  qa_net_sha256_hex((const uint8_t *)b.data, b.length, hex);
  free(b.data);
  size_t n = strlen(hex) + 18;
  char *out = xmalloc(n);
  snprintf(out, n, "resource:unified:%s", hex);
  return out;
}
char *qa_app_unified_resource_id_from_reference(const qa_resolved_resource_reference *reference) {
  qa_app_unified_resource_key key = qa_app_unified_resource_key_from_reference(reference);
  return qa_app_unified_resource_id_from_key(&key);
}
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
qa_app_unified_resource_key qa_app_unified_read_resource_key(const qa_save_reader *reader) {
  qa_app_unified_resource_key key = {0};
  qa_save_reader f = field_of(reader, "content");
  key.content = qa_read_content_id(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "path");
  key.path = qa_save_reader_string(&f);
  check_forward_slashes(key.path);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "digest");
  key.digest = qa_read_digest(&f);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "byteLength");
  double length = qa_save_reader_integer_min(&f, 0);
  if (length != trunc(length) || fabs(length) > 9007199254740991.0)
    qa_save_reader_fail(&f, "expected a safe nonnegative integer");
  key.byte_length = length;
  qa_save_reader_destroy(&f);
  return key;
}
qa_save_value *qa_app_unified_write_resource_key(const qa_app_unified_resource_key *key) {
  qa_save_value *out = qa_save_value_object();
  if (!out) return NULL;
  if (!qa_save_value_set(out, "content", qa_save_value_string(key->content))
      || !qa_save_value_set(out, "path", qa_save_value_string(key->path))
      || !qa_save_value_set(out, "digest", qa_save_value_string(key->digest))
      || !qa_save_value_set(out, "byteLength", qa_save_value_number(key->byte_length))) {
    qa_save_value_free(out);
    return NULL;
  }
  return out;
}
static bool key_matches(const qa_app_unified_resource_key *a, const qa_app_unified_resource_key *b) {
  return strcmp(a->content, b->content) == 0 && strcmp(a->path, b->path) == 0
      && strcmp(a->digest, b->digest) == 0 && a->byte_length == b->byte_length;
}
static void sidecars_check(const qa_app_unified_map_sidecar *sidecars, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_map_sidecar *entry = &sidecars[i];
    check_forward_slashes(entry->path);
    if (entry->has_resource
        && (strcmp(entry->resource.content, entry->content) != 0
            || strcmp(entry->resource.path, entry->path) != 0))
      qa_range_error("Unified sidecar resource identity differs from its content key");
    /* TS rejects onUnregistered here ("Sidecar contents cannot register here"). */
  }
}
static qa_save_value *write_sidecars(const qa_app_unified_map_sidecar *sidecars, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (!entry) qa_range_error("out of memory");
    if (!qa_save_value_set(entry, "content", qa_save_value_string(sidecars[i].content))
        || !qa_save_value_set(entry, "path", qa_save_value_string(sidecars[i].path)))
      qa_range_error("out of memory");
    if (sidecars[i].has_resource) {
      qa_save_value *resource = qa_app_unified_write_resource_key(&sidecars[i].resource);
      if (!resource || !qa_save_value_set(entry, "resource", resource))
        qa_range_error("out of memory");
    }
    if (!qa_save_value_push(list, entry)) qa_range_error("out of memory");
  }
  return list;
}
static size_t read_sidecars(const qa_save_reader *reader, qa_app_unified_map_sidecar **out) {
  size_t count = qa_save_reader_list_count(reader);
  qa_app_unified_map_sidecar *sidecars = xmalloc(count * sizeof *sidecars);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(reader, i, &row)) qa_range_error("out of memory");
    memset(&sidecars[i], 0, sizeof sidecars[i]);
    qa_save_reader f = field_of(&row, "content");
    sidecars[i].content = qa_read_content_id(&f);
    qa_save_reader_destroy(&f);
    f = field_of(&row, "path");
    sidecars[i].path = qa_save_reader_string(&f);
    qa_save_reader_destroy(&f);
    f = field_of(&row, "resource");
    if (!qa_save_reader_is_undefined(&f)) {
      sidecars[i].has_resource = true;
      sidecars[i].resource = qa_app_unified_read_resource_key(&f);
    }
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&row);
  }
  sidecars_check(sidecars, count);
  *out = sidecars;
  return count;
}
typedef struct mount_map { char *old_id; qa_save_value *mount; } mount_map;
typedef struct plan_map { char *old_id; char *new_id; } plan_map;
static char *tree_resource_id(qa_save_value *resource) {
  qa_save_value *provenance = object_get(resource, "provenance");
  qa_save_value *resolution = object_get(resource, "resolution");
  qa_save_value *mount = object_get(provenance, "mount");
  qa_save_value *identity = object_get(mount, "identity");
  const char *requested = text_of(object_get(resource, "requestedPath"));
  const char *member = text_of(object_get(provenance, "memberPath"));
  check_resource_path(requested);
  check_resource_path(member);
  qa_save_value *length_value = object_get(resource, "byteLength");
  double byte_length = length_value->number;
  if (byte_length != trunc(byte_length) || byte_length < 0
      || fabs(byte_length) > 9007199254740991.0)
    qa_range_error("Resource byte length must be a nonnegative safe integer");
  const char *resolution_kind = text_of(object_get(resolution, "kind"));
  bool is_link = strcmp(resolution_kind, "link") == 0;
  if (!is_link) {
    double rank = object_get(resolution, "rank")->number;
    if (rank != trunc(rank) || rank < 0 || fabs(rank) > 9007199254740991.0)
      qa_range_error("Resource precedence rank must be a nonnegative safe integer");
  }
  const char *provenance_kind = text_of(object_get(provenance, "kind"));
  bool archive = strcmp(provenance_kind, "archive") == 0;
  char *source;
  if (archive) {
    double index = object_get(provenance, "memberIndex")->number;
    if (index != trunc(index) || index < 0 || fabs(index) > 9007199254740991.0)
      qa_range_error("Archive member index must be a nonnegative safe integer");
    char *encoded = encode_uri(text_of(object_get(mount, "archivePath")));
    size_t n = strlen(encoded) + strlen(text_of(object_get(mount, "archiveDigest"))) + 32;
    source = xmalloc(n);
    snprintf(source, n, "%s:%s:%.0f", encoded,
        text_of(object_get(mount, "archiveDigest")), index);
    free(encoded);
  } else {
    source = encode_uri(text_of(object_get(mount, "rootPath")));
  }
  char *resolution_key;
  const char *plan = text_of(object_get(resolution, "plan"));
  if (strcmp(resolution_kind, "default-order") == 0) {
    size_t n = strlen(plan) + 40;
    resolution_key = xmalloc(n);
    snprintf(resolution_key, n, "%s:default:%.0f", plan, object_get(resolution, "rank")->number);
  } else if (strcmp(resolution_kind, "prefix-order") == 0) {
    char *prefix = encode_uri(text_of(object_get(resolution, "prefix")));
    size_t n = strlen(plan) + strlen(prefix) + 40;
    resolution_key = xmalloc(n);
    snprintf(resolution_key, n, "%s:prefix:%s:%.0f", plan, prefix,
        object_get(resolution, "rank")->number);
    free(prefix);
  } else {
    char *source_prefix = encode_uri(text_of(object_get(resolution, "sourcePrefix")));
    char *target = encode_uri(text_of(object_get(resolution, "targetPath")));
    size_t n = strlen(plan) + strlen(source_prefix) + strlen(target) + 16;
    resolution_key = xmalloc(n);
    snprintf(resolution_key, n, "%s:link:%s:%s", plan, source_prefix, target);
    free(source_prefix); free(target);
  }
  char *member_encoded = encode_uri(member);
  size_t n = strlen(text_of(object_get(identity, "content")))
      + strlen(text_of(object_get(identity, "id")))
      + strlen(source) + strlen(member_encoded)
      + strlen(text_of(object_get(resource, "digest"))) + strlen(resolution_key) + 48;
  char *out = xmalloc(n);
  snprintf(out, n, "resource:%s:%s:%.0f:%s:%s:%s:%s",
      text_of(object_get(identity, "content")), text_of(object_get(identity, "id")),
      object_get(identity, "generation")->number, source, member_encoded,
      text_of(object_get(resource, "digest")), resolution_key);
  free(source); free(member_encoded); free(resolution_key);
  return out;
}
static qa_save_value *normalize_recipe(const qa_save_value *source) {
  qa_save_value *recipe = clone_value(source);
  qa_save_value *plan = object_get(recipe, "mounts");
  qa_save_value *mounts = object_get(plan, "mounts");
  size_t mount_count = mounts->item_count;
  mount_map *mounts_map = xmalloc(mount_count * sizeof *mounts_map);
  for (size_t i = 0; i < mount_count; i++) {
    qa_save_value *mount = mounts->items[i];
    qa_save_value *identity = object_get(mount, "identity");
    mounts_map[i].old_id = copy_text(text_of(object_get(identity, "id")));
    char index_text[24];
    snprintf(index_text, sizeof index_text, "%zu", i);
    char *new_id = qa_create_mount_id("unified", index_text);
    if (!new_id) qa_range_error("out of memory");
    set_field(identity, "id", qa_save_value_string(new_id));
    free(new_id);
    set_field(identity, "generation", qa_save_value_number(0));
    const char *kind = text_of(object_get(mount, "kind"));
    char path[64];
    if (strcmp(kind, "archive") == 0) {
      snprintf(path, sizeof path, "unified/%zu.%s", i, text_of(object_get(mount, "format")));
      set_field(mount, "archivePath", qa_save_value_string(path));
    } else {
      snprintf(path, sizeof path, "unified/%zu", i);
      set_field(mount, "rootPath", qa_save_value_string(path));
    }
    mounts_map[i].mount = mount;
  }
  /* plan id */
  qa_save_value *plan_id = object_get(plan, "id");
  const char *old_plan = text_of(plan_id);
  const char *plan_namespace = strncmp(old_plan, "mount-plan:", 11) == 0 ? old_plan + 11 : old_plan;
  char *head = copy_text(plan_namespace);
  char *colon = strchr(head, ':');
  if (colon) *colon = '\0';
  char *plan_id_new = qa_create_mount_plan_id(head, "unified");
  free(head);
  if (!plan_id_new) qa_range_error("out of memory");
  /* TS: createMountPlanId(recipe.mounts.id.split(":")[1]!, "unified") */
  set_field(plan, "id", qa_save_value_string(plan_id_new));
  plan_map plans = {copy_text(old_plan), plan_id_new};
  /* defaultOrder */
  qa_save_value *order = object_get(plan, "defaultOrder");
  for (size_t i = 0; i < order->item_count; i++) {
    const char *id = text_of(order->items[i]);
    qa_save_value *found = NULL;
    for (size_t j = 0; j < mount_count; j++)
      if (strcmp(mounts_map[j].old_id, id) == 0) found = mounts_map[j].mount;
    if (!found) qa_range_error("Unknown mount in unified recipe: %s", id);
    qa_save_value_free(order->items[i]);
    order->items[i] = qa_save_value_string(
        text_of(object_get(object_get(found, "identity"), "id")));
  }
  /* prefixOrders */
  qa_save_value *prefix_orders = object_get(plan, "prefixOrders");
  for (size_t i = 0; i < prefix_orders->item_count; i++) {
    qa_save_value *entry = prefix_orders->items[i];
    const char *prefix = text_of(object_get(entry, "prefix"));
    char *stripped = copy_text(prefix);
    size_t length = strlen(stripped);
    if (length && stripped[length - 1] == '/') stripped[length - 1] = '\0';
    char *normalized = qa_normalize_resource_path(stripped);
    free(normalized);
    free(stripped);
    qa_save_value *mounts_list = object_get(entry, "mounts");
    for (size_t j = 0; j < mounts_list->item_count; j++) {
      const char *id = text_of(mounts_list->items[j]);
      qa_save_value *found = NULL;
      for (size_t k = 0; k < mount_count; k++)
        if (strcmp(mounts_map[k].old_id, id) == 0) found = mounts_map[k].mount;
      if (!found) qa_range_error("Unknown mount in unified recipe: %s", id);
      qa_save_value_free(mounts_list->items[j]);
      mounts_list->items[j] = qa_save_value_string(
          text_of(object_get(object_get(found, "identity"), "id")));
    }
  }
  qa_save_value **resources = NULL;
  size_t resource_count = 0, resource_cap = 0;
  qa_save_value *map = object_get(recipe, "map");
  qa_save_value *geometry = object_get(map, "geometry");
  resources = xmalloc(sizeof *resources);
  resources[resource_count++] = geometry;
  resource_cap = 1;
  qa_save_value *list = object_get(recipe, "resources");
  for (size_t i = 0; i < list->item_count; i++) {
    if (resource_count == resource_cap) {
      resource_cap *= 2;
      resources = realloc(resources, resource_cap * sizeof *resources);
      if (!resources) qa_range_error("out of memory");
    }
    resources[resource_count++] = list->items[i];
  }
  qa_save_value *execution = object_get(recipe, "execution");
  for (size_t i = 0; i < execution->item_count; i++) {
    qa_save_value *artifact = object_get(execution->items[i], "artifact");
    const char *kind = text_of(object_get(artifact, "kind"));
    if (kind && strcmp(kind, "typescript") == 0) continue;
    if (resource_count == resource_cap) {
      resource_cap *= 2;
      resources = realloc(resources, resource_cap * sizeof *resources);
      if (!resources) qa_range_error("out of memory");
    }
    resources[resource_count++] = object_get(artifact, "reference");
  }
  for (size_t i = 0; i < resource_count; i++) {
    qa_save_value *resource = resources[i];
    qa_save_value *provenance = object_get(resource, "provenance");
    qa_save_value *mount = object_get(provenance, "mount");
    const char *mount_id = text_of(object_get(object_get(mount, "identity"), "id"));
    qa_save_value *replacement = NULL;
    for (size_t j = 0; j < mount_count; j++)
      if (strcmp(mounts_map[j].old_id, mount_id) == 0) replacement = mounts_map[j].mount;
    if (!replacement || strcmp(text_of(object_get(replacement, "kind")),
            text_of(object_get(provenance, "kind"))) != 0)
      qa_range_error("Unified resource does not use a recipe mount");
    set_field(provenance, "mount", clone_value(replacement));
    qa_save_value *resolution = object_get(resource, "resolution");
    set_field(resolution, "plan", qa_save_value_string(plans.new_id));
    set_field(resource, "requestedPath",
        qa_save_value_string(text_of(object_get(resource, "requestedPath"))));
    char *requested = qa_normalize_resource_path(text_of(object_get(resource, "requestedPath")));
    set_field(resource, "requestedPath", qa_save_value_string(requested));
    free(requested);
    char *member = qa_normalize_resource_path(text_of(object_get(provenance, "memberPath")));
    set_field(provenance, "memberPath", qa_save_value_string(member));
    free(member);
    if (strcmp(text_of(object_get(resolution, "kind")), "prefix-order") == 0) {
      char *prefix = qa_normalize_resource_path(text_of(object_get(resolution, "prefix")));
      set_field(resolution, "prefix", qa_save_value_string(prefix));
      free(prefix);
    }
    if (strcmp(text_of(object_get(resolution, "kind")), "link") == 0) {
      char *source_prefix = qa_normalize_resource_path(text_of(object_get(resolution, "sourcePrefix")));
      set_field(resolution, "sourcePrefix", qa_save_value_string(source_prefix));
      free(source_prefix);
      char *target = qa_normalize_resource_path(text_of(object_get(resolution, "targetPath")));
      set_field(resolution, "targetPath", qa_save_value_string(target));
      free(target);
    }
    char *id = tree_resource_id(resource);
    set_field(resource, "id", qa_save_value_string(id));
    free(id);
  }
  free(resources);
  for (size_t i = 0; i < mount_count; i++) free(mounts_map[i].old_id);
  free(mounts_map);
  free(plans.old_id);
  free(plans.new_id);
  return recipe;
}
static qa_save_value *composition_tree(const qa_save_value *recipe_tree,
    const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("out of memory");
  set_field(out, "schemaVersion", qa_save_value_number(1));
  set_field(out, "recipe", clone_value(recipe_tree));
  set_field(out, "snapshotSchema", qa_save_value_string("qts:snapshot-v10"));
  qa_save_value *empty = qa_save_value_array();
  if (!empty) qa_range_error("out of memory");
  set_field(out, "actorConfigurations", empty);
  set_field(out, "sidecars", write_sidecars(sidecars, sidecar_count));
  return out;
}
static qa_app_unified_composition *make_identity(qa_save_value *normalized_recipe,
    const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  qa_save_value *composition = composition_tree(normalized_recipe, sidecars, sidecar_count);
  char *text = canonical_text(composition);
  qa_save_value_free(composition);
  qa_app_unified_composition *identity = xmalloc(sizeof *identity);
  identity->digest = sha256_digest(text);
  free(text);
  identity->recipe = normalized_recipe;
  identity->sidecars = xmalloc(sidecar_count * sizeof *identity->sidecars);
  if (sidecar_count) memcpy(identity->sidecars, sidecars, sidecar_count * sizeof *sidecars);
  identity->sidecar_count = sidecar_count;
  return identity;
}
qa_app_unified_composition *qa_app_unified_create_composition(
    const qa_save_value *recipe, const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  sidecars_check(sidecars, sidecar_count);
  return make_identity(normalize_recipe(recipe), sidecars, sidecar_count);
}
qa_app_unified_composition *qa_app_unified_build_composition(
    const qa_save_value *recipe,
    const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  return qa_app_unified_create_composition(recipe, sidecars, sidecar_count);
}
void qa_app_unified_composition_destroy(qa_app_unified_composition *composition) {
  if (!composition) return;
  free(composition->digest);
  qa_save_value_free(composition->recipe);
  free(composition->sidecars);
  free(composition);
}
qa_app_unified_composition *qa_app_unified_read_composition(const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, (qa_save_value *)value, "unified-composition"))
    qa_range_error("out of memory");
  qa_save_reader digest_reader = field_of(&reader, "digest");
  const char *digest = qa_read_digest(&digest_reader);
  qa_save_reader_destroy(&digest_reader);
  qa_save_reader composition = field_of(&reader, "composition");
  qa_save_reader schema = field_of(&composition, "schemaVersion");
  qa_save_reader_literal_number(&schema, 1);
  qa_save_reader_destroy(&schema);
  qa_save_reader snapshot = field_of(&composition, "snapshotSchema");
  qa_save_reader_literal_string(&snapshot, "qts:snapshot-v10");
  qa_save_reader_destroy(&snapshot);
  qa_save_reader actors = field_of(&composition, "actorConfigurations");
  if (qa_save_reader_list_count(&actors) != 0)
    qa_save_reader_fail(&actors, "expected exact value");
  qa_save_reader_destroy(&actors);
  qa_save_reader sidecars_reader = field_of(&composition, "sidecars");
  qa_app_unified_map_sidecar *sidecars = NULL;
  size_t sidecar_count = read_sidecars(&sidecars_reader, &sidecars);
  qa_save_reader_destroy(&sidecars_reader);
  const qa_save_value *recipe_value = qa_save_value_get(composition.value, "recipe");
  /* TS readRecipe (blocked upstream) validates the recipe shape;
   * the digest check below still rejects any tampering. */
  qa_app_unified_composition *parsed = make_identity(clone_value(recipe_value),
      sidecars, sidecar_count);
  qa_save_reader_destroy(&composition);
  qa_save_reader_destroy(&reader);
  if (strcmp(parsed->digest, digest) != 0) {
    qa_app_unified_composition_destroy(parsed);
    free(sidecars);
    qa_range_error("Unified composition digest differs from its content");
  }
  parsed->sidecars = sidecars;
  return parsed;
}
uint8_t *qa_app_unified_encode_composition(const qa_save_value *identity_tree, size_t *out_length) {
  return qa_encode_checkpoint_value((qa_save_value *)identity_tree, out_length);
}
qa_app_unified_composition *qa_app_unified_decode_composition(
    const uint8_t *bytes, size_t length) {
  if (length > 32 * 1024 * 1024) qa_range_error("Unified composition exceeds byte limit");
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) qa_range_error("out of memory");
  qa_app_unified_composition *identity = qa_app_unified_read_composition(tree);
  qa_save_value_free(tree);
  return identity;
}
static qa_save_value *find_mount_by_content(const qa_save_value *const *mounts, size_t count,
    const qa_save_value *expected) {
  const char *kind = text_of(object_get((qa_save_value *)expected, "kind"));
  const char *content = text_of(object_get(
      object_get((qa_save_value *)expected, "identity"), "content"));
  const char *format = text_of(object_get((qa_save_value *)expected, "format"));
  const qa_save_value *best = NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_value *candidate = (qa_save_value *)mounts[i];
    if (strcmp(text_of(object_get(candidate, "kind")), kind) != 0) continue;
    if (strcmp(text_of(object_get(object_get(candidate, "identity"), "content")), content) != 0)
      continue;
    if (strcmp(kind, "archive") == 0) {
      if (strcmp(text_of(object_get(candidate, "format")), format) != 0) continue;
      if (strcmp(text_of(object_get(candidate, "archiveDigest")),
              text_of(object_get((qa_save_value *)expected, "archiveDigest"))) != 0) continue;
      return candidate;
    }
    if (!best) best = candidate;
  }
  if (!best) {
    qa_range_error("Unified content is unavailable locally: %s", content);
  }
  return (qa_save_value *)best;
}
qa_save_value *qa_app_unified_resolve_composition(
    const qa_save_value *offered_identity, const qa_app_unified_content_host *host) {
  qa_app_unified_composition *offered = qa_app_unified_read_composition(offered_identity);
  qa_save_value *recipe = clone_value(offered->recipe);
  qa_save_value *plan = object_get(recipe, "mounts");
  qa_save_value *mounts = object_get(plan, "mounts");
  typedef struct { char *old_id; qa_save_value *mount; } entry;
  size_t mount_count = mounts->item_count;
  entry *resolved = xmalloc(mount_count * sizeof *resolved);
  for (size_t i = 0; i < mount_count; i++) {
    qa_save_value *expected = mounts->items[i];
    const char *content = text_of(object_get(object_get(expected, "identity"), "content"));
    size_t count = 0;
    const qa_save_value *const *candidates = host->catalog_mounts(host->ctx,
        (qa_content_id)content, &count);
    qa_save_value *found = find_mount_by_content(candidates, count, expected);
    resolved[i].old_id = copy_text(text_of(object_get(object_get(expected, "identity"), "id")));
    resolved[i].mount = found;
  }
  /* Rebind mounts in the plan copy: replace each mount entry. */
  for (size_t i = 0; i < mount_count; i++) {
    qa_save_value_free(mounts->items[i]);
    mounts->items[i] = clone_value(resolved[i].mount);
  }
  qa_save_value *order = object_get(plan, "defaultOrder");
  for (size_t i = 0; i < order->item_count; i++) {
    const char *id = text_of(order->items[i]);
    qa_save_value *found = NULL;
    for (size_t j = 0; j < mount_count; j++)
      if (strcmp(resolved[j].old_id, id) == 0) found = resolved[j].mount;
    qa_save_value_free(order->items[i]);
    order->items[i] = qa_save_value_string(
        text_of(object_get(object_get(found, "identity"), "id")));
  }
  qa_save_value *prefix_orders = object_get(plan, "prefixOrders");
  for (size_t i = 0; i < prefix_orders->item_count; i++) {
    qa_save_value *list = object_get(prefix_orders->items[i], "mounts");
    for (size_t j = 0; j < list->item_count; j++) {
      const char *id = text_of(list->items[j]);
      qa_save_value *found = NULL;
      for (size_t k = 0; k < mount_count; k++)
        if (strcmp(resolved[k].old_id, id) == 0) found = resolved[k].mount;
      qa_save_value_free(list->items[j]);
      list->items[j] = qa_save_value_string(
          text_of(object_get(object_get(found, "identity"), "id")));
    }
  }
  set_field(plan, "id", qa_save_value_string("mount-plan:local:unified-admission"));
  /* Rebind resources (geometry + resources + artifacts). */
  qa_save_value **resources = NULL;
  size_t resource_count = 0, resource_cap = 0;
  #define PUSH_RESOURCE(v) do { \
    if (resource_count == resource_cap) { resource_cap = resource_cap ? resource_cap * 2 : 8; \
      resources = realloc(resources, resource_cap * sizeof *resources); \
      if (!resources) qa_range_error("out of memory"); } \
    resources[resource_count++] = (v); } while (0)
  qa_save_value *map = object_get(recipe, "map");
  PUSH_RESOURCE(object_get(map, "geometry"));
  qa_save_value *list = object_get(recipe, "resources");
  for (size_t i = 0; i < list->item_count; i++) PUSH_RESOURCE(list->items[i]);
  qa_save_value *execution = object_get(recipe, "execution");
  for (size_t i = 0; i < execution->item_count; i++) {
    qa_save_value *artifact = object_get(execution->items[i], "artifact");
    const char *kind = text_of(object_get(artifact, "kind"));
    if (kind && strcmp(kind, "typescript") == 0) continue;
    PUSH_RESOURCE(object_get(artifact, "reference"));
  }
  for (size_t i = 0; i < resource_count; i++) {
    qa_save_value *resource = resources[i];
    qa_save_value *provenance = object_get(resource, "provenance");
    const char *mount_id = text_of(object_get(
        object_get(object_get(provenance, "mount"), "identity"), "id"));
    qa_save_value *replacement = NULL;
    for (size_t j = 0; j < mount_count; j++)
      if (strcmp(resolved[j].old_id, mount_id) == 0) replacement = resolved[j].mount;
    if (!replacement) qa_range_error("Unified resource does not use a recipe mount");
    set_field(provenance, "mount", clone_value(replacement));
    char *id = tree_resource_id(resource);
    set_field(resource, "id", qa_save_value_string(id));
    free(id);
    qa_save_value *resolution = object_get(resource, "resolution");
    set_field(resolution, "plan", qa_save_value_string("mount-plan:local:unified-admission"));
    host->plan_read(host->ctx, resource);
    const char *resolved_id = host->plan_resolve_id(host->ctx,
        text_of(object_get(resource, "requestedPath")));
    bool same = resolved_id && text_of(object_get(resource, "id"))
        && strcmp(resolved_id, text_of(object_get(resource, "id"))) == 0;
    char *recomputed = tree_resource_id(resource);
    same = same && strcmp(recomputed, text_of(object_get(resource, "id"))) == 0;
    free(recomputed);
    if (!same) {
      qa_save_value_free(recipe);
      qa_range_error("Unified resource does not resolve identically: %s",
          text_of(object_get(resource, "requestedPath")));
    }
  }
  #undef PUSH_RESOURCE
  free(resources);
  for (size_t i = 0; i < mount_count; i++) free(resolved[i].old_id);
  free(resolved);
  qa_app_unified_composition *local = qa_app_unified_create_composition(
      recipe, offered->sidecars, offered->sidecar_count);
  if (strcmp(local->digest, offered->digest) != 0) {
    qa_app_unified_composition_destroy(local);
    qa_save_value_free(recipe);
    qa_range_error("Local unified composition differs from the host offer");
  }
  qa_app_unified_composition_destroy(local);
  qa_app_unified_composition_destroy(offered);
  return recipe;
}
void *qa_app_unified_load_content(const qa_save_value *identity_tree,
    const qa_app_unified_content_host *host) {
  qa_app_unified_composition *offered = qa_app_unified_read_composition(identity_tree);
  qa_save_value *local = qa_app_unified_resolve_composition(identity_tree, host);
  const char *family = host->product_family(host->ctx,
      (qa_content_id)host->map_entities_content(host->ctx, local));
  const char *edition = host->product_edition(host->ctx,
      (qa_content_id)host->map_entities_content(host->ctx, local));
  const char *expected_family = family;
  if (strcmp(edition, "quakeworld") == 0) expected_family = "qw";
  void *loaded = host->load_content(host->ctx, local, family);
  const char *loaded_family = host->loaded_family(host->ctx, loaded);
  size_t sidecar_count = 0;
  const qa_app_unified_map_sidecar *sidecars = host->loaded_sidecars(host->ctx, loaded, &sidecar_count);
  qa_app_unified_composition *built = qa_app_unified_build_composition(
      host->loaded_recipe(host->ctx, loaded), sidecars, sidecar_count);
  if (strcmp(expected_family, loaded_family) != 0
      || strcmp(built->digest, offered->digest) != 0) {
    host->close_content(host->ctx, loaded);
    qa_app_unified_composition_destroy(built);
    qa_save_value_free(local);
    qa_range_error("Loaded unified content differs from the host offer");
  }
  qa_app_unified_composition_destroy(built);
  qa_app_unified_composition_destroy(offered);
  qa_save_value_free(local);
  return loaded;
}
qa_resolved_resource_reference qa_app_unified_resolve_resource(
    const qa_app_unified_content_host *host, const qa_app_unified_resource_key *key) {
  qa_resolved_resource_reference reference;
  if (!host->open_resource(host->ctx, key->content, key->path, &reference))
    qa_range_error("Unified resource is unavailable: %s:%s", key->content, key->path);
  qa_app_unified_resource_key actual = qa_app_unified_resource_key_from_reference(&reference);
  if (!key_matches(&actual, key))
    qa_range_error("Unified resource identity differs: %s:%s", key->content, key->path);
  return reference;
}
