#include "equipment_source.h"
#include "equipment_held_output.h"
#include "equipment_q3.h"
#include "equipment_gear_output.h"
#include "equipment_gear_world_output.h"
#include "source_companion.h"
#include "qa/ui_preferences.h"
#include "save_private.h"

typedef struct equipment_packet {
    struct equipment_packet *next;
    frontend_equipment_held_output *output;
    frontend_equipment_q3_output *q3_output;
    frontend_equipment_gear_output *gear_output;
    bool committed, companion;
} equipment_packet;

typedef struct equipment_world_packet {
    struct equipment_world_packet *next;
    frontend_equipment_gear_world_output *output;
} equipment_world_packet;

typedef struct equipment_source_actor {
    size_t entity;
    qa_actor_id actor;
    bool view;
} equipment_source_actor;

typedef struct equipment_source_effect {
    size_t ordinal;
    qa_actor_id actor;
    bool view;
} equipment_source_effect;

typedef struct equipment_companion_ref {
    qa_q3_ref_entity ref;
    qa_q3_refdef definition;
    bool view;
} equipment_companion_ref;

typedef struct equipment_companion_polygon {
    qa_q3_scene_polygon polygon;
    qa_scene_vertex *vertices;
    qa_q3_refdef definition;
    bool view;
} equipment_companion_polygon;

typedef struct equipment_companion_light {
    qa_scene_light light;
    qa_q3_refdef definition;
    bool view;
} equipment_companion_light;

struct frontend_equipment_source {
    frontend_equipment_source_options options;
    qa_application_q3_client_context client;
    qa_application_equipment_view selection;
    qa_application_equipment_view hud;
    char *hud_label;
    qa_application_q3_equipment_draw draw;
    frontend_equipment_media *view_media;
    frontend_equipment_q3_presenter *q3_presenter;
    frontend_equipment_q3_output *q3_view;
    frontend_equipment_gear_presenter *gear_presenter;
    frontend_equipment_gear_output *gear_view;
    equipment_packet *active, *packets, *tail;
    equipment_world_packet *world_packets;
    equipment_source_actor *source_actors;
    size_t source_actor_count;
    equipment_source_effect *source_polygons, *source_lights;
    size_t source_polygon_count, source_light_count;
    qa_application_q3_equipment_source_weapon *source_weapons;
    size_t source_weapon_count;
    frontend_source_companion_view companion;
    equipment_companion_ref *companion_refs;
    size_t companion_ref_count;
    equipment_companion_polygon *companion_polygons;
    equipment_companion_light *companion_lights;
    size_t companion_polygon_count, companion_light_count;
    qa_application_q3_equipment_source_weapon *companion_weapons;
    size_t companion_weapon_count;
    qa_scene_light *view_lights, *view_projected_lights;
    qa_model_transform view_transform;
    qa_q3_ref_entity view_entity;
    qa_vec3 view_offset;
    uint32_t first_order, reserved;
    bool borrowed, drawing, view_ready, world_ready, submitting, original_q3_view, companion_selected;
    bool companion_view_requested;
};

static bool original_q3_match(const frontend_equipment_source *owner,
    const qa_application_equipment_view *source, bool *matching, qa_error *error)
{
    *matching = false;
    if (!source->original_qvm || source->equipment_slot ||
        owner->client.source_owner != source->provider) return true;
    return qa_application_equipment_q3_source_draw_match(owner->options.frontend->application,
        source, owner->options.receiver, owner->options.seat, owner->options.assets, matching, error);
}

static bool current_owner(const frontend_equipment_source *owner)
{
    bool matching = false;
    return owner && owner->client.frontend_lifetime == owner->options.lease &&
        owner->client.receiver == owner->options.receiver && owner->client.seat == owner->options.seat &&
        owner->client.session == qa_application_session(owner->options.frontend->application) &&
        owner->options.current(owner->options.lease, &owner->client) &&
        (!owner->draw.selected || qa_application_equipment_current(owner->options.frontend->application,
            &owner->selection)) && (!owner->original_q3_view ||
            (original_q3_match(owner, &owner->selection, &matching, NULL) && matching)) &&
        (!owner->companion_selected || frontend_source_companion_current(owner->options.frontend, &owner->companion));
}

static bool companion_prepare(frontend_equipment_source *owner, bool *present, qa_error *error)
{
    *present = false;
    frontend_source_companion_view captured;
    if (!frontend_source_companion_read(owner->options.frontend, owner->options.physical_seat,
            owner->selection.actor, &captured, present, error)) return false;
    if (!*present) return true;
    const qa_application_q3_client_context *client = &captured.receipt.client.source;
    if (client->source_owner != owner->selection.provider || client->seat != owner->options.seat || !captured.assets)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected weapon capture changed its real GAME and launch seat");
    qa_application_q3_weapon_models models = {0}; bool registered;
    if (!qa_application_equipment_q3_models_read(owner->options.frontend->application, &owner->selection,
            client->receiver, client->seat, &models, &registered, error)) return false;
    for (size_t i = 0; i < captured.packet_count; ++i) {
        frontend_source_companion_packet packet;
        if (!frontend_source_companion_packet_read(owner->options.frontend, &captured, i, &packet, error)) return false;
        if (packet.definition.flags & 1) continue;
        for (size_t j = 0; j < packet.entity_count; ++j) {
            const qa_q3_ref_entity *ref = packet.entities + j;
            bool owned = packet.entity_actors && qa_actor_id_equal(packet.entity_actors[j], owner->selection.actor);
            bool view = owned && packet.entity_views && packet.entity_views[j];
            if (!owned && registered && (ref->flags & 4) && ref->kind == QA_Q3_REF_MODEL && ref->model > 0 &&
                (ref->model == models.gun || ref->model == models.hands ||
                    ref->model == models.barrel || ref->model == models.flash)) owned = view = true;
            if (!owned) continue;
            if (owner->companion_ref_count >= 1021)
                return frontend_fail(error, QA_ERROR_FORMAT, "Selected Source weapon refs exceed the real scene extent");
            equipment_companion_ref *refs = realloc(owner->companion_refs,
                (owner->companion_ref_count + 1) * sizeof(*refs));
            if (!refs) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining completed Source weapon refs");
            owner->companion_refs = refs;
            refs[owner->companion_ref_count++] = (equipment_companion_ref){*ref, packet.definition, view};
        }
        for (size_t j = 0; j < packet.polygon_count; ++j) {
            if (!packet.polygon_actors || !qa_actor_id_equal(packet.polygon_actors[j], owner->selection.actor)) continue;
            const qa_q3_scene_polygon *polygon = packet.polygons + j;
            if (polygon->first > packet.vertex_count || polygon->count > packet.vertex_count - polygon->first ||
                polygon->count > SIZE_MAX / sizeof(*packet.vertices) ||
                owner->companion_polygon_count == SIZE_MAX / sizeof(*owner->companion_polygons))
                return frontend_fail(error, QA_ERROR_FORMAT, "Selected Source polygon leaves its genuine captured vertex span");
            equipment_companion_polygon *polygons = realloc(owner->companion_polygons,
                (owner->companion_polygon_count + 1) * sizeof(*polygons));
            if (!polygons) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining completed Source weapon polygons");
            owner->companion_polygons = polygons;
            equipment_companion_polygon *row = polygons + owner->companion_polygon_count;
            *row = (equipment_companion_polygon){.polygon = *polygon, .definition = packet.definition,
                .view = packet.polygon_views && packet.polygon_views[j]};
            ++owner->companion_polygon_count;
            if (polygon->count) {
                row->vertices = malloc(polygon->count * sizeof(*row->vertices));
                if (!row->vertices) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Source weapon polygon vertices");
                memcpy(row->vertices, packet.vertices + polygon->first, polygon->count * sizeof(*row->vertices));
            }
            row->polygon.first = 0;
        }
        for (size_t j = 0; j < packet.light_count; ++j) {
            if (!packet.light_actors || !qa_actor_id_equal(packet.light_actors[j], owner->selection.actor)) continue;
            if (owner->companion_light_count == SIZE_MAX / sizeof(*owner->companion_lights))
                return frontend_fail(error, QA_ERROR_MEMORY, "Selected Source weapon lights exceed native extent");
            equipment_companion_light *lights = realloc(owner->companion_lights,
                (owner->companion_light_count + 1) * sizeof(*lights));
            if (!lights) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining completed Source weapon lights");
            owner->companion_lights = lights;
            lights[owner->companion_light_count++] = (equipment_companion_light){packet.lights[j],
                packet.definition, packet.light_views && packet.light_views[j]};
        }
        for (size_t j = 0; j < packet.weapon_count; ++j) {
            if (!qa_actor_id_equal(packet.weapons[j].actor, owner->selection.actor)) continue;
            if (owner->companion_weapon_count == SIZE_MAX / sizeof(*owner->companion_weapons))
                return frontend_fail(error, QA_ERROR_MEMORY, "Selected Source weapon completions exceed native extent");
            qa_application_q3_equipment_source_weapon *weapons = realloc(owner->companion_weapons,
                (owner->companion_weapon_count + 1) * sizeof(*weapons));
            if (!weapons) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining completed empty Source weapon recipe");
            owner->companion_weapons = weapons;
            weapons[owner->companion_weapon_count++] = packet.weapons[j];
        }
    }
    owner->companion = captured; owner->companion_selected = true;
    return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Selected weapon capture retired its real Source Draw receipt");
}

