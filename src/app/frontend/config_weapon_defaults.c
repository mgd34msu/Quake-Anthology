#include "config_weapon_defaults.h"
#include <stdio.h>

static bool append(frontend_config_weapon_catalog *catalog, qa_strings *strings,
    qa_actor_owner owner, const char *item, const char *label, qa_error *error) {
    qa_item_id id;
    if (!qa_strings_intern_cstr(strings, item, &id, error))
        return false;
    catalog->items[catalog->count++] = (qa_item_definition){
        .item = id, .owner = owner, .label = label, .weapon = true, .actions = QA_ITEM_USE};
    return true;
}

bool frontend_config_weapon_defaults(qa_application *app, const qa_launch_snapshot *snapshot,
    qa_launch_scope scope, qa_strings *strings, frontend_config_weapon_catalog *out,
    qa_error *error) {
    if (!strings || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Selected weapon defaults need catalog storage");
        return false;
    }
    *out = (frontend_config_weapon_catalog){0};
    frontend_config_weapon_catalog result = {0};
    bool found;
    qa_actor_owner owner;
    qa_q1_program program;
    if (!qa_application_startup_q1_arsenal_program(app, snapshot, scope, &program,
        &owner, &found, error))
        return false;
    if (found) {
        for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
            qa_q1_weapon_profile identity;
            if (qa_q1_weapon_profile_identity(program, (qa_q1_weapon)i, &identity) &&
                !append(&result, strings, owner, identity.item, identity.label, error))
                return false;
        }
        *out = result;
        return true;
    }
    qa_q2_options options;
    if (!qa_application_startup_q2_arsenal_options(app, snapshot, scope, &options, &found, error))
        return false;
    if (found) {
        for (unsigned i = 1; i < QA_Q2_WEAPON_COUNT; ++i) {
            qa_q2_weapon_identity identity;
            if (qa_q2_weapon_profile_identity(&options, (qa_q2_weapon)i, &identity) &&
                !append(&result, strings, options.owner, identity.item, identity.name, error))
                return false;
        }
        *out = result;
        return true;
    }
    qa_q3_product product;
    if (!qa_application_startup_q3_arsenal_product(app, snapshot, scope, &product,
        &owner, &found, error))
        return false;
    if (found) {
        size_t count;
        const qa_q3_item *items = qa_q3_items(product, &count);
        for (size_t i = 1; i < count; ++i) {
            if (items[i].kind != QA_Q3_ITEM_WEAPON)
                continue;
            char identity[64];
            snprintf(identity, sizeof(identity), "q3:weapon/%s",
                qa_q3_weapon_identity_name((qa_q3_weapon)items[i].tag));
            if (!append(&result, strings, owner, identity, items[i].name, error))
                return false;
        }
    }
    *out = result;
    return true;
}
