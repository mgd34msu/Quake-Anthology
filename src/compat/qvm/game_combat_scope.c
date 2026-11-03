/* Port of src/compat/qvm/game-combat-scope.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_combat_scope.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

#define MAX_FRAMES 32

struct qa_qvm_damage_scopes {
  qa_qvm_damage_scope_options options;
  qa_qvm_damage_frame frames[MAX_FRAMES];
  size_t frame_count;
};

qa_qvm_damage_scopes *qa_qvm_damage_scopes_create(qa_qvm_damage_scope_options options) {
  qa_qvm_damage_scopes *scopes = calloc(1, sizeof *scopes);
  if (scopes == NULL) return NULL;
  scopes->options = options;
  return scopes;
}

void qa_qvm_damage_scopes_destroy(qa_qvm_damage_scopes *scopes) { free(scopes); }

qa_qvm_damage_frame *qa_qvm_damage_scopes_current(qa_qvm_damage_scopes *scopes, double pointer) {
  if (scopes->frame_count == 0) return NULL;
  qa_qvm_damage_frame *frame = &scopes->frames[scopes->frame_count - 1];
  return frame->pointer == pointer && !frame->reacting ? frame : NULL;
}

typedef struct cancel_job {
  qa_qvm_module_function_call *call;
  qa_qvm_cancellation_scope scope;
} cancel_job;

static void cancel_body(void *ctx) {
  cancel_job *job = ctx;
  job->call->cancel_function(job->call->call_ctx, job->scope);
}

void qa_qvm_damage_scopes_cancel(qa_qvm_damage_scopes *scopes, qa_qvm_damage_frame *frame,
                                 qa_qvm_module_function_call *call) {
  (void)scopes;
  /* TS: try { call.cancelFunction(...) } catch (error) { frame.cancelled = { error }; throw error; } */
  cancel_job job = {call, frame->cancellation};
  qa_caught_error caught;
  if (qa_try(cancel_body, &job, &caught)) qa_range_error("QVM function scope cancelled");
  frame->cancelled = true;
  snprintf(frame->cancelled_error, sizeof frame->cancelled_error, "%s", caught.message);
  qa_rethrow(&caught);
}

static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool same_armor(qa_armor_state a, qa_armor_state b) {
  return a.regular.kind == QA_REGULAR_ARMOR_NONE
             ? b.regular.kind == QA_REGULAR_ARMOR_NONE
             : a.regular.kind == QA_REGULAR_ARMOR_Q3 && b.regular.kind == QA_REGULAR_ARMOR_Q3 &&
                   a.regular.points == b.regular.points &&
                   a.regular.protection == b.regular.protection;
}

struct run_ctx;
typedef struct run_ctx {
  qa_qvm_damage_scopes *scopes;
  qa_qvm_damage_frame *frame;
  qa_owned_actor actor;
  double slot;
  qa_source_damage_observer observer;
  qa_qvm_writable_view entity;
  int64_t health_offset;
  qa_qvm_write_range health_range, velocity_range;
  qa_qvm_write_range *armor_ranges; /* owned */
  size_t armor_range_count;
  qa_qvm_write_range *ranges; /* owned */
  int32_t health;
  qa_armor_state armor;
  qa_vec3 velocity;
  qa_source_damage_result result;
  qa_qvm_writable_view velocity_view;
  /* reaction observers */
  struct {
    int64_t offset;
    double entry;
    size_t removal;
    bool has_removal;
    const char *reaction;
    qa_qvm_reaction_call roles;
  } reactions[2];
  size_t watch;
  size_t refresh_watches[2];
  bool cleaned;
  qa_cleanup guard;
  struct reaction_ctx {
    struct run_ctx *run;
    int index;
    double entry;
  } reaction_contexts[2];
  struct refresh_ctx {
    struct run_ctx *run;
    int index;
  } refresh_contexts[2];
} run_ctx;

static bool actor_equal(qa_owned_actor a, qa_owned_actor b) {
  return a.token == b.token && a.id.slot == b.id.slot && a.id.generation == b.id.generation &&
         a.id.token == b.id.token && strcmp(a.owner, b.owner) == 0;
}

static bool run_current(run_ctx *run) {
  qa_qvm_damage_scopes *scopes = run->scopes;
  for (size_t index = scopes->frame_count; index-- > 0;) {
    qa_qvm_damage_frame *entry = &scopes->frames[index];
    if (actor_equal(entry->actor, run->actor))
      return entry == run->frame && !run->frame->reacting &&
             scopes->options.live(scopes->options.ctx, run->actor);
  }
  return false;
}

