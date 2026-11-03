/* Port of src/world/gameplay/policies.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/policies.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}
static double fround_of(double value) { return (double)(float)value; }
static double number_for(const char *profile, double value) {
  return str_eq(profile, "binary32") ? fround_of(value) : value;
}
static qa_vec3 scale_vec(qa_vec3 direction, double amount, const char *arithmetic) {
  double x = number_for(arithmetic, direction.x);
  double y = number_for(arithmetic, direction.y);
  double z = number_for(arithmetic, direction.z);
  double length = number_for(arithmetic, sqrt(number_for(arithmetic,
      number_for(arithmetic, number_for(arithmetic, x * x) + number_for(arithmetic, y * y)) +
      number_for(arithmetic, z * z))));
  qa_vec3 zero = {0, 0, 0};
  if (length == 0) return zero;
  double inverse = number_for(arithmetic, 1 / length);
  qa_vec3 result;
  result.x = number_for(arithmetic, number_for(arithmetic, x * inverse) * amount);
  result.y = number_for(arithmetic, number_for(arithmetic, y * inverse) * amount);
  result.z = number_for(arithmetic, number_for(arithmetic, z * inverse) * amount);
  return result;
}
static bool self_damage(qa_damage_request request) {
  return request.attack.attacker != NULL && qa_same_actor(request.target, *request.attack.attacker);
}
static bool same_team(qa_combat_state target, const qa_combat_state *attacker) {
  return target.team != NULL && target.team[0] != '\0' && attacker != NULL &&
         str_eq(target.team, attacker->team);
}
static const qa_combat_state *attacker_of(qa_current_combat_state current,
                                          qa_combat_state *storage, bool *has) {
  qa_combat_state_result result = current.attacker(current.ctx);
  *has = result.has_value;
  if (result.has_value) *storage = result.value;
  return result.has_value ? storage : NULL;
}

/* The progress owns the copy; the authority frees it once committed
 * (authority.c release_progress_mutations). No mutations: NULL. */
static qa_damage_mutation *copy_mutations(const qa_damage_mutation *mutations, size_t count) {
  if (count == 0) return NULL;
  qa_damage_mutation *copy = xcalloc(count * sizeof *copy);
  memcpy(copy, mutations, count * sizeof *copy);
  return copy;
}
static void decision_progress(qa_combat_progress *out, qa_damage_request request,
                              const qa_damage_mutation *mutations, size_t count,
                              qa_combat_result result) {
  memset(out, 0, sizeof *out);
  out->kind = "complete";
  out->request = request;
  out->mutations = copy_mutations(mutations, count);
  out->mutation_count = count;
  out->result = result;
}
static qa_combat_result plain_result(double applied, const char *reaction) {
  qa_combat_result result;
  memset(&result, 0, sizeof result);
  result.applied_damage = applied;
  result.reaction = reaction;
  return result;
}
static void none_progress(qa_combat_progress *out, qa_damage_request request) {
  decision_progress(out, request, NULL, 0, plain_result(0, "none"));
}

