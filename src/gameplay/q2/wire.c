#include "internal.h"
#include "entities/internal.h"
#include "player/internal.h"
#include "monsters/internal.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"
#include <math.h>

#define Q2_STATUS_VITALS \
    "yb -24 xv 0 hnum xv 50 pic 0 " \
    "if 2 xv 100 anum xv 150 pic 2 endif " \
    "if 4 xv 200 rnum xv 250 pic 4 endif " \
    "if 6 xv 296 pic 6 endif yb -50 "
#define Q2_STATUS_TIMER \
    "if 9 xv 262 num 2 10 xv 296 pic 9 endif "
#define Q2_STATUS_SCORE \
    "xr -50 yt 2 num 3 14 " \
    "if 17 xv 0 yb -58 string2 \"SPECTATOR MODE\" endif " \
    "if 16 xv 0 yb -68 string \"CHASING\" xv 64 stat_string 16 endif "
#define Q2_CLASSIC_PICKUP \
    "if 7 xv 0 pic 7 xv 26 yb -42 stat_string 8 yb -50 endif "
#define Q2_RERELEASE_PICKUP \
    "if 7 xv 0 pic 7 xv 26 yb -42 loc_stat_string 8 yb -50 endif " \
    "if 51 yb -34 xv 319 loc_stat_rstring 51 yb -58 endif "
const char *qa_q2_wire_statusbar(const qa_q2_game *g)
{
    static const char classic_single[] = Q2_STATUS_VITALS Q2_CLASSIC_PICKUP Q2_STATUS_TIMER
        "if 11 xv 148 pic 11 endif ";
    static const char classic_deathmatch[] = Q2_STATUS_VITALS Q2_CLASSIC_PICKUP
        "if 9 xv 246 num 2 10 xv 296 pic 9 endif if 11 xv 148 pic 11 endif " Q2_STATUS_SCORE;
    static const char rerelease_single[] = Q2_STATUS_VITALS Q2_RERELEASE_PICKUP Q2_STATUS_TIMER
        "yb -50 if 11 xv 150 pic 11 endif "
        "if 9 yb -76 endif if 51 yb -58 if 9 yb -84 endif endif "
        "if 44 xv 296 pic 44 endif if 45 xv 272 pic 45 endif if 46 xv 248 pic 46 endif "
        "if 52 yt 24 health_bars endif story ";
    static const char rerelease_coop[] = Q2_STATUS_VITALS Q2_RERELEASE_PICKUP Q2_STATUS_TIMER
        "yb -50 if 11 xv 150 pic 11 endif "
        "if 9 yb -76 endif if 51 yb -58 if 9 yb -84 endif endif "
        "if 44 xv 296 pic 44 endif if 45 xv 272 pic 45 endif if 46 xv 248 pic 46 endif "
        "if 48 xv 0 yt 0 loc_stat_cstring2 48 endif "
        "if 49 xr -16 yt 2 lives_num 49 xr 0 yt 28 loc_rstring 0 \"$g_lives\" endif "
        "if 52 yt 24 health_bars endif story ";
    static const char rerelease_deathmatch[] = Q2_STATUS_VITALS Q2_RERELEASE_PICKUP Q2_STATUS_TIMER
        "yb -50 if 11 xv 150 pic 11 endif " Q2_STATUS_SCORE
        "if 27 yb -137 xr -26 pic 27 endif ";
    if (g->options.edition == QA_Q2_CLASSIC)
        return g->options.deathmatch ? classic_deathmatch : classic_single;
    return g->options.deathmatch ? rerelease_deathmatch :
        g->options.cooperative ? rerelease_coop : rerelease_single;
}
#undef Q2_STATUS_VITALS
#undef Q2_STATUS_TIMER
#undef Q2_STATUS_SCORE
#undef Q2_CLASSIC_PICKUP
#undef Q2_RERELEASE_PICKUP

