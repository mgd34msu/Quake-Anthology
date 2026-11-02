/* Port of src/app/bootstrap/network/unified-component-publication.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_component_publication.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_unified_component_publisher {
  double revision;
  qa_app_unified_native_publication *native; size_t native_count;
  qa_app_unified_component_publication *sources; size_t source_count;
  struct { const char *provider; double sequence; } *sequences;
  size_t sequence_count, sequence_cap;
};
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}
qa_app_unified_component_publisher *qa_app_unified_component_publisher_create(void) {
  return calloc(1, sizeof(qa_app_unified_component_publisher));
}
void qa_app_unified_component_publisher_destroy(qa_app_unified_component_publisher *publisher) {
  if (!publisher) return;
  free(publisher->native);
  free(publisher->sources);
  free(publisher->sequences);
  free(publisher);
}
void qa_app_unified_component_projection_free(qa_app_unified_component_projection *projection) {
  if (projection->has_update) {
    /* States borrow game-state records and command strings from the
     * publications; free only the shells (not update_free, which
     * would destroy borrowed records). */
    for (size_t i = 0; i < projection->update.source_count; i++)
      free((void *)projection->update.sources[i].commands);
    free(projection->update.sources);
    free(projection->update.native);
  }
  for (size_t i = 0; i < projection->frame.source_count; i++) {
    free(projection->frame.sources[i].bindings);
    free((void *)projection->frame.sources[i].scene.snapshot.entities);
  }
  free(projection->frame.sources);
  qa_app_unified_native_frames_free(projection->frame.native, projection->frame.native_count);
  memset(projection, 0, sizeof *projection);
}
static double *sequence_slot(qa_app_unified_component_publisher *p, const char *provider) {
  for (size_t i = 0; i < p->sequence_count; i++)
    if (strcmp(p->sequences[i].provider, provider) == 0) return &p->sequences[i].sequence;
  return NULL;
}
static void sequence_set(qa_app_unified_component_publisher *p, const char *provider, double value) {
  double *slot = sequence_slot(p, provider);
  if (slot) { *slot = value; return; }
  if (p->sequence_count == p->sequence_cap) {
    p->sequence_cap = p->sequence_cap ? p->sequence_cap * 2 : 8;
    p->sequences = realloc(p->sequences, p->sequence_cap * sizeof *p->sequences);
    if (!p->sequences) qa_range_error("out of memory");
  }
  p->sequences[p->sequence_count].provider = provider;
  p->sequences[p->sequence_count].sequence = value;
  p->sequence_count++;
}
qa_app_unified_component_projection qa_app_unified_component_publisher_project(
    qa_app_unified_component_publisher *publisher,
    const qa_app_unified_component_publication *sources, size_t source_count,
    const qa_app_unified_native_publication *native, size_t native_count) {
  bool changed = source_count != publisher->source_count;
  if (!changed)
    for (size_t i = 0; i < source_count; i++)
      if (!qa_same_presentation_owner(&publisher->sources[i].identity.owner,
              sources[i].identity.owner)) changed = true;
  qa_app_unified_component_state *states = xmalloc(source_count * sizeof *states);
  for (size_t i = 0; i < source_count; i++) {
    const qa_app_unified_component_publication *source = &sources[i];
    const qa_app_unified_component_publication *old = NULL;
    for (size_t j = 0; j < publisher->source_count; j++)
      if (strcmp(publisher->sources[j].identity.owner.provider,
              source->identity.owner.provider) == 0) old = &publisher->sources[j];
    bool same = old && qa_same_presentation_owner(&source->identity.owner, old->identity.owner)
        && source->identity.generation == old->identity.generation;
    if (same && (!qa_same_mod_identity(&source->identity.identity, &old->identity.identity)
            || strcmp(source->identity.abi, old->identity.abi) != 0
            || strcmp(source->identity.runtime, old->identity.runtime) != 0
            || !actor_equal(source->viewer, old->viewer)))
      qa_range_error("Component activation changed identity or recipient");
    const qa_qvm_scene_context *scene = source->scene;
    double sequence = scene ? (double)scene->snapshot.server_command_sequence : 0;
    double *stored = sequence_slot(publisher, source->identity.owner.provider);
    double base = same && stored ? *stored : sequence;
    if (sequence < base) qa_range_error("Original component command sequence moved backward");
    size_t available = scene ? scene->command_count : 0;
    size_t first = 0;
    while (first < available && (double)scene->commands[first].sequence <= base) first++;
    size_t command_count = available - first;
    if (sequence != base && (command_count == 0
            || (double)scene->commands[first].sequence != base + 1
            || (double)scene->commands[available - 1].sequence != sequence))
      qa_range_error("Original component reliable commands exceeded their retained window");
    bool game_changed = !same || old->game_state_revision != source->game_state_revision;
    if (same && source->game_state_revision < old->game_state_revision)
      qa_range_error("Original component configstring revision moved backward");
    changed = changed || !same || game_changed || command_count > 0;
    sequence_set(publisher, source->identity.owner.provider, sequence);
    memset(&states[i], 0, sizeof states[i]);
    states[i].identity = source->identity;
    states[i].game_state_revision = source->game_state_revision;
    states[i].has_game_state = game_changed;
    if (game_changed && source->game_state) states[i].game_state = *source->game_state;
    states[i].command_base = base;
    states[i].command_count = command_count;
    states[i].commands = xmalloc(command_count * sizeof *states[i].commands);
    if (command_count)
      memcpy(states[i].commands, scene->commands + first,
          command_count * sizeof *states[i].commands);
  }
  size_t kept = 0;
  for (size_t i = 0; i < publisher->sequence_count; i++) {
    bool present = false;
    for (size_t j = 0; j < source_count; j++)
      if (strcmp(sources[j].identity.owner.provider, publisher->sequences[i].provider) == 0)
        present = true;
    if (present) publisher->sequences[kept++] = publisher->sequences[i];
  }
  publisher->sequence_count = kept;
  qa_app_unified_native_projection projected = qa_app_unified_project_native_components(
      publisher->native, publisher->native_count, native, native_count);
  free(publisher->sources);
  publisher->sources = xmalloc(source_count * sizeof *publisher->sources);
  if (source_count) memcpy(publisher->sources, sources, source_count * sizeof *sources);
  publisher->source_count = source_count;
  free(publisher->native);
  publisher->native = xmalloc(native_count * sizeof *publisher->native);
  if (native_count) memcpy(publisher->native, native, native_count * sizeof *native);
  publisher->native_count = native_count;
  qa_app_unified_component_projection out;
  memset(&out, 0, sizeof out);
  if (changed || projected.changed) {
    out.has_update = true;
    out.update.revision = ++publisher->revision;
    out.update.sources = states;
    out.update.source_count = source_count;
    out.update.native = projected.states;
    out.update.native_count = projected.state_count;
  } else {
    for (size_t i = 0; i < source_count; i++) free(states[i].commands);
    free(states);
    free(projected.states);
  }
  out.frame.revision = publisher->revision;
  out.frame.has_native = true;
  out.frame.native = projected.frames;
  out.frame.native_count = projected.frame_count;
  out.frame.sources = xmalloc(source_count * sizeof *out.frame.sources);
  out.frame.source_count = source_count;
  for (size_t i = 0; i < source_count; i++) {
    const qa_app_unified_component_publication *source = &sources[i];
    qa_app_unified_component_frame *frame = &out.frame.sources[i];
    memset(frame, 0, sizeof *frame);
    frame->owner = source->identity.owner;
    frame->generation = source->identity.generation;
    frame->abi = source->identity.abi;
    frame->viewer = source->viewer;
    frame->client_number = source->has_client_number ? source->client_number
        : source->snapshot.player_state.client_number;
    frame->game_state_revision = source->game_state_revision;
    frame->snapshot = source->snapshot;
    frame->weapon_presented = source->has_weapon_presented && source->weapon_presented;
    frame->binding_count = source->binding_count;
    frame->bindings = xmalloc(source->binding_count * sizeof *frame->bindings);
    if (source->binding_count)
      memcpy(frame->bindings, source->bindings, source->binding_count * sizeof *frame->bindings);
    if (source->scene) {
      frame->has_scene = true;
      frame->scene.revision = (double)source->scene->revision;
      frame->scene.snapshot = source->scene->snapshot;
      frame->scene.snapshot.entities = xmalloc(
          source->scene->snapshot.entity_count * sizeof *frame->scene.snapshot.entities);
      if (source->scene->snapshot.entity_count)
        memcpy((void *)frame->scene.snapshot.entities, source->scene->snapshot.entities,
            source->scene->snapshot.entity_count * sizeof *frame->scene.snapshot.entities);
    }
  }
  return out;
}