/* Armor-stage wrapper: fallback + saved-resume over a typed closure. */
typedef struct stage_state {
  qa_damage_request request;
  double amount;
  qa_armor_damage_flags flags;
  qa_victim_armor_policy armor;
  void *resume_ctx;
  void (*resume)(void *ctx, double saved, qa_current_combat_state current,
                 qa_combat_progress *out);
  void (*resume_discard)(void *ctx);
} stage_state;
static qa_armor_result stage_fallback(void *ctx, qa_armor_state current) {
  stage_state *state = ctx;
  qa_armor_damage_flags flags = state->flags;
  return state->armor.call(state->armor.ctx, state->request, current, state->amount, flags);
}
static void stage_resume(void *ctx, qa_armor_stage_result result,
                         qa_current_combat_state current, qa_combat_progress *out) {
  stage_state *state = ctx;
  void *resume_ctx = state->resume_ctx;
  void (*resume)(void *, double, qa_current_combat_state, qa_combat_progress *) = state->resume;
  double saved = result.saved;
  free(state);
  resume(resume_ctx, saved, current, out);
}
/* Drop a continuation state without resuming it (qa_combat_progress.discard). */
static void discard_flat(void *ctx) { free(ctx); }
static void stage_discard(void *ctx) {
  stage_state *state = ctx;
  if (state->resume_discard != NULL) state->resume_discard(state->resume_ctx);
  free(state);
}
static void armor_stage(qa_combat_progress *out, qa_protection_channel channel,
                        qa_damage_request request, double amount, qa_armor_damage_flags flags,
                        qa_victim_armor_policy armor, void *resume_ctx,
                        void (*resume)(void *ctx, double saved, qa_current_combat_state current,
                                       qa_combat_progress *out),
                        void (*resume_discard)(void *ctx)) {
  const char *stage = channel == QA_PROTECTION_POWERED ? "power" : "regular";
  qa_armor_damage_flags stage_flags = flags;
  stage_flags.has_stage = true;
  stage_flags.stage = stage;
  stage_state *state = xcalloc(sizeof *state);
  state->request = request;
  state->amount = amount;
  state->flags = stage_flags;
  state->armor = armor;
  state->resume_ctx = resume_ctx;
  state->resume = resume;
  state->resume_discard = resume_discard;
  memset(out, 0, sizeof *out);
  out->kind = "armor-stage";
  out->channel = channel;
  out->request = request;
  out->input.request = request;
  out->input.direction = request.direction;
  out->input.point = request.point;
  out->input.normal = request.normal;
  out->input.amount = amount;
  out->input.flags = stage_flags;
  out->ctx = state;
  out->fallback = stage_fallback;
  out->resume_armor = stage_resume;
  out->discard = stage_discard;
}
static void continuation(qa_combat_progress *out, qa_damage_request request,
                         const qa_damage_mutation *mutations, size_t count, void *ctx,
                         void (*resume)(void *ctx, qa_current_combat_state current,
                                        qa_combat_progress *out),
                         void (*discard)(void *ctx)) {
  memset(out, 0, sizeof *out);
  out->kind = "source-continuation";
  out->request = request;
  out->mutations = copy_mutations(mutations, count);
  out->mutation_count = count;
  out->ctx = ctx;
  out->resume_source = resume;
  out->discard = discard;
}

/* ================= Q1 ================= */
typedef struct q1_options { qa_q1_combat_policy_options value; } q1_options;

static void q1_add_impulse(qa_damage_request request, qa_damage_mutation *mutations,
                           size_t *count, qa_vec3 direction, double amount,
                           const char *arithmetic) {
  if (amount != 0) {
    qa_damage_mutation *mutation = &mutations[(*count)++];
    memset(mutation, 0, sizeof *mutation);
    mutation->kind = QA_DAMAGE_MUTATION_IMPULSE;
    mutation->impulse = scale_vec(direction, amount, arithmetic);
    mutation->movement_provider = request.attack.movement_provider;
  }
}

typedef struct q1_health_state {
  q1_options *options;
  qa_damage_request request;
  qa_q1_combat_context context;
  double damage;
  double take;
} q1_health_state;
static void q1_health_resume(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  q1_health_state *state = ctx;
  q1_options *options = state->options;
  qa_q1_damage_source_effects *effects =
      options->value.has_source_effects ? &options->value.source_effects : NULL;
  const char *arithmetic = state->context.arithmetic;
  qa_combat_state storage;
  bool has_attacker = false;
  qa_combat_state_result latest_result = current.target(current.ctx);
  if (!latest_result.has_value) { none_progress(out, state->request); free(state); return; }
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  bool protected_health = latest_result.value.invulnerable &&
      !(effects != NULL && effects->protection_applies != NULL &&
        !effects->protection_applies(effects->ctx, state->request, latest_result.value, attacker));
  latest_result = current.target(current.ctx);
  if (!latest_result.has_value) { none_progress(out, state->request); free(state); return; }
  attacker = attacker_of(current, &storage, &has_attacker);
  bool team_blocked = !(state->context.has_base_team_health && !state->context.base_team_health) &&
                      state->context.teamplay == 1 && same_team(latest_result.value, attacker);
  if (protected_health || team_blocked) { none_progress(out, state->request); free(state); return; }
  if (effects != NULL && effects->before_health != NULL &&
      !effects->before_health(effects->ctx, state->request, state->damage,
                              latest_result.value, attacker)) {
    none_progress(out, state->request);
    free(state);
    return;
  }
  latest_result = current.target(current.ctx);
  if (!latest_result.has_value) { none_progress(out, state->request); free(state); return; }
  attacker = attacker_of(current, &storage, &has_attacker);
  double health_take = number_for(arithmetic,
      effects != NULL && effects->after_armor != NULL
          ? effects->after_armor(effects->ctx, state->request, state->take,
                                 latest_result.value, attacker)
          : state->take);
  latest_result = current.target(current.ctx);
  if (!latest_result.has_value) { none_progress(out, state->request); free(state); return; }
  double health = number_for(arithmetic, latest_result.value.health - health_take);
  if (health < -99) health = -99;
  qa_q1_lethal_health lethal;
  lethal.has_value = false;
  if (health <= 0 && effects != NULL && effects->lethal_health != NULL) {
    attacker = attacker_of(current, &storage, &has_attacker);
    lethal = effects->lethal_health(effects->ctx, state->request, health,
                                     latest_result.value, attacker);
  }
  latest_result = current.target(current.ctx);
  if (!latest_result.has_value) { none_progress(out, state->request); free(state); return; }
  qa_damage_mutation mutation;
  memset(&mutation, 0, sizeof mutation);
  mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
  mutation.health_before = latest_result.value.health;
  mutation.health_after = lethal.has_value ? lethal.health : health;
  const char *reaction = lethal.has_value ? lethal.reaction : (health <= 0 ? "death" : "pain");
  decision_progress(out, state->request, &mutation, 1, plain_result(health_take, reaction));
  free(state);
}
static void q1_after_armor(q1_options *options, qa_damage_request request,
                           qa_q1_combat_context context, double damage, qa_combat_state victim,
                           double take, qa_combat_progress *out) {
  qa_damage_mutation mutations[1];
  size_t count = 0;
  if (context.walk && !(victim.has_no_knockback && victim.no_knockback) &&
      context.has_momentum_direction) {
    q1_add_impulse(request, mutations, &count, context.momentum_direction,
                   number_for(context.arithmetic, damage * 8), context.arithmetic);
  }
  q1_health_state *state = xcalloc(sizeof *state);
  state->options = options;
  state->request = request;
  state->context = context;
  state->damage = damage;
  state->take = take;
  continuation(out, request, mutations, count, state, q1_health_resume, discard_flat);
}

