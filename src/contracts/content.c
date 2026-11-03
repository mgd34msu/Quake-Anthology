/* Port of src/contracts/content.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool is_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* TS pattern ^[a-zA-Z0-9][a-zA-Z0-9._+-]*$ over a NUL-terminated string. */
static bool valid_identity_part(const char *value) {
  if (value == NULL || !is_alnum(value[0])) return false;
  for (size_t i = 1; value[i] != '\0'; i++) {
    char c = value[i];
    if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != '-') return false;
  }
  return true;
}

static const char *identity_part(const char *value) {
  if (!valid_identity_part(value)) {
    qa_range_error("Invalid content identity component: %s", value != NULL ? value : "");
  }
  return value;
}

/* Malloc'd printf-style build; NULL on allocation failure. */
static char *build_id(const char *prefix, const char *first, const char *second,
                      const char *third) {
  size_t length = strlen(prefix) + strlen(first) + 1 + strlen(second);
  if (third != NULL) length += 1 + strlen(third);
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  if (third != NULL) {
    (void)snprintf(out, length + 1, "%s%s:%s:%s", prefix, first, second, third);
  } else {
    (void)snprintf(out, length + 1, "%s%s:%s", prefix, first, second);
  }
  return out;
}

const char *qa_game_family_name(qa_game_family family) {
  switch (family) {
    case QA_GAME_FAMILY_Q1: return "q1";
    case QA_GAME_FAMILY_Q2: return "q2";
    case QA_GAME_FAMILY_Q3: return "q3";
  }
  return "";
}

bool qa_game_family_parse(const char *text, qa_game_family *out) {
  if (text == NULL || out == NULL) return false;
  if (strcmp(text, "q1") == 0) { *out = QA_GAME_FAMILY_Q1; return true; }
  if (strcmp(text, "q2") == 0) { *out = QA_GAME_FAMILY_Q2; return true; }
  if (strcmp(text, "q3") == 0) { *out = QA_GAME_FAMILY_Q3; return true; }
  return false;
}

char *qa_create_content_id(qa_content_identity identity) {
  const char *edition = identity_part(identity.edition);
  const char *package = identity_part(identity.package);
  const char *revision = identity_part(identity.revision);
  size_t length = 2 + 1 + strlen(edition) + 1 + strlen(package) + 1 + strlen(revision);
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  (void)snprintf(out, length + 1, "%s:%s:%s:%s", qa_game_family_name(identity.family),
                 edition, package, revision);
  return out;
}

bool qa_is_content_id(const char *value) {
  if (value == NULL) return false; /* TS typeof check */
  qa_game_family family;
  char head[3] = {value[0], value[0] != '\0' ? value[1] : '\0', '\0'};
  if (value[0] == '\0' || value[1] == '\0' || value[2] != ':') return false;
  if (!qa_game_family_parse(head, &family)) return false;
  const char *part = value + 3;
  for (int index = 0; index < 3; index++) {
    const char *colon = strchr(part, ':');
    size_t part_length = colon != NULL ? (size_t)(colon - part) : strlen(part);
    if (index < 2 && colon == NULL) return false;
    if (index == 2 && colon != NULL) return false;
    if (part_length == 0 || !is_alnum(part[0])) return false;
    for (size_t i = 1; i < part_length; i++) {
      char c = part[i];
      if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != '-') return false;
    }
    part = colon != NULL ? colon + 1 : part + part_length;
  }
  return true;
}

char *qa_create_recipe_id(const char *name_space, const char *revision) {
  return build_id("recipe:", identity_part(name_space), identity_part(revision), NULL);
}

char *qa_create_mount_id(const char *name_space, const char *name) {
  return build_id("mount:", identity_part(name_space), identity_part(name), NULL);
}

char *qa_create_mount_plan_id(const char *name_space, const char *revision) {
  return build_id("mount-plan:", identity_part(name_space), identity_part(revision), NULL);
}

