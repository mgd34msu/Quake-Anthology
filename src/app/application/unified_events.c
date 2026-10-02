#include "internal.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "map_players_private.h"
#include "unified_output.h"
#include "qa/json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static application_provider *source_provider(qa_application *app, qa_actor_owner owner)
{
    for (application_provider *p = app ? app->live_providers : NULL; p; p = p->next_live)
        if (p->owner == owner && !p->close_pending) return p;
    return NULL;
}

size_t application_unified_event_resource_count(const qa_application *app)
{ return app ? app->unified_event_resource_count : 0; }

const application_unified_event_resource *application_unified_event_resource_at(
    const qa_application *app, size_t index)
{
    return app && index < app->unified_event_resource_count ? app->unified_event_resources + index : NULL;
}

const qa_resource *application_unified_event_resource_read(const qa_application *app, const char *id)
{
    for (size_t i = 0; app && id && i < app->unified_event_resource_count; ++i)
        if (!strcmp(app->unified_event_resources[i].id, id)) return app->unified_event_resources[i].resource;
    return NULL;
}

bool application_unified_event_resource_lookup(qa_application *app, qa_actor_owner owner,
    const char *path, char id[81], bool *found, qa_error *error)
{
    if (!source_provider(app, owner) || !path || !id || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source sound lookup lost its actual registration owner");
    id[0] = 0; *found = false;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i) {
        const application_unified_event_registration *row = app->unified_event_registrations + i;
        const char *registered = qa_strings_cstr(qa_session_strings(app->session), row->path);
        if (row->provider != owner || !registered || strcmp(registered, path)) continue;
        if (row->resource >= app->unified_event_resource_count)
            return application_fail(error, QA_ERROR_FORMAT, "Source sound registration lost its retained resource");
        memcpy(id, app->unified_event_resources[row->resource].id, 81);
        *found = true; return true;
    }
    return true;
}

bool application_unified_event_registration_clear(qa_application *app, qa_actor_owner owner, qa_error *error)
{
    if (!app || !owner) return application_fail(error, QA_ERROR_ARGUMENT, "Source registration retirement has no actual owner");
    bool changed = false;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i)
        if (app->unified_event_registrations[i].provider == owner) { changed = true; break; }
    if (!changed) return true;
    if (app->unified_event_registration_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Source registration revision is exhausted");
    size_t kept = 0;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i)
        if (app->unified_event_registrations[i].provider != owner)
            app->unified_event_registrations[kept++] = app->unified_event_registrations[i];
    app->unified_event_registration_count = kept;
    ++app->unified_event_registration_revision;
    return true;
}

static bool registration_bind(qa_application *app, qa_actor_owner owner, const char *path,
    size_t resource, qa_error *error)
{
    qa_string_id name;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), path, &name, error)) return false;
    size_t index = 0;
    while (index < app->unified_event_registration_count &&
        (app->unified_event_registrations[index].provider != owner ||
         app->unified_event_registrations[index].path != name)) ++index;
    if (index < app->unified_event_registration_count &&
        app->unified_event_registrations[index].resource == resource) return true;
    if (app->unified_event_registration_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Source registration revision is exhausted");
    if (index == app->unified_event_registration_capacity) {
        size_t capacity = index ? index * 2 : 32;
        if (capacity < index || capacity > SIZE_MAX / sizeof(*app->unified_event_registrations))
            return application_fail(error, QA_ERROR_MEMORY, "Source registration extent overflows");
        void *rows = realloc(app->unified_event_registrations, capacity * sizeof(*app->unified_event_registrations));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Source registrations");
        app->unified_event_registrations = rows; app->unified_event_registration_capacity = capacity;
    }
    app->unified_event_registrations[index] = (application_unified_event_registration){owner, name, resource};
    if (index == app->unified_event_registration_count) ++app->unified_event_registration_count;
    ++app->unified_event_registration_revision;
    return true;
}

void application_unified_events_resources_dispose(qa_application *app)
{
    if (!app) return;
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        application_unified_event_resource *row = app->unified_event_resources + i;
        qa_buffer_free(&row->key);
        qa_resource_release(row->resource);
        qa_launch_instance_lease_release(row->descriptor);
        qa_resource_pool_destroy(row->pool);
    }
    free(app->unified_event_resources);
    free(app->unified_event_registrations);
    app->unified_event_registrations = NULL;
    app->unified_event_registration_count = app->unified_event_registration_capacity = 0;
    app->unified_event_resources = NULL;
    app->unified_event_resource_count = app->unified_event_resource_capacity = 0;
}

