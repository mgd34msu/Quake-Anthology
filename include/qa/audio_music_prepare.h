#ifndef QA_AUDIO_MUSIC_PREPARE_H
#define QA_AUDIO_MUSIC_PREPARE_H
#include "qa/audio.h"

typedef struct qa_audio_music_selection qa_audio_music_selection;
typedef struct qa_audio_music_controls qa_audio_music_controls;
typedef struct qa_audio_music_hold qa_audio_music_hold;
/* Separate object ownership from attached playback. A policy retains its own
 * reference; engine attachment still takes one owned reference. Destroy stops
 * playback and releases that caller's reference, while release alone retires
 * ownership. The final reference closes resources and frees the player. */
bool qa_audio_music_retain(qa_audio_music *, qa_error *);
void qa_audio_music_release(qa_audio_music *);
/* Actual enclosing resource lease. An existing current selection may complete
 * under this hold; unrelated playback/control mutations invalidate and refuse.
 * Destruction stays pending until all holds and the selection are returned. */
bool qa_audio_music_hold_prepare(qa_audio_music *, qa_audio_music_hold **, qa_error *);
bool qa_audio_music_hold_current(const qa_audio_music_hold *, const qa_audio_music *);
/* Publication proof after the captured child has actually returned. */
bool qa_audio_music_hold_consumed(const qa_audio_music_hold *, const qa_audio_music *);
void qa_audio_music_hold_release(qa_audio_music_hold **);
/* Application constructor-owned CD controls may be shared by genuine menu,
 * WORLD and explicit source players. The owner retains one reference; bound
 * players retain theirs. Imported external players reject playback until the
 * actual saved control owner is rebound, without resetting decoder state. */
bool qa_audio_music_controls_create(qa_audio_music_controls **, qa_error *);
void qa_audio_music_controls_release(qa_audio_music_controls *);
bool qa_audio_music_controls_bind(qa_audio_music *, qa_audio_music_controls *, qa_error *);
bool qa_audio_music_controls_is(const qa_audio_music *, const qa_audio_music_controls *);
/* Pure cold roster preflight. Pending players are genuine imported external
 * holders with no control owner; readiness admits all bindings without
 * allocation or resetting any playback field. */
bool qa_audio_music_controls_restore_pending(const qa_audio_music *);
bool qa_audio_music_controls_restore_ready(const qa_audio_music_controls *, size_t pending_players);
bool qa_audio_music_controls_enabled(const qa_audio_music_controls *, bool *);
unsigned qa_audio_music_controls_mapped_track(const qa_audio_music_controls *, unsigned);
bool qa_audio_music_controls_enable(qa_audio_music_controls *, bool, qa_error *);
bool qa_audio_music_controls_remap(qa_audio_music_controls *, const uint8_t *, size_t, qa_error *);
bool qa_audio_music_controls_reset(qa_audio_music_controls *, qa_error *);
bool qa_audio_music_controls_checkpoint(const qa_audio_music_controls *, qa_buffer *, qa_error *);
bool qa_audio_music_controls_restore(qa_bytes, qa_audio_music_controls **, qa_error *);
typedef enum qa_audio_music_selection_kind {
    QA_AUDIO_MUSIC_KEEP, QA_AUDIO_MUSIC_START, QA_AUDIO_MUSIC_STOP
} qa_audio_music_selection_kind;
typedef struct qa_audio_music_state {
    uint32_t output_rate;
    qa_audio_family family;
    uint64_t request, completions;
    unsigned cd_track;
    float target_volume;
    bool source_volume, enabled, paused, playing;
} qa_audio_music_state;
typedef struct qa_audio_music_source_profile {
    qa_audio_family family;
    bool source_volume;
} qa_audio_music_source_profile;
bool qa_audio_music_state_read(const qa_audio_music *, qa_audio_music_state *);
bool qa_audio_music_idle(const qa_audio_music *);
/* Pure read of the player's actual shared CD controls. */
unsigned qa_audio_music_mapped_track(const qa_audio_music *, unsigned track);
/* The serialized caller retains the real player and its bank/engine parents.
 * Successful preparation takes intro/loop ownership, preallocates replacement
 * PCM, and claims the player. KEEP requires no streams; START requires intro.
 * Source mutations attempted during the claim invalidate publication and
 * retain playback; destruction also stays pending until the claim is aborted.
 * The destructor's caller then retries destruction of that same retained owner.
 * A nonnull new_source declares a real source selection: its fresh player
 * profile resets smoothing, completion and pause, while shared CD controls and
 * target gain remain owned by the same application. It requires START or STOP.
 * The caller must exclude engine/source work until publication or abort. */
bool qa_audio_music_selection_prepare(qa_audio_music *, qa_audio_music_selection_kind,
    qa_audio_stream *intro, qa_audio_stream *loop, unsigned cd_track,
    const qa_audio_music_source_profile *new_source,
    qa_audio_music_selection **, qa_error *);
bool qa_audio_music_selection_ready(qa_audio_music_selection *, qa_error *);
/* Offside actual shared target; leaves live playback untouched and requires
 * ready to be renewed. Publication adopts it with the selected resources. */
bool qa_audio_music_selection_volume(qa_audio_music_selection *, float, qa_error *);
/* Borrowed actual claim, retained until publish/abort. Its current proof admits
 * a genuine prepared player into the enclosing gains lease without requiring
 * the player to be idle or making the selection ready. */
const qa_audio_music_selection *qa_audio_music_selection_read(const qa_audio_music *);
bool qa_audio_music_selection_current(const qa_audio_music_selection *, const qa_audio_music *);
/* Pure exact-identity/resource proof; no decoding, callback or clock read. */
bool qa_audio_music_selection_ready_is(const qa_audio_music_selection *, const qa_audio_music *);
/* Requires ready_is and the enclosing admitted source/engine leases. Retained
 * gains and smoothing are unchanged; START clears pause as the real start does. */
void qa_audio_music_selection_publish(qa_audio_music_selection **);
/* A refused abort retains the complete selection and its playback parent. */
bool qa_audio_music_selection_abort(qa_audio_music_selection **, qa_error *);
#endif
