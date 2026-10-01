#include "equipment_q3_private.h"
#include "qa/q3_assets_save.h"

struct frontend_equipment_q3_output {
    frontend_equipment_q3_presenter *presenter;
    qa_application_equipment_view source;
    qa_q3_ref_entity refs[7];
    size_t count;
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
            if (row->users) return false;
    }
    return true;
}

void frontend_equipment_q3_content_dispose(equipment_q3_content *owner)
{
    if (!owner) return;
    while (owner->presenters) {
        frontend_equipment_q3_presenter *row = owner->presenters;
        owner->presenters = row->next; free(row);
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

static bool draw(frontend_equipment_q3_presenter *presenter, const qa_application_equipment_view *source,
    const q3n_selected_weapon_view *view, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **out, bool *submitted, qa_error *error)
{
    if (submitted) *submitted = false;
    if (!presenter || !source || !out || *out || !submitted || !current || !current(context) ||
        !source_current(presenter->owner, source) || !qa_actor_id_equal(source->actor, presenter->actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 draw lost its authentic presenter and recipient");
    frontend_equipment_q3_output *output = calloc(1, sizeof(*output));
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected Q3 numeric output");
    if (!frontend_equipment_q3_retain(presenter, error)) { free(output); return false; }
    output->presenter = presenter; output->source = *source;
    equipment_q3_call call = {output, context, current};
    q3n_selected_weapon_media media;
    bool ok = q3n_selected_media_read(presenter->owner->view.media, source->q3_source.weapon, view != NULL, &media, error);
    q3n_selected_weapon_draw request = {.player = &source->q3_source, .time = source->q3_time_ms,
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
{ return output ? output->count : 0; }
void frontend_equipment_q3_output_destroy(frontend_equipment_q3_output *output)
{ if (output) { frontend_equipment_q3_release(output->presenter); free(output); } }

bool frontend_equipment_q3_output_submit(frontend_equipment_q3_output *output, qa_q3_presentation *presentation,
    const qa_q3_scene_options *options, uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{
    if (!output || !source_current(output->presenter->owner, &output->source) ||
        !presentation || !options || !frame || first_order > 1022 || output->count > 1022 - first_order)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 submission lost its real reserved scene interval");
    for (size_t i = 0; i < output->count; ++i)
        if (!qa_q3_presentation_selected_registered(presentation, output->presenter->owner->view.assets,
            output->refs + i, options, first_order + (uint32_t)i, frame, error)) return false;
    return true;
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
