#ifndef QA_HUD_H
#define QA_HUD_H
#include "qa/ui.h"
#include "qa/captions.h"
#include "qa/material.h"

typedef struct qa_hud qa_hud;
typedef struct qa_hud_center_policy {
    /* Source reveal cadence and terminal fade; zero means no such effect. */
    uint64_t character_ns, fade_ns;
    uint32_t initial_characters, columns;
    /* Explicit 640x480 source placement; otherwise stock viewport placement. */
    int32_t y, character_width;
    bool instant, source_layout;
} qa_hud_center_policy;
typedef struct qa_hud_center_state {
    char text[1024];
    uint64_t starts_ns, duration_ns;
    uint32_t lines;
    qa_hud_center_policy policy;
} qa_hud_center_state;
typedef struct qa_hud_pickup_state {
    const char *text;
    const qa_scene_image *icon;
    uint64_t starts_ns, until_ns, blend_ns;
    qa_game_family family;
    uint32_t source_item;
} qa_hud_pickup_state;
typedef struct qa_hud_value {
    const char *label;
    double value, maximum;
    const qa_scene_image *icon;
    bool warning;
} qa_hud_value;
typedef struct qa_hud_timer {
    const char *label;
    const qa_scene_image *icon;
    uint64_t until_ns;
} qa_hud_timer;
typedef struct qa_hud_score {
    const char *name, *team;
    int32_t score, ping;
    bool local, spectator;
} qa_hud_score;
typedef struct qa_hud_team_face {
    const qa_scene_image *border;
    qa_vec4 top, bottom;
    int32_t score;
    bool alternate_digits;
} qa_hud_team_face;
typedef enum qa_hud_q1_variant {
    QA_HUD_Q1_BASE, QA_HUD_Q1_HIPNOTIC, QA_HUD_Q1_ROGUE
} qa_hud_q1_variant;
typedef struct qa_hud_q1_status {
    void *picture_context;
    const qa_scene_image *(*picture)(void *, const char *, qa_error *);
    const qa_scene_image *face;
    /* Optional 32-entry Source item-acquisition clock array, in original bit order. */
    const double *item_gettime;
    double seconds, view_size;
    uint32_t items, active_weapon, armor, ammo_count, ammunition[4];
    int32_t health, total_secrets, total_monsters, found_secrets, killed_monsters;
    const char *level;
    qa_hud_q1_variant variant;
    bool present, deathmatch, quakeworld, overlay_status, hud_swap, intermission, reduced_flashes;
} qa_hud_q1_status;
typedef struct qa_hud_q1_placement {
    float x, y, scale;
    uint32_t lines, reserved;
} qa_hud_q1_placement;
/* One source-pixel transform for stock pictures and world-view reservation. */
qa_hud_q1_placement qa_hud_q1_place(qa_scene_rect, float scale, double view_size,
    bool overlay_status, bool intermission, bool deathmatch);
typedef struct qa_hud_frame {
    uint32_t seat;
    qa_actor_id actor;
    uint64_t time_ns;
    qa_scene_rect viewport, safe_area; /* World view and full seat UI region. */
    float scale;
    bool show_scores, show_inventory, visible, weapon_only, source_status_native;
    bool center_owned; /* A source HUD owns centerprint layout and visibility. */
} qa_hud_frame;
typedef struct qa_hud_weapon {
    const char *label;
    const qa_material *icon;
    double ammo_count;
    bool present, finite_ammo, has_ammo_to_start, low_ammo;
    bool native_status, suppress_active_warning, aggregate_low, aggregate_empty;
} qa_hud_weapon;
/* Providers return borrowed source data for this draw only. Canonical health,
 * armor and inventory are read by the common HUD unless source_vitals is set.
 * Providers and source draws may inspect but must not mutate the application,
 * HUD or controller during this call. Source draws retain
 * Q1/Q2/Q3 and rerelease stat/layout behavior alongside shared overlays. */
