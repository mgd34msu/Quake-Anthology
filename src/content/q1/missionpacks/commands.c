/* Port of src/content/q1/missionpacks/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/commands.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"
#include "qa/content/q1/missionpacks/player.h"
#include <stdio.h>

static void set_count(qa_q1mp_game *game, qa_q1mp_player_state *player, const char *item, double count, double capacity) {
  qa_inventory_entry entries[64]; size_t n = game->base->host->inventory_entries(game->base->host->ctx, player->actor.id, entries, 64);
  qa_inventory_entry e = {0}; e.item = item; e.count = count; e.capacity = capacity;
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(entries[i].item, item)) { e = entries[i]; e.count = count; break; }
  game->base->host->inventory_configure(game->base->host->ctx, player->actor, &e);
}
bool qa_q1mp_mission_pack_command(qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player, qa_q1mp_pack pack, double impulse_d, const qa_q1mp_mission_pack_command_options *options) {
  int impulse = (int)impulse_d; qa_q1b_options *o = &game->base->options;
  bool multiplayer = o->deathmatch != 0 || o->coop;
  if (impulse == 9) {
    if (multiplayer && (qa_q1mp_streq(o->edition,"classic") || !(options && options->cheats_allowed && options->cheats_allowed(options->ctx)))) return true;
    bool selected = options && options->cheat_arsenal && options->cheat_arsenal(options->ctx, player->actor.id, "weapons");
    if (!selected) {
      char buf[96];
      for (size_t i = 0; i < 8; i++) set_count(game, player, qa_q1mp_weapon_item(QA_Q1B_WEAPONS[i], buf, sizeof buf), 1, 1);
      char prefix[32]; (void)snprintf(prefix, sizeof prefix, "%s:", pack);
      for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) if (qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) set_count(game, player, qa_q1mp_weapon_item(qa_q1mp_mission_weapons[i].id, buf, sizeof buf), 1, 1);
    }
    set_count(game, player, "q1:key/silver", 1, 1); set_count(game, player, "q1:key/gold", 1, 1);
    if (!(options && options->cheat_arsenal && options->cheat_arsenal(options->ctx, player->actor.id, "ammo"))) {
      set_count(game, player, "q1:ammo/shells", 100, 100); set_count(game, player, "q1:ammo/nails", 200, 200);
      set_count(game, player, "q1:ammo/rockets", 100, 100); set_count(game, player, "q1:ammo/cells", 200, 100);
      if (qa_q1mp_streq(pack,"rogue")) { set_count(game, player, "rogue:ammo/lava-nails", 200, 200); set_count(game, player, "rogue:ammo/multi-rockets", 100, 100); set_count(game, player, "rogue:ammo/plasma", 100, 100); }
    }
    if (!selected) (void)game->base->select_weapon(game->base, player->actor, "rocketlauncher");
    return true;
  }
  if (impulse == 11) { if (base && base->campaign_write_flags) base->campaign_write_flags(base->ctx, qa_q1mp_fround(base->campaign_read_flags(base->ctx) * 2 + 1)); return true; }
  if (impulse == 255 || (qa_q1mp_streq(pack,"hipnotic") && (impulse == 200 || impulse == 201))) {
    if (multiplayer) return true;
    qa_q1mp_players_powerup(players, player, impulse == 200 ? "hipnotic:wetsuit" : impulse == 201 ? "hipnotic:empathy" : "quad", 30);
    const char *key = impulse == 200 ? "$qc_wetsuit_cheat" : impulse == 201 ? "$qc_empathy_cheat" : "$qc_quad_cheat";
    if (qa_q1mp_streq(pack,"rogue")) { if (options && options->developer_message) options->developer_message(options->ctx, qa_q1mp_streq(o->edition,"classic") ? "quad cheat\n" : key); }
    else qa_q1mp_mission_message(game, QA_Q1MP_NONE, key);
    return true;
  }
  if (!qa_q1mp_streq(pack, "hipnotic")) return false;
  if (impulse == 205) {
    if (multiplayer) return true;
    qa_q1mp_mission_message(game, QA_Q1MP_NONE, "$qc_genocide_cheat");
    qa_q1mp_actor *world = game->base->world;
    if (world) {
      qa_q1mp_actor *ents[512]; size_t n = game->base->entities(game->base, ents, 512);
      for (size_t i = 0; i < n; i++) if (ents[i]->monster && game->base->health(game->base, ents[i]->actor.id) > 0)
        qa_q1b_game_damage(game->base, ents[i]->actor.id, world->actor.id, QA_Q1MP_SOME(world->actor.id), game->base->health(game->base, ents[i]->actor.id) + 10);
    }
    return true;
  }
  if (impulse == 202 || impulse == 203) {
    qa_q1mp_actor *ents[512]; size_t n = game->base->entities(game->base, ents, 512); int ordinal = 0;
    for (size_t i = 0; i < n; i++) {
      if (ents[i] == game->base->world) continue;
      ordinal++;
      if (impulse == 203 && game->base->health(game->base, ents[i]->actor.id) <= 0) continue;
      qa_vec3 origin = game->base->body(game->base, ents[i]).origin; char buf[512];
      if (impulse == 202) (void)snprintf(buf, sizeof buf, "%d %s\n", ordinal, ents[i]->classname);
      else (void)snprintf(buf, sizeof buf, "%d %s '%g %g %g'\n--------------------\n", ordinal, ents[i]->classname, origin.x, origin.y, origin.z);
      if (options && options->developer_message) options->developer_message(options->ctx, buf);
    }
    return true;
  }
  if (impulse == 206) {
    qa_q1mp_actor *world = game->base->world;
    if (world) {
      double next = 1 - qa_q1mp_number(world, "hipnotic:dump-coordinates"); char buf[32]; qa_q1b_format_number(buf, sizeof buf, next);
      qa_q1b_fields_set(world, "hipnotic:dump-coordinates", buf);
      if (next == 1) qa_q1mp_mission_message(game, QA_Q1MP_NONE, "$qc_dump_player_loc");
    }
    return true;
  }
  return false;
}
void qa_q1mp_dump_mission_pack_coordinates(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  if (!game->base->world || qa_q1mp_number(game->base->world, "hipnotic:dump-coordinates") != 1 || game->base->time < player->attack_finished) return;
  qa_q1mp_opt_actor client = game->base->host->check_client(game->base->host->ctx, player->actor);
  qa_q1b_body body; if (!client.has || !qa_q1mp_body_read(game, client.id, &body)) return;
  char buf[256]; (void)snprintf(buf, sizeof buf, "Player: '%g %g %g'\n", body.origin.x, body.origin.y, body.origin.z);
  game->base->message(game->base, QA_Q1MP_NONE, buf, false);
}