static bool touches(const qa_qvm_committed_write *event, qa_qvm_write_range range) {
  for (size_t i = 0; i < event->range_count; i++) {
    const qa_qvm_committed_bytes *write = &event->ranges[i];
    if (write->byte_offset < range.byte_offset + range.byte_length &&
        range.byte_offset < write->byte_offset + (int64_t)write->length)
      return true;
  }
  return false;
}

typedef struct change_item {
  int64_t offset;
  int kind; /* 0 health 1 armor 2 velocity */
  double health_before, health_after;
  qa_armor_state armor_before, armor_after;
  qa_vec3 velocity_before, velocity_after;
} change_item;

static qa_vec3 read_velocity(run_ctx *run) {
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&run->velocity_view, 0, true),
               qa_qvm_writable_view_get_float32(&run->velocity_view, 4, true),
               qa_qvm_writable_view_get_float32(&run->velocity_view, 8, true)};
  return v;
}

static void write_observer(void *ctx, const qa_qvm_committed_write *event) {
  run_ctx *run = ctx;
  bool report = run_current(run);
  change_item changes[3];
  size_t change_count = 0;
  if (touches(event, run->health_range)) {
    double before = run->health;
    run->health = qa_qvm_writable_view_get_int32(&run->entity, run->health_offset, true);
    if (report && before != run->health) {
      changes[change_count].offset = run->health_range.byte_offset;
      changes[change_count].kind = 0;
      changes[change_count].health_before = before;
      changes[change_count].health_after = run->health;
      change_count++;
    }
  }
  bool armor_touched = false;
  int64_t armor_offset = 0;
  for (size_t i = 0; i < run->armor_range_count; i++)
    if (touches(event, run->armor_ranges[i])) {
      armor_touched = true;
      armor_offset = run->armor_ranges[i].byte_offset;
      break;
    }
  if (armor_touched) {
    qa_armor_state before = run->armor;
    run->armor = run->scopes->options.armor(run->scopes->options.ctx, run->slot);
    if (report && !same_armor(before, run->armor)) {
      changes[change_count].offset = armor_offset;
      changes[change_count].kind = 1;
      changes[change_count].armor_before = before;
      changes[change_count].armor_after = run->armor;
      change_count++;
    }
  }
  if (touches(event, run->velocity_range)) {
    qa_vec3 before = run->velocity;
    run->velocity = read_velocity(run);
    if (report && !same_vector(before, run->velocity)) {
      changes[change_count].offset = run->velocity_range.byte_offset;
      changes[change_count].kind = 2;
      changes[change_count].velocity_before = before;
      changes[change_count].velocity_after = run->velocity;
      change_count++;
    }
  }
  for (size_t i = 0; i < change_count; i++)
    for (size_t j = i + 1; j < change_count; j++)
      if (changes[j].offset < changes[i].offset) {
        change_item swap = changes[i];
        changes[i] = changes[j];
        changes[j] = swap;
      }
  for (size_t i = 0; i < change_count; i++) {
    qa_damage_mutation mutation;
    memset(&mutation, 0, sizeof mutation);
    if (changes[i].kind == 0) {
      mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
      mutation.health_before = changes[i].health_before;
      mutation.health_after = changes[i].health_after;
      run->observer.stored(run->observer.ctx, mutation);
      run->result.applied_damage += changes[i].health_before - changes[i].health_after;
    } else if (changes[i].kind == 1) {
      mutation.kind = QA_DAMAGE_MUTATION_ARMOR;
      mutation.armor_before = changes[i].armor_before;
      mutation.armor_after = changes[i].armor_after;
      run->observer.stored(run->observer.ctx, mutation);
    } else {
      mutation.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY;
      mutation.velocity_before = changes[i].velocity_before;
      mutation.velocity_after = changes[i].velocity_after;
      mutation.movement_provider = run->frame->request.attack.movement_provider;
      run->observer.stored(run->observer.ctx, mutation);
    }
  }
}

typedef struct reaction_ctx reaction_ctx;

static int32_t call_argument(qa_qvm_module_function_call *call, double argument_index) {
  int64_t offset = (int64_t)argument_index * 4;
  if (offset < 0 || offset > (int64_t)call->words_length - 4)
    qa_range_error("QVM argument index outside source call");
  const uint8_t *p = call->words + offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}

