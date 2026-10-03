#ifndef QA_SAVE_H
#define QA_SAVE_H

#include "qa/actors.h"
#include "qa/filesystem.h"
#include "qa/hash.h"
#include "qa/world.h"
#include "qa/session.h"

#define QA_SAVE_VERSION 12u
#define QA_SAVE_OWNER_LIMIT 65536u
#define QA_SAVE_NAME_LIMIT 255u

typedef enum qa_save_purpose {
    QA_SAVE_MANUAL, QA_SAVE_LEVEL_ENTRY, QA_SAVE_TRANSITION, QA_SAVE_RECOVERY,
    QA_SAVE_DEMO_KEYFRAME
} qa_save_purpose;

/* Exactly one of each shared record is required, including an explicit absent
 * value when that owner is not installed. PROVIDER has one record per selected
 * instance. The configuration codec must validate that exact instance set. */
typedef enum qa_save_owner_kind {
    QA_SAVE_STRINGS = 1, QA_SAVE_RESOURCES, QA_SAVE_CONFIGURATION,
    QA_SAVE_SESSION, QA_SAVE_ACTORS, QA_SAVE_WORLD, QA_SAVE_COMBAT,
    QA_SAVE_INVENTORY, QA_SAVE_PICKUPS, QA_SAVE_TARGETS, QA_SAVE_CAMPAIGN,
    QA_SAVE_MODES, QA_SAVE_EQUIPMENT, QA_SAVE_PROGRESSION, QA_SAVE_ROSTER,
    QA_SAVE_CONTROLS, QA_SAVE_CVARS, QA_SAVE_COMMANDS, QA_SAVE_EVENTS,
    QA_SAVE_NAVIGATION, QA_SAVE_BOTS, QA_SAVE_CONNECTIONS, QA_SAVE_PREDICTION,
    QA_SAVE_PRESENTATION, QA_SAVE_AUDIO, QA_SAVE_INPUT, QA_SAVE_MEDIA,
    QA_SAVE_APPLICATION, QA_SAVE_PROVIDER
} qa_save_owner_kind;

typedef struct qa_save_owner {
    qa_save_owner_kind kind;
    const char *instance;       /* Empty for shared owners. */
    const char *schema;         /* Explicit field codec, never a struct dump. */
    const char *backend;        /* ABI/execution identity, empty when portable. */
    qa_sha256_digest content;  /* Exact external program/content identity. */
} qa_save_owner;

typedef struct qa_save_metadata {
    qa_save_purpose purpose;
    uint64_t elapsed_ns, configuration_generation, world_generation;
    qa_sha256_digest composition;
} qa_save_metadata;

typedef struct qa_save_record {
    qa_save_owner owner;
    qa_bytes payload;
} qa_save_record;
typedef struct qa_save_image qa_save_image;
typedef struct qa_native_resource_inventory qa_native_resource_inventory;
typedef bool (*qa_save_native_release_fn)(qa_native_resource_inventory **, qa_error *);

/* The image owns every descriptor string and payload. Decoding verifies the
 * complete envelope digest before allocating, then validates the owner set.
 * Outputs remain unchanged on failure. Existing outputs must be freed first. */
bool qa_save_image_create(const qa_save_metadata *, const qa_save_record *, size_t,
                           qa_save_image **, qa_error *);
bool qa_save_image_decode(qa_bytes, qa_save_image **, qa_error *);
bool qa_save_image_encode(const qa_save_image *, qa_buffer *, qa_error *);
/* Checked release consumes the image only after all attached native references
 * close successfully. A refusal retains *image for retry; that retiring image
 * can no longer be encoded or used as an active capability graph. */
bool qa_save_image_destroy_checked(qa_save_image **, qa_error *);
/* Transfers the actual inventory on success. No pointer enters the encoded
 * image. A decoded image requires its real external graph to be attached by
 * the preparation owner before restoring native capabilities. */
bool qa_save_image_native_attach(qa_save_image *, qa_native_resource_inventory *,
    qa_save_native_release_fn, qa_error *);
