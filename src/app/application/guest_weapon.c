#include "guest_q3_private.h"
#include "guest_qc_profile.h"

static bool canonical(application_provider *provider, qa_actor_id actor,
                         const char *name, qa_item_id *out, qa_error *error)
{
    qa_strings *strings = qa_session_strings(provider->application->session);
    qa_item_id item = name ? qa_strings_find(strings,
        (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
    if (!item) return application_fail(error, QA_ERROR_NOT_FOUND, "Source selected weapon has no canonical inventory identity");
    qa_inventory_entry entry;
    if (!qa_inventory_entry_read(provider->application->inventory, actor, item, &entry, error)) return false;
    *out = item;
    return true;
}

bool application_guest_weapon_read(application_provider *provider, qa_actor_id actor,
                                      qa_item_id *out, qa_error *error)
{
    if (!provider || !provider->application || !out || !provider->constructed ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source selected weapon requires a live canonical actor");
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (!engine) return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC weapon owner is absent");
        int32_t reference;
        if (!application_qc_reference(engine, actor, &reference, error)) return false;
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        const qa_qc_definition *field = profile ? profile->weapon_field :
            qa_qc_program_find_field(provider->state.qc.program, "weapon");
        if (!field || field->type != QA_QC_FLOAT)
            return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC selected weapon has no declared source observer");
        float value;
        if (!qa_qc_entity_float(provider->state.qc.instance, reference, field->offset, &value, error)) return false;
        if (!isfinite(value)) return application_fail(error, QA_ERROR_FORMAT, "QuakeC selected weapon is not finite");
        if (profile) {
            for (size_t i = 0; i < profile->weapon_count; ++i)
                if (profile->weapon_values[i].value == value) {
                    const char *name = qa_strings_cstr(qa_session_strings(provider->application->session),
                        profile->weapon_values[i].item);
                    return canonical(provider, actor, name, out, error);
                }
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC selected weapon value is outside its declaration");
        }
        if (value == 0) { *out = 0; return true; }
        if (value < 0 || (double)value > UINT32_MAX || floorf(value) != value)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC selected weapon exceeds its source bit representation");
        qa_q1_weapon weapon;
        if (!qa_q1_weapon_source(application_q1_program(provider->product->campaign), (uint32_t)value, &weapon))
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC selected weapon is outside the selected original program");
        return canonical(provider, actor, qa_q1_weapon_identity(weapon), out, error);
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    uint32_t slot;
    if (!engine || !engine->game || !application_q3_guest_actor_client(provider, actor, &slot))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 selected weapon has no begun source client");
    qa_q3_player player;
    if (!qa_q3_host_source_player(engine->game->host, slot, &player, error)) return false;
    if (player.weapon == 0) { *out = 0; return true; }
    const char *name = qa_q3_weapon_identity_name((qa_q3_weapon)player.weapon);
    if (!name) return application_fail(error, QA_ERROR_FORMAT, "Q3 selected weapon exceeds the admitted source namespace");
    char identity[64];
    int length = snprintf(identity, sizeof(identity), "q3:weapon/%s", name);
    if (length < 0 || (size_t)length >= sizeof(identity))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 selected weapon identity exceeds its canonical representation");
    return canonical(provider, actor, identity, out, error);
}
