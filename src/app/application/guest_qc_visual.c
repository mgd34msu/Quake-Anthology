#include "guest_qc_visual.h"
#include "guest_qc_internal.h"
#include "qa/qc_visual.h"
#include <limits.h>

static bool source_integer(float value, int32_t *out, qa_error *error)
{
    double integer = trunc((double)value);
    if (integer < INT32_MIN || integer > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "QC appearance integer exceeds source bounds");
    *out = (int32_t)integer;
    return true;
}

static bool source_colors(const struct application_qc_state *engine, int32_t colormap,
    qa_application_visual_view *out, qa_error *error)
{
    if (colormap <= 0 || (uint32_t)colormap > engine->max_clients || !engine->clients) return true;
    const application_qc_client *client = &engine->clients[colormap];
    if (!client->connected) return true;
    const qa_actor_record *actor = qa_actors_get(qa_session_actors(engine->services.session), client->actor);
    qa_qc_slot_binding binding;
    if (!actor || !qa_qc_slot(engine->provider->state.qc.instance, (uint32_t)colormap, &binding) ||
        (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED) ||
        !qa_actor_id_equal(binding.actor, client->actor) || binding.owner != actor->owner ||
        binding.source_slot != (actor->has_source ? actor->source_slot : 0) ||
        (binding.kind == QA_QC_SLOT_OWNED && (actor->owner != engine->provider->owner ||
         !actor->has_source || actor->source_slot != (uint32_t)colormap)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QC colormap lost its actual connected source client");
    out->player_colors = client->colors;
    out->has_player_colors = true;
    return true;
}

bool application_qc_visual(application_provider *provider, qa_actor_id actor,
    qa_application_visual_view *out, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !out || !provider->state.qc.instance || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC appearance requires its actual idle source owner");
    qa_qc_visual source = {0};
    bool found = false;
    for (uint32_t slot = 1; slot < qa_qc_entity_count(provider->state.qc.instance); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QC appearance physical source row is missing");
        if (binding.kind == QA_QC_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor)) continue;
        if (!qa_qc_visual_read(provider->state.qc.instance, slot, actor, &source, error)) return false;
        found = true;
        break;
    }
    if (!found) return application_fail(error, QA_ERROR_NOT_FOUND, "QC actor has no actual source appearance row");
    int32_t model_index, effects;
    if (!source_integer(source.model_index, &model_index, error) || model_index < 0 ||
        !source_integer(source.frame, &out->frame, error) ||
        !source_integer(source.skin, &out->skin, error) ||
        !source_integer(source.colormap, &out->colormap, error) ||
        !source_integer(source.effects, &effects, error)) {
        if (error && error->code == QA_OK)
            application_fail(error, QA_ERROR_FORMAT, "QC appearance model index is negative");
        return false;
    }
    out->family = QA_GAME_Q1;
    out->old_frame = out->frame;
    out->effects = (uint32_t)effects;
    out->alpha = source.alpha == 0 ? 1 : fmaxf(0, fminf(1, source.alpha));
    out->scale = source.scale == 0 ? 1 : source.scale;
    out->visible = source.model[0] != '\0' && model_index != 0;
    if (source.model[0]) {
        const application_qc_resource *resource = NULL;
        for (size_t i = 0; i < engine->resource_count; ++i)
            if (engine->resources[i].kind == QA_QC_RESOURCE_MODEL &&
                !strcmp(engine->resources[i].name, source.model)) {
                resource = &engine->resources[i];
                break;
            }
        if (!resource)
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC appearance model has no retained source precache");
        if (model_index && resource->value.index != (uint32_t)model_index)
            return application_fail(error, QA_ERROR_FORMAT, "QC appearance model differs from its source precache index");
        out->models[0] = resource->name;
        out->model_resources[0] = resource->source;
        if (resource->name[0] != '*' && !resource->source)
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC appearance precache has no retained model resource");
    }
    return source_colors(engine, out->colormap, out, error);
}
