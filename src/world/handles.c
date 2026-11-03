/* Closure handles (see include/qa/world/handles.h). GPL-2.0-or-later. */
#include "qa/world/handles.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct handle_entry {
  uintptr_t serial;
  void *state;
};
static struct handle_entry *entries;
static size_t entry_count, entry_capacity;
static uintptr_t next_serial;

void *qa_world_handle_open(void *state) {
  if (entry_count == entry_capacity) {
    size_t capacity = entry_capacity != 0 ? entry_capacity * 2 : 16;
    struct handle_entry *grown = realloc(entries, capacity * sizeof *grown);
    if (grown == NULL) abort();
    entries = grown;
    entry_capacity = capacity;
  }
  uintptr_t serial = ++next_serial;
  if (serial == 0) abort(); /* 2^64 handles: unreachable */
  entries[entry_count].serial = serial;
  entries[entry_count].state = state;
  entry_count++;
  return (void *)serial;
}

void *qa_world_handle_get(const void *token) {
  uintptr_t serial = (uintptr_t)token;
  /* Newest first: open handles nest like the calls that open them. */
  for (size_t i = entry_count; i-- > 0;)
    if (entries[i].serial == serial) return entries[i].state;
  return NULL;
}

void qa_world_handle_close(const void *token) {
  uintptr_t serial = (uintptr_t)token;
  for (size_t i = entry_count; i-- > 0;)
    if (entries[i].serial == serial) {
      entries[i] = entries[entry_count - 1];
      entry_count--;
      if (entry_count == 0) {
        free(entries);
        entries = NULL;
        entry_capacity = 0;
      }
      return;
    }
}

unsigned long qa_world_handle_open_count(void) { return (unsigned long)entry_count; }