static void stat_word(int32_t value, int16_t *out)
{
    uint16_t bits = (uint16_t)(uint32_t)value;
    memcpy(out, &bits, sizeof(bits));
}
static bool stat_number(double value, int16_t *out, qa_error *error)
{
    if (!isfinite(value)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source statistic is not finite");
        return false;
    }
    stat_word(value >= INT32_MIN && value < 2147483648.0 ? (int32_t)value : INT32_MIN, out);
    return true;
}
static bool stat_image(const qa_q2_wire_stat_resources *resources, const char *name,
    int16_t *out, qa_error *error)
{
    uint32_t image = 0;
    if (name && *name && !resources->image(resources->context, name, &image, error)) return false;
    if (image > INT16_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source HUD image exceeds its stat word");
        return false;
    }
    *out = (int16_t)image;
    return true;
}
bool qa_q2_wire_stats(const qa_q2_game *g, const qa_q2_player_view *view,
    const qa_q2_wire_stat_resources *resources, int16_t out[64], qa_error *error)
{
    qa_strings *strings = qa_session_strings(g->services.session);
    int16_t stats[64] = {0};
    if (!stat_number(view->health, &stats[1], error) ||
        !stat_number(view->armor, &stats[5], error) ||
        !stat_image(resources, "i_health", &stats[0], error) ||
        !stat_image(resources, qa_strings_cstr(strings, view->ammo_icon), &stats[2], error) ||
        !stat_image(resources, qa_strings_cstr(strings, view->armor_icon), &stats[4], error) ||
        !stat_image(resources, qa_strings_cstr(strings, view->pickup_icon), &stats[7], error) ||
        !stat_image(resources, qa_strings_cstr(strings, view->help_icon), &stats[11], error)) return false;
    stat_word(view->ammo_count, &stats[3]); stat_word(view->timer_seconds, &stats[10]);
    stat_word(view->layouts, &stats[13]); stat_word(view->score, &stats[14]);
    stat_word(view->flashes, &stats[15]); stats[17] = view->spectator ? 1 : 0;
    const char *pickup = qa_strings_cstr(strings, view->pickup_text);
    const char *selected_name = qa_strings_cstr(strings, view->selected_item_name);
    for (size_t i = 0; i < qa_q2_item_count(g); ++i) {
        const qa_q2_item_definition *item = qa_q2_item_at(g, i);
        if (view->selected_item && item->item == view->selected_item) {
            stat_word((int32_t)i + 1, &stats[12]);
            if (!stat_image(resources, item->icon, &stats[6], error)) return false;
        }
        if (view->timer_item && item->item == view->timer_item &&
            !stat_image(resources, item->icon, &stats[9], error)) return false;
        if (pickup && item->name && !strcmp(pickup, item->name))
            stat_word((int32_t)resources->items_base + (int32_t)i + 1, &stats[8]);
        if (selected_name && item->name && !strcmp(selected_name, item->name))
            stat_word((int32_t)resources->items_base + (int32_t)i + 1, &stats[51]);
    }
    if (g->options.edition == QA_Q2_RERELEASE) {
        stat_word(view->hit_marker_damage, &stats[50]);
        for (unsigned i = 0; i < 3; ++i)
            if (!stat_image(resources, qa_strings_cstr(strings, view->key_icons[i]), &stats[44 + i], error)) return false;
    } else stats[51] = 0;
    memcpy(out, stats, sizeof(stats));
    return true;
}

static bool idle(const qa_q2_game *g, qa_error *error)
{
    return (g && !g->current_actor.registry && !g->restoring_continuation &&
        !g->continuation_pending && !g->continuation_failed && !g->release_failed &&
        qa_session_safe(g->services.session) && qa_world_idle(g->services.world)) ||
        (qa_error_set(error, QA_ERROR_ARGUMENT, 0,
            "Q2 wire observation requires its completed GAME owner"), false);
}

bool qa_q2_wire_configure(qa_q2_game *g, uint32_t capacity,
    uint32_t clients, qa_error *error)
{
    if (!idle(g, error)) return false;
    if (g->first_actor || !clients || clients > 256 || capacity <= clients || capacity > 65536) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
            "Q2 wire construction requires an empty representable source edict table");
        return false;
    }
    qa_actor_id *actors = calloc(capacity, sizeof(*actors));
    uint64_t *freed = calloc(capacity, sizeof(*freed));
    if (!actors || !freed) {
        free(actors); free(freed);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 physical source edicts");
        return false;
    }
    free(g->wire_actors); free(g->wire_freed_ns);
    free(g->wire_references); g->wire_references = NULL;
    g->wire_reference_count = g->wire_reference_capacity = 0;
    g->wire_actors = actors; g->wire_freed_ns = freed;
    g->wire_capacity = capacity; g->wire_clients = clients; g->wire_extent = clients + 1;
    g->wire_frame = 0;
    return true;
}

void q2_wire_reset(qa_q2_game *g)
{
    memset(g->wire_actors, 0, g->wire_capacity * sizeof(*g->wire_actors));
    memset(g->wire_freed_ns, 0, g->wire_capacity * sizeof(*g->wire_freed_ns));
    g->wire_extent = g->wire_clients + 1; g->wire_frame = 0;
    g->wire_reference_count = 0;
    memset(g->wire_lightstyles, 0, sizeof(g->wire_lightstyles));
    memset(g->wire_shadows, 0, sizeof(g->wire_shadows)); g->wire_shadow_count = 0;
    g->wire_music = 0; g->wire_music_present = false;
}

