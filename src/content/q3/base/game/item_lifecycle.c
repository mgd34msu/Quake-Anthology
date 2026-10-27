/* Port of src/content/q3/base/game/item-lifecycle.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/foundation/support.h"
qa_q3_player_inventory_mirror qa_q3_item_inventory(qa_q3_game_client *client) {
  qa_q3_player_inventory_mirror inv; inv.stats = client->ps.stats; inv.ammo = client->ps.ammo;
  inv.powerups = client->ps.powerups;
  size_t health_idx = 0; inv.health = qa_q3_game_slots_get_mirror(&client->ps.stats, health_idx); return inv;
}
void qa_q3_item_registry_init(qa_q3_item_registry *registry, const qa_q3_game_item_definition_mirror *items, size_t count) {
  registry->items = items; registry->count = count;
}
void qa_q3_respawn_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context) {
  (void)entity; (void)context; qa_range_error("Q3 item respawn requires ItemLifecycleContext world wiring (unported)");
}
void qa_q3_touch_item(qa_q3_game_entity *entity, qa_q3_damage_participant other, qa_touch_contact contact, qa_q3_item_lifecycle_context context) {
  (void)entity; (void)other; (void)contact; (void)context; qa_range_error("Q3 item touch requires ItemLifecycleContext world wiring (unported)");
}
void qa_q3_finish_spawning_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context) {
  (void)entity; (void)context; qa_range_error("Q3 item spawn finish requires ItemLifecycleContext world wiring (unported)");
}
void qa_q3_bind_item_save_callbacks(qa_q3_item_lifecycle_context context) { (void)context; }
