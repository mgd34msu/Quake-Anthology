/*
 * Port of src/formats/q12-model/pose-lru.ts: a bounded recency cache for
 * interpolated poses, keyed by owner identity plus a lerp string.
 *
 * C accommodations: TS keys owners through a WeakMap, so an id dies with its
 * object. C keys by address, so an owner that is freed must be forgotten with
 * qa_pose_lru_forget before its address can be reused; its old entries then
 * become unreachable and age out under the limit. Values are owned by the
 * cache and released through `release` on eviction, replacement and destroy.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q12_MODEL_POSE_LRU_H
#define QA_FORMATS_Q12_MODEL_POSE_LRU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_pose_lru_entry {
  char *key;
  void *value;
} qa_pose_lru_entry;

typedef struct qa_pose_lru_owner {
  const void *owner;
  double id;
} qa_pose_lru_owner;

typedef struct qa_pose_lru {
  size_t limit;
  /* Insertion (recency) order, oldest first, like the TS Map. */
  qa_pose_lru_entry *entries;
  size_t count, capacity;
  qa_pose_lru_owner *owners;
  size_t owner_count, owner_capacity;
  double next_id;
  void (*release)(void *value);
} qa_pose_lru;

void qa_pose_lru_init(qa_pose_lru *lru, size_t limit, void (*release)(void *value));
void qa_pose_lru_destroy(qa_pose_lru *lru);
/* TS key(owners, suffix): returns a malloc'd key; caller frees. */
char *qa_pose_lru_key(qa_pose_lru *lru, const void *const *owners, size_t owner_count, const char *suffix);
/* Borrowed value or NULL (TS undefined); a hit becomes the most recent. */
void *qa_pose_lru_get(qa_pose_lru *lru, const char *key);
/* Takes ownership of value; the key is copied. */
void qa_pose_lru_set(qa_pose_lru *lru, const char *key, void *value);
size_t qa_pose_lru_size(const qa_pose_lru *lru);
/* Drops an owner's id (the WeakMap entry dying with its object). */
void qa_pose_lru_forget(qa_pose_lru *lru, const void *owner);

#endif