typedef struct q1_regular_state {
  q1_options *options;
  qa_damage_request request;
  qa_q1_combat_context context;
  double damage, power_saved;
} q1_regular_state;
static void q1_regular_resume(void *ctx, double saved, qa_current_combat_state current,
                              qa_combat_progress *out) {
  q1_regular_state *state = ctx;
  qa_combat_state_result latest = current.target(current.ctx);
  if (!latest.has_value) { none_progress(out, state->request); free(state); return; }
  double take = ceil(number_for(state->context.arithmetic,
                                state->damage - (state->power_saved + saved)));
  q1_after_armor(state->options, state->request, state->context, state->damage,
                 latest.value, take, out);
  free(state);
}
typedef struct q1_power_state {
  q1_options *options;
  qa_damage_request request;
  qa_q1_combat_context context;
  double damage;
} q1_power_state;
static void q1_power_resume(void *ctx, double power_saved, qa_current_combat_state current,
                            qa_combat_progress *out) {
  q1_power_state *state = ctx;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  q1_regular_state *next = xcalloc(sizeof *next);
  next->options = state->options;
  next->request = state->request;
  next->context = state->context;
  next->damage = state->damage;
  next->power_saved = power_saved;
  qa_attack_damage_flags flags = qa_attack_damage_flags_of(state->request);
  armor_stage(out, QA_PROTECTION_REGULAR, state->request, state->damage - power_saved,
              flags.flags, state->options->value.armor, next, q1_regular_resume, discard_flat);
  free(state);
}
typedef struct q1_gate_state {
  q1_options *options;
  qa_damage_request request;
  qa_q1_combat_context context;
  double damage;
} q1_gate_state;
static void q1_gate_resume(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  q1_gate_state *state = ctx;
  qa_q1_damage_source_effects *effects =
      state->options->value.has_source_effects ? &state->options->value.source_effects : NULL;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  bool allowed = !(effects != NULL && effects->armor_allowed != NULL &&
                   !effects->armor_allowed(effects->ctx, state->request, state->damage,
                                           victim.value, attacker));
  qa_combat_state_result latest = current.target(current.ctx);
  if (!latest.has_value) { none_progress(out, state->request); free(state); return; }
  if (!allowed) {
    double take = ceil(number_for(state->context.arithmetic, state->damage));
    q1_after_armor(state->options, state->request, state->context, state->damage,
                   latest.value, take, out);
    free(state);
    return;
  }
  q1_power_state *next = xcalloc(sizeof *next);
  next->options = state->options;
  next->request = state->request;
  next->context = state->context;
  next->damage = state->damage;
  qa_attack_damage_flags flags = qa_attack_damage_flags_of(state->request);
  armor_stage(out, QA_PROTECTION_POWERED, state->request, state->damage, flags.flags,
              state->options->value.armor, next, q1_power_resume, discard_flat);
  free(state);
}