static bool current_draw(void *context, const qa_application_q3_equipment_draw *draw)
{
    const frontend_equipment_source *owner = context;
    return draw && current_owner(owner) && qa_actor_id_equal(draw->actor, owner->draw.actor) &&
        draw->selected == owner->draw.selected && draw->view_visible == owner->draw.view_visible &&
        draw->warning == owner->draw.warning;
}

static bool current_preparation(void *context)
{ return current_owner(context); }

static bool companion_held(const frontend_equipment_source *owner, qa_actor_id actor)
{
    if (!owner->companion_selected || !qa_actor_id_equal(actor, owner->selection.actor)) return false;
    for (size_t i = 0; i < owner->companion_weapon_count; ++i)
        if (!owner->companion_weapons[i].view && qa_actor_id_equal(owner->companion_weapons[i].actor, actor)) return true;
    for (size_t i = 0; i < owner->companion_ref_count; ++i)
        if (!owner->companion_refs[i].view) return true;
    for (size_t i = 0; i < owner->companion_polygon_count; ++i)
        if (!owner->companion_polygons[i].view) return true;
    for (size_t i = 0; i < owner->companion_light_count; ++i)
        if (!owner->companion_lights[i].view) return true;
    return false;
}

static bool requests(const frontend_equipment_source *owner, bool *hud, bool *view, qa_error *error)
{
    return owner->options.requests ? owner->options.requests(owner->options.requests_context, hud, view, error) :
        qa_application_q3_equipment_requests(owner->options.frontend->application,
            owner->options.receiver, owner->options.seat, hud, view, error);
}

static void clear_world(frontend_equipment_source *owner)
{
    equipment_world_packet *row = owner->world_packets;
    while (row) {
        equipment_world_packet *next = row->next;
        frontend_equipment_gear_world_destroy(row->output);
        free(row); row = next;
    }
    owner->world_packets = NULL; owner->world_ready = false;
}

void frontend_equipment_source_clear(frontend_equipment_source *owner)
{
    if (!owner) return;
    equipment_packet *row = owner->packets;
    while (row) {
        equipment_packet *next = row->next;
        frontend_equipment_held_output_destroy(row->output);
        frontend_equipment_q3_output_destroy(row->q3_output);
        frontend_equipment_gear_output_destroy(row->gear_output);
        free(row); row = next;
    }
    owner->packets = owner->tail = NULL;
    clear_world(owner);
    frontend_equipment_q3_output_destroy(owner->q3_view); owner->q3_view = NULL;
    frontend_equipment_gear_output_destroy(owner->gear_view); owner->gear_view = NULL;
    owner->reserved = 0; owner->view_ready = false;
    free(owner->source_actors); owner->source_actors = NULL; owner->source_actor_count = 0;
    free(owner->source_polygons); owner->source_polygons = NULL; owner->source_polygon_count = 0;
    free(owner->source_lights); owner->source_lights = NULL; owner->source_light_count = 0;
    free(owner->source_weapons); owner->source_weapons = NULL; owner->source_weapon_count = 0;
    free(owner->view_lights); owner->view_lights = NULL;
    free(owner->view_projected_lights); owner->view_projected_lights = NULL;
}

