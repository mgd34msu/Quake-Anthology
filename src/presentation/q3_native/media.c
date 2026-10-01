#include "media.h"
#include "remote_frame.h"
#include "../q3/internal.h"
#include "qa/q3_assets_save.h"
#include "qa/source_save.h"
#include <stdio.h>

struct q3n_media {
    q3n_media_options options;
    q3n_media_view view;
    q3n_inline_media *inline_models;
    uint64_t model_revision[256], sound_revision[256];
    bool model_observed[256], sound_observed[256];
    bool loading_graphics, sounds_loaded, graphics_loaded, effects_loaded, busy;
};

static bool enter(q3n_media *m, qa_error *error)
{
    if (!m || m->busy || !qa_q3_assets_idle(m->options.assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Native Q3 media requires its idle real registry");
    m->busy = true;
    return true;
}
static bool leave(q3n_media *m, bool okay) { m->busy = false; return okay; }
bool q3n_media_idle(const q3n_media *m) { return m && !m->busy; }
const q3n_media_view *q3n_media_read(const q3n_media *m)
{ return q3n_media_idle(m) ? &m->view : NULL; }
qa_q3_presentation_assets *q3n_media_assets(const q3n_media *m)
{ return m ? m->options.assets : NULL; }
bool q3n_media_remote_current(const q3n_media *m,const q3n_remote_source_view *source,qa_error *e)
{
    return q3n_media_idle(m) && source && m->options.remote_source==source->owner &&
        m->options.product==source->basis.product && m->sounds_loaded && m->graphics_loaded &&
        q3n_remote_source_current(source) ? true :
        q3p_fail(e,QA_ERROR_ARGUMENT,"Remote media requires its fully registered actual reached CLIENT owner");
}
bool q3n_media_effects_ready(const q3n_media *m)
{ return q3n_media_idle(m) && m->effects_loaded; }
bool q3n_media_loading_read(const q3n_media *m,q3n_loading_media *out,qa_error *e)
{
    if (!m || !out) return q3p_fail(e,QA_ERROR_ARGUMENT,"Native loading observation requires its actual media owner");
    *out=(q3n_loading_media){.product=m->options.product,.assets=m->options.assets,
        .remote_source=m->options.remote_source,
        .proportional=m->view.graphics[Q3N_G_CHARSET_PROP],.initialized=m->loading_graphics};
    return true;
}
bool q3n_media_create(const q3n_media_options *options, q3n_media **out, qa_error *error)
{
    if (!options || !options->assets || !out ||
        (options->product != QA_Q3_ARENA && options->product != QA_Q3_TEAM_ARENA))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Native Q3 media needs its actual product and registry");
    if (options->remote_source) {
        q3n_remote_source_view source;
        if (!q3n_remote_source_read(options->remote_source,&source,error) || source.basis.product!=options->product)
            return q3p_fail(error,QA_ERROR_ARGUMENT,"Remote media constructor has another actual source product");
    }
    q3n_media *m = calloc(1, sizeof(*m));
    if (!m) return q3p_fail(error, QA_ERROR_MEMORY, "Allocating native Q3 media");
    m->options = *options;
    m->view.product = options->product;
    qa_q3_items(options->product, &m->view.item_count);
    m->inline_models = calloc(1, sizeof(*m->inline_models));
    if (!m->inline_models || m->view.item_count > 256) {
        free(m->inline_models); free(m);
        return q3p_fail(error, QA_ERROR_MEMORY, "Allocating native Q3 inline media");
    }
    m->view.inline_count = 1; m->view.inline_models = m->inline_models;
    for (uint32_t i = 0; i < 16; ++i) m->view.weapons[i].item_index = -1;
    *out = m;
    return true;
}
void q3n_media_destroy(q3n_media *m)
{ if (q3n_media_idle(m)) { free(m->inline_models); free(m); } }

static bool model(q3n_media *m, const char *path, int32_t *out, qa_error *e)
{ return qa_q3_register_model(m->options.assets, path, out, e); }
static bool shader(q3n_media *m, const char *path, bool mipmap, int32_t *out, qa_error *e)
{ return qa_q3_register_shader(m->options.assets, path, mipmap, out, e); }
static bool sound(q3n_media *m, const char *path, bool compressed, int32_t *out, qa_error *e)
{ return qa_q3_register_sound(m->options.assets, path, compressed, out, e); }
static float midpoint(float minimum, float maximum)
{
    volatile float extent = maximum - minimum;
    volatile float half = 0.5f * extent;
    return minimum + half;
}
static bool model_midpoint(q3n_media *m, int32_t handle, qa_vec3 *out, qa_error *e)
{
    qa_bounds bounds;
    if (!qa_q3_presentation_model_bounds(m->options.assets, handle, &bounds, e)) return false;
    *out = qa_v3(midpoint(bounds.mins.x, bounds.maxs.x), midpoint(bounds.mins.y, bounds.maxs.y), midpoint(bounds.mins.z, bounds.maxs.z));
    return true;
}
static bool weapon_now(q3n_media *, uint32_t, qa_error *);
static bool item_now(q3n_media *m, uint32_t number, qa_error *e)
{
    if (number >= m->view.item_count)
        return q3p_fail(e, QA_ERROR_FORMAT, "CG_RegisterItemVisuals: itemNum out of range");
    q3n_item_media *visual = &m->view.items[number];
    if (visual->registered) return true;
    visual->registered = true;
    const qa_q3_item *item = qa_q3_items(m->options.product, NULL) + number;
    int32_t item_model=0,item_icon=0;
    if (!model(m, item->model, &item_model, e) ||
        (item->icon && !shader(m, item->icon, true, &item_icon, e))) return false;
    visual->models[0]=item_model; visual->icon=item_icon;
    if (item->kind == QA_Q3_ITEM_WEAPON && !weapon_now(m, (uint32_t)item->tag, e)) return false;
    if ((item->kind == QA_Q3_ITEM_POWERUP || item->kind == QA_Q3_ITEM_HEALTH ||
         item->kind == QA_Q3_ITEM_ARMOR || item->kind == QA_Q3_ITEM_HOLDABLE) && item->secondary_model &&
        !model(m, item->secondary_model, &visual->models[1], e)) return false;
    return true;
}
static bool flash_sound(q3n_media *m, q3n_weapon_media *w, const char *path, qa_error *e)
{ return sound(m, path, false, &w->flash_sounds[0], e); }
static bool specific(q3n_media *m, uint32_t number, q3n_weapon_media *w, qa_error *e)
{
    int32_t *g = m->view.graphics, *s = m->view.sounds;
    switch (number) {
    case 1:
        w->flash_light_color = qa_v3(.6f, .6f, 1);
        return sound(m, "sound/weapons/melee/fstrun.wav", false, &w->firing_sound, e) &&
            flash_sound(m, w, "sound/weapons/melee/fstatck.wav", e);
    case 6: {
        w->flash_light_color = qa_v3(.6f, .6f, 1);
        if (!(sound(m, "sound/weapons/melee/fsthum.wav", false, &w->ready_sound, e) &&
            sound(m, "sound/weapons/lightning/lg_hum.wav", false, &w->firing_sound, e) &&
            flash_sound(m, w, "sound/weapons/lightning/lg_fire.wav", e) &&
            shader(m, "lightningBoltNew", true, &g[Q3N_G_LIGHTNING_SHADER], e) &&
            model(m, "models/weaphits/crackle.md3", &g[Q3N_G_LIGHTNING_EXPLOSION], e))) return false;
        int32_t hits[3];
        if (!sound(m,"sound/weapons/lightning/lg_hit.wav",false,&hits[0],e) ||
            !sound(m,"sound/weapons/lightning/lg_hit2.wav",false,&hits[1],e) ||
            !sound(m,"sound/weapons/lightning/lg_hit3.wav",false,&hits[2],e)) return false;
        s[Q3N_S_LIGHTNING_HIT1]=hits[0]; s[Q3N_S_LIGHTNING_HIT2]=hits[1]; s[Q3N_S_LIGHTNING_HIT3]=hits[2]; return true;
    }
    case 10:
        if (!shader(m, "lightningBoltNew", true, &g[Q3N_G_LIGHTNING_SHADER], e)) return false;
        w->flash_light_color = qa_v3(.6f, .6f, 1);
        if (!model(m, "models/ammo/rocket/rocket.md3", &w->missile_model, e)) return false;
        w->trail = Q3N_TRAIL_GRAPPLE; w->missile_light = 200; w->trail_time = 2000; w->trail_radius = 64;
        w->missile_light_color = qa_v3(1, .75f, 0);
        return sound(m, "sound/weapons/melee/fsthum.wav", false, &w->ready_sound, e) &&
            sound(m, "sound/weapons/melee/fstrun.wav", false, &w->firing_sound, e);
    case 13:
        if (!sound(m, "sound/weapons/vulcan/wvulfire.wav", false, &w->firing_sound, e)) return false;
        w->loop_fire_sound = true;
        /* Flash registration below preserves the four source sound choices. */
        /* fall through */
    case 2: {
        w->flash_light_color = qa_v3(1, 1, 0);
        const char *const machine[4] = {"sound/weapons/machinegun/machgf1b.wav", "sound/weapons/machinegun/machgf2b.wav",
            "sound/weapons/machinegun/machgf3b.wav", "sound/weapons/machinegun/machgf4b.wav"};
        const char *const chain[4] = {"sound/weapons/vulcan/vulcanf1b.wav", "sound/weapons/vulcan/vulcanf2b.wav",
            "sound/weapons/vulcan/vulcanf3b.wav", "sound/weapons/vulcan/vulcanf4b.wav"};
        int32_t flashes[4];
        for (size_t i = 0; i < 4; ++i)
            if (!sound(m, number == 13 ? chain[i] : machine[i], false, &flashes[i], e)) return false;
        memcpy(w->flash_sounds,flashes,sizeof(flashes));
        w->eject_brass = Q3N_BRASS_MACHINEGUN;
        return shader(m, "bulletExplosion", true, &g[Q3N_G_BULLET_EXPLOSION], e);
    }
    case 3:
        w->flash_light_color = qa_v3(1, 1, 0);
        if (!flash_sound(m, w, "sound/weapons/shotgun/sshotf1b.wav", e)) return false;
        w->eject_brass = Q3N_BRASS_SHOTGUN; return true;
    case 5:
        if (!model(m, "models/ammo/rocket/rocket.md3", &w->missile_model, e) ||
            !sound(m, "sound/weapons/rocket/rockfly.wav", false, &w->missile_sound, e)) return false;
        w->trail = Q3N_TRAIL_ROCKET; w->missile_light = 200; w->trail_time = 2000; w->trail_radius = 64;
        w->missile_light_color = w->flash_light_color = qa_v3(1, .75f, 0);
        return flash_sound(m, w, "sound/weapons/rocket/rocklf1a.wav", e) &&
            shader(m, "rocketExplosion", true, &g[Q3N_G_ROCKET_EXPLOSION], e);
    case 12:
    case 4:
        if (!model(m, number == 12 ? "models/weaphits/proxmine.md3" : "models/ammo/grenade1.md3", &w->missile_model, e)) return false;
        w->trail = Q3N_TRAIL_GRENADE; w->trail_time = 700; w->trail_radius = 32;
        w->flash_light_color = qa_v3(1, .7f, 0);
        return flash_sound(m, w, number == 12 ? "sound/weapons/proxmine/wstbfire.wav" : "sound/weapons/grenade/grenlf1a.wav", e) &&
            shader(m, "grenadeExplosion", true, &g[Q3N_G_GRENADE_EXPLOSION], e);
    case 11:
        w->eject_brass = Q3N_BRASS_NAILGUN; w->trail = Q3N_TRAIL_NAIL; w->trail_radius = 16; w->trail_time = 250;
        if (!model(m, "models/weaphits/nail.md3", &w->missile_model, e)) return false;
        w->flash_light_color = qa_v3(1, .75f, 0);
        return flash_sound(m, w, "sound/weapons/nailgun/wnalfire.wav", e);
    case 8:
        w->trail = Q3N_TRAIL_PLASMA;
        if (!sound(m, "sound/weapons/plasma/lasfly.wav", false, &w->missile_sound, e)) return false;
        w->flash_light_color = qa_v3(.6f, .6f, 1);
        return flash_sound(m, w, "sound/weapons/plasma/hyprbf1a.wav", e) &&
            shader(m, "plasmaExplosion", true, &g[Q3N_G_PLASMA_EXPLOSION], e) &&
            shader(m, "railDisc", true, &g[Q3N_G_RAIL_RINGS], e);
    case 7:
        if (!sound(m, "sound/weapons/railgun/rg_hum.wav", false, &w->ready_sound, e)) return false;
        w->flash_light_color = qa_v3(1, .5f, 0);
        return flash_sound(m, w, "sound/weapons/railgun/railgf1a.wav", e) &&
            shader(m, "railExplosion", true, &g[Q3N_G_RAIL_EXPLOSION], e) &&
            shader(m, "railDisc", true, &g[Q3N_G_RAIL_RINGS], e) &&
            shader(m, "railCore", true, &g[Q3N_G_RAIL_CORE], e);
    case 9:
        if (!sound(m, "sound/weapons/bfg/bfg_hum.wav", false, &w->ready_sound, e)) return false;
        w->flash_light_color = qa_v3(1, .7f, 1);
        return flash_sound(m, w, "sound/weapons/bfg/bfg_fire.wav", e) &&
            shader(m, "bfgExplosion", true, &g[Q3N_G_BFG_EXPLOSION], e) &&
            model(m, "models/weaphits/bfg.md3", &w->missile_model, e) &&
            sound(m, "sound/weapons/rocket/rockfly.wav", false, &w->missile_sound, e);
    default:
        w->flash_light_color = qa_v3(1, 1, 1);
        return flash_sound(m, w, "sound/weapons/rocket/rocklf1a.wav", e);
    }
}
static bool derived_model(q3n_media *m, const char *path, const char *suffix, int32_t *out, qa_error *e)
{
    const char *dot = strchr(path, '.'); size_t length = dot ? (size_t)(dot - path) : strlen(path);
    size_t tail = strlen(suffix);
    if (length > SIZE_MAX - tail - 1) return q3p_fail(e, QA_ERROR_MEMORY, "Native weapon model path extent overflow");
    char *name = malloc(length + tail + 1);
    if (!name) return q3p_fail(e, QA_ERROR_MEMORY, "Allocating native weapon model path");
    memcpy(name, path, length); memcpy(name + length, suffix, tail + 1);
    bool okay = model(m, name, out, e); free(name); return okay;
}
static bool weapon_now(q3n_media *m, uint32_t number, qa_error *e)
{
    if (number >= 16) return q3p_fail(e, QA_ERROR_ARGUMENT, "Invalid weapon media index");
    q3n_weapon_media *w = &m->view.weapons[number];
    if (!number || w->registered) return true;
    w->registered = true;
    const qa_q3_item *items = qa_q3_items(m->options.product, NULL);
    uint32_t index;
    for (index = 0; index < m->view.item_count; ++index)
        if (items[index].kind == QA_Q3_ITEM_WEAPON && items[index].tag == (int32_t)number) break;
    if (index == m->view.item_count) return q3p_fail(e, QA_ERROR_NOT_FOUND, "Couldn't find weapon");
    w->item_index = (int32_t)index;
    if (!item_now(m, index, e)) return false;
    const qa_q3_item *item = items + index;
    if (!item->model || !item->icon) return q3p_fail(e, QA_ERROR_FORMAT, "Weapon has no world model or icon");
    if (!model(m, item->model, &w->weapon_model, e) || !model_midpoint(m, w->weapon_model, &w->weapon_midpoint, e) ||
        !shader(m, item->icon, true, &w->weapon_icon, e) || !shader(m, item->icon, true, &w->ammo_icon, e)) return false;
    for (size_t i = 0; i < m->view.item_count; ++i)
        if (items[i].kind == QA_Q3_ITEM_AMMO && items[i].tag == (int32_t)number) {
            if (items[i].model && !model(m, items[i].model, &w->ammo_model, e)) return false;
            break;
        }
    if (!derived_model(m, item->model, "_flash.md3", &w->flash_model, e) ||
        !derived_model(m, item->model, "_barrel.md3", &w->barrel_model, e) ||
        !derived_model(m, item->model, "_hand.md3", &w->hands_model, e)) return false;
    if (!w->hands_model && !model(m, "models/weapons2/shotgun/shotgun_hand.md3", &w->hands_model, e)) return false;
    if (!specific(m, number, w, e)) return false;
    w->ready = true;
    return true;
}
bool q3n_media_register_item(q3n_media *m, uint32_t number, qa_error *e)
{ return enter(m, e) && leave(m, item_now(m, number, e)); }
bool q3n_media_register_weapon(q3n_media *m, uint32_t number, qa_error *e)
{ return enter(m, e) && leave(m, weapon_now(m, number, e)); }

typedef struct sound_request { q3n_sound field; const char *path; bool compressed; } sound_request;
static bool sounds(q3n_media *m, const sound_request *requests, size_t count, qa_error *e)
{
    for (size_t i = 0; i < count; ++i)
        if (!sound(m, requests[i].path, requests[i].compressed, &m->view.sounds[requests[i].field], e)) return false;
    return true;
}
#define S(field,path,compressed) {Q3N_S_##field,path,compressed}
#define SOUND_GROUP(name,...) static const sound_request name[] = {__VA_ARGS__}
#define PLAY_GROUP(name) do { if (!sounds(m,name,sizeof(name)/sizeof(name[0]),e)) return false; } while (0)
SOUND_GROUP(countdown,
    S(ONE_MINUTE,"sound/feedback/1_minute.wav",true), S(FIVE_MINUTES,"sound/feedback/5_minute.wav",true),
    S(SUDDEN_DEATH,"sound/feedback/sudden_death.wav",true), S(ONE_FRAG,"sound/feedback/1_frag.wav",true),
    S(TWO_FRAGS,"sound/feedback/2_frags.wav",true), S(THREE_FRAGS,"sound/feedback/3_frags.wav",true),
    S(COUNT3,"sound/feedback/three.wav",true), S(COUNT2,"sound/feedback/two.wav",true),
    S(COUNT1,"sound/feedback/one.wav",true), S(FIGHT,"sound/feedback/fight.wav",true), S(PREPARE,"sound/feedback/prepare.wav",true));
SOUND_GROUP(team_sounds,
    S(CAPTURE_AWARD,"sound/teamplay/flagcapture_yourteam.wav",true), S(RED_LEADS,"sound/feedback/redleads.wav",true),
    S(BLUE_LEADS,"sound/feedback/blueleads.wav",true), S(TEAMS_TIED,"sound/feedback/teamstied.wav",true),
    S(HIT_TEAM,"sound/feedback/hit_teammate.wav",true), S(RED_SCORED,"sound/teamplay/voc_red_scores.wav",true),
    S(BLUE_SCORED,"sound/teamplay/voc_blue_scores.wav",true), S(CAPTURE_YOUR_TEAM,"sound/teamplay/flagcapture_yourteam.wav",true),
    S(CAPTURE_OPPONENT,"sound/teamplay/flagcapture_opponent.wav",true), S(RETURN_YOUR_TEAM,"sound/teamplay/flagreturn_yourteam.wav",true),
    S(RETURN_OPPONENT,"sound/teamplay/flagreturn_opponent.wav",true), S(TAKEN_YOUR_TEAM,"sound/teamplay/flagtaken_yourteam.wav",true),
    S(TAKEN_OPPONENT,"sound/teamplay/flagtaken_opponent.wav",true));
SOUND_GROUP(ctf_sounds, S(RED_FLAG_RETURNED,"sound/teamplay/voc_red_returned.wav",true),
    S(BLUE_FLAG_RETURNED,"sound/teamplay/voc_blue_returned.wav",true), S(ENEMY_TOOK_YOUR_FLAG,"sound/teamplay/voc_enemy_flag.wav",true),
    S(YOUR_TEAM_TOOK_ENEMY_FLAG,"sound/teamplay/voc_team_flag.wav",true));
SOUND_GROUP(neutral_sounds, S(NEUTRAL_FLAG_RETURNED,"sound/teamplay/flagreturn_opponent.wav",true),
    S(YOUR_TEAM_TOOK_FLAG,"sound/teamplay/voc_team_1flag.wav",true), S(ENEMY_TOOK_FLAG,"sound/teamplay/voc_enemy_1flag.wav",true));
SOUND_GROUP(flag_sounds, S(YOU_HAVE_FLAG,"sound/teamplay/voc_you_flag.wav",true), S(HOLY_SHIT,"sound/feedback/voc_holyshit.wav",true));
SOUND_GROUP(character_sounds,
    S(TRACER,"sound/weapons/machinegun/buletby1.wav",false), S(SELECT,"sound/weapons/change.wav",false),
    S(WEAR_OFF,"sound/items/wearoff.wav",false), S(USE_NOTHING,"sound/items/use_nothing.wav",false),
    S(GIB,"sound/player/gibsplt1.wav",false), S(GIB_BOUNCE1,"sound/player/gibimp1.wav",false),
    S(GIB_BOUNCE2,"sound/player/gibimp2.wav",false), S(GIB_BOUNCE3,"sound/player/gibimp3.wav",false));
SOUND_GROUP(mission_item_sounds,
    S(USE_INVULNERABILITY,"sound/items/invul_activate.wav",false), S(INVULNERABILITY_IMPACT1,"sound/items/invul_impact_01.wav",false),
    S(INVULNERABILITY_IMPACT2,"sound/items/invul_impact_02.wav",false), S(INVULNERABILITY_IMPACT3,"sound/items/invul_impact_03.wav",false),
    S(INVULNERABILITY_JUICED,"sound/items/invul_juiced.wav",false), S(OBELISK_HIT1,"sound/items/obelisk_hit_01.wav",false),
    S(OBELISK_HIT2,"sound/items/obelisk_hit_02.wav",false), S(OBELISK_HIT3,"sound/items/obelisk_hit_03.wav",false),
    S(OBELISK_RESPAWN,"sound/items/obelisk_respawn.wav",false), S(AMMOREGEN,"sound/items/cl_ammoregen.wav",false),
    S(DOUBLER,"sound/items/cl_doubler.wav",false), S(GUARD,"sound/items/cl_guard.wav",false), S(SCOUT,"sound/items/cl_scout.wav",false));
SOUND_GROUP(player_sounds,
    S(TELE_IN,"sound/world/telein.wav",false), S(TELE_OUT,"sound/world/teleout.wav",false),
    S(RESPAWN,"sound/items/respawn1.wav",false), S(NOAMMO,"sound/weapons/noammo.wav",false),
    S(TALK,"sound/player/talk.wav",false), S(LAND,"sound/player/land1.wav",false), S(HIT,"sound/feedback/hit.wav",false));
SOUND_GROUP(awards, S(IMPRESSIVE,"sound/feedback/impressive.wav",true), S(EXCELLENT,"sound/feedback/excellent.wav",true),
    S(DENIED,"sound/feedback/denied.wav",true), S(HUMILIATION,"sound/feedback/humiliation.wav",true),
    S(ASSIST,"sound/feedback/assist.wav",true), S(DEFEND,"sound/feedback/defense.wav",true));
SOUND_GROUP(first_awards, S(FIRST_IMPRESSIVE,"sound/feedback/first_impressive.wav",true),
    S(FIRST_EXCELLENT,"sound/feedback/first_excellent.wav",true), S(FIRST_HUMILIATION,"sound/feedback/first_gauntlet.wav",true));
SOUND_GROUP(leads, S(TAKEN_LEAD,"sound/feedback/takenlead.wav",true), S(TIED_LEAD,"sound/feedback/tiedlead.wav",true), S(LOST_LEAD,"sound/feedback/lostlead.wav",true));
SOUND_GROUP(voting, S(VOTE_NOW,"sound/feedback/vote_now.wav",true), S(VOTE_PASSED,"sound/feedback/vote_passed.wav",true), S(VOTE_FAILED,"sound/feedback/vote_failed.wav",true));
SOUND_GROUP(water, S(WATER_IN,"sound/player/watr_in.wav",false), S(WATER_OUT,"sound/player/watr_out.wav",false),
    S(WATER_UNDER,"sound/player/watr_un.wav",false), S(JUMP_PAD,"sound/world/jumppad.wav",false));
SOUND_GROUP(weapon_sounds,
    S(FLIGHT,"sound/items/flight.wav",false), S(MEDKIT,"sound/items/use_medkit.wav",false), S(QUAD,"sound/items/damage3.wav",false),
    S(RICOCHET1,"sound/weapons/machinegun/ric1.wav",false), S(RICOCHET2,"sound/weapons/machinegun/ric2.wav",false),
    S(RICOCHET3,"sound/weapons/machinegun/ric3.wav",false), S(RAIL_FIRE,"sound/weapons/railgun/railgf1a.wav",false),
    S(ROCKET_EXPLOSION,"sound/weapons/rocket/rocklx1a.wav",false), S(PLASMA_EXPLOSION,"sound/weapons/plasma/plasmx1a.wav",false));
SOUND_GROUP(mission_weapon_sounds,
    S(PROX_EXPLOSION,"sound/weapons/proxmine/wstbexpl.wav",false), S(NAIL_HIT,"sound/weapons/nailgun/wnalimpd.wav",false),
    S(NAIL_FLESH,"sound/weapons/nailgun/wnalimpl.wav",false), S(NAIL_METAL,"sound/weapons/nailgun/wnalimpm.wav",false),
    S(CHAINGUN_HIT,"sound/weapons/vulcan/wvulimpd.wav",false), S(CHAINGUN_FLESH,"sound/weapons/vulcan/wvulimpl.wav",false),
    S(CHAINGUN_METAL,"sound/weapons/vulcan/wvulimpm.wav",false), S(WEAPON_HOVER,"sound/weapons/weapon_hover.wav",false),
    S(KAMIKAZE_EXPLODE,"sound/items/kam_explode.wav",false), S(KAMIKAZE_IMPLODE,"sound/items/kam_implode.wav",false),
    S(KAMIKAZE_FAR,"sound/items/kam_explode_far.wav",false), S(WINNER,"sound/feedback/voc_youwin.wav",false),
    S(LOSER,"sound/feedback/voc_youlose.wav",false), S(YOU_SUCK,"sound/misc/yousuck.wav",false),
    S(PROX_FLESH,"sound/weapons/proxmine/wstbimpl.wav",false), S(PROX_METAL,"sound/weapons/proxmine/wstbimpm.wav",false),
    S(PROX_HIT,"sound/weapons/proxmine/wstbimpd.wav",false), S(PROX_ACTIVATE,"sound/weapons/proxmine/wstbactv.wav",false));
SOUND_GROUP(last_item_sounds, S(REGEN,"sound/items/regen.wav",false), S(PROTECT,"sound/items/protect3.wav",false),
    S(NORMAL_HEALTH,"sound/items/n_health.wav",false), S(GRENADE_BOUNCE1,"sound/weapons/grenade/hgrenb1a.wav",false), S(GRENADE_BOUNCE2,"sound/weapons/grenade/hgrenb2a.wav",false));

static bool source_string(q3n_media *m, const q3n_media_load *load, uint32_t number,
    const char **text, uint64_t *revision, qa_error *e)
{
    if (load && load->remote) {
        const q3n_remote_source_view *remote = load->remote;
        if (load->source || load->reader || m->options.remote_source!=remote->owner ||
            load->application != remote->basis.application ||
            remote->basis.product != m->options.product || !q3n_remote_source_current(remote))
            return q3p_fail(e, QA_ERROR_ARGUMENT, "Remote media requires its actual reached CLIENT receipt");
        return q3n_remote_source_configstring(remote->owner, number, text, revision, e);
    }
    qa_native_q3_wire_basis basis;
    if (m->options.remote_source || !load || !load->application || !load->source || load->source->product != m->options.product ||
        !qa_native_q3_wire_reader_basis(load->reader, &basis, e) ||
        basis.application != load->application || basis.product != m->options.product ||
        basis.source_game != load->source->source_game || basis.source_owner != load->source->source_owner ||
        basis.publication_generation != load->source->publication_generation ||
        basis.map_revision != load->source->map_revision)
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Native media requires its actual GAME observation");
    return qa_native_q3_wire_reader_configstring(load->reader, number, text, revision, e);
}
static bool load_valid(q3n_media *m,const q3n_media_load *load,qa_error *e)
{
    if (!load || !load->application || !load->loading ||
        !(load->remote ? q3n_remote_source_current(load->remote) :
            (load->source && qa_application_native_q3_presentation_current(load->application,load->source))))
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Native media loading requires the actual source and loading producer");
    const char *text; uint64_t revision; char value[8192];
    if (!source_string(m,load,0,&text,&revision,e) ||
        !qa_q3_info_value(text,"g_gametype",value,sizeof(value),e)) return false;
    const char *number=value;
    while (*number && (signed char)*number<=32) ++number;
    bool negative=*number=='-';
    if (*number=='-' || *number=='+') ++number;
    uint32_t parsed=0;
    while (*number>='0' && *number<='9') parsed=parsed*10u+(uint32_t)(*number++-'0');
    if (negative) parsed=0u-parsed;
    return load->game_type>=0 && load->game_type<=7 && parsed==(uint32_t)load->game_type ? true :
        q3p_fail(e,QA_ERROR_ARGUMENT,"Native media loading differs from its reached serverinfo game type");
}
static bool bits(q3n_media *m, const q3n_media_load *load, const char **text, qa_error *e)
{
    uint64_t revision;
    if (!source_string(m, load, 27, text, &revision, e)) return false;
    return strlen(*text) <= 256 || q3p_fail(e, QA_ERROR_FORMAT, "CS_ITEMS exceeds source MAX_ITEMS precache buffer");
}
static const char *item_extra(const char *classname)
{
    if (!strcmp(classname,"weapon_grenadelauncher")) return "sound/weapons/grenade/hgrenb1a.wav sound/weapons/grenade/hgrenb2a.wav";
    if (!strcmp(classname,"holdable_medkit")) return "sound/items/use_medkit.wav";
    if (!strcmp(classname,"item_quad")) return "sound/items/damage2.wav sound/items/damage3.wav";
    if (!strcmp(classname,"item_enviro")) return "sound/items/airout.wav sound/items/protect3.wav";
    if (!strcmp(classname,"item_regen")) return "sound/items/regen.wav";
    if (!strcmp(classname,"item_flight")) return "sound/items/flight.wav";
    if (!strcmp(classname,"holdable_kamikaze")) return "sound/items/kamikazerespawn.wav";
    if (!strcmp(classname,"weapon_prox_launcher")) return "sound/weapons/proxmine/wstbtick.wav sound/weapons/proxmine/wstbactv.wav sound/weapons/proxmine/wstbimpl.wav sound/weapons/proxmine/wstbimpm.wav sound/weapons/proxmine/wstbimpd.wav sound/weapons/proxmine/wstbactv.wav";
    if (!strcmp(classname,"weapon_chaingun")) return "sound/weapons/vulcan/wvulwind.wav";
    return "";
}
static bool item_sounds(q3n_media *m, uint32_t number, qa_error *e)
{
    const qa_q3_item *item = qa_q3_items(m->options.product, NULL) + number;
    if (item->sound && !sound(m,item->sound,false,&m->view.items[number].pickup_sound,e)) return false;
    const char *extra = item_extra(item->classname);
    while (*extra) {
        const char *end = strchr(extra,' '); size_t length = end ? (size_t)(end-extra) : strlen(extra);
        if (length < 5 || length >= 64) return q3p_fail(e,QA_ERROR_FORMAT,"PrecacheItem: bad precache string");
        char path[64]; memcpy(path,extra,length); path[length]=0;
        if (!memcmp(path+length-3,"wav",3)) {
            int32_t handle;
            if (!sound(m,path,false,&handle,e)) return false;
            if (!strcmp(path,"sound/weapons/vulcan/wvulwind.wav")) m->view.sounds[Q3N_S_CHAINGUN_WIND]=handle;
        }
        extra = end ? end+1 : extra+length;
    }
    return true;
}
static bool observe_server_revisions(q3n_media *m,const q3n_media_load *load,bool models,qa_error *e)
{
    for (uint32_t i=0;i<256;++i) {
        const char *text; uint64_t revision;
        if (!source_string(m,load,(models?32u:288u)+i,&text,&revision,e)) return false;
        if (models) { m->model_observed[i]=true; m->model_revision[i]=revision; }
        else { m->sound_observed[i]=true; m->sound_revision[i]=revision; }
    }
    return true;
}
static bool load_sound_now(q3n_media *m, const q3n_media_load *load, qa_error *e)
{
    if (!load_valid(m,load,e)) return false;
    bool mission=m->options.product==QA_Q3_TEAM_ARENA;
    PLAY_GROUP(countdown);
    if (mission && !sound(m,"sound/feedback/prepare_team.wav",true,&m->view.sounds[Q3N_S_PREPARE_TEAM],e)) return false;
    if (load->game_type>=3 || load->build_script) {
        PLAY_GROUP(team_sounds);
        if (load->game_type==4 || load->build_script) { PLAY_GROUP(ctf_sounds); }
        if (mission) {
            if (load->game_type==5 || load->build_script) { PLAY_GROUP(neutral_sounds); }
            if (load->game_type==4 || load->game_type==5 || load->build_script) { PLAY_GROUP(flag_sounds); }
            if ((load->game_type==6 || load->build_script) && !sound(m,"sound/teamplay/voc_base_attack.wav",true,&m->view.sounds[Q3N_S_BASE_UNDER_ATTACK],e)) return false;
        } else { PLAY_GROUP(flag_sounds); PLAY_GROUP(neutral_sounds); }
    }
    PLAY_GROUP(character_sounds);
    if (mission) { PLAY_GROUP(mission_item_sounds); }
    PLAY_GROUP(player_sounds);
    if (mission && (!sound(m,"sound/feedback/hithi.wav",false,&m->view.sounds[Q3N_S_HIT_HIGH_ARMOR],e) ||
        !sound(m,"sound/feedback/hitlo.wav",false,&m->view.sounds[Q3N_S_HIT_LOW_ARMOR],e))) return false;
    PLAY_GROUP(awards);
    if (mission) { PLAY_GROUP(first_awards); }
    PLAY_GROUP(leads);
    if (mission) { PLAY_GROUP(voting); }
    PLAY_GROUP(water);
    const char *const steps[7]={"step","boot","flesh","mech","energy","splash","clank"};
    for (size_t i=0;i<4;++i) for (size_t j=0;j<7;++j) {
        char path[64]; snprintf(path,sizeof(path),"sound/player/footsteps/%s%zu.wav",steps[j],i+1);
        if (!sound(m,path,false,&m->view.footsteps[j][i],e)) return false;
    }
    const char *item_bits;
    if (!bits(m,load,&item_bits,e)) return false;
    for (uint32_t i=1;i<m->view.item_count;++i) if (!item_sounds(m,i,e)) return false;
    for (uint32_t i=1;i<256;++i) {
        const char *text; uint64_t revision;
        if (!source_string(m,load,288+i,&text,&revision,e)) return false;
        if (!*text) break;
        if (*text=='*') continue;
        if (!sound(m,text,false,&m->view.game_sounds[i],e)) return false;
        m->sound_revision[i]=revision; m->sound_observed[i]=true;
    }
    PLAY_GROUP(weapon_sounds);
    if (mission) { PLAY_GROUP(mission_weapon_sounds); }
    PLAY_GROUP(last_item_sounds);
    if (mission) {
        const char *const names[12]={"death1","death2","death3","jump1","pain25_1","pain75_1","pain100_1","falling1","gasp","drown","fall1","taunt"};
        const char *const players[2]={"james","janet"};
        for (size_t i=0;i<2;++i) for (size_t j=0;j<12;++j) {
            char path[96]; int32_t handle;
            snprintf(path,sizeof(path),"sound/player/%s/%s.wav",players[i],names[j]);
            if (!sound(m,path,false,&handle,e)) return false;
        }
    }
    if (!observe_server_revisions(m,load,false,e)) return false;
    m->sounds_loaded=true;
    return true;
}
bool q3n_media_load_sounds(q3n_media *m, const q3n_media_load *load, qa_error *e)
{ return enter(m,e) && leave(m,load_sound_now(m,load,e)); }
bool q3n_media_loading_graphics(q3n_media *m, qa_error *e)
{
    if (!enter(m,e)) return false;
    bool okay=shader(m,"gfx/2d/bigchars",true,&m->view.graphics[Q3N_G_CHARSET],e) &&
        shader(m,"white",true,&m->view.graphics[Q3N_G_WHITE],e) &&
        shader(m,"menu/art/font1_prop.tga",false,&m->view.graphics[Q3N_G_CHARSET_PROP],e) &&
        shader(m,"menu/art/font1_prop_glo.tga",false,&m->view.graphics[Q3N_G_CHARSET_PROP_GLOW],e) &&
        shader(m,"menu/art/font2_prop.tga",false,&m->view.graphics[Q3N_G_CHARSET_PROP_B],e);
    if (okay) m->loading_graphics=true;
    return leave(m,okay);
}

typedef enum graphic_kind { GRAPHIC_SHADER, GRAPHIC_NO_MIP, GRAPHIC_MODEL, GRAPHIC_SKIN } graphic_kind;
typedef struct graphic_request { q3n_graphic field; const char *path; graphic_kind kind; } graphic_request;
static bool graphics(q3n_media *m, const graphic_request *requests, size_t count, qa_error *e)
{
    for (size_t i=0;i<count;++i) {
        int32_t *handle=&m->view.graphics[requests[i].field];
        bool okay;
        switch (requests[i].kind) {
        case GRAPHIC_MODEL: okay=model(m,requests[i].path,handle,e); break;
        case GRAPHIC_SKIN: okay=qa_q3_register_skin(m->options.assets,requests[i].path,handle,e); break;
        default: okay=shader(m,requests[i].path,requests[i].kind==GRAPHIC_SHADER,handle,e); break;
        }
        if (!okay) return false;
    }
    return true;
}
#define G(field,path,kind) {Q3N_G_##field,path,GRAPHIC_##kind}
#define GRAPHIC_GROUP(name,...) static const graphic_request name[] = {__VA_ARGS__}
#define DRAW_GROUP(name) do { if (!graphics(m,name,sizeof(name)/sizeof(name[0]),e)) return false; } while (0)
GRAPHIC_GROUP(first_graphics,
    G(VIEW_BLOOD,"viewBloodBlend",SHADER), G(DEFER,"gfx/2d/defer.tga",NO_MIP),
    G(SCOREBOARD_NAME,"menu/tab/name.tga",NO_MIP), G(SCOREBOARD_PING,"menu/tab/ping.tga",NO_MIP),
    G(SCOREBOARD_SCORE,"menu/tab/score.tga",NO_MIP), G(SCOREBOARD_TIME,"menu/tab/time.tga",NO_MIP),
    G(SMOKE_PUFF,"smokePuff",SHADER), G(SMOKE_RAGEPRO,"smokePuffRagePro",SHADER), G(SHOTGUN_SMOKE,"shotgunSmokePuff",SHADER));
GRAPHIC_GROUP(mission_trails,G(NAIL_PUFF,"nailtrail",SHADER),G(BLUE_PROX_MINE,"models/weaphits/proxmineb.md3",MODEL));
GRAPHIC_GROUP(trail_graphics,
    G(PLASMA_BALL,"sprites/plasma1",SHADER), G(BLOOD_TRAIL,"bloodTrail",SHADER), G(LAGOMETER,"lagometer",SHADER),
    G(CONNECTION,"disconnected",SHADER), G(WATER_BUBBLE,"waterBubble",SHADER), G(TRACER,"gfx/misc/tracer",SHADER), G(SELECT,"gfx/2d/select",SHADER));
GRAPHIC_GROUP(powerup_graphics,G(BACK_TILE,"gfx/2d/backtile",SHADER),G(NOAMMO,"icons/noammo",SHADER),
    G(QUAD,"powerups/quad",SHADER),G(QUAD_WEAPON,"powerups/quadWeapon",SHADER), G(BATTLE_SUIT,"powerups/battleSuit",SHADER),
    G(BATTLE_WEAPON,"powerups/battleWeapon",SHADER),G(INVIS,"powerups/invisibility",SHADER),G(REGEN,"powerups/regen",SHADER),G(HASTE_PUFF,"hasteSmokePuff",SHADER));
GRAPHIC_GROUP(cubes,G(RED_CUBE,"models/powerups/orb/r_orb.md3",MODEL),G(BLUE_CUBE,"models/powerups/orb/b_orb.md3",MODEL),
    G(RED_CUBE_ICON,"icons/skull_red",SHADER),G(BLUE_CUBE_ICON,"icons/skull_blue",SHADER));
GRAPHIC_GROUP(flags,G(RED_FLAG,"models/flags/r_flag.md3",MODEL),G(BLUE_FLAG,"models/flags/b_flag.md3",MODEL));
GRAPHIC_GROUP(flag_parts,G(FLAG_POLE,"models/flag2/flagpole.md3",MODEL),G(FLAG_FLAP,"models/flag2/flagflap3.md3",MODEL),
    G(RED_FLAG_SKIN,"models/flag2/red.skin",SKIN),G(BLUE_FLAG_SKIN,"models/flag2/blue.skin",SKIN),G(NEUTRAL_FLAG_SKIN,"models/flag2/white.skin",SKIN),
    G(RED_FLAG_BASE,"models/mapobjects/flagbase/red_base.md3",MODEL),G(BLUE_FLAG_BASE,"models/mapobjects/flagbase/blue_base.md3",MODEL),G(NEUTRAL_FLAG_BASE,"models/mapobjects/flagbase/ntrl_base.md3",MODEL));
GRAPHIC_GROUP(obelisk,G(OVERLOAD_BASE,"models/powerups/overload_base.md3",MODEL),G(OVERLOAD_TARGET,"models/powerups/overload_target.md3",MODEL),
    G(OVERLOAD_LIGHTS,"models/powerups/overload_lights.md3",MODEL),G(OVERLOAD_ENERGY,"models/powerups/overload_energy.md3",MODEL));
GRAPHIC_GROUP(harvester,G(HARVESTER,"models/powerups/harvester/harvester.md3",MODEL),G(HARVESTER_RED_SKIN,"models/powerups/harvester/red.skin",SKIN),
    G(HARVESTER_BLUE_SKIN,"models/powerups/harvester/blue.skin",SKIN),G(HARVESTER_NEUTRAL,"models/powerups/obelisk/obelisk.md3",MODEL));
GRAPHIC_GROUP(mission_dust,G(RED_KAMIKAZE,"models/weaphits/kamikred",SHADER),G(DUST_PUFF,"hasteSmokePuff",SHADER));
GRAPHIC_GROUP(team_graphics,G(FRIEND,"sprites/foe",SHADER),G(RED_QUAD,"powerups/blueflag",SHADER),G(TEAM_STATUS_BAR,"gfx/2d/colorbar.tga",SHADER));
GRAPHIC_GROUP(body_graphics,G(ARMOR,"models/powerups/armor/armor_yel.md3",MODEL),G(ARMOR_ICON,"icons/iconr_yellow",NO_MIP),
    G(MACHINEGUN_BRASS,"models/weapons2/shells/m_shell.md3",MODEL),G(SHOTGUN_BRASS,"models/weapons2/shells/s_shell.md3",MODEL),
    G(GIB_ABDOMEN,"models/gibs/abdomen.md3",MODEL),G(GIB_ARM,"models/gibs/arm.md3",MODEL),G(GIB_CHEST,"models/gibs/chest.md3",MODEL),
    G(GIB_FIST,"models/gibs/fist.md3",MODEL),G(GIB_FOOT,"models/gibs/foot.md3",MODEL),G(GIB_FOREARM,"models/gibs/forearm.md3",MODEL),
    G(GIB_INTESTINE,"models/gibs/intestine.md3",MODEL),G(GIB_LEG,"models/gibs/leg.md3",MODEL),G(GIB_SKULL,"models/gibs/skull.md3",MODEL),G(GIB_BRAIN,"models/gibs/brain.md3",MODEL),
    G(SMOKE2,"models/weapons2/shells/s_shell.md3",MODEL),G(BALLOON,"sprites/balloon3",SHADER),G(BLOOD_EXPLOSION,"bloodExplosion",SHADER),
    G(BULLET_FLASH,"models/weaphits/bullet.md3",MODEL),G(RING_FLASH,"models/weaphits/ring02.md3",MODEL),G(DISH_FLASH,"models/weaphits/boom01.md3",MODEL));
GRAPHIC_GROUP(mission_effects,G(KAMIKAZE_EFFECT,"models/weaphits/kamboom2.md3",MODEL),G(KAMIKAZE_SHOCKWAVE,"models/weaphits/kamwave.md3",MODEL),
    G(KAMIKAZE_HEAD,"models/powerups/kamikazi.md3",MODEL),G(KAMIKAZE_TRAIL,"models/powerups/trailtest.md3",MODEL),
    G(GUARD_PLAYER,"models/powerups/guard_player.md3",MODEL),G(SCOUT_PLAYER,"models/powerups/scout_player.md3",MODEL),
    G(DOUBLER_PLAYER,"models/powerups/doubler_player.md3",MODEL),G(AMMOREGEN_PLAYER,"models/powerups/ammo_player.md3",MODEL),
    G(INVULNERABILITY_IMPACT,"models/powerups/shield/impact.md3",MODEL),G(INVULNERABILITY_JUICED,"models/powerups/shield/juicer.md3",MODEL),
    G(MEDKIT_USAGE,"models/powerups/regen.md3",MODEL),G(HEART,"ui/assets/statusbar/selectedhealth.tga",NO_MIP));
GRAPHIC_GROUP(medals,G(INVULNERABILITY_PLAYER,"models/powerups/shield/shield.md3",MODEL),G(MEDAL_IMPRESSIVE,"medal_impressive",NO_MIP),
    G(MEDAL_EXCELLENT,"medal_excellent",NO_MIP),G(MEDAL_GAUNTLET,"medal_gauntlet",NO_MIP),G(MEDAL_DEFEND,"medal_defend",NO_MIP),
    G(MEDAL_ASSIST,"medal_assist",NO_MIP),G(MEDAL_CAPTURE,"medal_capture",NO_MIP));
GRAPHIC_GROUP(marks,G(BULLET_MARK,"gfx/damage/bullet_mrk",SHADER),G(BURN_MARK,"gfx/damage/burn_med_mrk",SHADER),
    G(HOLE_MARK,"gfx/damage/hole_lg_mrk",SHADER),G(ENERGY_MARK,"gfx/damage/plasma_mrk",SHADER),G(SHADOW_MARK,"markShadow",SHADER),G(WAKE_MARK,"wake",SHADER),G(BLOOD_MARK,"bloodMark",SHADER));
GRAPHIC_GROUP(mission_ui,G(PATROL,"ui/assets/statusbar/patrol.tga",NO_MIP),G(ASSAULT,"ui/assets/statusbar/assault.tga",NO_MIP),
    G(CAMP,"ui/assets/statusbar/camp.tga",NO_MIP),G(FOLLOW,"ui/assets/statusbar/follow.tga",NO_MIP),G(DEFEND,"ui/assets/statusbar/defend.tga",NO_MIP),
    G(TEAM_LEADER,"ui/assets/statusbar/team_leader.tga",NO_MIP),G(RETRIEVE,"ui/assets/statusbar/retrieve.tga",NO_MIP),G(ESCORT,"ui/assets/statusbar/escort.tga",NO_MIP),
    G(CURSOR,"menu/art/3_cursor2",NO_MIP),G(SIZE_CURSOR,"ui/assets/sizecursor.tga",NO_MIP),G(SELECT_CURSOR,"ui/assets/selectcursor.tga",NO_MIP));
static bool graphic_one(q3n_media *m,q3n_graphic field,const char *path,graphic_kind kind,qa_error *e)
{ graphic_request request={field,path,kind}; return graphics(m,&request,1,e); }

static bool effect_source_current(q3n_media *m, qa_application *app,
    const qa_application_selected_effects *source, const qa_application_effect_event *event, qa_error *e)
{
    if (!app || !source || source->kind != QA_APPLICATION_EFFECTS_Q3 ||
        source->q3_product != m->options.product ||
        !qa_vfs_lookup_equal(m->options.assets->options.provider.mounts, source->content) ||
        m->options.assets->options.provider.family != QA_SCENE_Q3 ||
        (event ? source != &event->source || !qa_application_effect_event_current(app, event) :
            !qa_application_selected_effects_current(app, source)))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected effect media lost its actual source, content or clock");
    return true;
}

bool q3n_media_effects_current(const q3n_media *m, qa_application *app,
    const qa_application_selected_effects *source, const qa_application_effect_event *event, qa_error *e)
{
    return q3n_media_effects_ready(m) && effect_source_current((q3n_media *)m, app, source, event, e);
}

bool q3n_media_load_effects(q3n_media *m, qa_application *app,
    const qa_application_selected_effects *source, const qa_application_effect_event *event, qa_error *e)
{
    if (!enter(m, e)) return false;
    bool okay = effect_source_current(m, app, source, event, e);
    if (okay && m->effects_loaded) return leave(m, true);
    static const graphic_request common[] = {
        G(WATER_BUBBLE,"waterBubble",SHADER), G(SMOKE_RAGEPRO,"smokePuffRagePro",SHADER),
        G(BLOOD_EXPLOSION,"bloodExplosion",SHADER),
        G(GIB_SKULL,"models/gibs/skull.md3",MODEL), G(GIB_BRAIN,"models/gibs/brain.md3",MODEL),
        G(GIB_ABDOMEN,"models/gibs/abdomen.md3",MODEL), G(GIB_ARM,"models/gibs/arm.md3",MODEL),
        G(GIB_CHEST,"models/gibs/chest.md3",MODEL), G(GIB_FIST,"models/gibs/fist.md3",MODEL),
        G(GIB_FOOT,"models/gibs/foot.md3",MODEL), G(GIB_FOREARM,"models/gibs/forearm.md3",MODEL),
        G(GIB_INTESTINE,"models/gibs/intestine.md3",MODEL), G(GIB_LEG,"models/gibs/leg.md3",MODEL),
        G(SMOKE2,"models/weapons2/shells/s_shell.md3",MODEL),
        G(SMOKE_PUFF,"smokePuff",SHADER), G(BLOOD_TRAIL,"bloodTrail",SHADER),
        G(BLOOD_MARK,"bloodMark",SHADER), G(BURN_MARK,"burnMark",SHADER)
    };
    /* Keep registration order from ApplicationEffects' real ClientEffects
     * and LocalEntitySystem construction, including the product branch. */
    for (size_t i = 0; okay && i < 3; ++i)
        okay = graphics(m, &common[i], 1, e) && effect_source_current(m, app, source, event, e);
    if (okay) okay = graphic_one(m, Q3N_G_TELEPORT_MODEL,
        m->options.product == QA_Q3_ARENA ? "models/misc/telep.md3" : "models/powerups/pop.md3",
        GRAPHIC_MODEL, e) && effect_source_current(m, app, source, event, e);
    for (size_t i = 3; okay && i < 14; ++i)
        okay = graphics(m, &common[i], 1, e) && effect_source_current(m, app, source, event, e);
    if (okay && m->options.product == QA_Q3_ARENA)
        okay = graphic_one(m, Q3N_G_TELEPORT_SHADER, "teleportEffect", GRAPHIC_SHADER, e) &&
            effect_source_current(m, app, source, event, e);
    if (okay && m->options.product == QA_Q3_TEAM_ARENA) {
        static const graphic_request mission[] = {
            G(LIGHTNING_SHADER,"lightningBolt",SHADER), G(KAMIKAZE_EFFECT,"models/weaphits/kamboom2.md3",MODEL),
            G(DISH_FLASH,"models/weaphits/boom01.md3",MODEL), G(ROCKET_EXPLOSION,"rocketExplosion",SHADER),
            G(INVULNERABILITY_IMPACT,"models/powerups/shield/impact.md3",MODEL),
            G(INVULNERABILITY_JUICED,"models/powerups/shield/juicer.md3",MODEL)
        };
        static const sound_request hits[] = {
            S(OBELISK_HIT1,"sound/items/obelisk_hit_01.wav",false), S(OBELISK_HIT2,"sound/items/obelisk_hit_02.wav",false),
            S(OBELISK_HIT3,"sound/items/obelisk_hit_03.wav",false),
            S(INVULNERABILITY_IMPACT1,"sound/items/invul_impact_01.wav",false),
            S(INVULNERABILITY_IMPACT2,"sound/items/invul_impact_02.wav",false),
            S(INVULNERABILITY_IMPACT3,"sound/items/invul_impact_03.wav",false),
            S(INVULNERABILITY_JUICED,"sound/items/invul_juiced.wav",false)
        };
        for (size_t i = 0; okay && i < 4; ++i)
            okay = graphics(m, &mission[i], 1, e) && effect_source_current(m, app, source, event, e);
        for (size_t i = 0; okay && i < 3; ++i)
            okay = sounds(m, &hits[i], 1, e) && effect_source_current(m, app, source, event, e);
        if (okay) okay = graphics(m, &mission[4], 1, e) && effect_source_current(m, app, source, event, e);
        for (size_t i = 3; okay && i < 6; ++i)
            okay = sounds(m, &hits[i], 1, e) && effect_source_current(m, app, source, event, e);
        if (okay) okay = graphics(m, &mission[5], 1, e) && effect_source_current(m, app, source, event, e);
        if (okay) okay = sounds(m, &hits[6], 1, e) && effect_source_current(m, app, source, event, e);
    }
    for (size_t i = 14; okay && i < sizeof(common) / sizeof(common[0]); ++i)
        okay = graphics(m, &common[i], 1, e) && effect_source_current(m, app, source, event, e);
    const char *const numbers[] = {"zero","one","two","three","four","five","six","seven","eight","nine","minus"};
    for (size_t i = 0; okay && i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        char path[64]; snprintf(path, sizeof(path), "gfx/2d/numbers/%s_32b", numbers[i]);
        okay = shader(m, path, true, &m->view.number_shaders[i], e) && effect_source_current(m, app, source, event, e);
    }
    static const sound_request bounce[] = {
        S(GIB_BOUNCE1,"sound/player/gibimp1.wav",false), S(GIB_BOUNCE2,"sound/player/gibimp2.wav",false),
        S(GIB_BOUNCE3,"sound/player/gibimp3.wav",false)
    };
    for (size_t i = 0; okay && i < sizeof(bounce) / sizeof(bounce[0]); ++i)
        okay = sounds(m, &bounce[i], 1, e) && effect_source_current(m, app, source, event, e);
    if (okay && m->options.product == QA_Q3_TEAM_ARENA) {
        okay = graphic_one(m, Q3N_G_KAMIKAZE_SHOCKWAVE, "models/weaphits/kamwave.md3", GRAPHIC_MODEL, e) &&
            effect_source_current(m, app, source, event, e);
        const sound_request kamikaze[] = {S(KAMIKAZE_EXPLODE,"sound/items/kam_explode.wav",false),
            S(KAMIKAZE_IMPLODE,"sound/items/kam_implode.wav",false)};
        for (size_t i = 0; okay && i < sizeof(kamikaze) / sizeof(kamikaze[0]); ++i)
            okay = sounds(m, &kamikaze[i], 1, e) && effect_source_current(m, app, source, event, e);
    }
    if (okay) m->effects_loaded = true;
    return leave(m, okay);
}
static bool load_graphics_now(q3n_media *m,const q3n_media_load *load,qa_error *e)
{
    if (!load_valid(m,load,e)) return false;
    if (!load->inline_models) return q3p_fail(e,QA_ERROR_ARGUMENT,"Native graphics require actual bound world model extent");
    bool mission=m->options.product==QA_Q3_TEAM_ARENA;
    if (!load->loading(load->context,"game media",-1,e) || !load_valid(m,load,e)) return false;
    const char *const numbers[11]={"zero","one","two","three","four","five","six","seven","eight","nine","minus"};
    for (size_t i=0;i<11;++i) { char path[64]; snprintf(path,sizeof(path),"gfx/2d/numbers/%s_32b",numbers[i]);
        if (!shader(m,path,true,&m->view.number_shaders[i],e)) return false; }
    for (size_t i=0;i<5;++i) { char path[32]; snprintf(path,sizeof(path),"menu/art/skill%zu.tga",i+1);
        if (!shader(m,path,true,&m->view.bot_skill_shaders[i],e)) return false; }
    DRAW_GROUP(first_graphics);
    if (mission) { DRAW_GROUP(mission_trails); }
    DRAW_GROUP(trail_graphics);
    for (size_t i=0;i<10;++i) { char path[32]; snprintf(path,sizeof(path),"gfx/2d/crosshair%c",(int)('a'+i));
        if (!shader(m,path,true,&m->view.crosshairs[i],e)) return false; }
    DRAW_GROUP(powerup_graphics);
    if (load->game_type==4 || (mission && (load->game_type==5 || load->game_type==7)) || load->build_script) {
        DRAW_GROUP(cubes); DRAW_GROUP(flags);
        for (size_t i=0;i<3;++i) { char path[32]; snprintf(path,sizeof(path),"icons/iconf_red%zu",i+1);
            if (!shader(m,path,false,&m->view.red_flag_shaders[i],e)) return false; }
        for (size_t i=0;i<3;++i) { char path[32]; snprintf(path,sizeof(path),"icons/iconf_blu%zu",i+1);
            if (!shader(m,path,false,&m->view.blue_flag_shaders[i],e)) return false; }
        if (mission) { DRAW_GROUP(flag_parts); }
    }
    if (mission) {
        if (load->game_type==5 || load->build_script) {
            if (!graphic_one(m,Q3N_G_NEUTRAL_FLAG,"models/flags/n_flag.md3",GRAPHIC_MODEL,e)) return false;
            const char *const paths[4]={"icons/iconf_neutral1","icons/iconf_red2","icons/iconf_blu2","icons/iconf_neutral3"};
            for (size_t i=0;i<4;++i) if (!shader(m,paths[i],false,&m->view.neutral_flag_shaders[i],e)) return false;
        }
        if (load->game_type==6 || load->build_script) { DRAW_GROUP(obelisk); }
        if (load->game_type==7 || load->build_script) { DRAW_GROUP(harvester); }
        DRAW_GROUP(mission_dust);
    }
    if (load->game_type>=3 || load->build_script) {
        DRAW_GROUP(team_graphics);
        if (mission && !graphic_one(m,Q3N_G_BLUE_KAMIKAZE,"models/weaphits/kamikblu",GRAPHIC_SHADER,e)) return false;
    }
    DRAW_GROUP(body_graphics);
    if (!graphic_one(m,Q3N_G_TELEPORT_MODEL,mission?"models/powerups/pop.md3":"models/misc/telep.md3",GRAPHIC_MODEL,e)) return false;
    if (!mission && !graphic_one(m,Q3N_G_TELEPORT_SHADER,"teleportEffect",GRAPHIC_SHADER,e)) return false;
    if (mission) { DRAW_GROUP(mission_effects); }
    DRAW_GROUP(medals);
    const char *item_bits;
    if (!bits(m,load,&item_bits,e)) return false;
    size_t item_extent=strlen(item_bits);
    char item_snapshot[257];
    memcpy(item_snapshot,item_bits,item_extent+1);
    for (uint32_t i=1;i<m->view.item_count;++i) if ((i<item_extent && item_snapshot[i]=='1') || load->build_script) {
        if (!load->loading(load->context,NULL,(int32_t)i,e) || !load_valid(m,load,e)) return false;
        if (!item_now(m,i,e)) return false;
    }
    DRAW_GROUP(marks);
    for (uint32_t i=1;i<load->inline_models;++i) {
        if (m->view.inline_count==SIZE_MAX/sizeof(*m->inline_models)) return q3p_fail(e,QA_ERROR_MEMORY,"Native inline model extent overflow");
        q3n_inline_media *next=realloc(m->inline_models,(m->view.inline_count+1)*sizeof(*next));
        if (!next) return q3p_fail(e,QA_ERROR_MEMORY,"Growing native inline model inventory");
        m->inline_models=next; m->view.inline_models=next;
        q3n_inline_media entry={0}; char path[32]; snprintf(path,sizeof(path),"*%u",i);
        if (!model(m,path,&entry.model,e) || !model_midpoint(m,entry.model,&entry.midpoint,e)) return false;
        next[m->view.inline_count++]=entry;
    }
    for (uint32_t i=1;i<256;++i) {
        const char *text; uint64_t revision;
        if (!source_string(m,load,32+i,&text,&revision,e)) return false;
        if (!*text) break;
        if (!model(m,text,&m->view.game_models[i],e)) return false;
        m->model_revision[i]=revision; m->model_observed[i]=true;
    }
    if (mission) {
        DRAW_GROUP(mission_ui);
        const char *const paths[3]={"ui/assets/statusbar/flag_in_base.tga","ui/assets/statusbar/flag_capture.tga","ui/assets/statusbar/flag_missing.tga"};
        for (size_t i=0;i<3;++i) if (!shader(m,paths[i],false,&m->view.flag_status_shaders[i],e)) return false;
        const char *const models[6]={"models/players/james/lower.md3","models/players/james/upper.md3","models/players/heads/james/james.md3",
            "models/players/janet/lower.md3","models/players/janet/upper.md3","models/players/heads/janet/janet.md3"};
        for (size_t i=0;i<6;++i) { int32_t handle; if (!model(m,models[i],&handle,e)) return false; }
    }
    if (!observe_server_revisions(m,load,true,e)) return false;
    m->graphics_loaded=true;
    return true;
}
bool q3n_media_load_graphics(q3n_media *m,const q3n_media_load *load,qa_error *e)
{ return enter(m,e) && leave(m,load_graphics_now(m,load,e)); }

bool q3n_media_configstring_changed(q3n_media *m,qa_native_q3_wire_reader *reader,
    uint32_t index,qa_error *e)
{
    if (!enter(m,e)) return false;
    qa_native_q3_wire_basis basis;
    qa_native_q3_wire_publication reached;
    const char *text; uint64_t revision;
    bool okay=!m->options.remote_source && index>=32 && index<544 && qa_native_q3_wire_reader_basis(reader,&basis,e) &&
        basis.product==m->options.product && qa_native_q3_wire_reader_publication(reader,&reached,e) &&
        qa_native_q3_wire_reader_configstring(reader,index,&text,&revision,e);
    if (!okay) return leave(m,q3p_fail(e,QA_ERROR_ARGUMENT,"Native media change requires its exact reached model/sound row"));
    size_t length=strlen(text);
    char *retained=malloc(length+1);
    if (!retained) return leave(m,q3p_fail(e,QA_ERROR_MEMORY,"Retaining reached native media text"));
    memcpy(retained,text,length+1);
    uint32_t slot=index<288?index-32:index-288;
    if (index<288) okay=model(m,retained,&m->view.game_models[slot],e);
    else if (*retained!='*') okay=sound(m,retained,false,&m->view.game_sounds[slot],e);
    if (okay) {
        const char *actual; uint64_t current;
        qa_native_q3_wire_publication after;
        okay=qa_native_q3_wire_reader_configstring(reader,index,&actual,&current,e) &&
            current==revision && !strcmp(actual,retained) &&
            qa_native_q3_wire_reader_publication(reader,&after,e) &&
            after.reached_command_sequence==reached.reached_command_sequence;
        if (!okay && (!e || e->code==QA_OK)) q3p_fail(e,QA_ERROR_ARGUMENT,"Native media row changed during registration");
    }
    if (okay) {
        if (index<288) { m->model_observed[slot]=true; m->model_revision[slot]=revision; }
        else { m->sound_observed[slot]=true; m->sound_revision[slot]=revision; }
    }
    free(retained);
    return leave(m,okay);
}
bool q3n_media_remote_configstring_changed(q3n_media *m,const q3n_remote_source_view *source,
    uint32_t index,qa_error *e)
{
    if (!enter(m,e)) return false;
    const char *text; uint64_t revision;
    bool okay=source && m->options.remote_source==source->owner && index>=32 && index<544 && source->basis.product==m->options.product &&
        q3n_remote_source_current(source) &&
        q3n_remote_source_configstring(source->owner,index,&text,&revision,e);
    if (!okay) return leave(m,q3p_fail(e,QA_ERROR_ARGUMENT,"Remote media change requires its exact reached model/sound row"));
    size_t length=strlen(text);
    char *retained=malloc(length+1);
    if (!retained) return leave(m,q3p_fail(e,QA_ERROR_MEMORY,"Retaining reached remote media text"));
    memcpy(retained,text,length+1);
    uint32_t slot=index<288?index-32:index-288;
    if (index<288) okay=model(m,retained,&m->view.game_models[slot],e);
    else if (*retained!='*') okay=sound(m,retained,false,&m->view.game_sounds[slot],e);
    if (okay) {
        const char *actual; uint64_t current;
        okay=q3n_remote_source_current(source) &&
            q3n_remote_source_configstring(source->owner,index,&actual,&current,e) &&
            current==revision && !strcmp(actual,retained);
        if (!okay && (!e || e->code==QA_OK)) q3p_fail(e,QA_ERROR_ARGUMENT,"Remote media row changed during registration");
    }
    if (okay) {
        if (index<288) { m->model_observed[slot]=true; m->model_revision[slot]=revision; }
        else { m->sound_observed[slot]=true; m->sound_revision[slot]=revision; }
    }
    free(retained);
    return leave(m,okay);
}
static bool captured(const q3n_media *m,qa_error *e)
{
    const qa_q3_presentation_assets *a=m?m->options.assets:NULL;
    return q3n_media_idle(m) && a && a->capturing && a->busy==1 && !a->codec_busy ? true :
        q3p_fail(e,QA_ERROR_ARGUMENT,"Native Q3 media codec requires the actual backend capture lease");
}
static bool handle_field(qa_source_save_io *io,q3n_media *m,int32_t *handle,q3p_resource_kind kind)
{
    if (!qa_source_save_i32(io,handle) || *handle<0) return false;
    const qa_q3_presentation_assets *a=m->options.assets;
    if (!*handle) return true;
    size_t index=(size_t)*handle-1;
    switch (kind) {
    case Q3P_MODEL: return index<a->model_count && a->models[index];
    case Q3P_SKIN: return index<a->skin_count && a->skins[index];
    case Q3P_SHADER: return index<a->shader_count && a->shaders[index];
    case Q3P_SOUND: return index<a->sound_count && a->sounds[index];
    }
    return false;
}
static q3p_resource_kind graphic_resource(size_t index)
{
    if (index==Q3N_G_RED_FLAG_SKIN || index==Q3N_G_BLUE_FLAG_SKIN || index==Q3N_G_NEUTRAL_FLAG_SKIN ||
        index==Q3N_G_HARVESTER_RED_SKIN || index==Q3N_G_HARVESTER_BLUE_SKIN) return Q3P_SKIN;
    switch (index) {
    case Q3N_G_BLUE_PROX_MINE: case Q3N_G_RED_CUBE: case Q3N_G_BLUE_CUBE:
    case Q3N_G_RED_FLAG: case Q3N_G_BLUE_FLAG: case Q3N_G_FLAG_POLE: case Q3N_G_FLAG_FLAP:
    case Q3N_G_RED_FLAG_BASE: case Q3N_G_BLUE_FLAG_BASE: case Q3N_G_NEUTRAL_FLAG_BASE: case Q3N_G_NEUTRAL_FLAG:
    case Q3N_G_OVERLOAD_BASE: case Q3N_G_OVERLOAD_TARGET: case Q3N_G_OVERLOAD_LIGHTS: case Q3N_G_OVERLOAD_ENERGY:
    case Q3N_G_HARVESTER: case Q3N_G_HARVESTER_NEUTRAL: case Q3N_G_ARMOR:
    case Q3N_G_MACHINEGUN_BRASS: case Q3N_G_SHOTGUN_BRASS: case Q3N_G_GIB_ABDOMEN: case Q3N_G_GIB_ARM:
    case Q3N_G_GIB_CHEST: case Q3N_G_GIB_FIST: case Q3N_G_GIB_FOOT: case Q3N_G_GIB_FOREARM: case Q3N_G_GIB_INTESTINE:
    case Q3N_G_GIB_LEG: case Q3N_G_GIB_SKULL: case Q3N_G_GIB_BRAIN: case Q3N_G_SMOKE2:
    case Q3N_G_BULLET_FLASH: case Q3N_G_RING_FLASH: case Q3N_G_DISH_FLASH: case Q3N_G_TELEPORT_MODEL:
    case Q3N_G_KAMIKAZE_EFFECT: case Q3N_G_KAMIKAZE_SHOCKWAVE: case Q3N_G_KAMIKAZE_HEAD: case Q3N_G_KAMIKAZE_TRAIL:
    case Q3N_G_GUARD_PLAYER: case Q3N_G_SCOUT_PLAYER: case Q3N_G_DOUBLER_PLAYER: case Q3N_G_AMMOREGEN_PLAYER:
    case Q3N_G_INVULNERABILITY_IMPACT: case Q3N_G_INVULNERABILITY_JUICED: case Q3N_G_MEDKIT_USAGE:
    case Q3N_G_INVULNERABILITY_PLAYER: case Q3N_G_LIGHTNING_EXPLOSION: return Q3P_MODEL;
    default: return Q3P_SHADER;
    }
}
static bool weapon_fields(qa_source_save_io *io,q3n_media *m,q3n_weapon_media *w)
{
    if (!qa_source_save_i32(io,&w->item_index) || w->item_index< -1 || w->item_index>=(int32_t)m->view.item_count ||
        !handle_field(io,m,&w->weapon_model,Q3P_MODEL) || !handle_field(io,m,&w->barrel_model,Q3P_MODEL) ||
        !handle_field(io,m,&w->hands_model,Q3P_MODEL) || !handle_field(io,m,&w->flash_model,Q3P_MODEL) ||
        !handle_field(io,m,&w->ammo_model,Q3P_MODEL) || !handle_field(io,m,&w->weapon_icon,Q3P_SHADER) ||
        !handle_field(io,m,&w->ammo_icon,Q3P_SHADER) || !qa_source_save_vec3(io,&w->weapon_midpoint) ||
        !qa_source_save_vec3(io,&w->flash_light_color) || !qa_source_save_vec3(io,&w->missile_light_color) ||
        !handle_field(io,m,&w->missile_model,Q3P_MODEL) || !qa_source_save_i32(io,&w->missile_render_flags) ||
        !handle_field(io,m,&w->missile_sound,Q3P_SOUND) || !qa_source_save_i32(io,&w->trail_time) ||
        !qa_source_save_f32(io,&w->missile_light) || !qa_source_save_f32(io,&w->trail_radius)) return false;
    uint32_t trail=w->trail,brass=w->eject_brass;
    if (!qa_source_save_u32(io,&trail) || trail>Q3N_TRAIL_NAIL || !qa_source_save_u32(io,&brass) || brass>Q3N_BRASS_NAILGUN) return false;
    w->trail=(q3n_missile_trail)trail; w->eject_brass=(q3n_brass)brass;
    for (size_t i=0;i<4;++i) if (!handle_field(io,m,&w->flash_sounds[i],Q3P_SOUND)) return false;
    return handle_field(io,m,&w->ready_sound,Q3P_SOUND) && handle_field(io,m,&w->firing_sound,Q3P_SOUND) &&
        qa_source_save_bool(io,&w->registered) && qa_source_save_bool(io,&w->ready) && qa_source_save_bool(io,&w->loop_fire_sound) &&
        (!w->ready || (w->registered && w->item_index>=0));
}
static bool media_fields(qa_source_save_io *io,q3n_media *m)
{
    uint8_t magic[4]={'Q','3','M','D'}; uint32_t schema=3,product=m->options.product;
    const char *expected=m->options.remote_source?"Q3MR":"Q3MD";
    memcpy(magic,expected,4);
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,expected,4) || !qa_source_save_u32(io,&schema) || schema!=3 ||
        !qa_source_save_u32(io,&product) || product!=(uint32_t)m->options.product) return false;
    if (m->options.remote_source) {
        q3n_remote_source_view source;
        if (!q3n_remote_source_read(m->options.remote_source,&source,io->error)) return false;
        const qa_native_q3_remote_client_basis *b=&source.basis;
        uint64_t owner=b->connection.owner,generation=b->connection.generation,epoch=b->epoch;
        uint64_t restart=b->restart_generation,publication=b->publication_generation,configuration=b->configuration_generation;
        uint32_t slot=b->connection.slot,physical=b->physical_client;
        int32_t message=b->initial_message,command=b->initial_command;
        if (!qa_source_save_u64(io,&owner) || owner!=b->connection.owner ||
            !qa_source_save_u64(io,&generation) || generation!=b->connection.generation ||
            !qa_source_save_u32(io,&slot) || slot!=b->connection.slot ||
            !qa_source_save_u64(io,&epoch) || epoch!=b->epoch ||
            !qa_source_save_u64(io,&restart) || restart!=b->restart_generation ||
            !qa_source_save_u64(io,&publication) || publication!=b->publication_generation ||
            !qa_source_save_u64(io,&configuration) || configuration!=b->configuration_generation ||
            !qa_source_save_u32(io,&physical) || physical!=b->physical_client ||
            !qa_source_save_i32(io,&message) || message!=b->initial_message ||
            !qa_source_save_i32(io,&command) || command!=b->initial_command ||
            !q3n_remote_source_current(&source)) return false;
    }
    if (
        !qa_source_save_bool(io,&m->loading_graphics) || !qa_source_save_bool(io,&m->sounds_loaded) || !qa_source_save_bool(io,&m->graphics_loaded) ||
        !qa_source_save_bool(io,&m->effects_loaded)) return false;
    for (size_t i=0;i<m->view.item_count;++i) {
        q3n_item_media *item=&m->view.items[i];
        if (!handle_field(io,m,&item->models[0],Q3P_MODEL) || !handle_field(io,m,&item->models[1],Q3P_MODEL) ||
            !handle_field(io,m,&item->icon,Q3P_SHADER) || !handle_field(io,m,&item->pickup_sound,Q3P_SOUND) ||
            !qa_source_save_bool(io,&item->registered)) return false;
    }
    for (size_t i=0;i<16;++i) {
        q3n_weapon_media *w=&m->view.weapons[i];
        if (!weapon_fields(io,m,w)) return false;
        if (w->item_index>=0) {
            const qa_q3_item *item=qa_q3_items(m->options.product,NULL)+w->item_index;
            if (item->kind!=QA_Q3_ITEM_WEAPON || item->tag!=(int32_t)i) return false;
        }
    }
    for (size_t i=0;i<Q3N_GRAPHIC_COUNT;++i) if (!handle_field(io,m,&m->view.graphics[i],graphic_resource(i))) return false;
    for (size_t i=0;i<Q3N_SOUND_COUNT;++i) if (!handle_field(io,m,&m->view.sounds[i],Q3P_SOUND)) return false;
#define SHADERS(array) for (size_t i=0;i<sizeof(m->view.array)/sizeof(m->view.array[0]);++i) if (!handle_field(io,m,&m->view.array[i],Q3P_SHADER)) return false
    SHADERS(number_shaders); SHADERS(bot_skill_shaders); SHADERS(crosshairs);
    SHADERS(red_flag_shaders); SHADERS(blue_flag_shaders); SHADERS(neutral_flag_shaders); SHADERS(flag_status_shaders);
#undef SHADERS
    for (size_t i=0;i<7;++i) for (size_t j=0;j<4;++j) if (!handle_field(io,m,&m->view.footsteps[i][j],Q3P_SOUND)) return false;
    for (size_t i=0;i<256;++i)
        if (!handle_field(io,m,&m->view.game_models[i],Q3P_MODEL) || !handle_field(io,m,&m->view.game_sounds[i],Q3P_SOUND) ||
            !qa_source_save_u64(io,&m->model_revision[i]) || !qa_source_save_u64(io,&m->sound_revision[i]) ||
            !qa_source_save_bool(io,&m->model_observed[i]) || !qa_source_save_bool(io,&m->sound_observed[i]) ||
            m->model_revision[i]>INT32_MAX || m->sound_revision[i]>INT32_MAX ||
            (!m->model_observed[i] && m->model_revision[i]) || (!m->sound_observed[i] && m->sound_revision[i])) return false;
    size_t count=m->view.inline_count;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ ? (io->input.size-io->offset)/16 : SIZE_MAX/sizeof(*m->inline_models)) || !count) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        q3n_inline_media *next=calloc(count,sizeof(*next));
        if (!next) return q3p_fail(io->error,QA_ERROR_MEMORY,"Restoring native inline media references");
        free(m->inline_models); m->inline_models=next; m->view.inline_models=next; m->view.inline_count=count;
    }
    for (size_t i=0;i<count;++i) if (!handle_field(io,m,&m->inline_models[i].model,Q3P_MODEL) ||
        !qa_source_save_vec3(io,&m->inline_models[i].midpoint)) return false;
    return !m->inline_models[0].model;
}
bool q3n_media_checkpoint(const q3n_media *borrowed,qa_buffer *out,qa_error *e)
{
    if (!out || out->data || out->size || !captured(borrowed,e)) return false;
    q3n_media *m=(q3n_media *)borrowed; m->busy=true; q3n_media saved=*m; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && media_fields(&io,&saved) && qa_source_save_finish(&io,out);
    if (!okay && e && e->code==QA_OK) q3p_fail(e,QA_ERROR_FORMAT,"Native media checkpoint holders are inconsistent");
    qa_source_save_dispose(&io); return leave(m,okay);
}
bool q3n_media_restore(q3n_media *m,qa_bytes bytes,qa_error *e)
{
    if (!captured(m,e)) return false;
    q3n_media *candidate=NULL;
    if (!q3n_media_create(&m->options,&candidate,e)) return false;
    m->busy=true; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && media_fields(&io,candidate) && qa_source_save_finish(&io,NULL);
    if (okay) {
        q3n_inline_media *old=m->inline_models; *m=*candidate; m->busy=true; candidate->inline_models=old;
    } else if (e && e->code==QA_OK) q3p_fail(e,QA_ERROR_FORMAT,"Saved native media holders are inconsistent");
    qa_source_save_dispose(&io); q3n_media_destroy(candidate); return leave(m,okay);
}
