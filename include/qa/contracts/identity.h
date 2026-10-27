/*
 * Port of src/contracts/identity.ts: session-scoped identity handles.
 * TS classes carry a private symbol token; C accommodation: the token
 * is an opaque unique address owned by the qa_identity_owner, compared
 * by identity exactly as TS compares symbols. Handles embed their
 * session by value (TS holds a reference to the frozen SessionId).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_IDENTITY_H
#define QA_CONTRACTS_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** TS template-literal ids `${string}:${string}`: plain C strings. */
typedef const char *qa_provider_id;
typedef const char *qa_callback_id;

typedef struct qa_session_id {
  const char *name;
  const void *token; /* TS private symbol; identity-compared. */
} qa_session_id;

typedef struct qa_actor_id {
  qa_session_id session;
  int64_t slot;
  int64_t generation;
  const void *token;
} qa_actor_id;

typedef struct qa_seat_id {
  qa_session_id session;
  int64_t index;
  const void *token;
} qa_seat_id;

typedef struct qa_client_id {
  qa_session_id session;
  int64_t slot;
  int64_t generation;
  const void *token;
} qa_client_id;

typedef struct qa_owned_actor {
  qa_actor_id id;
  qa_provider_id owner;
  const void *token;
} qa_owned_actor;

/** TS IdentityOwner. Held by the session registry. */
typedef struct qa_identity_owner {
  qa_session_id session;
  const void *token; /* malloc'd unique cell; freed by destroy. */
} qa_identity_owner;

/* Returns false only on allocation failure; an empty name aborts with
 * the TS RangeError message. The name string is borrowed, as the TS
 * SessionIdentity retains the JS string. */
bool qa_identity_owner_create(qa_identity_owner *owner, const char *name);
void qa_identity_owner_destroy(qa_identity_owner *owner);

/* Slot/generation/index must be nonnegative safe integers (TS RangeError). */
qa_actor_id qa_identity_owner_actor(const qa_identity_owner *owner, int64_t slot, int64_t generation);
qa_owned_actor qa_identity_owner_owned_actor(const qa_identity_owner *owner, qa_actor_id id, qa_provider_id provider);
qa_seat_id qa_identity_owner_seat(const qa_identity_owner *owner, int64_t index);
qa_client_id qa_identity_owner_client(const qa_identity_owner *owner, int64_t slot, int64_t generation);

/* TS owns() accepts ActorId | SeatId | ClientId; split by handle type. */
bool qa_identity_owner_owns_actor(const qa_identity_owner *owner, qa_actor_id id);
bool qa_identity_owner_owns_seat(const qa_identity_owner *owner, qa_seat_id id);
bool qa_identity_owner_owns_client(const qa_identity_owner *owner, qa_client_id id);
bool qa_identity_owner_owns_owned_actor(const qa_identity_owner *owner, qa_owned_actor id);

/** TS sameActor(): value equality within the same live registry. */
bool qa_same_actor(qa_actor_id left, qa_actor_id right);
bool qa_same_seat(qa_seat_id left, qa_seat_id right);
bool qa_same_client(qa_client_id left, qa_client_id right);

#endif