bool application_unified_event_resource_register(qa_application *app, qa_actor_owner owner,
    const char *path, const qa_resource *resource, char id[81], qa_error *error)
{
    application_provider *provider = source_provider(app, owner);
    if (!provider || !provider->launch || !provider->product || !path || !*path || !resource || !id)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source registration has no genuine held resource");
    const qa_vfs *view = provider->launch->content;
    bool opened = false;
    const char *registration_path = NULL;
    for (size_t i = 0; i < qa_vfs_read_count(view); ++i) {
        qa_vfs_read_reference actual;
        if (qa_vfs_read_at(view, i, &actual) && actual.resource == resource && actual.path &&
            (!strcmp(actual.path, path) || (!strncmp(actual.path, "sound/", 6) && !strcmp(actual.path + 6, path)) ||
             (path[0] == '#' && !strcmp(actual.path, path + 1)))) {
            opened = true; registration_path = actual.path; break;
        }
    }
    qa_resource_pool *pool = qa_vfs_resources(view);
    if (!opened || !pool || qa_resource_pool_find(pool, qa_resource_id(resource)) != resource)
        return application_fail(error, QA_ERROR_FORMAT, "Source registration is outside its actual precache opening");
    qa_unified_document *key = NULL;
    char actual_id[81];
    if (!application_unified_resource_key(provider->product, registration_path, resource, &key, actual_id, error)) return false;
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        if (!strcmp(app->unified_event_resources[i].id, actual_id)) {
            bool ok = registration_bind(app, owner, path, i, error);
            if (ok) memcpy(id, actual_id, 81);
            qa_unified_document_destroy(key);
            return ok;
        }
    }
    application_unified_event_resource row = {.provider = owner, .resource = (qa_resource *)resource, .pool = pool};
    qa_bytes bytes = qa_json_source(qa_unified_document_json(key), qa_unified_document_root(key));
    row.key.data = malloc(bytes.size);
    bool ok = row.key.data != NULL &&
        qa_strings_intern_cstr(qa_session_strings(app->session), registration_path, &row.path, error) &&
        qa_strings_intern_cstr(qa_session_strings(app->session), provider->product->identity, &row.content, error) &&
        qa_launch_instance_retain_metadata(provider->launch, &row.descriptor, error);
    if (!row.key.data) application_fail(error, QA_ERROR_MEMORY, "Retaining Source resource key");
    if (ok && app->unified_event_resource_count == app->unified_event_resource_capacity) {
        size_t capacity = app->unified_event_resource_capacity ? app->unified_event_resource_capacity * 2 : 32;
        if (capacity < app->unified_event_resource_capacity || capacity > SIZE_MAX / sizeof(row))
            ok = application_fail(error, QA_ERROR_MEMORY, "Source resource dictionary extent overflows");
        else {
            void *rows = realloc(app->unified_event_resources, capacity * sizeof(row));
            if (!rows) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining Source resource dictionary");
            else { app->unified_event_resources = rows; app->unified_event_resource_capacity = capacity; }
        }
    }
    if (ok) {
        memcpy(row.key.data, bytes.data, bytes.size); row.key.size = bytes.size;
        memcpy(row.id, actual_id, 81);
        qa_resource_retain(row.resource); qa_resource_pool_retain(pool);
        size_t index = app->unified_event_resource_count++;
        app->unified_event_resources[index] = row;
        ok = registration_bind(app, owner, path, index, error);
        if (ok) memcpy(id, actual_id, 81);
    } else {
        qa_buffer_free(&row.key); qa_launch_instance_lease_release(row.descriptor);
    }
    qa_unified_document_destroy(key);
    return ok;
}

bool application_unified_world_text_emit(qa_application *app, qa_actor_owner owner,
    const qa_q2_map_event *event, qa_error *error)
{
    application_provider *provider = source_provider(app, owner);
    qa_clock_state clock;
    if (!provider || !event || event->kind != QA_Q2_MAP_WORLD_TEXT ||
        !qa_session_clock(app->session, owner, &clock) || !event->text ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) || !qa_vec_finite(event->color) ||
        !isfinite(event->alpha) || !isfinite(event->value) || event->value <= 0 ||
        !isfinite(event->duration) || event->duration < 0 || app->unified_world_text_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "World text has no actual Source clock or valid geometry");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (primary) {
        qa_clock_state source_clock;
        if (!qa_session_clock(app->session, primary->owner, &source_clock))
            return application_fail(error, QA_ERROR_STATE, "World text lost its primary Source clock");
        clock = source_clock;
    }
    if (app->unified_world_text_count == app->unified_world_text_capacity) {
        size_t capacity = app->unified_world_text_capacity ? app->unified_world_text_capacity * 2 : 32;
        if (capacity < app->unified_world_text_capacity || capacity > SIZE_MAX / sizeof(*app->unified_world_text))
            return application_fail(error, QA_ERROR_MEMORY, "World text extent overflows");
        void *rows = realloc(app->unified_world_text, capacity * sizeof(*app->unified_world_text));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining genuine world text");
        app->unified_world_text = rows; app->unified_world_text_capacity = capacity;
    }
    application_unified_world_text row = {.provider = owner, .text = event->text,
        .origin = event->origin, .angles = event->direction, .color = event->color,
        .alpha = event->alpha, .cell_size = event->value, .timed = event->duration > 0,
        .expires = (double)clock.frame.time_ns / 1e9 + event->duration,
        .billboard = (event->flags & 2u) != 0, .depth_test = (event->flags & 1u) != 0};
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), provider->product->identity, &row.content, error)) return false;
    double now = (double)clock.frame.time_ns / 1e9;
    size_t count = app->unified_world_text_map == app->map_revision ? app->unified_world_text_count : 0;
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i)
        if (!app->unified_world_text[i].timed || app->unified_world_text[i].expires > now)
            app->unified_world_text[kept++] = app->unified_world_text[i];
    app->unified_world_text_count = kept;
    app->unified_world_text_map = app->map_revision;
    app->unified_world_text[app->unified_world_text_count++] = row;
    ++app->unified_world_text_revision;
    return true;
}

bool application_unified_event_payload_valid(qa_bytes bytes, bool presentation,
    bool link_presentation, qa_error *error)
{
    if (!bytes.size) return true;
    qa_unified_document *document = NULL;
    if (!qa_unified_document_create(QA_UNIFIED_CHECKPOINT, bytes, &document, error)) return false;
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id root = qa_unified_document_root(document);
    bool okay = qa_json_type(json, root) == QA_JSON_OBJECT &&
        qa_json_type(json, qa_json_get(json, root, "kind")) == QA_JSON_STRING;
    if (presentation) {
        static const char *const reserved[] = {"sequence", "seconds", "content", "recipient", "sourceEntity"};
        for (size_t i = 0; okay && i < sizeof(reserved) / sizeof(*reserved); ++i)
            okay = qa_json_get(json, root, reserved[i]) == QA_JSON_NONE;
    } else {
        qa_json_id kind = qa_json_get(json, root, "kind");
        okay = okay && (qa_json_string_equal(json, kind, "sound") || qa_json_string_equal(json, kind, "damage") ||
            qa_json_string_equal(json, kind, "transition") || qa_json_string_equal(json, kind, "message")) &&
            qa_json_get(json, root, "sourcePresentationSequence") == QA_JSON_NONE &&
            (!link_presentation || qa_json_string_equal(json, kind, "message"));
    }
    qa_unified_document_destroy(document);
    return okay || application_fail(error, QA_ERROR_FORMAT, "Source event payload has no genuine kind or replaces its owner envelope");
}

