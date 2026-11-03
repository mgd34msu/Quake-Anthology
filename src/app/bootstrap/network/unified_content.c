/* Port of src/app/bootstrap/network/unified-content.ts.
 * GPL-2.0-or-later. */
#include "qa/network/common/js_number.h"
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
#include "qa/foundation/js.h"
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
  if (!same) qa_raise("Error", 0, 0, "Unified paths must use forward slashes");
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
  qa_net_json_number(value, text, sizeof text);
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
      qa_js_sort(sorted, value->entry_count, sizeof *sorted, compare_entries);
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
  key.byte_length = qa_save_reader_integer_min(&f, 0);
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
static void cleanup_tree_early(void *arg) { qa_save_value_free(arg); }
static qa_save_value *write_sidecars(const qa_app_unified_map_sidecar *sidecars, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree_early, list);
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (!entry || !qa_save_value_push(list, entry)) { qa_save_value_free(entry); qa_range_error("out of memory"); }
    set_field(entry, "content", qa_save_value_string(sidecars[i].content));
    set_field(entry, "path", qa_save_value_string(sidecars[i].path));
    if (sidecars[i].has_resource) {
      qa_save_value *resource = qa_app_unified_write_resource_key(&sidecars[i].resource);
      if (!resource) qa_range_error("out of memory");
      set_field(entry, "resource", resource);
    } else set_field(entry, "resource", qa_save_value_null());
  }
  qa_cleanup_pop(&guard);
  return list;
}
/* TS readSidecars; strings borrow from the reader's tree. *out is set before any read can
 * raise, so the caller frees it either way. */
static size_t read_sidecars(const qa_save_reader *reader, qa_app_unified_map_sidecar **out) {
  size_t count = qa_save_reader_list_count(reader);
  qa_app_unified_map_sidecar *sidecars = calloc(count ? count : 1, sizeof *sidecars);
  if (!sidecars) qa_range_error("out of memory");
  *out = sidecars;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(reader, i, &row)) qa_range_error("out of memory");
    qa_save_reader f = field_of(&row, "content");
    sidecars[i].content = qa_read_content_id(&f);
    f = field_of(&row, "path");
    sidecars[i].path = qa_save_reader_string(&f);
    check_forward_slashes(sidecars[i].path);
    f = field_of(&row, "resource");
    if (!qa_save_reader_is_null(&f)) {
      sidecars[i].resource = qa_app_unified_read_resource_key(&f);
      sidecars[i].has_resource = true;
    }
  }
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
    snprintf(source, n, "%s:%s:%s", encoded,
        text_of(object_get(mount, "archiveDigest")), QA_JS_NUMBER(index));
    free(encoded);
  } else {
    source = encode_uri(text_of(object_get(mount, "rootPath")));
  }
  char *resolution_key;
  const char *plan = text_of(object_get(resolution, "plan"));
  if (strcmp(resolution_kind, "default-order") == 0) {
    size_t n = strlen(plan) + 40;
    resolution_key = xmalloc(n);
    snprintf(resolution_key, n, "%s:default:%s", plan, QA_JS_NUMBER(object_get(resolution, "rank")->number));
  } else if (strcmp(resolution_kind, "prefix-order") == 0) {
    char *prefix = encode_uri(text_of(object_get(resolution, "prefix")));
    size_t n = strlen(plan) + strlen(prefix) + 40;
    resolution_key = xmalloc(n);
    snprintf(resolution_key, n, "%s:prefix:%s:%s", plan, prefix,
        QA_JS_NUMBER(object_get(resolution, "rank")->number));
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
  snprintf(out, n, "resource:%s:%s:%s:%s:%s:%s:%s",
      text_of(object_get(identity, "content")), text_of(object_get(identity, "id")),
      QA_JS_NUMBER(object_get(identity, "generation")->number), source, member_encoded,
      text_of(object_get(resource, "digest")), resolution_key);
  free(source); free(member_encoded); free(resolution_key);
  return out;
}
/* TS createUnifiedComposition's rebinding, on a cloned tree. Plans map to
 * mount-plan:unified:<first-seen index>, the recipe plan first; mounts to mount:unified:<index>.
 * Paths are validated, never rewritten. Errors are TS's, in TS evaluation order. */
