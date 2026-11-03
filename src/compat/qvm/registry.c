/* See include/qa/compat/qvm/registry.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

typedef struct slot {
  bool occupied;
  uint64_t generation;
  char name[64];
  const char *kind; /* initializing | interpreted | typescript | freed */
  int64_t data_length;
  int64_t instruction_pointers_length;
  int64_t code_length;
  qa_qvm_interpreter *interpreter;
} slot;

static slot *slots_of(qa_qvm_vm_registry *registry) { return registry->slots; }

static bool is_current(const qa_qvm_vm_registration *registration) {
  slot *slots = slots_of(registration->registry);
  slot *entry = &slots[registration->slot];
  return entry->occupied && entry->generation == registration->generation;
}

static slot *current(const qa_qvm_vm_registration *registration) {
  if (!is_current(registration)) qa_range_error("VM registration has been freed");
  return &slots_of(registration->registry)[registration->slot];
}

static slot *preparing(const qa_qvm_vm_registration *registration) {
  slot *entry = current(registration);
  if (strcmp(entry->kind, "initializing") != 0) qa_range_error("VM registration is already bound");
  return entry;
}

static bool lower_equal(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return *a == *b;
}

void qa_qvm_vm_registry_init(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print,
                             void *print_ctx, qa_qvm_registry_profile_fn execution_profile,
                             void *profile_ctx) {
  memset(registry, 0, sizeof *registry);
  registry->print = print;
  registry->print_ctx = print_ctx;
  registry->execution_profile = execution_profile;
  registry->profile_ctx = profile_ctx;
  registry->slots = calloc(3, sizeof(slot));
  if (registry->slots == NULL) qa_range_error("QVM memory allocation failed");
  slot *slots = registry->slots;
  for (int i = 0; i < 3; i++) slots[i].kind = "freed";
}

void qa_qvm_vm_registry_destroy(qa_qvm_vm_registry *registry) {
  free(registry->slots);
  memset(registry, 0, sizeof *registry);
}

void qa_qvm_vm_registry_debug(qa_qvm_vm_registry *registry, int64_t level) {
  registry->debug_level = level;
}

qa_qvm_vm_registration qa_qvm_vm_registry_reserve(qa_qvm_vm_registry *registry, const char *name) {
  if (name[0] == '\0') qa_common_error(QA_COMMON_ERROR_FATAL, "VM_Create: bad parms");
  slot *slots = slots_of(registry);
  for (int i = 0; i < 3; i++) {
    if (slots[i].occupied && lower_equal(slots[i].name, name)) {
      qa_qvm_vm_registration existing;
      memset(&existing, 0, sizeof existing);
      memcpy(existing.name, slots[i].name, sizeof existing.name);
      existing.registry = registry;
      existing.slot = (size_t)i;
      existing.generation = slots[i].generation;
      return existing;
    }
  }
  int free_index = -1;
  for (int i = 0; i < 3; i++) {
    if (!slots[i].occupied) {
      free_index = i;
      break;
    }
  }
  if (free_index < 0) qa_common_error(QA_COMMON_ERROR_FATAL, "VM_Create: no free vm_t");
  slot *entry = &slots[free_index];
  entry->occupied = true;
  entry->generation++;
  size_t length = strlen(name);
  if (length > 63) length = 63;
  memcpy(entry->name, name, length);
  entry->name[length] = '\0';
  entry->kind = "initializing";
  entry->code_length = 0;
  entry->instruction_pointers_length = 0;
  entry->data_length = 1;
  entry->interpreter = NULL;
  qa_qvm_vm_registration registration;
  memset(&registration, 0, sizeof registration);
  memcpy(registration.name, entry->name, sizeof registration.name);
  registration.registry = registry;
  registration.slot = (size_t)free_index;
  registration.generation = entry->generation;
  return registration;
}

void qa_qvm_vm_registry_clear(qa_qvm_vm_registry *registry) {
  slot *slots = slots_of(registry);
  for (int i = 0; i < 3; i++) {
    slots[i].occupied = false;
    slots[i].kind = "freed";
    slots[i].interpreter = NULL;
  }
  registry->has_last_called = false;
}

static int64_t slot_code_length(const slot *entry) {
  if (strcmp(entry->kind, "interpreted") == 0)
    return (int64_t)qa_qvm_interpreter_get_code_length(entry->interpreter);
  if (strcmp(entry->kind, "initializing") == 0) return entry->code_length;
  return 0;
}

static int64_t slot_table_length(const slot *entry) {
  if (strcmp(entry->kind, "interpreted") == 0)
    return (int64_t)qa_qvm_interpreter_get_instruction_pointers_length(entry->interpreter);
  if (strcmp(entry->kind, "initializing") == 0) return entry->instruction_pointers_length;
  return 0;
}

static int64_t slot_data_length(const slot *entry) {
  if (strcmp(entry->kind, "interpreted") == 0) {
    size_t length = 0;
    (void)qa_qvm_interpreter_get_memory(entry->interpreter, &length);
    return (int64_t)length;
  }
  if (strcmp(entry->kind, "initializing") == 0) return entry->data_length;
  return 1;
}