static qa_damage_preparation q1_prepare(void *ctx, qa_damage_request request,
                                        qa_combat_state target, const qa_combat_state *attacker) {
  q1_options *options = ctx;
  qa_damage_preparation result;
  if (!target.can_take_damage) { result.is_continue = false; result.amount = 0; return result; }
  qa_q1_combat_context context = options->value.context(options->value.ctx, request, target, attacker);
  qa_q1_damage_source_effects *effects =
      options->value.has_source_effects ? &options->value.source_effects : NULL;
  bool has_before = false;
  qa_damage_preparation before;
  if (effects != NULL && effects->before_quad != NULL)
    has_before = effects->before_quad(effects->ctx, request,
                                      number_for(context.arithmetic, request.amount),
                                      target, attacker, &before);
  if (has_before && !before.is_continue) return before;
  bool quad = !request.attack.has_damage_powerup_owner &&
              (effects == NULL || effects->before_quad == NULL
                   ? context.quad
                   : options->value.context(options->value.ctx, request, target, attacker).quad);
  double base = has_before ? before.amount : request.amount;
  double damage = number_for(context.arithmetic,
      number_for(context.arithmetic, base) * (quad ? 4 : 1));
  bool has_after = false;
  qa_damage_preparation after;
  if (effects != NULL && effects->after_quad != NULL)
    has_after = effects->after_quad(effects->ctx, request, damage, target, attacker, &after);
  if (has_after && !after.is_continue) return after;
  result.is_continue = true;
  result.amount = number_for(context.arithmetic, has_after ? after.amount : damage);
  return result;
}
static void q1_decide(void *ctx, qa_damage_request request, qa_combat_state target,
                      const qa_combat_state *attacker, bool has_prepared, double prepared_amount,
                      qa_combat_progress *out) {
  q1_options *options = ctx;
  if (!target.can_take_damage) { none_progress(out, request); return; }
  qa_q1_combat_context context = options->value.context(options->value.ctx, request, target, attacker);
  double damage = has_prepared
      ? prepared_amount
      : number_for(context.arithmetic,
            number_for(context.arithmetic, request.amount) *
                (context.quad && !request.attack.has_damage_powerup_owner ? 4 : 1));
  q1_gate_state *state = xcalloc(sizeof *state);
  state->options = options;
  state->request = request;
  state->context = context;
  state->damage = damage;
  continuation(out, request, NULL, 0, state, q1_gate_resume, discard_flat);
}
qa_combat_policy qa_create_q1_combat_policy(qa_q1_combat_policy_options options) {
  q1_options *state = xcalloc(sizeof *state);
  state->value = options;
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = state;
  policy.id = options.id;
  policy.prepare = q1_prepare;
  policy.decide = q1_decide;
  return policy;
}

/* ================= Q2 ================= */
typedef struct q2_options { qa_q2_combat_policy_options value; } q2_options;

typedef struct q2_final_state {
  q2_options *options;
  qa_damage_request request;
  qa_q2_combat_context context;
  qa_attack_damage_flags flags;
  double damage, power_saved, protection_saved, knockback, after_power_take;
} q2_final_state;
static void q2_after_regular(double regular_saved, q2_final_state *state,
                             qa_current_combat_state current, qa_combat_progress *out) {
  qa_q2_damage_source_effects *effects =
      state->options->value.has_source_effects ? &state->options->value.source_effects : NULL;
  double after_regular_take = state->after_power_take - regular_saved;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); return; }
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  double take = trunc(effects != NULL && effects->after_armor != NULL
                          ? effects->after_armor(effects->ctx, state->request,
                                                 after_regular_take, victim.value, attacker)
                          : after_regular_take);
  victim = current.target(current.ctx);
  if (!victim.has_value || (!state->flags.no_protection && state->context.reject_team_damage)) {
    none_progress(out, state->request);
    return;
  }
  if (state->flags.destroy_armor && !victim.value.invulnerable && !state->flags.no_protection)
    take = state->damage;
  qa_combat_result feedback;
  memset(&feedback, 0, sizeof feedback);
  feedback.has_feedback = true;
  feedback.feedback_kind = "q2";
  feedback.feedback_power_armor = state->power_saved;
  feedback.feedback_armor = regular_saved + state->protection_saved;
  feedback.feedback_blood = take;
  feedback.feedback_knockback = state->knockback;
  if (take == 0) {
    feedback.reaction = "none";
    decision_progress(out, state->request, NULL, 0, feedback);
    return;
  }
  double health = trunc(victim.value.health - take);
  if (health < -999) health = -999;
  qa_damage_mutation mutation;
  memset(&mutation, 0, sizeof mutation);
  mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
  mutation.health_before = victim.value.health;
  mutation.health_after = health;
  feedback.applied_damage = take;
  feedback.reaction = health <= 0 ? "death" : state->context.suppress_pain ? "none" : "pain";
  decision_progress(out, state->request, &mutation, 1, feedback);
}
static void q2_regular_resume(void *ctx, double saved, qa_current_combat_state current,
                              qa_combat_progress *out) {
  q2_final_state *state = ctx;
  q2_after_regular(saved, state, current, out);
  free(state);
}
typedef struct q2_zero_state {
  q2_final_state *final;
} q2_zero_state;
static void q2_regular_zero(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  q2_zero_state *state = ctx;
  q2_after_regular(0, state->final, current, out);
  free(state->final);
  free(state);
}
static void q2_zero_discard(void *ctx) {
  q2_zero_state *state = ctx;
  free(state->final);
  free(state);
}