static bool bind(qa_q2_game *g, q2_actor *a, qa_actor_id id,
    uint32_t slot, qa_error *error)
{
    if (slot >= g->wire_capacity ||
        (g->wire_actors[slot].registry && !qa_actor_id_equal(g->wire_actors[slot], id))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot,
            "Q2 physical source edict is occupied or outside its admitted table");
        return false;
    }
    if (a->wire_bound && a->wire_slot != slot &&
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], id)) {
            qa_error_set(error, QA_ERROR_FORMAT, a->wire_slot,
                "Q2 actor lost its prior physical source edict");
            return false;
    }
    size_t reference = 0;
    while (reference < g->wire_reference_count &&
        !qa_actor_id_equal(g->wire_references[reference].actor, id)) ++reference;
    if (reference == g->wire_reference_capacity) {
        size_t capacity = reference ? reference * 2 : 32;
        if (capacity < reference || capacity > SIZE_MAX / sizeof(*g->wire_references)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 Engine provenance extent overflows"); return false;
        }
        q2_wire_reference *rows = realloc(g->wire_references, capacity * sizeof(*rows));
        if (!rows) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q2 Engine admission provenance"); return false; }
        g->wire_references = rows; g->wire_reference_capacity = capacity;
    }
    g->wire_references[reference] = (q2_wire_reference){id, slot};
    if (reference == g->wire_reference_count) ++g->wire_reference_count;
    if (a->wire_bound && a->wire_slot != slot) {
        g->wire_actors[a->wire_slot] = (qa_actor_id){0};
        g->wire_freed_ns[a->wire_slot] = g->now_ns;
        a->wire_lifetime = (qa_q2_wire_lifetime){0};
    }
    g->wire_actors[slot] = id; g->wire_freed_ns[slot] = 0;
    a->wire_slot = slot; a->wire_bound = true;
    if (g->wire_extent <= slot) g->wire_extent = slot + 1;
    return true;
}

bool q2_wire_bind(qa_q2_game *g, q2_actor *a, uint32_t slot, qa_error *error)
{
    return bind(g, a, a->id, slot, error);
}

static bool spawn_slot(const qa_q2_game *g,uint32_t *out,qa_error *error)
{
    uint32_t slot=g->wire_extent;
    for(uint32_t i=g->wire_clients+1;i<g->wire_extent;++i) {
        uint64_t freed=g->wire_freed_ns[i];
        if(!g->wire_actors[i].registry&&(freed<2*Q2_NS||
            (g->now_ns>freed&&g->now_ns-freed>Q2_NS/2))) {
            slot=i;break;
        }
    }
    if(slot>=g->wire_capacity) {
        qa_error_set(error,QA_ERROR_MEMORY,slot,"Q2 physical Source edict table is full");
        return false;
    }
    *out=slot;return true;
}

bool qa_q2_wire_spawn_slot(const qa_q2_game *g,uint32_t *out,qa_error *error)
{
    if(!g||!out||g->restoring_continuation||g->continuation_pending||g->continuation_failed||
        g->release_failed||!g->wire_actors||!g->wire_freed_ns||
        g->wire_extent<g->wire_clients+1||g->wire_extent>g->wire_capacity) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 Source spawn requires its actual physical edict namespace");
        return false;
    }
    return spawn_slot(g,out,error);
}

bool q2_wire_admit(qa_q2_game *g, q2_actor *a, qa_actor_id id, qa_error *error)
{
    if (g->restoring_continuation || a->wire_bound) return true;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    if (!record) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 physical source actor is retired");
        return false;
    }
    if (record->has_source && record->owner == g->options.owner)
        return bind(g, a, id, record->source_slot, error);
    uint32_t slot;
    if(!spawn_slot(g,&slot,error))return false;
    if (record->owner == g->options.owner) {
        if (!qa_actors_bind_source(qa_session_actor_registry(g->services.session), id, slot, error)) return false;
        if (g->entity_runtime)
            qa_targets_changed(g->entity_runtime->services.targets, id);
    }
    return bind(g, a, id, slot, error);
}

void q2_wire_release(qa_q2_game *g, q2_actor *a)
{
    for (uint32_t i = 0; i < g->wire_shadow_count; ++i)
        if (qa_actor_id_equal(g->wire_shadows[i].actor, a->id))
            g->wire_shadows[i] = (qa_q2_wire_shadow_light){0};
    if (a->wire_bound && a->wire_slot < g->wire_extent &&
        qa_actor_id_equal(g->wire_actors[a->wire_slot], a->id)) {
        g->wire_actors[a->wire_slot] = (qa_actor_id){0};
        g->wire_freed_ns[a->wire_slot] = g->now_ns;
    }
    a->wire_bound = false;
}

bool qa_q2_wire_extent(const qa_q2_game *g, uint32_t *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    *out = g->wire_extent;
    return true;
}

bool qa_q2_wire_policy(const qa_q2_game *g, uint32_t *capacity,
    uint32_t *clients, qa_error *error)
{
    if (!capacity || !clients || !idle(g, error)) return false;
    if (g->wire_clients != g->player_runtime->rules.max_clients) {
        qa_error_set(error, QA_ERROR_FORMAT, 0,
            "Q2 physical source reservation differs from its active maxclients");
        return false;
    }
    *capacity = g->wire_capacity; *clients = g->wire_clients;
    return true;
}

