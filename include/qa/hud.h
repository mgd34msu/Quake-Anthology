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
    float crosshair_size; /* Zero retains the source default. */
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
typedef struct qa_hud_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    /* Returns an existing borrowed image from the shared saved image graph. */
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
} qa_hud_checkpoint_refs;
bool qa_hud_checkpoint(qa_hud *, const qa_hud_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_hud_restore(qa_bytes, const qa_hud_options *, const qa_hud_checkpoint_refs *, qa_hud **, qa_error *);
bool qa_hud_create(const qa_hud_options *, qa_hud **, qa_error *);
bool qa_hud_idle(const qa_hud *);
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
/* Retains the typed source status for the event's full actor and provider.
 * The actual physical-seat dispatcher qualifies the recipient before calling. */
bool qa_hud_ctf_status(qa_hud *, const qa_builtin_event *, qa_error *);
/* Source-wide capture event; recipient is separately proved by the actual
 * physical-seat dispatcher. The three-second deadline uses Q1 GAME time. */
bool qa_hud_ctf_capture(qa_hud *, const qa_builtin_event *, qa_actor_id recipient, qa_error *);
bool qa_hud_draw(qa_hud *, const qa_hud_frame *, qa_scene_frame *, qa_error *);
/* Literal already-localized captions use the actual seat UI fonts/preferences.
 * Area is a caller-owned display-pixel region; scale is its viewport fit.
 * This draws only the bounded caption panel between completed UI callbacks. */
bool qa_ui_captions_draw(qa_ui *,qa_scene_frame *,qa_scene_rect viewport,qa_scene_rect_f area,
    float scale,const qa_active_caption *,size_t,qa_error *);
#endif
