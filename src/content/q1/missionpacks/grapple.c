/* Port of src/content/q1/missionpacks/grapple.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/grapple.h"
#include "missionpacks_internal.h"
#include <stdlib.h>

static qa_q1mp_actor *find_hook(qa_q1mp_game *game, qa_actor_id owner) {
  qa_q1mp_actor *ents[256]; size_t n = game->base->entities(game->base, ents, 256);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname,"hook") && ents[i]->owner.has && qa_same_actor(ents[i]->owner.id, owner)) return ents[i];
  return NULL;
}
static void presentation(qa_q1mp_rogue_grapple *gr, qa_q1mp_player_state *player, double punch) {
  qa_q1mp_game *game = gr->game;
  if (game->weapon_punch) game->weapon_punch(game->ctx, player, punch);
  if (game->emit_weapon) game->emit_weapon(game->ctx, player->actor.id, "rogue:grapple", "progs/v_grpple.mdl", player->weapon_frame, punch);
}
static void reset_hook(qa_q1mp_rogue_grapple *gr, qa_q1mp_actor *hook) {
  qa_q1mp_player_state *player = hook->owner.has ? gr->game->base->player(gr->game->base, hook->owner.id) : NULL;
  if (player) {
    player->weapon_frame = 0; player->attack_finished = qa_q1mp_fround(gr->game->base->time + 0.25); player->weapon_animation_at = -1;
    if (qa_q1mp_streq(player->weapon, "rogue:grapple")) presentation(gr, player, 0);
  }
  gr->game->base->remove(gr->game->base, hook);
}
static bool fire_hook(qa_q1mp_rogue_grapple *gr, qa_q1mp_player_state *player) {
  qa_q1mp_game *game = gr->game; qa_q1b_game *g = game->base;
  player->attack_finished = qa_q1mp_fround(g->time + 0.1);
  if (find_hook(game, player->actor.id)) { player->weapon_frame = 2; presentation(gr, player, 0); return true; }
  qa_q1b_body body; if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  qa_vec3 forward = qa_q1mp_vectors(game, player->view_angles).forward;
  qa_q1mp_actor *hook = g->create(g, "hook");
  hook->owner = QA_Q1MP_SOME(player->actor.id); hook->movement = "flymissile"; hook->solid = "bbox";
  qa_q1mp_set_model(hook, "progs/hook.mdl"); hook->frame = 1; hook->projectile_weapon = "rogue:grapple";
  hook->touch = g->named.touch(g->named.ctx, hook, "rogue:grapple-anchor");
  g->set_bounds(g, hook, QA_Q1MP_POINT);
  qa_q1b_body_patch p = {0};
  p.has_origin = true; p.origin = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, qa_q1mp_vscale(forward, 16)), QA_Q1MP_V3(0,0,16));
  p.has_velocity = true; p.velocity = qa_q1mp_vscale(forward, 800);
  p.has_angles = true; p.angles = qa_q1mp_velocity_angles(forward); qa_q1mp_patch(game, hook, &p);
  g->link(g, hook); g->schedule(g, hook, 2, g->named.action(g->named.ctx, hook, "rogue:grapple-reset"));
  g->sound_owned(g, player->actor, "weapons/chain1.wav", QA_Q1MP_CH("weapon"), 1, 1);
  player->weapon_frame = 1; player->weapon_animation_at = g->time; player->continuous_firing = false;
  presentation(gr, player, -2); return true;
}
static void anchor_hook(qa_q1mp_rogue_grapple *gr, qa_q1mp_actor *hook, qa_actor_id other) {
  qa_q1mp_game *game = gr->game; qa_q1b_game *g = game->base;
  qa_q1mp_player_state *player = hook->owner.has ? g->player(g, hook->owner.id) : NULL;
  if (!player) { reset_hook(gr, hook); return; }
  if (qa_same_actor(other, player->actor.id)) return;
  if (qa_q1mp_streq(g->host->contents(g->host->ctx, g->body(g, hook).origin), "sky")) { reset_hook(gr, hook); return; }
  if (g->is_player(g, QA_Q1MP_SOME(other))) {
    qa_combat_state tc, oc; const char *tt = g->host->combat_read(g->host->ctx, other, &tc) ? tc.team : NULL;
    const char *ot = g->host->combat_read(g->host->ctx, player->actor.id, &oc) ? oc.team : NULL;
    if ((tt == NULL && ot == NULL) || (tt && ot && qa_q1mp_streq(tt, ot))) { reset_hook(gr, hook); return; }
    qa_q1mp_sound_ch(game, hook, "player/axhit1.wav", "weapon");
    qa_q1mp_damage_w(game, other, hook->actor.id, QA_Q1MP_SOME(player->actor.id), 10, "rogue:grapple");
  } else {
    qa_q1mp_sound_ch(game, hook, "player/axhit2.wav", "weapon");
    if (qa_q1mp_can_damage_actor(game, other)) qa_q1mp_damage_w(game, other, hook->actor.id, QA_Q1MP_SOME(player->actor.id), 1, "rogue:grapple");
    qa_q1mp_set_velocity(game, hook, QA_Q1MP_ZERO); hook->angular_velocity = QA_Q1MP_ZERO;
  }
  hook->frame = 2; g->sound_owned(g, player->actor, "weapons/tink1.wav", QA_Q1MP_CH("weapon"), 1, 1);
  if (!player->attack_held) { reset_hook(gr, hook); return; }
  hook->count = 1; hook->solid = "none"; hook->touch.has = false;
  qa_q1mp_set_mission_reference(hook, "rogue:target", QA_Q1MP_SOME(other));
  qa_q1b_body body; if (qa_q1mp_body_read(game, player->actor.id, &body)) { body.ground = QA_Q1MP_NONE; g->host->bodies_write(g->host->ctx, player->actor, &body); }
  g->link(g, hook); g->schedule(g, hook, 0, g->named.action(g->named.ctx, hook, "rogue:grapple-track"));
}
static void track_hook(qa_q1mp_rogue_grapple *gr, qa_q1mp_actor *hook) {
  qa_q1mp_game *game = gr->game; qa_q1b_game *g = game->base;
  qa_q1mp_player_state *player = hook->owner.has ? g->player(g, hook->owner.id) : NULL;
  qa_q1mp_opt_actor target = qa_q1mp_mission_reference(game, hook, "rogue:target");
  if (!player || g->health(g, player->actor.id) <= 0 || !target.has || (g->is_player(g, target) && g->health(g, target.id) <= 0)) { reset_hook(gr, hook); return; }
  qa_q1b_body body; if (!qa_q1mp_body_read(game, target.id, &body)) { reset_hook(gr, hook); return; }
  if (g->is_player(g, target)) {
    qa_q1mp_player_state *tp = g->player(g, target.id);
    if ((tp ? tp->teleport_until : 0) > g->time) { reset_hook(gr, hook); return; }
    g->set_origin(g, hook, body.origin); qa_q1mp_sound_ch(game, hook, "pendulum/hit.wav", "weapon");
    qa_q1mp_damage_w(game, target.id, hook->actor.id, QA_Q1MP_SOME(player->actor.id), 1, "rogue:grapple");
    (void)qa_q1mp_random(game); (void)qa_q1mp_random(game); (void)qa_q1mp_random(game);
    qa_q1mp_effect_a(game, "blood", body.origin, target, 20);
  }
  qa_q1mp_actor *te = g->entity(g, target);
  if ((te && qa_q1mp_streq(te->solid, "slidebox")) || g->is_player(g, target)) {
    qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
    p.has_velocity = true; p.velocity = QA_Q1MP_ZERO; qa_q1mp_patch(game, hook, &p);
  } else qa_q1mp_set_velocity(game, hook, body.velocity);
  g->schedule(g, hook, 0.1, g->named.action(g->named.ctx, hook, "rogue:grapple-track"));
}
static void service(qa_q1mp_rogue_grapple *gr, qa_q1mp_player_state *player) {
  qa_q1mp_game *game = gr->game; qa_q1b_game *g = game->base;
  qa_q1mp_actor *hook = find_hook(game, player->actor.id); qa_q1b_body body;
  if (!hook || !qa_q1mp_body_read(game, player->actor.id, &body)) return;
  qa_vec3 origin = g->body(g, hook).origin; double distance = qa_q1mp_length(qa_q1mp_vsub(origin, body.origin));
  if (hook->count == 1) {
    if ((!player->attack_held && qa_q1mp_streq(player->weapon,"rogue:grapple")) || player->teleport_until > g->time) { reset_hook(gr, hook); return; }
    qa_q1b_basis basis = qa_q1mp_vectors(game, body.angles);
    qa_vec3 direction = qa_q1mp_vsub(origin, qa_q1mp_vadd(qa_q1mp_vadd(body.origin, qa_q1mp_vscale(basis.up, player->jump_held ? 0 : 16)), qa_q1mp_vscale(basis.forward, 16)));
    double speed = qa_q1mp_length(direction);
    body.velocity = qa_q1mp_vscale(qa_q1mp_normalize(direction), speed <= 100 ? speed * 10 : 1000); body.ground = QA_Q1MP_NONE;
    g->host->bodies_write(g->host->ctx, player->actor, &body);
  }
  if (distance > 50 && game->emit_beam) game->emit_beam(game->ctx, "grapple", hook->actor.id, origin, qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,16)));
}
static bool weapon_fire(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p) { (void)g; return fire_hook(vctx, p); }
static void weapon_anim(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p, double s) {
  (void)s; qa_q1mp_rogue_grapple *gr = vctx;
  if (!find_hook(gr->game, p->actor.id) || p->weapon_frame != 1 || g->base->time < p->weapon_animation_at + 0.1) return;
  p->weapon_frame = 2; presentation(gr, p, 0);
}
static void ext_attach(void *vctx, qa_q1mp_game *game, qa_q1mp_player_state *player) {
  (void)vctx; char buf[96]; const char *item = qa_q1mp_weapon_item("rogue:grapple", buf, sizeof buf);
  qa_inventory_entry e = {0}; e.item = item; e.count = (game->base->options.deathmatch != 0 && qa_q1mp_teamplay(game) >= 4) ? 1 : 0; e.capacity = 1;
  game->base->host->inventory_configure(game->base->host->ctx, player->actor, &e);
}
static void ext_frame(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p) { (void)g; service(vctx, p); }
static void reset_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; reset_hook(vctx, e); }
static void anchor_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e, qa_actor_id o, bool hn, qa_vec3 n, const void *s) { (void)r;(void)hn;(void)n;(void)s; anchor_hook(vctx, e, o); }
static void track_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; track_hook(vctx, e); }
void qa_q1mp_rogue_grapple_init(qa_q1mp_rogue_grapple *grapple, qa_q1mp_game *game) {
  grapple->game = game;
  if (game->register_weapon) {
    qa_q1mp_weapon_registration w = {0}; w.id = "rogue:grapple"; w.model = "progs/v_grpple.mdl"; w.ammo = NULL; w.rank = 12; w.ctx = grapple; w.fire = weapon_fire; w.animate = weapon_anim;
    game->register_weapon(game->ctx, &w);
  }
  if (game->register_player_extension) {
    qa_q1mp_player_extension ext = {0}; ext.id = "rogue:grapple"; ext.ctx = grapple; ext.attach = ext_attach; ext.frame = ext_frame;
    game->register_player_extension(game->ctx, &ext);
  }
  qa_q1b_handlers h = {0}; h.ctx = grapple; h.action = reset_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:grapple-reset", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = grapple; h2.touch = anchor_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:grapple-anchor", &h2);
  qa_q1b_handlers h3 = {0}; h3.ctx = grapple; h3.action = track_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:grapple-track", &h3);
}
