#ifndef QA_HUD_CONTROLS_H
#define QA_HUD_CONTROLS_H

#include "qa/console.h"

typedef struct qa_hud_cvar_handles {
    qa_cvar_handle language, use_font, center_time, draw_gun;
    qa_cvar_handle deathmatch, swap, teamplay;
} qa_hud_cvar_handles;

enum {
    QA_HUD_CVAR_LANGUAGE = 1u << 0,
    QA_HUD_CVAR_USE_FONT = 1u << 1,
    QA_HUD_CVAR_CENTER_TIME = 1u << 2,
    QA_HUD_CVAR_DRAW_GUN = 1u << 3,
    QA_HUD_CVAR_DEATHMATCH = 1u << 4,
    QA_HUD_CVAR_SWAP = 1u << 5,
    QA_HUD_CVAR_TEAMPLAY = 1u << 6
};

static inline void qa_hud_cvars_bind(const qa_cvars *registry, unsigned fields,
    qa_hud_cvar_handles *out)
{
    *out = (qa_hud_cvar_handles){
        .language = fields & QA_HUD_CVAR_LANGUAGE ? qa_cvars_resolve(registry, "language") : (qa_cvar_handle){0},
        .use_font = fields & QA_HUD_CVAR_USE_FONT ? qa_cvars_resolve(registry, "scr_usekfont") : (qa_cvar_handle){0},
        .center_time = fields & QA_HUD_CVAR_CENTER_TIME ? qa_cvars_resolve(registry, "scr_centertime") : (qa_cvar_handle){0},
        .draw_gun = fields & QA_HUD_CVAR_DRAW_GUN ? qa_cvars_resolve(registry, "cg_drawGun") : (qa_cvar_handle){0},
        .deathmatch = fields & QA_HUD_CVAR_DEATHMATCH ? qa_cvars_resolve(registry, "deathmatch") : (qa_cvar_handle){0},
        .swap = fields & QA_HUD_CVAR_SWAP ? qa_cvars_resolve(registry, "cl_hudswap") : (qa_cvar_handle){0},
        .teamplay = fields & QA_HUD_CVAR_TEAMPLAY ? qa_cvars_resolve(registry, "teamplay") : (qa_cvar_handle){0},
    };
}

#endif
