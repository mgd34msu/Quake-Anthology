#include "guest_q3_functions.h"
#include "guest_input_private.h"
#include "guest_q3_weapons_save.h"
#include "guest_q3_weapons_services.h"
#include "guest_q3_combat.h"
#include "guest_q3_pickups.h"
#include "guest_q3_body_save.h"
#include "qa/source_save.h"

typedef struct function_inventory {
    qa_qvm_saved_function *descriptors;
    size_t input_count, weapons_count, combat_count, pickups_count, equipment_count, body_count, models_count, count;
} function_inventory;

static bool inventory(q3g_role *role, function_inventory *out, qa_error *error)
{
    qa_qvm_saved_function input[7] = {0};
    function_inventory value = {0};
    if (!role || !application_guest_input_descriptors(role, input, &value.input_count, error)) return false;
    value.equipment_count = application_q3_equipment_descriptor_count(role->equipment);
    value.weapons_count = application_q3_weapons_descriptor_count(role->weapons);
    value.combat_count = application_q3_combat_descriptor_count(role->combat);
    value.pickups_count = application_q3_pickups_descriptor_count(role->pickups);
    value.body_count = application_q3_body_descriptor_count(role->body);
    value.models_count = application_q3_weapon_models_descriptor_count(role->weapon_models);
    if (value.weapons_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count ||
        value.combat_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count - value.weapons_count ||
        value.pickups_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count - value.weapons_count - value.combat_count ||
        value.equipment_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count - value.weapons_count - value.combat_count - value.pickups_count ||
        value.body_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count - value.weapons_count - value.combat_count - value.pickups_count - value.equipment_count ||
        value.models_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count - value.weapons_count - value.combat_count - value.pickups_count - value.equipment_count - value.body_count)
        return application_fail(error, QA_ERROR_MEMORY, "Complete source callback inventory exceeds address space");
    value.count = value.input_count + value.weapons_count + value.combat_count + value.pickups_count + value.equipment_count + value.body_count + value.models_count;
    value.descriptors = value.count ? calloc(value.count, sizeof(*value.descriptors)) : NULL;
    if (value.count && !value.descriptors)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete actual source callback descriptors");
    if (value.input_count) memcpy(value.descriptors, input, value.input_count * sizeof(*input));
    qa_qvm_saved_function *weapons = value.weapons_count ? value.descriptors + value.input_count : NULL;
    qa_qvm_saved_function *combat = value.combat_count ? value.descriptors + value.input_count + value.weapons_count : NULL;
    qa_qvm_saved_function *pickups = value.pickups_count ? value.descriptors + value.input_count + value.weapons_count + value.combat_count : NULL;
    qa_qvm_saved_function *equipment = value.equipment_count ? value.descriptors + value.input_count + value.weapons_count + value.combat_count + value.pickups_count : NULL;
    qa_qvm_saved_function *body = value.body_count ? value.descriptors + value.count - value.body_count - value.models_count : NULL;
    qa_qvm_saved_function *models = value.models_count ? value.descriptors + value.count - value.models_count : NULL;
    if (!application_q3_weapons_descriptors(role->weapons, weapons, value.weapons_count, error) ||
        !application_q3_combat_descriptors(role->combat, combat, value.combat_count, error) ||
        (role->pickups && !application_q3_pickups_descriptors(role->pickups, pickups, value.pickups_count, error)) ||
        !application_q3_equipment_descriptors(role->equipment, equipment, value.equipment_count, error) ||
        !application_q3_body_descriptors(role->body, body, value.body_count, error) ||
        !application_q3_weapon_models_descriptors(role->weapon_models, models, value.models_count, error)) {
        free(value.descriptors); return false;
    }
    *out = value;
    return true;
}

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[8] = {'Q','A','G','3','F','N',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','F','N',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic))) return false;
    return !memcmp(magic, expected, sizeof(magic)) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Invalid composed Q3 callback continuation");
}

static bool child(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t length = bytes->size;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated actual source callback child");
        *bytes = (qa_bytes){io->input.data + io->offset, length};
        io->offset += length; return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, length);
}

static bool state_fields(qa_source_save_io *io, application_q3_equipment_saved *state)
{
    uint32_t warning = state->draw.warning;
    bool ok = qa_source_save_actor(io, &state->draw.actor) &&
        qa_source_save_u32(io, &warning) && qa_source_save_bool(io, &state->draw.selected) &&
        qa_source_save_bool(io, &state->draw.view_visible) &&
        qa_source_save_bool(io, &state->hud_requested) && qa_source_save_bool(io, &state->view_requested);
    if (ok && warning > QA_APPLICATION_AMMO_EMPTY)
        return application_fail(io->error, QA_ERROR_FORMAT, "Saved equipment warning leaves its real enum");
    if (ok && io->direction == QA_SOURCE_SAVE_READ) state->draw.warning = (qa_application_ammo_warning)warning;
    return ok;
}