static bool retain_payload(qa_application *app, qa_bytes input, qa_bytes *out, qa_error *error)
{
    if (!input.size) { *out = (qa_bytes){0}; return true; }
    uint8_t *copy = qa_arena_alloc(&app->event_arena, input.size, 1, error);
    if (!copy) return false;
    memcpy(copy, input.data, input.size);
    *out = (qa_bytes){copy, input.size};
    return true;
}

bool application_unified_event_emit(qa_application *app, qa_actor_owner owner,
    qa_bytes presentation, qa_bytes simulation, qa_actor_id recipient,
    qa_actor_id simulation_recipient, uint64_t time_ns, int32_t source_entity,
    bool has_source_entity, bool link_presentation, qa_error *error)
{
    application_provider *provider = source_provider(app, owner);
    if (!app || !app->session || app->destroy_requested || !provider || !provider->launch ||
        !provider->product || !provider->product->identity ||
        (!presentation.size && !simulation.size) || (presentation.size && !presentation.data) ||
        (simulation.size && !simulation.data) ||
        (link_presentation && (!presentation.size || !simulation.size)) ||
        app->unified_event_sequence >= QA_UNIFIED_SAFE_INTEGER ||
        (presentation.size && app->presentation_event_sequence >= QA_UNIFIED_SAFE_INTEGER) ||
        (simulation.size && app->simulation_event_sequence >= QA_UNIFIED_SAFE_INTEGER))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source emission lost its actual owner, payload or sequence domain");
    application_unified_event_record record = {.recipient = recipient,
        .simulation_recipient = simulation_recipient, .provider = owner,
        .clock = provider->launch->selection.clock.kind, .time_ns = time_ns,
        .presentation_clock = provider->launch->selection.clock.kind,
        .simulation_time_ns = time_ns, .source_entity = source_entity,
        .has_source_entity = has_source_entity, .link_presentation = link_presentation};
    if (!has_source_entity) record.source_entity = 0;
    if (simulation.size && simulation_recipient.registry) {
        bool found = false;
        for (size_t i = 0; app->players && i < app->players->count; ++i) {
            const application_player_record *player = &app->players->records[i];
            if (!player->retiring && qa_actor_id_equal(player->actor, simulation_recipient)) {
                record.client = player->remote_client; found = true; break;
            }
        }
        if (!found) simulation = (qa_bytes){0};
    }
    if (!simulation.size) record.link_presentation = false;
    if (!presentation.size && !simulation.size) return true;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_clock_state clock;
    if (primary && qa_session_clock(app->session, primary->owner, &clock)) {
        record.clock = clock.frame.kind;
        record.simulation_time_ns = clock.frame.time_ns;
    }
    if (!application_unified_event_payload_valid(presentation, true, false, error) ||
        !application_unified_event_payload_valid(simulation, false, record.link_presentation, error) ||
        !qa_strings_intern_cstr(qa_session_strings(app->session), provider->product->identity,
            &record.content, error) ||
        !retain_payload(app, presentation, &record.presentation, error) ||
        !retain_payload(app, simulation, &record.simulation, error)) return false;
    if (app->unified_event_count == app->unified_event_capacity) {
        size_t capacity = app->unified_event_capacity ? app->unified_event_capacity : 64;
        if (app->unified_event_capacity) {
            if (capacity > SIZE_MAX / 2)
                return application_fail(error, QA_ERROR_MEMORY, "Source projection capacity is exhausted");
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(*app->unified_events))
            return application_fail(error, QA_ERROR_MEMORY, "Source projection extent overflows");
        void *rows = realloc(app->unified_events, capacity * sizeof(*app->unified_events));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Source event projection");
        app->unified_events = rows; app->unified_event_capacity = capacity;
    }
    record.order = app->unified_event_sequence++;
    if (presentation.size) record.presentation_sequence = app->presentation_event_sequence++;
    if (simulation.size) record.simulation_sequence = app->simulation_event_sequence++;
    app->unified_events[app->unified_event_count++] = record;
    return true;
}

static bool string_id(application_unified_json *j, qa_application *app, qa_string_id id, qa_error *error)
{
    qa_bytes value = qa_strings_text(qa_session_strings(app->session), id);
    qa_buffer quoted = {0};
    bool ok = (!id || value.data) && qa_json_quote(value, &quoted, error) &&
        application_unified_json_append(j, (qa_bytes){quoted.data, quoted.size}, error);
    qa_buffer_free(&quoted);
    return ok || application_fail(error, QA_ERROR_FORMAT, "Source damage identity is outside its real session table");
}

static bool optional_actor(application_unified_json *j, qa_actor_id actor, qa_error *error)
{ return actor.registry ? application_unified_json_actor(j, actor, error) : application_unified_json_text(j, "null", error); }

#define TEXT(s) application_unified_json_text(j, (s), error)
#define NUMBER(n) application_unified_json_number(j, (n), error)
#define ID(n) string_id(j, app, (n), error)
#define VECTOR(v) application_unified_json_vector(j, (v), error)

