#ifndef QA_HUD_H
#define QA_HUD_H
#include "qa/ui.h"
#include "qa/captions.h"

typedef struct qa_hud qa_hud;
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
typedef struct qa_hud_frame {
    uint32_t seat;
    qa_actor_id actor;
    uint64_t time_ns;
    qa_scene_rect viewport, safe_area;
    float scale;
    bool show_scores, show_inventory, visible;
} qa_hud_frame;
/* Providers return borrowed source data for this draw only. Canonical health,
 * armor and inventory are read by the common HUD unless source_vitals is set.
 * Providers and source draws may inspect but must not mutate the application,
 * HUD or controller during this call. Source draws retain
 * Q1/Q2/Q3 and rerelease stat/layout behavior alongside shared overlays. */
typedef struct qa_hud_data {
    const qa_hud_value *vitals, *bars;
    size_t vital_count, bar_count;
    const qa_hud_timer *timers;
    size_t timer_count;
    const qa_hud_score *scores;
    size_t score_count;
    const qa_scene_image *crosshair;
    qa_scene_vec4 crosshair_color;
    bool crosshair_visible, source_vitals;
    qa_item_id selected_weapon;
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
    bool (*source_draw)(void *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
} qa_hud_options;
bool qa_hud_create(const qa_hud_options *, qa_hud **, qa_error *);
bool qa_hud_destroy(qa_hud *, qa_error *);
/* Message text is copied. Instant center prints replace the queue; slow prints
 * retain their source reveal interval and queue behind earlier prints. */
bool qa_hud_notify(qa_hud *, const char *, bool chat, uint64_t starts_ns,
                    uint64_t duration_ns, qa_error *);
bool qa_hud_center_print(qa_hud *, const char *, uint64_t starts_ns, uint64_t duration_ns,
                          bool instant, uint64_t character_ns, qa_error *);
bool qa_hud_clear_notify(qa_hud *, qa_error *);
bool qa_hud_clear_center(qa_hud *, qa_error *);
bool qa_hud_pickup(qa_hud *, const char *, const qa_scene_image *, uint64_t until_ns, qa_error *);
void qa_hud_hit_marker(qa_hud *, float damage, uint64_t until_ns);
bool qa_hud_draw(qa_hud *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
#endif
