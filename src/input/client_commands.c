/* Port of src/input/client-commands.ts. GPL-2.0-or-later. */
#include "qa/input/client_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/input/support.h"

struct qa_client_command_owner {
  qa_seat_id seat;
  char **names;
  size_t name_count;
  size_t name_capacity;
  bool has_handler;
  qa_client_command_handler handler;
};

typedef struct installed_name {
  char *name;
} installed_name;

struct qa_client_command_bindings {
  qa_command_buffer *commands;
  qa_seat_id *seats;
  size_t seat_count;
  qa_client_command_execute_fn execute;
  void *execute_ctx;
  qa_client_command_owner **owners;
  size_t owner_count;
  size_t owner_capacity;
  installed_name *installed;
  size_t installed_count;
  size_t installed_capacity;
  bool active;
};

static void dispatch_handler(void *ctx, qa_command_invocation *command) {
  (void)qa_client_command_bindings_dispatch(
      (qa_client_command_bindings *)ctx, command);
}

static bool owner_has_name(const qa_client_command_owner *owner, const char *name) {
  for (size_t i = 0; i < owner->name_count; i++) {
    if (strcmp(owner->names[i], name) == 0) return true;
  }
  return false;
}

static bool any_owner_has_name(const qa_client_command_bindings *bindings,
                               const char *name) {
  for (size_t i = 0; i < bindings->owner_count; i++) {
    if (owner_has_name(bindings->owners[i], name)) return true;
  }
  return false;
}

static void remove_unused(qa_client_command_bindings *bindings, const char *name) {
  if (any_owner_has_name(bindings, name)) return;
  for (size_t i = 0; i < bindings->installed_count; i++) {
    if (strcmp(bindings->installed[i].name, name) == 0) {
      (void)qa_command_buffer_unregister(bindings->commands, name, dispatch_handler,
                                         bindings, true);
      free(bindings->installed[i].name);
      memmove(&bindings->installed[i], &bindings->installed[i + 1],
              (bindings->installed_count - i - 1) * sizeof *bindings->installed);
      bindings->installed_count--;
      return;
    }
  }
}

static void install_name(qa_client_command_bindings *bindings, const char *name) {
  for (size_t i = 0; i < bindings->installed_count; i++) {
    if (strcmp(bindings->installed[i].name, name) == 0) return;
  }
  if (qa_command_buffer_exists(bindings->commands, name)) return;
  if (!qa_command_buffer_register(bindings->commands, name, dispatch_handler,
                                  bindings, NULL, QA_COMMAND_WORLD)) {
    return;
  }
  if (bindings->installed_count == bindings->installed_capacity) {
    size_t grown =
        bindings->installed_capacity == 0 ? 8 : bindings->installed_capacity * 2;
    installed_name *installed = (installed_name *)realloc(
        bindings->installed, grown * sizeof *installed);
    if (installed == NULL) return;
    bindings->installed = installed;
    bindings->installed_capacity = grown;
  }
  char *copy = qa_input_dup(name);
  if (copy == NULL) return;
  bindings->installed[bindings->installed_count++].name = copy;
}

static bool seats_contain(const qa_seat_id *seats, size_t count, qa_seat_id seat) {
  for (size_t i = 0; i < count; i++) {
    if (qa_same_seat(seats[i], seat)) return true;
  }
  return false;
}

qa_client_command_bindings *qa_client_command_bindings_create(
    qa_command_buffer *commands, const qa_seat_id *seats, size_t seat_count,
    qa_client_command_execute_fn execute, void *execute_ctx) {
  qa_client_command_bindings *bindings =
      (qa_client_command_bindings *)calloc(1, sizeof *bindings);
  if (bindings == NULL) return NULL;
  bindings->commands = commands;
  bindings->execute = execute;
  bindings->execute_ctx = execute_ctx;
  if (seat_count != 0) {
    bindings->seats = (qa_seat_id *)malloc(seat_count * sizeof *bindings->seats);
    if (bindings->seats == NULL) {
      free(bindings);
      return NULL;
    }
    memcpy(bindings->seats, seats, seat_count * sizeof *bindings->seats);
    bindings->seat_count = seat_count;
  }
  return bindings;
}

static void owner_free(qa_client_command_owner *owner) {
  for (size_t i = 0; i < owner->name_count; i++) free(owner->names[i]);
  free(owner->names);
  free(owner);
}