static void reaction_observe(void *ctx, qa_qvm_module_function_call *call, int64_t argument_base) {
  (void)argument_base;
  struct reaction_ctx *reaction = ctx;
  run_ctx *run = reaction->run;
  int index = reaction->index;
  if (qa_qvm_writable_view_get_int32(&run->entity, run->reactions[index].offset, true) !=
          (int32_t)reaction->entry ||
      !run_current(run) ||
      call_argument(call, run->reactions[index].roles.role_target) != (int32_t)run->frame->pointer)
    return;
  run->frame->reacting = true;
  run->result.reaction = run->reactions[index].reaction;
  run->result.applied_damage = call_argument(call, run->reactions[index].roles.role_amount);
  run->observer.before_reaction(run->observer.ctx, run->result);
  if (!run->scopes->options.live(run->scopes->options.ctx, run->actor))
    qa_qvm_damage_scopes_cancel(run->scopes, run->frame, call);
}

static void reaction_refresh(run_ctx *run, int index) {
  qa_qvm_damage_scope_options *options = &run->scopes->options;
  int32_t next = qa_qvm_writable_view_get_int32(&run->entity, run->reactions[index].offset, true);
  if (next == (int32_t)run->reactions[index].entry) return;
  if (run->reactions[index].has_removal) {
    qa_qvm_module_unbind(&options->game->module, run->reactions[index].removal);
    run->reactions[index].has_removal = false;
  }
  run->reactions[index].entry = next;
  if (next != 0) {
    run->reaction_contexts[index].run = run;
    run->reaction_contexts[index].index = index;
    run->reaction_contexts[index].entry = next;
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = qa_qvm_module_get_profile(&options->game->module)->module;
    reference.instruction_index = next;
    run->reactions[index].removal = qa_qvm_module_observe_function(
        &options->game->module, &reference, reaction_observe, &run->reaction_contexts[index]);
    run->reactions[index].has_removal = true;
  }
}

static void refresh_observer(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  struct refresh_ctx *refresh = ctx;
  reaction_refresh(refresh->run, refresh->index);
}

/* TS finally: removals.runAll() and frames.pop(). The caller guarantees the
 * run owns the top frame. */
static void run_cleanup(run_ctx *run) {
  if (run->cleaned) return;
  run->cleaned = true;
  qa_qvm_game *game = run->scopes->options.game;
  qa_qvm_memory_writes_unobserve(&game->module.memory->writes, run->watch);
  for (int i = 0; i < 2; i++) {
    if (run->reactions[i].has_removal) qa_qvm_module_unbind(&game->module, run->reactions[i].removal);
    run->reactions[i].has_removal = false;
    qa_qvm_memory_writes_unobserve(&game->module.memory->writes, run->refresh_watches[i]);
  }
  run->frame->run = NULL;
  run->scopes->frame_count--;
}

static int32_t call_word(qa_qvm_module_function_call *call, double byte_index) {
  return call_argument(call, byte_index / 4);
}

static void proceed_body(void *ctx) {
  qa_qvm_module_function_call *call = ctx;
  (void)call->proceed(call->call_ctx);
}

static void run_free(run_ctx *run) {
  free(run->armor_ranges);
  free(run->ranges);
  free(run);
}

/* A throw leaving run(): TS finally. */
static void run_guard_unwind(void *ctx) {
  run_ctx *run = ctx;
  run_cleanup(run);
  run_free(run);
}

