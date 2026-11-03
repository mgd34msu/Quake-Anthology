/* Port of src/app/bootstrap/presentation-state.ts: PresentationState, the
 * store that retains source presentation events (up to 32,768 undrained)
 * until the application resolves their content-owned media, with owner
 * activations, persistent presentation, light styles, local component
 * media and save capture/restore.
 *
 * C accommodations:
 *  - Retained events are owned deep copies (qa_app_sim_presentation_event
 *    with storage set). Results the TS returns as fresh arrays are buffers
 *    owned by the state, valid until the next call of the same function
 *    (or as each function says); their elements stay valid for that long.
 *  - TS compares local media requests by object identity; C compares the
 *    qa_app_retained_presentation pointers these functions hand out.
 *  - capture() returns an owned qa_save_value tree (caller frees).
 *  - TS `throw new Error(...)` is qa_raise("Error", ...).
 *  - Strings are byte strings: lightStyles indexes the pattern by byte
 *    where TS uses UTF-16 units (equal for the ASCII patterns sources send).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_STATE_H
#define QA_APP_BOOTSTRAP_PRESENTATION_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/q1_fog.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/persistence/value.h"

/* TS PRESENTATION_EVENT_CAPACITY. */
#define QA_APP_PRESENTATION_EVENT_CAPACITY 32768

/* TS LocalPresentationMedia (kind "local-media"). has_owner false is TS
 * owner undefined. */
typedef struct qa_app_local_presentation_media {
  qa_component_presentation_media_request event;
  bool has_owner; qa_presentation_owner owner;
  qa_content_id content;
  double sequence, seconds;
} qa_app_local_presentation_media;
/* TS RetainedPresentation = SimulationPresentationEvent |
 * LocalPresentationMedia. local selects local_media (TS kind "local-media");
 * otherwise simulation applies. */
typedef struct qa_app_retained_presentation {
  bool local;
  union {
    qa_app_sim_presentation_event simulation;
    qa_app_local_presentation_media local_media;
  };
} qa_app_retained_presentation;

/* TS () => boolean. */
typedef bool (*qa_app_presentation_current_fn)(void *ctx);

typedef struct qa_app_presentation_state qa_app_presentation_state;
typedef struct qa_app_presentation_owner_binding qa_app_presentation_owner_binding;

/* new PresentationState(now, sourceSlot, fogOptions = null). source_slot
 * returns false for TS null. fog NULL is TS null; it is copied (its arrays
 * and alive callback must outlive the state). Returns NULL only on OOM. */
qa_app_presentation_state *qa_app_presentation_state_create(qa_source_time (*now)(void *ctx),
    bool (*source_slot)(void *ctx, qa_actor_id actor, double *out), void *ctx, const qa_app_sim_q1_fog_options *fog);
void qa_app_presentation_state_destroy(qa_app_presentation_state *state);
/* C-only: the state frees data with release(data) when destroyed (an
 * adapter context whose lifetime is the state's). */
void qa_app_presentation_state_adopt(qa_app_presentation_state *state, void *data, void (*release)(void *data));

/* bindOwner(provider, content, restoring): the returned binding is the TS
 * Pick<PresentationState,"emit"|"registerResource"> & {owner, close}. The
 * caller frees it with _binding_free (free does not close it). */
qa_app_presentation_owner_binding *qa_app_presentation_state_bind_owner(qa_app_presentation_state *state,
    qa_provider_id provider, qa_content_id content, bool restoring);
qa_presentation_owner qa_app_presentation_owner_binding_owner(const qa_app_presentation_owner_binding *binding);
/* has_time false uses now(); has_recipient false is TS undefined. */
void qa_app_presentation_owner_binding_emit(qa_app_presentation_owner_binding *binding, qa_content_id content,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient);
void qa_app_presentation_owner_binding_register_resource(qa_app_presentation_owner_binding *binding,
    qa_content_id content, const char *path, const qa_resolved_resource_reference *resource);
void qa_app_presentation_owner_binding_close(qa_app_presentation_owner_binding *binding);
void qa_app_presentation_owner_binding_free(qa_app_presentation_owner_binding *binding);

void qa_app_presentation_state_finish_owner_restore(qa_app_presentation_state *state);
void qa_app_presentation_state_refresh_owner(qa_app_presentation_state *state, qa_provider_id provider);
/* TS retire(actor). */
void qa_app_presentation_state_retire_actor(qa_app_presentation_state *state, qa_actor_id actor);
/* TS resource(id): NULL is TS null. Borrowed from the state. */
const qa_resolved_resource_reference *qa_app_presentation_state_resource(const qa_app_presentation_state *state,
    qa_resource_id id);
/* TS persistentPresentation(): sorted by sequence; valid until the next call. */
const qa_app_sim_presentation_event *const *qa_app_presentation_state_persistent_presentation(
    qa_app_presentation_state *state, size_t *out_count);
/* persistentPresentation() as unified event trees (TS typed-object shape, actor ids carrying their identity);
 * valid until the next call. */
qa_save_value *const *qa_app_presentation_state_persistent_presentation_trees(qa_app_presentation_state *state, size_t *out_count);

/* publishLocalMedia(owner, content, event, initializing, current?). owner
 * NULL is TS undefined; current NULL is TS undefined. The delivery keeps
 * current; release_current(current_ctx), when not NULL, runs once the
 * delivery is dropped. event's strings are copied. */
void qa_app_presentation_state_publish_local_media(qa_app_presentation_state *state, const qa_presentation_owner *owner,
    qa_content_id content, const qa_component_presentation_media_request *event, bool initializing,
    qa_app_presentation_current_fn current, void *current_ctx, void (*release_current)(void *current_ctx));
