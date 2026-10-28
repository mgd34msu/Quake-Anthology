/* Port of src/content/q3/base/game/personal-portal.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/personal_portal.h"
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#define CONTENTS_CORPSE_V (double)0x4000000
#define CONTENTS_TRIGGER_V (double)(int32_t)0x40000000
typedef qa_q3_personal_portal_runtime RT;
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
static qa_q3_entity_pool *pool(RT *r) { return r->host.combat->entities; }
static void owned(RT *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(pool(r), e->slot) != e)
    qa_range_error("Personal portal entity does not belong to its entity pool or was replaced");
}
static qa_q3_game_client *player_client(RT *r, qa_q3_game_entity *e) {
  owned(r, e);
  if (!e->client) qa_range_error("Personal portal use requires a client entity");
  return e->client;
}
static void free_portal(RT *r, qa_q3_game_entity *e) { owned(r, e); qa_q3_entity_pool_free(pool(r), e); }
static qa_q3_game_entity *destination(RT *r, double sequence) {
  qa_q3_game_entity *d = NULL;
  while ((d = qa_q3_find_entity(pool(r), d, QA_Q3_FIELD_CLASSNAME, "hi_portal destination")) != NULL)
    if (d->count == sequence) return d;
  return NULL;
}
static void teleport(RT *r, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles) {
  if (r->host.has_map_travel) { r->host.map_teleport(r->host.ctx, player, origin, angles); return; }
  qa_q3_teleport_context tc; tc.combat = r->host.combat; tc.kill_box = r->host.kill_box; tc.world = r->host.world;
  qa_q3_teleport_player(tc, player, origin, angles);
}
static void drop_carried_flag(RT *r, qa_q3_game_entity *player) {
  if (r->host.has_map_travel) { r->host.map_drop_carried_flag(r->host.ctx, player); return; }
  qa_q3_game_client *c = player->client;
  if (!c) qa_range_error("Portal touch requires a client entity");
  double powerup = qa_q3_game_slots_get_mirror(&c->ps.powerups, QA_Q3_PW_NEUTRALFLAG) != 0 ? QA_Q3_PW_NEUTRALFLAG
    : qa_q3_game_slots_get_mirror(&c->ps.powerups, QA_Q3_PW_REDFLAG) != 0 ? QA_Q3_PW_REDFLAG
    : qa_q3_game_slots_get_mirror(&c->ps.powerups, QA_Q3_PW_BLUEFLAG) != 0 ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_NONE;
  if ((int)powerup == QA_Q3_PW_NONE) return;
  const qa_q3_game_item_definition_mirror *item = r->host.find_item_for_powerup(r->host.ctx, powerup);
  if (!item) { char buf[128]; snprintf(buf, sizeof buf, "Portal carried flag %g is absent from the missionpack item table", powerup); qa_range_error("%s", buf); }
  (void)r->host.drop_item(r->host.ctx, player, item, 0);
  qa_q3_game_slots_set_mirror(&c->ps.powerups, (size_t)powerup, 0);
}
static void portal_touch(RT *r, qa_q3_game_entity *source, qa_q3_game_entity *other) {
  owned(r, source);
  if (qa_q3_game_entity_health(other) <= 0 || !other->client) return;
  owned(r, other); drop_carried_flag(r, other);
  qa_q3_game_entity *dest = destination(r, source->count);
  if (!dest) {
    if (source->pos1.x != 0 || source->pos1.y != 0 || source->pos1.z != 0) teleport(r, other, source->pos1, source->s.angles);
    qa_q3_damage_participant self = qa_q3_participant_entity(other);
    qa_q3_combat_damage(r->host.combat, self, &self, &self, NULL, NULL, 100000, QA_Q3_DAMAGE_NO_PROTECTION, 18);
    return;
  }
  teleport(r, other, dest->s.pos.base, dest->s.angles);
}
static void portal_enable(RT *r, qa_q3_game_entity *source) {
  owned(r, source);
  source->touch = qa_q3_callback_family_resolve(&pool(r)->callbacks.touch, "q3.base.game.personal-portal.portalEnable.touch");
  source->think = qa_q3_callback_family_resolve(&pool(r)->callbacks.think, "q3.base.game.personal-portal.portalEnable.think");
  qa_q3_game_entity_set_nextthink(source, qa_int32(r->host.combat->time + 120000));
}
void qa_q3_personal_portal_drop_destination(RT *r, qa_q3_game_entity *player) {
  qa_q3_game_client *client = player_client(r, player);
  qa_q3_game_entity *portal = qa_q3_entity_pool_spawn(pool(r));
  portal->s.modelindex = r->host.model_index(r->host.ctx, "models/powerups/teleporter/tele_exit.md3");
  qa_q3_set_origin(portal, qa_q3_snap_vector(player->s.pos.base));
  portal->r.mins = player->r.mins; portal->r.maxs = player->r.maxs;
  qa_q3_game_entity_set_classname(portal, "hi_portal destination");
  portal->r.contents = CONTENTS_CORPSE_V;
  qa_q3_game_entity_set_takedamage(portal, true); qa_q3_game_entity_set_health(portal, 200);
  portal->die = qa_q3_callback_family_resolve(&pool(r)->callbacks.die, "q3.base.game.personal-portal.dropPortalDestination.die");
  portal->s.angles = player->s.apos.base;
  portal->think = qa_q3_callback_family_resolve(&pool(r)->callbacks.think, "q3.base.game.personal-portal.portalEnable.think");
  qa_q3_game_entity_set_nextthink(portal, qa_int32(r->host.combat->time + 120000));
  r->host.world.link(r->host.world.ctx, portal);
  r->portal_sequence = qa_int32(r->portal_sequence + 1);
  client->portal_id = r->portal_sequence; portal->count = client->portal_id;
  const qa_q3_game_item_definition_mirror *item = r->host.find_item(r->host.ctx, "Portal");
  if (!item) qa_range_error("Portal holdable is absent from the missionpack item table");
  double index = r->host.item_index(r->host.ctx, item);
  if (index < 1) qa_range_error("Portal holdable has no missionpack item index");
  qa_q3_game_slots_set_mirror(&client->ps.stats, (size_t)qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK)->holdable_item, index);
}
void qa_q3_personal_portal_drop_source(RT *r, qa_q3_game_entity *player) {
  qa_q3_game_client *client = player_client(r, player);
  qa_q3_game_entity *portal = qa_q3_entity_pool_spawn(pool(r));
  portal->s.modelindex = r->host.model_index(r->host.ctx, "models/powerups/teleporter/tele_enter.md3");
  qa_q3_set_origin(portal, qa_q3_snap_vector(player->s.pos.base));
  portal->r.mins = player->r.mins; portal->r.maxs = player->r.maxs;
  qa_q3_game_entity_set_classname(portal, "hi_portal source");
  portal->r.contents = (double)((int32_t)CONTENTS_CORPSE_V | (int32_t)CONTENTS_TRIGGER_V);
  qa_q3_game_entity_set_takedamage(portal, true); qa_q3_game_entity_set_health(portal, 200);
  portal->die = qa_q3_callback_family_resolve(&pool(r)->callbacks.die, "q3.base.game.personal-portal.dropPortalDestination.die");
  r->host.world.link(r->host.world.ctx, portal);
  portal->count = client->portal_id; client->portal_id = 0;
  qa_q3_game_entity_set_nextthink(portal, qa_int32(r->host.combat->time + 1000));
  portal->think = qa_q3_callback_family_resolve(&pool(r)->callbacks.think, "q3.base.game.personal-portal.dropPortalSource.think");
  qa_q3_game_entity *dest = destination(r, portal->count);
  if (dest) portal->pos1 = dest->s.pos.base;
}
static void cb_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) { (void)c; if (o.entity) portal_touch(ctx, s, o.entity); }
static void cb_free(void *ctx, qa_q3_game_entity *s) { free_portal(ctx, s); }
static void cb_die(void *ctx, qa_q3_game_entity *s, qa_q3_damage_inflictor i, qa_q3_damage_participant a, double d, double m) {
  (void)i; (void)a; (void)d; (void)m; free_portal(ctx, s);
}
static void cb_enable(void *ctx, qa_q3_game_entity *s) { portal_enable(ctx, s); }
void qa_q3_personal_portal_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *c = &pool(r)->callbacks;
  qa_q3_entity_touch_fn t = cb_touch; qa_q3_entity_think_fn f = cb_free, en = cb_enable; qa_q3_entity_die_fn d = cb_die;
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.personal-portal.portalEnable.touch", mkcb(&t, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.personal-portal.portalEnable.think", mkcb(&f, r));
  qa_q3_callback_family_intern(&c->die, "q3.base.game.personal-portal.dropPortalDestination.die", mkcb(&d, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.personal-portal.dropPortalSource.think", mkcb(&en, r));
}
void qa_q3_personal_portal_runtime_init(RT *r, qa_q3_personal_portal_host host) {
  if (host.combat->entities->options.product != QA_Q3_GAME_PRODUCT_MISSIONPACK)
    qa_range_error("Personal portals require a missionpack entity pool");
  if (!host.spatial_is_world) qa_range_error("Personal portal combat and collision worlds must match");
  r->host = host; r->portal_sequence = 0; qa_q3_personal_portal_bind_save_callbacks(r);
}
double qa_q3_personal_portal_capture_sequence(const RT *r) { return r->portal_sequence; }
void qa_q3_personal_portal_restore_sequence(RT *r, double sequence) {
  if (sequence != (int32_t)sequence) qa_range_error("portal sequence exceeds source integer range");
  r->portal_sequence = sequence;
}
