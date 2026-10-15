#include "internal.h"
#include "entities/internal.h"
#include "player/internal.h"
#include "monsters/internal.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"
#include "qa/input.h"
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
static const char *statusbar_text(unsigned kind)
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
    switch (kind) {
    case 0: return classic_single;
    case 1: return classic_deathmatch;
    case 2: return rerelease_single;
    case 3: return rerelease_coop;
    default: return rerelease_deathmatch;
    }
}
bool q2_wire_names_prepare(qa_q2_game *g, qa_error *error)
{
    for (unsigned i = 0; i < 5; ++i)
        if (!qa_strings_intern_cstr(qa_session_strings(g->services.session), statusbar_text(i),
            &g->wire_statusbar_names[i], error)) return false;
    return true;
}
qa_string_id qa_q2_wire_statusbar_name(const qa_q2_game *g)
{
    unsigned kind=g->options.rules==QA_RULESET_Q2_CLASSIC?(g->options.deathmatch?1:0):
        g->options.deathmatch?4:g->options.cooperative?3:2;
    return g->wire_statusbar_names[kind];
}
const char *qa_q2_wire_statusbar(const qa_q2_game *g)
{
    return qa_strings_cstr(qa_session_strings(g->services.session),qa_q2_wire_statusbar_name(g));
}
#undef Q2_STATUS_VITALS
#undef Q2_STATUS_TIMER
#undef Q2_STATUS_SCORE
#undef Q2_CLASSIC_PICKUP
#undef Q2_RERELEASE_PICKUP

void qa_q2_wire_stat_word(int32_t value, int16_t *out)
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
    qa_q2_wire_stat_word(value >= INT32_MIN && value < 2147483648.0 ? (int32_t)value : INT32_MIN, out);
    return true;
}
static bool stat_image(const qa_q2_wire_stat_resources *resources, qa_string_id name,
    int16_t *out, qa_error *error)
{
    uint32_t image = 0;
    if (name && !resources->image(resources->context, name, &image, error)) return false;
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
    int16_t stats[64] = {0};
    if (!stat_number(view->health, &stats[1], error) ||
        !stat_number(view->armor, &stats[5], error) ||
        !stat_image(resources, g->runtime_names[Q2_NAME_RESOURCE_I_HEALTH], &stats[0], error) ||
        !stat_image(resources, view->ammo_icon, &stats[2], error) ||
        !stat_image(resources, view->armor_icon, &stats[4], error) ||
        !stat_image(resources, view->pickup_icon, &stats[7], error) ||
        !stat_image(resources, view->help_icon, &stats[11], error)) return false;
    qa_q2_wire_stat_word(view->ammo_count, &stats[3]); qa_q2_wire_stat_word(view->timer_seconds, &stats[10]);
    qa_q2_wire_stat_word(view->layouts, &stats[13]); qa_q2_wire_stat_word(view->score, &stats[14]);
    qa_q2_wire_stat_word(view->flashes, &stats[15]); stats[17] = view->spectator ? 1 : 0;

    for (size_t i = 0; i < qa_q2_item_count(g); ++i) {
        const qa_q2_item_definition *item = qa_q2_item_at(g, i);
        if (view->selected_item && item->item == view->selected_item) {
            qa_q2_wire_stat_word((int32_t)i + 1, &stats[12]);
            if (!stat_image(resources, item->icon_id, &stats[6], error)) return false;
        }
        if (view->timer_item && item->item == view->timer_item &&
            !stat_image(resources, item->icon_id, &stats[9], error)) return false;
        if (view->pickup_text && view->pickup_text==item->name_id)
            qa_q2_wire_stat_word((int32_t)resources->items_base + (int32_t)i + 1, &stats[8]);
        if (view->selected_item_name && view->selected_item_name==item->name_id)
            qa_q2_wire_stat_word((int32_t)resources->items_base + (int32_t)i + 1, &stats[51]);
    }
    if (g->options.rules == QA_RULESET_Q2_RERELEASE) {
        qa_q2_wire_stat_word(view->hit_marker_damage, &stats[50]);
        for (unsigned i = 0; i < 3; ++i)
            if (!stat_image(resources, view->key_icons[i], &stats[44 + i], error)) return false;
    } else stats[51] = 0;
    if (resources->modes && !qa_modes_q2_hud_stats(resources->modes, resources->mode,
        resources->actor, resources->source_frame, resources, stats, error)) return false;
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

bool q2_wire_binding_number(const qa_q2_game *g, qa_actor_id id, uint32_t *out)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    if (!record) return false;
    size_t number;
    if (record->owner == g->options.owner && record->has_source)
        number = record->source_slot;
    else if (!qa_actor_index_find(&g->wire_index, id, &number)) return false;
    if (number >= g->wire_extent) return false;
    *out = (uint32_t)number;
    return true;
}

