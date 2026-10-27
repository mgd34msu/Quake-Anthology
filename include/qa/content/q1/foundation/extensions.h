/* Port of src/content/q1/foundation/extensions.ts (types only). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_EXTENSIONS_H
#define QA_CONTENT_Q1_FOUNDATION_EXTENSIONS_H

#include "qa/content/q1/foundation/types.h"

typedef struct qa_q1_weapon_definition {
  void *ctx;
  qa_q1_weapon id;
  qa_item_id item; /* NULL = TS undefined -> weaponItem(id) */
  qa_item_id ammo; /* NULL = TS null */
  double ammo_per_shot; bool has_ammo_per_shot;
  const char *model;
  double rank;
  const char *(*model_for)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  bool (*available)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  bool (*best_available)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  bool (*fire)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  void (*animate)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds);
} qa_q1_weapon_definition;

typedef struct qa_q1_player_extension {
  void *ctx;
  const char *id;
  void (*attach)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  bool (*inventory_capacity)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double *out);
  void (*frame)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds);
  void (*after_physics)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds);
  bool (*capture_travel)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, uint8_t **out, size_t *out_len);
  void (*restore_travel)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, const uint8_t *bytes, size_t len);
} qa_q1_player_extension;

typedef struct qa_q1_pickup_rules {
  void *ctx;
  const char *id;
  bool (*weapon_leave)(void *ctx, qa_q1_services *game);
  qa_q1_weapon (*weapon_granted)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon);
  double (*weapon_ammo_grant)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon, double default_amount);
  double (*weapon_rank)(void *ctx, qa_q1_weapon weapon);
  bool (*auto_switch)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, bool was_owned);
  double (*respawn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, double default_seconds);
} qa_q1_pickup_rules;

typedef struct qa_q1_weapon_rules {
  void *ctx;
  const char *id;
  bool (*consume_ammo)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double amount);
  void (*before_fire)(void *ctx, qa_q1_services *game, qa_q1_player_state *player);
  double (*frame_delay)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay);
  double (*attack_delay)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay);
  double (*nail_speed)(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double speed);
} qa_q1_weapon_rules;

#endif
