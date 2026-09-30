#include "internal.h"

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
            out->effects = source.effects;
            out->alpha = source.alpha;
            out->scale = source.scale;
            out->visible = source.model != QA_STRING_NONE;
        }
        if (out->character == provider->owner &&
            qa_q1_character_read(provider->state.q1, actor, &character)) {
            out->models[0] = resource_text(application, character.model);
            out->frame = character.frame;
            out->visible = character.model != QA_STRING_NONE;
            found = true;
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
        out->visible = source.kind != QA_Q3_ENTITY_HIDDEN;
        return true;
    }
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return false;
    }
    return false;
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
            .models = {resource_text(application, object.model)}, .frame = object.frame,
            .skin = object.skin, .effects = object.effects, .visible = object.visible,
            .family = mode.rules.source <= QA_MODE_Q1_HORDE ? QA_GAME_Q1
                : mode.rules.source < QA_MODE_Q3 ? QA_GAME_Q2 : QA_GAME_Q3};
        qa_actor_collision collision;
        if (!qa_world_body_read(application->world, actor, &view.body, error) ||
            !qa_world_get_collision(application->world, actor, &collision, error))
            return false;
        view.inline_model = collision.model;
        view.has_inline_model = collision.inline_model;
        if (!qa_actors_get(qa_session_actors(application->session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Mode object retired during visual observation");
        *out = view;
        return true;
    }
    application_provider *appearance =
        application_provider_for(application, actor, QA_ROLE_APPEARANCE, "");
    application_provider *character =
        application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
    if (!appearance || !appearance->constructed || appearance->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected appearance owner is missing");
    qa_application_visual_view view = {.actor = actor, .provider = appearance->owner,
        .character = character ? character->owner : 0,
        .content = appearance->product->id,
        .character_content = character ? character->product->id : 0,
        .old_frame = -1, .alpha = 1, .scale = 1};
    if (!qa_world_body_read(application->world, actor, &view.body, error))
        return false;
    qa_error observed = {0};
    if (!native_visual(appearance, actor, &view, &observed)) {
        if (observed.code != QA_OK) {
            if (error)
                *error = observed;
        } else
            application_fail(error, QA_ERROR_NOT_FOUND, "Selected appearance has no source observation");
        return false;
    }
    qa_actor_collision collision;
    application_provider *map = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (map != NULL && map->constructed && map->kind == APPLICATION_PROVIDER_Q1)
        (void)qa_q1_game_map_effects(map->state.q1, actor, &view.q1_effects);
    if (!qa_world_get_collision(application->world, actor, &collision, error))
        return false;
    view.inline_model = collision.model;
    view.has_inline_model = collision.inline_model;
    if (!qa_actors_get(qa_session_actors(application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Visual actor retired during observation");
    *out = view;
    return true;
}
