#ifndef QA_UNIFIED_FRAME_PLAYER_H
#define QA_UNIFIED_FRAME_PLAYER_H

#include "qa/inventory.h"

#include "qa/network_unified_frame.h"
#include "qa/unified_frame_components.h"

typedef struct qa_unified_source_identity {
    qa_actor_owner provider;
    char *instance;
} qa_unified_source_identity;
typedef struct qa_unified_player_view {
    qa_vec3 origin, angles, kick_angles, client_view_offset_delta;
    float view_height, field_of_view;
    float blend[4], damage_blend[4];
    bool has_field_of_view, has_client_view_offset_delta, has_blend, has_damage_blend;
    bool foreign_character_death, has_pitch_drift, grounded, pitch_drift_disabled;
    float ideal_pitch;
} qa_unified_player_view;
typedef struct qa_unified_client_presentation {
    qa_actor_id recipient;
    bool has_hud, has_view;
    qa_unified_source_identity hud_source, view_source;
    double health, armor;
    qa_vec3 origin, angles;
    float view_height;
} qa_unified_client_presentation;
typedef struct qa_unified_powerup_state {
    qa_item_id id;
    char *label;
    double seconds;
} qa_unified_powerup_state;
typedef enum qa_unified_ui_item_kind { QA_UNIFIED_UI_WEAPON, QA_UNIFIED_UI_POWERUP } qa_unified_ui_item_kind;
typedef struct qa_unified_ui_item {
    qa_item_id id;
    char *label;
    qa_unified_ui_item_kind kind;
    int64_t source_ordinal;
    bool owned, has_ammo, has_count;
    double count, warning_count;
} qa_unified_ui_item;
typedef struct qa_unified_weapon_status {
    qa_unified_provider_state source;
    qa_item_id item, ammo_item;
    char *label;
    bool finite, has_ammo_to_start, low;
    double count;
} qa_unified_weapon_status;
typedef struct qa_unified_native_inventory_item { char *item, *label; double count; } qa_unified_native_inventory_item;
typedef enum qa_unified_inventory_presentation_kind {
    QA_UNIFIED_INVENTORY_PRESENTATION_NONE, QA_UNIFIED_INVENTORY_PRESENTATION_WEAPON,
    QA_UNIFIED_INVENTORY_PRESENTATION_AMMUNITION, QA_UNIFIED_INVENTORY_PRESENTATION_ITEM
} qa_unified_inventory_presentation_kind;
typedef enum qa_unified_inventory_icon_kind {
    QA_UNIFIED_INVENTORY_ICON_NONE, QA_UNIFIED_INVENTORY_ICON_SHADER,
    QA_UNIFIED_INVENTORY_ICON_IMAGE, QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE
} qa_unified_inventory_icon_kind;
typedef struct qa_unified_inventory_presentation {
    qa_unified_provider_state source;
    qa_unified_inventory_presentation_kind kind;
    qa_unified_inventory_icon_kind icon_kind;
    char *weapon, *content, *path, *lump;
} qa_unified_inventory_presentation;
typedef struct qa_unified_native_inventory {
    qa_unified_native_inventory_item *items;
    size_t item_count;
    char *selected;
    qa_unified_inventory_presentation presentation;
} qa_unified_native_inventory;
typedef struct qa_unified_q1_team_face { qa_string_id content; uint8_t colors; double frags; } qa_unified_q1_team_face;
typedef struct qa_unified_player_ui {
    double health;
    qa_armor armor;
    qa_inventory_entry *inventory;
    size_t inventory_count;
    qa_unified_powerup_state *powerups;
    size_t powerup_count;
    qa_unified_ui_item *items;
    size_t item_count;
    qa_unified_weapon_status *weapon_status;
    qa_ammo_warning arsenal_warning;
    qa_item_id active_weapon, ammo_item;
    bool has_ammo, selected_arsenal;
    double ammo_count;
    qa_unified_native_inventory *native_inventory;
    qa_unified_q1_team_face *q1_team_face;
} qa_unified_player_ui;
typedef struct qa_unified_q2_hud_state {
    qa_unified_native_hud frame;
    int32_t player_number;
} qa_unified_q2_hud_state;
struct qa_unified_frame_player {
    qa_actor_id actor, ui_actor;
    qa_unified_player_view view;
    qa_unified_player_ui ui;
    qa_unified_client_presentation *client_presentation;
    bool has_q2_hud;
    qa_unified_q2_hud_state q2_hud;
};

#endif
