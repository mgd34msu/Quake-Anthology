/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/q2_exports.h"
#include <ctype.h>
#include <string.h>
void qa_q2_bot_set_weapon(qa_q2_bot_exports_host *h, int64_t entity, int64_t weapon, bool instant) {
  if (weapon <= 0 || weapon > h->item_count) return;
  qa_q2_bot_client_view bot;
  if (!h->get_bot(h->ctx, entity, &bot) || !bot.bot || !bot.has_client || h->inventory(h->ctx, entity, weapon) == 0) return;
  if (bot.current_weapon == weapon || bot.pending_weapon == weapon) return;
  qa_q2_bot_item_view item;
  if (!h->get_item(h->ctx, weapon, &item) || !item.weapon || !item.use_callback) return;
  h->disable_weapon_chains(h->ctx, entity); h->use_item(h->ctx, item.use_callback, entity, item.id);
  if (instant) h->change_weapon_instantly(h->ctx, entity);
}
void qa_q2_bot_trigger_edict(qa_q2_bot_exports_host *h, int64_t entity, int64_t target) {
  qa_q2_bot_client_view bot; qa_q2_bot_entity_view initial;
  bool has_bot = h->get_bot(h->ctx, entity, &bot); bool has_init = h->get_entity(h->ctx, target, &initial);
  if (!has_bot || !bot.inuse || !bot.bot || !has_init || !initial.inuse) return;
  if (initial.use_callback) h->trigger_use(h->ctx, initial.use_callback, target, entity, entity);
  qa_q2_bot_entity_view reached; if (h->get_entity(h->ctx, target, &reached) && reached.touch_callback) h->trigger_touch(h->ctx, reached.touch_callback, target, entity, true);
}
void qa_q2_bot_use_item(qa_q2_bot_exports_host *h, int64_t entity, int64_t item_id) {
  qa_q2_bot_client_view bot; if (!h->get_bot(h->ctx, entity, &bot) || !bot.inuse || !bot.bot || !bot.has_client) return;
  h->set_selected_item(h->ctx, entity, item_id); h->validate_selected_item(h->ctx, entity);
  qa_q2_bot_client_view after; if (!h->get_bot(h->ctx, entity, &after) || !after.has_client) return;
  int64_t selected = after.selected_item; if (selected == 0 || selected != item_id) return;
  qa_q2_bot_item_view item; bool has_item = h->get_item(h->ctx, selected, &item);
  h->set_selected_item(h->ctx, entity, 0);
  if (!has_item || !item.use_callback) return;
  h->disable_weapon_chains(h->ctx, entity); h->use_item(h->ctx, item.use_callback, entity, item.id);
}
static bool eq_lower(const char *a, const char *b) { while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
a++; b++; } return *a == *b; }
int64_t qa_q2_bot_get_item_id(qa_q2_bot_exports_host *h, const char *classname) {
  if (!classname || !classname[0]) return -1;
  if (eq_lower(classname, "none")) return 0;
  for (int64_t i = 0; i < h->item_count; i++) { qa_q2_bot_item_view item; if (h->get_item(h->ctx, i, &item) && item.classname && item.classname[0] && eq_lower(item.classname, classname)) return item.id;
  }
  return -1;
}
void qa_q2_edict_force_look_at_point(qa_q2_bot_exports_host *h, int64_t entity, qa_bot_vec3 point) {
  qa_q2_bot_client_view bot; if (!h->get_bot(h->ctx, entity, &bot) || !bot.has_client) return;
  qa_bot_vec3 eye = {bot.origin.x + bot.view_offset.x, bot.origin.y + bot.view_offset.y, bot.origin.z + bot.view_offset.z};
  qa_bot_pitch_yaw angles = qa_bot_vector_to_angles(qa_bvec_sub(point, eye));
  h->force_look(h->ctx, entity, (qa_bot_vec3){angles.pitch - bot.command_angles.x, angles.yaw - bot.command_angles.y, -bot.command_angles.z});
}
bool qa_q2_bot_picked_up_item(qa_q2_bot_exports_host *h, int64_t entity, int64_t item_entity) { return h->picked_up_by(h->ctx, item_entity, entity - 1); }