static bool source_scene_current(const frontend_equipment_source *owner,
    size_t count, qa_error *error)
{
    if (!owner)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon attribution lost its actual Draw lease");
    if (!owner->drawing)
        return !owner->source_actor_count || frontend_fail(error, QA_ERROR_ARGUMENT,
            "Source weapon attribution outlived its actual declared Draw");
    if (!current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon attribution lost its actual Draw lease");
    for (size_t i = 0; i < owner->source_actor_count; ++i)
        if (owner->source_actors[i].entity >= count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Source weapon attribution leaves its reached entity queue");
    return true;
}

bool frontend_equipment_source_scene_actors(const frontend_equipment_source *owner,
    size_t count, qa_actor_id *out, qa_error *error)
{
    if ((count && !out) || count > SIZE_MAX / sizeof(*out))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon actors require their actual output extent");
    if (!source_scene_current(owner, count, error)) return false;
    if (count) memset(out, 0, count * sizeof(*out));
    for (size_t i = 0; i < owner->source_actor_count; ++i)
        out[owner->source_actors[i].entity] = owner->source_actors[i].actor;
    return true;
}

bool frontend_equipment_source_scene_views(const frontend_equipment_source *owner,
    size_t count, bool *out, qa_error *error)
{
    if ((count && !out) || count > SIZE_MAX / sizeof(*out))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon views require their actual output extent");
    if (!source_scene_current(owner, count, error)) return false;
    if (count) memset(out, 0, count * sizeof(*out));
    for (size_t i = 0; i < owner->source_actor_count; ++i)
        out[owner->source_actors[i].entity] = owner->source_actors[i].view;
    return true;
}

static bool source_effects_read(const frontend_equipment_source *owner,
    const equipment_source_effect *rows, size_t row_count, size_t count,
    qa_actor_id *actors, bool *views, qa_error *error)
{
    if (!owner || (count && (!actors || !views)) || count > SIZE_MAX / sizeof(*actors))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects require their actual output extent");
    if ((!owner->drawing && row_count) || (owner->drawing && !current_owner(owner)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects lost their actual declared Draw");
    if (count) { memset(actors, 0, count * sizeof(*actors)); memset(views, 0, count * sizeof(*views)); }
    for (size_t i = 0; i < row_count; ++i) {
        if (rows[i].ordinal >= count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Source effects leave their reached scene queue");
        actors[rows[i].ordinal] = rows[i].actor; views[rows[i].ordinal] = rows[i].view;
    }
    return true;
}

bool frontend_equipment_source_scene_polygons(const frontend_equipment_source *owner,
    size_t count, qa_actor_id *actors, bool *views, qa_error *error)
{
    return source_effects_read(owner, owner ? owner->source_polygons : NULL,
        owner ? owner->source_polygon_count : 0, count, actors, views, error);
}

bool frontend_equipment_source_scene_lights(const frontend_equipment_source *owner,
    size_t count, qa_actor_id *actors, bool *views, qa_error *error)
{
    return source_effects_read(owner, owner ? owner->source_lights : NULL,
        owner ? owner->source_light_count : 0, count, actors, views, error);
}

bool frontend_equipment_source_scene_weapons(const frontend_equipment_source *owner,
    const qa_application_q3_equipment_source_weapon **rows, size_t *count, qa_error *error)
{
    if (!owner || !rows || !count || (!owner->drawing && owner->source_weapon_count) ||
        (owner->drawing && !current_owner(owner)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon completions require their actual Draw lease");
    *rows = owner->source_weapons; *count = owner->source_weapon_count;
    return true;
}

static bool held_source_completed(void *context, qa_actor_id actor, bool view, qa_error *error)
{
    frontend_equipment_source *owner = context;
    if (!owner->original_q3_view) return true;
    if (!owner->drawing || !current_owner(owner) ||
        !qa_actors_get(qa_session_actors(owner->client.session), actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon completion lost its actual full actor and Draw");
    if (owner->source_weapon_count == SIZE_MAX / sizeof(*owner->source_weapons))
        return frontend_fail(error, QA_ERROR_MEMORY, "Source weapon completions exceed native extent");
    qa_application_q3_equipment_source_weapon *rows = realloc(owner->source_weapons,
        (owner->source_weapon_count + 1) * sizeof(*rows));
    if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual completed Source weapon helper");
    owner->source_weapons = rows;
    rows[owner->source_weapon_count++] = (qa_application_q3_equipment_source_weapon){actor, view};
    return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Source weapon completion changed its actual Draw namespace");
}

static bool held_source_effect(frontend_equipment_source *owner, qa_actor_id actor,
    bool view, bool polygon, size_t vertices, qa_error *error)
{
    if (!owner->original_q3_view) return true;
    if (!owner->drawing || !current_owner(owner) ||
        !qa_actors_get(qa_session_actors(owner->client.session), actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon effect lost its actual full actor and Draw");
    size_t ordinal; bool available;
    bool okay = polygon ? qa_q3_presentation_poly_cursor(owner->options.presentation,
        vertices, &ordinal, &available, error) :
        qa_q3_presentation_light_cursor(owner->options.presentation, &ordinal, &available, error);
    if (!okay || !available) return okay;
    equipment_source_effect **rows = polygon ? &owner->source_polygons : &owner->source_lights;
    size_t *count = polygon ? &owner->source_polygon_count : &owner->source_light_count;
    if (*count == SIZE_MAX / sizeof(**rows))
        return frontend_fail(error, QA_ERROR_MEMORY, "Source weapon effects exceed native extent");
    for (size_t i = 0; i < *count; ++i)
        if ((*rows)[i].ordinal == ordinal)
            return frontend_fail(error, QA_ERROR_FORMAT, "Source weapon effect repeats an uncommitted ordinal");
    equipment_source_effect *values = realloc(*rows, (*count + 1) * sizeof(*values));
    if (!values) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Source weapon effect attribution");
    *rows = values; values[(*count)++] = (equipment_source_effect){ordinal, actor, view};
    return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Source weapon effect changed its genuine Draw namespace");
}

static bool held_source_poly(void *context, qa_actor_id actor, bool view,
    size_t vertices, qa_error *error)
{ return held_source_effect(context, actor, view, true, vertices, error); }

static bool held_source_light(void *context, qa_actor_id actor, bool view, qa_error *error)
{ return held_source_effect(context, actor, view, false, 0, error); }

static bool held_source(void *context, qa_actor_id actor, bool view,
    const qa_q3_ref_entity *ref, size_t *ordinal, bool *observed, qa_error *error)
{
    frontend_equipment_source *owner = context;
    *observed = false;
    if (!owner->original_q3_view) return true;
    if (!ref || !owner->drawing || !current_owner(owner) ||
        !qa_actors_get(qa_session_actors(owner->client.session), actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon ref lost its actual full actor and Draw");
    size_t entity; bool available;
    if (!qa_q3_presentation_entity_cursor(owner->options.presentation, &entity, &available, error)) return false;
    if (!available) return true;
    if (owner->source_actor_count == SIZE_MAX / sizeof(*owner->source_actors))
        return frontend_fail(error, QA_ERROR_MEMORY, "Source weapon attribution exceeds native extent");
    for (size_t i = 0; i < owner->source_actor_count; ++i)
        if (owner->source_actors[i].entity == entity)
            return frontend_fail(error, QA_ERROR_FORMAT, "Source weapon attribution repeats an uncommitted ref");
    equipment_source_actor *rows = realloc(owner->source_actors,
        (owner->source_actor_count + 1) * sizeof(*rows));
    if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Source weapon actor attribution");
    owner->source_actors = rows;
    rows[owner->source_actor_count++] = (equipment_source_actor){entity, actor, view};
    *ordinal = entity; *observed = true;
    return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Source weapon attribution changed its genuine Draw namespace");
}

static bool held_source_cancel(void *context, qa_actor_id actor, bool view,
    size_t ordinal, qa_error *error)
{
    frontend_equipment_source *owner = context;
    if (!owner->original_q3_view || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Suppressed Source ref lost its actual Draw attribution");
    for (size_t i = owner->source_actor_count; i; --i) {
        equipment_source_actor *row = owner->source_actors + i - 1;
        if (row->entity != ordinal) continue;
        if (!qa_actor_id_equal(row->actor, actor) || row->view != view)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Suppressed Source ref changed its actual weapon scope");
        memmove(row, row + 1, (owner->source_actor_count - i) * sizeof(*row));
        --owner->source_actor_count;
        return true;
    }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Suppressed Source ref has no matching pending attribution");
}

static void release_draw(void *context)
{
    frontend_equipment_source *owner = context;
    if (!owner || owner->active || owner->submitting) return;
    frontend_equipment_source_clear(owner);
    frontend_equipment_media_release(owner->view_media); owner->view_media = NULL;
    frontend_equipment_q3_release(owner->q3_presenter); owner->q3_presenter = NULL;
    frontend_equipment_gear_release(owner->gear_presenter); owner->gear_presenter = NULL;
    owner->selection = owner->hud;
    owner->drawing = false; owner->original_q3_view = false;
    free(owner->companion_refs); owner->companion_refs = NULL; owner->companion_ref_count = 0;
    for (size_t i = 0; i < owner->companion_polygon_count; ++i) free(owner->companion_polygons[i].vertices);
    free(owner->companion_polygons); owner->companion_polygons = NULL; owner->companion_polygon_count = 0;
    free(owner->companion_lights); owner->companion_lights = NULL; owner->companion_light_count = 0;
    free(owner->companion_weapons); owner->companion_weapons = NULL; owner->companion_weapon_count = 0;
    owner->companion_selected = false; owner->companion = (frontend_source_companion_view){0};
    owner->companion_view_requested = false;
    if (owner->borrowed) {
        owner->borrowed = false;
        owner->options.release(owner->options.lease);
    }
}

static bool prepare(void *context, qa_actor_owner receiver, uint32_t seat,
    qa_application_q3_equipment_draw *out, qa_error *error)
{
    frontend_equipment_source *owner = context;
    if (!owner || !out || receiver != owner->options.receiver || seat != owner->options.seat ||
        !frontend_equipment_source_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment draw requires its actual idle CGAME receiver");
    owner->draw = (qa_application_q3_equipment_draw){.view_visible = true};
    owner->selection = (qa_application_equipment_view){0};
    owner->hud = (qa_application_equipment_view){0};
    free(owner->hud_label); owner->hud_label = NULL;
    owner->client = (qa_application_q3_client_context){0};
    owner->original_q3_view = false;
    owner->companion_selected = false;
    if (!owner->options.borrow(owner->options.lease, &owner->client, error)) return false;
    owner->borrowed = true;
    if (!current_owner(owner) || !owner->client.initialized)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment draw has no current successful CGAME source context");
    owner->draw.actor = owner->client.source_actor;
    if (owner->draw.actor.registry) {
        if (!qa_application_equipment_read(owner->options.frontend->application, owner->draw.actor,
                &owner->selection, error) || !current_owner(owner)) return false;
        if (owner->selection.selected) {
            if(owner->selection.source_slot) {
                frontend_equipment_media *media=NULL;const qa_material *icon=NULL;
                if(!frontend_equipment_media_prepare_source_icon(owner->options.frontend,
                    &owner->selection,&media,&icon,error)||!current_owner(owner))return false;
            }
            if (!original_q3_match(owner, &owner->selection, &owner->original_q3_view, error)) return false;
            if (!owner->original_q3_view && owner->selection.original_qvm) {
                bool captured;
                if (!companion_prepare(owner, &captured, error)) return false;
            }
            if (owner->original_q3_view) {
                if (!current_owner(owner))
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Original weapon view lost its actual Source Draw namespace");
            } else if (owner->companion_selected) {
                if (!current_owner(owner))
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected weapon capture lost its actual Source receipt");
            } else if (owner->selection.view_model && owner->selection.view_model[0]) {
                if (owner->selection.equipment_slot) {
                    application_equipment_gear_presentation gear; bool selected;
                    if (!application_equipment_gear_presentation_read(owner->options.frontend->application,
                            owner->selection.actor, &gear, &selected, error)) return false;
                    if (!selected) return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared gear slot lost its actual source");
                    if (!frontend_equipment_gear_prepare(owner->options.frontend, &gear,
                            true, owner, current_preparation, &owner->gear_presenter, error)) return false;
                    if (!frontend_equipment_gear_retain(owner->gear_presenter, error)) {
                        owner->gear_presenter = NULL; return false;
                    }
                } else if (owner->selection.family == QA_GAME_Q3) {
                    if (!frontend_equipment_q3_prepare(owner->options.frontend, &owner->selection,
                            false, NULL, owner, current_preparation, &owner->q3_presenter, error)) return false;
                    if (!frontend_equipment_q3_retain(owner->q3_presenter, error)) {
                        owner->q3_presenter = NULL; return false;
                    }
                } else if (!frontend_equipment_media_prepare(owner->options.frontend, &owner->selection,
                        &owner->view_media, error)) return false;
                if (owner->view_media && !frontend_equipment_media_retain(owner->view_media, error)) {
                    owner->view_media = NULL; return false;
                }
            } else if (!owner->selection.source_slot && (owner->selection.visible || owner->selection.item))
                return frontend_fail(error, QA_ERROR_FORMAT, "Selected equipment has no actual view model producer");
            owner->draw.selected = !owner->original_q3_view;
            owner->draw.warning = owner->selection.warning;
        }
    }
    if (!current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment preparation retired its actual receiver or source");
    if (owner->draw.selected) {
        const qa_application_equipment_view *source = &owner->selection;
        if (source->label) {
            size_t length = strlen(source->label) + 1;
            owner->hud_label = malloc(length);
            if (!owner->hud_label) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual selected equipment HUD label");
            memcpy(owner->hud_label, source->label, length);
        }
        owner->hud = (qa_application_equipment_view){.actor=source->actor,.provider=source->provider,
            .primary=source->primary,.family=source->family,.item=source->item,.ammo=source->ammo,
            .equipment_slot=source->equipment_slot,.gear_namespace=source->gear_namespace,
            .gear_service_owner=source->gear_service_owner,
            .source_slot=source->source_slot,.source_binding=source->source_binding,
            .source_generation=source->source_generation,.view_content=source->view_content,
            .pending=source->pending,.pending_provider=source->pending_provider,
            .source_icon=source->source_icon,.source_held=source->source_held,
            .label=owner->hud_label,.ammo_count=source->ammo_count,.warning=source->warning,
            .selected=source->selected,.visible=source->visible,.has_weapon_status=source->has_weapon_status,
            .finite_ammo=source->finite_ammo,.has_ammo_to_start=source->has_ammo_to_start,
            .low_ammo=source->low_ammo,.has_start_requirement=source->has_start_requirement};
    }
    owner->drawing = true;
    *out = owner->draw;
    return true;
}

static equipment_packet **token_slot(frontend_equipment_source *owner, const void *token)
{
    equipment_packet **slot = &owner->active;
    while (*slot && *slot != token) slot = &(*slot)->next;
    return slot;
}

static bool q3_held_output(frontend_equipment_source *owner, const qa_application_equipment_view *source,
    const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *parent, int32_t powerups, bool personal_model,
    frontend_equipment_q3_output **out, bool *submitted, qa_error *error)
{
    frontend_equipment_q3_presenter *presenter = NULL;
    if (!frontend_equipment_q3_prepare(owner->options.frontend, source, false, NULL,
            owner, current_preparation, &presenter, error)) return false;
    qa_ui_preferences preferences;
    q3n_selected_weapon_held held = {.parent_assets = parent_assets, .torso = parent,
        .lighting_origin = parent->lighting_origin, .powerups = powerups, .personal_model = personal_model};
    return qa_ui_preferences_read(qa_application_cvars(owner->options.frontend->application),
        owner->options.physical_seat, &preferences, error) &&
        frontend_equipment_q3_held(presenter, source, &held, preferences.reduced_flashes,
            owner, current_preparation, out, submitted, error);
}

static bool gear_held_output(frontend_equipment_source *owner, const qa_application_equipment_view *source,
    const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *parent, int32_t powerups,
    bool personal_model, frontend_equipment_gear_output **out, bool *submitted, qa_error *error)
{
    application_equipment_gear_presentation gear; bool selected;
    if (!application_equipment_gear_presentation_read(owner->options.frontend->application,
            source->actor, &gear, &selected, error)) return false;
    if (!selected || gear.source.gear_owner != source->gear_namespace ||
        gear.source.service_owner != source->gear_service_owner)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held gear changed its actual private namespace");
    frontend_equipment_gear_presenter *presenter = NULL;
    if (!frontend_equipment_gear_prepare(owner->options.frontend, &gear, false,
            owner, current_preparation, &presenter, error)) return false;
    qa_ui_preferences preferences;
    q3n_selected_weapon_held held = {.parent_assets = parent_assets, .torso = parent,
        .lighting_origin = parent->lighting_origin, .powerups = powerups, .personal_model = personal_model};
    return qa_ui_preferences_read(qa_application_cvars(owner->options.frontend->application),
        owner->options.physical_seat, &preferences, error) &&
        frontend_equipment_gear_held(presenter, &gear, &held, preferences.reduced_flashes,
            owner, current_preparation, out, submitted, error);
}

bool frontend_equipment_source_held_begin_from(frontend_equipment_source *owner,
    qa_actor_id actor, const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *parent,
    void **token, bool *selected, qa_error *error)
{
    if (!owner || !parent_assets || !parent || !token || *token || !selected || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held equipment requires its actual active draw lease");
    *selected = false;
    qa_application_equipment_view source;
    if (!qa_application_equipment_read(owner->options.frontend->application, actor, &source, error)) return false;
    if (source.selected && source.original_qvm) {
        bool original;
        if (!original_q3_match(owner, &source, &original, error)) return false;
        if (original) {
            if (parent_assets != owner->options.assets)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Original held weapon displaced its actual CG parent registry");
            return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
                "Original held weapon lost its genuine Source Draw");
        }
    }
    if (source.selected && source.original_qvm && companion_held(owner, actor)) {
        equipment_packet *packet = calloc(1, sizeof(*packet));
        if (!packet) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining captured Source held replacement");
        packet->companion = true; packet->next = owner->active; owner->active = packet;
        *token = packet; *selected = true;
        return true;
    }
    if(source.source_slot) {
        frontend_equipment_media *media=NULL;bool authored=false;
        if(!frontend_equipment_media_prepare_source_held(owner->options.frontend,&source,&media,&authored,error))return false;
        if(!authored)return true;
        equipment_packet *packet=calloc(1,sizeof(*packet));
        if(!packet)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source held invocation");
        if(!frontend_equipment_held_output_create_from(owner->options.frontend,&source,media,
            parent_assets,owner->options.assets,parent,&packet->output,error)) {free(packet);return false;}
        packet->next=owner->active;owner->active=packet;*token=packet;*selected=true;return true;
    }
    if (!source.selected || !source.view_model || !source.view_model[0]) return true;
    equipment_packet *packet = calloc(1, sizeof(*packet));
    if (!packet) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual held source invocation");
    bool okay;
    if (source.family == QA_GAME_Q3) {
        frontend_equipment_media *media = NULL; bool authored = false;
        okay = frontend_equipment_media_prepare_q3_held(owner->options.frontend,
            &source, &media, &authored, error);
        if (okay && authored) okay = frontend_equipment_held_output_create_from(owner->options.frontend,
            &source, media, parent_assets, owner->options.assets, parent, &packet->output, error);
        else if (okay) {
            bool submitted = false;
            okay = source.equipment_slot ? gear_held_output(owner, &source, parent_assets, parent,
                0, false, &packet->gear_output, &submitted, error) :
                q3_held_output(owner, &source, parent_assets, parent, 0, false, &packet->q3_output, &submitted, error);
            if (okay && !submitted)
                okay = frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 weapon has no actual source held output");
            if (okay) okay = packet->gear_output ? frontend_equipment_gear_output_source_style(packet->gear_output,
                owner->options.assets, parent, error) : frontend_equipment_q3_output_source_style(packet->q3_output,
                    owner->options.assets, parent, error);
        }
    } else {
        frontend_equipment_media *media = NULL;
        okay = frontend_equipment_media_prepare(owner->options.frontend, &source, &media, error) &&
            current_owner(owner) && frontend_equipment_held_output_create_from(owner->options.frontend,
                &source, media, parent_assets, owner->options.assets, parent, &packet->output, error);
    }
    if (!okay) {
        frontend_equipment_held_output_destroy(packet->output);
        frontend_equipment_q3_output_destroy(packet->q3_output);
        frontend_equipment_gear_output_destroy(packet->gear_output); free(packet); return false;
    }
    packet->next = owner->active; owner->active = packet;
    *token = packet; *selected = true;
    return true;
}

static bool held_begin(void *context, qa_actor_id actor, const qa_q3_ref_entity *parent,
    void **token, bool *selected, qa_error *error)
{
    frontend_equipment_source *owner = context;
    return frontend_equipment_source_held_begin_from(owner, actor,
        owner ? owner->options.assets : NULL, parent, token, selected, error);
}

static bool held_pass(void *context, void *token, const qa_q3_ref_entity *pass, qa_error *error)
{
    frontend_equipment_source *owner = context;
    if (!owner || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held pass requires its retained source draw");
    equipment_packet *packet = *token_slot(owner, token);
    if (!packet || packet->committed)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held pass has no actual active invocation token");
    if (packet->companion) {
        if (!pass || !companion_held(owner, owner->selection.actor) ||
            !frontend_source_companion_current(owner->options.frontend, &owner->companion))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Captured Source held replacement lost its actual completed refs");
        return true;
    }
    return packet->gear_output ? frontend_equipment_gear_output_source_pass(packet->gear_output, pass, error) :
        packet->q3_output ? frontend_equipment_q3_output_source_pass(packet->q3_output, pass, error) :
        frontend_equipment_held_output_pass(packet->output, pass, error);
}

static bool held_submit(void *context, void *token, qa_error *error)
{
    frontend_equipment_source *owner = context;
    if (!owner || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held completion requires its current draw owner");
    equipment_packet *packet = *token_slot(owner, token);
    if (!packet || packet->committed)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held completion has no genuine open source token");
    packet->committed = true;
    return true;
}

static void held_release(void *context, void *token)
{
    frontend_equipment_source *owner = context;
    if (!owner) return;
    equipment_packet **slot = token_slot(owner, token), *packet = *slot;
    if (!packet) return;
    *slot = packet->next; packet->next = NULL;
    if (packet->committed) {
        if (owner->tail) owner->tail->next = packet;
        else owner->packets = packet;
        owner->tail = packet;
    } else {
        frontend_equipment_held_output_destroy(packet->output);
        frontend_equipment_q3_output_destroy(packet->q3_output);
        frontend_equipment_gear_output_destroy(packet->gear_output); free(packet);
    }
}

bool frontend_equipment_source_native_held(frontend_equipment_source *owner,
    qa_actor_id actor, const qa_q3_ref_entity *parent, int32_t powerups,
    bool personal_model, bool *authored, bool *submitted, qa_error *error)
{
    return frontend_equipment_source_native_held_from(owner, actor,
        owner ? owner->options.assets : NULL, parent, powerups, personal_model,
        authored, submitted, error);
}

bool frontend_equipment_source_native_held_from(frontend_equipment_source *owner,
    qa_actor_id actor, const qa_q3_presentation_assets *parent_assets,
    const qa_q3_ref_entity *parent, int32_t powerups,
    bool personal_model, bool *authored, bool *submitted, qa_error *error)
{
    if (authored) *authored = false;
    if (submitted) *submitted = false;
    if (!owner || !authored || !submitted || !parent_assets || !parent || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native held equipment requires its actual active receiver");
    qa_application_equipment_view source;
    if (!qa_application_equipment_read(owner->options.frontend->application, actor, &source, error)) return false;
    if (source.selected && source.original_qvm && companion_held(owner, actor)) {
        *submitted = true;
        return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
            "Native held replacement retired its genuine Source capture");
    }
    if(source.source_slot) {
        frontend_equipment_media *media=NULL;
        return frontend_equipment_media_prepare_source_held(owner->options.frontend,&source,&media,authored,error);
    }
    if (!source.selected || source.family != QA_GAME_Q3 || !source.view_model || !source.view_model[0]) return true;
    frontend_equipment_media *media = NULL;
    if (!frontend_equipment_media_prepare_q3_held(owner->options.frontend, &source,
            &media, authored, error)) return false;
    if (*authored) return true;
    equipment_packet *packet = calloc(1, sizeof(*packet));
    if (!packet) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining native selected held output");
    bool okay = source.equipment_slot ? gear_held_output(owner, &source, parent_assets, parent,
        powerups, personal_model, &packet->gear_output, submitted, error) :
        q3_held_output(owner, &source, parent_assets, parent, powerups, personal_model,
            &packet->q3_output, submitted, error);
    if (!okay || !*submitted) {
        frontend_equipment_q3_output_destroy(packet->q3_output);
        frontend_equipment_gear_output_destroy(packet->gear_output); free(packet); return okay;
    }
    packet->committed = true;
    if (owner->tail) owner->tail->next = packet; else owner->packets = packet;
    owner->tail = packet;
    return true;
}

bool frontend_equipment_source_create(const frontend_equipment_source_options *options,
    frontend_equipment_source **out, qa_error *error)
{
    if (!options || !out || *out || !options->frontend || !options->frontend->application ||
        !options->receiver || !options->assets || !options->presentation || !options->lease ||
        options->physical_seat >= options->frontend->options.seats ||
        !options->borrow || !options->current || !options->release ||
        qa_q3_presentation_resources(options->presentation) != options->assets)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment receiver constructor requires its actual frontend CGAME owners");
    frontend_equipment_source *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained CGAME equipment receiver");
    owner->options = *options;
    *out = owner;
    return true;
}

void frontend_equipment_source_services(frontend_equipment_source *owner,
    qa_application_q3_equipment_services *out)
{
    if (out) *out = (qa_application_q3_equipment_services){.context = owner,
        .prepare = prepare, .current = current_draw, .release_draw = release_draw,
        .held_begin = held_begin, .held_pass = held_pass, .held_submit = held_submit,
        .held_release = held_release, .held_source = held_source,
        .held_source_cancel = held_source_cancel,
        .held_source_poly = held_source_poly, .held_source_light = held_source_light,
        .held_source_completed = held_source_completed};
}

bool frontend_equipment_source_idle(const frontend_equipment_source *owner)
{
    return !owner || (!owner->borrowed && !owner->drawing && !owner->active &&
        !owner->packets && !owner->world_packets && !owner->submitting && !owner->view_media &&
        !owner->q3_presenter && !owner->q3_view && !owner->gear_presenter && !owner->gear_view &&
        !owner->companion_selected && !owner->companion_refs && !owner->source_actors &&
        !owner->source_polygons && !owner->source_lights && !owner->companion_polygons &&
        !owner->companion_lights && !owner->view_lights && !owner->view_projected_lights &&
        !owner->source_weapons && !owner->companion_weapons);
}

bool frontend_equipment_source_destroy(frontend_equipment_source *owner, qa_error *error)
{
    if (!owner) return true;
    if (!frontend_equipment_source_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment receiver retains actual draw or submission scopes");
    free(owner->companion_refs); free(owner->source_actors); free(owner->source_polygons);
    free(owner->source_lights); free(owner->companion_lights); free(owner->companion_polygons);
    free(owner->source_weapons); free(owner->companion_weapons);
    free(owner->view_lights); free(owner->view_projected_lights); free(owner->hud_label); free(owner);
    return true;
}

bool frontend_equipment_source_rebind_ready(const frontend_equipment_source *owner,
    const qa_frontend *owned, qa_error *error)
{
    return !owner || (owner->options.frontend == owned && frontend_equipment_source_idle(owner)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment publication requires its idle actual frontend owner");
}

void frontend_equipment_source_rebind(frontend_equipment_source *owner, qa_frontend *destination)
{
    if (owner) owner->options.frontend = destination;
}

bool frontend_equipment_source_weapon(const frontend_equipment_source *owner,
    qa_application_equipment_view *out, bool *requested, qa_error *error)
{
    if (!out || !requested)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment HUD observation requires actual output fields");
    *requested = false;
    if (!owner || !owner->draw.selected || !current_owner(owner)) return true;
    bool hud, view;
    if (!requests(owner, &hud, &view, error)) return false;
    if (hud) { *out = owner->hud; *requested = true; }
    return true;
}

static bool build_view(frontend_equipment_source *owner, qa_vec3 offset, float fov, qa_error *error)
{
    qa_application_camera_view camera;
    if (!qa_application_control_camera(owner->options.frontend->application, owner->selection.actor, &camera))
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Equipment view has no actual full-actor camera producer");
    qa_vec3 origin = camera.origin; origin.z += camera.view_height;
    if (owner->gear_presenter) {
        if (!owner->gear_view) {
            application_equipment_gear_presentation gear; bool selected;
            if (!application_equipment_gear_presentation_read(owner->options.frontend->application,
                    owner->selection.actor, &gear, &selected, error)) return false;
            if (!selected || gear.source.gear_owner != owner->selection.gear_namespace ||
                gear.source.service_owner != owner->selection.gear_service_owner)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear view changed its actual private namespace");
            const qa_q3_player *source = &gear.gear.player;
            q3n_selected_weapon_view view = {.origin = origin, .angles = camera.angles,
                .horizontal_speed = hypot((double)source->velocity[0], (double)source->velocity[1]),
                .bob_cycle = source->bobCycle, .draw_gun = true};
            bool submitted = false;
            if (!frontend_equipment_gear_view(owner->gear_presenter, &gear, &view, fov,
                    owner, current_preparation, &owner->gear_view, &submitted, error)) return false;
        }
        owner->view_offset = offset; owner->view_ready = owner->gear_view != NULL; return true;
    }
    if (owner->q3_presenter) {
        if (!owner->q3_view) {
            frontend_equipment_q3_presenter *presenter = NULL;
            if (!frontend_equipment_q3_prepare(owner->options.frontend, &owner->selection,
                    true, NULL, owner, current_preparation, &presenter, error) || presenter != owner->q3_presenter) return false;
            const qa_q3_player *source = &owner->selection.q3_source;
            q3n_selected_weapon_view view = {.origin = origin, .angles = camera.angles,
                .horizontal_speed = hypot((double)source->velocity[0], (double)source->velocity[1]),
                .bob_cycle = source->bobCycle, .draw_gun = true};
            bool submitted = false; qa_ui_preferences preferences;
            if (!qa_ui_preferences_read(qa_application_cvars(owner->options.frontend->application),
                    owner->options.physical_seat, &preferences, error) ||
                !frontend_equipment_q3_view(presenter, &owner->selection, &view, preferences.reduced_flashes,
                    owner, current_preparation, &owner->q3_view, &submitted, error)) return false;
        }
        owner->view_offset = offset; owner->view_ready = owner->q3_view != NULL;
        return true;
    }
    origin = qa_vec_add(origin, owner->selection.has_source_gun_pose ?
        owner->selection.gun_origin : owner->selection.kick_origin);
    if (owner->selection.family == QA_GAME_Q1) origin.z += 2;
    origin = qa_vec_add(origin, offset);
    qa_vec3 angles = qa_vec_add(camera.angles, owner->selection.has_source_gun_pose ?
        owner->selection.gun_angles : owner->selection.kick_angles);
    qa_q3_ref_entity *entity = &owner->view_entity;
    *entity = (qa_q3_ref_entity){.kind = QA_Q3_REF_MODEL, .flags = 4 | 8,
        .origin = origin, .old_origin = origin, .lighting_origin = origin,
        .frame = owner->selection.frame, .old_frame = owner->selection.frame,
        .skin = owner->selection.has_skin ? owner->selection.skin : 0};
    memset(entity->color, 255, sizeof(entity->color)); frontend_camera_axes(angles, entity->axis);
    qa_model_transform_identity(&owner->view_transform);
    for (size_t i = 0; i < 3; ++i) {
        owner->view_transform.axes[i][0] = entity->axis[i].x;
        owner->view_transform.axes[i][1] = entity->axis[i].y;
        owner->view_transform.axes[i][2] = entity->axis[i].z;
    }
    owner->view_transform.origin[0] = origin.x; owner->view_transform.origin[1] = origin.y;
    owner->view_transform.origin[2] = origin.z; owner->view_ready = true;
    return true;
}

bool frontend_equipment_source_native_view(frontend_equipment_source *owner, float fov, bool *consumed, qa_error *error)
{
    if (consumed) *consumed = false;
    if (!owner || !consumed || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native view admission requires its actual prepared draw");
    if (!owner->draw.selected) return true;
    if (owner->original_q3_view) return true;
    bool hud, requested;
    if (!requests(owner, &hud, &requested, error)) return false;
    if (owner->companion_selected) {
        owner->companion_view_requested = requested;
        *consumed = true;
        return current_owner(owner) || frontend_fail(error, QA_ERROR_ARGUMENT,
            "Native view replacement lost its actual companion Draw");
    }
    owner->view_ready = false;
    if (requested && owner->selection.visible) {
        if ((!owner->view_media && !owner->q3_presenter && !owner->gear_presenter) ||
            !build_view(owner, qa_v3(0, 0, 0), fov, error)) return false;
        if (!owner->view_ready)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected native view has no actual admitted model output");
    }
    *consumed = true;
    return true;
}

static bool build_world(frontend_equipment_source *owner, qa_error *error)
{
    if (owner->world_ready) return true;
    qa_application *application = owner->options.frontend->application;
    const qa_actor_registry *actors = qa_session_actors(owner->client.session);
    uint64_t revision = qa_actors_revision(actors);
    equipment_world_packet **tail = &owner->world_packets;
    uint32_t cursor = 0; const qa_actor_record *record;
    bool okay = true;
    while (okay && qa_actors_next(actors, &cursor, &record)) {
        qa_actor_id actor = record->id;
        application_equipment_gear_world_view source; bool visible = false;
        okay = application_equipment_gear_world_read(application, actor, &source, &visible, error);
        if (okay && visible) {
            equipment_world_packet *row = calloc(1, sizeof(*row));
            if (!row) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual world gear output roster");
            else {
                okay = frontend_equipment_gear_world_prepare(owner->options.frontend, &source,
                    owner->client.source_actor, owner, current_preparation, &row->output, error);
                if (okay) { *tail = row; tail = &row->next; }
                else free(row);
            }
        }
        if (okay && (!current_owner(owner) || qa_actors_revision(actors) != revision))
            okay = frontend_fail(error, QA_ERROR_ARGUMENT, "World gear preparation changed its actual actor roster or recipient");
    }
    if (!okay) { clear_world(owner); return false; }
    owner->world_ready = true;
    return true;
}

static qa_vec3 companion_vector(const qa_vec3 source[3], const qa_vec3 destination[3], qa_vec3 vector)
{
    qa_vec3 result = {0};
    for (size_t i = 0; i < 3; ++i)
        result = qa_vec_add(result, qa_vec_scale(destination[i], qa_vec_dot(vector, source[i])));
    return result;
}

static qa_vec3 companion_point(const qa_q3_refdef *definition, const qa_scene_view *view, qa_vec3 point)
{
    return qa_vec_add(view->origin, companion_vector(definition->axis, view->axis,
        qa_vec_sub(point, definition->origin)));
}

static bool companion_lights_prepare(frontend_equipment_source *owner,
    qa_q3_scene_options *options, qa_error *error)
{
    if (!owner->companion_selected || options->world.no_world) return true;
    size_t count = 0;
    for (size_t i = 0; i < owner->companion_light_count; ++i)
        if (!owner->companion_lights[i].view || owner->companion_view_requested) ++count;
    if (!count) return true;
    if ((options->world.light_count && !options->world.lights) ||
        options->world.projected_light_count > 32 ||
        (options->world.projected_light_count && !options->world.projected_lights) ||
        count > SIZE_MAX / sizeof(*owner->view_lights) ||
        options->world.light_count > SIZE_MAX / sizeof(*owner->view_lights) - count)
        return frontend_fail(error, QA_ERROR_FORMAT, "Source weapon lights leave the actual recipient span");
    size_t total = options->world.light_count + count;
    qa_scene_light *lights = malloc(total * sizeof(*lights));
    size_t projected = options->world.projected_light_count;
    size_t added = count < 32 - projected ? count : 32 - projected;
    qa_scene_light *projected_lights = added ? malloc((projected + added) * sizeof(*projected_lights)) : NULL;
    if (!lights || (added && !projected_lights)) {
        free(lights); free(projected_lights);
        return frontend_fail(error, QA_ERROR_MEMORY, "Combining genuine Source weapon lights");
    }
    if (options->world.light_count)
        memcpy(lights, options->world.lights, options->world.light_count * sizeof(*lights));
    if (added && projected)
        memcpy(projected_lights, options->world.projected_lights, projected * sizeof(*projected_lights));
    size_t at = options->world.light_count, emitted = 0;
    for (size_t i = 0; i < owner->companion_light_count; ++i) {
        const equipment_companion_light *captured = owner->companion_lights + i;
        if (captured->view && !owner->companion_view_requested) continue;
        qa_scene_light light = captured->light;
        if (captured->view) light.origin = companion_point(&captured->definition, &options->world.view, light.origin);
        lights[at++] = light;
        if (emitted < added) projected_lights[projected + emitted] = light;
        ++emitted;
    }
    if (!frontend_source_companion_current(owner->options.frontend, &owner->companion)) {
        free(lights); free(projected_lights);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source weapon lights retired their completed Draw receipt");
    }
    free(owner->view_lights); owner->view_lights = lights;
    free(owner->view_projected_lights); owner->view_projected_lights = projected_lights;
    options->world.lights = lights; options->world.light_count = total;
    if (added) { options->world.projected_lights = projected_lights; options->world.projected_light_count = projected + added; }
    return true;
}

bool frontend_equipment_source_prepare_view(frontend_equipment_source *owner,
    const qa_q3_refdef *definition, qa_q3_scene_options *options, qa_error *error)
{
    if (!owner || !definition || !options)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment view requires its actual source refdef");
    if (!owner->drawing) return true;
    if (!current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment view lost its active client lease");
    bool hud, requested;
    if (!requests(owner, &hud, &requested, error)) return false;
    if (!options->world.no_world && !build_world(owner, error)) return false;
    owner->view_ready = false;
    if (!options->world.no_world && owner->draw.selected && requested &&
        owner->selection.visible && (owner->view_media || owner->q3_presenter || owner->gear_presenter)) {
        if (!options->world.view.clip_enabled &&
            !build_view(owner, qa_vec_sub(options->world.view.origin, definition->origin), definition->fov_x, error)) return false;
    }
    size_t count = owner->view_ready ? owner->gear_view ? frontend_equipment_gear_output_count(owner->gear_view) :
        owner->q3_view ? frontend_equipment_q3_output_count(owner->q3_view) : 1 : 0;
    owner->companion_view_requested = owner->companion_selected && requested && !options->world.view.clip_enabled;
    if (!companion_lights_prepare(owner, options, error)) return false;
    if (owner->companion_selected && !options->world.no_world)
        for (size_t i = 0; i < owner->companion_ref_count; ++i)
            if (!owner->companion_refs[i].view || owner->companion_view_requested) ++count;
    for (equipment_packet *row = owner->packets; row; row = row->next) {
        size_t extent = row->companion ? 0 : row->gear_output ? frontend_equipment_gear_output_count(row->gear_output) :
            row->q3_output ? frontend_equipment_q3_output_count(row->q3_output) :
            frontend_equipment_held_output_count(row->output);
        if (extent > 1021 - count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Equipment scene exceeds its actual source ordering extent");
        count += extent;
    }
    if (count && (options->first_entity >= 1022 || count > 1021 - options->first_entity))
        return frontend_fail(error, QA_ERROR_FORMAT, "Equipment cannot reserve its physical source scene orders");
    owner->first_order = options->first_entity; owner->reserved = (uint32_t)count;
    options->first_entity += (uint32_t)count;
    options->supplemental_weapon = owner->view_ready || owner->companion_view_requested;
    return true;
}

static bool companion_submit(frontend_equipment_source *owner,
    const qa_q3_scene_options *options, uint32_t *order, qa_scene_frame *frame, qa_error *error)
{
    if (!owner->companion_selected || options->world.no_world) return true;
    for (size_t i = 0; i < owner->companion_ref_count; ++i) {
        const equipment_companion_ref *captured = owner->companion_refs + i;
        if (captured->view && !owner->companion_view_requested) continue;
        qa_q3_ref_entity ref = captured->ref;
        if (captured->view) {
            ref.origin = companion_point(&captured->definition, &options->world.view, ref.origin);
            ref.old_origin = companion_point(&captured->definition, &options->world.view, ref.old_origin);
            ref.lighting_origin = companion_point(&captured->definition, &options->world.view, ref.lighting_origin);
            for (size_t j = 0; j < 3; ++j)
                ref.axis[j] = companion_vector(captured->definition.axis, options->world.view.axis, ref.axis[j]);
        }
        if (!frontend_source_companion_current(owner->options.frontend, &owner->companion) ||
            !qa_q3_presentation_source_component_entity(owner->options.presentation, owner->companion.assets,
                &ref, captured->definition.time, options, (*order)++, frame, error)) return false;
    }
    for (size_t i = 0; i < owner->companion_polygon_count; ++i) {
        const equipment_companion_polygon *captured = owner->companion_polygons + i;
        if (captured->view && !owner->companion_view_requested) continue;
        qa_scene_vertex *vertices = captured->vertices, *transformed = NULL;
        if (captured->view && captured->polygon.count) {
            transformed = malloc(captured->polygon.count * sizeof(*transformed));
            if (!transformed) return frontend_fail(error, QA_ERROR_MEMORY, "Transforming genuine Source view weapon polygon");
            memcpy(transformed, vertices, captured->polygon.count * sizeof(*transformed));
            for (size_t j = 0; j < captured->polygon.count; ++j)
                transformed[j].position = companion_point(&captured->definition, &options->world.view, transformed[j].position);
            vertices = transformed;
        }
        bool okay = frontend_source_companion_current(owner->options.frontend, &owner->companion) &&
            qa_q3_presentation_source_component_poly(owner->options.presentation, owner->companion.assets,
                captured->polygon.shader, vertices, captured->polygon.count, &captured->polygon.fog,
                captured->definition.time, options, frame, error);
        free(transformed);
        if (!okay) return false;
    }
    return true;
}

bool frontend_equipment_source_submit(frontend_equipment_source *owner,
    const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    if (!owner || !options || !frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment submission requires its actual view and frame");
    if (!owner->drawing) return true;
    if (owner->submitting || !current_owner(owner) ||
        options->first_entity != owner->first_order + owner->reserved)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment submission left its retained source ordering and lease");
    owner->submitting = true;
    uint32_t order = owner->first_order;
    bool ok = companion_submit(owner, options, &order, frame, error);
    if (ok && owner->view_ready) {
        if (!options->world.view.clip_enabled) {
            if (owner->gear_view) ok = frontend_equipment_gear_output_submit(owner->gear_view,
                owner->options.presentation, owner->view_offset, options, order, frame, error);
            else if (owner->q3_view) ok = frontend_equipment_q3_output_submit_offset(owner->q3_view,
                owner->options.presentation, owner->view_offset, options, order, frame, error);
            else {
                frontend_equipment_media_view media;
                qa_q3_foreign_view_lighting lighting={
                    .content=owner->selection.family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q1,
                    .flags=owner->selection.family==QA_GAME_Q2?1u|4u|16u:0};
                ok = frontend_equipment_media_read(owner->view_media, &media) &&
                    qa_q3_presentation_selected_view_model(owner->options.presentation, media.view.scene,
                        media.view.model, media.view.path, &owner->view_transform, &owner->view_entity,
                        options, &lighting, order, frame, error);
            }
        }
        order += owner->gear_view ? (uint32_t)frontend_equipment_gear_output_count(owner->gear_view) :
            owner->q3_view ? (uint32_t)frontend_equipment_q3_output_count(owner->q3_view) : 1;
    }
    if (!options->world.no_world) {
        for (equipment_packet *row = owner->packets; ok && row; row = row->next) {
            if (row->companion) continue;
            ok = row->gear_output ? frontend_equipment_gear_output_submit(row->gear_output,
                owner->options.presentation, qa_v3(0, 0, 0), options, order, frame, error) :
                row->q3_output ? frontend_equipment_q3_output_submit(row->q3_output,
                owner->options.presentation, options, order, frame, error) :
                frontend_equipment_held_output_submit(row->output, owner->options.presentation,
                    options, order, frame, error);
            order += (uint32_t)(row->gear_output ? frontend_equipment_gear_output_count(row->gear_output) :
                row->q3_output ? frontend_equipment_q3_output_count(row->q3_output) :
                frontend_equipment_held_output_count(row->output));
        }
        for (equipment_world_packet *row = owner->world_packets; ok && row; row = row->next)
            ok = frontend_equipment_gear_world_submit(row->output, owner->options.presentation,
                options, frame, error);
    }
    owner->submitting = false;
    return ok;
}

typedef struct equipment_source_saved {
    qa_actor_owner receiver, source_owner;
    uint32_t seat, source_client;
    uint64_t service_owner;
    qa_actor_id source_actor;
    qa_source_frame frame;
    int32_t milliseconds;
    qa_application_q3_equipment_draw draw;
    qa_application_equipment_view hud;
    char *label;
    bool present, native_source;
} equipment_source_saved;

static bool saved_fields(qa_source_save_io *io, qa_application *application,
    equipment_source_saved *saved)
{
    uint8_t magic[4]={'Q','F','E','S'}; uint32_t schema=3;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QFES",sizeof(magic)) ||
        !qa_source_save_u32(io,&schema) || schema!=3 || !qa_source_save_bool(io,&saved->present)) return false;
    if (!saved->present) return true;
    uint32_t kind=saved->frame.kind,phase=saved->frame.phase;
    if (!frontend_save_provider(io,application,&saved->receiver) || !saved->receiver ||
        !qa_source_save_u32(io,&saved->seat) || !qa_source_save_u64(io,&saved->service_owner) || !saved->service_owner ||
        !frontend_save_provider(io,application,&saved->source_owner) ||
        !qa_source_save_actor(io,&saved->source_actor) || !qa_source_save_u32(io,&saved->source_client) ||
        !qa_source_save_bool(io,&saved->native_source) ||
        !frontend_save_provider(io,application,&saved->frame.provider) ||
        !qa_source_save_u32(io,&kind) || kind>QA_CLOCK_Q3 || !qa_source_save_u32(io,&phase) || phase>QA_FRAME_EXIT ||
        !qa_source_save_u64(io,&saved->frame.number) || !qa_source_save_u64(io,&saved->frame.start_ns) ||
        !qa_source_save_u64(io,&saved->frame.elapsed_ns) || !qa_source_save_u64(io,&saved->frame.time_ns) ||
        !qa_source_save_i32(io,&saved->milliseconds) || !qa_source_save_actor(io,&saved->draw.actor) ||
        !qa_actor_id_equal(saved->source_actor,saved->draw.actor) ||
        !qa_source_save_bool(io,&saved->draw.selected) || !qa_source_save_bool(io,&saved->draw.view_visible)) return false;
    saved->frame.kind=(qa_clock_kind)kind; saved->frame.phase=(qa_frame_phase)phase;
    uint32_t warning=saved->draw.warning;
    if (!qa_source_save_u32(io,&warning) || warning>QA_APPLICATION_AMMO_EMPTY) return false;
    saved->draw.warning=(qa_application_ammo_warning)warning;
    if (!saved->draw.selected) return true;
    qa_application_equipment_view *hud=&saved->hud;
    uint32_t family=hud->family;
    if (!qa_source_save_actor(io,&hud->actor) || !qa_actor_id_equal(hud->actor,saved->draw.actor) ||
        !qa_source_save_bool(io,&hud->source_slot) ||
        !(hud->source_slot?qa_source_save_string(io,&hud->provider):frontend_save_provider(io,application,&hud->provider)) || !hud->provider ||
        !frontend_save_provider(io,application,&hud->primary) || !hud->primary ||
        !qa_source_save_u32(io,&family) || family>QA_GAME_Q3 ||
        !qa_source_save_bool(io,&hud->equipment_slot)) return false;
    if(hud->source_slot && (hud->equipment_slot ||
        !qa_source_save_u64(io,&hud->source_generation) ||
        !qa_source_save_string(io,&hud->pending) || !qa_source_save_string(io,&hud->pending_provider) ||
        (!hud->pending!=!hud->pending_provider))) return false;
    if (hud->equipment_slot && (family!=QA_GAME_Q3 ||
        !qa_source_save_string(io,&hud->gear_namespace) || !hud->gear_namespace ||
        !qa_source_save_u64(io,&hud->gear_service_owner) || !hud->gear_service_owner)) return false;
    if (!qa_source_save_string(io,&hud->item) || !qa_source_save_string(io,&hud->ammo) ||
        !frontend_save_text(io,&saved->label) || !qa_source_save_f64(io,&hud->ammo_count) || !isfinite(hud->ammo_count) ||
        !qa_source_save_bool(io,&hud->visible) || !qa_source_save_bool(io,&hud->has_weapon_status) ||
        !qa_source_save_bool(io,&hud->finite_ammo) || !qa_source_save_bool(io,&hud->has_ammo_to_start) ||
        !qa_source_save_bool(io,&hud->low_ammo) || !qa_source_save_bool(io,&hud->has_start_requirement)) return false;
    hud->family=(qa_game_family)family; hud->selected=true;
    hud->warning=saved->draw.warning; hud->label=saved->label;
    return true;
}

static bool restore_source_hud(qa_application *app,qa_application_equipment_view *saved,qa_error *error)
{
    if(!saved->source_slot)return true;
    qa_application_equipment_view current;
    if(!qa_application_equipment_read(app,saved->actor,&current,error))return false;
    if(!current.source_slot || current.provider!=saved->provider || current.primary!=saved->primary ||
        current.family!=saved->family || current.item!=saved->item || current.ammo!=saved->ammo ||
        current.pending!=saved->pending || current.pending_provider!=saved->pending_provider ||
        current.source_generation!=saved->source_generation || current.ammo_count!=saved->ammo_count ||
        current.visible!=saved->visible || current.warning!=saved->warning ||
        current.has_weapon_status!=saved->has_weapon_status || current.finite_ammo!=saved->finite_ammo ||
        current.has_ammo_to_start!=saved->has_ammo_to_start || current.low_ammo!=saved->low_ammo ||
        current.has_start_requirement!=saved->has_start_requirement ||
        (!current.label!=!saved->label) || (current.label&&strcmp(current.label,saved->label)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved source weapon HUD differs from its actual rebound source");
    const char *label=saved->label;
    *saved=current;saved->label=label;
    return true;
}

bool frontend_equipment_source_checkpoint(const frontend_equipment_source *owner,
    qa_buffer *out, qa_error *error)
{
    if (!owner || !out || out->data || out->size || !frontend_equipment_source_idle(owner) ||
        owner->options.frontend->stepping)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment continuation requires its actual idle receiver");
    equipment_source_saved saved={.present=owner->client.frontend_lifetime!=NULL};
    if (saved.present) {
        if (!current_owner(owner))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment snapshot lost its retained actual client context");
        saved.receiver=owner->client.receiver; saved.seat=owner->client.seat;
        saved.service_owner=owner->client.service_owner; saved.source_owner=owner->client.source_owner;
        saved.source_actor=owner->client.source_actor; saved.source_client=owner->client.source_client;
        saved.native_source=owner->client.native_source; saved.frame=owner->client.source_frame;
        saved.milliseconds=owner->client.source_milliseconds; saved.draw=owner->draw;
        saved.hud=owner->hud; saved.label=owner->hud_label;
    }
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,qa_application_session(owner->options.frontend->application),error) &&
        saved_fields(&io,owner->options.frontend->application,&saved) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid actual equipment receiver continuation");
    return ok;
}

bool frontend_equipment_source_restore(frontend_equipment_source *owner,
    const qa_application_q3_client_context *client, qa_bytes bytes, qa_error *error)
{
    if (!owner || !frontend_equipment_source_idle(owner) || owner->client.frontend_lifetime ||
        owner->hud_label || owner->options.frontend->stepping)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment import requires its fresh idle physical receiver");
    qa_application *application=owner->options.frontend->application;
    equipment_source_saved saved={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(application),bytes,error) &&
        saved_fields(&io,application,&saved) && qa_source_save_finish(&io,NULL);
    if(ok&&saved.present&&saved.draw.selected)ok=restore_source_hud(application,&saved.hud,error);
    if (ok && saved.present) {
        ok=client && client->session==qa_application_session(application) &&
            client->frontend_lifetime==owner->options.lease && client->receiver==owner->options.receiver &&
            client->seat==owner->options.seat && client->initialized && client->receiver==saved.receiver &&
            client->seat==saved.seat && client->service_owner==saved.service_owner &&
            client->source_owner==saved.source_owner && qa_actor_id_equal(client->source_actor,saved.source_actor) &&
            client->source_client==saved.source_client && client->native_source==saved.native_source &&
            (!saved.draw.selected || qa_application_equipment_current(application,&saved.hud));
    }
    if (ok && saved.present) {
        owner->client=*client; owner->client.source_frame=saved.frame;
        owner->client.source_milliseconds=saved.milliseconds; owner->draw=saved.draw;
        owner->hud=saved.hud; owner->hud_label=saved.label; saved.label=NULL;
        owner->hud.label=owner->hud_label; owner->selection=owner->hud;
    }
    free(saved.label); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Equipment continuation leaves its real saved source and receiver owners");
    return ok;
}
