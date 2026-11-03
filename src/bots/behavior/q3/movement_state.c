/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(qa_bot_avoid_spot) == 20, "avoid spot");
_Static_assert(sizeof(qa_bot_move_record) == 772, "bot_movestate_t");
_Static_assert(offsetof(qa_bot_move_record, view_angles) == 52, "view angles");
_Static_assert(offsetof(qa_bot_move_record, area) == 64, "area");
_Static_assert(offsetof(qa_bot_move_record, last_origin) == 80, "last origin");
_Static_assert(offsetof(qa_bot_move_record, reach_area) == 92, "reach area");
_Static_assert(offsetof(qa_bot_move_record, avoid_reach) == 116, "avoid reach");
_Static_assert(offsetof(qa_bot_move_record, avoid_spots) == 128, "avoid spots");
_Static_assert(offsetof(qa_bot_move_record, num_avoid_spots) == 768, "spot count");

void qa_bot_move_state_init(qa_bot_move_state *st, qa_bot_memory *memory,
                            const qa_bot_memory_allocation *restored) {
  memset(st, 0, sizeof *st);
  st->memory = memory;
  if (restored != NULL) {
    if (restored->length != sizeof(qa_bot_move_record))
      qa_range_error("Saved bot move allocation size mismatch");
    st->allocation = *restored;
  } else if (!qa_bot_memory_allocate(memory, sizeof(qa_bot_move_record), QA_BOT_MEMORY_HEAP, true,
                                     &st->allocation))
    qa_range_error("Out of memory allocating a bot move state");
  st->src = (qa_bot_move_record *)(void *)st->allocation.bytes;
}
qa_bot_move_record *qa_bot_move_state_record(const qa_bot_move_state *st) {
  return (qa_bot_move_record *)(void *)qa_bot_memory_allocation_bytes(&st->allocation);
}
void qa_bot_move_state_set_last_reachability(qa_bot_move_state *st, int64_t value) {
  int32_t cell = (int32_t)value;
  if (cell != st->src->last_reachability)
    st->has_walk_progress = false;
  st->src->last_reachability = cell;
}
void qa_bot_move_state_reset(qa_bot_move_state *st) {
  st->has_walk_progress = false;
  memset(qa_bot_move_state_record(st), 0, sizeof(qa_bot_move_record));
}
void qa_bot_move_state_free(qa_bot_move_state *st) {
  qa_bot_memory_free(st->memory, &st->allocation);
  st->src = NULL;
}
/* i386 source reads tries[1], the adjacent avoidspots[0].origin.x word.
 * Compatibility for that source layout, not defined universal C behavior. */
int32_t qa_bot_move_state_native_reset_last_avoid_probe(const qa_bot_move_state *st) {
  int32_t bits;
  memcpy(&bits, &st->src->avoid_spots[0].origin.x, 4);
  return bits;
}