typedef struct q2_after_power_state {
  q2_options *options;
  qa_damage_request request;
  qa_q2_combat_context context;
  qa_attack_damage_flags flags;
  double damage, amount, protection_saved, knockback;
} q2_after_power_state;
static void q2_after_power(double power_saved, q2_after_power_state *state,
                           qa_current_combat_state current, qa_combat_progress *out) {
  qa_q2_damage_source_effects *effects =
      state->options->value.has_source_effects ? &state->options->value.source_effects : NULL;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); return; }
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  double after_power_take = trunc(effects != NULL && effects->after_power_armor != NULL
      ? effects->after_power_armor(effects->ctx, state->request, state->amount - power_saved,
                                   victim.value, attacker)
      : state->amount - power_saved);
  victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); return; }
  attacker = attacker_of(current, &storage, &has_attacker);
  bool allowed = !(effects != NULL && effects->armor_allowed != NULL &&
                   !effects->armor_allowed(effects->ctx, state->request, victim.value, attacker));
  victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); return; }
  q2_final_state *final = xcalloc(sizeof *final);
  final->options = state->options;
  final->request = state->request;
  final->context = state->context;
  final->flags = state->flags;
  final->damage = state->damage;
  final->power_saved = power_saved;
  final->protection_saved = state->protection_saved;
  final->knockback = state->knockback;
  final->after_power_take = after_power_take;
  if (allowed) {
    armor_stage(out, QA_PROTECTION_REGULAR, state->request, after_power_take,
                state->flags.flags, state->options->value.armor, final, q2_regular_resume, discard_flat);
  } else {
    q2_zero_state *zero = xcalloc(sizeof *zero);
    zero->final = final;
    continuation(out, state->request, NULL, 0, zero, q2_regular_zero, q2_zero_discard);
  }
}
static void q2_power_resume(void *ctx, double saved, qa_current_combat_state current,
                            qa_combat_progress *out);
typedef struct q2_power_wrap {
  q2_after_power_state *next_state;
  double saved;
} q2_power_wrap;
static void q2_power_wrap_resume(void *ctx, qa_current_combat_state current,
                                 qa_combat_progress *out) {
  q2_power_wrap *wrap = ctx;
  q2_after_power(wrap->saved, wrap->next_state, current, out);
  free(wrap->next_state);
  free(wrap);
}
static void q2_power_wrap_discard(void *ctx) {
  q2_power_wrap *wrap = ctx;
  free(wrap->next_state);
  free(wrap);
}
static void q2_power_resume(void *ctx, double saved, qa_current_combat_state current,
                            qa_combat_progress *out) {
  (void)current;
  q2_after_power_state *state = ctx;
  q2_power_wrap *wrap = xcalloc(sizeof *wrap);
  wrap->next_state = state;
  wrap->saved = saved;
  continuation(out, state->request, NULL, 0, wrap, q2_power_wrap_resume, q2_power_wrap_discard);
}
typedef struct q2_power_zero {
  q2_after_power_state *next_state;
} q2_power_zero;
static void q2_power_zero_resume(void *ctx, qa_current_combat_state current,
                                 qa_combat_progress *out) {
  q2_power_zero *zero = ctx;
  q2_after_power(0, zero->next_state, current, out);
  free(zero->next_state);
  free(zero);
}
static void q2_power_zero_discard(void *ctx) {
  q2_power_zero *zero = ctx;
  free(zero->next_state);
  free(zero);
}