void qa_client_command_bindings_destroy(qa_client_command_bindings *bindings) {
  if (bindings == NULL) return;
  qa_client_command_bindings_deactivate(bindings);
  for (size_t i = 0; i < bindings->owner_count; i++) owner_free(bindings->owners[i]);
  free(bindings->owners);
  free(bindings->seats);
  free(bindings);
}

void qa_client_command_bindings_publish_seats(qa_client_command_bindings *bindings,
                                              const qa_seat_id *seats,
                                              size_t seat_count) {
  size_t i = 0;
  while (i < bindings->owner_count) {
    qa_client_command_owner *owner = bindings->owners[i];
    if (!seats_contain(seats, seat_count, owner->seat)) {
      memmove(&bindings->owners[i], &bindings->owners[i + 1],
              (bindings->owner_count - i - 1) * sizeof *bindings->owners);
      bindings->owner_count--;
      for (size_t n = 0; n < owner->name_count; n++) {
        remove_unused(bindings, owner->names[n]);
      }
      owner_free(owner);
    } else {
      i++;
    }
  }
  free(bindings->seats);
  bindings->seats = NULL;
  bindings->seat_count = 0;
  if (seat_count != 0) {
    bindings->seats = (qa_seat_id *)malloc(seat_count * sizeof *bindings->seats);
    if (bindings->seats != NULL) {
      memcpy(bindings->seats, seats, seat_count * sizeof *bindings->seats);
      bindings->seat_count = seat_count;
    }
  }
}

qa_client_command_owner *qa_client_command_bindings_create_owner(
    qa_client_command_bindings *bindings, qa_seat_id seat,
    const qa_client_command_handler *handler) {
  if (!seats_contain(bindings->seats, bindings->seat_count, seat)) {
    qa_input_error("Client commands require a local seat");
  }
  qa_client_command_owner *owner =
      (qa_client_command_owner *)calloc(1, sizeof *owner);
  if (owner == NULL) return NULL;
  owner->seat = seat;
  if (handler != NULL) {
    owner->has_handler = true;
    owner->handler = *handler;
  }
  if (bindings->owner_count == bindings->owner_capacity) {
    size_t grown = bindings->owner_capacity == 0 ? 4 : bindings->owner_capacity * 2;
    qa_client_command_owner **owners = (qa_client_command_owner **)realloc(
        bindings->owners, grown * sizeof *owners);
    if (owners == NULL) {
      free(owner);
      return NULL;
    }
    bindings->owners = owners;
    bindings->owner_capacity = grown;
  }
  bindings->owners[bindings->owner_count++] = owner;
  return owner;
}

static bool owner_registered(const qa_client_command_bindings *bindings,
                             const qa_client_command_owner *owner) {
  for (size_t i = 0; i < bindings->owner_count; i++) {
    if (bindings->owners[i] == owner) return true;
  }
  return false;
}

void qa_client_command_owner_register(qa_client_command_bindings *bindings,
                                      qa_client_command_owner *owner,
                                      const char *name) {
  if (!owner_registered(bindings, owner)) {
    qa_input_error("Client command owner is retired");
  }
  char *key = qa_ascii_fold(name);
  if (key == NULL) return;
  if (!owner_has_name(owner, key)) {
    if (owner->name_count == owner->name_capacity) {
      size_t grown = owner->name_capacity == 0 ? 4 : owner->name_capacity * 2;
      char **names = (char **)realloc(owner->names, grown * sizeof *names);
      if (names == NULL) {
        free(key);
        return;
      }
      owner->names = names;
      owner->name_capacity = grown;
    }
    char *copy = qa_input_dup(key);
    if (copy == NULL) {
      free(key);
      return;
    }
    owner->names[owner->name_count++] = copy;
  }
  if (bindings->active) install_name(bindings, key);
  free(key);
}

void qa_client_command_owner_remove(qa_client_command_bindings *bindings,
                                    qa_client_command_owner *owner,
                                    const char *name) {
  char *key = qa_ascii_fold(name);
  if (key == NULL) return;
  for (size_t i = 0; i < owner->name_count; i++) {
    if (strcmp(owner->names[i], key) == 0) {
      free(owner->names[i]);
      memmove(&owner->names[i], &owner->names[i + 1],
              (owner->name_count - i - 1) * sizeof *owner->names);
      owner->name_count--;
      remove_unused(bindings, key);
      break;
    }
  }
  free(key);
}

