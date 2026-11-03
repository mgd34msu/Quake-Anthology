/* SimulationPresentationEvent values and the unified event trees of
 * unified_event_codec.h, in both directions.
 *
 * TS encodes and decodes its typed event objects directly: the codec's writers read
 * the object a source emitted and its readers build that object. The C codec works on
 * qa_save_value trees in the TS object shape, so a typed C event becomes that tree here
 * (every TS field of its variant, under the TS name, with the TS value: nullable members
 * as null, absent optional members left out) and a decoded tree becomes the typed event
 * again. The two directions share one field list per variant, so a typed event survives
 * tree and back field for field.
 *
 * Identities: an actor node is {slot, generation} carrying the live qa_actor_id as its
 * attachment (as the codec's reader leaves it), so qa_app_unified_event_actor_id and
 * qa_same_actor see the identity TS keeps on the object.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_VALUES_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_VALUES_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

/* The TS SimulationPresentationEvent object of `event` as an owned tree. An unknown kind
 * or payload variant raises RangeError. */
qa_save_value *qa_app_unified_presentation_event_tree(const qa_app_sim_presentation_event *event);
/* The typed event of a tree in that shape (a tree qa_app_unified_read_presentation_event
 * returned, or one built above): *out becomes an owned value (storage set) the caller
 * releases with qa_app_sim_presentation_event_release. An unknown kind or variant raises
 * RangeError. */
void qa_app_unified_presentation_event_from_tree(const qa_save_value *tree, qa_app_sim_presentation_event *out);
/* SimulationEvent: the client's view of a tree qa_app_unified_read_simulation_event returned. Strings
 * borrow the tree; actor/origin/counts point into *storage (release with
 * qa_app_unified_simulation_event_storage_release). A unified frame carries the sound and message
 * payloads only (network-unified.ts frame() filters with permitted(), which drops damage and transition),
 * so a damage or transition payload raises RangeError here. */
typedef struct qa_app_unified_simulation_event_storage {
  qa_actor_id actor; qa_vec3 origin; double *counts;
} qa_app_unified_simulation_event_storage;
void qa_app_unified_simulation_event_view(const qa_save_value *tree, qa_simulation_event *out,
    qa_app_unified_simulation_event_storage *storage);
void qa_app_unified_simulation_event_storage_release(qa_app_unified_simulation_event_storage *storage);
#endif