bool qa_q2_wire_admit_actor(qa_q2_game *g, qa_actor_id id, qa_error *error)
{
    if (!g || g->restoring_continuation || g->continuation_pending || g->continuation_failed ||
        g->release_failed || !qa_actors_get(qa_session_actors(g->services.session), id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Engine admission requires a live physical World generation");
        return false;
    }
    return q2_actor_get(g, id, true, error) != NULL;
}

bool qa_q2_wire_entity_number(qa_q2_game *g, qa_actor_id id, uint32_t *out, qa_error *error)
{
    if (!g || !out || !id.registry || g->restoring_continuation || g->continuation_pending ||
        g->continuation_failed || g->release_failed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 entity reference requires its actual Engine namespace"); return false;
    }
    for (size_t i = 0; i < g->wire_reference_count; ++i)
        if (qa_actor_id_equal(g->wire_references[i].actor, id)) {
            *out = g->wire_references[i].number; return true;
        }
    if (!qa_q2_wire_admit_actor(g, id, error)) return false;
    q2_actor *a = g->actors[id.slot];
    if (!a || !a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], id)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 emitted actor lost its genuine Engine admission"); return false;
    }
    *out = a->wire_slot; return true;
}

bool qa_q2_wire_linked(qa_q2_game *g, const qa_linked_body *linked, qa_error *error)
{
    if (!linked || !qa_vec_finite(linked->state.origin) ||
        !qa_q2_wire_admit_actor(g, linked->actor, error)) return false;
    q2_actor *a = g->actors[linked->actor.slot];
    if (!a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], linked->actor) || a->wire_lifetime.link_count == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source link lost its actual physical Engine membership");
        return false;
    }
    if (!a->wire_lifetime.present)
        a->wire_lifetime = (qa_q2_wire_lifetime){.creation_origin = linked->state.origin,
            .creation_frame = g->wire_frame, .present = true};
    ++a->wire_lifetime.link_count;
    a->wire_lifetime.origins[g->wire_frame & 7u] = (qa_q2_wire_origin){
        .source_frame = g->wire_frame, .origin = linked->state.origin, .present = true};
    return true;
}

bool qa_q2_wire_lightstyle_revision(const qa_q2_game *g, uint64_t *out, qa_error *error) {
    if (!out || !idle(g, error)) return false;
    *out = g->wire_lightstyle_revision;
    return true;
}
bool qa_q2_wire_lightstyle_read(const qa_q2_game *g, uint32_t style,
    qa_string_id *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    uint32_t count = 256;
    if (style >= count) { qa_error_set(error, QA_ERROR_ARGUMENT, style, "Q2 lightstyle leaves its actual Source table"); return false; }
    *out = g->wire_lightstyles[style];
    return true;
}

bool qa_q2_wire_map_read(const qa_q2_game *g, qa_q2_wire_map *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    const q2_entities *state = g->entity_runtime;
    *out = (qa_q2_wire_map){.music = g->wire_music, .music_present = g->wire_music_present,
        .sky = state->sky, .sky_axis = state->sky_axis, .sky_rotation = state->sky_rotation,
        .sky_auto = state->sky_auto};
    return true;
}

bool qa_q2_wire_shadow_read(const qa_q2_game *g, uint32_t index,
    qa_q2_wire_shadow_light *out, qa_error *error)
{
    if (!out || index >= 256 || !idle(g, error)) return false;
    qa_q2_wire_shadow_light value = index < g->wire_shadow_count ?
        g->wire_shadows[index] : (qa_q2_wire_shadow_light){0};
    if (value.present) {
        qa_q2_wire_binding binding;
        if (!qa_q2_wire_actor(g, value.actor, &binding, error) || binding.source_slot != value.source_slot)
            return false;
    }
    *out = value;
    return true;
}

bool q2_wire_shadow_event(qa_q2_game *g, const qa_q2_map_event *event, qa_error *error)
{
    q2_actor *a = event->actor.slot < g->capacity ? g->actors[event->actor.slot] : NULL;
    if (g->options.edition != QA_Q2_RERELEASE || !a || !qa_actor_id_equal(a->id, event->actor) ||
        !a->entity || a->entity->kind != Q2E_DYNAMIC_LIGHT || a->entity->stage != 1) return true;
    if (event->radius <= 0) return true;
    float cone = q2_field_float(g, a->entity, "shadowlightconeangle", 45);
    if (!a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], a->id) ||
        !qa_vec_finite(event->direction) || !isfinite(event->radius) || !isfinite(event->intensity) ||
        !isfinite(event->fade_start) || !isfinite(event->fade_end) || !isfinite(cone) ||
        event->style < -1 || event->style >= 256) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 shadow light lost its actual physical Source fields");
        return false;
    }
    uint32_t index = 0;
    while (index < g->wire_shadow_count && !qa_actor_id_equal(g->wire_shadows[index].actor, a->id)) ++index;
    if (index >= 256) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source shadow-light table is full"); return false; }
    if (index == g->wire_shadow_count) ++g->wire_shadow_count;
    g->wire_shadows[index] = (qa_q2_wire_shadow_light){.actor = a->id, .source_slot = a->wire_slot,
        .type = (event->flags & 1) ? 1u : 0u, .resolution = event->resolution, .radius = event->radius,
        .intensity = event->intensity, .fade_start = event->fade_start, .fade_end = event->fade_end,
        .style = event->style, .direction = event->direction,
        .cone_angle = cone, .present = true};
    return true;
}