typedef struct q2_step2_state {
  q2_options *options;
  qa_damage_request request;
  qa_q2_combat_context context;
  qa_attack_damage_flags flags;
  double damage, knockback;
} q2_step2_state;
static void q2_step2_resume(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  q2_step2_state *state = ctx;
  qa_q2_damage_source_effects *effects =
      state->options->value.has_source_effects ? &state->options->value.source_effects : NULL;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  double protection_saved = victim.value.invulnerable && !state->flags.no_protection
                                ? state->damage : 0;
  double amount = state->damage - protection_saved;
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  bool allowed = !(effects != NULL && effects->power_armor_allowed != NULL &&
                   !effects->power_armor_allowed(effects->ctx, state->request, victim.value,
                                                 attacker));
  victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  q2_after_power_state *next = xcalloc(sizeof *next);
  next->options = state->options;
  next->request = state->request;
  next->context = state->context;
  next->flags = state->flags;
  next->damage = state->damage;
  next->amount = amount;
  next->protection_saved = protection_saved;
  next->knockback = state->knockback;
  if (!allowed) {
    q2_power_zero *zero = xcalloc(sizeof *zero);
    zero->next_state = next;
    continuation(out, state->request, NULL, 0, zero, q2_power_zero_resume, q2_power_zero_discard);
  } else {
    armor_stage(out, QA_PROTECTION_POWERED, state->request, amount, state->flags.flags,
                state->options->value.armor, next, q2_power_resume, discard_flat);
  }
  free(state);
}

typedef struct q2_step1_state {
  q2_options *options;
  qa_damage_request request;
  qa_q2_combat_context context;
  qa_attack_damage_flags flags;
  double base_damage;
} q2_step1_state;
static void q2_step1_resume(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  q2_step1_state *state = ctx;
  qa_q2_damage_source_effects *effects =
      state->options->value.has_source_effects ? &state->options->value.source_effects : NULL;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  double damage = trunc(effects != NULL && effects->before_momentum != NULL
      ? effects->before_momentum(effects->ctx, state->request, state->base_damage,
                                 victim.value, attacker)
      : state->base_damage);
  victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  qa_damage_mutation mutations[1];
  size_t count = 0;
  double knockback = state->context.no_knockback ||
                             (victim.value.has_no_knockback && victim.value.no_knockback)
                         ? 0
                         : trunc(state->request.knockback);
  if (!state->flags.no_knockback && state->context.movable) {
    double coefficient = state->context.player && self_damage(state->request) ? 1600 : 500;
    double divisor = qa_js_max(50, victim.value.mass);
    double amount = number_for(state->context.arithmetic,
        number_for(state->context.arithmetic, coefficient * knockback) / divisor);
    if (amount != 0) {
      memset(&mutations[0], 0, sizeof mutations[0]);
      mutations[0].kind = QA_DAMAGE_MUTATION_IMPULSE;
      mutations[0].impulse = scale_vec(state->request.direction, amount, state->context.arithmetic);
      mutations[0].movement_provider = state->request.attack.movement_provider;
      count = 1;
    }
  }
  q2_step2_state *next = xcalloc(sizeof *next);
  next->options = state->options;
  next->request = state->request;
  next->context = state->context;
  next->flags = state->flags;
  next->damage = damage;
  next->knockback = knockback;
  continuation(out, state->request, mutations, count, next, q2_step2_resume, discard_flat);
  free(state);
}

static void q2_decide(void *ctx, qa_damage_request request, qa_combat_state target,
                      const qa_combat_state *attacker, bool has_prepared, double prepared_amount,
                      qa_combat_progress *out) {
  (void)has_prepared;
  (void)prepared_amount;
  q2_options *options = ctx;
  if (!target.can_take_damage) { none_progress(out, request); return; }
  qa_q2_combat_context context = options->value.context(options->value.ctx, request, target, attacker);
  qa_attack_damage_flags flags = qa_attack_damage_flags_of(request);
  double damage = trunc(request.amount);
  if (!self_damage(request) && context.team_damage_enabled && same_team(target, attacker) &&
      !context.friendly_fire && !context.nuke) damage = 0;
  /* Math.max: a NaN amount stays NaN. */
  if (context.easy_skill && !context.deathmatch && context.player)
    damage = qa_js_max(1, trunc(damage * 0.5));
  if (context.defender_sphere && context.player)
    damage = qa_js_max(1, trunc(damage * 0.5));
  if (!str_eq(request.delivery, "radius") && context.monster && context.attacker_player &&
      !context.has_enemy && target.health > 0) damage = trunc(damage * 2);
  q2_step1_state *state = xcalloc(sizeof *state);
  state->options = options;
  state->request = request;
  state->context = context;
  state->flags = flags;
  state->base_damage = damage;
  continuation(out, request, NULL, 0, state, q2_step1_resume, discard_flat);
}
static const char *q2_after_health(void *ctx, qa_damage_decision decision,
                                   qa_current_combat_state current) {
  q2_options *options = ctx;
  if (!options->value.has_source_effects || options->value.source_effects.after_health == NULL)
    return decision.reaction;
  options->value.source_effects.after_health(options->value.source_effects.ctx, decision, current);
  qa_combat_state_result target = current.target(current.ctx);
  if (!target.has_value) return "none";
  if (target.value.health <= 0) return "death";
  qa_combat_state storage;
  bool has_attacker = false;
  const qa_combat_state *attacker = attacker_of(current, &storage, &has_attacker);
  return options->value.context(options->value.ctx, decision.request, target.value, attacker)
             .suppress_pain
             ? "none"
             : "pain";
}
qa_combat_policy qa_create_q2_combat_policy(qa_q2_combat_policy_options options) {
  q2_options *state = xcalloc(sizeof *state);
  state->value = options;
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = state;
  policy.id = options.id;
  policy.decide = q2_decide;
  policy.after_health = q2_after_health;
  return policy;
}

