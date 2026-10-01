#include "guest_qc_profile.h"
#include "map_players_private.h"

static bool client_binding(const struct application_qc_state *engine, uint32_t slot,
    bool canonical, int32_t *reference, qa_error *error)
{
    if (!slot || slot > engine->max_clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC output slot is outside its source reservation");
    const application_qc_client *client = engine->clients + slot;
    qa_qc_slot_binding binding;
    if (!client->connected || !client->spawned ||
        !qa_actors_get(qa_session_actors(engine->services.session), client->actor) ||
        !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
        binding.kind != QA_QC_SLOT_BORROWED || !qa_actor_id_equal(binding.actor, client->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QC output lost its admitted original client binding");
    if (canonical) {
        const struct application_player_roster *roster = engine->provider->application->players;
        bool found = false;
        for (size_t i = 0; roster && i < roster->count; ++i)
            if (qa_actor_id_equal(roster->records[i].actor, client->actor) && roster->records[i].seat == client->seat) {
                found = true; break;
            }
        if (!found)
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC output client differs from its canonical roster generation");
    }
    return !reference || qa_qc_slot_reference(engine->provider->state.qc.instance, slot, reference, error);
}

bool application_qc_output_claim_available(const struct application_qc_state *engine,
    uint8_t channels, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (channels && (!profile || channels != profile->client_output_channels))
        return application_fail(error, QA_ERROR_FORMAT, "QC output lease differs from its qualified declaration");
    for (const application_provider *owner = engine->provider->application->live_providers;
         channels && owner; owner = owner->next_live) {
        if (owner == engine->provider || owner->kind != APPLICATION_PROVIDER_QC || !owner->state.qc.engine) continue;
        if (owner->state.qc.engine->output_channels & channels)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC client output channel already has a retained source owner");
    }
    return true;
}

static bool read_outputs(struct application_qc_state *engine, uint32_t slot,
    application_client_outputs *out, qa_error *error)
{
    int32_t reference;
    if (!client_binding(engine, slot, false, &reference, error)) return false;
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    struct application_qc_state *projection = engine->provider->state.qc.engine;
    bool previous = projection->projecting;
    projection->projecting = true;
    application_client_outputs values = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < profile->client_output_count; ++i) {
        const application_qc_client_output *output = profile->client_outputs + i;
        switch (output->channel) {
        case APPLICATION_CLIENT_VIEW_OFFSET:
            values.has_view_offset = true;
            if (output->height) {
                float height;
                ok = qa_qc_entity_float(vm, reference, output->field->definition->offset, &height, error);
                if (ok) values.view_offset = qa_v3(0, 0, height);
            } else ok = qa_qc_entity_vector(vm, reference, output->field->definition->offset, &values.view_offset, error);
            if (ok && !qa_vec_finite(values.view_offset))
                ok = application_fail(error, QA_ERROR_FORMAT, "QC client view offset is nonfinite");
            break;
        case APPLICATION_CLIENT_BODY_SHAPE:
            values.has_body_bounds = true;
            ok = qa_qc_entity_vector(vm, reference, output->field->definition->offset, &values.body_bounds.mins, error) &&
                 qa_qc_entity_vector(vm, reference, output->maximum->definition->offset, &values.body_bounds.maxs, error);
            if (ok && (!qa_vec_finite(values.body_bounds.mins) || !qa_vec_finite(values.body_bounds.maxs) ||
                values.body_bounds.mins.x > values.body_bounds.maxs.x ||
                values.body_bounds.mins.y > values.body_bounds.maxs.y ||
                values.body_bounds.mins.z > values.body_bounds.maxs.z))
                ok = application_fail(error, QA_ERROR_FORMAT, "QC client body shape has invalid original bounds");
            break;
        case APPLICATION_CLIENT_MOVEMENT_MODE: case APPLICATION_CLIENT_STANCE: {
            float scalar;
            ok = qa_qc_entity_float(vm, reference, output->field->definition->offset, &scalar, error);
            double encoded = ok ? scalar : 0;
            if (ok && (!isfinite(encoded) || (output->masked &&
                (encoded < INT32_MIN || encoded > UINT32_MAX || trunc(encoded) != encoded))))
                ok = application_fail(error, QA_ERROR_FORMAT, "QC client output has an invalid original scalar word");
            if (ok && output->masked) {
                uint32_t bits = (uint32_t)(encoded < 0 ? encoded + 4294967296.0 : encoded);
                encoded = bits & output->mask;
            }
            const application_qc_client_output_value *mapping = NULL;
            for (size_t j = 0; ok && j < output->value_count; ++j)
                if (output->values[j].value == encoded) { mapping = output->values + j; break; }
            if (ok && !mapping) ok = application_fail(error, QA_ERROR_FORMAT, "QC client output has no declared source value");
            if (ok && output->channel == APPLICATION_CLIENT_MOVEMENT_MODE) {
                values.has_mode = true; values.mode = mapping->output.mode;
            } else if (ok) { values.has_stance = true; values.crouched = mapping->output.crouched; }
            break;
        }
        case APPLICATION_CLIENT_OUTPUT_COUNT:
            ok = application_fail(error, QA_ERROR_FORMAT, "QC client output channel is invalid"); break;
        }
    }
    projection->projecting = previous;
    if (ok) *out = values;
    return ok;
}

bool application_qc_admit_client_outputs(struct application_qc_state *engine,
    uint32_t slot, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile || !profile->client_output_count) return true;
    if (!client_binding(engine, slot, true, NULL, error) ||
        !application_control_output_admit(engine->provider->application, engine->clients[slot].actor,
            profile->client_output_channels, error)) return false;
    application_client_outputs values;
    if (!read_outputs(engine, slot, &values, error) ||
        !application_qc_output_claim_available(engine, profile->client_output_channels, error)) return false;
    if (engine->output_channels && engine->output_channels != profile->client_output_channels)
        return application_fail(error, QA_ERROR_FORMAT, "QC client output lease changed its channel topology");
    engine->output_channels = profile->client_output_channels;
    engine->clients[slot].outputs = values;
    engine->clients[slot].output_published = true;
    return true;
}

bool application_qc_publish_client_outputs(struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile || !profile->client_output_count || !engine->output_channels) return true;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = engine->clients + slot;
        if (!client->spawned || !client->output_published ||
            !qa_actors_get(qa_session_actors(engine->services.session), client->actor)) continue;
        application_client_outputs values;
        if (!client_binding(engine, slot, true, NULL, error) || !read_outputs(engine, slot, &values, error)) return false;
        client->outputs = values;
    }
    return true;
}