static bool current(const qa_q2_game *g, uint32_t slot,
    qa_q2_wire_binding *out, qa_error *error)
{
    if (slot >= g->wire_extent) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q2 edict leaves its physical source extent");
        return false;
    }
    qa_actor_id id = g->wire_actors[slot];
    qa_q2_wire_binding value = {.source_owner = g->options.owner, .source_slot = slot};
    if (id.registry) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
        q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
        if (!record || !a || !qa_actor_id_equal(a->id, id) ||
            !a->wire_bound || a->wire_slot != slot ||
            (record->owner == g->options.owner && record->has_source && record->source_slot != slot)) {
            qa_error_set(error, QA_ERROR_FORMAT, slot, "Q2 physical source binding lost its full actor generation");
            return false;
        }
        value.actor = id; value.in_use = true;
    }
    *out = value;
    return true;
}

bool qa_q2_wire_binding_read(const qa_q2_game *g, uint32_t slot,
    qa_q2_wire_binding *out, qa_error *error)
{
    return out && idle(g, error) && current(g, slot, out, error);
}

bool qa_q2_wire_actor(const qa_q2_game *g, qa_actor_id id,
    qa_q2_wire_binding *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    const q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    if (!a || !qa_actor_id_equal(a->id, id) || !a->wire_bound) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Actor has no admitted Q2 physical source edict");
        return false;
    }
    return current(g, a->wire_slot, out, error);
}

bool qa_q2_wire_view_read(const qa_q2_game *g, qa_actor_id id,
    qa_q2_wire_view *out, qa_error *error)
{
    qa_q2_wire_binding binding;
    if (!g || !out || g->restoring_continuation || g->continuation_pending ||
        g->continuation_failed || g->release_failed) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 VIEW requires its live GAME owner");
        return false;
    }
    if (g->current_actor.registry || !qa_session_safe(g->services.session) ||
        !qa_world_idle(g->services.world)) {
        qa_clock_state clock;
        if ((g->current_actor.registry &&
             !qa_actors_get(qa_session_actors(g->services.session),g->current_actor)) ||
            !qa_session_clock(g->services.session,g->options.owner,&clock) ||
            clock.frame.provider!=g->options.owner || clock.frame.time_ns!=g->now_ns ||
            clock.frame.kind!=(g->options.edition==QA_Q2_RERELEASE?
                QA_RULESET_Q2_RERELEASE:QA_RULESET_Q2_CLASSIC)) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 VIEW lost its executing Source clock");
            return false;
        }
    }
    const q2_actor *a=id.slot<g->capacity?g->actors[id.slot]:NULL;
    if (!a || !qa_actor_id_equal(a->id,id) || !a->wire_bound ||
        !current(g,a->wire_slot,&binding,error)) {
        if (!error || error->code==QA_OK)
            qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Actor has no admitted Q2 physical source edict");
        return false;
    }
    if (!a->client || !a->client->info.connected || binding.source_slot != a->client->info.slot + 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 VIEW has no physical admitted source player");
        return false;
    }
    if (a->wire_view.present && (a->wire_view.frame > g->wire_frame || a->wire_view.time_ns > g->now_ns)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 retained VIEW exceeds its actual source clock");
        return false;
    }
    *out = a->wire_view;
    if (!out->present) out->view.fov = a->client->rule.fov;
    return true;
}

