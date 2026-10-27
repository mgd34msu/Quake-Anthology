/* Port of src/contracts/identity.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/identity.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

/* TS natural(): Number.isSafeInteger and >= 0. */
static int64_t natural(int64_t value, const char *label) {
  if (value < 0 || value > QA_MAX_SAFE_INTEGER) {
    qa_range_error("%s must be a nonnegative safe integer", label);
  }
  return value;
}

bool qa_identity_owner_create(qa_identity_owner *owner, const char *name) {
  if (name == NULL || name[0] == '\0') {
    qa_range_error("A session identity needs a name");
  }
  /* The TS symbol token is a unique malloc'd cell compared by address. */
  void *token = malloc(1);
  if (token == NULL) {
    owner->session.name = NULL;
    owner->session.token = NULL;
    owner->token = NULL;
    return false;
  }
  owner->token = token;
  owner->session.name = name;
  owner->session.token = token;
  return true;
}

void qa_identity_owner_destroy(qa_identity_owner *owner) {
  free((void *)owner->token);
  owner->token = NULL;
  owner->session.name = NULL;
  owner->session.token = NULL;
}

qa_actor_id qa_identity_owner_actor(const qa_identity_owner *owner, int64_t slot,
                                    int64_t generation) {
  qa_actor_id id;
  id.session = owner->session;
  id.slot = natural(slot, "Actor slot");
  id.generation = natural(generation, "Actor generation");
  id.token = owner->token;
  return id;
}

qa_owned_actor qa_identity_owner_owned_actor(const qa_identity_owner *owner, qa_actor_id id,
                                             qa_provider_id provider) {
  if (id.session.token != owner->token) {
    qa_range_error("Actor belongs to another session");
  }
  qa_owned_actor owned;
  owned.id = id;
  owned.owner = provider;
  owned.token = owner->token;
  return owned;
}

qa_seat_id qa_identity_owner_seat(const qa_identity_owner *owner, int64_t index) {
  qa_seat_id id;
  id.session = owner->session;
  id.index = natural(index, "Seat index");
  id.token = owner->token;
  return id;
}

qa_client_id qa_identity_owner_client(const qa_identity_owner *owner, int64_t slot,
                                      int64_t generation) {
  qa_client_id id;
  id.session = owner->session;
  id.slot = natural(slot, "Client slot");
  id.generation = natural(generation, "Client generation");
  id.token = owner->token;
  return id;
}

/* TS owns() consults only the handle's session token. */
bool qa_identity_owner_owns_actor(const qa_identity_owner *owner, qa_actor_id id) {
  return id.session.token == owner->token;
}

bool qa_identity_owner_owns_seat(const qa_identity_owner *owner, qa_seat_id id) {
  return id.session.token == owner->token;
}

bool qa_identity_owner_owns_client(const qa_identity_owner *owner, qa_client_id id) {
  return id.session.token == owner->token;
}

bool qa_identity_owner_owns_owned_actor(const qa_identity_owner *owner, qa_owned_actor id) {
  return id.id.session.token == owner->token;
}

/* TS ActorIdentity.equals: own token, slot, and generation. */
bool qa_same_actor(qa_actor_id left, qa_actor_id right) {
  return left.token == right.token && left.slot == right.slot &&
         left.generation == right.generation;
}

bool qa_same_seat(qa_seat_id left, qa_seat_id right) {
  return left.token == right.token && left.index == right.index;
}

bool qa_same_client(qa_client_id left, qa_client_id right) {
  return left.token == right.token && left.slot == right.slot &&
         left.generation == right.generation;
}
