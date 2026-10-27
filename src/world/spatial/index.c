/* Port of src/world/spatial/index.ts. GPL-2.0-or-later. */
#include "qa/world/spatial/index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

bool qa_bounds_intersect(qa_bounds first, qa_bounds second) {
  return first.min.x <= second.max.x && first.min.y <= second.max.y &&
         first.min.z <= second.max.z && first.max.x >= second.min.x &&
         first.max.y >= second.min.y && first.max.z >= second.min.z;
}

typedef struct member member;
typedef struct sector sector;
struct sector {
  bool has_split;
  int axis; /* 0=x, 1=y */
  double distance;
  sector *front;
  sector *back;
  member *head;
  member *tail;
};
struct member {
  qa_spatial_actor actor;
  qa_actor_id owner_storage;
  qa_actor_id ground_storage;
  sector *sector;
  member *next;
  member *previous;
};

static double axis_value(qa_vec3 vector, int axis) { return axis == 0 ? vector.x : vector.y; }

static sector *make_sector(qa_bounds bounds, int depth) {
  sector *value = xmalloc(sizeof *value);
  if (depth == 4) return value;
  int axis = bounds.max.x - bounds.min.x > bounds.max.y - bounds.min.y ? 0 : 1;
  /* TS Math.fround staging. */
  double distance = (double)(float)((double)(float)(axis_value(bounds.max, axis) +
                                                     axis_value(bounds.min, axis)) * 0.5);
  qa_bounds front_bounds = bounds, back_bounds = bounds;
  if (axis == 0) { front_bounds.min.x = distance; back_bounds.max.x = distance; }
  else { front_bounds.min.y = distance; back_bounds.max.y = distance; }
  value->has_split = true;
  value->axis = axis;
  value->distance = distance;
  value->front = make_sector(front_bounds, depth + 1);
  value->back = make_sector(back_bounds, depth + 1);
  return value;
}
static void free_sector(sector *value) {
  if (value == NULL) return;
  free_sector(value->front);
  free_sector(value->back);
  free(value);
}

struct qa_spatial_index {
  sector *root;
  member **members; /* by actor slot */
  size_t member_capacity;
  bool has_session;
  qa_session_id session;
  qa_spatial_actor *scratch;
  size_t scratch_count;
  size_t scratch_capacity;
};

qa_spatial_index *qa_spatial_index_create(qa_bounds bounds) {
  qa_spatial_index *index = calloc(1, sizeof *index);
  if (index == NULL) return NULL;
  index->root = make_sector(bounds, 0);
  return index;
}
void qa_spatial_index_destroy(qa_spatial_index *index) {
  if (index == NULL) return;
  for (size_t i = 0; i < index->member_capacity; i++) free(index->members[i]);
  free(index->members);
  free_sector(index->root);
  free(index->scratch);
  free(index);
}

static member *member_at(qa_spatial_index *index, int64_t slot_value) {
  if (slot_value < 0 || (size_t)slot_value >= index->member_capacity) return NULL;
  return index->members[slot_value];
}
static void set_member(qa_spatial_index *index, int64_t slot_value, member *value) {
  if ((size_t)slot_value >= index->member_capacity) {
    size_t capacity = index->member_capacity != 0 ? index->member_capacity : 16;
    while ((size_t)slot_value >= capacity) capacity *= 2;
    index->members = xrealloc(index->members, capacity * sizeof *index->members);
    for (size_t i = index->member_capacity; i < capacity; i++) index->members[i] = NULL;
    index->member_capacity = capacity;
  }
  index->members[slot_value] = value;
}