/* ================= Q3 ================= */
typedef struct q3_options { qa_q3_combat_policy_options value; } q3_options;
typedef struct q3_regular_state {
  qa_damage_request request;
  double amount, power_saved, knockback;
  bool battlesuit;
} q3_regular_state;
static void q3_regular_resume(void *ctx, double saved, qa_current_combat_state current,
                              qa_combat_progress *out) {
  q3_regular_state *state = ctx;
  qa_combat_state_result latest = current.target(current.ctx);
  if (!latest.has_value) { none_progress(out, state->request); free(state); return; }
  double take = (double)qa_js_to_int32(state->amount - (state->power_saved + saved));
  qa_combat_result feedback;
  memset(&feedback, 0, sizeof feedback);
  feedback.has_feedback = true;
  feedback.feedback_kind = "q3";
  feedback.feedback_knockback = state->knockback;
  feedback.feedback_battlesuit = state->battlesuit;
  if (take == 0) {
    feedback.reaction = "none";
    decision_progress(out, state->request, NULL, 0, feedback);
    free(state);
    return;
  }
  double health = (double)qa_js_to_int32(latest.value.health - take);
  if (health < -999) health = -999;
  qa_damage_mutation mutation;
  memset(&mutation, 0, sizeof mutation);
  mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
  mutation.health_before = latest.value.health;
  mutation.health_after = health;
  feedback.applied_damage = take;
  feedback.reaction = health <= 0 ? "death" : "pain";
  decision_progress(out, state->request, &mutation, 1, feedback);
  free(state);
}
typedef struct q3_power_state {
  q3_options *options;
  qa_damage_request request;
  qa_attack_damage_flags flags;
  double amount, knockback;
  bool battlesuit;
} q3_power_state;
static void q3_power_resume(void *ctx, double power_saved, qa_current_combat_state current,
                            qa_combat_progress *out) {
  q3_power_state *state = ctx;
  qa_combat_state_result victim = current.target(current.ctx);
  if (!victim.has_value) { none_progress(out, state->request); free(state); return; }
  q3_regular_state *next = xcalloc(sizeof *next);
  next->request = state->request;
  next->amount = state->amount;
  next->power_saved = power_saved;
  next->knockback = state->knockback;
  next->battlesuit = state->battlesuit;
  armor_stage(out, QA_PROTECTION_REGULAR, state->request, state->amount - power_saved,
              state->flags.flags, state->options->value.armor, next, q3_regular_resume, discard_flat);
  free(state);
}
typedef struct q3_stage_state {
  q3_options *options;
  qa_damage_request request;
  qa_attack_damage_flags flags;
  double amount, knockback;
  bool battlesuit;
} q3_stage_state;
static void q3_stage_resume(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  (void)current;
  q3_stage_state *state = ctx;
  q3_power_state *next = xcalloc(sizeof *next);
  next->options = state->options;
  next->request = state->request;
  next->flags = state->flags;
  next->amount = state->amount;
  next->knockback = state->knockback;
  next->battlesuit = state->battlesuit;
  armor_stage(out, QA_PROTECTION_POWERED, state->request, state->amount, state->flags.flags,
              state->options->value.armor, next, q3_power_resume, discard_flat);
  free(state);
}
static void q3_decide(void *ctx, qa_damage_request request, qa_combat_state target,
                      const qa_combat_state *attacker, bool has_prepared, double prepared_amount,
                      qa_combat_progress *out) {
  (void)has_prepared;
  (void)prepared_amount;
  q3_options *options = ctx;
  if (!target.can_take_damage) { none_progress(out, request); return; }
  qa_q3_combat_context context = options->value.context(options->value.ctx, request, target, attacker);
  if (context.intermission || context.noclip ||
      (context.missionpack_invulnerability && !context.juiced)) {
    none_progress(out, request);
    return;
  }
  qa_attack_damage_flags flags = qa_attack_damage_flags_of(request);
  double damage = trunc(request.amount);
  if (context.attacker_player && !self_damage(request)) {
    double maximum = context.attacker_guard ? trunc(context.attacker_max_health / 2)
                                            : context.attacker_max_health;
    /* TS Math.trunc(Math.imul(damage, maximum) / 100). */
    int32_t product = (int32_t)((uint32_t)qa_js_to_int32(damage) * (uint32_t)qa_js_to_int32(maximum));
    damage = trunc((double)product / 100);
  }
  qa_damage_mutation mutations[1];
  size_t count = 0;
  double knockback = context.no_knockback ||
                             (target.has_no_knockback && target.no_knockback) ||
                             flags.no_knockback
                         ? 0
                         : qa_js_min(damage, 200);
  bool battlesuit = false;
  if (context.player && !context.no_knockback &&
      !(target.has_no_knockback && target.no_knockback) && !flags.no_knockback) {
    double amount = fround_of(fround_of(fround_of(context.knockback_scale) *
                                        fround_of(knockback)) / 200);
    if (amount != 0) {
      memset(&mutations[0], 0, sizeof mutations[0]);
      mutations[0].kind = QA_DAMAGE_MUTATION_IMPULSE;
      mutations[0].impulse = scale_vec(request.direction, amount, "binary32");
      mutations[0].movement_provider = request.attack.movement_provider;
      count = 1;
    }
  }
  qa_combat_result early;
  memset(&early, 0, sizeof early);
  early.reaction = "none";
  early.has_feedback = true;
  early.feedback_kind = "q3";
  early.feedback_knockback = knockback;
  if (!flags.no_protection) {
    bool check_team = str_eq(context.product, "baseq3") ||
                      (!context.juiced && !flags.no_team_protection);
    if ((check_team && !self_damage(request) && same_team(target, attacker) &&
         !context.friendly_fire) || context.proximity_protected || target.invulnerable) {
      decision_progress(out, request, mutations, count, early);
      return;
    }
  }
  if (context.battlesuit) {
    battlesuit = true;
    early.feedback_battlesuit = true;
    if (str_eq(request.delivery, "radius") || context.falling) {
      decision_progress(out, request, mutations, count, early);
      return;
    }
    damage = trunc(damage * 0.5);
  }
  if (self_damage(request)) damage = trunc(damage * 0.5);
  if (damage < 1) damage = 1;
  q3_stage_state *state = xcalloc(sizeof *state);
  state->options = options;
  state->request = request;
  state->flags = flags;
  state->amount = damage;
  state->knockback = knockback;
  state->battlesuit = battlesuit;
  continuation(out, request, mutations, count, state, q3_stage_resume, discard_flat);
}
qa_combat_policy qa_create_q3_combat_policy(qa_q3_combat_policy_options options) {
  q3_options *state = xcalloc(sizeof *state);
  state->value = options;
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = state;
  policy.id = options.id;
  policy.decide = q3_decide;
  return policy;
}

typedef struct native_armor_state {
  qa_victim_armor_context (*context)(void *ctx, qa_damage_request request);
  void *ctx;
} native_armor_state;
static qa_armor_result native_armor_call(void *ctx, qa_damage_request request,
                                         qa_armor_state armor, double damage,
                                         qa_armor_damage_flags flags) {
  native_armor_state *state = ctx;
  return qa_absorb_native_armor(armor, damage, flags, state->context(state->ctx, request));
}
void qa_combat_policy_free(qa_combat_policy policy) { free(policy.ctx); }
void qa_victim_armor_policy_free(qa_victim_armor_policy policy) { free(policy.ctx); }

qa_victim_armor_policy qa_native_victim_armor(
    qa_victim_armor_context (*context)(void *ctx, qa_damage_request request), void *ctx) {
  native_armor_state *state = xcalloc(sizeof *state);
  state->context = context;
  state->ctx = ctx;
  qa_victim_armor_policy policy;
  policy.ctx = state;
  policy.call = native_armor_call;
  return policy;
}
