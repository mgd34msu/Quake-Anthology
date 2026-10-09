#include "equipment_q3_private.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_asset_shader.h"
#include "qa/scene_world_save.h"
#include "config_store.h"
#include "q3_color_policy.h"
#include "material_movies.h"

struct frontend_equipment_q3_output {
    frontend_equipment_q3_presenter *presenter;
    qa_application_equipment_view source;
    qa_q3_ref_entity refs[7];
    qa_q3_presentation_assets *source_assets;
    qa_q3_ref_entity *source_passes;
    size_t count, base_count, pass_count, pass_capacity;
    bool view, source_style;
};
typedef struct equipment_q3_call {
    frontend_equipment_q3_output *output;
    void *context;
    bool (*current)(void *);
} equipment_q3_call;

static bool source_current(equipment_q3_content *owner, const qa_application_equipment_view *source)
{
    return owner && source && source->family == QA_GAME_Q3 && source->has_q3_source &&
        source->selected && source->provider == owner->view.provider &&
        source->q3_source.product == owner->view.product &&
        source->q3_source.weapon == (int32_t)source->q3_weapon &&
        qa_application_equipment_current(owner->frontend->application, source);
}

bool frontend_equipment_q3_idle(const qa_frontend *frontend)
{
    const frontend_equipment_q3 *equipment = frontend ? frontend->equipment_q3 : NULL;
    if (!equipment) return true;
    if (equipment->admitting) return false;
    for (const equipment_q3_content *owner = equipment->contents; owner; owner = owner->next) {
        if (owner->admitting || !qa_q3_assets_idle(owner->view.assets) ||
            !q3n_selected_media_idle(owner->view.media) || !q3n_weapons_idle(owner->weapons)) return false;
        for (const frontend_equipment_q3_presenter *row = owner->presenters; row; row = row->next)
            if (row->users || (row->recipient && !qa_q3_presentation_idle(row->recipient))) return false;
    }
    return true;
}

void frontend_equipment_q3_content_dispose(equipment_q3_content *owner)
{
    if (!owner) return;
    while (owner->presenters) {
        frontend_equipment_q3_presenter *row = owner->presenters;
        owner->presenters = row->next;
        (void)qa_q3_presentation_destroy(row->recipient, NULL); free(row);
    }
    q3n_weapons_destroy(owner->weapons);
    q3n_selected_media_destroy(owner->view.media);
    qa_q3_presentation_assets_destroy(owner->view.assets);
    free(owner);
}

bool frontend_equipment_q3_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_equipment_q3_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 equipment retains a real draw, registration or capture");
    if (!frontend || !frontend->equipment_q3) return true;
    equipment_q3_content *row = frontend->equipment_q3->contents;
    while (row) { equipment_q3_content *next = row->next; frontend_equipment_q3_content_dispose(row); row = next; }
    frontend->equipment_q3->contents = frontend->equipment_q3->tail = NULL;
    return true;
}

void frontend_equipment_q3_destroy(qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment_q3 || !frontend_equipment_q3_retire(frontend, NULL)) return;
    free(frontend->equipment_q3); frontend->equipment_q3 = NULL;
}

static bool content_create(qa_frontend *frontend, const qa_application_equipment_view *source,
    equipment_q3_content **out, qa_error *error)
{
    equipment_q3_content *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected Q3 content registry");
    owner->frontend = frontend; owner->view.provider = source->provider;
    owner->view.product = source->q3_source.product;
    bool ok = frontend_visual_media_acquire(frontend, source->provider, QA_GAME_Q3, &owner->view.content, error);
    qa_q3_presentation_asset_options assets = {.provider = {
        .mounts = owner->view.content.mounts, .images = owner->view.content.images,
        .materials = owner->view.content.materials, .family = QA_SCENE_Q3}};
    if (ok) ok = qa_q3_presentation_assets_create(&assets, &owner->view.assets, error);
    q3n_selected_media_options media = {.content = owner->view.content.mounts,
        .assets = owner->view.assets, .product = owner->view.product};
    if (ok) ok = q3n_selected_media_create(&media, &owner->view.media, error);
    q3n_weapon_options weapons = {.product = owner->view.product, .assets = owner->view.assets};
    if (ok) ok = q3n_weapons_create(&weapons, &owner->weapons, error);
    if (!ok) { frontend_equipment_q3_content_dispose(owner); return false; }
    *out = owner; return true;
}