void qa_spatial_index_link(qa_spatial_index *index, qa_linked_body body,
                           qa_actor_collision collision) {
  if (!index->has_session) {
    index->session = body.actor.session;
    index->has_session = true;
  }
  if (index->session.token != body.actor.session.token)
    qa_range_error("Actor belongs to another scene session");
  member *previous = member_at(index, body.actor.slot);
  if (previous != NULL) qa_spatial_index_unlink(index, previous->actor.body.actor);
  member *value = xmalloc(sizeof *value);
  value->actor.body = body;
  if (body.state.ground != NULL) {
    value->ground_storage = *body.state.ground;
    value->actor.body.state.ground = &value->ground_storage;
  }
  value->actor.collision = collision;
  if (collision.owner != NULL) {
    value->owner_storage = *collision.owner;
    value->actor.collision.owner = &value->owner_storage;
  }
  sector *current = index->root;
  qa_bounds bounds = body.absolute_bounds;
  while (current->has_split) {
    if (axis_value(bounds.min, current->axis) > current->distance) current = current->front;
    else if (axis_value(bounds.max, current->axis) < current->distance) current = current->back;
    else break;
  }
  value->sector = current;
  if (collision.family != NULL && strcmp(collision.family, "q3") == 0) {
    value->next = current->head;
    if (current->head != NULL) current->head->previous = value;
    else current->tail = value;
    current->head = value;
  } else {
    value->previous = current->tail;
    if (current->tail != NULL) current->tail->next = value;
    else current->head = value;
    current->tail = value;
  }
  set_member(index, body.actor.slot, value);
}

void qa_spatial_index_unlink(qa_spatial_index *index, qa_actor_id actor) {
  member *value = member_at(index, actor.slot);
  if (value == NULL || !qa_same_actor(value->actor.body.actor, actor)) return;
  sector *current = value->sector;
  if (current == NULL) return;
  if (value->previous == NULL) current->head = value->next;
  else value->previous->next = value->next;
  if (value->next == NULL) current->tail = value->previous;
  else value->next->previous = value->previous;
  value->sector = NULL;
  index->members[actor.slot] = NULL;
  free(value);
}

bool qa_spatial_index_get(qa_spatial_index *index, qa_actor_id actor, qa_spatial_actor *out) {
  member *value = member_at(index, actor.slot);
  if (value == NULL || !qa_same_actor(value->actor.body.actor, actor)) return false;
  *out = value->actor;
  return true;
}

static bool walk_sector(sector *current, qa_bounds bounds, qa_spatial_visit_fn visit, void *ctx) {
  for (member *value = current->head; value != NULL;) {
    member *next = value->next;
    if (qa_bounds_intersect(value->actor.body.absolute_bounds, bounds)) {
      const char *result = visit(ctx, value->actor);
      if (result != NULL && strcmp(result, "stop") == 0) return false;
      if (result != NULL && strcmp(result, "stop-sector") == 0) return true;
    }
    value = next;
  }
  if (current->has_split) {
    if (axis_value(bounds.max, current->axis) > current->distance &&
        !walk_sector(current->front, bounds, visit, ctx)) return false;
    if (axis_value(bounds.min, current->axis) < current->distance &&
        !walk_sector(current->back, bounds, visit, ctx)) return false;
  }
  return true;
}

void qa_spatial_index_visit(qa_spatial_index *index, qa_bounds bounds,
                            qa_spatial_visit_fn visit, void *ctx) {
  (void)walk_sector(index->root, bounds, visit, ctx);
}

typedef struct query_state {
  qa_spatial_index *index;
  const char *role;
} query_state;
static const char *query_visit(void *ctx, qa_spatial_actor actor) {
  query_state *state = ctx;
  if (strcmp(state->role, "both") == 0 ||
      (actor.collision.role != NULL && strcmp(actor.collision.role, state->role) == 0)) {
    qa_spatial_index *index = state->index;
    if (index->scratch_count == index->scratch_capacity) {
      index->scratch_capacity = index->scratch_capacity != 0 ? index->scratch_capacity * 2 : 8;
      index->scratch = xrealloc(index->scratch, index->scratch_capacity * sizeof *index->scratch);
    }
    index->scratch[index->scratch_count++] = actor;
  }
  return "continue";
}

const qa_spatial_actor *qa_spatial_index_query(qa_spatial_index *index, qa_bounds bounds,
                                               const char *role, size_t *out_count) {
  index->scratch_count = 0;
  query_state state = {index, role != NULL ? role : "both"};
  qa_spatial_index_visit(index, bounds, query_visit, &state);
  *out_count = index->scratch_count;
  return index->scratch;
}

void qa_spatial_index_clear(qa_spatial_index *index) {
  for (size_t i = 0; i < index->member_capacity; i++) {
    member *value = index->members[i];
    if (value == NULL) continue;
    if (value->sector != NULL) {
      value->sector->head = NULL;
      value->sector->tail = NULL;
      value->sector = NULL;
    }
    free(value);
    index->members[i] = NULL;
  }
}