bool application_guest_q3_functions_checkpoint(q3g_role *role, qa_buffer *out, qa_error *error)
{
    function_inventory functions = {0};
    qa_buffer input = {0}, weapons = {0}, requests = {0}, pickups = {0}, body = {0}, models = {0};
    application_q3_equipment_saved state = {0};
    uint64_t combat_sequence = 0;
    if (!out || !inventory(role, &functions, error)) return false;
    bool ok = qa_qvm_checkpoint_functions(role->vm, functions.descriptors, functions.count, error) &&
        application_guest_input_checkpoint(role, &input, error) &&
        application_q3_weapons_checkpoint(role->weapons, &weapons, error) &&
        (!role->weapon_services || application_q3_weapons_services_checkpoint(role->weapon_services, &requests, error)) &&
        (!role->pickups || application_q3_pickups_checkpoint(role->pickups, &pickups, error)) &&
        application_q3_combat_sequence_read(role->combat, &combat_sequence, error) &&
        application_q3_body_checkpoint(role->body, &body, error) &&
        (!role->weapon_models || application_q3_weapon_models_checkpoint(role->weapon_models, &models, error)) &&
        application_q3_equipment_state_read(role->equipment, &state, error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, role->engine->provider->application->session, error) && signature(&io);
    qa_bytes input_bytes = {input.data, input.size}, weapons_bytes = {weapons.data, weapons.size},
        request_bytes = {requests.data, requests.size}, pickup_bytes = {pickups.data, pickups.size}, body_bytes = {body.data, body.size},
        model_bytes = {models.data, models.size};
    bool present = role->equipment != NULL;
    size_t count = functions.equipment_count;
    if (ok) ok = child(&io, &input_bytes) && child(&io, &weapons_bytes) && child(&io, &request_bytes) && child(&io, &pickup_bytes) && child(&io, &body_bytes) && child(&io, &model_bytes) &&
        qa_source_save_count(&io, &functions.combat_count, 3) && qa_source_save_u64(&io, &combat_sequence);
    for (size_t i = 0; ok && i < functions.combat_count; ++i)
        ok = qa_source_save_u64(&io, &functions.descriptors[functions.input_count + functions.weapons_count + i].binding);
    if (ok) ok = qa_source_save_count(&io, &functions.pickups_count, SIZE_MAX);
    for (size_t i = 0; ok && i < functions.pickups_count; ++i)
        ok = qa_source_save_u64(&io, &functions.descriptors[functions.input_count + functions.weapons_count + functions.combat_count + i].binding);
    if (ok) ok = qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i)
        ok = qa_source_save_u64(&io, &functions.descriptors[functions.input_count + functions.weapons_count + functions.combat_count + functions.pickups_count + i].binding);
    if (ok) ok = state_fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_buffer_free(&input); qa_buffer_free(&weapons); qa_buffer_free(&requests); qa_buffer_free(&pickups); qa_buffer_free(&body); qa_buffer_free(&models); free(functions.descriptors);
    return ok;
}