typedef struct rebind_state {
  mount_map *mounts; size_t mount_count;
  plan_map *plans; size_t plan_count, plan_cap;
} rebind_state;
static void rebind_state_free(void *arg) {
  rebind_state *state = arg;
  for (size_t i = 0; i < state->mount_count; i++) free(state->mounts[i].old_id);
  free(state->mounts);
  for (size_t i = 0; i < state->plan_count; i++) { free(state->plans[i].old_id); free(state->plans[i].new_id); }
  free(state->plans);
}
static const char *rebind_plan(rebind_state *state, const char *id) {
  for (size_t i = 0; i < state->plan_count; i++)
    if (strcmp(state->plans[i].old_id, id) == 0) return state->plans[i].new_id;
  if (state->plan_count == state->plan_cap) {
    state->plan_cap = state->plan_cap ? state->plan_cap * 2 : 4;
    state->plans = realloc(state->plans, state->plan_cap * sizeof *state->plans);
    if (!state->plans) qa_range_error("out of memory");
  }
  char index_text[24];
  snprintf(index_text, sizeof index_text, "%zu", state->plan_count);
  char *logical = qa_create_mount_plan_id("unified", index_text);
  if (!logical) qa_range_error("out of memory");
  state->plans[state->plan_count].old_id = copy_text(id);
  state->plans[state->plan_count].new_id = logical;
  return state->plans[state->plan_count++].new_id;
}
static qa_save_value *rebind_mount_for(rebind_state *state, const char *id) {
  for (size_t i = 0; i < state->mount_count; i++)
    if (strcmp(state->mounts[i].old_id, id) == 0) return state->mounts[i].mount;
  qa_range_error("Unified reference uses an undeclared mount");
}
static void validate_path(const char *value) {
  char *normalized = qa_normalize_resource_path(value);
  free(normalized);
}
static void validate_prefix(const char *value) {
  char *stripped = copy_text(value);
  size_t length = strlen(stripped);
  if (length && stripped[length - 1] == '/') stripped[length - 1] = '\0';
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, stripped);
  validate_path(stripped);
  qa_cleanup_pop(&guard);
  free(stripped);
}
static void rebind_order(rebind_state *state, qa_save_value *list) {
  for (size_t i = 0; i < list->item_count; i++)
    for (size_t j = i + 1; j < list->item_count; j++)
      if (strcmp(text_of(list->items[i]), text_of(list->items[j])) == 0)
        qa_range_error("Unified mount order must include every mount once");
  if (list->item_count != state->mount_count) qa_range_error("Unified mount order must include every mount once");
  for (size_t i = 0; i < list->item_count; i++) {
    qa_save_value *mount = rebind_mount_for(state, text_of(list->items[i]));
    qa_save_value *id = qa_save_value_string(text_of(object_get(object_get(mount, "identity"), "id")));
    if (!id) qa_range_error("out of memory");
    qa_save_value_free(list->items[i]);
    list->items[i] = id;
  }
}
/* TS resourceWithMount(resource, mountFor(provenance.mount.identity.id), plan(resolution.plan)). */
static void rebind_resource(rebind_state *state, qa_save_value *resource) {
  qa_save_value *provenance = object_get(resource, "provenance");
  qa_save_value *mount = rebind_mount_for(state,
      text_of(object_get(object_get(object_get(provenance, "mount"), "identity"), "id")));
  qa_save_value *resolution = object_get(resource, "resolution");
  const char *plan = rebind_plan(state, text_of(object_get(resolution, "plan")));
  validate_path(text_of(object_get(resource, "requestedPath")));
  validate_path(text_of(object_get(provenance, "memberPath")));
  const char *kind = text_of(object_get(provenance, "kind"));
  const char *mount_kind = text_of(object_get(mount, "kind"));
  bool same = (strcmp(kind, "archive") == 0 && strcmp(mount_kind, "archive") == 0)
      || (strcmp(kind, "loose") == 0 && strcmp(mount_kind, "loose") == 0);
  if (!same) qa_range_error("Unified resource mount kind differs");
  if (strcmp(text_of(object_get(resolution, "kind")), "link") == 0) {
    validate_prefix(text_of(object_get(resolution, "sourcePrefix")));
    validate_path(text_of(object_get(resolution, "targetPath")));
  }
  set_field(provenance, "mount", clone_value(mount));
  set_field(resolution, "plan", qa_save_value_string(plan));
  char *id = tree_resource_id(resource);
  set_field(resource, "id", qa_save_value_string(id));
  free(id);
}
static void normalize_body(qa_save_value *recipe, rebind_state *state) {
  qa_save_value *plan = object_get(recipe, "mounts");
  qa_save_value *mounts = object_get(plan, "mounts");
  const char *logical_plan = rebind_plan(state, text_of(object_get(plan, "id")));
  state->mounts = xmalloc(mounts->item_count * sizeof *state->mounts);
  for (size_t i = 0; i < mounts->item_count; i++) {
    qa_save_value *mount = mounts->items[i];
    qa_save_value *identity = object_get(mount, "identity");
    const char *old_id = text_of(object_get(identity, "id"));
    for (size_t j = 0; j < state->mount_count; j++)
      if (strcmp(state->mounts[j].old_id, old_id) == 0) qa_range_error("Repeated unified mount identity");
    state->mounts[state->mount_count].old_id = copy_text(old_id);
    state->mounts[state->mount_count].mount = mount;
    state->mount_count++;
    char index_text[24];
    snprintf(index_text, sizeof index_text, "%zu", i);
    char *new_id = qa_create_mount_id("unified", index_text);
    if (!new_id) qa_range_error("out of memory");
    qa_save_value *fresh = qa_save_value_object();
    if (!fresh) qa_range_error("out of memory");
    set_field(fresh, "id", qa_save_value_string(new_id));
    free(new_id);
    set_field(fresh, "content", clone_value(object_get(identity, "content")));
    set_field(fresh, "generation", qa_save_value_number(0));
    set_field(mount, "identity", fresh);
    char path[64];
    if (strcmp(text_of(object_get(mount, "kind")), "archive") == 0) {
      snprintf(path, sizeof path, "unified/%zu.%s", i, text_of(object_get(mount, "format")));
      set_field(mount, "archivePath", qa_save_value_string(path));
    } else {
      snprintf(path, sizeof path, "unified/%zu", i);
      set_field(mount, "rootPath", qa_save_value_string(path));
    }
  }
  set_field(plan, "id", qa_save_value_string(logical_plan));
  rebind_order(state, object_get(plan, "defaultOrder"));
  qa_save_value *prefix_orders = object_get(plan, "prefixOrders");
  for (size_t i = 0; i < prefix_orders->item_count; i++) {
    qa_save_value *entry = prefix_orders->items[i];
    validate_prefix(text_of(object_get(entry, "prefix")));
    rebind_order(state, object_get(entry, "mounts"));
  }
  rebind_resource(state, object_get(object_get(recipe, "map"), "geometry"));
  qa_save_value *resources = object_get(recipe, "resources");
  for (size_t i = 0; i < resources->item_count; i++) rebind_resource(state, resources->items[i]);
  qa_save_value *execution = object_get(recipe, "execution");
  for (size_t i = 0; i < execution->item_count; i++) {
    const char *kind = text_of(object_get(execution->items[i], "kind"));
    if (kind && strcmp(kind, "typescript") == 0) continue;
    rebind_resource(state, object_get(execution->items[i], "artifact"));
  }
}
static void cleanup_tree(void *arg) { qa_save_value_free(arg); }
static void cleanup_tree_ref(void *arg) { qa_save_value_free(*(qa_save_value **)arg); }
static void cleanup_composition(void *arg) { qa_app_unified_composition_destroy(*(qa_app_unified_composition **)arg); }
static qa_save_value *normalize_recipe(const qa_save_value *source) {
  qa_save_value *recipe = clone_value(source);
  rebind_state state;
  memset(&state, 0, sizeof state);
  qa_cleanup recipe_guard, state_guard;
  qa_cleanup_push(&recipe_guard, cleanup_tree, recipe);
  qa_cleanup_push(&state_guard, rebind_state_free, &state);
  normalize_body(recipe, &state);
  qa_cleanup_pop(&state_guard);
  qa_cleanup_pop(&recipe_guard);
  rebind_state_free(&state);
  return recipe;
}
static qa_save_value *composition_tree(const qa_save_value *recipe_tree, const qa_save_value *sidecar_tree) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree, out);
  set_field(out, "schemaVersion", qa_save_value_number(1));
  set_field(out, "recipe", clone_value(recipe_tree));
  set_field(out, "snapshotSchema", qa_save_value_string("qts:snapshot-v10"));
  qa_save_value *empty = qa_save_value_array();
  if (!empty) qa_range_error("out of memory");
  set_field(out, "actorConfigurations", empty);
  set_field(out, "sidecars", clone_value(sidecar_tree));
  qa_cleanup_pop(&guard);
  return out;
}
static char *composition_digest(const qa_save_value *recipe_tree, const qa_save_value *sidecar_tree) {
  qa_save_value *composition = composition_tree(recipe_tree, sidecar_tree);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree, composition);
  char *text = canonical_text(composition);
  qa_cleanup_pop(&guard);
  qa_save_value_free(composition);
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, free, text);
  char *digest = sha256_digest(text);
  qa_cleanup_pop(&text_guard);
  free(text);
  return digest;
}
/* Takes the normalized recipe. The identity owns a canonical sidecar tree its sidecars borrow
 * from, so it outlives whatever tree the caller read them from. */