static bool cause_write(application_unified_json *j, qa_application *app,
    const qa_damage_cause *cause, qa_error *error)
{
    if (cause->kind == QA_CAUSE_Q1) {
        bool ok = TEXT("{\"kind\":\"q1\",\"deathType\":") && ID(cause->source.q1.death_type);
        if (ok && cause->source.q1.armor != QA_Q1_ARMOR_NORMAL)
            ok = TEXT(cause->source.q1.armor == QA_Q1_ARMOR_BYPASS ?
                ",\"armorEffect\":\"bypass\"" : ",\"armorEffect\":\"half-effectiveness\"");
        return ok && TEXT("}");
    }
    if (cause->kind == QA_CAUSE_Q2 || cause->kind == QA_CAUSE_Q3) {
        bool q2 = cause->kind == QA_CAUSE_Q2;
        bool ok = TEXT(q2 ? "{\"kind\":\"q2\",\"meansOfDeath\":" : "{\"kind\":\"q3\",\"meansOfDeath\":") &&
            NUMBER(q2 ? cause->source.q2.means_of_death : cause->source.q3.means_of_death) &&
            TEXT(",\"damageFlags\":") && NUMBER(q2 ? cause->source.q2.flags : cause->source.q3.flags);
        if (ok && q2 && cause->source.q2.native == QA_Q2_CAUSE_CLASSIC) {
            static const char *const games[] = {"base", "xatrix", "rogue", "ctf"};
            if (cause->source.q2.classic_product >= sizeof(games) / sizeof(*games))
                return application_fail(error, QA_ERROR_FORMAT, "Damage cause has no actual classic Q2 game");
            ok = TEXT(",\"native\":{\"edition\":\"classic\",\"game\":") &&
                application_unified_json_string(j, games[cause->source.q2.classic_product], error) &&
                TEXT(",\"value\":") && NUMBER(cause->source.q2.native_value) && TEXT("}");
        } else if (ok && q2 && cause->source.q2.native == QA_Q2_CAUSE_RERELEASE) {
            ok = TEXT(",\"native\":{\"edition\":\"rerelease\",\"id\":") && NUMBER(cause->source.q2.native_value) &&
                TEXT(cause->source.q2.friendly_fire ? ",\"friendlyFire\":true" : ",\"friendlyFire\":false") &&
                TEXT(cause->source.q2.no_point_loss ? ",\"noPointLoss\":true}" : ",\"noPointLoss\":false}");
        }
        return ok && TEXT("}");
    }
    static const char *const hazards[] = {"fall", "drown", "lava", "slime", "crush", "trigger"};
    if (cause->kind != QA_CAUSE_ENVIRONMENT || (unsigned)cause->source.hazard >= sizeof(hazards) / sizeof(*hazards))
        return application_fail(error, QA_ERROR_FORMAT, "Damage cause lost its actual source variant");
    return TEXT("{\"kind\":\"environment\",\"hazard\":") &&
        application_unified_json_string(j, hazards[cause->source.hazard], error) && TEXT("}");
}

static bool request_write(application_unified_json *j, qa_application *app,
    const qa_damage_request *request, qa_error *error)
{
    const qa_attack *attack = &request->attack;
    application_provider *source = source_provider(app, attack->weapon_provider);
    if (!source || !source->launch)
        return application_fail(error, QA_ERROR_FORMAT, "Damage attack lost its genuine source weapon provider");
    qa_clock_kind clock = source->launch->selection.clock.kind;
    bool ms = clock == QA_CLOCK_Q2_RERELEASE || clock == QA_CLOCK_Q3;
    double time = (double)attack->time_ns / (ms ? 1e6 : 1e9);
    if (clock == QA_CLOCK_Q3) {
        uint32_t word = (uint32_t)(attack->time_ns / UINT64_C(1000000));
        int32_t signed_word; memcpy(&signed_word, &word, sizeof(word));
        time = signed_word;
    }
    bool ok = TEXT("{\"attack\":{\"sequence\":") && application_unified_json_natural(j, attack->sequence, error) &&
        TEXT(ms ? ",\"time\":{\"kind\":\"milliseconds\",\"value\":" : ",\"time\":{\"kind\":\"seconds\",\"value\":") &&
        NUMBER(time) && TEXT("},\"attacker\":") &&
        optional_actor(j, attack->attacker, error) && TEXT(",\"inflictor\":") && optional_actor(j, attack->inflictor, error);
    if (ok && attack->projectile.registry)
        ok = TEXT(",\"originatingProjectile\":") && application_unified_json_actor(j, attack->projectile, error);
    if (ok) ok = TEXT(",\"weapon\":") && (attack->weapon ? ID(attack->weapon) : TEXT("null")) &&
        TEXT(",\"weaponProvider\":") && ID(attack->weapon_provider);
    if (ok && attack->powerup_applied && attack->powerup_owner)
        ok = TEXT(",\"damagePowerupOwner\":") && ID(attack->powerup_owner);
    if (ok) ok = TEXT(",\"combatProvider\":") && ID(attack->combat_provider) &&
        TEXT(",\"inventoryProvider\":") && ID(attack->inventory_provider) && TEXT(",\"movementProvider\":") && ID(attack->movement_provider) &&
        TEXT(",\"cause\":") && cause_write(j, app, &attack->cause, error) && TEXT("},\"target\":") &&
        application_unified_json_actor(j, request->target, error) && TEXT(",\"amount\":") && NUMBER(request->amount) &&
        TEXT(",\"knockback\":") && NUMBER(request->knockback) && TEXT(",\"direction\":") && VECTOR(request->direction) &&
        TEXT(",\"point\":") && VECTOR(request->point) && TEXT(",\"normal\":") && VECTOR(request->normal) &&
        TEXT(request->radius ? ",\"delivery\":\"radius\"}" : ",\"delivery\":\"direct\"}");
    return ok;
}

