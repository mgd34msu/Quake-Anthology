/* Port of src/content/q1/missionpacks/commands.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/commands.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"

static void set_count(qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double count, double capacity) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, player->actor.id, &entry_count);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item; entry.count = count; entry.capacity = capacity;
  for (size_t i = 0; i < entry_count; i++)
    if (qa_q1mp_streq(entries[i].item, item)) { entry = entries[i]; entry.count = count; break; }
  qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
}
static bool cheat_arsenal(const qa_q1mp_mission_pack_command_options *options, qa_actor_id actor, const char *category) {
  return options != NULL && options->cheat_arsenal != NULL ? options->cheat_arsenal(options->ctx, actor, category) : false;
}
static void developer_message(const qa_q1mp_mission_pack_command_options *options, const char *text) {
  if (options != NULL && options->developer_message != NULL) options->developer_message(options->ctx, text);
}
bool qa_q1mp_mission_pack_command(qa_q1_services *game, qa_q1_base *base, qa_q1mp_mission_pack_players *players,
                                  qa_q1_player_state *player, qa_q1mp_pack pack, double impulse,
                                  const qa_q1mp_mission_pack_command_options *options) {
  const bool multiplayer = game->options.deathmatch != 0 || game->options.coop;
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic"), rogue = qa_q1mp_streq(pack, "rogue");
  const bool classic = qa_q1mp_streq(game->options.edition, "classic");
  if (impulse == 9) {
    const bool cheats_allowed = options != NULL && options->cheats_allowed != NULL ? options->cheats_allowed(options->ctx) : false;
    if (multiplayer && (classic || !cheats_allowed)) return true;
    const bool selected_weapons = cheat_arsenal(options, player->actor.id, "weapons");
    if (!selected_weapons) {
      for (size_t i = 0; i < 8; i++) set_count(game, player, qa_q1_weapon_item(QA_Q1_WEAPONS[i]), 1, 1);
      char prefix[32];
      (void)snprintf(prefix, sizeof prefix, "%s:", pack);
      for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++)
        if (qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) set_count(game, player, qa_q1_weapon_item(qa_q1mp_mission_weapons[i].id), 1, 1);
    }
    set_count(game, player, "q1:key/silver", 1, 1); set_count(game, player, "q1:key/gold", 1, 1);
    if (!cheat_arsenal(options, player->actor.id, "ammo")) {
      set_count(game, player, "q1:ammo/shells", 100, 100); set_count(game, player, "q1:ammo/nails", 200, 200);
      set_count(game, player, "q1:ammo/rockets", 100, 100); set_count(game, player, "q1:ammo/cells", 200, 100);
      if (rogue) {
        set_count(game, player, "rogue:ammo/lava-nails", 200, 200); set_count(game, player, "rogue:ammo/multi-rockets", 100, 100);
        set_count(game, player, "rogue:ammo/plasma", 100, 100);
      }
    }
    if (!selected_weapons) (void)qa_q1_select_weapon(game, player->actor, "rocketlauncher");
    return true;
  }
  if (impulse == 11) {
    const qa_q1_campaign_binding campaign = qa_q1_base_campaign(base);
    campaign.write_flags(campaign.ctx, qa_q1mp_fround(campaign.read_flags(campaign.ctx) * 2 + 1));
    return true;
  }
  if (impulse == 255 || (hipnotic && (impulse == 200 || impulse == 201))) {
    if (multiplayer) return true;
    qa_q1mp_players_powerup(players, player, impulse == 200 ? "hipnotic:wetsuit" : impulse == 201 ? "hipnotic:empathy" : "quad", 30);
    const char *key = impulse == 200 ? "$qc_wetsuit_cheat" : impulse == 201 ? "$qc_empathy_cheat" : "$qc_quad_cheat";
    if (rogue) developer_message(options, classic ? "quad cheat\n" : key);
    else qa_q1mp_mission_message(game, NULL, key);
    return true;
  }
  if (!hipnotic) return false;
  if (impulse == 205) {
    if (multiplayer) return true;
    qa_q1mp_mission_message(game, NULL, "$qc_genocide_cheat");
    qa_q1_actor *world = game->world;
    if (world != NULL) {
      qa_q1mp_entity_walk walk = {0};
      for (qa_q1_actor *entity; (entity = qa_q1mp_entity_walk_next(game, &walk)) != NULL;) {
        if (entity->monster != NULL && qa_q1_health(game, entity->actor.id) > 0) {
          const qa_actor_id self = world->actor.id;
          (void)qa_q1_damage(game, entity->actor.id, self, &self, qa_q1_health(game, entity->actor.id) + 10, NULL, NULL, NULL, NULL);
        }
      }
      qa_q1mp_entity_walk_free(&walk);
    }
    return true;
  }
  if (impulse == 202 || impulse == 203) {
    int ordinal = 0;
    for (size_t i = 0; i < game->entity_count; i++) {
      qa_q1_actor *entity = game->entities[i];
      if (entity == game->world) continue;
      ordinal++;
      if (impulse == 203 && qa_q1_health(game, entity->actor.id) <= 0) continue;
      const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
      char text[512];
      if (impulse == 202) (void)snprintf(text, sizeof text, "%d %s\n", ordinal, entity->classname);
      else (void)snprintf(text, sizeof text, "%d %s '%s %s %s'\n--------------------\n", ordinal, entity->classname,
                          QA_JS_NUMBER(origin.x), QA_JS_NUMBER(origin.y), QA_JS_NUMBER(origin.z));
      developer_message(options, text);
    }
    return true;
  }
  if (impulse == 206) {
    qa_q1_actor *world = game->world;
    if (world != NULL) {
      const double next = 1 - qa_q1_actor_number(world, "hipnotic:dump-coordinates");
      qa_q1mp_field_number(world, "hipnotic:dump-coordinates", next);
      if (next == 1) qa_q1mp_mission_message(game, NULL, "$qc_dump_player_loc");
    }
    return true;
  }
  return false;
}
void qa_q1mp_dump_mission_pack_coordinates(qa_q1_services *game, qa_q1_player_state *player) {
  if (game->world == NULL || qa_q1_actor_number(game->world, "hipnotic:dump-coordinates") != 1 || game->time < player->attack_finished) return;
  qa_actor_id client;
  if (!game->host.check_client(game->host.ctx, player->actor, &client)) return;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, client);
  if (!body.has_value) return;
  char text[256];
  (void)snprintf(text, sizeof text, "Player: '%s %s %s'\n", QA_JS_NUMBER(body.value.origin.x), QA_JS_NUMBER(body.value.origin.y),
                 QA_JS_NUMBER(body.value.origin.z));
  qa_q1_message(game, NULL, text, false, NULL, 0);
}
