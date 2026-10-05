#include "config_weapon_defaults.h"
#include "../application/arsenal_profile.h"
#include <stdio.h>

static bool append(frontend_config_weapon_binding_catalog *catalog,
    const char *item, const char *label, unsigned q1_impulse, unsigned q3_weapon, qa_error *error) {
    if (catalog->count == FRONTEND_CONFIG_WEAPON_CAPACITY) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Selected weapon catalog exceeds native declarations");
        return false;
    }
    catalog->items[catalog->count++] = (qa_input_weapon_binding){
        .id = item, .label = label, .q1_impulse = q1_impulse, .q3_weapon = q3_weapon};
    return true;
}

static bool collect(const application_arsenal_profile *profile,
    frontend_config_weapon_binding_catalog *out, qa_error *error) {
    switch (profile->family) {
    case QA_GAME_Q1:
        for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
            qa_q1_weapon_profile identity;
            if (qa_q1_weapon_profile_identity(profile->source.q1, (qa_q1_weapon)i, &identity) &&
                !append(out, identity.item, identity.label, i <= QA_Q1_LIGHTNING ? i + 1 : 0, 0, error))
                return false;
        }
        return true;
    case QA_GAME_Q2:
        for (unsigned i = 1; i < QA_Q2_WEAPON_COUNT; ++i) {
            qa_q2_weapon_identity identity;
            if (qa_q2_weapon_profile_identity(&profile->source.q2, (qa_q2_weapon)i, &identity) &&
                !append(out, identity.item, identity.name, 0, 0, error))
                return false;
        }
        return true;
    case QA_GAME_Q3: {
        size_t count;
        const qa_q3_item *items = qa_q3_items(profile->source.q3, &count);
        for (size_t i = 1; i < count; ++i) {
            if (items[i].kind != QA_Q3_ITEM_WEAPON)
                continue;
            const char *name = qa_q3_weapon_identity_name((qa_q3_weapon)items[i].tag);
            if (out->count == FRONTEND_CONFIG_WEAPON_CAPACITY || !name) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Selected Q3 weapon has no catalog identity");
                return false;
            }
            char *identity = out->identities[out->count];
            int length = snprintf(identity, sizeof(out->identities[0]), "q3:weapon/%s", name);
            if (length < 0 || (size_t)length >= sizeof(out->identities[0])) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Selected Q3 weapon identity exceeds catalog storage");
                return false;
            }
            if (!append(out, identity, items[i].name, 0, (unsigned)items[i].tag, error)) return false;
        }
        return true;
    }
    }
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Selected weapon catalog has no native family");
    return false;
}

bool frontend_config_weapon_defaults(qa_application *app, const qa_launch_snapshot *snapshot,
    qa_launch_scope scope, qa_strings *strings, frontend_config_weapon_catalog *out,
    qa_error *error) {
    if (!strings || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Selected weapon defaults need catalog storage");
        return false;
    }
    *out = (frontend_config_weapon_catalog){0};
    application_arsenal_profile profile;
    qa_actor_owner owner;
    bool found;
    if (!application_startup_arsenal_profile(app, snapshot, scope, &profile, &owner, &found, error)) return false;
    if (!found) return true;
    frontend_config_weapon_binding_catalog catalog = {0};
    if (!collect(&profile, &catalog, error)) return false;
    frontend_config_weapon_catalog result = {0};
    for (size_t i = 0; i < catalog.count; ++i) {
        qa_item_id item;
        if (!qa_strings_intern_cstr(strings, catalog.items[i].id, &item, error)) return false;
        result.items[result.count++] = (qa_item_definition){
            .item = item, .owner = owner, .label = catalog.items[i].label, .weapon = true, .actions = QA_ITEM_USE};
    }
    *out = result;
    return true;
}

bool frontend_config_weapon_bindings(const qa_launch_draft *draft, qa_launch_scope scope,
    frontend_config_weapon_binding_catalog *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Draft weapon bindings need catalog storage");
        return false;
    }
    *out = (frontend_config_weapon_binding_catalog){0};
    application_arsenal_profile profile;
    bool found;
    return application_draft_arsenal_profile(draft, scope, &profile, &found, error) &&
        (!found || collect(&profile, out, error));
}
