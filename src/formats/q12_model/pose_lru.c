/* Port of src/formats/q12-model/pose-lru.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/pose_lru.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

void qa_pose_lru_init(qa_pose_lru *lru, size_t limit, void (*release)(void *value)) {
  memset(lru, 0, sizeof *lru);
  lru->limit = limit;
  lru->release = release;
}

void qa_pose_lru_destroy(qa_pose_lru *lru) {
  if (lru == NULL) return;
  for (size_t i = 0; i < lru->count; i++) {
    free(lru->entries[i].key);
    if (lru->release != NULL) lru->release(lru->entries[i].value);
  }
  free(lru->entries);
  free(lru->owners);
  memset(lru, 0, sizeof *lru);
}

static double owner_id(qa_pose_lru *lru, const void *owner) {
  for (size_t i = 0; i < lru->owner_count; i++) {
    if (lru->owners[i].owner == owner) return lru->owners[i].id;
  }
  if (lru->owner_count == lru->owner_capacity) {
    lru->owner_capacity = lru->owner_capacity ? lru->owner_capacity * 2 : 16;
    lru->owners = xrealloc(lru->owners, lru->owner_capacity * sizeof *lru->owners);
  }
  double id = lru->next_id++;
  lru->owners[lru->owner_count].owner = owner;
  lru->owners[lru->owner_count].id = id;
  lru->owner_count++;
  return id;
}

char *qa_pose_lru_key(qa_pose_lru *lru, const void *const *owners, size_t owner_count, const char *suffix) {
  size_t length = strlen(suffix);
  char *key = xrealloc(NULL, length + 1);
  memcpy(key, suffix, length + 1);
  for (size_t i = 0; i < owner_count; i++) {
    const char *id = QA_JS_NUMBER(owner_id(lru, owners[i]));
    size_t id_length = strlen(id);
    char *next = xrealloc(NULL, id_length + 1 + length + 1);
    memcpy(next, id, id_length);
    next[id_length] = '|';
    memcpy(next + id_length + 1, key, length + 1);
    free(key);
    key = next;
    length += id_length + 1;
  }
  return key;
}

static size_t find(const qa_pose_lru *lru, const char *key) {
  for (size_t i = 0; i < lru->count; i++) {
    if (strcmp(lru->entries[i].key, key) == 0) return i;
  }
  return lru->count;
}

/* Map delete then set: the entry moves to the newest position. */
static void move_to_end(qa_pose_lru *lru, size_t at) {
  qa_pose_lru_entry entry = lru->entries[at];
  memmove(&lru->entries[at], &lru->entries[at + 1], (lru->count - at - 1) * sizeof entry);
  lru->entries[lru->count - 1] = entry;
}

void *qa_pose_lru_get(qa_pose_lru *lru, const char *key) {
  size_t at = find(lru, key);
  if (at == lru->count) return NULL;
  void *value = lru->entries[at].value;
  move_to_end(lru, at);
  return value;
}

void qa_pose_lru_set(qa_pose_lru *lru, const char *key, void *value) {
  size_t at = find(lru, key);
  if (at < lru->count) {
    void *old = lru->entries[at].value;
    move_to_end(lru, at);
    lru->entries[lru->count - 1].value = value;
    if (old != value && lru->release != NULL) lru->release(old);
  } else {
    if (lru->count == lru->capacity) {
      lru->capacity = lru->capacity ? lru->capacity * 2 : 16;
      lru->entries = xrealloc(lru->entries, lru->capacity * sizeof *lru->entries);
    }
    size_t length = strlen(key) + 1;
    char *copy = xrealloc(NULL, length);
    memcpy(copy, key, length);
    lru->entries[lru->count].key = copy;
    lru->entries[lru->count].value = value;
    lru->count++;
  }
  if (lru->count > lru->limit) {
    qa_pose_lru_entry oldest = lru->entries[0];
    memmove(&lru->entries[0], &lru->entries[1], (lru->count - 1) * sizeof oldest);
    lru->count--;
    free(oldest.key);
    if (lru->release != NULL) lru->release(oldest.value);
  }
}

size_t qa_pose_lru_size(const qa_pose_lru *lru) { return lru->count; }

void qa_pose_lru_forget(qa_pose_lru *lru, const void *owner) {
  for (size_t i = 0; i < lru->owner_count; i++) {
    if (lru->owners[i].owner == owner) {
      lru->owners[i] = lru->owners[lru->owner_count - 1];
      lru->owner_count--;
      return;
    }
  }
}
