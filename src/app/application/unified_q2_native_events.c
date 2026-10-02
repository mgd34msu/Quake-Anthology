#include "unified_q2_native_events.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "qa/game_q2_wire.h"

#include <limits.h>
#include <string.h>

typedef application_unified_json json;
static bool text(json *j, const char *s, qa_error *e) { return application_unified_json_text(j, s, e); }
static bool number(json *j, const char *key, double n, qa_error *e) {
    return text(j, key, e) && application_unified_json_number(j, n, e);
}
static bool string(json *j, const char *key, const char *s, qa_error *e) {
    return text(j, key, e) && application_unified_json_string(j, s ? s : "", e);
}
static bool alias(json *j, qa_application *app, const char *key, qa_string_id id, qa_error *e) {
    const char *s = id ? qa_strings_cstr(qa_session_strings(app->session), id) : "";
    if (!s) return application_fail(e, QA_ERROR_FORMAT, "Q2 event lost its retained Source string");
    return string(j, key, s, e);
}
static bool actor(json *j, const char *key, qa_actor_id id, qa_error *e) {
    return text(j, key, e) && (id.registry ? application_unified_json_actor(j, id, e) : text(j, "null", e));
}
static bool vector(json *j, const char *key, qa_vec3 v, qa_error *e) {
    return text(j, key, e) && application_unified_json_vector(j, v, e);
}
static bool boolean(json *j, const char *key, bool value, qa_error *e) {
    return text(j, key, e) && text(j, value ? "true" : "false", e);
}
static bool item(json *j, qa_application *app, const char *key, qa_item_id id, qa_error *e) {
    return id ? alias(j, app, key, id, e) : text(j, key, e) && text(j, "null", e);
}
static bool begin(json *j, const char *family, const char *kind, qa_error *e) {
    return string(j, "{\"kind\":", family, e) && string(j, ",\"event\":{\"kind\":", kind, e);
}
static bool source(application_provider *p, qa_clock_state *clock, qa_error *e) {
    return (p && p->application && p->constructed && !p->close_pending &&
        p->application->session && qa_session_clock(p->application->session, p->owner, clock)) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Q2 event lost its actual Source clock owner");
}
static application_provider *provider(qa_application *app, qa_actor_owner owner) {
    for (application_provider *p = app->live_providers; p; p = p->next_live)
        if (p->owner == owner) return p;
    return NULL;
}
static bool emit(application_provider *p, json *j, json *simulation, qa_actor_id event_actor,
    qa_actor_id recipient, uint64_t time, const qa_application_q2_audience *audience, qa_error *e) {
    if (!j->bytes.size && (!simulation || !simulation->bytes.size)) return true;
    if (j->bytes.size && !text(j, "}}", e)) return false;
    qa_q2_wire_binding binding = {0};
    qa_error missing = {0};
    bool has_slot = p->kind == APPLICATION_PROVIDER_Q2 &&
        qa_q2_wire_actor(p->state.q2, event_actor, &binding, &missing) &&
        binding.in_use && binding.source_owner == p->owner && binding.source_slot <= INT32_MAX;
    int32_t slot = has_slot ? (int32_t)binding.source_slot : 0;
    qa_bytes presentation = {j->bytes.data, j->bytes.size};
    qa_bytes payload = simulation ? (qa_bytes){simulation->bytes.data, simulation->bytes.size} : (qa_bytes){0};
    if (audience && audience->captured) {
        if (audience->source != p->owner || (audience->count && !audience->recipients))
            return application_fail(e, QA_ERROR_ARGUMENT, "Q2 event audience belongs to another Source");
        for (size_t i = 0; i < audience->count; ++i)
            if (!application_unified_event_emit(p->application, p->owner, presentation, payload,
                audience->recipients[i].actor, audience->recipients[i].actor,
                audience->source_time_ns, slot, has_slot, false, e)) return false;
        return true;
    }
    return application_unified_event_emit(p->application, p->owner, presentation, payload,
        recipient, recipient, time, slot, has_slot, false, e);
}
static const char *level(int value) {
    return value == 3 ? "chat" : value == 2 ? "high" : value == 1 ? "medium" : "low";
}
static bool blend(json *j, const char *key, qa_q2_blend b, qa_error *e) {
    return text(j, key, e) && number(j, "{\"x\":", b.x, e) && number(j, ",\"y\":", b.y, e) &&
        number(j, ",\"z\":", b.z, e) && number(j, ",\"w\":", b.w, e) && text(j, "}", e);
}
static bool player_view(json *j, qa_application *app, const qa_q2_player_view *v, qa_error *e) {
    bool ok = vector(j, ",\"view\":{\"angles\":", v->angles, e) &&
        vector(j, ",\"offset\":", v->offset, e) && vector(j, ",\"kickAngles\":", v->kick_angles, e) &&
        vector(j, ",\"gunAngles\":", v->gun_angles, e) && vector(j, ",\"gunOffset\":", v->gun_offset, e) &&
        blend(j, ",\"blend\":", v->blend, e) && number(j, ",\"fov\":", v->fov, e) &&
        boolean(j, ",\"underwater\":", v->underwater, e) && number(j, ",\"flashes\":", v->flashes, e) &&
        number(j, ",\"health\":", v->health, e) && number(j, ",\"armor\":", v->armor, e) &&
        number(j, ",\"ammo\":", v->ammo, e) && number(j, ",\"score\":", v->score, e) &&
        item(j, app, ",\"selectedItem\":", v->selected_item, e) && text(j, ",\"timer\":", e);
    if (ok && v->timer_item) ok = item(j, app, "{\"item\":", v->timer_item, e) &&
        number(j, ",\"seconds\":", v->timer_seconds, e) && text(j, "}", e);
    else if (ok) ok = text(j, "null", e);
    return ok && boolean(j, ",\"spectator\":", v->spectator, e) &&
        number(j, ",\"layouts\":", v->layouts, e) && text(j, "}", e);
}
bool application_unified_q2_native_player(application_provider *p, const qa_q2_player_event *v,
    qa_error *e) {
    qa_clock_state clock;
    if (!v || !source(p, &clock, e)) return false;
    json j = {0};
    const char *kind = NULL, *family = "q2-player";
    switch (v->kind) {
    case QA_Q2_PLAYER_PRINT: kind = "print"; break;
    case QA_Q2_PLAYER_USERINFO: kind = "userinfo"; break;
    case QA_Q2_PLAYER_STUFFTEXT: kind = "stufftext"; break;
    case QA_Q2_PLAYER_VIEW: kind = "view"; break;
    case QA_Q2_PLAYER_SCOREBOARD: kind = "scoreboard"; break;
    case QA_Q2_PLAYER_INVENTORY: kind = "inventory"; break;
    case QA_Q2_PLAYER_HELP: kind = "help"; break;
    case QA_Q2_PLAYER_LOAD_MENU: kind = "load-menu"; break;
    case QA_Q2_PLAYER_TRAIL: kind = "trail"; break;
    case QA_Q2_PLAYER_CHASE: kind = "chase"; break;
    case QA_Q2_PLAYER_FLASHLIGHT: kind = "flashlight"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_DOGTAG: kind = "player-dogtag"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_RESPAWN_STATUS: kind = "coop-respawn"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_RESTART: kind = "restart-level"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_DIRECTIONAL_DAMAGE: kind = "directional-damage"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_HELP_PATH: kind = "help-path"; family = "q2-rerelease"; break;
    case QA_Q2_PLAYER_ALPHA: kind = "alpha"; family = "q2-rerelease"; break;
    }
    if (!kind) return application_fail(e, QA_ERROR_ARGUMENT, "Unknown Q2 player Source event");
    qa_actor_id recipient = v->kind == QA_Q2_PLAYER_PRINT ? v->target : v->actor;
    bool ok = begin(&j, family, kind, e);
    if (ok && v->kind != QA_Q2_PLAYER_PRINT && v->kind != QA_Q2_PLAYER_RESTART)
        ok = actor(&j, ",\"actor\":", v->actor, e);
    switch (v->kind) {
    case QA_Q2_PLAYER_PRINT:
        ok = ok && actor(&j, ",\"target\":", v->target, e) && string(&j, ",\"level\":", level(v->level), e) &&
            string(&j, ",\"text\":", v->text, e); break;
    case QA_Q2_PLAYER_USERINFO:
        ok = ok && number(&j, ",\"slot\":", v->slot, e) && string(&j, ",\"name\":", v->text, e) &&
            string(&j, ",\"skin\":", v->skin, e); recipient = (qa_actor_id){0}; break;
    case QA_Q2_PLAYER_STUFFTEXT: ok = ok && string(&j, ",\"text\":", v->text, e); break;
    case QA_Q2_PLAYER_VIEW: ok = ok && player_view(&j, p->application, &v->view, e); break;
    case QA_Q2_PLAYER_SCOREBOARD:
        ok = ok && actor(&j, ",\"killer\":", v->target, e) && boolean(&j, ",\"reliable\":", v->reliable, e) &&
            text(&j, ",\"rows\":[", e);
        for (size_t i = 0; ok && i < v->count; ++i) {
            const qa_q2_score_row *r = v->scores + i;
            ok = (!i || text(&j, ",", e)) && number(&j, "{\"slot\":", r->slot, e) &&
                string(&j, ",\"name\":", r->name, e) && number(&j, ",\"score\":", r->score, e) &&
                number(&j, ",\"ping\":", r->ping, e) && number(&j, ",\"minutes\":", r->minutes, e) &&
                boolean(&j, ",\"spectator\":", r->spectator, e) && text(&j, "}", e);
        }
        ok = ok && text(&j, "]", e); break;
    case QA_Q2_PLAYER_INVENTORY:
        ok = ok && boolean(&j, ",\"visible\":", v->visible, e) &&
            item(&j, p->application, ",\"selected\":", v->selected_item, e) && text(&j, ",\"entries\":[", e);
        for (size_t i = 0; ok && i < v->count; ++i) {
            const qa_inventory_entry *r = v->inventory + i;
            ok = (!i || text(&j, ",", e)) && item(&j, p->application, "{\"item\":", r->item, e) &&
                number(&j, ",\"count\":", r->count, e) && number(&j, ",\"capacity\":", r->capacity, e);
            if (ok && r->policy == QA_COUNT_STACK) ok = text(&j, ",\"countPolicy\":{\"kind\":\"stack\"}", e);
            else if (ok) ok = string(&j, ",\"countPolicy\":{\"kind\":\"source-counter\",\"arithmetic\":",
                r->policy == QA_COUNT_SOURCE_INT32 ? "int32" :
                r->policy == QA_COUNT_SOURCE_DOUBLE ? "binary64" : "binary32", e) && text(&j, "}", e);
            ok = ok && text(&j, "}", e);
        }
        ok = ok && text(&j, "]", e); break;
    case QA_Q2_PLAYER_HELP: ok = ok && boolean(&j, ",\"visible\":", v->visible, e); break;
    case QA_Q2_PLAYER_LOAD_MENU: break;
    case QA_Q2_PLAYER_TRAIL: ok = ok && vector(&j, ",\"origin\":", v->origin, e) &&
        number(&j, ",\"time\":", (double)v->time_ns / 1e9, e); break;
    case QA_Q2_PLAYER_CHASE: ok = ok && actor(&j, ",\"target\":", v->target, e); break;
    case QA_Q2_PLAYER_FLASHLIGHT: {
        static const char *const hands[] = {"right", "left", "center"};
        if ((unsigned)v->hand >= 3) { ok = application_fail(e, QA_ERROR_ARGUMENT, "Invalid Q2 Source hand"); break; }
        ok = ok && boolean(&j, ",\"enabled\":", v->visible, e) && string(&j, ",\"hand\":", hands[v->hand], e); break;
    }
    case QA_Q2_PLAYER_DOGTAG: ok = ok && string(&j, ",\"value\":", v->text, e); break;
    case QA_Q2_PLAYER_RESPAWN_STATUS: {
        static const char *const states[] = {"none", "in-combat", "bad-area", "blocked", "waiting", "no-lives"};
        if ((unsigned)v->respawn_status >= 6) { ok = application_fail(e, QA_ERROR_ARGUMENT, "Invalid Q2 Source respawn status"); break; }
        ok = ok && string(&j, ",\"state\":", states[v->respawn_status], e) && number(&j, ",\"lives\":", v->lives, e); break;
    }
    case QA_Q2_PLAYER_RESTART: ok = ok && string(&j, ",\"map\":", v->text, e); break;
    case QA_Q2_PLAYER_DIRECTIONAL_DAMAGE: ok = ok && vector(&j, ",\"direction\":", v->direction, e) &&
        number(&j, ",\"damage\":", v->damage, e) && boolean(&j, ",\"health\":", v->health, e) &&
        boolean(&j, ",\"armor\":", v->armor, e) && boolean(&j, ",\"shield\":", v->shield, e); break;
    case QA_Q2_PLAYER_HELP_PATH: ok = ok && boolean(&j, ",\"first\":", v->first, e) &&
        vector(&j, ",\"position\":", v->origin, e) && vector(&j, ",\"direction\":", v->direction, e); break;
    case QA_Q2_PLAYER_ALPHA: ok = ok && number(&j, ",\"alpha\":", v->alpha, e); break;
    }
    if (ok) ok = emit(p, &j, NULL, v->actor, recipient, clock.frame.time_ns, NULL, e);
    application_unified_json_dispose(&j);
    return ok;
}