static bool is_hex(char c, bool allow_upper) {
  if (c >= '0' && c <= '9') return true;
  if (c >= 'a' && c <= 'f') return true;
  return allow_upper && c >= 'A' && c <= 'F';
}

char *qa_create_content_digest(const char *hex) {
  if (hex == NULL || strlen(hex) != 64) {
    qa_range_error("Expected a SHA-256 digest with 64 hexadecimal digits");
  }
  for (size_t i = 0; i < 64; i++) {
    if (!is_hex(hex[i], true)) {
      qa_range_error("Expected a SHA-256 digest with 64 hexadecimal digits");
    }
  }
  char *out = (char *)malloc(7 + 64 + 1);
  if (out == NULL) return NULL;
  memcpy(out, "sha256:", 7);
  for (size_t i = 0; i < 64; i++) {
    char c = hex[i];
    if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a'); /* TS toLowerCase */
    out[7 + i] = c;
  }
  out[71] = '\0';
  return out;
}

bool qa_is_content_digest(const char *value) {
  if (value == NULL || strlen(value) != 71) return false;
  if (strncmp(value, "sha256:", 7) != 0) return false;
  for (size_t i = 7; i < 71; i++) {
    if (!is_hex(value[i], false)) return false;
  }
  return true;
}

qa_mount_identity qa_create_mount_identity(qa_mount_id id, qa_content_id content,
                                           int64_t generation) {
  if (generation < 0 || generation > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Mount generation must be a nonnegative safe integer");
  }
  qa_mount_identity identity = {id, content, generation};
  return identity;
}

/* TS encodeURIComponent over the string's UTF-8 bytes (C strings are
 * byte strings; callers hold UTF-8). Unescaped: A-Z a-z 0-9 - _ . ! ~ * ' ( ) */
static bool uri_unescaped(unsigned char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  switch (c) {
    case '-': case '_': case '.': case '!': case '~':
    case '*': case '\'': case '(': case ')':
      return true;
    default:
      return false;
  }
}

static char *encode_uri_component(const char *value) {
  size_t length = 0;
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    length += uri_unescaped(*p) ? 1 : 3;
  }
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  static const char hex_digits[] = "0123456789ABCDEF";
  char *cursor = out;
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    if (uri_unescaped(*p)) {
      *cursor++ = (char)*p;
    } else {
      *cursor++ = '%';
      *cursor++ = hex_digits[*p >> 4];
      *cursor++ = hex_digits[*p & 0xf];
    }
  }
  *cursor = '\0';
  return out;
}

/* TS resourcePath(): rejects empty, absolute, backslash, and
 * empty/"." /".." '/'-segments. A NUL byte cannot appear in a C
 * string, so the TS includes("\0") arm needs no check. */
static const char *resource_path(const char *value) {
  bool invalid = value == NULL || value[0] == '\0' || value[0] == '/' ||
                 strchr(value, '\\') != NULL;
  if (!invalid) {
    const char *part = value;
    for (;;) {
      const char *slash = strchr(part, '/');
      size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
      if (part_length == 0 ||
          (part_length == 1 && part[0] == '.') ||
          (part_length == 2 && part[0] == '.' && part[1] == '.')) {
        invalid = true;
        break;
      }
      if (slash == NULL) break;
      part = slash + 1;
    }
  }
  if (invalid) {
    qa_range_error("Expected a relative resource path: %s", value != NULL ? value : "");
  }
  return value;
}

static bool nonnegative_safe(int64_t value) {
  return value >= 0 && value <= QA_MAX_SAFE_INTEGER;
}

