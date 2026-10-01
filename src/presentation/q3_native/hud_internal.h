#ifndef QA_Q3_NATIVE_HUD_INTERNAL_H
#define QA_Q3_NATIVE_HUD_INTERNAL_H
#include "hud.h"
#include "player_state_internal.h"
#include "qa/game_q3_configstrings.h"

struct q3n_hud {
    q3n_hud_options options;
    const qa_q3_game *source_game;
    qa_q3_product product;
    q3n_hud_state state;
    int32_t previous_times[4], previous_milliseconds, fps_index, scores_request_time;
    int32_t frame_samples[128], frame_count, snapshot_samples[128], snapshot_flags[128], snapshot_count;
    int32_t oldest_command_time;
    bool has_oldest_command, busy;
};
typedef struct q3n_hud_draw {
    q3n_hud *owner;
    const q3n_frame *frame;
    const q3n_hud_settings *settings;
    q3n_server_commands *commands;
    q3n_player_state *player;
    qa_scene_rect viewport;
    qa_error *error;
    float anchor_x, anchor_y;
    qa_ui_presentation typography;
    qa_scene_frame *scene;
} q3n_hud_draw;
bool q3nh_preferences(q3n_hud_draw *);
void q3nh_anchor(q3n_hud_draw *,float x,float y);
qa_scene_rect_f q3nh_rect(const q3n_hud_draw *,qa_scene_rect_f);
void q3nh_palette(const q3n_hud_draw *,const float input[4],float output[4]);
bool q3nh_font_metric(q3n_hud_draw *,const char *,float height,int32_t limit,
    float *width,float *out_height,bool *handled);
bool q3nh_font_text(q3n_hud_draw *,float x,float y,const char *,float height,const float color[4],
    bool force,bool shadow,int32_t limit,qa_font_alignment,bool baseline,bool *handled);
bool q3nh_width(q3n_hud_draw *,const char *,float width,float height,int32_t limit,float *);
bool q3nh_current(q3n_hud *,const q3n_frame *,qa_error *);
size_t q3nh_strlen(const char *);
bool q3nh_fade(int32_t time,int32_t start,int32_t duration,float color[4]);
void q3nh_health(int32_t health,int32_t armor,float color[4]);
bool q3nh_color(q3n_hud_draw *,const float color[4]);
bool q3nh_picture(q3n_hud_draw *,float x,float y,float width,float height,int32_t);
bool q3nh_pixels(q3n_hud_draw *,float x,float y,float width,float height,int32_t,qa_scene_vec4 uv);
bool q3nh_fill(q3n_hud_draw *,float x,float y,float width,float height,const float color[4]);
bool q3nh_text(q3n_hud_draw *,float x,float y,const char *,float width,float height,
    const float color[4],bool force,bool shadow,int32_t limit);
bool q3nh_big(q3n_hud_draw *,float x,float y,const char *,float alpha);
bool q3nh_center(q3n_hud_draw *,float y,const char *,float alpha);
bool q3nh_right(q3n_hud_draw *,float x,float y,const char *,float alpha);
bool q3nh_field(q3n_hud_draw *,float x,float y,int32_t width,int32_t value);
bool q3nh_model(q3n_hud_draw *,float x,float y,float width,float height,
    int32_t model,int32_t skin,qa_vec3 origin,qa_vec3 angles);
bool q3nh_head(q3n_hud_draw *,float x,float y,float width,float height,int32_t client,qa_vec3 angles);
bool q3nh_flag(q3n_hud_draw *,float x,float y,float width,float height,int32_t team,bool force_2d);
bool q3nh_team_background(q3n_hud_draw *,float x,float y,float width,float height,float alpha,int32_t team);
bool q3nh_scoreboard(q3n_hud_draw *,bool *showing);
bool q3nh_tourney(q3n_hud_draw *);
bool q3nh_corners(q3n_hud_draw *);
bool q3nh_team_chat(q3n_hud_draw *);
bool q3nh_sound(q3n_hud_draw *,q3n_sound,int32_t channel);
bool q3nh_location(q3n_hud_draw *,int32_t,const char **);
extern const float q3nh_white[4],q3nh_normal[4],q3nh_red[4];
#endif