void qa_app_presentation_state_enable_local_media(qa_app_presentation_state *state);
/* pendingLocalMedia(): sorted by sequence. Each request stays valid until
 * the next call of this function, even if acknowledged meanwhile. */
const qa_app_retained_presentation *const *qa_app_presentation_state_pending_local_media(
    qa_app_presentation_state *state, size_t *out_count);
double qa_app_presentation_state_applied_media_sequence(const qa_app_presentation_state *state);
void qa_app_presentation_state_applied_media(qa_app_presentation_state *state, double sequence);
void qa_app_presentation_state_applied_shader(qa_app_presentation_state *state, const char *original, double sequence);
/* resolveShaderReplay(request): NULL is TS null. request is local. The
 * result stays valid until the next pendingLocalMedia call. */
const qa_app_retained_presentation *qa_app_presentation_state_resolve_shader_replay(qa_app_presentation_state *state,
    const qa_app_retained_presentation *request);
bool qa_app_presentation_state_local_media_current(qa_app_presentation_state *state,
    const qa_app_retained_presentation *request);
void qa_app_presentation_state_acknowledge_local_media(qa_app_presentation_state *state,
    const qa_app_retained_presentation *request, bool committed);
void qa_app_presentation_state_assert_local_media_consumed(const qa_app_presentation_state *state);

/* registerResource(content, path, resource): the reference is deep-copied
 * (its provenance mount pointers stay borrowed from the mount plan). */
void qa_app_presentation_state_register_resource_reference(qa_app_presentation_state *state, qa_content_id content,
    const char *path, const qa_resolved_resource_reference *resource);
/* resourceFor(content, path): NULL is TS undefined. Borrowed. */
const qa_resolved_resource_reference *qa_app_presentation_state_resource_reference_for(
    const qa_app_presentation_state *state, qa_content_id content, const char *path);

/* emit(content, source, time = now(), recipient?). */
void qa_app_presentation_state_emit(qa_app_presentation_state *state, qa_content_id content,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient);
/* emitOwned: returns a view (storage NULL) of the retained event, valid
 * until that event is drained. */
qa_app_sim_presentation_event qa_app_presentation_state_emit_owned_event(qa_app_presentation_state *state,
    const qa_presentation_owner *owner, qa_content_id content, const qa_app_sim_source_presentation_event *source,
    bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient);

/* TS virtual dispatch of emitOwned: SimulationEvents overrides it, and
 * PresentationState's own emit and bound-owner emit call the override. The
 * override calls _emit_owned_event for the TS super.emitOwned. Unset (the
 * default), they call _emit_owned_event directly. */
typedef qa_app_sim_presentation_event (*qa_app_presentation_emit_owned_fn)(void *ctx, const qa_presentation_owner *owner,
    qa_content_id content, const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient);
void qa_app_presentation_state_set_emit_owned_override(qa_app_presentation_state *state,
    qa_app_presentation_emit_owned_fn emit_owned, void *ctx);

void qa_app_presentation_state_admit_replicated_owner(qa_app_presentation_state *state, qa_presentation_owner token,
    qa_content_id content);
void qa_app_presentation_state_retire_replicated_owner(qa_app_presentation_state *state, qa_presentation_owner token);
/* receivePresentation(source): returns the sequence before admission. */
double qa_app_presentation_state_receive_presentation(qa_app_presentation_state *state,
    const qa_app_sim_presentation_event *source);

/* takePresentation(): owned by the state until the next take or drain. */
const qa_app_sim_presentation_event *qa_app_presentation_state_take(qa_app_presentation_state *state, size_t *out_count);
/* drainPresentation(visit): each event is valid during its visit. */
size_t qa_app_presentation_state_drain_presentation(qa_app_presentation_state *state,
    void (*visit)(void *ctx, const qa_app_sim_presentation_event *event), void *visit_ctx);
void qa_app_presentation_state_assert_output_consumed(qa_app_presentation_state *state);

/* capture(): owned tree. */
qa_save_value *qa_app_presentation_state_capture(qa_app_presentation_state *state);
/* restore(reader, reference). */
void qa_app_presentation_state_restore(qa_app_presentation_state *state, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx);

/* lightStyle(style): "" when unset; borrowed. */
const char *qa_app_presentation_state_light_style(qa_app_presentation_state *state, double style);
/* lightStyles(seconds): valid until the next call. */
const qa_scene_light_style *qa_app_presentation_state_light_styles(qa_app_presentation_state *state, double seconds,
    size_t *out_count);

/* ---- The runtime's PresentationState surface (simulation/
 * shared_simulation_internal.h declares these with exactly these
 * signatures; presentation_state_source.c defines create_source). ---- */
void qa_app_presentation_state_emit_owned(qa_app_presentation_state *state, bool has_owner, qa_presentation_owner owner,
    qa_content_id content, qa_app_sim_source_presentation_event source, qa_source_time time, bool has_recipient,
    qa_actor_id recipient, qa_app_sim_presentation_event *out);
bool qa_app_presentation_state_resource_for(qa_app_presentation_state *state, qa_content_id content, const char *path,
    qa_resource_id *out);
void qa_app_presentation_state_register_resource(qa_app_presentation_state *state, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
/* takePresentation() as a pointer list (same buffer as _take). */
const qa_app_sim_presentation_event *const *qa_app_presentation_state_take_presentation(qa_app_presentation_state *state,
    size_t *out_count);
#endif