qa_source_damage_result qa_qvm_damage_scopes_run(qa_qvm_damage_scopes *scopes,
                                                 qa_qvm_module_function_call *call, qa_owned_actor actor,
                                                 double slot, qa_damage_request request,
                                                 qa_source_damage_observer observer) {
  qa_qvm_damage_scope_options *options = &scopes->options;
  qa_qvm_game *game = options->game;
  if (scopes->frame_count >= MAX_FRAMES) qa_range_error("out of memory");
  qa_qvm_damage_frame *frame = &scopes->frames[scopes->frame_count];
  memset(frame, 0, sizeof *frame);
  frame->actor = actor;
  frame->request = request;
  frame->call = call;
  frame->pointer = call_word(call, options->target_argument * 4);
  frame->cancellation = call->cancellation_scope(call->call_ctx);
  run_ctx *run = calloc(1, sizeof *run);
  if (run == NULL) qa_range_error("out of memory");
  size_t armor_capacity = 2 + options->mode_word_count;
  run->armor_ranges = malloc(armor_capacity * sizeof *run->armor_ranges);
  run->ranges = malloc((armor_capacity + 2) * sizeof *run->ranges);
  if (run->armor_ranges == NULL || run->ranges == NULL) qa_range_error("out of memory");
  frame->run = run;
  run->scopes = scopes;
  run->frame = frame;
  run->actor = actor;
  run->slot = slot;
  run->observer = observer;
  run->result.reaction = "none";
  qa_qvm_game_data_entity_bytes(&game->data, slot, &run->entity);
  int64_t entity_offset = run->entity.start;
  run->health_offset = (int64_t)options->health;
  run->health_range.byte_offset = entity_offset + run->health_offset;
  run->health_range.byte_length = 4;
  bool has_client = slot < qa_qvm_game_data_get_num_clients(&game->data);
  qa_qvm_writable_view client;
  int64_t client_offset = -1;
  if (has_client) {
    qa_qvm_game_data_client_bytes(&game->data, slot, &client);
    client_offset = client.start;
  }
  if (client_offset >= 0) {
    run->armor_ranges[run->armor_range_count].byte_offset =
        client_offset + 184 + (int64_t)options->points_stat * 4;
    run->armor_ranges[run->armor_range_count].byte_length = 4;
    run->armor_range_count++;
    if (options->has_tier_stat) {
      run->armor_ranges[run->armor_range_count].byte_offset =
          client_offset + 184 + (int64_t)options->tier_stat * 4;
      run->armor_ranges[run->armor_range_count].byte_length = 4;
      run->armor_range_count++;
    }
    for (size_t i = 0; i < options->mode_word_count; i++) {
      run->armor_ranges[run->armor_range_count].byte_offset = (int64_t)options->mode_words[i];
      run->armor_ranges[run->armor_range_count].byte_length = 4;
      run->armor_range_count++;
    }
  }
  run->velocity_range.byte_offset = client_offset < 0 ? entity_offset + 36 : client_offset + 32;
  run->velocity_range.byte_length = 12;
  qa_qvm_memory_data_view(game->module.memory, run->velocity_range.byte_offset, 12,
                          &run->velocity_view);
  run->health = qa_qvm_writable_view_get_int32(&run->entity, run->health_offset, true);
  run->armor = options->armor(options->ctx, slot);
  run->velocity = read_velocity(run);
  scopes->frame_count++;
  run->ranges[0] = run->health_range;
  run->ranges[1] = run->velocity_range;
  for (size_t i = 0; i < run->armor_range_count; i++) run->ranges[2 + i] = run->armor_ranges[i];
  run->watch = qa_qvm_memory_observe_writes(game->module.memory, run->ranges,
                                            2 + run->armor_range_count, write_observer, run,
                                            NULL, NULL);
  const double offsets[2] = {options->reactions.pain, options->reactions.die};
  const qa_qvm_reaction_call roles[2] = {options->reactions.pain_call, options->reactions.die_call};
  const char *names[2] = {"pain", "death"};
  for (int i = 0; i < 2; i++) {
    run->reactions[i].offset = (int64_t)offsets[i];
    run->reactions[i].reaction = names[i];
    run->reactions[i].roles = roles[i];
    run->refresh_contexts[i].run = run;
    run->refresh_contexts[i].index = i;
    qa_qvm_write_range range = {entity_offset + (int64_t)offsets[i], 4};
    run->refresh_watches[i] = qa_qvm_memory_observe_writes(game->module.memory, &range, 1,
                                                           refresh_observer, &run->refresh_contexts[i],
                                                           NULL, NULL);
    reaction_refresh(run, i);
  }
  /* TS: try { call.proceed(); } catch (error) { if (frame.cancelled?.error !== error) throw error; } */
  qa_cleanup_push(&run->guard, run_guard_unwind, run);
  qa_caught_error caught;
  if (!qa_try(proceed_body, call, &caught) &&
      !(frame->cancelled && strcmp(caught.message, frame->cancelled_error) == 0))
    qa_rethrow(&caught);
  qa_cleanup_pop(&run->guard);
  run_cleanup(run);
  qa_source_damage_result result = run->result;
  run_free(run);
  return result;
}