static int16_t source_short(float value)
{
    uint16_t bits = (uint16_t)(uint32_t)qa_source_float_to_i32(value);
    int16_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static qa_vec3 source_command_angles(const qa_usercmd *command)
{
    return command->kind == QA_RULESET_Q2_CLASSIC ?
        qa_v3((float)command->angle_words[0] * (360.f / 65536.f),
            (float)command->angle_words[1] * (360.f / 65536.f),
            (float)command->angle_words[2] * (360.f / 65536.f)) : command->angles;
}

static void source_policy_read(const qa_q2_player_pm_rules *rules, qa_movement_state *state)
{
    if (state->kind == QA_RULESET_Q2_RERELEASE) {
        state->data.q2r.type = rules->type;
        state->data.q2r.flags = rules->flags;
        state->data.q2r.time_ms = rules->time;
        state->data.q2r.gravity = rules->gravity;
        state->data.q2r.delta_angles = rules->delta.angles;
    } else {
        state->data.q2.type = rules->type;
        state->data.q2.flags = rules->flags;
        state->data.q2.time_eight_ms = (uint8_t)rules->time;
        state->data.q2.gravity = rules->gravity;
        memcpy(state->data.q2.delta_angle_shorts, rules->delta.words, sizeof(rules->delta.words));
    }
}

static void source_policy_write(qa_q2_player_pm_rules *rules, const qa_movement_state *state)
{
    if (state->kind == QA_RULESET_Q2_RERELEASE) {
        rules->type = state->data.q2r.type;
        rules->flags = state->data.q2r.flags;
        rules->time = state->data.q2r.time_ms;
        rules->gravity = state->data.q2r.gravity;
        rules->delta.angles = state->data.q2r.delta_angles;
    } else {
        rules->type = state->data.q2.type;
        rules->flags = state->data.q2.flags;
        rules->time = state->data.q2.time_eight_ms;
        rules->gravity = state->data.q2.gravity;
        memcpy(rules->delta.words, state->data.q2.delta_angle_shorts, sizeof(rules->delta.words));
    }
}

bool qa_q2_player_movement_read(const qa_q2_game *game, qa_actor_id id,
    qa_movement_result *out, qa_vec3 *command_angles, qa_error *error)
{
    qa_q2_game *g = (qa_q2_game *)game;
    q2_actor *a = q2_actor_get(g, id, false, error);
    qa_q2_player_movement current;
    qa_body_state body;
    if (!a || !q2_player_observe(g, a, &current, error) ||
        !qa_world_body_read(g->services.world, id, &body, error)) return false;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_movement_state state = {.kind = rr ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC};
    if (current.source_movement) state = *current.state;
    else {
        source_policy_read(&a->source_pm, &state);
        if (rr) {
            state.data.q2r.origin = body.origin;
            state.data.q2r.velocity = body.velocity;
            state.data.q2r.view_height = a->client->info.view_height;
        } else {
            const float positions[] = {body.origin.x, body.origin.y, body.origin.z};
            const float speeds[] = {body.velocity.x, body.velocity.y, body.velocity.z};
            for (unsigned i = 0; i < 3; ++i) {
                state.data.q2.origin_eighths[i] = source_short(positions[i] * 8.f);
                state.data.q2.velocity_eighths[i] = source_short(speeds[i] * 8.f);
            }
        }
    }
    *out = (qa_movement_result){.status = QA_MOVEMENT_ACTIVE, .actor = id, .state = state,
        .view_angles = current.view_angles, .view_offset = current.view_offset,
        .view_height = rr ? state.data.q2r.view_height : current.view_height, .bounds = body.bounds,
        .water_level = current.water_level, .water_type = (int32_t)current.water_type,
        .ground = *current.ground};
    if (command_angles) *command_angles = a->source_pm.command_angles;
    return true;
}

bool qa_q2_player_movement_prepare(qa_q2_game *g, qa_actor_id id,
    const qa_usercmd *command, qa_movement_result *out,
    bool *run_pmove, qa_error *error)
{
    if (!qa_q2_player_movement_read(g, id, out, NULL, error)) return false;
    q2_actor *a = g->actors[id.slot];
    q2_client_state *client = a->client;
    qa_q2_player_movement current;
    if (!q2_player_observe(g, a, &current, error)) return false;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_movement_state *state = &out->state;
    a->source_pm.frame = g->wire_frame; a->source_pm.time_ns = g->now_ns;
    a->source_pm.command_angles = source_command_angles(command);
    *run_pmove = !g->player_runtime->intermission && !(rr && client->rule.awaiting_respawn) &&
        !client->info.chase_target.registry;
    int32_t type;
    if (g->player_runtime->intermission || (rr && client->rule.awaiting_respawn)) {
        type = rr ? 6 : 4;
        if (rr) {
            out->view_height = g->player_runtime->intermission &&
                g->options.product == QA_Q2_N64 && !g->options.deathmatch ? 0 : 22;
            state->data.q2r.view_height = out->view_height;
            client->info.view_height = out->view_height;
        }
    } else if (client->info.chase_target.registry) {
        type = rr ? state->data.q2r.type : state->data.q2.type;
    } else {
        type = client->info.noclip ? (rr ? (client->info.spectator ? 3 : 2) : 1) :
            client->rule.gibbed ? (rr ? 5 : 3) : client->info.dead ? (rr ? 4 : 2) :
            rr && a->grapples[QA_Q2_CTF_GRAPPLE].hook.registry &&
                a->grapples[QA_Q2_CTF_GRAPPLE].phase >= QA_Q2_GRAPPLE_PULL ? 1 : 0;
        float gravity = g->services.physics->gravity;
        if (!rr && !qa_q2_source_value(g, QA_Q2_SOURCE_GRAVITY, 800, &gravity, error)) return false;
        if (rr) gravity *= a->physics_bound ? a->physics.gravity_scale : 1;
        if (rr) {
            state->data.q2r.gravity = source_short(gravity);
            bool collide = !g->options.cooperative ||
                (g->player_runtime->rules.coop_player_collision && client->rule.player_collision);
            state->data.q2r.flags = collide ? state->data.q2r.flags & ~UINT32_C(512) :
                state->data.q2r.flags | UINT32_C(512);
        } else state->data.q2.gravity = source_short(gravity);
    }
    if (rr) state->data.q2r.type = type;
    else state->data.q2.type = type;
    if (current.source_movement) *current.state = *state;
    else source_policy_write(&a->source_pm, state);
    a->source_pm.command_pending = *run_pmove;
    a->source_pm.pending_sequence = *run_pmove ? command->sequence : 0;
    if (!*run_pmove) {
        a->source_pm.source_sequence = command->sequence;
        a->source_pm.command_seen = true;
    }
    return true;
}

bool qa_q2_player_movement_complete(qa_q2_game *g, qa_actor_id id,
    const qa_movement_result *result, const qa_usercmd *command,
    bool source_movement, bool was_grounded, qa_error *error)
{
    q2_actor *a = q2_actor_get(g, id, false, error);
    if (!a) return false;
    a->source_pm.command_pending = false; a->source_pm.pending_sequence = 0;
    a->source_pm.source_sequence = command->sequence; a->source_pm.command_seen = true;
    if (!result) return true;
    a->client->info.view_height = result->view_height;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    bool jumped = rr ? result->jump_sound &&
        !(source_movement && (result->state.data.q2r.flags & 128u)) :
        was_grounded && result->ground.hit == QA_TRACE_HIT_NONE && command->up_move >= 10 &&
        result->water_level == 0;
    return !jumped || q2_player_jump(g, id, qa_movement_origin(&result->state), error);
}

bool q2_player_source_motion_rules(qa_q2_game *g, q2_actor *a,
    const qa_q2_player_motion *change, qa_error *error)
{
    qa_q2_player_movement current;
    if (!q2_player_observe(g, a, &current, error)) return false;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (change->kind == QA_Q2_PLAYER_SPAWN && !change->preserve_view_angles) {
        uint64_t sequence = a->source_pm.source_sequence, pending = a->source_pm.pending_sequence;
        bool seen = a->source_pm.command_seen, command_pending = a->source_pm.command_pending;
        a->source_pm = (qa_q2_player_pm_rules){.source_sequence = sequence, .pending_sequence = pending,
            .command_seen = seen, .command_pending = command_pending};
        a->wire_view = (qa_q2_wire_view){0}; a->wire_event = 0; a->wire_event_frame = g->wire_frame;
    }
    a->source_pm.frame = g->wire_frame; a->source_pm.time_ns = g->now_ns;
    bool force_view = change->kind != QA_Q2_PLAYER_NOCLIP && !change->preserve_view_angles;
    if (force_view) a->source_pm.command_angles = change->command_angles;
    qa_q2_player_pm_rules policy = {0};
    if (current.source_movement &&
        !(change->kind == QA_Q2_PLAYER_SPAWN && !change->preserve_view_angles))
        source_policy_write(&policy, current.state);
    else if (!current.source_movement) policy = a->source_pm;
    qa_q2_player_pm_rules *rules = &policy;
    if (force_view) {
        qa_vec3 view = change->has_command_view_angles ? change->command_view_angles : change->angles;
        qa_vec3 delta = qa_vec_sub(view, change->command_angles);
        if (rr) rules->delta.angles = delta;
        else {
            const float angles[] = {delta.x, delta.y, delta.z};
            for (unsigned i = 0; i < 3; ++i) {
                uint16_t bits = qa_angle_to_word(angles[i]);
                memcpy(&rules->delta.words[i], &bits, sizeof(bits));
            }
        }
    }
    rules->type = change->kind == QA_Q2_PLAYER_FREEZE ? (rr ? 6 : 4) :
        change->kind == QA_Q2_PLAYER_NOCLIP ? (change->enabled ? (rr ? 2 : 1) : 0) :
        change->spectator ? (rr ? 3 : 1) : 0;
    if (change->hold_ns) {
        rules->flags |= 32;
        rules->time = (uint32_t)fmin((double)(change->hold_ns / (rr ? Q2_MS : 8 * Q2_MS)), rr ? UINT16_MAX : UINT8_MAX);
    }
    if (current.source_movement) {
        source_policy_read(rules, current.state);
        if (rr) current.state->data.q2r.view_height = a->client->info.view_height;
    } else {
        a->source_pm.type = policy.type; a->source_pm.flags = policy.flags;
        a->source_pm.time = policy.time; a->source_pm.gravity = policy.gravity;
        a->source_pm.delta = policy.delta;
    }
    return true;
}

bool qa_q2_player_movement_restore(qa_q2_game *g, qa_actor_id id, const qa_body_state *body,
    const qa_movement_result *state, const qa_vec3 *command_angles, qa_error *error)
{
    q2_actor *a = q2_actor_get(g, id, false, error);
    qa_q2_player_movement current;
    if (!a || !q2_player_observe(g, a, &current, error)) return false;
    if (!current.source_movement) source_policy_write(&a->source_pm, &state->state);
    if (command_angles) a->source_pm.command_angles = *command_angles;
    qa_q2_player_motion change = {.kind = QA_Q2_PLAYER_SPAWN,
        .origin = body->origin, .velocity = body->velocity,
        .angles = state->view_angles, .command_angles = a->source_pm.command_angles,
        .preserve_view_angles = true, .spectator = a->client->info.spectator, .restore = state};
    return g->player_runtime->services.set_movement(g->player_runtime->services.context, id, &change, error);
}

bool qa_q2_wire_next(const qa_q2_game *g, uint64_t *order,
    qa_q2_wire_binding *out, bool *found, qa_error *error)
{
    if (!order || !out || !found || !idle(g, error)) return false;
    *found = false;
    for (const q2_actor *a = g->first_actor; a; a = a->live_next) {
        if (a->source_order <= *order || !a->wire_bound) continue;
        if (!current(g, a->wire_slot, out, error)) return false;
        *order = a->source_order; *found = true;
        return true;
    }
    return true;
}

bool qa_q2_wire_entity_read(qa_q2_game *g, uint32_t slot,
    qa_q2_wire_source_entity *out, qa_error *error)
{
    qa_q2_wire_source_entity value = {0};
    if (!out || !qa_q2_wire_binding_read(g, slot, &value.binding, error)) return false;
    if (!value.binding.in_use) { *out = value; return true; }
    q2_actor *a = g->actors[value.binding.actor.slot];
    value.has_visual = qa_q2_presentation_read(g, a->id, &value.visual);
    value.body_serial = qa_world_body_storage_serial(g->services.world, a->id);
    if (!value.body_serial || !qa_world_body_read(g->services.world, a->id, &value.body, error)) return false;
    value.previous_origin = a->entity && (value.visual.render_flags & 128u)
        ? a->entity->beam_end : a->projectile.kind == Q2_BFG_LASER ||
            a->projectile.kind == Q2_RERELEASE_SPAWN_BEAM
        ? a->projectile.movedir : value.body.origin;
    value.model_beam = qa_q2_model_beam(g->options.edition, value.visual.render_flags,
        value.visual.models[0] != QA_STRING_NONE);
    value.solid = a->physics_bound ? a->physics.solid : QA_PHYSICS_NOT_SOLID;
    qa_actor_collision collision;
    qa_error collision_error = {0};
    bool has_collision = qa_world_get_collision(g->services.world, a->id, &collision, &collision_error);
    if (!has_collision && collision_error.code != QA_OK) {
        if (error) *error = collision_error;
        return false;
    }
    if (has_collision) {
        value.owner = qa_actor_reference_resolve(qa_session_actors(g->services.session), collision.owner);
        if (collision.monster) value.server_flags |= 4;
        if (collision.dead_monster) value.server_flags |= 2;
        if (!a->physics_bound)
            value.solid = collision.role == QA_COLLISION_TRIGGER ? QA_PHYSICS_TRIGGER :
                collision.inline_model ? QA_PHYSICS_BRUSH : QA_PHYSICS_BOX;
    }
    value.has_weapon = a->weapon_bound;
    if (value.has_weapon) value.weapon = a->weapon;
    if (a->projectile.kind != Q2_PROJECTILE_NONE) {
        value.classname = a->projectile.classname; value.loop_sound = a->projectile.loop_sound;
        value.owner = qa_actor_reference_resolve(qa_session_actors(g->services.session), a->projectile.owner);
    } else if (a->client) {
        value.classname = a->entity ? a->entity->classname : 0;
        value.loop_sound = a->client->rule.loop_sound;
    } else if (a->monster) {
        value.classname = a->monster->controller_kind != Q2M_CONTROLLER_NONE && a->entity
            ? a->entity->classname : a->monster->classname;
        if (a->monster->controller_kind != Q2M_CONTROLLER_NONE && a->entity)
            value.owner = a->entity->owner;
        value.loop_sound = a->monster->weapon_sound;
    } else if (a->item) {
        value.classname = a->item->definition ? a->item->definition->classname_id : 0;
        value.owner = qa_actor_reference_resolve(qa_session_actors(g->services.session), a->item->owner);
        if (a->item->companion) value.loop_sound = a->item->companion->loop_sound;
    } else if (a->entity) {
        value.classname = a->entity->classname; value.owner = a->entity->owner;
        value.loop_sound = a->entity->loop_sound;
        value.spawn_flags = a->entity->spawnflags;
        value.volume = a->entity->volume; value.attenuation = a->entity->attenuation;
        if (a->entity->kind == Q2E_SPEAKER && (a->entity->spawnflags & 3) && a->entity->active) {
            value.loop_sound = a->entity->noise;
            value.volume = 1;
            if (g->options.edition == QA_Q2_RERELEASE && a->entity->attenuation == -1)
                value.server_flags |= 1024;
        }
        value.solid = a->entity->collision.inline_model ? QA_PHYSICS_BRUSH : value.solid;
        if (a->entity->kind == Q2E_FLARE) {
            value.flare = true; value.flare_image = q2_field_id(g, a->entity, "image");
            value.flare_start = q2_field_float(g, a->entity, "fade_start_dist", 96);
            value.flare_end = q2_field_float(g, a->entity, "fade_end_dist", 384);
        }
    }
    if (a->entity && (a->entity->kind == Q2E_ROTATING || a->entity->kind == Q2E_SPEAKER ||
        a->entity->kind == Q2E_SOUND_FX || a->entity->kind == Q2E_EARTHQUAKE ||
        (a->entity->kind == Q2E_SCENERY && a->entity->scenery == Q2S_EXPLOSIVE)))
        value.precache_sound = a->entity->noise;
    value.lifetime = a->wire_lifetime;
    value.event = a->wire_event_frame == g->wire_frame ? a->wire_event : 0;
    qa_q2_wire_binding after;
    if (!qa_q2_wire_actor(g, a->id, &after, error) ||
        value.body_serial != qa_world_body_storage_serial(g->services.world, a->id) ||
        !qa_actor_id_equal(after.actor, value.binding.actor) || after.source_slot != slot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q2 wire body/source binding changed during observation");
        return false;
    }
    *out = value;
    return true;
}