typedef struct qa_hud_data {
    const qa_hud_value *vitals, *bars;
    qa_hud_value source_values[2];
    size_t vital_count, bar_count;
    const qa_hud_timer *timers;
    size_t timer_count;
    const qa_hud_score *scores;
    size_t score_count;
    const qa_scene_image *crosshair, *health_icon;
    qa_hud_team_face health_team_face;
    qa_hud_q1_status q1;
    qa_vec4 crosshair_color;
    float crosshair_size; /* Zero retains the source default. */
    bool crosshair_visible, source_vitals;
    qa_item_id selected_weapon;
    qa_hud_weapon weapon;
    const char *help_title;
    const char *const *help_lines;
    size_t help_count;
    const qa_active_caption *captions;
    size_t caption_count;
} qa_hud_data;
typedef struct qa_hud_options {
    qa_ui *ui;
    qa_application *application;
    uint32_t seat;
    void *context;
    bool (*read)(void *, const qa_hud_frame *, qa_hud_data *, qa_error *);
    /* Borrowed seat typography/preferences for this draw; menu presentation survives. */
    bool (*presentation)(void *, qa_ui_presentation *, qa_error *);
    bool (*source_draw)(void *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
    const qa_scene_image *(*video_frame)(void *, uint64_t, double, qa_error *);
    void *video_context;
} qa_hud_options;
bool qa_hud_create(const qa_hud_options *, qa_hud **, qa_error *);
bool qa_hud_idle(const qa_hud *);
bool qa_hud_destroy(qa_hud *, qa_error *);
/* One current center record per seat, replaced as SCR_CenterPrint/CG_CenterPrint.
 * The fixed source-sized text buffer retains no borrowed module memory. */
bool qa_hud_notify(qa_hud *, const char *, bool chat, uint64_t starts_ns,
                    uint64_t duration_ns, qa_error *);
bool qa_hud_center_print(qa_hud *, const char *, uint64_t starts_ns, uint64_t duration_ns,
                          qa_hud_center_policy, qa_error *);
const qa_hud_center_state *qa_hud_center_read(const qa_hud *);
size_t qa_hud_center_length(const qa_hud_center_state *, uint64_t time_ns);
float qa_hud_center_alpha(const qa_hud_center_state *, uint64_t time_ns, uint64_t duration_ns);
bool qa_hud_clear_notify(qa_hud *, qa_error *);
bool qa_hud_clear_center(qa_hud *, qa_error *);
bool qa_hud_pickup(qa_hud *, const qa_hud_pickup_state *, qa_error *);
const qa_hud_pickup_state *qa_hud_pickup_read(const qa_hud *);
void qa_hud_clear_pickup(qa_hud *);
void qa_hud_pickup_clear_time(qa_hud *);
void qa_hud_hit_marker(qa_hud *, float damage, uint64_t until_ns);
/* Retains the typed source status for the event's full actor and provider.
 * The actual physical-seat dispatcher qualifies the recipient before calling. */
bool qa_hud_ctf_status(qa_hud *, const qa_builtin_event *, qa_error *);
/* Source-wide capture event; recipient is separately proved by the actual
 * physical-seat dispatcher. The three-second deadline uses Q1 GAME time. */
bool qa_hud_ctf_capture(qa_hud *, const qa_builtin_event *, qa_actor_id recipient, qa_error *);
bool qa_hud_draw(qa_hud *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
/* Layout providers borrow the physical player's state for this draw. Content
 * leaves retained messages untouched; the messages phase draws/expires them once. */
bool qa_hud_draw_content(qa_hud *, const qa_hud_options *, const qa_hud_frame *,
    qa_scene_frame *, qa_error *);
bool qa_hud_draw_messages(qa_hud *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
/* Literal already-localized captions use the actual seat UI fonts/preferences.
 * Area is a caller-owned display-pixel region; scale is its viewport fit.
 * This draws only the bounded caption panel between completed UI callbacks. */
bool qa_ui_captions_draw(qa_ui *,qa_scene_frame *,qa_scene_rect viewport,qa_scene_rect_f area,
    float scale,const qa_active_caption *,size_t,qa_error *);
#endif
