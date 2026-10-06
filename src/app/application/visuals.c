#include "internal.h"
#include "guest_qc_visual.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_items.h"
#include "qa/game_q1_bots.h"
#include <math.h>
#include <string.h>

static const char *resource_text(qa_application *application, qa_string_id id) {
    return id ? qa_strings_cstr(qa_session_strings(application->session), id) : NULL;
}

static bool native_visual(application_provider *provider, qa_actor_id actor,
                           qa_application_visual_view *out, qa_error *error) {
    qa_application *application = provider->application;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1: {
        out->family = QA_GAME_Q1;
        qa_q1_presentation source;
        qa_q1_character_view character;
        bool found = qa_q1_game_presentation(provider->state.q1, actor, &source);
        if (found) {
            out->models[0] = resource_text(application, source.model);
            out->frame = source.frame;
            out->skin = source.skin;
            out->colormap = source.color_map;
            out->effects = source.effects;
            out->alpha = source.alpha;
            out->scale = source.scale;
            out->visible = source.model != QA_STRING_NONE;
            out->has_inline_model = source.has_inline_model;
            out->inline_model = source.inline_model;
        }
        if (out->character == provider->owner &&
            qa_q1_character_read(provider->state.q1, actor, &character)) {
            out->models[0] = resource_text(application, character.model);
            out->frame = character.frame;
            out->visible = character.model != QA_STRING_NONE;
            out->has_inline_model = false;
            found = true;
        }
        qa_q1_source_client_view client;
        qa_actor_id colored;
        if (qa_q1_source_client_read(provider->state.q1, actor, &client)) {
            out->colormap = (int32_t)client.slot + 1;
            out->player_colors = (uint8_t)((client.shirt << 4) | client.pants);
            out->has_player_colors = true;
        } else if (out->colormap > 0 && qa_q1_source_client_actor(provider->state.q1,
            (uint32_t)out->colormap - 1, &colored) &&
            qa_q1_source_client_read(provider->state.q1, colored, &client)) {
            out->player_colors = (uint8_t)((client.shirt << 4) | client.pants);
            out->has_player_colors = true;
        }
        return found;
    }
    case APPLICATION_PROVIDER_Q2: {
        out->family = QA_GAME_Q2;
        qa_q2_visual source;
        if (!qa_q2_presentation_read(provider->state.q2, actor, &source))
            return false;
        for (unsigned i = 0; i < 4; ++i)
            out->models[i] = resource_text(application, source.models[i]);
        out->frame = source.frame;
        out->old_frame = source.old_frame;
        out->skin = source.skin;
        out->effects = source.effects;
        out->render_flags = source.render_flags;
        out->alpha = source.alpha;
        out->scale = source.scale;
        out->visible = source.visible;
        out->has_inline_model = source.has_inline_model;
        out->inline_model = source.inline_model;
        if (source.render_flags & (0x200000u | 128u)) {
            qa_q2_wire_binding binding;
            qa_q2_wire_source_entity entity;
            if (!qa_q2_wire_actor(provider->state.q2, actor, &binding, error) ||
                !qa_q2_wire_entity_read(provider->state.q2, binding.source_slot, &entity, error))
                return false;
            out->previous_origin = entity.previous_origin;
            out->model_beam = entity.model_beam;
            if (!(source.render_flags & 0x200000u)) return true;
            if (!entity.flare || !isfinite(entity.flare_start) || !isfinite(entity.flare_end) ||
                !isfinite(source.scale))
                return application_fail(error, QA_ERROR_FORMAT, "Q2 flare lost its authored Source fields");
            uint32_t skin = (uint32_t)source.skin, shell = source.render_flags & 0x1c00;
            const char *image = (source.render_flags & 256) ? resource_text(application, entity.flare_image) : NULL;
            out->q2_flare = (qa_application_q2_flare_view){
                .image = image && *image ? image : "misc/flare.tga",
                .fade_start = truncf(entity.flare_start), .fade_end = truncf(entity.flare_end),
                .color = skin ? qa_v3((float)(skin >> 24) / 255,
                    (float)((skin >> 16) & 255) / 255, (float)((skin >> 8) & 255) / 255) : qa_v3(1, 1, 1),
                .rim_color = qa_v3((shell & 0x400) ? 1.f : 0.f, (shell & 0x800) ? 1.f : 0.f, (shell & 0x1000) ? 1.f : 0.f),
                .present = true, .has_rim_color = shell != 0, .lock_angle = (source.render_flags & 1) != 0};
            memset(out->models, 0, sizeof(out->models));
        }
        return true;
    }
    case APPLICATION_PROVIDER_Q3: {
        out->family = QA_GAME_Q3;
        qa_q3_entity_view source;
        if (!qa_q3_entity_read(provider->state.q3, actor, &source, error))
            return false;
        out->models[0] = source.model;
        out->models[1] = source.secondary_model;
        out->legs_animation = source.legs_animation;
        out->torso_animation = source.torso_animation;
        out->source_flags = source.flags;
        out->powerups = source.powerups;
        out->alpha = source.alpha;
        out->source_entity = source.source_entity;
        out->has_source_entity = source.has_source_entity;
        out->has_inline_model = source.has_inline_model;
        out->inline_model = source.inline_model;
        out->source_number = source.has_source_entity ? source.source_number : -1;
        out->source_client = source.has_source_entity ? source.source_client : -1;
        if (source.has_source_entity) out->frame = source.source_entity.frame;
        out->visible = source.kind != QA_Q3_ENTITY_HIDDEN;
        return true;
    }
    case APPLICATION_PROVIDER_QC:
        return application_qc_visual(provider, actor, out, error);
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return false;
    }
    return false;
}