static bool armor_write(application_unified_json *j, qa_application *app, const qa_armor *armor, qa_error *error)
{
    const qa_regular_armor *regular = &armor->regular;
    static const char *const kinds[] = {"none", "q1", "q2", "q3", "source"};
    if ((unsigned)regular->kind >= sizeof(kinds) / sizeof(*kinds) || armor->powered.kind > QA_POWER_SHIELD)
        return application_fail(error, QA_ERROR_FORMAT, "Damage journal armor has no actual source variant");
    bool ok = TEXT("{\"regular\":{\"kind\":") && application_unified_json_string(j, kinds[regular->kind], error);
    if (ok && regular->kind != QA_ARMOR_NONE) ok = TEXT(",\"points\":") && NUMBER(regular->points);
    if (ok && regular->kind == QA_ARMOR_Q1) ok = TEXT(",\"absorption\":") && NUMBER(regular->protection.q1_absorption);
    if (ok && regular->kind == QA_ARMOR_Q2) ok = TEXT(",\"normalProtection\":") && NUMBER(regular->protection.q2.normal) &&
        TEXT(",\"energyProtection\":") && NUMBER(regular->protection.q2.energy);
    if (ok && regular->kind == QA_ARMOR_Q3) ok = TEXT(",\"protection\":") && NUMBER(regular->protection.q3_protection);
    if (ok && (regular->kind == QA_ARMOR_Q1 || regular->kind == QA_ARMOR_Q2 || regular->kind == QA_ARMOR_SOURCE))
        ok = TEXT(",\"item\":") && (regular->item ? ID(regular->item) : TEXT("null"));
    if (ok) ok = TEXT("},\"powered\":{\"kind\":") && application_unified_json_string(j,
        armor->powered.kind == QA_POWER_NONE ? "none" : armor->powered.kind == QA_POWER_SCREEN ? "screen" : "shield", error);
    if (ok && armor->powered.kind != QA_POWER_NONE) ok = TEXT(",\"cells\":") && NUMBER(armor->powered.cells);
    return ok && TEXT("}}");
}

static bool mutation_write(application_unified_json *j, qa_application *app,
    const qa_damage_mutation *mutation, qa_error *error)
{
    switch (mutation->kind) {
    case QA_MUTATION_HEALTH:
        return TEXT("{\"kind\":\"health\",\"before\":") && NUMBER(mutation->value.health.before) &&
            TEXT(",\"after\":") && NUMBER(mutation->value.health.after) && TEXT("}");
    case QA_MUTATION_ARMOR:
        return TEXT("{\"kind\":\"armor\",\"before\":") && armor_write(j, app, &mutation->value.armor.before, error) &&
            TEXT(",\"after\":") && armor_write(j, app, &mutation->value.armor.after, error) && TEXT("}");
    case QA_MUTATION_SOURCE_VELOCITY:
        return TEXT("{\"kind\":\"source-velocity\",\"before\":") && VECTOR(mutation->value.velocity.before) &&
            TEXT(",\"after\":") && VECTOR(mutation->value.velocity.after) && TEXT(",\"movementProvider\":") &&
            ID(mutation->value.velocity.movement) && TEXT("}");
    case QA_MUTATION_IMPULSE:
        return TEXT("{\"kind\":\"impulse\",\"impulse\":") && VECTOR(mutation->value.impulse.value) &&
            TEXT(",\"movementProvider\":") && ID(mutation->value.impulse.movement) && TEXT("}");
    }
    return application_fail(error, QA_ERROR_FORMAT, "Damage journal mutation has no actual source variant");
}

bool application_unified_damage_emit(qa_application *app, const qa_damage_outcome *outcome, qa_error *error)
{
    if (!app || !outcome) return application_fail(error, QA_ERROR_ARGUMENT, "Damage event has no actual outcome");
    application_unified_json buffer = {0};
    application_unified_json *j = &buffer;
    bool ok = TEXT("{\"kind\":\"damage\",\"outcome\":") &&
        TEXT(outcome->stale ? "{\"kind\":\"stale-target\",\"request\":" : "{\"kind\":\"committed\",\"decision\":{\"request\":") &&
        request_write(j, app, &outcome->request, error);
    if (ok && !outcome->stale) {
        ok = TEXT(",\"mutations\":[");
        for (size_t i = 0; ok && i < outcome->mutation_count; ++i)
            ok = (!i || TEXT(",")) && mutation_write(j, app, outcome->mutations + i, error);
        static const char *const reactions[] = {"none", "pain", "death"};
        if ((unsigned)outcome->result.reaction >= sizeof(reactions) / sizeof(*reactions))
            ok = application_fail(error, QA_ERROR_FORMAT, "Damage result has no actual reaction");
        if (ok) ok = TEXT("],\"appliedDamage\":") && NUMBER(outcome->result.applied_damage) && TEXT(",\"reaction\":") &&
            application_unified_json_string(j, reactions[outcome->result.reaction], error);
        if (ok && outcome->result.has_feedback && outcome->result.feedback_family == QA_GAME_Q2)
            ok = TEXT(",\"feedback\":{\"kind\":\"q2\",\"powerArmor\":") && NUMBER(outcome->result.power_saved) &&
                TEXT(",\"armor\":") && NUMBER(outcome->result.armor_saved) && TEXT(",\"blood\":") && NUMBER(outcome->result.blood) &&
                TEXT(",\"knockback\":") && NUMBER(outcome->result.knockback) && TEXT("}");
        if (ok && outcome->result.has_feedback && outcome->result.feedback_family == QA_GAME_Q3)
            ok = TEXT(",\"feedback\":{\"kind\":\"q3\",\"knockback\":") && NUMBER(outcome->result.knockback) &&
                TEXT(outcome->result.battlesuit ? ",\"battlesuit\":true}" : ",\"battlesuit\":false}");
        if (ok) ok = TEXT(outcome->survived ? "},\"survived\":true" : "},\"survived\":false");
    }
    if (ok) ok = TEXT("}}") && application_unified_event_emit(app, outcome->request.attack.weapon_provider,
        (qa_bytes){0}, (qa_bytes){buffer.bytes.data, buffer.bytes.size}, (qa_actor_id){0}, (qa_actor_id){0},
        outcome->request.attack.time_ns, 0, false, false, error);
    application_unified_json_dispose(&buffer);
    return ok;
}
#undef TEXT
#undef NUMBER
#undef ID
#undef VECTOR

