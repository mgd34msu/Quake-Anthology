/* Port of src/content/q2/missionpacks/players.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/players.h"

#include <ctype.h>
#include <string.h>

#include "missionpacks_internal.h"

void qa_q2mp_rogue_player_spawns_init(qa_q2mp_rogue_player_spawns *spawns) { spawns->unused = 0; }

bool qa_q2mp_rogue_player_spawns_spawn(qa_q2mp_rogue_player_spawns *s, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)s;
  if (game->options.edition != QA_Q2F_CLASSIC || strcmp(entity->classname, "info_player_coop_lava") != 0) return false;
  if (!q2mp_coop(game)) game->remove(game->ctx, entity);
  return true;
}
static bool map_is(const char *map_name, const char *name) {
  size_t i = 0;
  for (; map_name[i] != '\0' && name[i] != '\0'; i++)
    if (tolower((unsigned char)map_name[i]) != name[i]) return false;
  return map_name[i] == '\0' && name[i] == '\0';
}
bool qa_q2mp_rogue_player_spawns_select_spawn(qa_q2mp_rogue_player_spawns *s, qa_q2f_game *game,
                                              qa_q2mp_spawn_point *out) {
  (void)s;
  const char *map_name = game->options.map_name != NULL ? game->options.map_name : "";
  if (game->options.edition != QA_Q2F_CLASSIC || !q2mp_coop(game) || (!map_is(map_name, "rmine2") && !map_is(map_name, "rmine2p")))
    return false;
  double lava_top = -99999;
  qa_q2f_entity *highest_lava = NULL;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q2f_entity *lava = game->entities[i];
    if (strcmp(lava->classname, "func_door") != 0 || ((q2mp_u32(lava->spawnflags) & 2u) == 0)) continue;
    qa_body_state body = q2mp_body(game, lava);
    qa_vec3 center = q2mp_add(body.origin, q2mp_scale(q2mp_add(body.bounds.min, body.bounds.max), 0.5));
    if (((q2mp_u32(game->host->point_contents(game->host->ctx, center)) & 56u) != 0) &&
        body.origin.z + body.bounds.max.z > lava_top) {
      lava_top = body.origin.z + body.bounds.max.z;
      highest_lava = lava;
    }
  }
  if (highest_lava == NULL) return false;
  lava_top += 64;
  double count = 0, lowest = 999999;
  qa_q2f_entity *selected = NULL;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q2f_entity *point = game->entities[i];
    if (strcmp(point->classname, "info_player_coop_lava") != 0) continue;
    if (++count > 64) break;
    qa_vec3 origin = q2mp_body(game, point).origin;
    if (origin.z < lava_top) continue;
    double distance = 9999999;
    size_t players = 0;
    const qa_actor_id *list = game->host->players(game->host->ctx, &players);
    for (size_t j = 0; j < players; j++) {
      qa_body_state_result body = q2mp_bread(game, list[j]);
      if (body.has_value && q2mp_health(game, list[j]) > 0) {
        double d = q2mp_len(q2mp_sub(origin, body.value.origin));
        if (d < distance) distance = d;
      }
    }
    if (distance > 32 && origin.z < lowest) { selected = point; lowest = origin.z; }
  }
  if (selected == NULL) return false;
  qa_body_state body = q2mp_body(game, selected);
  out->origin = q2mp_add(body.origin, q2mp_v3(0, 0, 9));
  out->angles = body.angles;
  return true;
}
