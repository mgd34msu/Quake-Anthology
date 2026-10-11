#ifndef QA_HUD_Q2_H
#define QA_HUD_Q2_H
#include "qa/font.h"
#include "qa/localization.h"
#include "qa/network_q2.h"
#include "qa/console.h"

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
typedef struct qa_hud_q2_program qa_hud_q2_program;
typedef struct qa_hud_q2_picture_names {
    qa_string_id digits[2][11], field, ping, inventory, conchars, marker, player_icon, tag;
} qa_hud_q2_picture_names;
bool qa_hud_q2_picture_names_bind(qa_strings *, qa_hud_q2_picture_names *, qa_error *);
bool qa_hud_q2_player_picture_names(qa_strings *, const char *, qa_string_id *, qa_string_id *, qa_error *);
typedef struct qa_hud_q2_player_names { qa_bytes name, appearance, tag; } qa_hud_q2_player_names;
qa_hud_q2_player_names qa_hud_q2_player_names_read(const char *);
bool qa_hud_q2_player_icon_path(qa_bytes appearance, char *, size_t, qa_error *);
enum { QA_HUD_Q2_LAYOUT_BYTES = 65536, QA_HUD_Q2_LAYOUT_SCRATCH_BYTES = 16 * QA_HUD_Q2_LAYOUT_BYTES };
typedef struct qa_hud_q2_names {
    qa_strings *strings;
    qa_q2_config_layout layout;
    qa_net_protocol_id protocol;
    qa_hud_q2_program *status;
    qa_string_id values[QA_Q2_MAX_CONFIGSTRINGS];
    qa_string_id appearances[256], icons[256], tags[256];
    qa_string_id default_icon, default_tag;
    qa_scene_resources *images;
    uint64_t image_generation;
    qa_scene_filter filter;
    bool configured, rerelease, images_ready;
} qa_hud_q2_names;
bool qa_hud_q2_names_prepare_images(qa_hud_q2_names *,qa_scene_resources *,const qa_hud_q2_picture_names *,qa_scene_filter,qa_error *);
bool qa_hud_q2_program_prepare_images(qa_hud_q2_program *,qa_scene_resources *,bool,qa_scene_filter,qa_error *);
bool qa_hud_q2_names_init(qa_hud_q2_names *, qa_strings *, qa_error *);
bool qa_hud_q2_names_configure(qa_hud_q2_names *, qa_net_protocol_id, qa_error *);
bool qa_hud_q2_names_set(qa_hud_q2_names *, uint32_t, qa_string_id, qa_error *);
qa_string_id qa_hud_q2_names_picture(const qa_hud_q2_names *, int32_t, unsigned variant);
void qa_hud_q2_names_clear(qa_hud_q2_names *);
void qa_hud_q2_names_destroy(qa_hud_q2_names *);
bool qa_hud_q2_player_icon(qa_strings *, const char *appearance, size_t, qa_string_id *, qa_error *);

bool qa_hud_q2_program_create(qa_strings *, qa_hud_q2_program **, qa_error *);
bool qa_hud_q2_program_update(qa_hud_q2_program *, const char *, bool rerelease, qa_error *);
void qa_hud_q2_program_clear(qa_hud_q2_program *);
void qa_hud_q2_program_destroy(qa_hud_q2_program *);
typedef struct qa_hud_q2_marker_cvars {
    qa_cvar_handle mode, crosshair, duration, scale, alpha, x, y;
} qa_hud_q2_marker_cvars;
qa_hud_q2_marker_cvars qa_hud_q2_marker_bind(const qa_cvars *);
typedef struct qa_hud_q2_marker {
    const qa_scene_image *image;
    uint64_t now_ns, started_ns;
    uint32_t layout_flags;
    const qa_cvar_view *mode, *crosshair, *duration, *scale, *alpha, *x, *y;
} qa_hud_q2_marker;
void qa_hud_q2_marker_settings(const qa_cvars *, const qa_hud_q2_marker_cvars *, qa_hud_q2_marker *);
bool qa_hud_q2_marker_draw(const qa_hud_q2_marker *, uint32_t *, qa_scene_rect, qa_scene_frame *, qa_error *);
typedef struct qa_hud_q2_frame {
    qa_net_protocol_id protocol;
    const int16_t *stats;
    size_t stat_count;
    const int32_t *inventory;
    size_t inventory_count;
    const qa_hud_q2_program *layout;
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
    const qa_hud_q2_program *status;
    const qa_hud_q2_picture_names *picture_names;
    void *context;
    /* Read-only callbacks; each returned string/image is borrowed through its
     * immediate observation. NULL means absent. Frame spans, fonts, localization
     * and source owners remain immutable and alive through the whole draw. */
    const char *(*configstring)(void *, int32_t index);
    const qa_scene_image *(*picture)(void *, qa_string_id name, qa_error *);
    qa_string_id (*config_picture)(void *, int32_t index, unsigned variant);
    const char *(*binding)(void *, const char *use_command);
} qa_hud_q2_options;
typedef struct qa_hud_q2_stat_references {
    uint64_t images, configstrings;
} qa_hud_q2_stat_references;
/* Reads every conditional branch with the draw parser. Resets exclusive scratch
 * on entry and return; the returned masks retain no scratch pointers. */
bool qa_hud_q2_layout_stat_references(const char *, bool rerelease, qa_arena *scratch,
    qa_hud_q2_stat_references *, qa_error *);
bool qa_hud_q2_draw(const qa_hud_q2_options *, const qa_hud_q2_frame *, bool overlay_only,
                    qa_scene_frame *, qa_error *);
#endif