bool qa_application_visual_visible_to(qa_application *application, qa_actor_id actor,
    qa_actor_id recipient, bool *out, qa_error *error)
{
    if (!application || !out ||
        !qa_actors_get(qa_session_actors(application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Visual actor is missing");
    application_provider *body = application_provider_for(application, actor, QA_ROLE_BODY, "");
    if (!body || !body->constructed || body->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected body owner is missing");
    if (body->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_item_visible_to(body->state.q2, actor, recipient, out, error);
    *out = true;
    return true;
}

bool qa_application_visual_read(qa_application *application, qa_actor_id actor,
                                  qa_application_visual_view *out, qa_error *error) {
    if (!application || !out ||
        !qa_actors_get(qa_session_actors(application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Visual actor is missing");
    qa_mode_object_view object;
    if (application->modes && qa_modes_object_read(application->modes, actor, &object)) {
        application_provider *source = application_mode_provider(application, object.mode);
        qa_mode_view mode;
        if (!source || !qa_modes_read(application->modes, object.mode, &mode, error))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Mode object has no live source content owner");
        qa_application_visual_view view = {.actor = actor, .provider = source->owner,
            .content = source->product->id, .old_frame = -1, .alpha = 1, .scale = 1,
            .source_number = -1, .source_client = -1,
            .models = {resource_text(application, object.model)}, .frame = object.frame,
            .skin = object.skin, .effects = object.effects, .visible = object.visible,
            .family = mode.rules.source <= QA_MODE_Q1_HORDE ? QA_GAME_Q1
                : mode.rules.source < QA_MODE_Q3 ? QA_GAME_Q2 : QA_GAME_Q3};
        if (!qa_world_body_read(application->world, actor, &view.body, error))
            return false;
        view.previous_origin = view.body.origin;
        if (!qa_actors_get(qa_session_actors(application->session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Mode object retired during visual observation");
        *out = view;
        return true;
    }
    application_provider *body =
        application_provider_for(application, actor, QA_ROLE_BODY, "");
    application_provider *character =
        application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
    if (!body || !body->constructed || body->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected body owner is missing");
    qa_application_visual_view view = {.actor = actor, .provider = body->owner,
        .character = character ? character->owner : 0,
        .content = body->product->id,
        .character_content = character ? character->product->id : 0,
        .old_frame = -1, .alpha = 1, .scale = 1, .source_number = -1, .source_client = -1};
    if (!qa_world_body_read(application->world, actor, &view.body, error))
        return false;
    view.previous_origin = view.body.origin;
    qa_error observed = {0};
    if (!native_visual(body, actor, &view, &observed)) {
        if (observed.code != QA_OK) {
            if (error)
                *error = observed;
        } else
            application_fail(error, QA_ERROR_NOT_FOUND, "Selected body has no source observation");
        return false;
    }
    application_provider *map = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (map != NULL && map->constructed && map->kind == APPLICATION_PROVIDER_Q1)
        (void)qa_q1_game_map_effects(map->state.q1, actor, &view.q1_effects);
    if (!qa_actors_get(qa_session_actors(application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Visual actor retired during observation");
    *out = view;
    return true;
}