bool application_guest_q3_functions_restore(q3g_role *role, qa_bytes bytes,
    qa_bytes executor, qa_error *error)
{
    function_inventory functions = {0};
    if (!role || !role->engine->restore_pending || role->initialized) {
        return application_fail(error, QA_ERROR_ARGUMENT, "Composed source import requires its isolated uninitialized executor");
    }
    qa_source_save_io io = {0};
    qa_bytes input_bytes = {0}, weapons_bytes = {0}, request_bytes = {0}, pickup_bytes = {0}, body_bytes = {0}, model_bytes = {0};
    application_q3_body_saved body = {0};
    bool ok = qa_source_save_reader(&io, role->engine->provider->application->session, bytes, error) && signature(&io) &&
        child(&io, &input_bytes) && child(&io, &weapons_bytes) && child(&io, &request_bytes) && child(&io, &pickup_bytes) && child(&io, &body_bytes) && child(&io, &model_bytes) &&
        application_q3_body_saved_read(role->body ? &role->artifact->body_profile : NULL, body_bytes, &body, error);
    if (ok && role->body) {
        ok = application_q3_body_destroy(role->body, error);
        if (ok) {
            role->body = NULL;
            application_q3_body_module module = {.session = role->engine->provider->application->session,
                .vm = role->vm, .image = role->image, .profile = &role->artifact->body_profile,
                .receiver = role->engine->provider->owner, .seat = role->seat, .client = role->client_services};
            ok = application_q3_body_create_module(&module, &role->body_services, &body, &role->body, error);
        }
    }
    if (ok) ok = inventory(role, &functions, error);
    if (!ok) { application_q3_body_saved_free(&body); qa_source_save_dispose(&io); return false; }
    qa_qvm_binding *bindings = functions.count ? calloc(functions.count, sizeof(*bindings)) : NULL;
    if (functions.count && !bindings) {
        free(functions.descriptors);
        application_q3_body_saved_free(&body); qa_source_save_dispose(&io);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete saved source callback identities");
    }
    application_guest_input_saved input = {0};
    application_q3_weapons_saved weapons = {0};
    application_q3_equipment_saved state = {0};
    if (ok) ok = application_guest_input_prepare_restore(role, input_bytes, &input, error) &&
        application_q3_weapons_prepare_restore(role->weapons, weapons_bytes, &weapons, error);
    if (ok && body.count != functions.body_count)
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved body hooks differ from their actual enabled constructor");
    if (ok && body.count) memcpy(bindings + functions.count - body.count - functions.models_count, body.bindings, body.count * sizeof(*bindings));
    if (ok && (model_bytes.size != 0) != (role->weapon_models != NULL))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved weapon model owner differs from its real CG declaration");
    qa_qvm_binding model_binding = 0;
    if (ok && role->weapon_models)
        ok = application_q3_weapon_models_restore(role->weapon_models, model_bytes, &model_binding, error);
    if (ok && functions.models_count) bindings[functions.count - functions.models_count] = model_binding;
    if (ok && input.binding_count != functions.input_count)
        ok = application_fail(error, QA_ERROR_FORMAT, "Composed input identities differ from their actual constructor");
    if (ok && functions.input_count)
        memcpy(bindings, input.bindings, functions.input_count * sizeof(*bindings));
    if (ok && (weapons.count != functions.weapons_count ||
        (request_bytes.size != 0) != (role->weapon_services != NULL)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved weapon actions differ from their actual constructor");
    if (ok && functions.weapons_count)
        memcpy(bindings + functions.input_count, weapons.bindings, functions.weapons_count * sizeof(*bindings));
    if (ok && role->weapon_services)
        ok = application_q3_weapons_services_restore(role->weapon_services, request_bytes, error);
    if (ok && (pickup_bytes.size != 0) != (role->pickups != NULL))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved pickup owner differs from its actual Source profile");
    if (ok && role->pickups)
        ok = application_q3_pickups_restore(role->pickups, pickup_bytes, error);
    size_t combat_count = 0;
    uint64_t combat_sequence = 0;
    if (ok) ok = qa_source_save_count(&io, &combat_count, 3) && qa_source_save_u64(&io, &combat_sequence);
    if (ok && combat_count != functions.combat_count)
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved combat hooks differ from their retained source artifact");
    if (ok && !role->combat && combat_sequence)
        ok = application_fail(error, QA_ERROR_FORMAT, "Absent Source combat cannot own an attack sequence");
    for (size_t i = 0; ok && i < combat_count; ++i)
        ok = qa_source_save_u64(&io, bindings + functions.input_count + functions.weapons_count + i);
    size_t pickups_count = 0;
    if (ok) ok = qa_source_save_count(&io, &pickups_count, functions.pickups_count);
    if (ok && pickups_count != functions.pickups_count)
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved pickup hooks differ from their actual Source constructor");
    for (size_t i = 0; ok && i < pickups_count; ++i)
        ok = qa_source_save_u64(&io, bindings + functions.input_count + functions.weapons_count + functions.combat_count + i);
    bool present = false;
    size_t count = 0;
    if (ok) ok = qa_source_save_bool(&io, &present) &&
        qa_source_save_count(&io, &count, functions.equipment_count);
    if (ok && (present != (role->equipment != NULL) || count != functions.equipment_count))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved equipment callbacks differ from the genuine artifact constructor");
    for (size_t i = 0; ok && i < count; ++i)
        ok = qa_source_save_u64(&io, bindings + functions.input_count + functions.weapons_count + functions.combat_count + functions.pickups_count + i);
    if (ok) ok = state_fields(&io, &state) && qa_source_save_finish(&io, NULL) &&
        application_q3_equipment_state_qualify(role->equipment, &state, error) &&
        qa_qvm_restore_candidate_bindings(role->vm, executor, functions.descriptors,
            bindings, functions.count, error);
    if (ok) {
        application_guest_input_adopt_restore(role, &input);
        application_q3_weapons_adopt(role->weapons,
            functions.weapons_count ? bindings + functions.input_count : NULL);
        application_q3_combat_adopt(role->combat,
            functions.combat_count ? bindings + functions.input_count + functions.weapons_count : NULL);
        application_q3_combat_sequence_adopt(role->combat, combat_sequence);
        application_q3_pickups_adopt(role->pickups,
            functions.pickups_count ? bindings + functions.input_count + functions.weapons_count + functions.combat_count : NULL);
        application_q3_equipment_adopt(role->equipment,
            functions.equipment_count ? bindings + functions.input_count + functions.weapons_count + functions.combat_count + functions.pickups_count : NULL);
        application_q3_equipment_state_adopt(role->equipment, &state);
        application_q3_body_adopt(role->body, functions.body_count ? bindings + functions.count - functions.body_count - functions.models_count : NULL);
        application_q3_weapon_models_adopt(role->weapon_models, functions.models_count ? bindings + functions.count - functions.models_count : NULL);
    }
    qa_source_save_dispose(&io);
    application_q3_body_saved_free(&body);
    free(bindings); free(functions.descriptors);
    return ok;
}
