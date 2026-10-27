/* Port of src/input/binding-store.ts. GPL-2.0-or-later. */
#include "qa/input/binding_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/support.h"

char *qa_physical_input_key(const qa_physical_input *input) {
  char buffer[256];
  char *first = NULL, *second = NULL;
  switch (input->kind) {
    case QA_PHYSICAL_INPUT_KEY:
      first = qa_input_js_number(input->code);
      if (first == NULL) return NULL;
      snprintf(buffer, sizeof buffer, "key:%s", first);
      break;
    case QA_PHYSICAL_INPUT_MOUSE_BUTTON:
      first = qa_input_js_number(input->button);
      if (first == NULL) return NULL;
      snprintf(buffer, sizeof buffer, "mouse:%s", first);
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_BUTTON:
      first = qa_input_js_number(input->device);
      second = qa_input_js_number(input->button);
      if (first == NULL || second == NULL) {
        free(first);
        free(second);
        return NULL;
      }
      snprintf(buffer, sizeof buffer, "pad:%s:button:%s", first, second);
      break;
    case QA_PHYSICAL_INPUT_CONTROLLER_AXIS:
      first = qa_input_js_number(input->device);
      if (first == NULL) return NULL;
      snprintf(buffer, sizeof buffer, "pad:%s:axis:%s:%s", first,
               input->axis != NULL ? input->axis : "",
               input->direction != NULL ? input->direction : "");
      break;
  }
  free(first);
  free(second);
  return qa_input_dup(buffer);
}

struct qa_binding_store {
  qa_input_binding *bindings;
  char **keys;
  size_t count;
  size_t capacity;
};

qa_binding_store *qa_binding_store_create(const qa_input_binding *bindings,
                                          size_t count) {
  qa_binding_store *store = (qa_binding_store *)calloc(1, sizeof *store);
  if (store == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    if (!qa_binding_store_bind(store, &bindings[i])) {
      qa_binding_store_destroy(store);
      return NULL;
    }
  }
  return store;
}

void qa_binding_store_destroy(qa_binding_store *store) {
  if (store == NULL) return;
  for (size_t i = 0; i < store->count; i++) {
    qa_input_binding_dispose(&store->bindings[i]);
    free(store->keys[i]);
  }
  free(store->bindings);
  free(store->keys);
  free(store);
}

const qa_input_binding *qa_binding_store_bindings(const qa_binding_store *store,
                                                  size_t *out_count) {
  *out_count = store->count;
  return store->bindings;
}

static size_t find_key(const qa_binding_store *store, const char *key) {
  for (size_t i = 0; i < store->count; i++) {
    if (strcmp(store->keys[i], key) == 0) return i;
  }
  return store->count;
}

const qa_input_binding_target *qa_binding_store_binding(
    const qa_binding_store *store, const qa_physical_input *input) {
  char *key = qa_physical_input_key(input);
  if (key == NULL) return NULL;
  size_t index = find_key(store, key);
  free(key);
  return index == store->count ? NULL : &store->bindings[index].target;
}

bool qa_binding_store_bind(qa_binding_store *store, const qa_input_binding *binding) {
  char *key = qa_physical_input_key(&binding->input);
  if (key == NULL) return false;
  size_t index = find_key(store, key);
  qa_input_binding copy;
  if (!qa_input_binding_copy(&copy, binding)) {
    free(key);
    return false;
  }
  if (index != store->count) {
    qa_input_binding_dispose(&store->bindings[index]);
    store->bindings[index] = copy;
    free(key);
    return true;
  }
  if (store->count == store->capacity) {
    size_t grown = store->capacity == 0 ? 8 : store->capacity * 2;
    qa_input_binding *bindings =
        (qa_input_binding *)realloc(store->bindings, grown * sizeof *bindings);
    if (bindings == NULL) {
      qa_input_binding_dispose(&copy);
      free(key);
      return false;
    }
    store->bindings = bindings;
    char **keys = (char **)realloc(store->keys, grown * sizeof *keys);
    if (keys == NULL) {
      qa_input_binding_dispose(&copy);
      free(key);
      return false;
    }
    store->keys = keys;
    store->capacity = grown;
  }
  store->bindings[store->count] = copy;
  store->keys[store->count] = key;
  store->count++;
  return true;
}

void qa_binding_store_unbind(qa_binding_store *store, const qa_physical_input *input) {
  char *key = qa_physical_input_key(input);
  if (key == NULL) return;
  size_t index = find_key(store, key);
  free(key);
  if (index == store->count) return;
  qa_input_binding_dispose(&store->bindings[index]);
  free(store->keys[index]);
  memmove(&store->bindings[index], &store->bindings[index + 1],
          (store->count - index - 1) * sizeof *store->bindings);
  memmove(&store->keys[index], &store->keys[index + 1],
          (store->count - index - 1) * sizeof *store->keys);
  store->count--;
}

void qa_binding_store_unbind_all(qa_binding_store *store) {
  for (size_t i = 0; i < store->count; i++) {
    qa_input_binding_dispose(&store->bindings[i]);
    free(store->keys[i]);
  }
  store->count = 0;
}