char *qa_create_resource_id(const qa_resolved_resource_reference *resource) {
  const qa_resource_provenance *provenance = &resource->provenance;
  const qa_resource_resolution *resolution = &resource->resolution;
  const bool is_archive = provenance->kind == QA_CONTENT_MOUNT_ARCHIVE;
  const qa_mount_identity *identity = is_archive
      ? &provenance->archive_mount->identity
      : &provenance->loose_mount->identity;
  (void)resource_path(resource->requested_path);
  (void)resource_path(provenance->member_path);
  if (!nonnegative_safe(resource->byte_length)) {
    qa_range_error("Resource byte length must be a nonnegative safe integer");
  }
  if (resolution->kind != QA_RESOURCE_RESOLUTION_LINK &&
      !nonnegative_safe(resolution->rank)) {
    qa_range_error("Resource precedence rank must be a nonnegative safe integer");
  }
  if (is_archive && !nonnegative_safe(provenance->member_index)) {
    qa_range_error("Archive member index must be a nonnegative safe integer");
  }

  char *source = NULL;
  if (is_archive) {
    char *encoded_path = encode_uri_component(provenance->archive_mount->archive_path);
    if (encoded_path == NULL) return NULL;
    size_t source_length = strlen(encoded_path) + 1 +
                           strlen(provenance->archive_mount->archive_digest) + 24;
    source = (char *)malloc(source_length);
    if (source != NULL) {
      (void)snprintf(source, source_length, "%s:%s:%lld", encoded_path,
                     provenance->archive_mount->archive_digest,
                     (long long)provenance->member_index);
    }
    free(encoded_path);
  } else {
    source = encode_uri_component(provenance->loose_mount->root_path);
  }
  if (source == NULL) return NULL;

  char *resolution_key = NULL;
  if (resolution->kind == QA_RESOURCE_RESOLUTION_DEFAULT_ORDER) {
    size_t key_length = strlen(resolution->plan) + 32;
    resolution_key = (char *)malloc(key_length);
    if (resolution_key != NULL) {
      (void)snprintf(resolution_key, key_length, "%s:default:%lld", resolution->plan,
                     (long long)resolution->rank);
    }
  } else if (resolution->kind == QA_RESOURCE_RESOLUTION_PREFIX_ORDER) {
    char *encoded = encode_uri_component(resolution->prefix);
    if (encoded != NULL) {
      size_t key_length = strlen(resolution->plan) + strlen(encoded) + 32;
      resolution_key = (char *)malloc(key_length);
      if (resolution_key != NULL) {
        (void)snprintf(resolution_key, key_length, "%s:prefix:%s:%lld", resolution->plan,
                       encoded, (long long)resolution->rank);
      }
      free(encoded);
    }
  } else {
    char *encoded_source = encode_uri_component(resolution->source_prefix);
    char *encoded_target = encoded_source != NULL
        ? encode_uri_component(resolution->target_path) : NULL;
    if (encoded_target != NULL) {
      size_t key_length = strlen(resolution->plan) + strlen(encoded_source) +
                          strlen(encoded_target) + 16;
      resolution_key = (char *)malloc(key_length);
      if (resolution_key != NULL) {
        (void)snprintf(resolution_key, key_length, "%s:link:%s:%s", resolution->plan,
                       encoded_source, encoded_target);
      }
    }
    free(encoded_source);
    free(encoded_target);
  }
  if (resolution_key == NULL) {
    free(source);
    return NULL;
  }

  char *member = encode_uri_component(provenance->member_path);
  if (member == NULL) {
    free(source);
    free(resolution_key);
    return NULL;
  }
  size_t length = strlen("resource:") + strlen(identity->content) + 1 +
                  strlen(identity->id) + 24 + strlen(source) + 1 + strlen(member) + 1 +
                  strlen(resource->digest) + 1 + strlen(resolution_key);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    (void)snprintf(out, length + 1, "resource:%s:%s:%lld:%s:%s:%s:%s", identity->content,
                   identity->id, (long long)identity->generation, source, member,
                   resource->digest, resolution_key);
  }
  free(member);
  free(resolution_key);
  free(source);
  return out;
}

