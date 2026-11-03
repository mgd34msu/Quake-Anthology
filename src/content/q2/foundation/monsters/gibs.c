/* Port of src/content/q2/foundation/monsters/gibs.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/gibs.h"

#include <math.h>

#include "monsters_internal.h"
#include "qa/foundation/js.h"

static void free_gib(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) { (void)ctx; g->remove(g->ctx, e); }
static void animate_meat(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)ctx;
  e->frame++;
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, e->frame == 10 ? 8 + q2m_rnd(g) * 10 : 0.1,
              (qa_q2_think){NULL, e->frame == 10 ? free_gib : animate_meat});
}
static void gib_die(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  const qa_attack_cause *cause = e->last_attack ? &e->last_attack->cause : NULL;
  if (!q2m_rr(g) || (cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 20)) g->remove(g->ctx, e);
}
static void upright_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  (void)ctx;
  if (contact.plane && contact.plane->normal.z > 0.7) {
    qa_vec3 current = q2m_body(g, e).angles;
    qa_q2_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_angles = true;
    c.angles = q2m_v3(qa_js_max(-5, qa_js_min(5, current.x)), current.y, qa_js_max(-5, qa_js_min(5, current.z)));
    q2m_move(g, e, c, false, false);
  }
}
static void organic_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  (void)ctx;
  if (q2m_body(g, e).ground == NULL) return;
  e->touch = (qa_q2_touch){NULL, NULL};
  if (contact.plane) {
    q2m_sound(g, e, "misc/fhit3.wav", true, 2, false, 0, false, 0);
    qa_vec3 angles = qa_vector_to_angles_with_rule(
        qa_math_angle_vectors_rule(qa_vector_to_angles_with_rule(contact.plane->normal, QA_VECTOR_TO_ANGLES_NATIVE_AI),
                                   QA_ANGLE_RULE_NATIVE).right,
        QA_VECTOR_TO_ANGLES_NATIVE_AI);
    qa_q2_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_angles = true; c.angles = angles;
    q2m_move(g, e, c, false, false);
    if (strcmp(e->model, "models/objects/gibs/sm_meat/tris.md2") == 0) {
      e->frame++; g->show(g->ctx, e);
      g->schedule(g->ctx, e, 0.1, (qa_q2_think){NULL, animate_meat});
    }
  }
}
static const qa_q2_named_think gib_think_defs[] = {
  {"gib_free", {NULL, free_gib}}, {"gib_think", {NULL, animate_meat}},
};
static const qa_q2_named_die gib_die_defs[] = {{"gib_die", {NULL, gib_die}}};
static const qa_q2_named_touch gib_touch_defs[] = {
  {"gib_touch", {NULL, organic_touch}}, {"gib_touch_upright", {NULL, upright_touch}},
};
qa_q2_callback_definitions qa_q2m_gib_callbacks(void) {
  qa_q2_callback_definitions d;
  memset(&d, 0, sizeof d);
  d.think = gib_think_defs; d.think_count = 2;
  d.die = gib_die_defs; d.die_count = 1;
  d.touch = gib_touch_defs; d.touch_count = 2;
  return d;
}

qa_q2_entity *qa_q2m_throw_gib(qa_q2_entity *self, qa_q2_game_services *game, const char *model, double damage, qa_q2m_gib_options options) {
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2m_gib_callbacks());
  bool rr = q2m_rr(game);
  qa_body_state body = q2m_body(game, self);
  qa_q2_entity *gib = options.head ? self : game->create(game->ctx, "gib", NULL, 0);
  qa_vec3 half = qa_scale3_double(q2m_v3(body.bounds.max.x - body.bounds.min.x, body.bounds.max.y - body.bounds.min.y,
                                         body.bounds.max.z - body.bounds.min.z), 0.5);
  qa_vec3 center = qa_add3_double(body.origin, qa_add3_double(body.bounds.min,
      qa_add3_double(half, rr ? q2m_zero() : q2m_v3(-1, -1, -1))));
  qa_vec3 origin = body.origin;
  if (!options.head || rr) {
    int tries = 0;
    do {
      double cx = q2m_rnd(game) * 2 - 1, cy = q2m_rnd(game) * 2 - 1, cz = q2m_rnd(game) * 2 - 1;
      origin = qa_add3_double(center, q2m_v3(cx * half.x, cy * half.y, cz * half.z));
      tries++;
    } while (rr && tries < 3 && (qa_js_to_uint32(game->host->point_contents(game->host->ctx, origin)) & 3u) != 0);
    if (rr && (qa_js_to_uint32(game->host->point_contents(game->host->ctx, origin)) & 3u) != 0 && !options.head) {
      game->remove(game->ctx, gib);
      return NULL;
    }
  }
  double ix = 100 * (q2m_rnd(game) * 2 - 1), iy = 100 * (q2m_rnd(game) * 2 - 1);
  qa_vec3 impulse = qa_scale3_double(q2m_v3(ix, iy, 200 + 100 * q2m_rnd(game)), damage < 50 ? 0.7 : 1.2);
  qa_vec3 velocity = qa_add3_double(body.velocity, qa_scale3_double(impulse, options.metallic ? 1 : 0.5));
  qa_vec3 clipped = q2m_v3(qa_js_max(-300, qa_js_min(300, velocity.x)), qa_js_max(-300, qa_js_min(300, velocity.y)),
                           qa_js_max(200, qa_js_min(500, velocity.z)));
  gib->model = model;
  if (options.head) {
    /* ThrowHead drops the attached models and the looping sound of the dying actor. */
    gib->model2 = ""; gib->model3 = ""; gib->model4 = "";
    qa_q2_presentation_event stop;
    memset(&stop, 0, sizeof stop);
    stop.kind = "sound"; stop.has_actor = true; stop.actor = gib->actor.id; stop.origin = body.origin; stop.path = "";
    stop.channel = 0; stop.volume = 0; stop.attenuation = 1; stop.reliable = false; stop.loop = "stop";
    game->host->emit(game->host->ctx, stop);
  }
  if (rr) gib->classname = "gib";
  gib->frame = 0; gib->old_frame = -1;
  gib->skin = options.skinned ? self->skin : 0;
  gib->effects = rr ? 2 : (double)(qa_js_to_int32((qa_js_to_uint32(gib->effects) | 2u) & ~0x4000u));
  if (rr) gib->render_flags = (1 << 24) | (1 << 13) | (1 << 15);
  gib->flags = (double)(qa_js_to_int32(qa_js_to_uint32(gib->flags) | 2048u | (rr ? (1u << 20) | (1u << 28) : 0u)));
  gib->server_flags = (double)(qa_js_to_int32((qa_js_to_uint32(gib->server_flags) & ~4u) | (rr ? 2u : 0u)));
  gib->clip_mask = 3;
  gib->scale = rr ? self->scale : 1;
  if (options.head && !rr) {
    gib->angular_velocity.y = (q2m_rnd(game) * 2 - 1) * 600;
  } else {
    double ax = q2m_rnd(game) * 600, ay = q2m_rnd(game) * 600, az = q2m_rnd(game) * 600;
    gib->angular_velocity = q2m_v3(ax, ay, az);
  }
  qa_vec3 angles;
  if (rr) {
    double ax = q2m_rnd(game) * 359, ay = q2m_rnd(game) * 359, az = q2m_rnd(game) * 359;
    angles = q2m_v3(ax, ay, az);
  } else angles = q2m_body(game, gib).angles;
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = origin;
  c.has_velocity = true; c.velocity = clipped;
  c.has_angles = true; c.angles = angles;
  c.has_bounds = true; c.bounds = (qa_bounds){q2m_zero(), q2m_zero()};
  c.has_ground = true; c.ground_null = true;
  q2m_move(game, gib, c, true, false);
  if (!qa_gameplay_authority_read(game->host->combat, gib->actor.id).has_value) {
    qa_combat_state state;
    memset(&state, 0, sizeof state);
    state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    state.armor.powered.kind = "none";
    state.can_take_damage = true;
    qa_gameplay_authority_create_state(game->host->combat, gib->actor, state, NULL, NULL);
  } else {
    qa_combat_trait_changes traits;
    memset(&traits, 0, sizeof traits);
    traits.has_can_take_damage = true; traits.can_take_damage = true;
    qa_gameplay_authority_set_traits(game->host->combat, gib->actor, traits);
  }
  gib->die = (qa_q2_die){NULL, gib_die};
  gib->pain = (qa_q2_pain){NULL, NULL};
  gib->use = (qa_q2_use){NULL, NULL};
  gib->touch = rr ? (options.upright ? (qa_q2_touch){NULL, upright_touch} : (qa_q2_touch){NULL, NULL})
                  : (options.metallic ? (qa_q2_touch){NULL, NULL} : (qa_q2_touch){NULL, organic_touch});
  game->solid(game->ctx, gib, "none");
  game->motion(game->ctx, gib, options.metallic ? "bounce" : "toss");
  game->schedule(game->ctx, gib, 10 + q2m_rnd(game) * 10, (qa_q2_think){NULL, free_gib});
  game->link(game->ctx, gib);
  game->show(game->ctx, gib);
  return gib;
}
qa_q2_entity *qa_q2m_throw_head(qa_q2_entity *self, qa_q2_game_services *game, const char *model, double damage) {
  qa_q2m_gib_options options = {true, false, false, false};
  return qa_q2m_throw_gib(self, game, model, damage, options);
}