static qa_app_unified_composition *make_identity(qa_save_value *normalized_recipe,
    const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  qa_app_unified_composition *identity = calloc(1, sizeof *identity);
  if (!identity) { qa_save_value_free(normalized_recipe); qa_range_error("out of memory"); }
  identity->recipe = normalized_recipe;
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_composition, &identity);
  identity->sidecar_tree = write_sidecars(sidecars, sidecar_count);
  identity->digest = composition_digest(identity->recipe, identity->sidecar_tree);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, identity->sidecar_tree, "unified.sidecars")) qa_range_error("out of memory");
  identity->sidecar_count = read_sidecars(&reader, &identity->sidecars);
  qa_cleanup_pop(&guard);
  return identity;
}
qa_app_unified_composition *qa_app_unified_create_composition(
    const qa_save_value *recipe, const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count) {
  qa_save_value *normalized = normalize_recipe(recipe);
  /* TS re-reads the sidecars (readSidecars over the given list) and rejects repeats. */
  for (size_t i = 0; i < sidecar_count; i++) {
    check_forward_slashes(sidecars[i].path);
    if (sidecars[i].has_resource) check_forward_slashes(sidecars[i].resource.path);
  }
  for (size_t i = 0; i < sidecar_count; i++)
    for (size_t j = 0; j < i; j++)
      if (strcmp(sidecars[i].content, sidecars[j].content) == 0 && strcmp(sidecars[i].path, sidecars[j].path) == 0) {
        qa_save_value_free(normalized);
        qa_raise("Error", 0, 0, "Repeated unified map sidecar");
      }
  return make_identity(normalized, sidecars, sidecar_count);
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
  qa_save_value_free(composition->sidecar_tree);
  free(composition);
}
typedef struct read_scope {
  qa_app_unified_map_sidecar *sidecars;
  qa_app_unified_composition *identity;
  char *parsed;
} read_scope;
static void read_scope_free(void *arg) {
  read_scope *scope = arg;
  free(scope->sidecars);
  qa_app_unified_composition_destroy(scope->identity);
  free(scope->parsed);
}
qa_app_unified_composition *qa_app_unified_read_composition(const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "unified.composition")) qa_range_error("out of memory");
  qa_save_reader composition = field_of(&reader, "composition");
  qa_save_reader f = field_of(&composition, "schemaVersion");
  qa_save_reader_literal_number(&f, 1);
  f = field_of(&composition, "snapshotSchema");
  qa_save_reader_literal_string(&f, "qts:snapshot-v10");
  f = field_of(&composition, "actorConfigurations");
  if (qa_save_reader_list_count(&f) != 0)
    qa_save_reader_fail(&composition, "actor configurations are negotiated by frame");
  qa_save_reader recipe_reader = field_of(&composition, "recipe");
  /* TS readRecipe: the checked recipe; only its validation is needed here, the tree carries on. */
  qa_executable_recipe checked = qa_read_recipe(&recipe_reader);
  qa_executable_recipe_free(&checked);
  read_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, read_scope_free, &scope);
  f = field_of(&composition, "sidecars");
  size_t sidecar_count = read_sidecars(&f, &scope.sidecars);
  scope.identity = qa_app_unified_create_composition(recipe_reader.value, scope.sidecars, sidecar_count);
  /* compositionIdentity({...identity.composition, recipe}): the offered recipe must already be
   * the normalized one. */
  scope.parsed = composition_digest(recipe_reader.value, scope.identity->sidecar_tree);
  f = field_of(&reader, "digest");
  const char *digest = qa_read_digest(&f);
  if (strcmp(scope.parsed, scope.identity->digest) != 0 || strcmp(digest, scope.identity->digest) != 0)
    qa_save_reader_fail(&reader, "noncanonical or mismatched unified composition");
  qa_cleanup_pop(&guard);
  qa_app_unified_composition *identity = scope.identity;
  free(scope.sidecars);
  free(scope.parsed);
  return identity;
}
/* The TS identity value: {digest, composition:{schemaVersion, recipe, snapshotSchema,
 * actorConfigurations, sidecars}} rebuilt from a read composition. */