bool application_event_journal_reserve(qa_application *app, qa_error *error)
{
    if (!app || app->event_sequence >= QA_UNIFIED_SAFE_INTEGER)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source event sequence is exhausted");
    if (app->event_journal_count < app->event_journal_capacity) return true;
    size_t capacity = app->event_journal_capacity ? app->event_journal_capacity : 64;
    if (app->event_journal_capacity) {
        if (capacity > SIZE_MAX / 2)
            return application_fail(error, QA_ERROR_MEMORY, "Source event journal capacity is exhausted");
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*app->event_journal))
        return application_fail(error, QA_ERROR_MEMORY, "Source event journal extent overflows");
    void *rows = realloc(app->event_journal, capacity * sizeof(*app->event_journal));
    if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Source event order");
    app->event_journal = rows;
    app->event_journal_capacity = capacity;
    return true;
}

void application_event_journal_append(qa_application *app, application_event_queue queue,
    size_t index, qa_actor_owner owner)
{
    application_event_journal_record record = {.sequence = app->event_sequence++,
        .queue = queue, .index = index};
    qa_clock_state clock;
    if (owner && qa_session_clock(app->session, owner, &clock)) {
        record.frame = clock.frame;
        record.has_frame = true;
    }
    app->event_journal[app->event_journal_count++] = record;
}

static bool payload_begin(application_unified_json *json, qa_bytes payload, qa_error *error)
{
    while (payload.size && (payload.data[payload.size - 1] == ' ' || payload.data[payload.size - 1] == '\n' ||
        payload.data[payload.size - 1] == '\r' || payload.data[payload.size - 1] == '\t')) --payload.size;
    if (!payload.size || payload.data[payload.size - 1] != '}')
        return application_fail(error, QA_ERROR_FORMAT, "Source event lost its retained object");
    --payload.size;
    return application_unified_json_append(json, payload, error);
}

/* Presentation events use JSON.stringify, whereas SimulationEvents use the
 * checkpoint value codec. Preserve that genuine distinction at the wire. */
static bool presentation_wire(application_unified_json *out, const qa_json_document *json,
    qa_json_id value, unsigned depth, qa_error *error)
{
    if (depth > 128) return application_fail(error, QA_ERROR_FORMAT, "Source presentation nesting exceeds its wire extent");
    qa_json_kind kind = qa_json_type(json, value);
    if (kind == QA_JSON_OBJECT && qa_json_string_equal(json, qa_json_get(json, value, "$qts"), "number")) {
        qa_json_id number = qa_json_get(json, value, "value");
        return application_unified_json_text(out, qa_json_string_equal(json, number, "-0") ? "0" : "null", error);
    }
    if (kind != QA_JSON_ARRAY && kind != QA_JSON_OBJECT)
        return application_unified_json_append(out, qa_json_source(json, value), error);
    bool object = kind == QA_JSON_OBJECT;
    if (!application_unified_json_text(out, object ? "{" : "[", error)) return false;
    for (size_t i = 0; i < qa_json_size(json, value); ++i) {
        if (i && !application_unified_json_text(out, ",", error)) return false;
        if (object && (!application_unified_json_append(out, qa_json_source(json, qa_json_key_at(json, value, i)), error) ||
            !application_unified_json_text(out, ":", error))) return false;
        if (!presentation_wire(out, json, qa_json_at(json, value, i), depth + 1, error)) return false;
    }
    return application_unified_json_text(out, object ? "}" : "]", error);
}

static bool presentation_write(application_unified_json *j, qa_application *app,
    const application_unified_event_record *row, qa_error *error)
{
    const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
    double seconds = (double)row->time_ns / 1e9;
    if (row->presentation_clock == QA_CLOCK_Q3) {
        uint32_t word = (uint32_t)(row->time_ns / 1000000);
        int32_t signed_word; memcpy(&signed_word, &word, sizeof(word));
        seconds = (double)signed_word / 1000;
    }
    bool ok = payload_begin(j, row->presentation, error) &&
        application_unified_json_text(j, ",\"sequence\":", error) &&
        application_unified_json_natural(j, row->presentation_sequence, error) &&
        application_unified_json_text(j, ",\"content\":", error) &&
        application_unified_json_string(j, content, error) &&
        application_unified_json_text(j, ",\"seconds\":", error) &&
        application_unified_json_number(j, seconds, error);
    if (ok && row->recipient.registry) ok = application_unified_json_text(j, ",\"recipient\":", error) &&
        application_unified_json_actor(j, row->recipient, error);
    if (ok) ok = application_unified_json_text(j, ",\"sourceEntity\":", error) &&
        (row->has_source_entity ? application_unified_json_number(j, row->source_entity, error) :
            application_unified_json_text(j, "null", error)) && application_unified_json_text(j, "}", error);
    return ok;
}

static bool simulation_write(application_unified_json *j,
    const application_unified_event_record *row, qa_error *error)
{
    bool seconds = row->clock != QA_CLOCK_Q2_RERELEASE && row->clock != QA_CLOCK_Q3;
    bool ok = application_unified_json_text(j, "{\"sequence\":", error) &&
        application_unified_json_natural(j, row->simulation_sequence, error) &&
        application_unified_json_text(j, seconds ? ",\"time\":{\"kind\":\"seconds\",\"value\":" :
            ",\"time\":{\"kind\":\"milliseconds\",\"value\":", error) &&
        application_unified_json_number(j, (double)row->simulation_time_ns / (seconds ? 1e9 : 1e6), error) &&
        application_unified_json_text(j, "},\"audience\":", error);
    if (ok && row->simulation_recipient.registry) ok =
        application_unified_json_text(j, "{\"kind\":\"client\",\"client\":{\"slot\":", error) &&
        application_unified_json_natural(j, row->client.slot, error) &&
        application_unified_json_text(j, ",\"generation\":", error) &&
        application_unified_json_natural(j, row->client.generation, error) &&
        application_unified_json_text(j, "}}", error);
    else if (ok) ok = application_unified_json_text(j, "{\"kind\":\"world\"}", error);
    if (ok) ok = application_unified_json_text(j, ",\"payload\":", error);
    if (ok && row->link_presentation) ok = payload_begin(j, row->simulation, error) &&
        application_unified_json_text(j, ",\"sourcePresentationSequence\":", error) &&
        application_unified_json_natural(j, row->presentation_sequence, error) &&
        application_unified_json_text(j, "}", error);
    else if (ok) ok = application_unified_json_append(j, row->simulation, error);
    return ok && application_unified_json_text(j, "}", error);
}