/* ---- owned ResolvedResourceReference values ---------------------- */

/* Copies a nullable string into *out; false only on OOM. */
static bool owned_text(const char *source, const char **out) {
  if (source == NULL) { *out = NULL; return true; }
  size_t length = strlen(source) + 1;
  char *copy = (char *)malloc(length);
  if (copy == NULL) return false;
  memcpy(copy, source, length);
  *out = copy;
  return true;
}

static bool owned_identity(const qa_mount_identity *source, qa_mount_identity *out) {
  out->generation = source->generation;
  return owned_text(source->id, &out->id) && owned_text(source->content, &out->content);
}

bool qa_resolved_resource_copy(const qa_resolved_resource_reference *source,
                               qa_resolved_resource_reference *out) {
  qa_resolved_resource_reference copy;
  memset(&copy, 0, sizeof copy);
  copy.provenance.kind = source->provenance.kind;
  copy.provenance.member_index = source->provenance.member_index;
  copy.byte_length = source->byte_length;
  copy.parsed_order = source->parsed_order;
  copy.resolution.kind = source->resolution.kind;
  copy.resolution.rank = source->resolution.rank;
  bool ok = owned_text(source->id, &copy.id) &&
            owned_text(source->requested_path, &copy.requested_path) &&
            owned_text(source->digest, &copy.digest) &&
            owned_text(source->provenance.member_path, &copy.provenance.member_path) &&
            owned_text(source->resolution.plan, &copy.resolution.plan) &&
            owned_text(source->resolution.prefix, &copy.resolution.prefix) &&
            owned_text(source->resolution.source_prefix, &copy.resolution.source_prefix) &&
            owned_text(source->resolution.target_path, &copy.resolution.target_path);
  if (ok && source->provenance.archive_mount != NULL) {
    const qa_archive_mount *from = source->provenance.archive_mount;
    qa_archive_mount *mount = (qa_archive_mount *)calloc(1, sizeof *mount);
    copy.provenance.archive_mount = mount;
    ok = mount != NULL;
    if (ok) {
      mount->format = from->format;
      ok = owned_identity(&from->identity, &mount->identity) &&
           owned_text(from->archive_path, &mount->archive_path) &&
           owned_text(from->archive_digest, &mount->archive_digest);
    }
  }
  if (ok && source->provenance.loose_mount != NULL) {
    const qa_loose_mount *from = source->provenance.loose_mount;
    qa_loose_mount *mount = (qa_loose_mount *)calloc(1, sizeof *mount);
    copy.provenance.loose_mount = mount;
    ok = mount != NULL;
    if (ok)
      ok = owned_identity(&from->identity, &mount->identity) &&
           owned_text(from->root_path, &mount->root_path);
  }
  if (!ok) {
    qa_resolved_resource_release(&copy);
    return false;
  }
  *out = copy;
  return true;
}

void qa_resolved_resource_release(qa_resolved_resource_reference *resource) {
  free((void *)resource->id);
  free((void *)resource->requested_path);
  free((void *)resource->digest);
  free((void *)resource->provenance.member_path);
  free((void *)resource->resolution.plan);
  free((void *)resource->resolution.prefix);
  free((void *)resource->resolution.source_prefix);
  free((void *)resource->resolution.target_path);
  qa_archive_mount *archive = (qa_archive_mount *)resource->provenance.archive_mount;
  if (archive != NULL) {
    free((void *)archive->identity.id);
    free((void *)archive->identity.content);
    free((void *)archive->archive_path);
    free((void *)archive->archive_digest);
    free(archive);
  }
  qa_loose_mount *loose = (qa_loose_mount *)resource->provenance.loose_mount;
  if (loose != NULL) {
    free((void *)loose->identity.id);
    free((void *)loose->identity.content);
    free((void *)loose->root_path);
    free(loose);
  }
  memset(resource, 0, sizeof *resource);
}
