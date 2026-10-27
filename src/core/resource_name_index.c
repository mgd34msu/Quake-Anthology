/* Port of src/core/resource-name-index.ts. GPL-2.0-or-later. */
#include "qa/core/resource_name_index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static bool is_reserved(const qa_resource_name_index *index, int64_t slot) {
  for (size_t i = 0; i < index->reserved_count; i++) {
    if (index->reserved[i] == slot) return true;
  }
  return false;
}

static void advance(qa_resource_name_index *index) {
  while (index->free_slot < (int64_t)index->capacity &&
         (is_reserved(index, index->free_slot) ||
          index->values[index->free_slot][0] != '\0')) {
    index->free_slot++;
  }
}

static qa_resource_name_slots *find_entry(qa_resource_name_index *index,
                                          const char *name) {
  for (size_t i = 0; i < index->name_count; i++) {
    if (strcmp(index->names[i].name, name) == 0) return &index->names[i];
  }
  return NULL;
}

static const qa_resource_name_slots *find_entry_const(
    const qa_resource_name_index *index, const char *name) {
  for (size_t i = 0; i < index->name_count; i++) {
    if (strcmp(index->names[i].name, name) == 0) return &index->names[i];
  }
  return NULL;
}

static void remove_entry_at(qa_resource_name_index *index, size_t at) {
  free(index->names[at].name);
  free(index->names[at].slots);
  for (size_t i = at + 1; i < index->name_count; i++) {
    index->names[i - 1] = index->names[i];
  }
  index->name_count--;
}

static void clear_names(qa_resource_name_index *index) {
  for (size_t i = 0; i < index->name_count; i++) {
    free(index->names[i].name);
    free(index->names[i].slots);
  }
  index->name_count = 0;
}

bool qa_resource_name_index_init(qa_resource_name_index *index,
                                 int64_t capacity, const int64_t *reserved,
                                 size_t reserved_count) {
  if (capacity < 1 || capacity > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid resource capacity");
  }
  memset(index, 0, sizeof *index);
  index->capacity = (size_t)capacity;
  index->values = (char **)calloc(index->capacity, sizeof(char *));
  if (index->values == NULL) return false;
  for (size_t i = 0; i < index->capacity; i++) {
    index->values[i] = copy_text("");
    if (index->values[i] == NULL) {
      qa_resource_name_index_destroy(index);
      return false;
    }
  }
  if (reserved_count > 0) {
    index->reserved =
        (int64_t *)malloc(reserved_count * sizeof(int64_t));
    if (index->reserved == NULL) {
      qa_resource_name_index_destroy(index);
      return false;
    }
    memcpy(index->reserved, reserved, reserved_count * sizeof(int64_t));
    index->reserved_count = reserved_count;
  }
  index->free_slot = 1;
  advance(index);
  return true;
}

void qa_resource_name_index_destroy(qa_resource_name_index *index) {
  if (index->values != NULL) {
    for (size_t i = 0; i < index->capacity; i++) free(index->values[i]);
    free(index->values);
  }
  free(index->reserved);
  clear_names(index);
  free(index->names);
  memset(index, 0, sizeof *index);
}

void qa_resource_name_index_clear(qa_resource_name_index *index) {
  for (size_t i = 0; i < index->capacity; i++) {
    free(index->values[i]);
    index->values[i] = copy_text("");
  }
  clear_names(index);
  index->free_slot = 1;
  advance(index);
}

void qa_resource_name_index_set(qa_resource_name_index *index, int64_t slot,
                                const char *value) {
  if (slot <= 0 || slot >= (int64_t)index->capacity ||
      is_reserved(index, slot)) {
    return;
  }
  const char *previous = index->values[slot];
  if (strcmp(previous, value) == 0) return;
  if (previous[0] != '\0') {
    qa_resource_name_slots *entry = find_entry(index, previous);
    if (entry != NULL) {
      for (size_t i = 0; i < entry->slot_count; i++) {
        if (entry->slots[i] == slot) {
          for (size_t j = i + 1; j < entry->slot_count; j++) {
            entry->slots[j - 1] = entry->slots[j];
          }
          entry->slot_count--;
          break;
        }
      }
      if (entry->slot_count == 0) {
        remove_entry_at(index, (size_t)(entry - index->names));
      } else if (entry->first == slot) {
        entry->first = (int64_t)index->capacity;
        for (size_t i = 0; i < entry->slot_count; i++) {
          if (entry->slots[i] < entry->first) entry->first = entry->slots[i];
        }
      }
    }
  }
  char *copy = copy_text(value);
  if (copy == NULL) return; /* No TS counterpart: allocation failure. */
  free(index->values[slot]);
  index->values[slot] = copy;
  if (value[0] == '\0') {
    if (slot < index->free_slot) index->free_slot = slot;
  } else {
    qa_resource_name_slots *entry = find_entry(index, value);
    if (entry == NULL) {
      if (index->name_count == index->name_capacity) {
        size_t grown = index->name_capacity == 0 ? 8 : index->name_capacity * 2;
        qa_resource_name_slots *names = (qa_resource_name_slots *)realloc(
            index->names, grown * sizeof(qa_resource_name_slots));
        if (names == NULL) return;
        index->names = names;
        index->name_capacity = grown;
      }
      entry = &index->names[index->name_count];
      entry->name = copy_text(value);
      entry->slots = (int64_t *)malloc(4 * sizeof(int64_t));
      if (entry->name == NULL || entry->slots == NULL) {
        free(entry->name);
        free(entry->slots);
        return;
      }
      entry->slot_capacity = 4;
      entry->slot_count = 1;
      entry->slots[0] = slot;
      entry->first = slot;
      index->name_count++;
    } else {
      if (entry->slot_count == entry->slot_capacity) {
        size_t grown = entry->slot_capacity * 2;
        int64_t *slots =
            (int64_t *)realloc(entry->slots, grown * sizeof(int64_t));
        if (slots == NULL) return;
        entry->slots = slots;
        entry->slot_capacity = grown;
      }
      entry->slots[entry->slot_count++] = slot;
      if (slot < entry->first) entry->first = slot;
    }
    if (slot == index->free_slot) advance(index);
  }
}

int64_t qa_resource_name_index_find(const qa_resource_name_index *index,
                                    const char *name) {
  if (name[0] == '\0') return 0;
  const qa_resource_name_slots *entry = find_entry_const(index, name);
  if (entry != NULL && entry->first < index->free_slot) return entry->first;
  return index->free_slot < (int64_t)index->capacity ? index->free_slot : -1;
}