static bool world_text_read(qa_application *app, const application_unified_source *source,
    qa_unified_document **out, qa_error *error)
{
    double now = (double)source->frame.time_ns / 1e9;
    bool changed = app->unified_world_text_map != source->map_revision;
    size_t count = changed ? 0 : app->unified_world_text_count, kept = 0;
    for (size_t i = 0; !changed && i < count; ++i) {
        const application_unified_world_text *row = app->unified_world_text + i;
        changed = (row->timed && row->expires <= now) ||
            (!row->timed && (!row->observed || row->first_frame != source->frame.number));
    }
    if (changed && app->unified_world_text_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "World text revision is exhausted");
    for (size_t i = 0; i < count; ++i) {
        application_unified_world_text row = app->unified_world_text[i];
        if ((row.timed && row.expires <= now) ||
            (!row.timed && row.observed && row.first_frame != source->frame.number)) { changed = true; continue; }
        if (!row.timed && !row.observed) {
            row.observed = true; row.first_frame = source->frame.number; changed = true;
        }
        app->unified_world_text[kept++] = row;
    }
    if (changed) ++app->unified_world_text_revision;
    app->unified_world_text_count = kept; app->unified_world_text_map = source->map_revision;
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "[", error);
    for (size_t i = 0; ok && i < kept; ++i) {
        const application_unified_world_text *row = app->unified_world_text + i;
        const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
        qa_bytes value = qa_strings_text(qa_session_strings(app->session), row->text);
        qa_buffer quoted = {0};
        ok = qa_json_quote(value, &quoted, error) && (!i || application_unified_json_text(&j, ",", error)) &&
            application_unified_json_text(&j, "{\"content\":", error) &&
            application_unified_json_string(&j, content, error) &&
            application_unified_json_text(&j, ",\"text\":", error) &&
            application_unified_json_append(&j, (qa_bytes){quoted.data, quoted.size}, error) &&
            application_unified_json_text(&j, ",\"origin\":", error) &&
            application_unified_json_vector(&j, row->origin, error) &&
            application_unified_json_text(&j, ",\"color\":{\"x\":", error) &&
            application_unified_json_number(&j, row->color.x, error) &&
            application_unified_json_text(&j, ",\"y\":", error) && application_unified_json_number(&j, row->color.y, error) &&
            application_unified_json_text(&j, ",\"z\":", error) && application_unified_json_number(&j, row->color.z, error) &&
            application_unified_json_text(&j, ",\"w\":", error) && application_unified_json_number(&j, row->alpha, error) &&
            application_unified_json_text(&j, "},\"cellSize\":", error) &&
            application_unified_json_number(&j, row->cell_size, error) &&
            application_unified_json_text(&j, ",\"distanceCullFactor\":0.004,\"orientation\":", error);
        if (ok) ok = row->billboard ? application_unified_json_text(&j, "{\"kind\":\"billboard\"}", error) :
            (application_unified_json_text(&j, "{\"kind\":\"fixed\",\"angles\":", error) &&
             application_unified_json_vector(&j, row->angles, error) && application_unified_json_text(&j, "}", error));
        if (ok) ok = application_unified_json_text(&j, row->depth_test ?
            ",\"depthTest\":true,\"font\":\"classic\"}" : ",\"depthTest\":false,\"font\":\"classic\"}", error);
        qa_buffer_free(&quoted);
    }
    if (ok) ok = application_unified_json_text(&j, "]", error) && qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){j.bytes.data, j.bytes.size}, out, error);
    application_unified_json_dispose(&j);
    return ok;
}

