#ifndef QA_HUD_WHEEL_H
#define QA_HUD_WHEEL_H
#include "qa/font.h"
#include "qa/input.h"
#include "qa/gameplay.h"

typedef struct qa_hud_wheel qa_hud_wheel;
typedef enum qa_hud_wheel_mode { QA_HUD_WHEEL_WEAPONS, QA_HUD_WHEEL_POWERUPS } qa_hud_wheel_mode;
typedef struct qa_hud_wheel_identity {
    uint64_t key;
    qa_item_id item;
    int32_t source_ordinal;
} qa_hud_wheel_identity;
typedef struct qa_hud_wheel_item {
    qa_hud_wheel_identity identity;
    int32_t sort_order;
    const char *label;
    bool owned, has_ammunition, has_count;
    double count, warning_count;
    const qa_scene_image *icon, *selected_icon;
} qa_hud_wheel_item;
typedef struct qa_hud_wheel_options {
    uint32_t seat;
    float radius, selection_distance, fade_per_second;
    uint64_t carousel_timeout_ns, carousel_lock_ns;
    bool q2_slot_zero_deselect;
    void *context;
    /* Observations are read-only. Returned spans/images survive the current
     * wheel operation. Selection revalidates the actual actor/source/item and
     * invokes its ordinary arsenal/inventory path. Owners survive callbacks. */
    bool (*items)(void *, uint32_t seat, qa_hud_wheel_mode, const qa_hud_wheel_item **,
                   size_t *, qa_error *);
    bool (*active)(void *, uint32_t seat, uint64_t *key, qa_error *);
    bool (*select)(void *, uint32_t seat, qa_hud_wheel_mode, qa_hud_wheel_identity, qa_error *);
    void (*changed)(void *, uint32_t seat);
} qa_hud_wheel_options;
typedef struct qa_hud_wheel_command {
    bool holster, consume_attack;
    uint64_t lock_until_ns;
    float time_scale;
} qa_hud_wheel_command;
typedef struct qa_hud_wheel_status {
    bool open, visible, carousel_visible;
    qa_hud_wheel_mode mode;
    uint64_t selected, carousel_selected;
    float opacity;
    qa_vec2 cursor;
} qa_hud_wheel_status;
typedef struct qa_hud_wheel_draw_options {
    qa_scene_rect viewport;
    qa_font_selection fonts;
    const qa_scene_image *white;
    qa_scene_vec4 text, accent, disabled, panel;
    float scale;
    bool reduced_flashes;
} qa_hud_wheel_draw_options;
qa_hud_wheel_options qa_hud_wheel_defaults(uint32_t seat);
bool qa_hud_wheel_create(const qa_hud_wheel_options *, uint64_t now_ns, qa_hud_wheel **, qa_error *);
bool qa_hud_wheel_destroy(qa_hud_wheel *, qa_error *);
bool qa_hud_wheel_open(qa_hud_wheel *, qa_hud_wheel_mode, bool *opened, qa_error *);
bool qa_hud_wheel_close(qa_hud_wheel *, bool select, qa_error *);
bool qa_hud_wheel_input(qa_hud_wheel *, uint32_t seat, const qa_input_event *, bool *handled, qa_error *);
bool qa_hud_wheel_cycle(qa_hud_wheel *, int direction, uint64_t now_ns, qa_error *);
/* Run before the selected source encodes command button bits. */
bool qa_hud_wheel_prepare(qa_hud_wheel *, bool attack, uint64_t now_ns, qa_hud_wheel_command *, qa_error *);
bool qa_hud_wheel_update(qa_hud_wheel *, uint64_t now_ns, qa_error *);
/* Detached owner-thread state, also safe inside a read-only callback. */
bool qa_hud_wheel_read(const qa_hud_wheel *, qa_hud_wheel_status *);
bool qa_hud_wheel_round_ready(const qa_hud_wheel *);
bool qa_hud_wheel_draw(qa_hud_wheel *, const qa_hud_wheel_draw_options *, qa_scene_frame *, qa_error *);
#endif
