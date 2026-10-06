#include "internal.h"
#include "guest_qc_visual.h"
#include "guest_qc_internal.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_items.h"
#include "qa/game_q1_bots.h"
#include "visual_visibility.h"
#include "native_q2_console.h"
#include "guest_native_q2_private.h"
#include "native_q3_wire.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/game_q3_source.h"
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

static bool pickup_visible(qa_application *application, qa_actor_id actor,
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

struct qa_application_visual_visibility {
    qa_application *app;
    application_provider *source;
    qa_actor_id recipient, tracked;
    qa_bytes q1_pvs;
    qa_application_native_q2_presentation q2_source;
    application_q2_visibility_recipient q2_recipient;
    uint8_t q3_visible[QA_Q3_ENTITIES / 8];
    bool q1, q2, q3, no_vis;
};

size_t qa_application_visual_visibility_bytes(const qa_application *app)
{
    size_t pvs = app && app->geometry ? qa_collision_q1_pvs_bytes(app->geometry) : 0;
    application_provider *source = app ? application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "") : NULL;
    if (source && source->product && source->product->family == QA_GAME_Q2 &&
        source->kind == APPLICATION_PROVIDER_NATIVE && app->geometry) {
        size_t pending = application_q2_visibility_pending_capacity(app);
        if (!pending) return 0;
        if (pending * sizeof(int32_t) > pvs) pvs = pending * sizeof(int32_t);
    }
    return pvs > SIZE_MAX - sizeof(qa_application_visual_visibility) ? 0 :
        sizeof(qa_application_visual_visibility) + pvs;
}