void qa_bot_move_state_store_init(qa_bot_move_state_store *s) {
  memset(s, 0, sizeof *s);
  qa_bot_memory_init(&s->owned_memory);
  s->memory = &s->owned_memory;
}
void qa_bot_move_state_store_bind(qa_bot_move_state_store *s, qa_bot_move_state_host host,
                                  qa_bot_memory *memory) {
  s->host = host;
  if (memory != NULL)
    s->memory = memory;
}
void qa_bot_move_state_store_destroy(qa_bot_move_state_store *s) {
  qa_bot_memory_destroy(&s->owned_memory);
}
void qa_bot_move_result_init(qa_bot_move_result *r) { memset(r, 0, sizeof *r); }
int64_t qa_bot_move_state_store_allocate(qa_bot_move_state_store *s) {
  for (int i = 1; i <= QA_MAX_MOVE_STATES; i++)
    if (!s->allocated[i]) {
      qa_bot_move_state_init(&s->states[i], s->memory, NULL);
      s->allocated[i] = true;
      return i;
    }
  return 0;
}
qa_bot_move_state *qa_bot_move_state_store_from_handle(qa_bot_move_state_store *s, int64_t h) {
  char b[128];
  if (h <= 0 || h > QA_MAX_MOVE_STATES) {
    if (s->host.print != NULL) {
      snprintf(b, sizeof b, "move state handle %lld out of range\n", (long long)h);
      s->host.print(s->host.ctx, 4, b);
    }
    return NULL;
  }
  if (!s->allocated[h]) {
    if (s->host.print != NULL) {
      snprintf(b, sizeof b, "invalid move state %lld\n", (long long)h);
      s->host.print(s->host.ctx, 4, b);
    }
    return NULL;
  }
  return &s->states[h];
}
void qa_bot_move_state_store_free(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st != NULL) {
    qa_bot_move_state_free(st);
    s->allocated[h] = false;
  }
}
void qa_bot_move_state_store_initialize(qa_bot_move_state_store *s, int64_t h,
                                        qa_bot_init_move in) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st == NULL)
    return;
  if (in.or_move_flags & QA_BOT_MF_TELEPORTED)
    st->has_walk_progress = false;
  qa_bot_move_record *r = st->src;
  r->origin = qa_bot_fv3(in.origin);
  r->velocity = qa_bot_fv3(in.velocity);
  r->view_offset = qa_bot_fv3(in.view_offset);
  r->entity_num = (int32_t)in.entity_num;
  r->client = (int32_t)in.client;
  r->think_time = (float)in.think_time;
  r->presence_type = (int32_t)in.presence_type;
  r->view_angles = qa_bot_fv3(in.view_angles);
  int32_t mask = QA_BOT_MF_ONGROUND | QA_BOT_MF_TELEPORTED | QA_BOT_MF_WATERJUMP | QA_BOT_MF_WALK |
                 QA_BOT_MF_GRAPPLEPULL;
  r->move_flags = (r->move_flags & ~mask) | ((int32_t)in.or_move_flags & mask);
}
void qa_bot_move_state_store_reset(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st != NULL)
    qa_bot_move_state_reset(st);
}
void qa_bot_move_state_store_reset_avoid_reach(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st == NULL)
    return;
  st->src->avoid_reach = 0;
  st->src->avoid_reach_time = 0;
  st->src->avoid_reach_tries = 0;
}
void qa_bot_move_state_store_reset_last_avoid_reach(qa_bot_move_state_store *s, int64_t h) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st == NULL)
    return;
  if (st->src->avoid_reach_time > 0) {
    st->src->avoid_reach_time = 0;
    if (qa_bot_move_state_native_reset_last_avoid_probe(st) > 0)
      st->src->avoid_reach_tries = (int32_t)((uint32_t)st->src->avoid_reach_tries - 1u);
  }
}
void qa_bot_move_state_store_add_to_avoid_reach(qa_bot_move_state_store *s, qa_bot_move_state *st,
                                                int64_t num, double at) {
  float avoid = (float)at;
  float now = (float)s->host.time(s->host.ctx);
  qa_bot_move_record *r = st->src;
  if (r->avoid_reach == (int32_t)num) {
    r->avoid_reach_tries =
        r->avoid_reach_time > now ? (int32_t)((uint32_t)r->avoid_reach_tries + 1u) : 1;
    r->avoid_reach_time = (float)((double)(float)s->host.time(s->host.ctx) + (double)avoid);
    return;
  }
  if (r->avoid_reach_time < now) {
    r->avoid_reach = (int32_t)num;
    r->avoid_reach_time = (float)((double)(float)s->host.time(s->host.ctx) + (double)avoid);
    r->avoid_reach_tries = 1;
  }
}
void qa_bot_move_state_store_add_avoid_spot(qa_bot_move_state_store *s, int64_t h, qa_vec3 o,
                                            double radius, int64_t t) {
  qa_bot_move_state *st = qa_bot_move_state_store_from_handle(s, h);
  if (st == NULL)
    return;
  qa_bot_move_record *r = st->src;
  if (t == QA_AVOID_CLEAR) {
    r->num_avoid_spots = 0;
    return;
  }
  if (r->num_avoid_spots >= QA_MAX_AVOID_SPOTS)
    return;
  if (r->num_avoid_spots < 0)
    qa_range_error("Invalid move-state avoid-spot count");
  qa_bot_avoid_spot *sp = &r->avoid_spots[r->num_avoid_spots];
  sp->origin = qa_bot_fv3(o);
  sp->radius = (float)radius;
  sp->type = (int32_t)t;
  r->num_avoid_spots++;
}
int64_t qa_bot_move_state_store_setup(qa_bot_move_state_store *s) {
  void *c = s->host.ctx;
  s->sv_max_step = s->host.lib_var(c, "sv_step", "18");
  s->sv_max_barrier = s->host.lib_var(c, "sv_maxbarrier", "32");
  s->sv_gravity = s->host.lib_var(c, "sv_gravity", "800");
  s->rocket_launcher_index = s->host.lib_var(c, "weapindex_rocketlauncher", "5");
  s->bfg_index = s->host.lib_var(c, "weapindex_bfg10k", "9");
  s->grapple_index = s->host.lib_var(c, "weapindex_grapple", "10");
  s->missile_entity_type = s->host.lib_var(c, "entitytypemissile", "3");
  s->offhand_grapple = s->host.lib_var(c, "offhandgrapple", "0");
  s->grapple_on_command = s->host.lib_var(c, "cmd_grappleon", "grappleon");
  s->grapple_off_command = s->host.lib_var(c, "cmd_grappleoff", "grappleoff");
  return 0;
}
void qa_bot_move_state_store_shutdown(qa_bot_move_state_store *s) {
  for (int i = 1; i <= QA_MAX_MOVE_STATES; i++)
    if (s->allocated[i]) {
      qa_bot_move_state_free(&s->states[i]);
      s->allocated[i] = false;
    }
}