qa_actor_id q2_wire_actor_at(const qa_q2_game *g, uint32_t slot)
{
    qa_actor_id bound = qa_actor_index_at_source(&g->wire_index, slot);
    if (bound.registry) return bound;
    const qa_actor_record *record = qa_actors_at_source(qa_session_actors(g->services.session),
        g->options.owner, slot);
    return record ? record->id : (qa_actor_id){0};
}

bool qa_q2_source_client_actor(const qa_q2_game *g, uint32_t client, qa_actor_id *out)
{
    if (!g || !out || client >= g->wire_clients) return false;
    qa_actor_id id = q2_wire_actor_at(g, client + 1);
    const q2_actor *actor = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    if (!q2_actor_live(g, id) || !actor || !qa_actor_id_equal(actor->id, id) || !actor->client)
        return false;
    *out = id;
    return true;
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
    qa_actor_index index = {0}, shadows = {0};
    uint64_t *freed = calloc(capacity, sizeof(*freed));
    if (!freed) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 physical source edicts");
        return false;
    }
    if (!qa_actor_index_create(&index, qa_session_actors(g->services.session), NULL, error) ||
        !qa_actor_index_reverse_reserve(&index, capacity, error) ||
        !qa_actor_index_create(&shadows, qa_session_actors(g->services.session), NULL, error)) {
        qa_actor_index_destroy(&index); qa_actor_index_destroy(&shadows); free(freed); return false;
    }
    qa_actor_index_destroy(&g->wire_index); free(g->wire_freed_ns);
    qa_actor_index_destroy(&g->wire_shadow_index);
    g->wire_index = index; g->wire_freed_ns = freed;
    g->wire_shadow_index = shadows;
    g->wire_capacity = capacity; g->wire_clients = clients; g->wire_extent = clients + 1;
    g->wire_frame = 0;
    return true;
}

void q2_wire_reset(qa_q2_game *g)
{
    qa_actor_index_clear(&g->wire_index);
    qa_actor_index_clear(&g->wire_shadow_index);
    memset(g->wire_freed_ns, 0, g->wire_capacity * sizeof(*g->wire_freed_ns));
    g->wire_extent = g->wire_clients + 1; g->wire_frame = 0;
    ++g->wire_lightstyles.revision;
    memset(g->wire_lightstyles.patterns, 0, sizeof(g->wire_lightstyles.patterns));
    memset(g->wire_shadows, 0, sizeof(g->wire_shadows)); g->wire_shadow_count = 0;
}

static bool bind(qa_q2_game *g, q2_actor *a, qa_actor_id id,
    uint32_t slot, qa_error *error)
{
    qa_actor_id occupied = q2_wire_actor_at(g, slot);
    if (slot >= g->wire_capacity ||
        (occupied.registry && !qa_actor_id_equal(occupied, id))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot,
            "Q2 physical source edict is occupied or outside its admitted table");
        return false;
    }
    uint32_t previous;
    size_t module_previous;
    bool bound = qa_actor_index_find(&g->wire_index, id, &module_previous);
    if (bound) previous = (uint32_t)module_previous;
    else bound = q2_wire_binding_number(g, id, &previous);
    if (bound && previous != slot &&
        !qa_actor_id_equal(q2_wire_actor_at(g, previous), id)) {
            qa_error_set(error, QA_ERROR_FORMAT, previous,
                "Q2 actor lost its prior physical source edict");
            return false;
    }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    if (record->owner == g->options.owner && (!record->has_source || record->source_slot != slot) &&
        !qa_actors_bind_source(qa_session_actor_registry(g->services.session), id, slot, error)) return false;
    if (!qa_actor_index_remember(&g->wire_index, id, slot, error)) return false;
    if (bound && previous != slot) {
        qa_actor_index_remove(&g->wire_index, id);
        g->wire_freed_ns[previous] = g->now_ns;
        a->wire_lifetime = (qa_q2_wire_lifetime){0};
    }
    if (record->owner == g->options.owner) qa_actor_index_remove(&g->wire_index, id);
    else qa_actor_index_put(&g->wire_index, id, slot);
    g->wire_freed_ns[slot] = 0;
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
        if(!q2_wire_actor_at(g,i).registry&&(freed<2*Q2_NS||
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
        g->release_failed||!g->wire_freed_ns||
        g->wire_extent<g->wire_clients+1||g->wire_extent>g->wire_capacity) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 Source spawn requires its actual physical edict namespace");
        return false;
    }
    return spawn_slot(g,out,error);
}