void qa_client_command_owner_close(qa_client_command_bindings *bindings,
                                   qa_client_command_owner *owner) {
  size_t index = bindings->owner_count;
  for (size_t i = 0; i < bindings->owner_count; i++) {
    if (bindings->owners[i] == owner) index = i;
  }
  if (index == bindings->owner_count) return;
  memmove(&bindings->owners[index], &bindings->owners[index + 1],
          (bindings->owner_count - index - 1) * sizeof *bindings->owners);
  bindings->owner_count--;
  for (size_t i = 0; i < owner->name_count; i++) {
    remove_unused(bindings, owner->names[i]);
  }
  owner_free(owner);
}

void qa_client_command_bindings_activate(qa_client_command_bindings *bindings) {
  if (bindings->active) return;
  bindings->active = true;
  for (size_t i = 0; i < bindings->owner_count; i++) {
    for (size_t n = 0; n < bindings->owners[i]->name_count; n++) {
      install_name(bindings, bindings->owners[i]->names[n]);
    }
  }
}

void qa_client_command_bindings_deactivate(qa_client_command_bindings *bindings) {
  bindings->active = false;
  for (size_t i = 0; i < bindings->installed_count; i++) {
    (void)qa_command_buffer_unregister(bindings->commands,
                                       bindings->installed[i].name,
                                       dispatch_handler, bindings, true);
    free(bindings->installed[i].name);
  }
  bindings->installed_count = 0;
  free(bindings->installed);
  bindings->installed = NULL;
  bindings->installed_capacity = 0;
}

bool qa_client_command_bindings_dispatch(qa_client_command_bindings *bindings,
                                         qa_command_invocation *command) {
  if (!bindings->active) return false;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) {
    origin = origin->caller;
  }
  bool has_seat = false;
  qa_seat_id seat;
  memset(&seat, 0, sizeof seat);
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    seat = origin->seat;
    has_seat = true;
  } else if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE) {
    for (size_t i = 0; i < bindings->seat_count && !has_seat; i++) {
      for (size_t o = 0; o < bindings->owner_count; o++) {
        if (qa_same_seat(bindings->owners[o]->seat, bindings->seats[i])) {
          seat = bindings->seats[i];
          has_seat = true;
        }
      }
    }
  }
  if (!has_seat) return false;
  char *name = qa_ascii_fold(command->argc > 0 ? command->argv[0] : "");
  if (name == NULL) return false;
  qa_client_command_owner **claims = (qa_client_command_owner **)calloc(
      bindings->owner_count != 0 ? bindings->owner_count : 1, sizeof *claims);
  if (claims == NULL) {
    free(name);
    return false;
  }
  size_t claim_count = 0;
  for (size_t i = 0; i < bindings->owner_count; i++) {
    if (qa_same_seat(bindings->owners[i]->seat, seat) &&
        owner_has_name(bindings->owners[i], name)) {
      claims[claim_count++] = bindings->owners[i];
    }
  }
  if (claim_count == 0) {
    free(claims);
    free(name);
    return false;
  }
  const qa_command_producer *producer = command->source.producer;
  if (producer != NULL && producer->kind != NULL &&
      strcmp(producer->kind, "client-module") == 0) {
    qa_client_command_owner *claim = NULL;
    for (size_t i = 0; i < claim_count; i++) {
      if (claims[i]->has_handler &&
          claims[i]->handler.instance == producer->instance) {
        claim = claims[i];
      }
    }
    free(claims);
    free(name);
    if (claim == NULL || !claim->has_handler) return false;
    claim->handler.execute(claim->handler.ctx, command);
    return true;
  }
  for (size_t i = 0; i < claim_count; i++) {
    if (!claims[i]->has_handler) {
      bindings->execute(bindings->execute_ctx, command, seat);
      free(claims);
      free(name);
      return true;
    }
  }
  if (claim_count > 1) {
    size_t length = strlen(name) + 64;
    for (size_t i = 0; i < claim_count; i++) {
      length += strlen(claims[i]->has_handler && claims[i]->handler.label != NULL
                           ? claims[i]->handler.label
                           : "") +
                2;
    }
    char *labels = (char *)malloc(length);
    if (labels != NULL) {
      labels[0] = '\0';
      for (size_t i = 0; i < claim_count; i++) {
        if (i != 0) strcat(labels, ", ");
        strcat(labels, claims[i]->has_handler && claims[i]->handler.label != NULL
                           ? claims[i]->handler.label
                           : "");
      }
      qa_input_error("Ambiguous component client command %s: %s", name, labels);
    }
    qa_input_error("Ambiguous component client command %s: ", name);
  }
  qa_client_command_owner *claim = claims[0];
  free(claims);
  free(name);
  if (!claim->has_handler) return false;
  claim->handler.execute(claim->handler.ctx, command);
  return true;
}