static qa_save_value *sv(qa_save_value *value) {
  if (value == NULL)
    qa_range_error("Out of memory building bot move checkpoint");
  return value;
}
static void set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child))
    qa_range_error("Out of memory building bot move checkpoint");
}
#define VARIABLES(X)                                                                               \
  X("svMaxStep", sv_max_step)                                                                      \
  X("svMaxBarrier", sv_max_barrier)                                                                \
  X("svGravity", sv_gravity)                                                                       \
  X("rocketLauncherIndex", rocket_launcher_index)                                                  \
  X("bfgIndex", bfg_index)                                                                         \
  X("grappleIndex", grapple_index)                                                                 \
  X("missileEntityType", missile_entity_type)                                                      \
  X("offhandGrapple", offhand_grapple)                                                             \
  X("grappleOnCommand", grapple_on_command)                                                        \
  X("grappleOffCommand", grapple_off_command)
static qa_save_value *reference(qa_bot_lib_vars *variables, const qa_bot_lib_var *variable) {
  return sv(variable == NULL
                ? qa_save_value_null()
                : qa_save_value_number((double)qa_bot_lib_vars_reference(variables, variable)));
}
qa_save_value *qa_bot_move_state_store_checkpoint(qa_bot_move_state_store *s,
                                                  qa_bot_memory_capture *memory,
                                                  qa_bot_lib_vars *variables) {
  qa_save_value *out = sv(qa_save_value_object()), *states = sv(qa_save_value_array());
  for (int i = 0; i <= QA_MAX_MOVE_STATES; i++) {
    qa_save_value *entry;
    if (!s->allocated[i])
      entry = sv(qa_save_value_null());
    else {
      entry = sv(qa_save_value_object());
      set(entry, "allocation",
          sv(qa_save_value_number(
              (double)qa_bot_memory_capture_reference(memory, &s->states[i].allocation))));
      set(entry, "walkEdge",
          sv(s->states[i].has_walk_progress
                 ? qa_save_value_number((double)s->states[i].walk_edge_id)
                 : qa_save_value_null()));
    }
    if (!qa_save_value_push(states, entry))
      qa_range_error("Out of memory building bot move checkpoint");
  }
  set(out, "states", states);
  qa_save_value *refs = sv(qa_save_value_object());
#define CAPTURE(name, member) set(refs, name, reference(variables, s->member));
  VARIABLES(CAPTURE)
#undef CAPTURE
  set(out, "variables", refs);
  return out;
}
static void field(const qa_save_reader *r, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(r, name, out))
    qa_range_error("Out of memory reading bot move checkpoint");
}
void qa_bot_move_state_store_restore(qa_bot_move_state_store *s, const qa_save_value *value,
                                     qa_bot_memory_restored *memory, qa_bot_lib_vars *variables,
                                     qa_bot_move_edge_fn edge, void *edge_ctx) {
  qa_save_reader reader, refs, states;
  if (!qa_save_reader_init(&reader, value, "bot.movement"))
    qa_range_error("Out of memory reading bot move checkpoint");
  field(&reader, "variables", &refs);
  /* TS reads every variable pointer (integer(1)) before validating the states. */
  double pointers[10];
  int next = 0;
#define READ_POINTER(name, member)                                                                 \
  {                                                                                                \
    qa_save_reader cell;                                                                           \
    field(&refs, name, &cell);                                                                     \
    pointers[next++] = qa_save_reader_is_null(&cell) ? -1 : qa_save_reader_integer_min(&cell, 1);  \
    qa_save_reader_destroy(&cell);                                                                 \
  }
  VARIABLES(READ_POINTER)
#undef READ_POINTER
  field(&reader, "states", &states);
  size_t count = qa_save_reader_list_count(&states);
  bool invalid = count != QA_MAX_MOVE_STATES + 1;
  for (int i = 0; i <= QA_MAX_MOVE_STATES; i++)
    invalid = invalid || s->allocated[i];
  if (!invalid) {
    qa_save_reader first;
    if (!qa_save_reader_item(&states, 0, &first))
      qa_range_error("Out of memory reading bot move checkpoint");
    invalid = !qa_save_reader_is_null(&first);
    qa_save_reader_destroy(&first);
  }
  if (invalid)
    qa_range_error("Invalid bot move state restoration");
  bool allocated[QA_MAX_MOVE_STATES + 1];
  qa_bot_move_state restored[QA_MAX_MOVE_STATES + 1];
  memset(allocated, 0, sizeof allocated);
  for (size_t i = 1; i < count; i++) {
    qa_save_reader entry, cell;
    if (!qa_save_reader_item(&states, i, &entry))
      qa_range_error("Out of memory reading bot move checkpoint");
    if (!qa_save_reader_is_null(&entry)) {
      field(&entry, "allocation", &cell);
      qa_bot_memory_allocation allocation =
          qa_bot_memory_restored_allocation(memory, (int64_t)qa_save_reader_integer_min(&cell, 0));
      qa_save_reader_destroy(&cell);
      qa_bot_move_state_init(&restored[i], s->memory, &allocation);
      allocated[i] = true;
      field(&entry, "walkEdge", &cell);
      if (!qa_save_reader_is_null(&cell)) {
        int64_t id = (int64_t)qa_save_reader_integer_min(&cell, 0);
        if (edge == NULL || !edge(edge_ctx, restored[i].src->client, id))
          qa_range_error("Saved bot movement references an unknown navigation edge");
        restored[i].has_walk_progress = true;
        restored[i].walk_edge_id = id;
      }
      qa_save_reader_destroy(&cell);
    }
    qa_save_reader_destroy(&entry);
  }
  next = 0;
  qa_bot_lib_var *resolved[10];
  for (int i = 0; i < 10; i++)
    resolved[i] = pointers[i] < 0 ? NULL : qa_bot_lib_vars_resolve(variables, (int64_t)pointers[i]);
  for (size_t i = 1; i < count; i++)
    if (allocated[i]) {
      s->states[i] = restored[i];
      s->allocated[i] = true;
    }
#define ASSIGN(name, member) s->member = resolved[next++];
  VARIABLES(ASSIGN)
#undef ASSIGN
  qa_save_reader_destroy(&states);
  qa_save_reader_destroy(&refs);
  qa_save_reader_destroy(&reader);
}
