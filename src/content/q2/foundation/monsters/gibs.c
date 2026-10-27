/* Port of src/content/q2/foundation/monsters/gibs.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/gibs.h"

#include <math.h>

#include "monsters_internal.h"

static void free_gib(void *ctx, qa_q2f_entity *e, qa_q2f_game *g) { (void)ctx; g->remove(g->ctx, e); }
static void animate_meat(void *ctx, qa_q2f_entity *e, qa_q2f_game *g) {
  (void)ctx;
  e->frame++;
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, e->frame == 10 ? 8 + q2m_rnd(g) * 10 : 0.1,
              (qa_q2f_think){NULL, e->frame == 10 ? free_gib : animate_meat});
}
static void gib_die(void *ctx, qa_q2f_entity *e, qa_q2f_game *g, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  const qa_attack_cause *cause = e->last_attack ? &e->last_attack->cause : NULL;
  if (!q2m_rr(g) || (cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 20)) g->remove(g->ctx, e);
}
static void upright_touch(void *ctx, qa_q2f_entity *e, qa_q2f_game *g, qa_touch_contact contact) {
  (void)ctx;
  if (contact.plane && contact.plane->normal.z > 0.7) {
    qa_vec3 current = q2m_body(g, e).angles;
    qa_q2f_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_angles = true;
    c.angles = q2m_v3(fmax(-5, fmin(5, current.x)), current.y, fmax(-5, fmin(5, current.z)));
    q2m_move(g, e, c, false, false);
  }
}
static void organic_touch(void *ctx, qa_q2f_entity *e, qa_q2f_game *g, qa_touch_contact contact) {
  (void)ctx;
  if (q2m_body(g, e).ground == NULL) return;
  e->touch = (qa_q2f_touch){NULL, NULL};
  if (contact.plane) {
    q2m_sound(g, e, "misc/fhit3.wav", true, 2, false, 0, false, 0);
    qa_vec3 angles = qa_vector_to_angles_with_rule(
        qa_math_angle_vectors_rule(qa_vector_to_angles_with_rule(contact.plane->normal, QA_VECTOR_TO_ANGLES_NATIVE_AI),
                                   QA_ANGLE_RULE_NATIVE).right,
        QA_VECTOR_TO_ANGLES_NATIVE_AI);
    qa_q2f_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_angles = true; c.angles = angles;
    q2m_move(g, e, c, false, false);
    if (strcmp(e->model, "models/objects/gibs/sm_meat/tris.md2") == 0) {
      e->frame++; g->show(g->ctx, e);
      g->schedule(g->ctx, e, 0.1, (qa_q2f_think){NULL, animate_meat});
    }
  }
}
static const qa_q2f_named_think gib_think_defs[] = {
  {"gib_free", {NULL, free_gib}}, {"gib_think", {NULL, animate_meat}},
};
static const qa_q2f_named_die gib_die_defs[] = {{"gib_die", {NULL, gib_die}}};
static const qa_q2f_named_touch gib_touch_defs[] = {
  {"gib_touch", {NULL, organic_touch}}, {"gib_touch_upright", {NULL, upright_touch}},
};
qa_q2f_callback_definitions qa_q2m_gib_callbacks(void) {
  qa_q2f_callback_definitions d;
  memset(&d, 0, sizeof d);
  d.think = gib_think_defs; d.think_count = 2;
  d.die = gib_die_defs; d.die_count = 1;
  d.touch = gib_touch_defs; d.touch_count = 2;
  return d;
}

qa_q2f_entity *qa_q2m_throw_gib(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage, qa_q2m_gib_options options) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2m_gib_callbacks());
  bool rr = q2m_rr(game);
  qa_body_state body = q2m_body(game, self);
  qa_q2f_entity *gib = options.head ? self : game->create(game->ctx, "gib", NULL);
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
    } while (rr && tries < 3 && ((uint32_t)game->host->point_contents(game->host->ctx, origin) & 3u) != 0);
    if (rr && ((uint32_t)game->host->point_contents(game->host->ctx, origin) & 3u) != 0 && !options.head) {
      game->remove(game->ctx, gib);
      return NULL;
    }
  }
  double ix = 100 * (q2m_rnd(game) * 2 - 1), iy = 100 * (q2m_rnd(game) * 2 - 1);
  qa_vec3 impulse = qa_scale3_double(q2m_v3(ix, iy, 200 + 100 * q2m_rnd(game)), damage < 50 ? 0.7 : 1.2);
  qa_vec3 velocity = qa_add3_double(body.velocity, qa_scale3_double(impulse, options.metallic ? 1 : 0.5));
  qa_vec3 clipped = q2m_v3(fmax(-300, fmin(300, velocity.x)), fmax(-300, fmin(300, velocity.y)),
                           fmax(200, fmin(500, velocity.z)));
  gib->model = model;
  if (rr) gib->classname = "gib";
  gib->frame = 0; gib->old_frame = -1;
  gib->skin = options.skinned ? self->skin : 0;
  gib->effects = rr ? 2 : (double)(((uint32_t)gib->effects | 2u) & ~0x4000u);
  if (rr) gib->render_flags = (1 << 24) | (1 << 13) | (1 << 15);
  gib->flags = (double)((uint32_t)gib->flags | 2048u | (rr ? (1u << 20) | (1u << 28) : 0u));
  gib->server_flags = (double)(((uint32_t)gib->server_flags & ~4u) | (rr ? 2u : 0u));
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
  qa_q2f_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = origin;
  c.has_velocity = true; c.velocity = clipped;
  c.has_angles = true; c.angles = angles;
  c.has_bounds = true; c.bounds = (qa_bounds){q2m_zero(), q2m_zero()};
  c.has_ground = true; c.ground = NULL;
  q2m_move(game, gib, c, true, false);
  if (!game->host->combat.read(game->host->combat.ctx, gib->actor.id).has_value) {
    qa_combat_state state;
    memset(&state, 0, sizeof state);
    state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    state.armor.powered.kind = "none";
    state.can_take_damage = true;
    game->host->combat.create(game->host->combat.ctx, gib->actor, state);
  } else {
    qa_q2f_combat_traits traits;
    memset(&traits, 0, sizeof traits);
    traits.has_can_take_damage = true; traits.can_take_damage = true;
    game->host->combat.set_traits(game->host->combat.ctx, gib->actor, traits);
  }
  gib->die = (qa_q2f_die){NULL, gib_die};
  gib->pain = (qa_q2f_pain){NULL, NULL};
  gib->use = (qa_q2f_use){NULL, NULL};
  gib->touch = rr ? (options.upright ? (qa_q2f_touch){NULL, upright_touch} : (qa_q2f_touch){NULL, NULL})
                  : (options.metallic ? (qa_q2f_touch){NULL, NULL} : (qa_q2f_touch){NULL, organic_touch});
  game->solid(game->ctx, gib, QA_Q2F_SOLID_NONE);
  game->motion(game->ctx, gib, options.metallic ? QA_Q2F_MOTION_BOUNCE : QA_Q2F_MOTION_TOSS);
  game->schedule(game->ctx, gib, 10 + q2m_rnd(game) * 10, (qa_q2f_think){NULL, free_gib});
  game->link(game->ctx, gib);
  game->show(game->ctx, gib);
  return gib;
}
qa_q2f_entity *qa_q2m_throw_head(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage) {
  qa_q2m_gib_options options = {true, false, false, false};
  return qa_q2m_throw_gib(self, game, model, damage, options);
}