bool qa_application_visual_visibility_prepare(qa_application *app, qa_actor_id recipient,
    qa_vec3 pvs_origin, bool no_vis, void *storage, size_t capacity,
    qa_application_visual_visibility **out, qa_error *error)
{
    size_t required = qa_application_visual_visibility_bytes(app);
    if (!app || !out || !storage || (uintptr_t)storage % _Alignof(max_align_t) ||
        !required || capacity < required || !qa_vec_finite(pvs_origin) ||
        (recipient.registry && !qa_actors_get(qa_session_actors(app->session), recipient)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Visual visibility requires its actual recipient and view storage");
    qa_application_visual_visibility *v = storage;
    memset(v, 0, sizeof(*v));
    v->app = app; v->recipient = recipient; v->no_vis = no_vis;
    v->source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    application_provider *source = v->source;
    if (!source || !source->product || !app->geometry || !recipient.registry) { *out = v; return true; }
    if (source->product->family == QA_GAME_Q1 &&
        qa_collision_geometry_family(app->geometry) == QA_COLLISION_Q1) {
        size_t bytes = qa_collision_q1_pvs_bytes(app->geometry);
        uint8_t *pvs = (uint8_t *)(v + 1);
        if (!qa_collision_q1_fat_pvs(app->geometry, pvs_origin, pvs, bytes, error)) return false;
        v->q1_pvs = (qa_bytes){pvs, bytes}; v->q1 = true;
        if (source->kind == APPLICATION_PROVIDER_Q1) {
            qa_q1_source_client_view client;
            if (qa_q1_source_client_read(source->state.q1, recipient, &client) && client.observer)
                v->tracked = client.spectator_track;
        } else if (source->kind == APPLICATION_PROVIDER_QC) {
            const struct application_qc_state *engine = source->state.qc.engine;
            /* Guest QW has no retained tracked-actor observation. Preserve
             * its observer's models until that Source provenance exists. */
            if (engine && engine->profile == QA_QC_QUAKEWORLD)
                for (uint32_t i = 1; i <= engine->max_clients; ++i)
                    if (qa_actor_id_equal(engine->clients[i].actor, recipient) && engine->clients[i].spectator) {
                        v->q1 = false; break;
                    }
        }
    } else if (source->product->family == QA_GAME_Q2) {
        bool found;
        if (!qa_application_native_q2_presentation_selected(app, &v->q2_source, &found, error)) return false;
        if (found) {
            uint32_t slot = 0;
            if (v->q2_source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
                qa_q2_wire_binding binding;
                if (qa_q2_wire_actor(v->q2_source.source.game, recipient, &binding, NULL)) slot = binding.source_slot;
            } else {
                struct application_native_q2 *engine = source->state.native.q2_engine;
                for (uint32_t i = 1; i < 257; ++i)
                    if (qa_actor_id_equal(engine->clients[i].actor, recipient)) { slot = i; break; }
            }
            if (slot) {
                qa_cvars *cvars = v->q2_source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN ?
                    application_native_q2_console_registry(source) : source->state.native.q2_engine->cvars;
                const qa_cvar_view *novis = cvars ? qa_cvars_find(cvars, "sv_novis") : NULL;
                v->no_vis |= novis && novis->number != 0;
                if (!application_q2_visibility_recipient_prepare(app, recipient, slot, pvs_origin,
                    &v->q2_recipient, error)) return false;
                if (v->q2_source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
                    v->q2_recipient.pending = (int32_t *)(v + 1);
                    v->q2_recipient.pending_capacity = application_q2_visibility_pending_capacity(app);
                }
                v->q2 = true;
            }
        }
    } else if (source->kind == APPLICATION_PROVIDER_Q3) {
        uint32_t slot;
        if (qa_q3_native_client_slot(source->state.q3, recipient, &slot, NULL)) {
            qa_q3_player player;
            qa_q3_visible_entities visible;
            if (!application_native_q3_wire_current_view(source, slot, &player, &visible, error)) return false;
            for (size_t i = 0; i < visible.count; ++i) {
                uint32_t number = (uint32_t)visible.entities[i].number;
                if (number >= QA_Q3_ENTITIES)
                    return application_fail(error, QA_ERROR_FORMAT, "Visual Source selector returned an invalid physical entity");
                v->q3_visible[number / 8] |= (uint8_t)(1u << (number & 7));
            }
            v->q3 = true;
        }
    }
    *out = v;
    return true;
}

static bool q2_visual_visible(const qa_application_visual_visibility *v, qa_actor_id actor,
    const qa_application_visual_view *appearance, bool *out, qa_error *error)
{
    qa_application *app = v->app;
    bool builtin = v->q2_source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN;
    qa_application_native_q2_entity_prefix prefix;
    bool physical = false;
    uint32_t slot = 0;
    if (builtin) {
        qa_q2_wire_binding binding;
        if (qa_q2_wire_actor(v->q2_source.source.game, actor, &binding, NULL)) {
            slot = binding.source_slot; physical = true;
        }
    } else {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
        if (record->has_source && record->owner == v->q2_source.source_owner) {
            slot = record->source_slot; physical = true;
        }
    }
    application_q2_visibility_entity entity = {.actor = actor};
    qa_q2_entity state = {.number = UINT32_MAX};
    if (physical) {
        bool found;
        if (!qa_application_native_q2_presentation_entity(app, &v->q2_source, slot,
            &prefix, &found, error)) return false;
        if (!found || !qa_actor_id_equal(prefix.actor, actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Visual visibility lost its actual physical Source entity");
        state.number = slot;
        if (builtin) {
            const qa_q2_wire_source_entity *source = &prefix.source.builtin;
            entity.flags = source->server_flags;
            state.renderfx = source->visual.render_flags;
            state.modelindex = source->flare || (source->visual.render_flags & 128u) ||
                (source->has_visual && source->visual.visible && source->visual.models[0]) ? 1u : 0u;
            state.sound = source->loop_sound != 0;
            state.loop_attenuation = source->attenuation;
            state.origin[0] = source->body.origin.x;
            state.origin[1] = source->body.origin.y;
            state.origin[2] = source->body.origin.z;
            qa_linked_body linked;
            if (qa_world_linked(app->world, actor, &linked)) entity.bounds = linked.absolute_bounds;
            else entity.bounds = (qa_bounds){
                qa_vec_sub(qa_vec_add(source->body.origin, source->body.bounds.mins), qa_v3(1, 1, 1)),
                qa_vec_add(qa_vec_add(source->body.origin, source->body.bounds.maxs), qa_v3(1, 1, 1))};
        } else {
            entity.original = &prefix.source.original;
            entity.flags = entity.original->server_flags;
            entity.bounds = entity.original->absolute_bounds;
            state = entity.original->state; state.number = slot;
        }
    } else {
        qa_linked_body linked;
        if (!qa_world_linked(app->world, actor, &linked)) { *out = true; return true; }
        entity.bounds = linked.absolute_bounds;
        /* A linked foreign model has no invented Source flags or sound. */
        state.modelindex = 1;
    }
    if (appearance) {
        state.renderfx = appearance->render_flags;
        /* Source lasers and flares use modelindex 1 as a drawable marker,
         * including when their procedural presentation has no asset path. */
        state.modelindex = appearance->q2_flare.present || (appearance->render_flags & 128u) ||
            appearance->has_inline_model ||
            (appearance->models[0] && appearance->models[0][0]) ||
            (appearance->models[1] && appearance->models[1][0]) ||
            (appearance->models[2] && appearance->models[2][0]) ||
            (appearance->models[3] && appearance->models[3][0]);
        state.origin[0] = appearance->body.origin.x;
        state.origin[1] = appearance->body.origin.y;
        state.origin[2] = appearance->body.origin.z;
    } else {
        /* Callers with no sampled appearance already found a drawable
         * descriptor or selected character. Its model replaces the Source
         * model, so it cannot take the sound-only distance fallback. */
        state.modelindex = 1;
    }
    return application_q2_visibility_test(app, builtin ? NULL : v->source->state.native.q2_engine,
        v->q2_source.edition == QA_Q2_RERELEASE, builtin, &v->q2_recipient, &entity,
        &state, v->no_vis, out, error);
}

bool qa_application_visual_visibility_actor(const qa_application_visual_visibility *v,
    qa_actor_id actor, const qa_application_visual_view *appearance, bool *out, qa_error *error)
{
    if (!v || !out || !pickup_visible(v->app, actor, v->recipient, out, error)) return false;
    if (!*out) return true;
    if (v->q2) return q2_visual_visible(v, actor, appearance, out, error);
    /* A foreign beam/flare has no primary Source PHS classification. Its
     * origin or collision box cannot conservatively classify its draw. */
    if (appearance && (appearance->model_beam || appearance->q2_flare.present ||
        (appearance->family == QA_GAME_Q2 && (appearance->render_flags & 128u)))) return true;
    if (qa_actor_id_equal(actor, v->recipient) || qa_actor_id_equal(actor, v->tracked)) return true;
    if (v->no_vis) return true;
    if (v->q3) {
        uint32_t slot;
        if (qa_q3_source_actor_slot(v->source->state.q3, actor, &slot, NULL))
            *out = (v->q3_visible[slot / 8] & (1u << (slot & 7))) != 0;
        return true;
    }
    if (v->q1) {
        qa_linked_body linked;
        if (!qa_world_linked(v->app->world, actor, &linked)) return true;
        return qa_world_q1_visible(v->app->world, actor, &linked.absolute_bounds, v->q1_pvs, out, error);
    }
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