qa_save_value *qa_app_unified_composition_value(const qa_app_unified_composition *identity) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree, out);
  set_field(out, "digest", qa_save_value_string(identity->digest));
  set_field(out, "composition", composition_tree(identity->recipe, identity->sidecar_tree));
  qa_cleanup_pop(&guard);
  return out;
}
uint8_t *qa_app_unified_encode_composition(const qa_save_value *identity_tree, size_t *out_length) {
  qa_app_unified_composition *identity = qa_app_unified_read_composition(identity_tree);
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_composition, &identity);
  qa_save_value *value = qa_app_unified_composition_value(identity);
  qa_cleanup_pop(&guard);
  qa_app_unified_composition_destroy(identity);
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  if (!bytes) qa_range_error("out of memory");
  return bytes;
}
qa_app_unified_composition *qa_app_unified_decode_composition(const uint8_t *bytes, size_t length) {
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_tree_ref, &tree);
  qa_app_unified_composition *identity = qa_app_unified_read_composition(tree);
  qa_cleanup_pop(&guard);
  qa_save_value_free(tree);
  return identity;
}
static bool text_equal(const qa_save_value *a, const qa_save_value *b) {
  const char *x = text_of(a), *y = text_of(b);
  return x && y && strcmp(x, y) == 0;
}
typedef struct resolve_scope {
  qa_app_unified_composition *identity;
  qa_save_value *local;
  qa_app_unified_composition *check;
  struct { const char *old_id; const qa_save_value *mount; } *mounts;
  const char **used;
  char *new_id;
  const qa_app_unified_content_host *opened; /* TS `using opened = await openMountPlan(plan)` */
} resolve_scope;
static void resolve_scope_free(void *arg) {
  resolve_scope *scope = arg;
  if (scope->opened) {
    const qa_app_unified_content_host *host = scope->opened;
    scope->opened = NULL;
    host->close_plan(host->ctx);
  }
  qa_app_unified_composition_destroy(scope->identity);
  qa_save_value_free(scope->local);
  qa_app_unified_composition_destroy(scope->check);
  free(scope->mounts);
  free(scope->used);
  free(scope->new_id);
}
static const qa_save_value *resolve_mount_for(resolve_scope *scope, size_t count, const char *id) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(scope->mounts[i].old_id, id) == 0) return scope->mounts[i].mount;
  qa_raise("Error", 0, 0, "Unknown unified mount");
}
static void resolve_order(resolve_scope *scope, size_t count, qa_save_value *list) {
  for (size_t i = 0; i < list->item_count; i++) {
    const qa_save_value *mount = resolve_mount_for(scope, count, text_of(list->items[i]));
    qa_save_value *id = qa_save_value_string(text_of(object_get(object_get((qa_save_value *)mount, "identity"), "id")));
    if (!id) qa_range_error("out of memory");
    qa_save_value_free(list->items[i]);
    list->items[i] = id;
  }
}
/* TS resourceWithMount(resource, mountFor(id), resource.resolution.plan): the plan is kept. */
static void resolve_resource_mount(resolve_scope *scope, size_t count, qa_save_value *resource) {
  qa_save_value *provenance = object_get(resource, "provenance");
  const qa_save_value *mount = resolve_mount_for(scope, count,
      text_of(object_get(object_get(object_get(provenance, "mount"), "identity"), "id")));
  validate_path(text_of(object_get(resource, "requestedPath")));
  validate_path(text_of(object_get(provenance, "memberPath")));
  const char *kind = text_of(object_get(provenance, "kind"));
  const char *mount_kind = text_of(object_get((qa_save_value *)mount, "kind"));
  if (strcmp(kind, mount_kind) != 0) qa_raise("Error", 0, 0, "Unified resource mount kind differs");
  qa_save_value *resolution = object_get(resource, "resolution");
  if (strcmp(text_of(object_get(resolution, "kind")), "link") == 0) {
    validate_prefix(text_of(object_get(resolution, "sourcePrefix")));
    validate_path(text_of(object_get(resolution, "targetPath")));
  }
  set_field(provenance, "mount", clone_value(mount));
  char *id = tree_resource_id(resource);
  scope->new_id = id;
  set_field(resource, "id", qa_save_value_string(id));
  scope->new_id = NULL;
  free(id);
}
qa_save_value *qa_app_unified_resolve_composition(
    const qa_save_value *offered_identity, const qa_app_unified_content_host *host) {
  resolve_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, resolve_scope_free, &scope);
  scope.identity = qa_app_unified_read_composition(offered_identity);
  scope.local = clone_value(scope.identity->recipe);
  qa_save_value *plan = object_get(scope.local, "mounts");
  qa_save_value *mounts = object_get(plan, "mounts");
  size_t mount_count = mounts->item_count;
  scope.mounts = calloc(mount_count ? mount_count : 1, sizeof *scope.mounts);
  scope.used = calloc(mount_count ? mount_count : 1, sizeof *scope.used);
  if (!scope.mounts || !scope.used) qa_range_error("out of memory");
  for (size_t i = 0; i < mount_count; i++) {
    const qa_save_value *expected = mounts->items[i];
    const qa_save_value *identity = object_get((qa_save_value *)expected, "identity");
    const char *content = text_of(object_get((qa_save_value *)identity, "content"));
    bool archive = strcmp(text_of(object_get((qa_save_value *)expected, "kind")), "archive") == 0;
    size_t count = 0;
    const qa_save_value *const *candidates = host->catalog_mounts(host->ctx, (qa_content_id)content, &count);
    const qa_save_value *local = NULL;
    for (size_t c = 0; c < count && !local; c++) {
      qa_save_value *candidate = (qa_save_value *)candidates[c];
      const char *candidate_id = text_of(object_get(object_get(candidate, "identity"), "id"));
      bool taken = false;
      for (size_t u = 0; u < i; u++) if (strcmp(scope.used[u], candidate_id) == 0) taken = true;
      if (taken || !text_equal(object_get(object_get(candidate, "identity"), "content"), object_get((qa_save_value *)identity, "content")))
        continue;
      bool kind_matches = archive
          ? strcmp(text_of(object_get(candidate, "kind")), "archive") == 0
            && text_equal(object_get(candidate, "format"), object_get((qa_save_value *)expected, "format"))
            && text_equal(object_get(candidate, "archiveDigest"), object_get((qa_save_value *)expected, "archiveDigest"))
          : strcmp(text_of(object_get(candidate, "kind")), "loose") == 0;
      if (kind_matches) local = candidate;
    }
    if (!local)
      qa_raise("Error", 0, 0, "Installed content lacks unified mount %s/%s", content,
          text_of(object_get((qa_save_value *)identity, "id")));
    scope.used[i] = text_of(object_get(object_get((qa_save_value *)local, "identity"), "id"));
    scope.mounts[i].old_id = text_of(object_get((qa_save_value *)identity, "id"));
    scope.mounts[i].mount = local;
  }
  /* Rebinding replaces nodes the old ids live in: keep the old ids alive in a copy first. */
  qa_save_value *old_mounts = clone_value(mounts);
  qa_cleanup old_guard;
  qa_cleanup_push(&old_guard, cleanup_tree, old_mounts);
  for (size_t i = 0; i < mount_count; i++)
    scope.mounts[i].old_id = text_of(object_get(object_get(old_mounts->items[i], "identity"), "id"));
  for (size_t i = 0; i < mount_count; i++) {
    qa_save_value_free(mounts->items[i]);
    mounts->items[i] = clone_value(scope.mounts[i].mount);
  }
  resolve_order(&scope, mount_count, object_get(plan, "defaultOrder"));
  qa_save_value *prefix_orders = object_get(plan, "prefixOrders");
  for (size_t i = 0; i < prefix_orders->item_count; i++)
    resolve_order(&scope, mount_count, object_get(prefix_orders->items[i], "mounts"));
  qa_save_value *map = object_get(scope.local, "map");
  resolve_resource_mount(&scope, mount_count, object_get(map, "geometry"));
  qa_save_value *list = object_get(scope.local, "resources");
  for (size_t i = 0; i < list->item_count; i++) resolve_resource_mount(&scope, mount_count, list->items[i]);
  qa_save_value *execution = object_get(scope.local, "execution");
  for (size_t i = 0; i < execution->item_count; i++) {
    const char *kind = text_of(object_get(execution->items[i], "kind"));
    if (kind && strcmp(kind, "typescript") == 0) continue;
    resolve_resource_mount(&scope, mount_count, object_get(execution->items[i], "artifact"));
  }
  qa_cleanup_pop(&old_guard);
  qa_save_value_free(old_mounts);
  /* TS: each distinct resource (by id) is read, and its precedence checked against the plan. */
  host->open_plan(host->ctx, plan);
  scope.opened = host;
  const char *plan_id = text_of(object_get(plan, "id"));
  qa_save_value *references[1] = {object_get(map, "geometry")};
  size_t total = 1 + list->item_count + execution->item_count;
  for (size_t i = 0; i < total; i++) {
    qa_save_value *resource = i == 0 ? references[0]
        : i <= list->item_count ? list->items[i - 1]
        : object_get(execution->items[i - 1 - list->item_count], "artifact");
    if (!resource) continue;
    const char *id = text_of(object_get(resource, "id"));
    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++) {
      qa_save_value *earlier = j == 0 ? references[0]
          : j <= list->item_count ? list->items[j - 1]
          : object_get(execution->items[j - 1 - list->item_count], "artifact");
      if (earlier && strcmp(text_of(object_get(earlier, "id")), id) == 0) seen = true;
    }
    if (seen) continue;
    host->plan_read(host->ctx, resource);
    qa_save_value *resolution = object_get(resource, "resolution");
    if (strcmp(text_of(object_get(resolution, "plan")), plan_id) == 0) {
      const char *resolved = host->plan_resolve_id(host->ctx, text_of(object_get(resource, "requestedPath")));
      if (!resolved || strcmp(resolved, id) != 0)
        qa_raise("Error", 0, 0, "Unified resource precedence differs: %s", text_of(object_get(resource, "requestedPath")));
    } else if (strcmp(text_of(object_get(resolution, "kind")), "link") != 0
        && object_get(resolution, "rank")->number >= (double)mount_count) {
      qa_raise("Error", 0, 0, "Unified resource precedence is outside its mount plan");
    }
  }
  scope.opened = NULL;
  host->close_plan(host->ctx);
  scope.check = qa_app_unified_create_composition(scope.local, scope.identity->sidecars, scope.identity->sidecar_count);
  if (strcmp(scope.check->digest, scope.identity->digest) != 0)
    qa_raise("Error", 0, 0, "Local unified recipe differs from admitted composition");
  qa_cleanup_pop(&guard);
  qa_save_value *local = scope.local;
  scope.local = NULL;
  resolve_scope_free(&scope);
  return local;
}
typedef struct load_scope {
  const qa_app_unified_content_host *host;
  qa_save_value *local;
  void *loaded;
  qa_app_unified_composition *built;
} load_scope;
static void load_scope_free(void *arg) {
  load_scope *scope = arg;
  qa_save_value_free(scope->local);
  qa_app_unified_composition_destroy(scope->built);
  /* TS catch: loaded.close() before rethrowing. */
  if (scope->loaded) scope->host->close_content(scope->host->ctx, scope->loaded);
}
void *qa_app_unified_load_content(const qa_save_value *identity_tree,
    const qa_app_unified_content_host *host) {
  load_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.host = host;
  qa_cleanup guard;
  qa_cleanup_push(&guard, load_scope_free, &scope);
  scope.local = qa_app_unified_resolve_composition(identity_tree, host);
  qa_content_id entities = (qa_content_id)host->map_entities_content(host->ctx, scope.local);
  const char *family = host->product_family(host->ctx, entities);
  const char *edition = host->product_edition(host->ctx, entities);
  if (strcmp(family, "q1") == 0 && strcmp(edition, "quakeworld") == 0) family = "qw";
  scope.loaded = host->load_content(host->ctx, scope.local, family);
  size_t sidecar_count = 0;
  const qa_app_unified_map_sidecar *sidecars = host->loaded_sidecars(host->ctx, scope.loaded, &sidecar_count);
  scope.built = qa_app_unified_build_composition(host->loaded_recipe(host->ctx, scope.loaded), sidecars, sidecar_count);
  const qa_save_value *digest_value = qa_save_value_get(identity_tree, "digest");
  const char *digest = text_of(digest_value);
  if (!digest || strcmp(scope.built->digest, digest) != 0)
    qa_raise("Error", 0, 0, "Unified map sidecars differ from the authoritative world");
  qa_cleanup_pop(&guard);
  void *loaded = scope.loaded;
  scope.loaded = NULL;
  load_scope_free(&scope);
  return loaded;
}
qa_resolved_resource_reference qa_app_unified_resolve_resource(
    const qa_app_unified_content_host *host, const qa_app_unified_resource_key *key) {
  check_forward_slashes(key->path);
  qa_resolved_resource_reference reference;
  bool opened = host->open_resource(host->ctx, key->content, key->path, &reference);
  qa_app_unified_resource_key actual;
  if (opened) actual = qa_app_unified_resource_key_from_reference(&reference);
  if (!opened || !key_matches(&actual, key))
    qa_raise("Error", 0, 0, "Unified resource is absent or differs: %s/%s", key->content, key->path);
  return reference;
}
