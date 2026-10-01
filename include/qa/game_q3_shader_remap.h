#ifndef QA_GAME_Q3_SHADER_REMAP_H
#define QA_GAME_Q3_SHADER_REMAP_H

#include "qa/session.h"

#define QA_Q3_SHADER_REMAPS 128u
#define QA_Q3_SHADER_PATH 64u
#define QA_Q3_SHADER_STATE_BYTES 4096u
#define QA_Q3_CS_SHADERSTATE 24u

typedef struct qa_q3_game qa_q3_game;
typedef struct qa_q3_shader_remap {
    char old_name[QA_Q3_SHADER_PATH], new_name[QA_Q3_SHADER_PATH];
    float time_offset;
} qa_q3_shader_remap;
typedef struct qa_q3_shader_remap_state {
    uint32_t count;
    qa_q3_shader_remap rows[QA_Q3_SHADER_REMAPS];
} qa_q3_shader_remap_state;

/* Source AddRemap retains the first old-name spelling, updates ASCII-folded
 * matches in place, and ignores new names after the 128th distinct row. */
bool qa_q3_shader_remap_add(qa_q3_game *, const char *old_name,
                            const char *new_name, float time_offset, qa_error *);
/* BuildShaderStateConfig uses the source QVM formatter and writes the actual
 * engine-owned CS_SHADERSTATE. Its ordinary notification may call game code. */
bool qa_q3_shader_remap_publish(qa_q3_game *, qa_error *);
bool qa_q3_shader_remap_apply(qa_q3_game *, const char *old_name,
                              const char *new_name, float time_offset, qa_error *);
float qa_q3_shader_remap_source_time(int32_t level_time_ms);
/* G_RemapTeamShaders takes the copied source vmCvar strings and actual ENTRY
 * clock. Base Quake III has no team-shader operation. */
bool qa_q3_shader_remap_teams(qa_q3_game *, const char *red_team,
                              const char *blue_team, int32_t level_time_ms, qa_error *);

#endif
