#include "guest_q3_weapon_models_private.h"
#include "internal.h"
#include "qa/q3_asset_shader.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool application_q3_weapon_models_current(const application_q3_weapon_models *owner, qa_error *error)
{
    if (!owner || !owner->module.vm || !owner->module.image || !owner->module.profile ||
        !owner->module.assets || !owner->module.current)
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon receipt lost its actual module or model registry");
    if (!owner->module.current(owner->module.context, owner->module.vm, owner->module.game_vm,
        owner->module.assets, error)) return false;
    if (qa_qvm_get_role(owner->module.vm) != QA_QVM_CGAME ||
        qa_qvm_get_abi(owner->module.vm) != owner->module.profile->abi ||
        !qa_sha256_equal(qa_qvm_digest(owner->module.vm), &owner->module.profile->artifact))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon receipt lost its actual module or model registry");
    if (owner->module.profile->present && (!owner->module.game_vm || !owner->module.game_image ||
        !owner->module.game_artifact_path ||
        qa_qvm_get_role(owner->module.game_vm) != QA_QVM_GAME ||
        qa_qvm_get_abi(owner->module.game_vm) != owner->module.profile->game_abi ||
        !qa_sha256_equal(qa_qvm_digest(owner->module.game_vm), &owner->module.profile->game_artifact) ||
        !qa_sha256_equal(qa_qvm_image_digest(owner->module.game_image), &owner->module.profile->game_artifact)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model owner changed its actual GAME namespace");
    if (owner->module.profile->present && !application_q3_weapon_models_profile_namespace(owner->module.profile,
        owner->module.game_image, qa_qvm_get_abi(owner->module.game_vm), owner->module.game_artifact_path, error)) return false;
    return qa_qvm_read(owner->module.vm, 0, NULL, 0, error) &&
        (!owner->module.profile->present || qa_qvm_read(owner->module.game_vm, 0, NULL, 0, error));
}
static bool word(const application_q3_weapon_models *owner, uint32_t address, int32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(owner->module.vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return true;
}
bool application_q3_weapon_models_record(application_q3_weapon_models *owner, int32_t weapon,
    application_q3_weapon_model_record *out, const char **gun_path, bool *present, qa_error *error)
{
    if (!out || !present || !application_q3_weapon_models_current(owner, error)) return false;
    *present = false;
    const application_q3_weapon_models_profile *profile = owner->module.profile;
    if (!profile->present) return true;
    application_q3_weapon_model_record record = {.weapon = weapon};
    bool found = false;
    if (profile->indexed) {
        int64_t index = (int64_t)weapon - profile->index_base;
        if (index < 0 || (uint64_t)index >= profile->count) return true;
        record.row = (uint32_t)index; found = true;
    } else for (uint32_t i = 0; i < profile->count; ++i) {
        int32_t source;
        if (!word(owner, profile->base + i * profile->stride + profile->weapon_offset, &source, error)) return false;
        if (source != weapon) continue;
        if (found) return application_fail(error, QA_ERROR_FORMAT, "CG weapon table repeats a full source identity");
        record.row = i; found = true;
    }
    if (!found) return true;
    uint32_t base = profile->base + record.row * profile->stride;
    int32_t registered;
    if (!word(owner, base + profile->registered_offset, &registered, error)) return false;
    if (!registered) return true;
    for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i)
        if (profile->fields[i] && !word(owner, base + profile->offsets[i], &record.handles[i], error)) return false;
    if (record.handles[APPLICATION_Q3_WEAPON_GUN] <= 0)
        return application_fail(error, QA_ERROR_FORMAT, "Registered CG weapon has no actual source gun model handle");
    qa_arena scratch = {0}; const qa_q3_registered_model *models = NULL; size_t count = 0;
    bool ok = qa_q3_registered_models(owner->module.assets, &scratch, &models, &count, error);
    const char *path = NULL;
    for (size_t i = 0; ok && i <= APPLICATION_Q3_WEAPON_FLASH; ++i) {
        int32_t handle = record.handles[i];
        if (!handle) continue;
        const qa_q3_registered_model *model = NULL;
        for (size_t j = 0; j < count; ++j) if (models[j].handle == handle) { model = models + j; break; }
        if (!model || model->world || model->inline_model)
            ok = application_fail(error, QA_ERROR_FORMAT, "CG weapon table handle has no real registered model parent");
        else if (i == APPLICATION_Q3_WEAPON_GUN) path = model->name;
    }
    for (size_t i = APPLICATION_Q3_WEAPON_INVISIBILITY; ok && i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i) {
        const qa_material *material;
        if (record.handles[i]) ok = qa_q3_assets_shader_read(owner->module.assets, record.handles[i], &material, error);
    }
    qa_arena_destroy(&scratch);
    if (ok) ok = application_q3_weapon_models_current(owner, error);
    if (!ok) return false;
    *out = record; *present = true;
    if (gun_path) *gun_path = path;
    return true;
}
static bool registration(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_weapon_models *owner = context;
    int32_t weapon;
    if (!call || call->vm != owner->module.vm || !application_q3_weapon_models_current(owner, error) ||
        !qa_qvm_call_argument(call, owner->module.profile->weapon_argument, &weapon, error)) return false;
    if (owner->calls == UINT_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "CG registration nesting exceeds its retained owner");
    ++owner->calls;
    bool ok = qa_qvm_proceed(call, result, error);
    bool cancelled = false;
    if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
    application_q3_weapon_model_record record = {0}; bool present = false;
    if (ok && !cancelled) ok = application_q3_weapon_models_record(owner, weapon, &record, NULL, &present, error);
    if (!ok || !present) { --owner->calls; return ok; }
    size_t index = 0;
    while (index < owner->count && owner->records[index].weapon != weapon) ++index;
    if (ok && index == owner->count) {
        if (owner->count >= owner->module.profile->count || owner->count == SIZE_MAX / sizeof(*owner->records))
            ok = application_fail(error, QA_ERROR_MEMORY, "CG registration receipts exceed the real declared table");
        else {
            application_q3_weapon_model_record *records = realloc(owner->records, (owner->count + 1) * sizeof(*records));
            if (!records) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual CG weapon registration receipt");
            else { owner->records = records; ++owner->count; }
        }
    }
    if (ok) owner->records[index] = record;
    --owner->calls; return ok;
}
bool application_q3_weapon_models_create(const application_q3_weapon_models_module *module,
    application_q3_weapon_models **out, qa_error *error)
{
    if (!module || !out || *out || !module->profile || !module->image || !module->session ||
        !application_q3_weapon_models_profile_qualify(module->image, module->profile->abi,
            module->profile->artifact_path, module->profile, error)) return false;
    application_q3_weapon_models *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining original CG weapon model owner");
    owner->module = *module; *out = owner;
    if (!application_q3_weapon_models_current(owner, error)) return false;
    return !module->profile->present || qa_qvm_bind_function(module->vm, module->profile->registration,
        false, registration, owner, &owner->binding, error);
}
bool application_q3_weapon_models_idle(const application_q3_weapon_models *owner)
{ return !owner || owner->calls == 0; }
bool application_q3_weapon_models_destroy(application_q3_weapon_models *owner, qa_error *error)
{
    if (!owner) return true;
    if (!application_q3_weapon_models_idle(owner) || !qa_qvm_can_destroy(owner->module.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model teardown retains an actual source call");
    if (owner->binding && !qa_qvm_unbind(owner->module.vm, owner->binding, error)) return false;
    free(owner->records); free(owner); return true;
}
size_t application_q3_weapon_models_descriptor_count(const application_q3_weapon_models *owner)
{ return owner && owner->module.profile->present ? 1 : 0; }
bool application_q3_weapon_models_descriptors(const application_q3_weapon_models *owner,
    qa_qvm_saved_function *out, size_t count, qa_error *error)
{
    if (count != application_q3_weapon_models_descriptor_count(owner) || (count && (!out || !owner->binding)) ||
        !application_q3_weapon_models_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon descriptors differ from their genuine constructor");
    if (count) *out = (qa_qvm_saved_function){owner->binding, owner->module.profile->registration, false, registration, (void *)owner};
    return true;
}
void application_q3_weapon_models_adopt(application_q3_weapon_models *owner, const qa_qvm_binding *bindings)
{ if (owner && owner->module.profile->present) owner->binding = bindings[0]; }
bool application_q3_weapon_models_read(application_q3_weapon_models *owner, int32_t weapon,
    qa_application_q3_weapon_models *out, bool *present, qa_error *error)
{
    if (!owner || !out || !present) return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon read needs its actual retained owner");
    *present = false;
    size_t i = 0;
    while (i < owner->count && owner->records[i].weapon != weapon) ++i;
    if (i == owner->count) return application_q3_weapon_models_current(owner, error);
    application_q3_weapon_model_record actual = {0}; bool found = false; const char *path = NULL;
    if (!application_q3_weapon_models_record(owner, weapon, &actual, &path, &found, error)) return false;
    if (!found || actual.row != owner->records[i].row ||
        memcmp(actual.handles, owner->records[i].handles, sizeof(actual.handles)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon receipt changed without its actual registration entry");
    *out = (qa_application_q3_weapon_models){.assets = owner->module.assets, .source_weapon = weapon,
        .gun = actual.handles[0], .hands = actual.handles[1], .barrel = actual.handles[2], .flash = actual.handles[3],
        .invisibility = actual.handles[4], .battle = actual.handles[5], .quad = actual.handles[6], .gun_path = path};
    *present = true; return true;
}
bool application_q3_weapon_models_qualify(application_q3_weapon_models *owner, qa_error *error)
{
    if (!owner) return true;
    if (!application_q3_weapon_models_idle(owner) || !application_q3_weapon_models_current(owner, error)) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        qa_application_q3_weapon_models receipt; bool present;
        if (!application_q3_weapon_models_read(owner, owner->records[i].weapon, &receipt, &present, error) || !present) return false;
    }
    return true;
}