bool application_qc_output_field_owned(const struct application_qc_state *engine,
    qa_actor_id actor, const application_qc_bound_field *field)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    bool admitted = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
        if (engine->clients[slot].spawned && qa_actor_id_equal(engine->clients[slot].actor, actor)) { admitted = true; break; }
    if (!profile || !admitted) return false;
    for (size_t i = 0; i < profile->client_output_count; ++i) {
        const application_qc_client_output *output = profile->client_outputs + i;
        if ((output->channel == APPLICATION_CLIENT_BODY_SHAPE && (output->field == field || output->maximum == field)) ||
            (output->channel == APPLICATION_CLIENT_VIEW_OFFSET && !output->height && output->field == field)) return true;
    }
    return false;
}

bool application_qc_restore_client_outputs(struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!application_qc_output_claim_available(engine, engine->output_channels, error)) return false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = engine->clients + slot;
        bool expected = profile && profile->client_output_count && client->spawned;
        if (client->output_published != expected || (client->output_published && !engine->output_channels))
            return application_fail(error, QA_ERROR_FORMAT, "QC restored output membership differs from its original admission");
        if (client->output_published && !read_outputs(engine, slot, &client->outputs, error)) return false;
    }
    return true;
}

bool application_qc_capture_client_outputs(const struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!application_qc_output_claim_available(engine, engine->output_channels, error)) return false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        const application_qc_client *client = engine->clients + slot;
        bool expected = profile && profile->client_output_count && client->spawned;
        if (client->output_published != expected || (expected && !engine->output_channels))
            return application_fail(error, QA_ERROR_FORMAT, "QC output publication differs from its actual admitted client");
        if (expected && (!client_binding(engine, slot, true, NULL, error) ||
            !application_control_output_admit(engine->provider->application, client->actor, engine->output_channels, error))) return false;
    }
    return true;
}

bool application_qc_control_outputs(const qa_application *application, qa_actor_id actor,
    application_client_outputs *out, qa_error *error)
{
    if (!application || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC output read requires its application and destination");
    *out = (application_client_outputs){0};
    if (!qa_actors_get(qa_session_actors(application->session), actor)) return true;
    bool canonical = false;
    for (size_t i = 0; application->players && i < application->players->count; ++i)
        if (qa_actor_id_equal(application->players->records[i].actor, actor)) { canonical = true; break; }
    if (!canonical) return true;
    uint8_t claimed = 0;
    for (size_t i = 0; i < application->provider_count; ++i) {
        const application_provider *provider = application->providers[i];
        if (!provider->attached || !provider->constructed || provider->kind != APPLICATION_PROVIDER_QC ||
            !provider->state.qc.qualified || !provider->state.qc.engine) continue;
        const struct application_qc_state *engine = provider->state.qc.engine;
        if (!engine->output_channels) continue;
        if (claimed & engine->output_channels)
            return application_fail(error, QA_ERROR_FORMAT, "QC output read found competing channel owners");
        claimed |= engine->output_channels;
        for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
            const application_qc_client *client = engine->clients + slot;
            if (!client->output_published || !qa_actor_id_equal(client->actor, actor)) continue;
            if (!client_binding(engine, slot, true, NULL, error)) return false;
            const application_client_outputs *values = &client->outputs;
            if (values->has_view_offset) { out->has_view_offset = true; out->view_offset = values->view_offset; }
            if (values->has_mode) { out->has_mode = true; out->mode = values->mode; }
            if (values->has_stance) { out->has_stance = true; out->crouched = values->crouched; }
            if (values->has_body_bounds) { out->has_body_bounds = true; out->body_bounds = values->body_bounds; }
            break;
        }
    }
    return true;
}
