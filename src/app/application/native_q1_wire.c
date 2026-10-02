#include "native_q1_wire.h"
#include "native_q1_console.h"
#include "map_players_private.h"
#include "qa/game_q1_bots.h"
#include "qa/network_q1_nq.h"
#include "qa/q1_text.h"
#include "qa/text.h"
#include "qa/localization.h"
#include "qa/application_equipment.h"
#include "qa/application_language.h"
#include "qa/ui_language.h"
#include "qa/caption_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <string.h>

typedef struct native_q1_wire_language {
    qa_actor_id actor;
    char *language;
    qa_localization *catalog;
    struct native_q1_wire_language *next;
} native_q1_wire_language;
typedef struct application_native_q1_wire {
    qa_launch_instance_lease *source;
    qa_localization_pool *catalogs;
    native_q1_wire_language *languages;
    qa_resource **models, **sounds;
    size_t model_count, sound_count;
    size_t language_admissions, readers;
    uint64_t generation;
} application_native_q1_wire;
typedef struct native_q1_wire_language_pending {
    application_native_q1_wire *owner;
    application_provider *provider;
    qa_q1_game_operation operation;
    native_q1_wire_language *row;
    struct native_q1_wire_language_pending *next;
} native_q1_wire_language_pending;
struct application_native_q1_wire_language_ticket {
    native_q1_wire_language_pending *pending;
    char *language;
    qa_application *application;
    application_provider *primary;
    qa_actor_id actor;
    uint32_t slot;
};
struct qa_application_language_ticket {
    application_native_q1_wire_language_ticket *native;
};
static const application_player_record *roster(qa_application *, qa_actor_id);
static void assets_release(qa_resource **rows, size_t count) {
    for (size_t i = 0; i < count; ++i) qa_resource_release(rows[i]);
    free(rows);
}
bool application_native_q1_wire_idle(const application_provider *p) {
    return !p || !p->native_q1_wire ||
        (!p->native_q1_wire->language_admissions && !p->native_q1_wire->readers);
}
bool application_native_q1_wire_language_idle(const application_provider *p,
    const qa_application_language_ticket *const *owned, size_t count) {
    const application_native_q1_wire *owner = p ? p->native_q1_wire : NULL;
    if (!owner) return true;
    if (owner->readers || (count && !owned)) return false;
    size_t admissions = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!owned[i]) continue;
        const application_native_q1_wire_language_ticket *ticket = owned[i]->native;
        if (!ticket) return false;
        for (size_t j = 0; j < i; ++j)
            if (owned[j] && (owned[j] == owned[i] || owned[j]->native == ticket)) return false;
        for (const native_q1_wire_language_pending *pending = ticket->pending;
             pending; pending = pending->next) {
            if (pending->provider != p) {
                if (pending->owner == owner) return false;
                continue;
            }
            if (p->kind != APPLICATION_PROVIDER_Q1 || pending->owner != owner ||
                pending->operation.game != p->state.q1 || pending->operation.retained_owner ||
                !qa_q1_game_operation_live(&pending->operation) || !pending->row ||
                !pending->row->catalog || admissions == owner->language_admissions) return false;
            ++admissions;
        }
    }
    return admissions == owner->language_admissions;
}
void application_native_q1_wire_destroy(application_provider *p) {
    application_native_q1_wire *owner = p ? p->native_q1_wire : NULL;
    if (!owner) return;
    p->native_q1_wire = NULL;
    while (owner->languages) {
        native_q1_wire_language *next = owner->languages->next;
        qa_localization_release(owner->languages->catalog);
        free(owner->languages->language); free(owner->languages); owner->languages = next;
    }
    assets_release(owner->models, owner->model_count); assets_release(owner->sounds, owner->sound_count);
    qa_localization_pool_destroy(owner->catalogs);
    qa_launch_instance_lease_release(owner->source);
    free(owner);
}
bool application_native_q1_wire_create(application_provider *p, qa_error *error) {
    if (!p || !p->launch || !p->launch->content || p->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 wire resource owner lost its genuine content source");
    if (p->native_q1_wire) return true;
    application_native_q1_wire *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q1 wire resources");
    p->native_q1_wire = owner;
    if (!qa_launch_instance_retain_metadata(p->launch, &owner->source, error) ||
        !(owner->catalogs = qa_localization_pool_create(error))) {
        application_native_q1_wire_destroy(p); return false;
    }
    return true;
}
bool application_native_q1_wire_resources_prepare(application_provider *p, qa_error *error) {
    if (!p || !qa_q1_wire_enabled(p->state.q1)) return true;
    application_native_q1_wire *owner = p->native_q1_wire;
    const qa_launch_instance *source = owner ? qa_launch_instance_lease_view(owner->source) : NULL;
    qa_q1_wire_receipt receipt = {0};
    if (!source || !source->content || !qa_q1_wire_read_begin(p->state.q1, &receipt, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 wire resource admission lost its held source");
    if (owner->readers == SIZE_MAX) {
        qa_q1_wire_read_end(&receipt);
        return application_fail(error, QA_ERROR_MEMORY, "Q1 source resource admission extent exhausted");
    }
    ++owner->readers;
    qa_resource **models = NULL, **sounds = NULL;
    bool okay = receipt.model_count <= SIZE_MAX / sizeof(*models) && receipt.sound_count <= SIZE_MAX / sizeof(*sounds);
    if (okay) { models = calloc(receipt.model_count, sizeof(*models)); sounds = calloc(receipt.sound_count, sizeof(*sounds)); okay = models && sounds; }
    if (!okay) application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 ordered resource receipts");
    qa_strings *strings = qa_session_strings(p->application->session);
    for (size_t i = 1; okay && i < receipt.model_count; ++i) {
        const char *path = qa_strings_cstr(strings, receipt.models[i]);
        if (!path) { okay = application_fail(error, QA_ERROR_FORMAT, "Q1 model declaration lost its source path"); break; }
        if (*path != '*') okay = qa_vfs_acquire(source->content, path, &models[i], NULL, error);
    }
    for (size_t i = 1; okay && i < receipt.sound_count; ++i) {
        const char *path = qa_strings_cstr(strings, receipt.sounds[i]);
        size_t length = path ? strlen(path) : SIZE_MAX;
        if (length > SIZE_MAX - 7) { okay = application_fail(error, QA_ERROR_FORMAT, "Q1 sound declaration lost its source path"); break; }
        char *full = malloc(length + 7);
        if (!full) { okay = application_fail(error, QA_ERROR_MEMORY, "Admitting Q1 source sound path"); break; }
        memcpy(full, "sound/", 6); memcpy(full + 6, path, length + 1);
        okay = qa_vfs_acquire(source->content, full, &sounds[i], NULL, error); free(full);
        if (okay) {
            char id[81];
            okay = application_unified_event_resource_register(p->application, p->owner,
                path, sounds[i], id, error);
        }
    }
    if (okay && (p->close_pending || p->native_q1_wire != owner ||
        !qa_q1_wire_receipt_current(&receipt)))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 source changed during resource admission");
    if (okay) {
        assets_release(owner->models, owner->model_count); assets_release(owner->sounds, owner->sound_count);
        owner->models = models; owner->sounds = sounds;
        owner->model_count = receipt.model_count; owner->sound_count = receipt.sound_count;
        owner->generation = receipt.generation;
    } else { assets_release(models, models ? receipt.model_count : 0); assets_release(sounds, sounds ? receipt.sound_count : 0); }
    --owner->readers;
    qa_q1_wire_read_end(&receipt);
    return okay;
}
bool application_native_q1_wire_sound_resource(qa_application *app, qa_actor_owner source,
    qa_string_id path, qa_resource **out, const qa_product **product, bool *found,
    qa_error *error)
{
    if (!out || !product || !found || !app || !app->session || !source || !path)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 sound registration read has no actual source");
    *out = NULL; *product = NULL; *found = false;
    application_provider *p = NULL;
    for (application_provider *actual = app->live_providers; actual; actual = actual->next_live)
        if (actual->owner == source) { p = actual; break; }
    if (!p || p->close_pending || !p->product || p->product->family != QA_GAME_Q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 sound source retired before its registration read");
    if (p->kind != APPLICATION_PROVIDER_Q1 || !p->state.q1) return true;
    application_native_q1_wire *owner = p->native_q1_wire;
    if (!owner || !owner->sound_count || !qa_q1_wire_enabled(p->state.q1)) return true;
    uint64_t generation;
    bool loading;
    if (!qa_q1_wire_registration_state(p->state.q1, &generation, &loading) ||
        loading || owner->generation != generation) return true;
    qa_q1_wire_receipt receipt = {0};
    if (!qa_q1_wire_read_begin(p->state.q1, &receipt, error)) return false;
    uint32_t index;
    bool okay = owner->generation == receipt.generation &&
        owner->sound_count == receipt.sound_count && p->native_q1_wire == owner &&
        receipt.owner == p->owner;
    if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 sound registration differs from its current declaration generation");
    else if (qa_q1_wire_index(&receipt, false, path, &index) && index &&
        index < owner->sound_count && owner->sounds[index]) {
        const qa_launch_instance *descriptor = qa_launch_instance_lease_view(owner->source);
        const qa_product *held = descriptor ? qa_catalog_product(
            qa_launch_instance_catalog(descriptor), descriptor->selection.product) : NULL;
        if (!held || held != p->product || !qa_q1_wire_receipt_current(&receipt))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 sound registration lost its held content product");
        else {
            *out = owner->sounds[index]; qa_resource_retain(*out);
            *product = held; *found = true;
        }
    }
    qa_q1_wire_read_end(&receipt);
    return okay;
}

static bool language_prepare(application_provider *p, qa_actor_id actor,
    const char *language, application_native_q1_wire_language_ticket *ticket, qa_error *error) {
    application_native_q1_wire *owner = p->native_q1_wire;
    const qa_launch_instance *source = owner ? qa_launch_instance_lease_view(owner->source) : NULL;
    if (!source || !source->content) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 text has no held content owner");
    if (owner->language_admissions == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Q1 catalog admission extent exhausted");
    native_q1_wire_language_pending *pending = calloc(1, sizeof(*pending));
    native_q1_wire_language *fresh = calloc(1, sizeof(*fresh));
    size_t length = strlen(language) + 1;
    char *copy = malloc(length);
    if (!pending || !fresh || !copy) {
        free(pending); free(fresh); free(copy);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 recipient catalog");
    }
    memcpy(copy, language, length);
    if (!qa_q1_game_operation_begin(p->state.q1, &pending->operation, error)) {
        free(pending); free(fresh); free(copy); return false;
    }
    ++owner->language_admissions;
    qa_localization *catalog = NULL;
    native_q1_wire_language *row = owner->languages;
    while (row && !qa_actor_id_equal(row->actor, actor)) row = row->next;
    if (row && !strcmp(row->language, copy)) {
        catalog = row->catalog;
        qa_localization_retain(catalog);
    } else if (!qa_localization_acquire(owner->catalogs, source->content, copy,
            &(qa_localization_options){.profile = QA_LOCALIZATION_Q1_RERELEASE}, &catalog, error)) {
        --owner->language_admissions;
        qa_q1_game_operation_end(&pending->operation);
        free(pending); free(fresh); free(copy); return false;
    }
    *fresh = (native_q1_wire_language){.actor = actor, .language = copy, .catalog = catalog};
    pending->owner = owner; pending->provider = p; pending->row = fresh;
    pending->next = ticket->pending; ticket->pending = pending;
    return true;
}
bool application_native_q1_wire_language_prepare(qa_application *app, qa_actor_id actor,
    const char *language, application_native_q1_wire_language_ticket **out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 language admission ticket");
    *out = NULL;
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!primary || primary->kind != APPLICATION_PROVIDER_Q1 || !qa_q1_wire_enabled(primary->state.q1)) return true;
    uint32_t slot;
    if (!language || !*language || !qa_q1_native_client_slot(primary->state.q1, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 text admission requires its actual recipient language");
    application_native_q1_wire_language_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return application_fail(error, QA_ERROR_MEMORY, "Preparing Q1 recipient catalogs");
    size_t language_size = strlen(language) + 1;
    ticket->language = malloc(language_size);
    if (!ticket->language) { free(ticket); return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 language declaration"); }
    memcpy(ticket->language, language, language_size);
    ticket->application=app; ticket->primary=primary; ticket->actor=actor; ticket->slot=slot;
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p->kind == APPLICATION_PROVIDER_Q1 && p->constructed && p->attached && !p->close_pending &&
            !language_prepare(p, actor, ticket->language, ticket, error)) {
            application_native_q1_wire_language_abort(ticket); return false;
        }
    }
    uint32_t current_slot;
    if (app->destroy_requested || primary->close_pending ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != primary ||
        !qa_q1_native_client_slot(primary->state.q1, actor, &current_slot, error) || current_slot != slot) {
        application_native_q1_wire_language_abort(ticket);
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 language recipient changed during catalog admission");
    }
    for (native_q1_wire_language_pending *pending = ticket->pending; pending; pending = pending->next)
        if (!qa_q1_game_operation_live(&pending->operation) || pending->provider->close_pending ||
            !pending->provider->constructed || !pending->provider->attached) {
            application_native_q1_wire_language_abort(ticket);
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 language source changed during catalog admission");
        }
    *out = ticket;
    return true;
}
void application_native_q1_wire_language_abort(application_native_q1_wire_language_ticket *ticket) {
    if (!ticket) return;
    while (ticket->pending) {
        native_q1_wire_language_pending *pending = ticket->pending;
        ticket->pending = pending->next;
        qa_localization_release(pending->row->catalog);
        free(pending->row->language); free(pending->row);
        --pending->owner->language_admissions;
        qa_q1_game_operation_end(&pending->operation); free(pending);
    }
    free(ticket->language); free(ticket);
}
void application_native_q1_wire_language_commit(application_native_q1_wire_language_ticket *ticket) {
    if (!ticket) return;
    for (native_q1_wire_language_pending *pending = ticket->pending; pending; pending = pending->next) {
        if (!qa_q1_game_operation_live(&pending->operation) || pending->provider->close_pending ||
            pending->provider->application->destroy_requested ||
            !qa_actors_get(qa_session_actors(pending->provider->application->session), pending->row->actor)) continue;
        native_q1_wire_language **position = &pending->owner->languages;
        while (*position && !qa_actor_id_equal((*position)->actor, pending->row->actor)) position = &(*position)->next;
        native_q1_wire_language *old = *position;
        pending->row->next = old ? old->next : NULL;
        *position = pending->row;
        pending->row = old;
    }
    /* Release replaced rows and declarations whose full actor retired. */
    while (ticket->pending) {
        native_q1_wire_language_pending *pending = ticket->pending;
        ticket->pending = pending->next;
        if (pending->row) {
            qa_localization_release(pending->row->catalog);
            free(pending->row->language); free(pending->row);
        }
        --pending->owner->language_admissions;
        qa_q1_game_operation_end(&pending->operation); free(pending);
    }
    free(ticket->language); free(ticket);
}
bool qa_application_language_prepare(qa_application *app, qa_actor_id actor,
    const char *language, qa_application_language_ticket **out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing source language admission ticket");
    *out = NULL;
    if (!app || !app->session || !language || !*language ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source language admission requires its actual actor and language");
    qa_application_language_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return application_fail(error, QA_ERROR_MEMORY, "Retaining source language admission");
    if (!application_native_q1_wire_language_prepare(app, actor, language, &ticket->native, error)) {
        free(ticket); return false;
    }
    if (ticket->native) *out = ticket;
    else free(ticket);
    return true;
}
void qa_application_language_commit(qa_application_language_ticket *ticket) {
    if (!ticket) return;
    application_native_q1_wire_language_commit(ticket->native); free(ticket);
}
bool qa_application_language_ready_is(const qa_application_language_ticket *held,
    const qa_application *app,qa_actor_id actor,const char *language)
{
    const application_native_q1_wire_language_ticket *ticket=held?held->native:NULL;
    if (!ticket || !app || ticket->application!=app || app->destroy_requested || !app->session ||
        !language || !ticket->language || strcmp(ticket->language,language) ||
        !qa_actor_id_equal(ticket->actor,actor) || !qa_actors_get(qa_session_actors(app->session),actor) ||
        !ticket->pending || application_world_provider(ticket->application,QA_ROLE_ENTITIES,"")!=ticket->primary)
        return false;
    uint32_t slot;
    if (!ticket->primary || ticket->primary->kind!=APPLICATION_PROVIDER_Q1 ||
        !qa_q1_native_client_slot(ticket->primary->state.q1,actor,&slot,NULL) || slot!=ticket->slot)
        return false;
    size_t expected=0,actual=0;
    for (size_t i=0;i<app->provider_count;++i) {
        const application_provider *p=app->providers[i];
        if (p->kind!=APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached || p->close_pending) continue;
        ++expected;
        size_t matches=0;
        for (const native_q1_wire_language_pending *row=ticket->pending;row;row=row->next)
            if (row->provider==p) ++matches;
        if (matches!=1) return false;
    }
    for (const native_q1_wire_language_pending *row=ticket->pending;row;row=row->next) {
        const application_provider *p=row->provider;
        bool present=false;
        for (size_t i=0;i<app->provider_count;++i) present=present || app->providers[i]==p;
        uint64_t catalog_key=0;
        const qa_launch_instance *source=row->owner?qa_launch_instance_lease_view(row->owner->source):NULL;
        if (!present || !p || p->application!=app || p->kind!=APPLICATION_PROVIDER_Q1 ||
            !p->constructed || !p->attached || p->close_pending || p->native_q1_wire!=row->owner ||
            !row->owner || row->owner->readers || !row->owner->language_admissions ||
            !source || !source->content || !row->row || !row->row->catalog || !row->row->language ||
            !qa_actor_id_equal(row->row->actor,actor) || strcmp(row->row->language,language) ||
            row->operation.game!=p->state.q1 || row->operation.retained_owner ||
            !qa_q1_game_operation_live(&row->operation) ||
            !qa_localization_pool_catalog_key(row->owner->catalogs,row->row->catalog,&catalog_key) || !catalog_key)
            return false;
        ++actual;
    }
    return actual==expected;
}
void qa_application_language_abort(qa_application_language_ticket *ticket) {
    if (!ticket) return;
    application_native_q1_wire_language_abort(ticket->native); free(ticket);
}
void application_native_q1_wire_actor_released(qa_application *app, qa_actor_id actor) {
    for (application_provider *p = app ? app->live_providers : NULL; p; p = p->next_live) {
        application_native_q1_wire *owner = p->native_q1_wire;
        if (!owner) continue;
        native_q1_wire_language **position = &owner->languages;
        while (*position) {
            native_q1_wire_language *row = *position;
            if (!qa_actor_id_equal(row->actor, actor)) { position = &row->next; continue; }
            *position = row->next;
            qa_localization_release(row->catalog); free(row->language); free(row);
        }
    }
}

static qa_net_protocol_id protocol(void) { return (qa_net_protocol_id){.kind = QA_NET_NQ15}; }
static const char *text(qa_application *app, qa_string_id id) {
    return id ? qa_strings_cstr(qa_session_strings(app->session), id) : "";
}
bool application_native_q1_wire_begin(qa_application *app, qa_actor_owner owner,
    application_native_q1_wire_source *out, qa_error *error) {
    application_provider *p = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!out || !app || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !p || p->kind != APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached ||
        p->close_pending || !p->launch || p->launch->selection.clock.kind != QA_CLOCK_NETQUAKE ||
        (owner && p->owner != owner) || !qa_session_safe(app->session) ||
        qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        !application_native_q1_console_idle(p))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Original native Q1 wire requires its installed primary source owner");
    qa_q1_wire_receipt receipt = {0};
    if (!qa_q1_wire_read_begin(p->state.q1, &receipt, error)) return false;
    if (receipt.owner != p->owner || !receipt.client_slots || receipt.client_slots > 255 ||
        receipt.entity_slots <= receipt.client_slots || receipt.entity_slots > UINT16_MAX + 1u ||
        receipt.model_count > 256 || receipt.sound_count > 256) {
        qa_q1_wire_read_end(&receipt);
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Native Q1 source exceeds the original protocol extent");
    }
    if (!p->native_q1_wire || p->native_q1_wire->generation != receipt.generation ||
        p->native_q1_wire->model_count != receipt.model_count ||
        p->native_q1_wire->sound_count != receipt.sound_count) {
        qa_q1_wire_read_end(&receipt);
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q1 wire resources are not admitted for this source generation");
    }
    if (p->native_q1_wire->readers == SIZE_MAX) {
        qa_q1_wire_read_end(&receipt);
        return application_fail(error, QA_ERROR_MEMORY, "Q1 source reader extent exhausted");
    }
    ++p->native_q1_wire->readers;
    *out = (application_native_q1_wire_source){.provider = p, .receipt = receipt};
    return true;
}
void application_native_q1_wire_end(application_native_q1_wire_source *source) {
    if (!source) return;
    if (source->receipt.operation.game && source->provider)
        --source->provider->native_q1_wire->readers;
    qa_q1_wire_read_end(&source->receipt);
    *source = (application_native_q1_wire_source){0};
}
static bool source_retain(application_provider *p, application_native_q1_wire_source *out,
    qa_error *error) {
    if (!p->native_q1_wire || p->native_q1_wire->readers == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source callback lost its held application owner");
    qa_q1_wire_receipt receipt = {0};
    if (!qa_q1_wire_read_begin(p->state.q1, &receipt, error)) return false;
    ++p->native_q1_wire->readers;
    *out = (application_native_q1_wire_source){.provider = p, .receipt = receipt};
    return true;
}
static bool client(application_native_q1_wire_source *source, qa_actor_id actor,
    uint32_t *slot, qa_error *error) {
    uint32_t physical;
    if (!qa_q1_native_client_slot(source->provider->state.q1, actor, &physical, error) ||
        physical >= source->receipt.client_slots)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 recipient has no physical source client row");
    *slot = physical + 1;
    return true;
}
static application_control_record *control(qa_application *app, qa_actor_id actor) {
    if (actor.slot >= app->control_capacity) return NULL;
    application_control_record *row = &app->controls[actor.slot];
    return row->active && !row->retired && !row->moving &&
        qa_actor_id_equal(row->actor, actor) && row->state.kind == QA_MOVEMENT_NETQUAKE ? row : NULL;
}
bool application_native_q1_wire_host(qa_application *app, qa_application_network_q1_host *out,
    qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 host observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    *out = (qa_application_network_q1_host){.owner = source.receipt.owner, .protocol = protocol(),
        .client_slots = source.receipt.client_slots, .entity_slots = source.receipt.entity_slots,
        .source_board_events = true};
    application_native_q1_wire_end(&source);
    return true;
}
bool application_native_q1_wire_source_player(qa_application *app, qa_actor_id actor,
    qa_actor_owner *owner, uint32_t *slot, qa_net_protocol_id *profile, qa_error *error) {
    if (!owner || !slot || !profile) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 source observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t physical;
    bool okay = client(&source, actor, &physical, error);
    if (okay) { *owner = source.receipt.owner; *slot = physical; *profile = protocol(); }
    application_native_q1_wire_end(&source);
    return okay;
}
bool application_native_q1_wire_extents(qa_application *app, qa_actor_owner owner,
    uint32_t *clients, uint32_t *entities, qa_error *error) {
    if (!clients || !entities) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 extent observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, owner, &source, error)) return false;
    *clients = source.receipt.client_slots; *entities = source.receipt.entity_slots;
    application_native_q1_wire_end(&source); return true;
}
qa_cvars *application_native_q1_wire_cvars(qa_application *app, qa_actor_owner owner, qa_error *error) {
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, owner, &source, error)) return NULL;
    qa_cvars *cvars = application_native_q1_console_registry(source.provider);
    application_native_q1_wire_end(&source);
    if (!cvars) application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 source cvars are absent");
    return cvars;
}
static void vector(float out[3], qa_vec3 value) { out[0] = value.x; out[1] = value.y; out[2] = value.z; }
static bool entity_read(qa_application *app, application_native_q1_wire_source *source,
    qa_actor_id actor, bool baseline, qa_q1_entity *out, qa_error *error) {
    uint32_t slot;
    if (!qa_q1_wire_actor_slot(&source->receipt, actor, &slot) || !slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 entity has no physical source edict");
    qa_application_visual_view visual;
    if (!qa_application_visual_read(app, actor, &visual, error)) return false;
    qa_string_id resource = visual.models[0] ? qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)visual.models[0], strlen(visual.models[0])}) : 0;
    uint32_t model;
    if (!qa_q1_wire_index(&source->receipt, true, resource, &model))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q1 entity model was not precached by the source");
    qa_q1_entity value; qa_q1_entity_init(&value);
    uint32_t frame = (uint32_t)visual.frame;
    value.number = slot; value.model = model;
    value.frame = baseline && (frame & UINT32_C(0xff00)) ? 0 : (uint8_t)frame;
    value.skin = (uint8_t)visual.skin; value.effects = (uint8_t)visual.effects;
    uint32_t client_slot;
    bool player = qa_q1_native_client_slot(source->provider->state.q1, actor, &client_slot, NULL);
    value.colormap = player ? slot : 0;
    qa_physics_properties physics;
    value.step = !player && qa_q1_game_physics_read(source->provider->state.q1, actor, &physics) &&
                 physics.motion == QA_PHYSICS_STEP;
    for (size_t i = 0; i < app->event_count; ++i)
        if (app->events[i].event.family == QA_GAME_Q1 && app->events[i].event.kind == QA_BUILTIN_MUZZLE &&
            qa_actor_id_equal(app->events[i].event.actor, actor)) value.effects |= 2;
    vector(value.origin, visual.body.origin); vector(value.angles, visual.body.angles);
    uint32_t current_slot;
    if (application_world_provider(app, QA_ROLE_ENTITIES, "") != source->provider ||
        !qa_q1_wire_actor_slot(&source->receipt, actor, &current_slot) || current_slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 entity changed during visual observation");
    *out = value; return true;
}
bool application_native_q1_wire_entity(qa_application *app, qa_actor_id recipient,
    qa_actor_id actor, qa_q1_entity *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 entity observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot;
    bool okay = client(&source, recipient, &slot, error) && entity_read(app, &source, actor, false, out, error);
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_precache(qa_application *app, qa_actor_owner owner, bool models,
    const char *names[255], size_t *count, qa_error *error) {
    if (!names || !count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 precache inventory");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, owner, &source, error)) return false;
    const qa_string_id *rows = models ? source.receipt.models : source.receipt.sounds;
    size_t length = models ? source.receipt.model_count : source.receipt.sound_count;
    bool okay = true;
    for (size_t i = 1; i < length; ++i) {
        names[i - 1] = text(app, rows[i]);
        if (!names[i - 1]) { okay = application_fail(error, QA_ERROR_FORMAT, "Native Q1 declaration lost its source string"); break; }
    }
    if (okay) *count = length - 1;
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_world(qa_application *app, qa_actor_owner owner,
    qa_application_network_q1_world *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 world observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, owner, &source, error)) return false;
    qa_q1_wire_world world;
    bool okay = qa_q1_wire_world_read(&source.receipt, &world);
    if (okay) {
        qa_application_network_q1_world value = {.protocol = protocol(),
            .max_clients = source.receipt.client_slots, .standard_quake = true,
            .deathmatch = source.receipt.deathmatch != 0, .seconds = (float)source.receipt.seconds,
            .map = text(app, world.map), .level = text(app, world.level),
            .total_secrets = (int32_t)world.total_secrets, .found_secrets = (int32_t)world.found_secrets,
            .total_monsters = (int32_t)world.total_monsters, .killed_monsters = (int32_t)world.killed_monsters};
        for (size_t i = 0; i < 64; ++i) value.lightstyles[i] = text(app, world.lightstyles[i]);
        *out = value;
    } else application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 world source fields are absent");
    application_native_q1_wire_end(&source); return okay;
}
static uint32_t counter(double value) {
    return value <= 0 ? 0 : value >= UINT32_MAX ? UINT32_MAX : (uint32_t)trunc(value);
}
static uint8_t source_byte(double value) {
    return (uint8_t)(uint32_t)(fmod(trunc(value), 256.0) + 256.0);
}
static int16_t source_short(double value) {
    uint16_t bits = (uint16_t)(uint32_t)(fmod(trunc(value), 65536.0) + 65536.0);
    int16_t result; memcpy(&result, &bits, sizeof(result)); return result;
}
static int32_t source_integer(double value) {
    double wrapped = fmod(trunc(value), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    uint32_t bits = (uint32_t)wrapped;
    int32_t result; memcpy(&result, &bits, sizeof(result)); return result;
}
static float particle_direction(float value) {
    double scaled = trunc((double)value * 16);
    return (float)(fmax(-128, fmin(127, scaled)) / 16);
}
bool application_native_q1_wire_clientdata(qa_application *app, qa_actor_id actor,
    qa_q1_clientdata *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 clientdata observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    application_control_record *row = NULL;
    qa_q1_wire_player player; qa_combat_state combat; qa_q1_wire_world world;
    qa_application_equipment_view equipment;
    uint32_t slot, model;
    bool okay = client(&source, actor, &slot, error) &&
        qa_q1_wire_player_read(&source.receipt, actor, &player, error) &&
        qa_application_equipment_read(app, actor, &equipment, error) &&
        qa_combat_read(app->combat, actor, &combat, error) &&
        qa_q1_wire_world_read(&source.receipt, &world) &&
        qa_q1_wire_index(&source.receipt, true, player.weapon_model, &model) &&
        qa_application_equipment_current(app, &equipment) &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == source.provider &&
        client(&source, actor, &slot, error) && (row = control(app, actor)) != NULL;
    if (okay) {
        const qa_nq_movement_state *movement = &row->state.data.nq;
        qa_q1_clientdata value = {.viewheight = row->view_height, .idealpitch = movement->ideal_pitch,
            .items = player.weapons | player.powers | (world.server_flags << 28),
            .onground = (movement->flags & 512) != 0, .inwater = movement->water_level >= 2,
            .weapon_frame = source_byte(player.weapon_frame), .weapon_model = model,
            .armor = source_byte(combat.armor.regular.kind == QA_ARMOR_NONE ? 0 : combat.armor.regular.points),
            .health = source_short(combat.health), .ammo = source_byte(equipment.ammo ? equipment.ammo_count : 0),
            .shells = source_byte(player.shells), .nails = source_byte(player.nails),
            .rockets = source_byte(player.rockets), .cells = source_byte(player.cells), .weapon = source_byte(player.weapon)};
        if (combat.armor.regular.kind != QA_ARMOR_NONE)
            value.items |= combat.armor.regular.kind == QA_ARMOR_Q1 && combat.armor.regular.protection.q1_absorption >= .8f ? 32768 :
                combat.armor.regular.kind == QA_ARMOR_Q1 && combat.armor.regular.protection.q1_absorption >= .6f ? 16384 : 8192;
        vector(value.punch, movement->punch_angles); vector(value.velocity, movement->velocity);
        *out = value;
    } else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q1 clientdata lacks its selected source movement or arsenal");
    application_native_q1_wire_end(&source); return okay;
}
static const application_player_record *roster(qa_application *app, qa_actor_id actor) {
    for (size_t i = 0; app->players && i < app->players->count; ++i) {
        const application_player_record *row = &app->players->records[i];
        if (!row->retiring && qa_actor_id_equal(row->actor, actor)) return row;
    }
    return NULL;
}
bool application_native_q1_wire_status(qa_application *app, qa_actor_owner owner,
    qa_application_network_q1_status_player players[255], size_t *out_count, qa_error *error) {
    if (!players || !out_count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 client inventory");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, owner, &source, error)) return false;
    size_t count = 0; bool okay = true;
    for (uint32_t slot = 0; slot < source.receipt.client_slots; ++slot) {
        qa_actor_id actor; qa_q1_source_client_view view;
        if (!qa_q1_source_client_actor(source.provider->state.q1, slot, &actor)) continue;
        const application_player_record *row = roster(app, actor);
        if (!row || !qa_q1_source_client_read(source.provider->state.q1, actor, &view) ||
            view.slot != slot || !isfinite(view.frags)) {
            okay = application_fail(error, QA_ERROR_FORMAT, "Native Q1 source client lost its actual roster binding"); break;
        }
        float score = view.frags;
        int32_t frags = source_integer(score);
        players[count++] = (qa_application_network_q1_status_player){.actor = actor,
            .source_slot = slot + 1, .name = view.name, .source_frags = score, .frags = frags,
            .colors = (uint8_t)((view.shirt << 4) | view.pants),
            .spawned = !row->deferred && !row->source_begin_pending};
    }
    if (okay) *out_count = count;
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_feedback(qa_application *app, qa_actor_id actor,
    qa_application_network_q1_feedback *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 feedback consumption");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; application_control_record *row = control(app, actor);
    const application_player_record *connection = roster(app, actor);
    qa_q1_wire_feedback feedback;
    bool okay = client(&source, actor, &slot, error) && row && connection &&
        !connection->deferred && !connection->source_begin_pending &&
        qa_q1_wire_feedback_consume(&source.receipt, actor, &feedback);
    if (okay) {
        connection = roster(app, actor);
        row = control(app, actor);
        okay = qa_q1_wire_receipt_current(&source.receipt) &&
            application_world_provider(app, QA_ROLE_ENTITIES, "") == source.provider &&
            client(&source, actor, &slot, error) && row && connection &&
            !connection->deferred && !connection->source_begin_pending;
    }
    if (okay) {
        qa_application_network_q1_feedback value = {.damage = feedback.armor != 0 || feedback.blood != 0,
            .armor = source_byte(feedback.armor), .blood = source_byte(feedback.blood),
            .set_angle = row->state.data.nq.fix_angle};
        memcpy(value.origin, feedback.origin, sizeof(value.origin));
        vector(value.angles, row->state.data.nq.angles);
        row->state.data.nq.fix_angle = false;
        *out = value;
    } else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 feedback requires an actual spawned source client");
    application_native_q1_wire_end(&source); return okay;
}
static bool player_model(application_native_q1_wire_source *source, uint32_t *model) {
    qa_strings *strings = qa_session_strings(source->provider->application->session);
    const char *path = "progs/player.mdl";
    qa_string_id resource = qa_strings_find(strings, (qa_bytes){(const uint8_t *)path, strlen(path)});
    return resource && qa_q1_wire_index(&source->receipt, true, resource, model);
}
bool application_native_q1_wire_baseline(qa_application *app, qa_actor_id recipient,
    const qa_q1_entity *entity, qa_q1_entity *out, qa_error *error) {
    if (!entity || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 source baseline");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; bool okay = client(&source, recipient, &slot, error) &&
        entity->number && entity->number < source.receipt.entity_slots;
    qa_q1_entity value = *entity;
    qa_actor_id actor;
    if (okay && qa_q1_wire_actor_at(&source.receipt, entity->number, &actor))
        okay = entity_read(app, &source, actor, true, &value, error);
    else if (okay && entity->number > source.receipt.client_slots) okay = false;
    if (okay && entity->number <= source.receipt.client_slots) {
        okay = player_model(&source, &value.model);
        value.colormap = entity->number;
    } else value.colormap = 0;
    if (okay) {
        value.model = value.model > 255 ? 0 : value.model;
        value.frame = value.frame > 255 ? 0 : value.frame;
        value.effects = 0; value.alpha = 0; value.scale = 16;
        value.lerp_finish = 0; value.step = false; *out = value;
    } else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 baseline has no source row or declared player model");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_client_baseline(qa_application *app, qa_actor_id recipient,
    uint32_t physical, qa_q1_entity *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing reserved native Q1 baseline");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; qa_q1_entity value; qa_q1_entity_init(&value);
    bool okay = client(&source, recipient, &slot, error) && physical &&
        physical <= source.receipt.client_slots;
    if (okay) {
        qa_actor_id actor;
        if (qa_q1_wire_actor_at(&source.receipt, physical, &actor))
            okay = entity_read(app, &source, actor, true, &value, error);
        if (okay) okay = player_model(&source, &value.model);
        value.number = physical; value.colormap = physical;
        value.model = value.model > 255 ? 0 : value.model;
        value.frame = value.frame > 255 ? 0 : value.frame;
        value.effects = 0; value.alpha = 0; value.scale = 16;
        value.step = false; value.lerp_finish = 0;
        if (okay) *out = value;
    } else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Reserved native Q1 baseline leaves its actual client extent");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_damage(qa_application *app, const qa_damage_outcome *outcome,
    qa_error *error) {
    if (!app || !outcome || !outcome->result.has_feedback ||
        outcome->result.feedback_family != QA_GAME_Q1) return true;
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    uint32_t slot;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached ||
        p->close_pending || !qa_q1_wire_enabled(p->state.q1) ||
        !qa_q1_native_client_slot(p->state.q1, outcome->request.target, &slot, NULL)) return true;
    qa_actor_id inflictor = outcome->request.attack.inflictor;
    application_native_q1_wire_source source = {0};
    if (!source_retain(p, &source, error)) return false;
    qa_body_state body;
    double origin[3];
    uint64_t serial = qa_world_body_storage_serial(app->world, inflictor);
    bool okay = true;
    if (serial && qa_world_body_read(app->world, inflictor, &body, NULL) &&
        qa_world_body_storage_serial(app->world, inflictor) == serial) {
        origin[0] = (double)body.origin.x + .5 * ((double)body.bounds.mins.x + body.bounds.maxs.x);
        origin[1] = (double)body.origin.y + .5 * ((double)body.bounds.mins.y + body.bounds.maxs.y);
        origin[2] = (double)body.origin.z + .5 * ((double)body.bounds.mins.z + body.bounds.maxs.z);
    } else if (outcome->inflictor_center.present &&
               qa_actor_id_equal(outcome->inflictor_center.inflictor, inflictor))
        memcpy(origin, outcome->inflictor_center.center, sizeof(origin));
    else okay = application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 feedback lost its source inflictor center");
    if (okay && (p->close_pending || application_world_provider(app, QA_ROLE_ENTITIES, "") != p ||
        !qa_q1_wire_receipt_current(&source.receipt) ||
        !qa_q1_native_client_slot(p->state.q1, outcome->request.target, &slot, error)))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 feedback recipient changed during body observation");
    if (okay) okay = qa_q1_wire_feedback_add(p->state.q1, outcome->request.target, inflictor,
        outcome->result.power_saved + outcome->result.armor_saved, outcome->result.blood, origin, error);
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_inflictor_center(qa_application *app,
    const qa_damage_request *request, double center[3], bool *found, qa_error *error) {
    if (!found || !center || !request) return application_fail(error, QA_ERROR_ARGUMENT, "Missing source damage body receipt");
    *found = false;
    application_provider *p = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    uint32_t slot;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached ||
        p->close_pending || !qa_q1_wire_enabled(p->state.q1) ||
        !qa_q1_native_client_slot(p->state.q1, request->target, &slot, NULL)) return true;
    qa_actor_id actor = request->attack.inflictor;
    qa_body_state body;
    uint64_t serial = qa_world_body_storage_serial(app->world, actor);
    if (!actor.registry || !serial) return true;
    application_native_q1_wire_source source = {0};
    if (!source_retain(p, &source, error)) return false;
    bool okay = qa_world_body_read(app->world, actor, &body, error) &&
        qa_world_body_storage_serial(app->world, actor) == serial && !p->close_pending &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == p &&
        qa_q1_wire_receipt_current(&source.receipt) &&
        qa_q1_native_client_slot(p->state.q1, request->target, &slot, error);
    if (okay) {
        center[0] = (double)body.origin.x + .5 * ((double)body.bounds.mins.x + body.bounds.maxs.x);
        center[1] = (double)body.origin.y + .5 * ((double)body.bounds.mins.y + body.bounds.maxs.y);
        center[2] = (double)body.origin.z + .5 * ((double)body.bounds.mins.z + body.bounds.maxs.z);
        *found = true;
    } else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_NOT_FOUND, "Q1 damage admission lost its genuine inflictor body");
    application_native_q1_wire_end(&source); return okay;
}
static bool emit_message(application_provider *p, const qa_builtin_event *event,
    const qa_nq_message *message, qa_actor_id recipient, bool reliable, bool signon,
    const qa_application_protocol_reference *reference, qa_error *error) {
    uint8_t bytes[8192]; qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_nq_write(&writer, protocol(), (qa_nq_options){.standard_quake = true},
                    message, NULL, 0)) return false;
    qa_application_protocol_event record = {.provider = p->owner, .dialect = QA_CLOCK_NETQUAKE,
        .time_ns = event->time_ns, .recipient = recipient, .origin = event->origin,
        .payload = {bytes, qa_net_writer_size(&writer)}, .references = reference,
        .reference_count = reference ? 1 : 0, .destination = signon ? 3 : recipient.registry ? 1 : reliable ? 2 : 0,
        .reliable = reliable, .signon = signon};
    return application_emit_protocol(p, &record, error);
}
static bool emit_text(qa_application *app, application_provider *wire,
    const qa_builtin_event *event, qa_nq_svc op, qa_error *error) {
    const char *format = text(app, event->text);
    if (!format) return application_fail(error, QA_ERROR_FORMAT, "Q1 wire text lost its source format");
    bool formatted = (event->flags & 2u) || event->argument_count || *format == '$';
    const char **arguments = NULL;
    if (event->argument_count > SIZE_MAX / (sizeof(*arguments) + 32))
        return application_fail(error, QA_ERROR_MEMORY, "Q1 wire source text tuple exceeds its extent");
    if (event->argument_count) {
        arguments = malloc(event->argument_count * (sizeof(*arguments) + 32));
        if (!arguments) return application_fail(error, QA_ERROR_MEMORY, "Reading Q1 wire source text tuple");
    }
    char *numbers = arguments ? (char *)(arguments + event->argument_count) : NULL;
    bool okay = true;
    for (size_t i = 0; okay && i < event->argument_count; ++i) {
        if (event->arguments[i].kind == QA_BUILTIN_MESSAGE_NUMBER) {
            arguments[i] = numbers + i * 32;
            okay = qa_format_number(event->arguments[i].value.number, numbers + i * 32, error);
        } else if (event->arguments[i].kind == QA_BUILTIN_MESSAGE_STRING) {
            arguments[i] = text(app, event->arguments[i].value.text);
            if (!arguments[i]) okay = application_fail(error, QA_ERROR_FORMAT, "Q1 wire argument lost its source string");
        } else okay = application_fail(error, QA_ERROR_FORMAT, "Q1 wire argument lost its source type");
    }
    application_provider *source = NULL;
    if (formatted) {
        for (size_t i = 0; i < app->provider_count; ++i)
            if (app->providers[i]->owner == event->provider) { source = app->providers[i]; break; }
        if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->constructed || !source->attached ||
            source->close_pending || !source->native_q1_wire || !source->product || source->product->family != QA_GAME_Q1)
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "Localized Q1 wire text lost its actual content source");
    }
    qa_q1_game_operation operation = {0}; uint32_t client_slots = 0;
    if (okay) okay = qa_q1_game_operation_begin(wire->state.q1, &operation, error) &&
        qa_q1_bot_max_clients(wire->state.q1, &client_slots, error);
    for (uint32_t slot = 0; okay && slot < client_slots; ++slot) {
        qa_actor_id actor;
        if (!qa_q1_source_client_actor(wire->state.q1, slot, &actor)) continue;
        if (event->actor.registry && !qa_actor_id_equal(event->actor, actor)) continue;
        const application_player_record *recipient = roster(app, actor);
        if (!recipient) { okay = application_fail(error, QA_ERROR_NOT_FOUND, "Q1 wire text lost its actual recipient connection"); break; }
        if (recipient->bot && !recipient->remote) continue;
        char output[1024]; const char *value = format;
        if (formatted) {
            native_q1_wire_language *row = source->native_q1_wire->languages;
            while (row && !qa_actor_id_equal(row->actor, actor)) row = row->next;
            if (!row || !row->catalog) { okay = application_fail(error, QA_ERROR_NOT_FOUND, "Q1 wire text has no admitted recipient catalog"); break; }
            if (source->product->edition != QA_EDITION_RERELEASE &&
                (*format != '$' || !qa_localization_find(row->catalog, format + 1)))
                okay = qa_q1_classic_text(format, arguments, event->argument_count, output, sizeof(output), error);
            else (void)qa_localize_presentation(row->catalog, format, arguments,
                event->argument_count, true, output, sizeof(output));
            value = output;
        }
        qa_nq_message message = {.op = op, .data.text = value};
        if (okay) okay = emit_message(wire, event, &message, actor, true, false, NULL, error);
    }
    qa_q1_game_operation_end(&operation); free(arguments);
    return okay;
}
bool application_native_q1_wire_emit(qa_application *app, const qa_builtin_event *event,
    qa_error *error) {
    if (!event || event->family != QA_GAME_Q1) return true;
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached ||
        p->close_pending || !p->launch || p->launch->selection.clock.kind != QA_CLOCK_NETQUAKE ||
        !qa_q1_wire_enabled(p->state.q1)) return true;
    qa_nq_message message = {0}; qa_actor_id recipient = {0};
    qa_application_protocol_reference reference = {0}; bool has_reference = false;
    bool reliable = false, signon = false;
    uint32_t slot, index;
    switch (event->kind) {
    case QA_BUILTIN_MESSAGE:
    case QA_BUILTIN_CENTERPRINT:
        return emit_text(app, p, event,
            event->kind == QA_BUILTIN_MESSAGE ? QA_NQ_PRINT : QA_NQ_CENTERPRINT, error);
    case QA_BUILTIN_SOUND:
        if (event->provider != p->owner)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q1 sound belongs to another source content owner");
        if (!qa_q1_wire_emission_index(p->state.q1, false, event->resource, &index)) return true;
        message.op = event->flags & 1 ? QA_NQ_STATICSOUND : QA_NQ_SOUND;
        message.data.sound = (qa_q1_sound){.index = index, .channel = (uint32_t)event->channel,
            .volume = (uint8_t)(counter((double)event->volume * 255) & 255), .attenuation = event->attenuation};
        vector(message.data.sound.origin, event->origin);
        if (event->flags & 1) { signon = true; message.data.sound.channel = 0; }
        else {
            if (!qa_q1_wire_emission_slot(p->state.q1, event->actor, &slot)) return true;
            message.data.sound.entity = slot;
            reference = (qa_application_protocol_reference){.actor = event->actor, .packed_sound = true,
                .offset = (size_t)2 + (size_t)(message.data.sound.volume != 255) +
                    (size_t)(event->attenuation != 1)};
            has_reference = true;
        }
        break;
    case QA_BUILTIN_STOP_SOUND:
        if (!qa_q1_wire_emission_slot(p->state.q1, event->actor, &slot)) return true;
        message.op = QA_NQ_STOPSOUND; message.data.stop_sound.entity = (uint16_t)slot;
        message.data.stop_sound.channel = (uint8_t)event->channel;
        reference = (qa_application_protocol_reference){.actor = event->actor, .packed_sound = true, .offset = 1};
        has_reference = true; break;
    case QA_BUILTIN_LIGHT:
        if (event->provider != p->owner) return true;
        message.op = QA_NQ_LIGHTSTYLE;
        message.data.indexed_text.index = (uint8_t)event->code;
        message.data.indexed_text.text = text(app, event->resource); reliable = true; break;
    case QA_BUILTIN_PARTICLES:
        message.op = QA_NQ_PARTICLE; vector(message.data.particle.origin, event->origin);
        message.data.particle.direction[0] = particle_direction(event->direction.x);
        message.data.particle.direction[1] = particle_direction(event->direction.y);
        message.data.particle.direction[2] = particle_direction(event->direction.z);
        message.data.particle.count = (uint8_t)event->count;
        message.data.particle.color = (uint8_t)event->code; break;
    case QA_BUILTIN_BEAM:
        if (!qa_q1_wire_emission_slot(p->state.q1, event->actor, &slot)) return true;
        message.op = QA_NQ_TEMPENTITY; message.data.temporary.kind = QA_Q1_TEMP_BEAM;
        if (event->code < 1 || event->code > 4) return true;
        message.data.temporary.type = event->code == 1 ? 5 : event->code == 2 ? 6 : event->code == 3 ? 9 : 13;
        message.data.temporary.entity = (uint16_t)slot;
        vector(message.data.temporary.origin, event->origin); vector(message.data.temporary.end, event->end);
        reference = (qa_application_protocol_reference){.actor = event->actor, .offset = 2};
        has_reference = true; break;
    case QA_BUILTIN_IMPACT:
        if (event->code == 1) {
            message.op = QA_NQ_PARTICLE; vector(message.data.particle.origin, event->origin);
            message.data.particle.count = (uint8_t)(counter((double)event->value * 2) & 255);
            message.data.particle.color = 73; break;
        }
        message.op = QA_NQ_TEMPENTITY; message.data.temporary.kind = QA_Q1_TEMP_POINT;
        if (event->code == 2) message.data.temporary.type = 2;
        else if (event->code == 3) message.data.temporary.type = 0;
        else if (event->code == 4) message.data.temporary.type = 1;
        else if (event->code == 7 || event->code == 8 || event->code == 10)
            message.data.temporary.type = (uint8_t)event->code;
        else return true;
        vector(message.data.temporary.origin, event->origin); break;
    case QA_BUILTIN_EXPLOSION:
    case QA_BUILTIN_TELEPORT:
        message.op = QA_NQ_TEMPENTITY; message.data.temporary.kind = QA_Q1_TEMP_POINT;
        message.data.temporary.type = event->kind == QA_BUILTIN_TELEPORT ? 11 :
            event->code == 1 ? 4 : event->code == 10 ? 10 : 3;
        vector(message.data.temporary.origin, event->origin); break;
    case QA_BUILTIN_ITEM:
        if (!event->other.registry) return true;
        if (!qa_q1_native_client_slot(p->state.q1, event->other, &slot, NULL)) return true;
        recipient = event->other; message.op = QA_NQ_STUFFTEXT; message.data.text = "bf\n"; reliable = true; break;
    case QA_BUILTIN_EFFECT:
        if (event->provider != p->owner) return true;
        if (event->flags & UINT32_C(0x80000000)) {
            if (event->code == 1) { message.op = QA_NQ_INTERMISSION; reliable = true; break; }
            if (event->code != 4) return true;
            return emit_text(app, p, event, QA_NQ_FINALE, error);
        }
        if (!event->actor.registry && event->resource &&
            qa_q1_wire_emission_index(p->state.q1, true, event->resource, &index)) {
            message.op = QA_NQ_STATIC; qa_q1_entity_init(&message.data.entity);
            message.data.entity.model = index; message.data.entity.frame = (uint32_t)event->frame;
            message.data.entity.skin = (uint32_t)event->code; message.data.entity.colormap = (uint32_t)event->channel;
            vector(message.data.entity.origin, event->origin); vector(message.data.entity.angles, event->direction);
            signon = true; break;
        }
        if (event->other.registry && !event->resource && event->count > 0) {
            qa_q1_presentation source;
            if (!qa_q1_game_presentation(p->state.q1, event->other, &source)) return true;
            const char *classname = text(app, source.classname);
            if (!classname || strcmp(classname, "trigger_secret")) return true;
            message.op = QA_NQ_FOUNDSECRET; reliable = true; break;
        }
        if (event->resource && !strcmp(text(app, event->resource), "colored-explosion")) {
            message.op = QA_NQ_TEMPENTITY; message.data.temporary.kind = QA_Q1_TEMP_COLORS;
            message.data.temporary.type = 12; message.data.temporary.color_start = (uint8_t)event->code;
            message.data.temporary.color_length = (uint8_t)event->count;
            vector(message.data.temporary.origin, event->origin); break;
        }
        return true;
    case QA_BUILTIN_DEATH:
        if (event->provider != p->owner || !event->count) return true;
        message.op = QA_NQ_KILLEDMONSTER; reliable = true; break;
    case QA_BUILTIN_TARGET:
        if (event->provider != p->owner || !(event->flags & UINT32_C(0x80000000)) || event->code != 1) return true;
        message.op = QA_NQ_INTERMISSION; reliable = true; break;
    default: return true;
    }
    return emit_message(p, event, &message, recipient, reliable, signon,
                        has_reference ? &reference : NULL, error);
}
bool application_native_q1_wire_observe(qa_application *app, qa_error *error) {
    application_provider *p = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !qa_q1_wire_enabled(p->state.q1)) return true;
    application_native_q1_wire_source source = {0};
    source.provider = p;
    if (!p->constructed || !p->attached || p->close_pending ||
        !qa_q1_wire_read_begin(p->state.q1, &source.receipt, error)) return false;
    if (!p->native_q1_wire || p->native_q1_wire->readers == SIZE_MAX) {
        qa_q1_wire_read_end(&source.receipt);
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 observation lost its held application source owner");
    }
    ++p->native_q1_wire->readers;
    if (source.receipt.client_slots > 255) {
        application_native_q1_wire_end(&source);
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q1 client observation exceeds its actual wire extent");
    }
    uint64_t time_ns; double elapsed;
    bool okay = qa_q1_game_clock_read(p->state.q1, &time_ns, &elapsed);
    if (!okay) application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 client observation lost its source clock");
    qa_builtin_event event = {.family = QA_GAME_Q1, .provider = p->owner, .time_ns = okay ? time_ns : 0};
    for (uint32_t slot = 0; okay && slot < source.receipt.client_slots; ++slot) {
        qa_q1_wire_board_change change;
        okay = qa_q1_wire_board_observe(&source.receipt, slot, &change, error);
        if (!okay) break;
        qa_nq_message message = {0};
        if (change.name_changed) {
            message.op = QA_NQ_NAME; message.data.indexed_text.index = (uint8_t)slot;
            message.data.indexed_text.text = text(app, change.name);
            okay = emit_message(p, &event, &message, (qa_actor_id){0}, true, false, NULL, error);
        }
        if (okay && change.colors_changed) {
            message.op = QA_NQ_COLORS; message.data.indexed.index = (uint8_t)slot;
            message.data.indexed.value = change.colors;
            okay = emit_message(p, &event, &message, (qa_actor_id){0}, true, false, NULL, error);
        }
        if (okay && change.frags_changed) {
            message.op = QA_NQ_FRAGS; message.data.indexed.index = (uint8_t)slot;
            message.data.indexed.value = source_short(change.frags);
            okay = emit_message(p, &event, &message, (qa_actor_id){0}, true, false, NULL, error);
        }
        if (okay && !qa_q1_wire_board_commit(&source.receipt, &change))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 client emission retired its source observation");
    }
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_client_userinfo(application_provider *p, qa_actor_id actor,
    qa_error *error) {
    uint32_t slot;
    const application_player_record *record = p ? roster(p->application, actor) : NULL;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !record ||
        !qa_q1_native_client_slot(p->state.q1, actor, &slot, error) || slot != record->client_slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source admission lost its actual connection");
    if (!record->name) {
        const qa_launch_snapshot *publication = p->application->routing_snapshot;
        if (!publication) publication = qa_application_launch(p->application);
        const qa_launch_choices *choices = qa_launch_snapshot_choices(publication);
        const char *name = NULL;
        for (size_t i = 0; choices && i < choices->seat_count; ++i)
            if (choices->seats[i].id == record->seat) { name = choices->seats[i].name; break; }
        if (!name) return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 source client lost its declared logical seat name");
        size_t length = strlen(name);
        char *copy = malloc(length + 1);
        if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 source seat name");
        memcpy(copy, name, length + 1);
        application_player_record *actual = &p->application->players->records[record - p->application->players->records];
        actual->name = copy;
        record = actual;
    }
    return qa_q1_source_client_userinfo_named(p->state.q1, actor,
        record->userinfo ? record->userinfo : "", record->name, error);
}
bool application_native_q1_wire_client_admit(application_provider *p, qa_actor_id actor,
    qa_error *error) {
    uint32_t slot;
    const application_player_record *record = p ? roster(p->application, actor) : NULL;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !record ||
        !qa_q1_native_client_slot(p->state.q1, actor, &slot, error) || slot != record->client_slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source spawned admission lost its actual connection");
    return qa_q1_source_client_spawned(p->state.q1, actor, error);
}
bool application_native_q1_wire_client_publish(void *opaque,
    const qa_q1_source_client_view *view, qa_error *error) {
    application_provider *p = opaque;
    uint32_t slot;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !view ||
        !qa_q1_native_client_slot(p->state.q1, view->actor, &slot, error) || slot != view->slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 publication lost its actual native source client");
    application_provider *primary = application_world_provider(p->application, QA_ROLE_ENTITIES, "");
    if (primary == p && qa_q1_wire_enabled(p->state.q1)) {
        const application_player_record *record = roster(p->application, view->actor);
        const char *language = NULL;
        if (!record) return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 text recipient lost its actual connection");
        if (record->bot && !record->remote) return application_native_q1_wire_observe(p->application, error);
        if (record->remote) {
            if (!qa_q1_source_client_info(p->state.q1, view->actor, "language", &language) || !language || !*language)
                return application_fail(error, QA_ERROR_NOT_FOUND, "Remote Q1 text requires its declared source language");
        } else {
            const qa_launch_snapshot *publication = p->application->routing_snapshot;
            if (!publication) publication = qa_application_launch(p->application);
            const qa_launch_choices *choices = qa_launch_snapshot_choices(publication);
            size_t ordinal = 0;
            while (choices && ordinal < choices->seat_count && choices->seats[ordinal].id != record->seat) ++ordinal;
            if (!choices || ordinal >= choices->seat_count || !choices->seats[ordinal].local ||
                ordinal > UINT32_MAX || !qa_ui_language_read(p->application->cvars, (uint32_t)ordinal, &language, error))
                return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 text recipient lost its physical ENGINE language");
        }
        application_native_q1_wire_language_ticket *ticket = NULL;
        if (!application_native_q1_wire_language_prepare(p->application, view->actor, language, &ticket, error)) return false;
        application_native_q1_wire_language_commit(ticket);
    }
    return application_native_q1_wire_observe(p->application, error);
}
bool application_native_q1_wire_reconnect(qa_application *app, qa_error *error) {
    if (!app) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 wire reconnect lost its actual application");
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p->kind == APPLICATION_PROVIDER_Q1 && !application_native_q1_wire_resources_prepare(p, error)) return false;
    }
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !qa_q1_wire_enabled(p->state.q1)) return true;
    uint32_t clients;
    if (!qa_q1_bot_max_clients(p->state.q1, &clients, error)) return false;
    for (uint32_t slot = 0; slot < clients; ++slot) {
        qa_actor_id actor; qa_q1_source_client_view view;
        if (!qa_q1_source_client_actor(p->state.q1, slot, &actor)) continue;
        if (!qa_q1_source_client_read(p->state.q1, actor, &view) ||
            !application_native_q1_wire_client_publish(p, &view, error)) return false;
    }
    return true;
}
bool application_native_q1_wire_client_observer(void *opaque, qa_actor_id actor,
    bool observer, qa_error *error) {
    application_provider *p = opaque;
    uint32_t slot;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 ||
        !qa_q1_native_client_slot(p->state.q1, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 observer lost its actual native source client");
    if (!p->native_q1_wire || p->native_q1_wire->readers == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 observer lost its retained application owner");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(p->state.q1, &operation, error)) return false;
    ++p->native_q1_wire->readers;
    qa_application *app = p->application;
    qa_combat_state traits;
    bool okay = application_control_player_mode(app, actor,
            observer ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL, observer, error);
    if (okay) okay = qa_combat_read_traits(app->combat, actor, &traits, error);
    if (okay) {
        traits.can_take_damage = !observer;
        okay = qa_combat_set_traits(app->combat, actor, &traits, error) &&
            qa_world_link(app->world, actor, NULL, error);
    }
    uint32_t current_slot;
    if (okay && (!qa_q1_game_operation_live(&operation) || p->close_pending ||
        !qa_q1_native_client_slot(p->state.q1, actor, &current_slot, error) || current_slot != slot))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 observer source changed during shared body callbacks");
    --p->native_q1_wire->readers;
    qa_q1_game_operation_end(&operation); return okay;
}
bool application_native_q1_wire_entity_next(qa_application *app, qa_actor_id recipient,
    uint32_t *cursor, bool *present, qa_actor_id *actor, qa_q1_entity *out, qa_error *error) {
    if (!cursor || !present || !actor || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 entity inventory");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; bool okay = client(&source, recipient, &slot, error);
    if (okay) {
        *present = false;
        if (!*cursor) *cursor = 1;
        while (*cursor < source.receipt.entity_slots) {
            qa_actor_id candidate;
            uint32_t physical = (*cursor)++;
            if (!qa_q1_wire_actor_at(&source.receipt, physical, &candidate)) continue;
            qa_q1_entity value;
            if (!entity_read(app, &source, candidate, false, &value, error)) { okay = false; break; }
            if (!value.model) continue;
            *actor = candidate; *out = value; *present = true; break;
        }
    }
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_eye(qa_application *app, qa_actor_id recipient, qa_vec3 *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 eye observation");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; qa_body_state body; application_control_record *row = NULL;
    uint64_t serial = qa_world_body_storage_serial(app->world, recipient);
    bool okay = client(&source, recipient, &slot, error) && serial &&
        qa_world_body_read(app->world, recipient, &body, error) &&
        qa_world_body_storage_serial(app->world, recipient) == serial &&
        qa_q1_wire_receipt_current(&source.receipt) &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == source.provider &&
        client(&source, recipient, &slot, error) && (row = control(app, recipient)) != NULL;
    if (okay) *out = qa_vec_add(body.origin, row->view_offset);
    else if (error && error->code == QA_OK) application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 eye has no selected movement state");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_bounds(qa_application *app, qa_actor_id recipient, qa_actor_id actor,
    qa_bounds *out, bool *has_model, qa_error *error) {
    if (!out || !has_model) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 source bounds");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; qa_linked_body linked; qa_q1_entity entity;
    bool okay = client(&source, recipient, &slot, error) && entity_read(app, &source, actor, false, &entity, error) &&
        qa_world_linked(app->world, actor, &linked);
    if (okay) { *out = linked.absolute_bounds; *has_model = entity.model != 0; }
    else if (error && error->code == QA_OK) application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 source body is not linked");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_chat(qa_application *app, qa_actor_id sender, bool team_only,
    const char **name, qa_actor_id recipients[255], size_t *out_count, qa_error *error) {
    if (!name || !recipients || !out_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q1 source chat outputs");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; qa_q1_source_client_view from;
    bool okay = client(&source, sender, &slot, error) &&
        qa_q1_source_client_read(source.provider->state.q1, sender, &from);
    qa_cvars *cvars = application_native_q1_console_registry(source.provider);
    const qa_cvar_view *teamplay = cvars ? qa_cvars_find(cvars, "teamplay") : NULL;
    bool filtered = team_only && teamplay && teamplay->number != 0;
    qa_actor_id values[255]; size_t count = 0;
    for (uint32_t i = 0; okay && i < source.receipt.client_slots; ++i) {
        qa_actor_id actor; qa_q1_source_client_view view;
        if (!qa_q1_source_client_actor(source.provider->state.q1, i, &actor)) continue;
        if (!qa_q1_source_client_read(source.provider->state.q1, actor, &view)) {
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 chat lost its physical source recipient"); break;
        }
        if (!filtered || view.team == from.team) values[count++] = actor;
    }
    if (okay) { memcpy(recipients, values, count * sizeof(*values)); *out_count = count; *name = from.name; }
    else if (error && error->code == QA_OK)
        application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 chat lost its actual source sender");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_pause(qa_application *app, qa_actor_id actor,
    qa_buffer *out, bool *changed, qa_error *error) {
    if (!out || out->data || out->size || !changed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 pause requires empty announcement output");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot; qa_q1_source_client_view view;
    bool okay = app->operation == APPLICATION_IDLE && client(&source, actor, &slot, error) &&
        qa_q1_source_client_read(source.provider->state.q1, actor, &view);
    qa_cvars *cvars = application_native_q1_console_registry(source.provider);
    const qa_cvar_view *policy = cvars ? qa_cvars_find(cvars, "pausable") : NULL;
    const char *denial = policy && policy->number == 0 ? "Pause not allowed.\n" : NULL;
    bool paused = !qa_application_q1_paused(app);
    const char *suffix = paused ? " paused the game\n" : " unpaused the game\n";
    size_t length = okay ? strlen(denial ? denial : view.name) : 0;
    if (okay && !denial && length > SIZE_MAX - strlen(suffix) - 1)
        okay = application_fail(error, QA_ERROR_MEMORY, "Native Q1 pause announcement extent overflows");
    if (okay && !denial) length += strlen(suffix);
    if (okay && length > 7998)
        okay = application_fail(error, QA_ERROR_FORMAT, "Native Q1 pause announcement exceeds its reliable extent");
    qa_buffer result = {0};
    if (okay) {
        result = (qa_buffer){.data = malloc(length + 1), .size = length};
        if (!result.data) okay = application_fail(error, QA_ERROR_MEMORY, "Retaining native Q1 pause announcement");
        else if (denial) memcpy(result.data, denial, length + 1);
        else {
            size_t prefix = strlen(view.name);
            memcpy(result.data, view.name, prefix); memcpy(result.data + prefix, suffix, length - prefix + 1);
        }
    }
    application_provider *provider = source.provider;
    application_native_q1_wire_end(&source);
    if (okay && !denial) okay = application_q1_pause_set(app, provider, paused, error);
    if (okay) { *out = result; *changed = denial == NULL; }
    else {
        qa_buffer_free(&result);
        if (error && error->code == QA_OK)
            application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 pause lost its idle source client");
    }
    return okay;
}
static bool source_roster_refresh(application_native_q1_wire_source *source, qa_actor_id actor,
    qa_error *error) {
    qa_application *app = source->provider->application;
    qa_q1_source_client_view view; qa_buffer userinfo = {0};
    const application_player_record *record = roster(app, actor);
    if (!record || !qa_q1_source_client_read(source->provider->state.q1, actor, &view) ||
        record->client_slot != view.slot)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 source userinfo lost its full actor roster");
    if (!qa_q1_source_client_userinfo_read(source->provider->state.q1, actor, false, &userinfo, error)) return false;
    const char *name = NULL;
    if (!qa_q1_source_client_info(source->provider->state.q1, actor, "name", &name) || !name) {
        qa_buffer_free(&userinfo);
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q1 continuation lost its typed source name");
    }
    size_t length = strlen(name);
    char *copy = malloc(length + 1);
    if (!copy) { qa_buffer_free(&userinfo); return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q1 source name"); }
    memcpy(copy, name, length + 1);
    application_player_record *actual = &app->players->records[record - app->players->records];
    free(actual->name); free(actual->userinfo);
    actual->name = copy; actual->userinfo = (char *)userinfo.data;
    return true;
}
bool application_native_q1_wire_name(qa_application *app, qa_actor_id actor, const char *name,
    qa_error *error) {
    if (!name || !app || app->operation != APPLICATION_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 name requires its idle source owner");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot;
    bool okay = client(&source, actor, &slot, error) &&
        qa_q1_source_client_name(source.provider->state.q1, actor, name, error) &&
        qa_q1_wire_receipt_current(&source.receipt) &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == source.provider &&
        client(&source, actor, &slot, error) && source_roster_refresh(&source, actor, error);
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 source name changed during publication");
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_wire_colors(qa_application *app, qa_actor_id actor,
    int32_t top, int32_t bottom, qa_error *error) {
    if (!app || app->operation != APPLICATION_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 colors require their idle source owner");
    application_native_q1_wire_source source = {0};
    if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
    uint32_t slot;
    bool okay = client(&source, actor, &slot, error) &&
        qa_q1_source_client_colors(source.provider->state.q1, actor, top, bottom, error) &&
        qa_q1_wire_receipt_current(&source.receipt) &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == source.provider &&
        client(&source, actor, &slot, error) && source_roster_refresh(&source, actor, error);
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 source colors changed during publication");
    application_native_q1_wire_end(&source); return okay;
}