bool frontend_equipment_q3_prepare(qa_frontend *frontend, const qa_application_equipment_view *source,
    bool view_required, const q3n_selected_animation *character,
    void *context, bool (*current)(void *), frontend_equipment_q3_presenter **out, qa_error *error)
{
    if (!frontend || !source || !out || !current || !source->selected || source->family != QA_GAME_Q3 ||
        !source->has_q3_source || !source->actor.registry ||
        source->q3_source.weapon != (int32_t)source->q3_weapon || source->q3_weapon == QA_Q3_W_NONE ||
        !qa_application_equipment_current(frontend->application, source) || !current(context) ||
        (frontend->equipment_q3 && frontend->equipment_q3->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 media requires its actual full actor and recipient lease");
    if (!frontend->equipment_q3) {
        frontend->equipment_q3 = calloc(1, sizeof(*frontend->equipment_q3));
        if (!frontend->equipment_q3) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected Q3 equipment owner");
    }
    frontend_equipment_q3 *equipment = frontend->equipment_q3;
    equipment_q3_content *owner = equipment->contents;
    while (owner && owner->view.provider != source->provider) owner = owner->next;
    bool fresh = !owner; equipment->admitting = true;
    bool ok = !fresh || content_create(frontend, source, &owner, error);
    if (ok && !source_current(owner, source))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 content changed its real source product");
    frontend_equipment_q3_presenter *presenter = ok ? owner->presenters : NULL;
    while (presenter && !qa_actor_id_equal(presenter->actor, source->actor)) presenter = presenter->next;
    frontend_equipment_q3_presenter *candidate = NULL;
    if (ok && !presenter) {
        candidate = calloc(1, sizeof(*candidate));
        if (!candidate) ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining full actor selected Q3 presenter");
        else { candidate->actor = source->actor; candidate->owner = owner; presenter = candidate; }
    }
    if (ok) {
        owner->admitting = true;
        q3n_selected_media_request request = {.weapon = source->q3_source.weapon,
            .view_required = view_required, .character = character, .context = context, .current = current};
        q3n_selected_weapon_media admitted;
        ok = q3n_selected_media_prepare(owner->view.media, &request, &admitted, error);
        if (ok && (!source_current(owner, source) || !current(context)))
            ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 preparation retired its actual source or recipient");
        owner->admitting = false;
    }
    equipment->admitting = false;
    if (!ok) { free(candidate); if (fresh) frontend_equipment_q3_content_dispose(owner); return false; }
    if (fresh) {
        if (equipment->tail) equipment->tail->next = owner; else equipment->contents = owner;
        equipment->tail = owner;
    }
    if (candidate) {
        if (owner->tail) owner->tail->next = candidate; else owner->presenters = candidate;
        owner->tail = candidate;
    }
    *out = presenter; return true;
}

bool frontend_equipment_q3_retain(frontend_equipment_q3_presenter *presenter, qa_error *error)
{
    if (!presenter || presenter->users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 presenter retain exceeds its real lifetime");
    ++presenter->users; return true;
}
void frontend_equipment_q3_release(frontend_equipment_q3_presenter *presenter)
{ if (presenter) --presenter->users; }

static bool call_current(void *context)
{
    equipment_q3_call *call = context;
    return call->current(call->context) && source_current(call->output->presenter->owner, &call->output->source);
}
static bool collect(void *context, qa_q3_presentation_assets *assets, const qa_q3_ref_entity *ref, qa_error *error)
{
    equipment_q3_call *call = context; frontend_equipment_q3_output *output = call->output;
    if (!call_current(call) || assets != output->presenter->owner->view.assets || !ref ||
        output->count == sizeof(output->refs) / sizeof(output->refs[0]))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 output exceeds its actual registry or pass lifetime");
    output->refs[output->count++] = *ref; return true;
}

static bool draw_output(frontend_equipment_q3_presenter *presenter, const qa_application_equipment_view *source,
    const q3n_selected_weapon_view *view, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output *output, bool *submitted, qa_error *error)
{
    if (submitted) *submitted = false;
    if (!presenter || !source || !output || !submitted || !current || !current(context) ||
        !source_current(presenter->owner, source) || !qa_actor_id_equal(source->actor, presenter->actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 draw lost its authentic presenter and recipient");
    output->presenter = presenter; output->source = *source; output->view = view != NULL;
    equipment_q3_call call = {output, context, current};
    q3n_selected_weapon_media media;
    bool ok = q3n_selected_media_read(presenter->owner->view.media, source->q3_source.weapon, view != NULL, &media, error);
    q3n_selected_weapon_draw request = {.player = &source->q3_source, .time = source->q3_time_ms,
        .presentation_weapon = source->q3_source.weapon,
        .has_last_fire = source->q3_fire.present, .last_fire = source->q3_fire.time_ms,
        .firing = (source->q3_source.eFlags & 256) != 0, .reduced_flashes = reduced_flashes,
        .context = &call, .current = call_current, .submit = collect};
    if (ok && view) {
        q3n_selected_animation animation = {0}; bool character;
        ok = q3n_selected_media_animation(presenter->owner->view.media, &animation, &character, error);
        q3n_selected_weapon_view actual = *view; actual.animations = animation.config;
        if (ok) ok = q3n_weapons_selected_view(presenter->owner->weapons, &media,
            &presenter->state, &request, &actual, submitted, error);
    } else if (ok) ok = q3n_weapons_selected_held(presenter->owner->weapons, &media,
        &presenter->state, &request, held, submitted, error);
    return ok;
}

static bool draw(frontend_equipment_q3_presenter *presenter, const qa_application_equipment_view *source,
    const q3n_selected_weapon_view *view, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **out, bool *submitted, qa_error *error)
{
    if (!out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 draw requires an empty output slot");
    frontend_equipment_q3_output *output = calloc(1, sizeof(*output));
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected Q3 numeric output");
    if (!frontend_equipment_q3_retain(presenter, error)) { free(output); return false; }
    output->presenter = presenter;
    bool ok = draw_output(presenter, source, view, held, reduced_flashes,
        context, current, output, submitted, error);
    if (!ok || !*submitted) { frontend_equipment_q3_output_destroy(output); return ok; }
    *out = output; return true;
}

bool frontend_equipment_q3_view(frontend_equipment_q3_presenter *presenter,
    const qa_application_equipment_view *source, const q3n_selected_weapon_view *view, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **out, bool *submitted, qa_error *error)
{ return draw(presenter, source, view, NULL, reduced_flashes, context, current, out, submitted, error); }
bool frontend_equipment_q3_held(frontend_equipment_q3_presenter *presenter,
    const qa_application_equipment_view *source, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **out, bool *submitted, qa_error *error)
{ return draw(presenter, source, NULL, held, reduced_flashes, context, current, out, submitted, error); }
size_t frontend_equipment_q3_output_count(const frontend_equipment_q3_output *output)
{
    return !output ? 0 : !output->source_style ? output->count :
        output->base_count * output->pass_count + output->count - output->base_count;
}
void frontend_equipment_q3_output_destroy(frontend_equipment_q3_output *output)
{ if (output) { frontend_equipment_q3_release(output->presenter); free(output->source_passes); free(output); } }

bool frontend_equipment_q3_output_source_style(frontend_equipment_q3_output *output,
    qa_q3_presentation_assets *assets, const qa_q3_ref_entity *parent, qa_error *error)
{
    q3n_selected_weapon_media media;
    if (!output || output->view || output->source_style || !assets || !parent ||
        !source_current(output->presenter->owner, &output->source) || !qa_q3_assets_idle(assets) ||
        parent->kind != QA_Q3_REF_MODEL || !qa_vec_finite(parent->lighting_origin) ||
        !isfinite(parent->shadow_plane) ||
        !q3n_selected_media_read(output->presenter->owner->view.media,
            output->source.q3_source.weapon, false, &media, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 source style requires its real retained torso and registries");
    if (!output->count || output->refs[0].model != media.gun || output->refs[0].custom_shader)
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 source style has no genuine base gun group");
    size_t base = 1;
    if (output->count > base && media.barrel && output->refs[base].model == media.barrel &&
        !output->refs[base].custom_shader) ++base;
    if (output->count > base && (output->count != base + 1 ||
        output->refs[base].model != media.flash || output->refs[base].custom_shader))
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 source style contains unrelated native powerup passes");
    for (size_t i = 0; i < output->count; ++i) {
        output->refs[i].flags = parent->flags;
        output->refs[i].lighting_origin = parent->lighting_origin;
        output->refs[i].shadow_plane = parent->shadow_plane;
    }
    output->source_assets = assets; output->base_count = base; output->source_style = true;
    return true;
}

bool frontend_equipment_q3_output_source_pass(frontend_equipment_q3_output *output,
    const qa_q3_ref_entity *pass, qa_error *error)
{
    const qa_material *material;
    if (!output || !output->source_style || !pass || !isfinite(pass->shader_time) ||
        !source_current(output->presenter->owner, &output->source) ||
        !qa_q3_assets_shader_read(output->source_assets, pass->custom_shader, &material, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 source pass lost its captured primary shader owner");
    size_t remaining = output->count - output->base_count;
    if (output->pass_count >= (1022 - remaining) / output->base_count)
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 held groups exceed their actual scene extent");
    if (output->pass_count == output->pass_capacity) {
        size_t capacity = output->pass_capacity ? output->pass_capacity * 2 : 4;
        if (capacity > 1022) capacity = 1022;
        qa_q3_ref_entity *passes = realloc(output->source_passes, capacity * sizeof(*passes));
        if (!passes) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining primary selected Q3 shader passes");
        output->source_passes = passes; output->pass_capacity = capacity;
    }
    output->source_passes[output->pass_count++] = *pass;
    return true;
}

static qa_q3_ref_entity shifted(const qa_q3_ref_entity *ref, qa_vec3 offset)
{
    qa_q3_ref_entity result = *ref;
    result.origin = qa_vec_add(result.origin, offset); result.old_origin = qa_vec_add(result.old_origin, offset);
    result.lighting_origin = qa_vec_add(result.lighting_origin, offset); result.shadow_plane += offset.z;
    return result;
}

static bool output_submit(frontend_equipment_q3_output *output, qa_q3_presentation *presentation,
    qa_vec3 offset,
    const qa_q3_scene_options *options, uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{
    if (!output || !source_current(output->presenter->owner, &output->source) ||
        !presentation || !options || !frame || !qa_vec_finite(offset) || first_order > 1022 ||
        frontend_equipment_q3_output_count(output) > 1022 - first_order)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 submission lost its real reserved scene interval");
    for (size_t i = 0; i < output->count; ++i) {
        qa_q3_ref_entity ref = shifted(output->refs + i, offset);
        if (!qa_vec_finite(ref.origin) || !qa_vec_finite(ref.old_origin) ||
            !qa_vec_finite(ref.lighting_origin) || !isfinite(ref.shadow_plane))
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 view offset exceeds finite scene coordinates");
    }
    size_t first = 0;
    if (output->source_style) {
        for (size_t pass = 0; pass < output->pass_count; ++pass)
            for (size_t part = 0; part < output->base_count; ++part) {
                qa_q3_ref_entity ref = shifted(output->refs + part, offset);
                if (!qa_q3_presentation_selected_registered_pass(presentation, output->presenter->owner->view.assets,
                    &ref, output->source_assets, output->source_passes + pass, options, first_order++, frame, error)) return false;
            }
        first = output->base_count;
    }
    for (size_t i = first; i < output->count; ++i) {
        qa_q3_ref_entity ref = shifted(output->refs + i, offset);
        if (!qa_q3_presentation_selected_registered(presentation, output->presenter->owner->view.assets,
            &ref, options, first_order++, frame, error)) return false;
    }
    return true;
}

bool frontend_equipment_q3_output_submit(frontend_equipment_q3_output *output, qa_q3_presentation *presentation,
    const qa_q3_scene_options *options, uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{ return output_submit(output, presentation, qa_v3(0, 0, 0), options, first_order, frame, error); }
bool frontend_equipment_q3_output_submit_offset(frontend_equipment_q3_output *output,
    qa_q3_presentation *presentation, qa_vec3 offset, const qa_q3_scene_options *options,
    uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{
    if (!output || !output->view)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 view offset requires its actual queued view output");
    return output_submit(output, presentation, offset, options, first_order, frame, error);
}

typedef struct equipment_local_view {
    qa_frontend *frontend;
    uint32_t physical_seat, launch_seat;
    const qa_application_equipment_view *source;
} equipment_local_view;

static bool local_view_current(void *context)
{
    const equipment_local_view *view = context;
    uint32_t launch_seat; qa_actor_id actor;
    return frontend_seat_launch_id_read(view->frontend, view->physical_seat, &launch_seat) &&
        launch_seat == view->launch_seat &&
        qa_application_player_actor(view->frontend->application, launch_seat, &actor) &&
        qa_actor_id_equal(actor, view->source->actor) &&
        qa_application_equipment_current(view->frontend->application, view->source);
}

static double local_view_clock(void *context)
{
    const frontend_equipment_q3_presenter *presenter = context;
    return presenter->source_time_ms;
}

bool frontend_equipment_q3_local_view(qa_frontend *frontend, uint32_t physical_seat,
    const qa_application_equipment_view *source, const qa_scene_world_input *world,
    qa_scene_frame *frame, qa_error *error)
{
    uint32_t launch_seat;
    if (!frontend || !source || !world || frame != &frontend->frame ||
        physical_seat >= frontend->options.seats || world->view.seat != physical_seat ||
        !source->selected || source->family != QA_GAME_Q3 || !source->has_q3_source ||
        source->source_slot || source->original_qvm || source->equipment_slot ||
        !frontend_seat_launch_id_read(frontend, physical_seat, &launch_seat))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected view requires its actual local seat and scene");
    equipment_local_view call = {frontend, physical_seat, launch_seat, source};
    if (!local_view_current(&call))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected view lost its local player recipient");
    if (!source->visible || world->view.clip_enabled || frontend->seats[physical_seat].q1_chase ||
        source->q3_source.persistant[3] == 3 || source->q3_source.pmType == 5) return true;
    qa_application_camera_view camera;
    if (!qa_application_control_camera(frontend->application, source->actor, &camera))
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected view lost its actual player camera");
    if (camera.cutscene) return true;
    frontend_equipment_q3_presenter *presenter = NULL;
    if (!frontend_equipment_q3_prepare(frontend, source, true, NULL,
            &call, local_view_current, &presenter, error)) return false;
    qa_q3_presentation_options settings = {.assets = presenter->owner->view.assets,
        .clock = {presenter, local_view_clock}, .seat = physical_seat,
        .viewport = world->view.viewport, .near_clip = 4, .far_clip = 16384,
        .identity_light = 1, .lod_scale = 5, .rail_segment_length = 32,
        .video_frame = frontend_material_movies_frontend_resolve, .video_context = frontend};
    if (!presenter->recipient) {
        const qa_launch_snapshot *publication = qa_application_launch(frontend->application);
        const char *instance = qa_application_provider_instance(frontend->application, source->provider);
        const qa_launch_instance *descriptor = instance ? qa_launch_snapshot_find(publication, instance) : NULL;
        if (descriptor) {
            qa_application_startup_source tuple;
            if (!qa_application_startup_source_read(frontend->application, publication, descriptor, &tuple, error))
                return false;
            if (tuple.scope.provider != source->provider)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected presenter changed its actual equipment provider");
            frontend_config_source *config = frontend_config_store_source(frontend->config_store, tuple.cvars);
            presenter->cvars = frontend_config_source_seat_cvars(config, launch_seat);
            qa_hud_cvars_bind(presenter->cvars, QA_HUD_CVAR_DRAW_GUN, &presenter->hud_cvars);
        }
        if (!frontend_source_identity_allocate(frontend, &settings.owner, error) ||
            !qa_q3_presentation_create(&settings, &presenter->recipient, error)) return false;
        presenter->physical_seat = physical_seat;
    }
    if (presenter->physical_seat != physical_seat)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected presenter changed its physical recipient seat");
    presenter->source_time_ms = source->q3_time_ms;
    const qa_cvar_view *draw = qa_cvars_read(presenter->cvars, presenter->hud_cvars.draw_gun);
    if (draw && draw->integer == 0) return true;
    qa_scene_world_options recipient;
    if (!qa_scene_world_options_read(frontend->scene_world, &recipient) ||
        !qa_q3_presentation_frame(presenter->recipient, frame, world->view.viewport, error)) return false;
    qa_q3_scene_options options = {.world = *world, .world_family = recipient.images.family,
        .ambient_scale = .6f, .directed_scale = 1, .near_clip = settings.near_clip,
        .lod_scale = settings.lod_scale, .split_screen = frontend->options.seats > 1};
    qa_scene_state_default(&options.state);
    if (!frontend_q3_generic_recipient(frontend, &options.world, error)) return false;
    q3n_selected_weapon_view view = {.origin = world->view.origin, .angles = camera.angles,
        .horizontal_speed = hypot((double)source->q3_source.velocity[0], (double)source->q3_source.velocity[1]),
        .bob_cycle = source->q3_source.bobCycle, .draw_gun = true};
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(frontend->application),qa_application_ui_preference_handles(frontend->application), physical_seat, &preferences, error) ||
        !frontend_equipment_q3_retain(presenter, error)) return false;
    frontend_equipment_q3_output output = {0}; bool submitted = false;
    bool ok = draw_output(presenter, source, &view, NULL, preferences.reduced_flashes,
        &call, local_view_current, &output, &submitted, error);
    for (size_t i = 0; ok && i < output.count; ++i)
        ok = qa_q3_presentation_completed_entity(presenter->recipient, presenter->owner->view.assets,
            frontend->scene_world, output.refs + i, source->q3_time_ms, &options, (uint32_t)i, frame, error);
    if (ok && !submitted)
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Visible selected weapon produced no actual model output");
    frontend_equipment_q3_release(presenter);
    return ok && (local_view_current(&call) || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Selected view changed its actual local player recipient"));
}

size_t frontend_equipment_q3_count(const qa_frontend *frontend)
{
    size_t count = 0;
    for (const equipment_q3_content *owner = frontend && frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
        owner; owner = owner->next) ++count;
    return count;
}
bool frontend_equipment_q3_at(const qa_frontend *frontend, size_t ordinal,
    frontend_equipment_q3_owner_view *out, qa_error *error)
{
    const equipment_q3_content *owner = frontend && frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
    while (owner && ordinal) { owner = owner->next; --ordinal; }
    if (!owner || !out || owner->admitting)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 inventory lacks its actual physical content owner");
    *out = owner->view; return true;
}

bool frontend_equipment_q3_content_visit(const qa_frontend *frontend,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->view ||
        (frontend->equipment_q3 && frontend->equipment_q3->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 content visitor requires its actual retained owner");
    for (const equipment_q3_content *owner = frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
        owner; owner = owner->next) {
        q3n_selected_animation animation = {0}; bool character;
        if (owner->admitting || !q3n_selected_media_animation(owner->view.media, &animation, &character, error) ||
            !visitor->view(visitor->context, owner->view.content.mounts, error)) return false;
        if (animation.content && !visitor->view(visitor->context, animation.content, error)) return false;
    }
    return true;
}
