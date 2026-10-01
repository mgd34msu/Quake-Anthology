#include "equipment_source.h"
#include "equipment_held_output.h"
#include "equipment_q3.h"
#include "qa/ui_preferences.h"
#include "save_private.h"

typedef struct equipment_packet {
    struct equipment_packet *next;
    frontend_equipment_held_output *output;
    frontend_equipment_q3_output *q3_output;
    bool committed;
} equipment_packet;

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
    equipment_packet *active, *packets, *tail;
    qa_model_transform view_transform;
    qa_q3_ref_entity view_entity;
    qa_vec3 view_offset;
    uint32_t first_order, reserved;
    bool borrowed, drawing, view_ready, submitting;
};

static bool current_owner(const frontend_equipment_source *owner)
{
    return owner && owner->client.frontend_lifetime == owner->options.lease &&
        owner->client.receiver == owner->options.receiver && owner->client.seat == owner->options.seat &&
        owner->client.session == qa_application_session(owner->options.frontend->application) &&
        owner->options.current(owner->options.lease, &owner->client) &&
        (!owner->draw.selected || qa_application_equipment_current(owner->options.frontend->application,
            &owner->selection));
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

static bool requests(const frontend_equipment_source *owner, bool *hud, bool *view, qa_error *error)
{
    return owner->options.requests ? owner->options.requests(owner->options.requests_context, hud, view, error) :
        qa_application_q3_equipment_requests(owner->options.frontend->application,
            owner->options.receiver, owner->options.seat, hud, view, error);
}

void frontend_equipment_source_clear(frontend_equipment_source *owner)
{
    if (!owner) return;
    equipment_packet *row = owner->packets;
    while (row) {
        equipment_packet *next = row->next;
        frontend_equipment_held_output_destroy(row->output);
        frontend_equipment_q3_output_destroy(row->q3_output);
        free(row); row = next;
    }
    owner->packets = owner->tail = NULL;
    frontend_equipment_q3_output_destroy(owner->q3_view); owner->q3_view = NULL;
    owner->reserved = 0; owner->view_ready = false;
}

static void release_draw(void *context)
{
    frontend_equipment_source *owner = context;
    if (!owner || owner->active || owner->submitting) return;
    frontend_equipment_source_clear(owner);
    frontend_equipment_media_release(owner->view_media); owner->view_media = NULL;
    frontend_equipment_q3_release(owner->q3_presenter); owner->q3_presenter = NULL;
    owner->selection = owner->hud;
    owner->drawing = false;
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
    if (!owner->options.borrow(owner->options.lease, &owner->client, error)) return false;
    owner->borrowed = true;
    if (!current_owner(owner) || !owner->client.initialized)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment draw has no current successful CGAME source context");
    owner->draw.actor = owner->client.source_actor;
    if (owner->draw.actor.registry) {
        if (!qa_application_equipment_read(owner->options.frontend->application, owner->draw.actor,
                &owner->selection, error) || !current_owner(owner)) return false;
        if (owner->selection.selected) {
            if (owner->selection.view_model && owner->selection.view_model[0]) {
                if (owner->selection.family == QA_GAME_Q3) {
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
            } else if (owner->selection.visible || owner->selection.item)
                return frontend_fail(error, QA_ERROR_FORMAT, "Selected equipment has no actual view model producer");
            owner->draw.selected = true; owner->draw.warning = owner->selection.warning;
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

bool frontend_equipment_source_held_begin_from(frontend_equipment_source *owner,
    qa_actor_id actor, const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *parent,
    void **token, bool *selected, qa_error *error)
{
    if (!owner || !parent_assets || !parent || !token || *token || !selected || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held equipment requires its actual active draw lease");
    *selected = false;
    qa_application_equipment_view source;
    if (!qa_application_equipment_read(owner->options.frontend->application, actor, &source, error)) return false;
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
            okay = q3_held_output(owner, &source, parent_assets, parent, 0, false, &packet->q3_output, &submitted, error);
            if (okay && !submitted)
                okay = frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 weapon has no actual source held output");
            if (okay) okay = frontend_equipment_q3_output_source_style(packet->q3_output,
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
        frontend_equipment_q3_output_destroy(packet->q3_output); free(packet); return false;
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
    return packet->q3_output ? frontend_equipment_q3_output_source_pass(packet->q3_output, pass, error) :
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
        frontend_equipment_q3_output_destroy(packet->q3_output); free(packet);
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
    if (!source.selected || source.family != QA_GAME_Q3 || !source.view_model || !source.view_model[0]) return true;
    frontend_equipment_media *media = NULL;
    if (!frontend_equipment_media_prepare_q3_held(owner->options.frontend, &source,
            &media, authored, error)) return false;
    if (*authored) return true;
    equipment_packet *packet = calloc(1, sizeof(*packet));
    if (!packet) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining native selected held output");
    bool okay = q3_held_output(owner, &source, parent_assets, parent, powerups, personal_model,
        &packet->q3_output, submitted, error);
    if (!okay || !*submitted) {
        frontend_equipment_q3_output_destroy(packet->q3_output); free(packet); return okay;
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
    if (out) *out = (qa_application_q3_equipment_services){owner, prepare, current_draw,
        release_draw, held_begin, held_pass, held_submit, held_release};
}

bool frontend_equipment_source_idle(const frontend_equipment_source *owner)
{
    return !owner || (!owner->borrowed && !owner->drawing && !owner->active &&
        !owner->packets && !owner->submitting && !owner->view_media &&
        !owner->q3_presenter && !owner->q3_view);
}

bool frontend_equipment_source_destroy(frontend_equipment_source *owner, qa_error *error)
{
    if (!owner) return true;
    if (!frontend_equipment_source_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment receiver retains actual draw or submission scopes");
    free(owner->hud_label); free(owner);
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

static bool build_view(frontend_equipment_source *owner, qa_vec3 offset, qa_error *error)
{
    qa_application_camera_view camera;
    if (!qa_application_control_camera(owner->options.frontend->application, owner->selection.actor, &camera))
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Equipment view has no actual full-actor camera producer");
    qa_vec3 origin = camera.origin; origin.z += camera.view_height;
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

bool frontend_equipment_source_native_view(frontend_equipment_source *owner, bool *consumed, qa_error *error)
{
    if (consumed) *consumed = false;
    if (!owner || !consumed || !owner->drawing || !current_owner(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native view admission requires its actual prepared draw");
    if (!owner->draw.selected) return true;
    bool hud, requested;
    if (!requests(owner, &hud, &requested, error)) return false;
    owner->view_ready = false;
    if (requested && owner->selection.visible) {
        if ((!owner->view_media && !owner->q3_presenter) || !build_view(owner, qa_v3(0, 0, 0), error)) return false;
        if (!owner->view_ready)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected native view has no actual admitted model output");
    }
    *consumed = true;
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
    owner->view_ready = false;
    if (!options->world.no_world && owner->draw.selected && requested &&
        owner->selection.visible && (owner->view_media || owner->q3_presenter)) {
        if (!options->world.view.clip_enabled &&
            !build_view(owner, qa_vec_sub(options->world.view.origin, definition->origin), error)) return false;
    }
    size_t count = owner->view_ready ? owner->q3_view ? frontend_equipment_q3_output_count(owner->q3_view) : 1 : 0;
    for (equipment_packet *row = owner->packets; row; row = row->next) {
        size_t extent = row->q3_output ? frontend_equipment_q3_output_count(row->q3_output) :
            frontend_equipment_held_output_count(row->output);
        if (extent > 1021 - count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Equipment scene exceeds its actual source ordering extent");
        count += extent;
    }
    if (options->first_entity >= 1022 || count > 1021 - options->first_entity)
        return frontend_fail(error, QA_ERROR_FORMAT, "Equipment cannot reserve its physical source scene orders");
    owner->first_order = options->first_entity; owner->reserved = (uint32_t)count;
    options->first_entity += (uint32_t)count;
    options->supplemental_weapon = owner->view_ready;
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
    bool ok = true;
    if (owner->view_ready) {
        if (!options->world.view.clip_enabled) {
            if (owner->q3_view) ok = frontend_equipment_q3_output_submit_offset(owner->q3_view,
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
        order += owner->q3_view ? (uint32_t)frontend_equipment_q3_output_count(owner->q3_view) : 1;
    }
    if (!options->world.no_world)
        for (equipment_packet *row = owner->packets; ok && row; row = row->next) {
            ok = row->q3_output ? frontend_equipment_q3_output_submit(row->q3_output,
                owner->options.presentation, options, order, frame, error) :
                frontend_equipment_held_output_submit(row->output, owner->options.presentation,
                    options, order, frame, error);
            order += (uint32_t)(row->q3_output ? frontend_equipment_q3_output_count(row->q3_output) :
                frontend_equipment_held_output_count(row->output));
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
    uint8_t magic[4]={'Q','F','E','S'}; uint32_t schema=1;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QFES",sizeof(magic)) ||
        !qa_source_save_u32(io,&schema) || schema!=1 || !qa_source_save_bool(io,&saved->present)) return false;
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
        !frontend_save_provider(io,application,&hud->provider) || !hud->provider ||
        !frontend_save_provider(io,application,&hud->primary) || !hud->primary ||
        !qa_source_save_u32(io,&family) || family>QA_GAME_Q3 ||
        !qa_source_save_string(io,&hud->item) || !qa_source_save_string(io,&hud->ammo) ||
        !frontend_save_text(io,&saved->label) || !qa_source_save_f64(io,&hud->ammo_count) || !isfinite(hud->ammo_count) ||
        !qa_source_save_bool(io,&hud->visible) || !qa_source_save_bool(io,&hud->has_weapon_status) ||
        !qa_source_save_bool(io,&hud->finite_ammo) || !qa_source_save_bool(io,&hud->has_ammo_to_start) ||
        !qa_source_save_bool(io,&hud->low_ammo) || !qa_source_save_bool(io,&hud->has_start_requirement)) return false;
    hud->family=(qa_game_family)family; hud->selected=true;
    hud->warning=saved->draw.warning; hud->label=saved->label;
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