bool q2_wire_admit(qa_q2_game *g, q2_actor *a, qa_actor_id id, qa_error *error)
{
    if (g->restoring_continuation) return true;
    uint32_t number;
    size_t retained;
    if (q2_wire_binding_number(g, id, &number) &&
        qa_actor_index_find_retained(&g->wire_index, id, &retained) && retained == number) return true;
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

void q2_wire_release(qa_q2_game *g, q2_actor *a, qa_actor_record released)
{
    size_t shadow;
    if (qa_actor_index_find(&g->wire_shadow_index, a->id, &shadow)) {
        qa_actor_index_remove(&g->wire_shadow_index, a->id);
        g->wire_shadows[shadow] = (q2_shadow_slot){0};
        if (g->entity_runtime) {
            qa_error ignored={0};
            qa_q2_map_event event={.kind=QA_Q2_MAP_DYNAMIC_LIGHT,.actor=a->id,
                .slot=(int)shadow,.flags=QA_Q2_MAP_LIGHT_SHADOW};
            (void)q2_map_event(g,&event,&ignored);
        }
    }
    size_t slot;
    bool bound = released.owner == g->options.owner && released.has_source;
    if (bound) slot = released.source_slot;
    else bound = qa_actor_index_find(&g->wire_index, a->id, &slot);
    if (bound && slot < g->wire_extent) g->wire_freed_ns[slot] = g->now_ns;
    qa_actor_index_remove(&g->wire_index, a->id);
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
    size_t retained;
    if (qa_actor_index_find_retained(&g->wire_index, id, &retained)) {
        *out = (uint32_t)retained; return true;
    }
    if (!qa_q2_wire_admit_actor(g, id, error)) return false;
    q2_actor *a = g->actors[id.slot];
    uint32_t slot;
    if (!a || !q2_wire_binding_number(g, id, &slot) ||
        !qa_actor_id_equal(q2_wire_actor_at(g, slot), id)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 emitted actor lost its genuine Engine admission"); return false;
    }
    *out = slot; return true;
}

bool qa_q2_wire_linked(qa_q2_game *g, const qa_linked_body *linked, qa_error *error)
{
    if (!linked || !qa_vec_finite(linked->state.origin) ||
        !qa_q2_wire_admit_actor(g, linked->actor, error)) return false;
    q2_actor *a = g->actors[linked->actor.slot];
    uint32_t slot;
    if (!q2_wire_binding_number(g, linked->actor, &slot) ||
        !qa_actor_id_equal(q2_wire_actor_at(g, slot), linked->actor) || a->wire_lifetime.link_count == UINT64_MAX) {
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

const qa_lightstyles *qa_q2_wire_lightstyles(const qa_q2_game *g, qa_error *error) {
    return idle(g,error) ? &g->wire_lightstyles : NULL;
}

bool q2_wire_shadow_event(qa_q2_game *g, qa_q2_map_event *event, qa_error *error)
{
    q2_actor *a = event->actor.slot < g->capacity ? g->actors[event->actor.slot] : NULL;
    if (g->options.rules != QA_RULESET_Q2_RERELEASE || !a || !qa_actor_id_equal(a->id, event->actor) ||
        !a->entity || a->entity->kind != Q2E_DYNAMIC_LIGHT || a->entity->stage != 1) return true;
    if (event->radius <= 0) return true;
    float cone = q2_field_float(g, a->entity, g->field_keys[QA_TARGET_KEY_SHADOWLIGHTCONEANGLE], 45);
    uint32_t slot;
    if (!q2_wire_binding_number(g, a->id, &slot) ||
        !qa_actor_id_equal(q2_wire_actor_at(g, slot), a->id) ||
        !qa_vec_finite(event->direction) || !isfinite(event->radius) || !isfinite(event->intensity) ||
        !isfinite(event->fade_start) || !isfinite(event->fade_end) || !isfinite(cone) ||
        event->style < -1 || event->style >= 256) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 shadow light lost its actual physical Source fields");
        return false;
    }
    size_t index;
    if (!qa_actor_index_find(&g->wire_shadow_index, a->id, &index)) index = g->wire_shadow_count;
    if (index >= 256) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source shadow-light table is full"); return false; }
    if (index == g->wire_shadow_count) ++g->wire_shadow_count;
    event->slot=(int)index;event->count=(int)slot;
    event->value=cone;event->flags|=QA_Q2_MAP_LIGHT_SHADOW;
    g->wire_shadows[index] = (q2_shadow_slot){.actor = a->id, .source_slot = slot};
    qa_actor_index_put(&g->wire_shadow_index, a->id, index);
    return true;
}

static bool current(const qa_q2_game *g, uint32_t slot,
    qa_q2_wire_binding *out, qa_error *error)
{
    if (slot >= g->wire_extent) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q2 edict leaves its physical source extent");
        return false;
    }
    qa_actor_id id = q2_wire_actor_at(g, slot);
    qa_q2_wire_binding value = {.source_owner = g->options.owner, .source_slot = slot};
    if (id.registry) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
        q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
        uint32_t number;
        if (!record || !a || !qa_actor_id_equal(a->id, id) ||
            !q2_wire_binding_number(g, id, &number) || number != slot) {
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
    uint32_t slot;
    if (!a || !qa_actor_id_equal(a->id, id) || !q2_wire_binding_number(g, id, &slot)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Actor has no admitted Q2 physical source edict");
        return false;
    }
    return current(g, slot, out, error);
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
            clock.frame.kind!=(g->options.rules)) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 VIEW lost its executing Source clock");
            return false;
        }
    }
    const q2_actor *a=id.slot<g->capacity?g->actors[id.slot]:NULL;
    uint32_t slot;
    if (!a || !qa_actor_id_equal(a->id,id) || !q2_wire_binding_number(g,id,&slot) ||
        !current(g,slot,&binding,error)) {
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
    uint16_t bits = (uint16_t)(uint32_t)qa_source_to_i32(value);
    int16_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static qa_vec3 source_command_angles(const qa_usercmd *command)
{
    if (command->kind != QA_RULESET_Q2_CLASSIC) return command->angles;
    qa_usercmd converted;
    qa_input_command_basis from = {.kind = command->kind, .words = true, .signed_shorts = true},
        to = {.kind = command->kind};
    qa_input_command_convert(command, NULL, &from, &to, (qa_input_axis_rule){0}, &converted);
    return converted.angles;
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
    bool rr = g->options.rules == QA_RULESET_Q2_RERELEASE;
    qa_movement_state state = {.kind = g->options.rules};
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
    bool rr = g->options.rules == QA_RULESET_Q2_RERELEASE;
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
    bool rr = g->options.rules == QA_RULESET_Q2_RERELEASE;
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
    bool rr = g->options.rules == QA_RULESET_Q2_RERELEASE;
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
                uint16_t bits = qa_angle_to_word(angles[i],QA_ANGLE_WORD_NATIVE);
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
        uint32_t slot;
        if (a->source_order <= *order || !q2_wire_binding_number(g, a->id, &slot)) continue;
        if (!current(g, slot, out, error)) return false;
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
    value.model_beam = qa_q2_model_beam(g->options.rules, value.visual.render_flags,
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
            if (g->options.rules == QA_RULESET_Q2_RERELEASE && a->entity->attenuation == -1)
                value.server_flags |= 1024;
        }
        value.solid = a->entity->collision.inline_model ? QA_PHYSICS_BRUSH : value.solid;
        if (a->entity->kind == Q2E_FLARE) {
            value.flare = true; value.flare_image = q2_field_id(a->entity, g->field_keys[QA_TARGET_KEY_IMAGE]);
            value.flare_start = q2_field_float(g, a->entity, g->field_keys[QA_TARGET_KEY_FADE_START_DIST], 96);
            value.flare_end = q2_field_float(g, a->entity, g->field_keys[QA_TARGET_KEY_FADE_END_DIST], 384);
        }
    }
    if (a->entity && a->entity->kind != Q2E_POI)
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