const qa_native_resource_inventory *qa_save_image_native_read(const qa_save_image *);
const qa_save_metadata *qa_save_image_metadata(const qa_save_image *);
size_t qa_save_image_record_count(const qa_save_image *);
const qa_save_record *qa_save_image_record_at(const qa_save_image *, size_t);
const qa_save_record *qa_save_image_find(const qa_save_image *, qa_save_owner_kind,
                                         const char *instance);

/* begin holds a session and every owner at a safe point until end. Providers
 * must have no active callbacks/reentry. Borrowed inventory lives until end;
 * capture returns owned, explicitly encoded bytes. end runs exactly once after
 * a successful begin, including failure. No producer can be omitted. */
typedef struct qa_save_capture_ops {
    bool (*begin)(void *, qa_save_purpose, qa_save_metadata *,
                   const qa_save_owner **, size_t *, qa_error *);
    bool (*capture)(void *, const qa_save_owner *, qa_buffer *, qa_error *);
    bool (*validate)(void *, const qa_save_image *, qa_error *);
    void (*end)(void *);
    /* Move captured external holds into the genuine image before the capture
     * scope ends. Failure leaves every untransferred hold with its producer. */
    bool (*attach)(void *, qa_save_image *, qa_error *);
} qa_save_capture_ops;
/* A failed capture can return a retained image only when actual checked native
 * cleanup refuses. The caller must keep it and retry checked destruction. */
bool qa_save_capture(void *, const qa_save_capture_ops *, qa_save_purpose,
                      qa_save_image **, qa_error *);

/* create must pin content, schema and backend identities and build a separate
 * candidate. restore rebuilds references through the candidate's actor/string/
 * resource tables. Shared owners restore once; provider actors/bindings rebuild
 * before controls and roster. finish checks every selected instance, actor,
 * resource, source slot, body binding, callback ID and queued continuation.
 * publish only exchanges the active candidate at an application safe point;
 * it allocates nothing and invokes no guest/game callbacks. Failure retains
 * candidate ownership. discard closes a candidate on every prior failure.
 * Closing the displaced session is the application's separate responsibility. */
typedef struct qa_save_restore_ops {
    bool (*create)(void *, const qa_save_image *, void **candidate, qa_error *);
    bool (*restore)(void *, void *candidate, const qa_save_record *, qa_error *);
    bool (*finish)(void *, void *candidate, const qa_save_image *, qa_error *);
    bool (*publish)(void *, void *candidate, qa_error *);
    void (*discard)(void *, void *candidate);
} qa_save_restore_ops;
bool qa_save_restore(void *, const qa_save_restore_ops *, const qa_save_image *, qa_error *);

/* Contained .sav names only. Every path component named current is reserved
 * for transition storage. Atomic file replacement is supplied by filesystem. */
bool qa_save_slot_name(const char *, qa_error *);
bool qa_save_write(qa_fs_root *, const char *, const qa_save_image *, uint64_t nonce, qa_error *);
bool qa_save_read(qa_fs_root *, const char *, qa_save_image **, qa_error *);

/* Registry codecs retain vacant generations as well as live source bindings.
 * No live registry namespace or C pointer appears in the file. */
bool qa_save_actors_encode(const qa_actor_checkpoint *, qa_buffer *, qa_error *);
bool qa_save_actors_decode(qa_bytes, qa_actor_checkpoint *, qa_error *);
bool qa_save_world_encode(const qa_world_checkpoint *, qa_buffer *, qa_error *);
bool qa_save_world_decode(qa_bytes, qa_world_checkpoint *, qa_error *);
bool qa_save_session_encode(const qa_session_checkpoint *, qa_buffer *, qa_error *);
bool qa_save_session_decode(qa_bytes, qa_session_checkpoint *, qa_error *);
bool qa_save_strings_encode(const qa_strings *, qa_buffer *, qa_error *);
bool qa_save_strings_decode(qa_bytes, qa_strings **, qa_error *);

#endif