static bool arguments(json *j, qa_application *app, const qa_builtin_message_arg *args,
    size_t count, qa_error *e) {
    if (!text(j, ",\"args\":[", e)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (args[i].kind != QA_BUILTIN_MESSAGE_STRING)
            return application_fail(e, QA_ERROR_UNSUPPORTED, "Q2 localized Source event requires its authored string argument");
        if ((i && !text(j, ",", e)) || !alias(j, app, "", args[i].value.text, e)) return false;
    }
    return text(j, "]", e);
}
static bool fog(json *j, const qa_q2_fog *f, qa_error *e) {
    return number(j, ",\"value\":{\"fog\":{\"density\":", f->density, e) &&
        vector(j, ",\"color\":", f->color, e) && number(j, ",\"skyFactor\":", f->sky_factor, e) &&
        vector(j, "},\"heightFog\":{\"startColor\":", f->start_color, e) &&
        number(j, ",\"startDistance\":", f->start_distance, e) && vector(j, ",\"endColor\":", f->end_color, e) &&
        number(j, ",\"endDistance\":", f->end_distance, e) && number(j, ",\"falloff\":", f->falloff, e) &&
        number(j, ",\"density\":", f->height_density, e) && text(j, "}}", e);
}
bool application_unified_q2_native_map(application_provider *p, const qa_q2_map_event *v,
    const qa_application_q2_audience *audience, qa_error *e) {
    qa_clock_state clock;
    if (!v || !source(p, &clock, e)) return false;
    json j = {0};
    qa_application *app = p->application;
    qa_actor_id recipient = v->recipient;
    bool ok = true;
    switch (v->kind) {
    case QA_Q2_MAP_HELP:
        ok = begin(&j, "q2", "help", e) && number(&j, ",\"slot\":", v->slot, e) &&
            alias(&j, app, ",\"text\":", v->text, e); break;
    case QA_Q2_MAP_MUSIC:
        ok = begin(&j, "q2", "music", e) && alias(&j, app, ",\"track\":", v->resource ? v->resource : v->text, e); break;
    case QA_Q2_MAP_LIGHTSTYLE:
        ok = begin(&j, "q2", "lightstyle", e) && number(&j, ",\"style\":", v->style, e) &&
            alias(&j, app, ",\"pattern\":", v->text, e); break;
    case QA_Q2_MAP_SKY:
        ok = begin(&j, "q2-rerelease", "sky", e) && alias(&j, app, ",\"name\":", v->resource, e) &&
            number(&j, ",\"rotation\":", v->value, e) && boolean(&j, ",\"autoRotate\":", (v->flags & 1u) != 0, e) &&
            vector(&j, ",\"axis\":", v->direction, e); break;
    case QA_Q2_MAP_STORY:
        ok = begin(&j, "q2-rerelease", "story", e) && alias(&j, app, ",\"text\":", v->text, e); break;
    case QA_Q2_MAP_FOG:
        ok = begin(&j, "q2-rerelease", "fog", e) && actor(&j, ",\"actor\":", recipient, e) &&
            fog(&j, &v->fog, e) && number(&j, ",\"transitionMilliseconds\":", (double)v->duration * 1000, e); break;
    case QA_Q2_MAP_POI:
        ok = begin(&j, "q2-rerelease", "poi", e) && actor(&j, ",\"actor\":", recipient, e) &&
            vector(&j, ",\"position\":", v->origin, e) && alias(&j, app, ",\"image\":", v->resource, e) &&
            number(&j, ",\"duration\":", v->duration, e) && number(&j, ",\"color\":", v->count, e); break;
    case QA_Q2_MAP_REMOVE_POI:
        ok = begin(&j, "q2-rerelease", "remove-poi", e) && actor(&j, ",\"actor\":", recipient, e) &&
            number(&j, ",\"key\":", v->slot, e); break;
    case QA_Q2_MAP_HEALTHBAR:
        ok = begin(&j, "q2-rerelease", "healthbar", e) && actor(&j, ",\"actor\":", recipient, e) &&
            actor(&j, ",\"target\":", v->target, e) && number(&j, ",\"slot\":", v->slot, e) &&
            alias(&j, app, ",\"name\":", v->text, e) && number(&j, ",\"fraction\":", v->value, e) &&
            boolean(&j, ",\"visible\":", v->visible, e); break;
    case QA_Q2_MAP_WORLD_TEXT: break; /* Retained by the canonical world-text owner. */
    case QA_Q2_MAP_STEAM:
        ok = begin(&j, "q2-composition", "missionpack-entity", e) && text(&j, ",\"event\":{\"kind\":\"steam\"", e) &&
            number(&j, ",\"id\":", v->slot, e) && vector(&j, ",\"origin\":", v->origin, e) &&
            vector(&j, ",\"direction\":", v->direction, e) && number(&j, ",\"count\":", v->count, e) &&
            number(&j, ",\"color\":", v->style, e) && number(&j, ",\"speed\":", v->value, e) &&
            number(&j, ",\"milliseconds\":", v->duration, e) && text(&j, "}", e); break;
    case QA_Q2_MAP_FORCE_WALL:
        ok = begin(&j, "q2-composition", "missionpack-entity", e) && text(&j, ",\"event\":{\"kind\":\"force-wall\"", e) &&
            vector(&j, ",\"start\":", v->origin, e) && vector(&j, ",\"end\":", v->direction, e) &&
            number(&j, ",\"color\":", v->style, e) && text(&j, "}", e); break;
    case QA_Q2_MAP_AUTOSAVE: ok = begin(&j, "q2-rerelease", "autosave", e); break;
    case QA_Q2_MAP_ACHIEVEMENT:
        ok = begin(&j, "q2-rerelease", "achievement", e) && alias(&j, app, ",\"id\":", v->text, e); break;
    case QA_Q2_MAP_SCREEN_BLEND:
        ok = begin(&j, "q2-rerelease", "screen-blend", e) && actor(&j, ",\"actor\":", recipient, e) &&
            blend(&j, ",\"blend\":", (qa_q2_blend){v->color.x, v->color.y, v->color.z, v->alpha}, e); break;
    case QA_Q2_MAP_DYNAMIC_LIGHT:
        ok = begin(&j, "q2", "dynamic-light", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            vector(&j, ",\"origin\":", v->origin, e) && vector(&j, ",\"color\":", v->color, e) &&
            boolean(&j, ",\"visible\":", v->visible, e) && number(&j, ",\"radius\":", v->radius, e) &&
            number(&j, ",\"intensity\":", v->intensity, e) && number(&j, ",\"resolution\":", v->resolution, e) &&
            number(&j, ",\"fadeStart\":", v->fade_start, e) && number(&j, ",\"fadeEnd\":", v->fade_end, e) &&
            number(&j, ",\"lightstyle\":", v->style, e) && text(&j, ",\"cone\":", e);
        if (ok && (v->flags & 1u)) ok = vector(&j, "{\"direction\":", v->direction, e) &&
            number(&j, ",\"cosHalfAngle\":", v->cone_cosine, e) && text(&j, "}", e);
        else if (ok) ok = text(&j, "null", e);
        break;
    case QA_Q2_MAP_MISSION_STATUS:
        ok = begin(&j, "q2-rerelease", "mission-status", e) && actor(&j, ",\"actor\":", recipient, e) &&
            boolean(&j, ",\"iconVisible\":", v->visible, e); break;
    case QA_Q2_MAP_MISSION_OBJECTIVE:
        ok = begin(&j, "q2-rerelease", "mission-objective", e) && actor(&j, ",\"actor\":", recipient, e) &&
            alias(&j, app, ",\"text\":", v->text, e) && arguments(&j, app, v->arguments, v->argument_count, e) &&
            boolean(&j, ",\"talkSound\":", (v->flags & 1u) != 0, e); break;
    case QA_Q2_MAP_HELP_COMPUTER:
        ok = begin(&j, "q2-rerelease", "help-computer", e) && actor(&j, ",\"actor\":", recipient, e) &&
            boolean(&j, ",\"visible\":", v->visible, e) && alias(&j, app, ",\"primary\":", v->text, e) &&
            alias(&j, app, ",\"secondary\":", v->resource, e) && boolean(&j, ",\"slowTime\":", (v->flags & 1u) != 0, e); break;
    case QA_Q2_MAP_GOAL:
    case QA_Q2_MAP_SECRET: break;
    case QA_Q2_MAP_END_UNIT:
        if (v->level_count>QA_Q2_CAMPAIGN_LEVEL_LIMIT || (v->level_count && !v->levels)) {
            ok=application_fail(e,QA_ERROR_ARGUMENT,"Q2 unit report lost its actual campaign rows"); break;
        }
        ok=begin(&j,"q2-rerelease","end-of-unit",e) && text(&j,",\"levels\":[",e);
        for (size_t i=0;ok && i<v->level_count;++i) {
            const qa_q2_campaign_level *row=v->levels+i;
            ok=(!i || text(&j,",",e)) && alias(&j,app,"{\"map\":",row->map,e) &&
                alias(&j,app,",\"name\":",row->name,e) && number(&j,",\"visitOrder\":",row->visit_order,e) &&
                number(&j,",\"totalSecrets\":",row->total_secrets,e) &&
                number(&j,",\"foundSecrets\":",row->found_secrets,e) &&
                number(&j,",\"totalMonsters\":",row->total_monsters,e) &&
                number(&j,",\"killedMonsters\":",row->killed_monsters,e) &&
                number(&j,",\"time\":",row->time_seconds,e) && text(&j,"}",e);
        }
        ok=ok && text(&j,"]",e) && number(&j,",\"buttonTime\":",(double)v->button_time_ns/1e9,e);
        break;
    }
    if (ok) ok = emit(p, &j, NULL, v->actor, recipient, clock.frame.time_ns, audience, e);
    application_unified_json_dispose(&j);
    return ok;
}

static const char *damage_effect(int code) {
    switch (code) {
    case 0: return "gunshot";
    case 1: return "blood";
    case 4: return "shotgun";
    case 9: return "sparks";
    case 12: return "screen-sparks";
    case 13: return "shield-sparks";
    case 14: return "bullet-sparks";
    case 26: return "greenblood";
    case 42: return "moreblood";
    case 46: return "electric-sparks";
    default: return NULL;
    }
}
static bool sound_simulation(json *j, qa_application *app, const qa_builtin_event *v,
    qa_error *e) {
    const char *path = qa_strings_cstr(qa_session_strings(app->session), v->resource);
    if (!path) return application_fail(e, QA_ERROR_FORMAT, "Q2 sound lost its declared Source path");
    char key[81];
    bool found;
    if (!application_unified_event_resource_lookup(app, v->provider, path, key, &found, e)) return false;
    if (!found) return true;
    return string(j, "{\"kind\":\"sound\",\"resource\":", key, e) &&
        actor(j, ",\"actor\":", v->actor, e) && vector(j, ",\"origin\":", v->origin, e) &&
        number(j, ",\"channel\":", v->channel, e) && number(j, ",\"volume\":", v->volume, e) &&
        number(j, ",\"attenuation\":", v->attenuation, e) && text(j, "}", e);
}
bool application_unified_q2_native_visual(application_provider *p, qa_actor_id id,
    const qa_q2_visual *visual, qa_error *e) {
    qa_clock_state clock;
    if (!visual || !source(p, &clock, e)) return false;
    json j = {0};
    bool ok = begin(&j, "q2", "model", e) && actor(&j, ",\"actor\":", id, e) &&
        alias(&j, p->application, ",\"path\":", visual->models[0], e) && text(&j, ",\"attachedModels\":[", e);
    bool first = true;
    for (size_t i = 1; ok && i < 4; ++i) {
        if (!visual->models[i]) continue;
        ok = (first || text(&j, ",", e)) && alias(&j, p->application, "", visual->models[i], e);
        first = false;
    }
    ok = ok && text(&j, "]", e) && number(&j, ",\"frame\":", visual->frame, e) &&
        number(&j, ",\"oldFrame\":", visual->old_frame, e) && number(&j, ",\"scale\":", visual->scale, e) &&
        number(&j, ",\"alpha\":", visual->alpha, e) && number(&j, ",\"skin\":", visual->skin, e) &&
        number(&j, ",\"effects\":", (double)visual->effects, e) && number(&j, ",\"renderFlags\":", visual->render_flags, e);
    if (ok) ok = emit(p, &j, NULL, id, (qa_actor_id){0}, clock.frame.time_ns, NULL, e);
    application_unified_json_dispose(&j);
    if (!ok) return false;
    ok = begin(&j, "q2", "visibility", e) && actor(&j, ",\"actor\":", id, e) &&
        boolean(&j, ",\"visible\":", visual->visible, e) &&
        emit(p, &j, NULL, id, (qa_actor_id){0}, clock.frame.time_ns, NULL, e);
    application_unified_json_dispose(&j);
    return ok;
}
bool application_unified_q2_native_builtin(qa_application *app, const qa_builtin_event *v,
    const qa_application_q2_audience *audience, qa_error *e) {
    if (!v || v->family != QA_GAME_Q2) return true;
    application_provider *p = provider(app, v->provider);
    qa_clock_state clock;
    if (!source(p, &clock, e)) return false;
    json j = {0}, simulation = {0};
    qa_actor_id recipient = {0};
    bool ok = true;
    switch (v->kind) {
    case QA_BUILTIN_SOUND:
    case QA_BUILTIN_STOP_SOUND:
        ok = begin(&j, "q2", "sound", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            vector(&j, ",\"origin\":", v->origin, e) && alias(&j, app, ",\"path\":", v->resource, e) &&
            number(&j, ",\"channel\":", v->channel, e) && number(&j, ",\"volume\":", v->volume, e) &&
            number(&j, ",\"attenuation\":", v->attenuation, e) && boolean(&j, ",\"reliable\":", false, e) &&
            string(&j, ",\"loop\":", v->kind == QA_BUILTIN_STOP_SOUND ? "stop" : (v->flags & 1u) ? "start" : "once", e);
        if (ok && v->kind == QA_BUILTIN_SOUND && !(v->flags & 1u)) ok = sound_simulation(&simulation, app, v, e);
        break;
    case QA_BUILTIN_CENTERPRINT:
        recipient = v->actor;
        ok = begin(&j, "q2", "centerprint", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            alias(&j, app, ",\"text\":", v->text, e); break;
    case QA_BUILTIN_MESSAGE:
        if (v->argument_count) {
            ok = begin(&j, "q2-rerelease", "localized-print", e) && actor(&j, ",\"actor\":", v->actor, e) &&
                string(&j, ",\"level\":", level(v->code), e) && alias(&j, app, ",\"text\":", v->text, e) &&
                arguments(&j, app, v->arguments, v->argument_count, e);
        } else ok = begin(&j, "q2", "print", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            string(&j, ",\"level\":", level(v->code), e) && alias(&j, app, ",\"text\":", v->text, e);
        recipient = v->actor; break;
    case QA_BUILTIN_MUZZLE: {
        const char *path = v->resource ? qa_strings_cstr(qa_session_strings(app->session), v->resource) : NULL;
        bool monster = path && !strcmp(path, "q2:monster-muzzle");
        ok = begin(&j, monster ? "q2" : "q2-weapon", monster ? "monster-muzzleflash" : "muzzleflash", e) &&
            actor(&j, ",\"actor\":", v->actor, e) && number(&j, ",\"flash\":", v->code, e);
        if (monster) {
            ok = ok && vector(&j, ",\"origin\":", v->origin, e) && vector(&j, ",\"direction\":", v->direction, e);
            if (v->has_muzzle_pose) ok = ok && vector(&j, ",\"angles\":", v->muzzle_angles, e) &&
                number(&j, ",\"scale\":", v->muzzle_scale, e);
        }
        else ok = ok && boolean(&j, ",\"silenced\":", (v->flags & 128u) != 0, e);
        break;
    }
    case QA_BUILTIN_ANIMATION: {
        qa_q2_visual visual;
        if (p->kind != APPLICATION_PROVIDER_Q2 || !qa_q2_presentation_read(p->state.q2, v->actor, &visual)) break;
        ok = begin(&j, "q2", "model", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            alias(&j, app, ",\"path\":", visual.models[0], e) && text(&j, ",\"attachedModels\":[", e);
        bool first = true;
        for (size_t i = 1; ok && i < 4; ++i) {
            if (!visual.models[i]) continue;
            ok = (first || text(&j, ",", e)) && alias(&j, app, "", visual.models[i], e);
            first = false;
        }
        ok = ok && text(&j, "]", e) && number(&j, ",\"frame\":", visual.frame, e) &&
            number(&j, ",\"oldFrame\":", visual.old_frame, e) && number(&j, ",\"scale\":", visual.scale, e) &&
            number(&j, ",\"alpha\":", visual.alpha, e) && number(&j, ",\"skin\":", visual.skin, e) &&
            number(&j, ",\"effects\":", (double)visual.effects, e) && number(&j, ",\"renderFlags\":", visual.render_flags, e);
        break;
    }
    case QA_BUILTIN_Q2_PLAYER_ANIMATION: {
        static const char *const priorities[] = {"attack", "pain", "reverse"};
        if ((unsigned)v->code >= 3) {
            ok = application_fail(e, QA_ERROR_ARGUMENT, "Q2 player animation has an unknown Source priority");
            break;
        }
        ok = begin(&j, "q2-weapon", "player-animation", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            string(&j, ",\"priority\":", priorities[v->code], e) && number(&j, ",\"first\":", v->frame, e) &&
            number(&j, ",\"last\":", v->count, e) && boolean(&j, ",\"resetTime\":", (v->flags & 1u) != 0, e);
        break;
    }
    case QA_BUILTIN_Q2_ENTITY_EVENT:
        ok = begin(&j, "q2", "entity-event", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            number(&j, ",\"event\":", v->code, e);
        break;
    case QA_BUILTIN_BEAM: {
        const char *path = v->resource ? qa_strings_cstr(qa_session_strings(app->session), v->resource) : NULL;
        if (!path) {
            ok = begin(&j, "q2", "beam", e) && actor(&j, ",\"actor\":", v->actor, e) &&
                vector(&j, ",\"start\":", v->origin, e) && vector(&j, ",\"end\":", v->end, e) &&
                number(&j, ",\"width\":", v->value, e) && number(&j, ",\"color\":", (uint32_t)v->code, e) &&
                boolean(&j, ",\"visible\":", (v->flags & 1u) != 0, e);
            break;
        }
        if (!strcmp(path, "q2:parasite") || !strcmp(path, "q2:medic-cable")) {
            ok = begin(&j, "q2", "monster-beam", e) &&
                string(&j, ",\"effect\":", !strcmp(path, "q2:parasite") ? "parasite" : "medic", e) &&
                actor(&j, ",\"actor\":", v->actor, e) && vector(&j, ",\"start\":", v->origin, e) &&
                vector(&j, ",\"end\":", v->end, e);
            break;
        }
        if (!strcmp(path, "q2:grapple-cable")) {
            ok = begin(&j, "q2-composition", (v->flags & 1u) ? "lmctf" : "ctf", e) &&
                text(&j, ",\"event\":{\"kind\":\"grapple-cable\"", e) && actor(&j, ",\"actor\":", v->actor, e) &&
                vector(&j, ",\"start\":", v->origin, e) && vector(&j, ",\"end\":", v->end, e) &&
                vector(&j, ",\"offset\":", v->direction, e) && text(&j, "}", e);
            break;
        }
        if (strncmp(path, "q2:", 3)) break;
        const char *effect = path + 3;
        if (strcmp(effect, "rail") && strcmp(effect, "rail-water") && strcmp(effect, "bubble-trail") &&
            strcmp(effect, "bfg-laser") && strcmp(effect, "bfg-zap") && strcmp(effect, "bfg-lightning") &&
            strcmp(effect, "heatbeam") && strcmp(effect, "monster-heatbeam")) break;
        ok = begin(&j, "q2-weapon", "beam", e) && string(&j, ",\"effect\":", effect, e) &&
            actor(&j, ",\"actor\":", v->actor, e) && vector(&j, ",\"start\":", v->origin, e) &&
            vector(&j, ",\"end\":", v->end, e) && number(&j, ",\"duration\":", v->value, e);
        break;
    }
    case QA_BUILTIN_PARTICLES:
    case QA_BUILTIN_IMPACT:
    case QA_BUILTIN_EXPLOSION:
    case QA_BUILTIN_EFFECT:
    case QA_BUILTIN_TELEPORT: {
        const char *effect = v->resource ? qa_strings_cstr(qa_session_strings(app->session), v->resource) : NULL;
        if (effect && !strncmp(effect, "q2:", 3)) effect += 3;
        if (v->kind == QA_BUILTIN_EFFECT && effect && !strcmp(effect, "entity-event")) {
            ok = begin(&j, "q2", "entity-event", e) && actor(&j, ",\"actor\":", v->actor, e) &&
                number(&j, ",\"event\":", v->code, e);
            break;
        }
        if (!effect && v->kind == QA_BUILTIN_PARTICLES) effect = damage_effect(v->code);
        if (!effect) break;
        bool palette = !strcmp(effect, "splash") || !strcmp(effect, "laser-sparks") ||
            !strcmp(effect, "laser_sparks") || !strcmp(effect, "tunnel-sparks");
        ok = begin(&j, "q2", "effect", e) && string(&j, ",\"effect\":", effect, e) &&
            vector(&j, ",\"origin\":", v->origin, e) && vector(&j, ",\"direction\":", v->direction, e) &&
            number(&j, ",\"count\":", v->count, e) && number(&j, ",\"color\":", palette ? v->code : 0, e);
        break;
    }
    case QA_BUILTIN_ITEM:
        if (v->code == 1) ok = begin(&j, "q2", "entity-event", e) && actor(&j, ",\"actor\":", v->actor, e) &&
            number(&j, ",\"event\":", 2, e);
        else if (v->code == 0) {
            recipient = v->actor;
            ok = begin(&j, "q2", "pickup", e) && actor(&j, ",\"player\":", v->actor, e) &&
                item(&j, app, ",\"item\":", v->item, e) && alias(&j, app, ",\"icon\":", v->resource, e) &&
                alias(&j, app, ",\"name\":", v->text, e);
        }
        break;
    default: break;
    }
    if (ok) ok = emit(p, &j, &simulation, v->actor, recipient, v->time_ns, audience, e);
    application_unified_json_dispose(&j);
    application_unified_json_dispose(&simulation);
    return ok;
}
