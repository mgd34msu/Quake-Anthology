#ifndef QA_HUD_Q2_H
#define QA_HUD_Q2_H
#include "qa/font.h"
#include "qa/localization.h"
#include "qa/network_q2.h"

typedef struct qa_hud_q2_item {
    const char *label;
    double count;
    bool selected;
} qa_hud_q2_item;
typedef struct qa_hud_q2_arsenal {
    bool has_ammunition, unlimited_ammunition, has_selected_item, has_inventory;
    double ammunition;
    const qa_scene_image *ammunition_icon, *selected_icon;
    float ammunition_aspect, selected_aspect;
    const char *selected_label, *selected_localized_label;
    const qa_hud_q2_item *inventory;
    size_t inventory_count;
} qa_hud_q2_arsenal;
typedef struct qa_hud_q2_frame {
    qa_net_protocol_id protocol;
    const int16_t *stats;
    size_t stat_count;
    const int32_t *inventory;
    size_t inventory_count;
    const char *layout;
    int32_t player_number, server_frame;
    uint64_t time_ns, frame_ns;
    const qa_hud_q2_arsenal *arsenal;
} qa_hud_q2_frame;
/* Source tables persist between status/overlay layout calls in one frame.
 * Zero initialization is valid. They belong to one seat; rerelease cells retain
 * the source 23-byte crop, repairing a truncated UTF-8 sequence afterward. */
typedef struct qa_hud_q2_table {
    char cells[11][6][70];
    float columns[5];
    size_t row_count, column_count;
} qa_hud_q2_table;
typedef struct qa_hud_q2_options {
    qa_scene_rect viewport;
    float scale;
    qa_font_selection fonts;
    const qa_scene_image *white;
    const qa_localization *localization;
    bool use_font;
    float font_line_height;
    qa_hud_q2_table *table;
    void *context;
    /* Read-only callbacks; each returned string/image is borrowed through its
     * immediate observation. NULL means absent. Frame spans, fonts, localization
     * and source owners remain immutable and alive through the whole draw. */
    const char *(*configstring)(void *, int32_t index);
    const qa_scene_image *(*picture)(void *, const char *name, qa_error *);
    const char *(*binding)(void *, const char *use_command);
} qa_hud_q2_options;
bool qa_hud_q2_layout(const qa_hud_q2_options *, const qa_hud_q2_frame *, const char *source,
                      qa_scene_frame *, qa_error *);
bool qa_hud_q2_draw(const qa_hud_q2_options *, const qa_hud_q2_frame *, bool overlay_only,
                    qa_scene_frame *, qa_error *);
#endif