void qa_qvm_vm_registry_print_info(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print, void *print_ctx) {
  slot *slots = slots_of(registry);
  print(print_ctx, "Registered virtual machines:\n");
  for (int i = 0; i < 3; i++) {
    if (!slots[i].occupied) break;
    char line[256];
    snprintf(line, sizeof line, "%s : ", slots[i].name);
    print(print_ctx, line);
    if (strcmp(slots[i].kind, "typescript") == 0) {
      print(print_ctx, "TypeScript replacement\n");
      continue;
    }
    snprintf(line, sizeof line, "%s\n",
             strcmp(slots[i].kind, "interpreted") == 0 ? qa_qvm_interpreter_get_semantics(slots[i].interpreter) : "interpreted");
    print(print_ctx, line);
    snprintf(line, sizeof line, "    code length : %7lld\n", (long long)slot_code_length(&slots[i]));
    print(print_ctx, line);
    snprintf(line, sizeof line, "    table length: %7lld\n", (long long)slot_table_length(&slots[i]));
    print(print_ctx, line);
    snprintf(line, sizeof line, "    data length : %7lld\n", (long long)slot_data_length(&slots[i]));
    print(print_ctx, line);
  }
}

void qa_qvm_vm_registry_print_profile(qa_qvm_vm_registry *registry, qa_qvm_registry_print_fn print, void *print_ctx) {
  if (!registry->has_last_called) return;
  slot *slots = slots_of(registry);
  slot *entry = &slots[registry->last_called_slot];
  if (!entry->occupied || entry->generation != registry->last_called_generation) return;
  if (strcmp(entry->kind, "interpreted") != 0) return;
  qa_qvm_symbols_print_profile(qa_qvm_interpreter_get_symbols(entry->interpreter), print, print_ctx,
                               qa_qvm_interpreter_get_debug_enabled(entry->interpreter));
}

qa_qvm_vm_binding qa_qvm_vm_registration_get_binding(const qa_qvm_vm_registration *registration) {
  qa_qvm_vm_binding binding = {"freed", NULL};
  if (!is_current(registration)) return binding;
  slot *entry = &slots_of(registration->registry)[registration->slot];
  binding.kind = entry->kind;
  binding.interpreter = entry->interpreter;
  return binding;
}

qa_qvm_execution_profile qa_qvm_vm_registration_execution_profile(const qa_qvm_vm_registration *registration) {
  (void)current(registration);
  qa_qvm_vm_registry *registry = registration->registry;
  if (registry->execution_profile != NULL) return registry->execution_profile(registry->profile_ctx);
  qa_qvm_execution_profile release = {"release", 0, 0};
  return release;
}

void qa_qvm_vm_registration_bind_data(const qa_qvm_vm_registration *registration, size_t memory_length) {
  preparing(registration)->data_length = (int64_t)memory_length;
}

void qa_qvm_vm_registration_bind_instruction_pointers_length(const qa_qvm_vm_registration *registration, int64_t length) {
  preparing(registration)->instruction_pointers_length = length;
}

void qa_qvm_vm_registration_bind_code_length(const qa_qvm_vm_registration *registration, int64_t length) {
  preparing(registration)->code_length = length;
}

void qa_qvm_vm_registration_bind_interpreter(const qa_qvm_vm_registration *registration, qa_qvm_interpreter *interpreter) {
  slot *entry = preparing(registration);
  entry->kind = "interpreted";
  entry->interpreter = interpreter;
}

void qa_qvm_vm_registration_bind_typescript(const qa_qvm_vm_registration *registration) {
  slot *entry = preparing(registration);
  entry->kind = "typescript";
}

void qa_qvm_vm_registration_called(const qa_qvm_vm_registration *registration) {
  (void)current(registration);
  qa_qvm_vm_registry *registry = registration->registry;
  registry->has_last_called = true;
  registry->last_called_slot = registration->slot;
  registry->last_called_generation = registration->generation;
}

void qa_qvm_vm_registration_debug(const qa_qvm_vm_registration *registration, int64_t level) {
  (void)current(registration);
  qa_qvm_vm_registry_debug(registration->registry, level);
}

void qa_qvm_vm_registration_print(const qa_qvm_vm_registration *registration, const char *text) {
  (void)current(registration);
  qa_qvm_vm_registry *registry = registration->registry;
  if (registry->print != NULL) registry->print(registry->print_ctx, text);
}

void qa_qvm_vm_registration_print_call(const qa_qvm_vm_registration *registration, int64_t callnum) {
  (void)current(registration);
  qa_qvm_vm_registry *registry = registration->registry;
  if (registry->debug_level != 0 && registry->print != NULL) {
    char line[64];
    snprintf(line, sizeof line, "VM_Call( %lld )\n", (long long)callnum);
    registry->print(registry->print_ctx, line);
  }
}

void qa_qvm_vm_registration_free(const qa_qvm_vm_registration *registration) {
  if (!is_current(registration)) return;
  slot *entry = &slots_of(registration->registry)[registration->slot];
  entry->occupied = false;
  entry->kind = "freed";
  entry->interpreter = NULL;
  registration->registry->has_last_called = false;
}