bool application_unified_events_read(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    uint64_t after, application_unified_events *out, qa_error *error)
{
    if (!app || !source || !player || !out || !epoch || after > app->unified_event_sequence ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, recipient, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source event projection has no current physical recipient");
    application_unified_events result = {.application = app, .source = *source, .recipient = recipient,
        .player = *player, .generation = app->protocol_events_generation,
        .through = app->unified_event_sequence, .count = app->unified_event_count,
        .resource_count = app->unified_event_resource_count,
        .registration_revision = app->unified_event_registration_revision};
    application_unified_json presentation = {0}, simulation = {0}, control = {0}, wire = {0};
    size_t presentations = 0, simulations = 0;
    size_t *references = result.resource_count ? calloc(result.resource_count, sizeof(*references)) : NULL;
    size_t reference_count = 0;
    bool ok = (!result.resource_count || references) &&
        application_unified_json_text(&presentation, "[", error) && application_unified_json_text(&simulation, "[", error);
    if (result.resource_count && !references)
        application_fail(error, QA_ERROR_MEMORY, "Retaining actual referenced Source sounds");
    for (size_t i = 0; ok && i < result.count; ++i) {
        const application_unified_event_record *row = app->unified_events + i;
        if (row->order < after) continue;
        if (row->presentation.size && (!row->recipient.registry || qa_actor_id_equal(row->recipient, player->actor))) {
            ok = (!presentations || application_unified_json_text(&presentation, ",", error)) &&
                presentation_write(&presentation, app, row, error);
            ++presentations;
        }
        if (ok && row->simulation.size && (!row->simulation_recipient.registry ||
            (qa_actor_id_equal(row->simulation_recipient, player->actor) && row->client.owner == recipient.owner &&
             row->client.slot == recipient.slot && row->client.generation == recipient.generation))) {
            ok = (!simulations || application_unified_json_text(&simulation, ",", error)) &&
                simulation_write(&simulation, row, error);
            ++simulations;
            qa_unified_document *payload = NULL;
            if (ok) ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT, row->simulation, &payload, error);
            const qa_json_document *json = qa_unified_document_json(payload);
            qa_json_id root = qa_unified_document_root(payload);
            if (ok && qa_json_string_equal(json, qa_json_get(json, root, "kind"), "sound")) {
                bool found = false;
                for (size_t n = 0; n < result.resource_count; ++n) {
                    if (!qa_json_string_equal(json, qa_json_get(json, root, "resource"), app->unified_event_resources[n].id)) continue;
                    found = true;
                    size_t r = 0;
                    while (r < reference_count && references[r] != n) ++r;
                    if (r == reference_count) references[reference_count++] = n;
                    break;
                }
                if (!found) ok = application_fail(error, QA_ERROR_FORMAT, "Source sound lost its previously registered dictionary resource");
            }
            qa_unified_document_destroy(payload);
        }
    }
    qa_unified_document *events = NULL;
    qa_unified_document *presentation_value = NULL;
    qa_buffer encoded = {0}, bytes = {0}, simulation_encoded = {0}, simulation_bytes = {0};
    if (ok) ok = application_unified_json_text(&presentation, "]", error) &&
        application_unified_json_text(&simulation, "]", error) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){presentation.bytes.data, presentation.bytes.size}, &presentation_value, error) &&
        presentation_wire(&wire, qa_unified_document_json(presentation_value), qa_unified_document_root(presentation_value), 0, error) &&
        qa_unified_document_create(QA_UNIFIED_EVENTS_DOCUMENT, (qa_bytes){wire.bytes.data, wire.bytes.size}, &events, error) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){simulation.bytes.data, simulation.bytes.size}, &result.simulation, error) &&
        world_text_read(app, source, &result.world_text, error);
    result.world_text_revision = app->unified_world_text_revision;
    if (ok && (reference_count || presentations || simulations)) {
        result.controls = calloc(2, sizeof(*result.controls));
        if (!result.controls) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining real Source prerequisite controls");
    }
    if (ok && reference_count) {
        qa_unified_document **keys = calloc(reference_count, sizeof(*keys));
        if (!keys) ok = application_fail(error, QA_ERROR_MEMORY, "Projecting Source resource dictionary");
        for (size_t i = 0; ok && i < reference_count; ++i) {
            const application_unified_event_resource *row = app->unified_event_resources + references[i];
            ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){row->key.data, row->key.size}, keys + i, error);
        }
        if (ok) ok = application_unified_resource_control(epoch, (const qa_unified_document *const *)keys,
            reference_count, result.controls + result.control_count, error);
        if (ok) ++result.control_count;
        for (size_t i = 0; keys && i < reference_count; ++i) qa_unified_document_destroy(keys[i]);
        free(keys);
    }
    if (ok && (presentations || simulations)) {
        ok = qa_unified_document_encode(events, &encoded, error) &&
            qa_unified_checkpoint_bytes((qa_bytes){encoded.data, encoded.size}, &bytes, error) &&
            qa_unified_document_encode(result.simulation, &simulation_encoded, error) &&
            qa_unified_checkpoint_bytes((qa_bytes){simulation_encoded.data, simulation_encoded.size}, &simulation_bytes, error) &&
            application_unified_json_text(&control, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"events\",\"epoch\":", error) &&
            application_unified_json_natural(&control, epoch, error) && application_unified_json_text(&control, ",\"frame\":", error) &&
            application_unified_json_natural(&control, source->frame.number, error) &&
            application_unified_json_text(&control, ",\"payload\":", error) &&
            application_unified_json_append(&control, (qa_bytes){bytes.data, bytes.size}, error) &&
            application_unified_json_text(&control, ",\"simulation\":", error) &&
            application_unified_json_append(&control, (qa_bytes){simulation_bytes.data, simulation_bytes.size}, error) &&
            application_unified_json_text(&control, "}}", error) &&
            qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, (qa_bytes){control.bytes.data, control.bytes.size},
                result.controls + result.control_count, error);
        if (ok) ++result.control_count;
    }
    if (ok && !application_unified_events_current(&result))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Source event projection changed its actual owner");
    qa_unified_document_destroy(events); qa_buffer_free(&encoded); qa_buffer_free(&bytes);
    qa_unified_document_destroy(presentation_value); application_unified_json_dispose(&wire);
    qa_buffer_free(&simulation_encoded); qa_buffer_free(&simulation_bytes); free(references);
    application_unified_json_dispose(&presentation); application_unified_json_dispose(&simulation);
    application_unified_json_dispose(&control);
    if (!ok) { application_unified_events_dispose(&result); return false; }
    *out = result;
    return true;
}

bool application_unified_events_current(const application_unified_events *events)
{
    return events && events->application &&
        application_unified_source_current(events->application, &events->source) &&
        application_unified_player_current(events->application, events->recipient, &events->player) &&
        events->generation == events->application->protocol_events_generation &&
        events->through == events->application->unified_event_sequence &&
        events->count == events->application->unified_event_count &&
        events->resource_count == events->application->unified_event_resource_count &&
        events->registration_revision == events->application->unified_event_registration_revision &&
        events->world_text_revision == events->application->unified_world_text_revision;
}

void application_unified_events_dispose(application_unified_events *events)
{
    if (!events) return;
    qa_unified_document_destroy(events->simulation);
    qa_unified_document_destroy(events->world_text);
    for (size_t i = 0; i < events->control_count; ++i)
        qa_unified_document_destroy(events->controls[i]);
    free(events->controls);
    *events = (application_unified_events){0};
}
