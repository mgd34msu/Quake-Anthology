#include "guest_qc_profile.h"
#include "qa/json.h"
#include <float.h>
#include <limits.h>

static char *string(const qa_json_document *doc, qa_json_id node, qa_error *error)
{
    qa_buffer value = {0};
    if (!qa_json_string(doc, node, &value, error)) return NULL;
    if (memchr(value.data, 0, value.size)) {
        qa_buffer_free(&value); application_fail(error, QA_ERROR_FORMAT, "QC declaration string contains NUL"); return NULL;
    }
    return (char *)value.data;
}
static bool array(const qa_json_document *doc, qa_json_id node, bool optional, qa_error *error)
{
    return (optional && node == QA_JSON_NONE) || qa_json_type(doc, node) == QA_JSON_ARRAY ||
        application_fail(error, QA_ERROR_FORMAT, "QC declaration requires an array");
}
static bool empty(const qa_json_document *doc, qa_json_id node, qa_error *error)
{
    return node == QA_JSON_NONE || (qa_json_type(doc, node) == QA_JSON_ARRAY && qa_json_size(doc, node) == 0) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "QC declaration capability has no application owner yet");
}
static bool number(const qa_json_document *doc, qa_json_id node, float *out, qa_error *error)
{
    double value;
    if (!qa_json_number(doc, node, &value, error)) return false;
    if (!isfinite(value) || fabs(value) > FLT_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "QC declaration value exceeds binary32");
        return false;
    }
    *out = (float)value; return true;
}
static bool input(const char *name, application_qc_input_id *out)
{
    static const char *names[] = {"self", "other", "time", "elapsed", "view-angles", "attack", "jump", "impulse",
        "forward-move", "side-move", "up-move", "result"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(name, names[i]) == 0) { *out = (application_qc_input_id)i; return true; }
    return false;
}
static qa_qc_value_type source_type(const application_qc_value *value)
{
    if (value->kind == QC_VALUE_INPUT) return value->source == QC_INPUT_SELF || value->source == QC_INPUT_OTHER ? QA_QC_ENTITY :
        value->source == QC_INPUT_ANGLES ? QA_QC_VECTOR : QA_QC_FLOAT;
    if (value->kind == QC_VALUE_ARGUMENTS_TEXT) return QA_QC_STRING;
    if (value->kind == QC_VALUE_ARGUMENT_COUNT) return QA_QC_FLOAT;
    return value->constant.kind == QA_QC_GAME_STRING ? QA_QC_STRING :
        value->constant.kind == QA_QC_GAME_VECTOR ? QA_QC_VECTOR : QA_QC_FLOAT;
}
static void free_value(application_qc_value *value)
{
    if (value->kind == QC_VALUE_CONSTANT && value->constant.kind == QA_QC_GAME_STRING) free((char *)value->constant.value.string);
}
static bool value(const qa_json_document *doc, qa_json_id node, uint64_t available, bool console,
                    application_qc_value *out, qa_error *error)
{
    qa_json_id kind = qa_json_get(doc, node, "kind"), data = qa_json_get(doc, node, "value");
    if (qa_json_string_equal(doc, kind, "input")) {
        char *name = string(doc, qa_json_get(doc, node, "name"), error);
        bool ok = name && input(name, &out->source) && (available & (UINT64_C(1) << out->source));
        free(name); out->kind = QC_VALUE_INPUT;
        return ok || application_fail(error, QA_ERROR_FORMAT, "QC source call reads an unavailable input");
    }
    if (console && qa_json_string_equal(doc, kind, "argument")) {
        uint64_t index;
        qa_json_id type = qa_json_get(doc, node, "type");
        if (!qa_json_u64(doc, qa_json_get(doc, node, "index"), &index, error) ||
            index > UINT64_C(9007199254740991) || index > SIZE_MAX ||
            (!qa_json_string_equal(doc, type, "float") && !qa_json_string_equal(doc, type, "string")))
            return application_fail(error, QA_ERROR_FORMAT, "QC console argument declaration is invalid");
        out->kind = QC_VALUE_ARGUMENT; out->argument = (size_t)index;
        out->constant.kind = qa_json_string_equal(doc, type, "string") ? QA_QC_GAME_STRING : QA_QC_GAME_FLOAT;
        return true;
    }
    if (console && qa_json_string_equal(doc, kind, "arguments-text")) {
        out->kind = QC_VALUE_ARGUMENTS_TEXT; return true;
    }
    if (console && qa_json_string_equal(doc, kind, "argument-count")) {
        out->kind = QC_VALUE_ARGUMENT_COUNT; return true;
    }
    if (qa_json_string_equal(doc, kind, "float")) {
        out->constant.kind = QA_QC_GAME_FLOAT;
        return number(doc, data, &out->constant.value.number, error);
    }
    if (qa_json_string_equal(doc, kind, "string")) {
        out->constant.kind = QA_QC_GAME_STRING; out->constant.value.string = string(doc, data, error);
        return out->constant.value.string != NULL;
    }
    if (qa_json_string_equal(doc, kind, "vector")) {
        float x, y, z; out->constant.kind = QA_QC_GAME_VECTOR;
        if (!number(doc, qa_json_get(doc, data, "x"), &x, error) ||
            !number(doc, qa_json_get(doc, data, "y"), &y, error) ||
            !number(doc, qa_json_get(doc, data, "z"), &z, error)) return false;
        out->constant.value.vector = qa_v3(x, y, z); return true;
    }
    return application_fail(error, QA_ERROR_FORMAT, "Unknown QC source value kind");
}
static void free_call(application_qc_call *call)
{
    for (size_t j = 0; j < call->argument_count; ++j) free_value(&call->arguments[j]);
    for (size_t j = 0; call->globals && j < call->global_count; ++j) free_value(&call->globals[j].value);
    free(call->globals);
}
static void free_calls(application_qc_calls *calls)
{
    if (!calls->values) return;
    for (size_t i = 0; i < calls->count; ++i) {
        free_call(&calls->values[i]);
    }
    free(calls->values);
}
void application_qc_release_qualification(application_provider *provider)
{
    struct application_qc_profile *profile = provider->state.qc.qualified;
    if (!profile) return;
    for (size_t i = 0; profile->fields && i < profile->field_count; ++i) free_value(&profile->fields[i].constant);
    free(profile->fields);
    application_qc_calls *lists[] = {&profile->initialize, &profile->admit, &profile->userinfo,
        &profile->disconnect, &profile->client_frame, &profile->frame};
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) free_calls(lists[i]);
    for (size_t i = 0; profile->input && i < profile->input_count; ++i) { free_calls(&profile->input[i].calls); free(profile->input[i].outputs); }
    free(profile->input);
    for (size_t i = 0; i < profile->client_output_count; ++i) free(profile->client_outputs[i].values);
    for (size_t i = 0; profile->cvars && i < profile->cvar_count; ++i) { free(profile->cvars[i].name); free(profile->cvars[i].value); }
    free(profile->cvars);
    for (size_t i = 0; profile->commands && i < profile->command_count; ++i) {
        free(profile->commands[i].name); free_call(&profile->commands[i].call);
    }
    free(profile->commands);
    for (size_t i = 0; profile->callbacks && i < profile->callback_count; ++i)
        free_call(&profile->callbacks[i].call);
    free(profile->callbacks);
    for (size_t i = 0; profile->weapon_values && i < profile->weapon_count; ++i)
        free(profile->weapon_values[i].label);
    free(profile->weapon_values); free(profile); provider->state.qc.qualified = NULL;
}
static bool call(const qa_json_document *doc, qa_json_id node, const qa_qc_program *program,
                   uint64_t available, bool console, application_qc_call *out, qa_error *error)
{
    char *name = string(doc, qa_json_get(doc, node, "function"), error);
    bool found = name && qa_qc_program_find_function(program, name, &out->function); free(name);
    const qa_qc_function *fn = found ? qa_qc_program_function(program, out->function) : NULL;
    qa_json_id args = qa_json_get(doc, node, "arguments"), globals = qa_json_get(doc, node, "globals");
    if (!fn || !out->function || fn->first_statement < 0 || fn->named_builtin ||
        !array(doc, args, false, error) || !array(doc, globals, false, error) ||
        qa_json_size(doc, args) > 8 || fn->parameter_count != qa_json_size(doc, args))
        return application_fail(error, QA_ERROR_FORMAT, "QC declaration callback signature differs from its compiled source");
    out->argument_count = qa_json_size(doc, args); out->global_count = qa_json_size(doc, globals);
    out->globals = out->global_count ? calloc(out->global_count, sizeof(*out->globals)) : NULL;
    if (out->global_count && !out->globals) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC source globals");
    for (size_t i = 0; i < out->argument_count; ++i) {
        if (!value(doc, qa_json_at(doc, args, i), available, console, &out->arguments[i], error)) return false;
        if (fn->parameter_sizes[i] != (source_type(&out->arguments[i]) == QA_QC_VECTOR ? 3u : 1u))
            return application_fail(error, QA_ERROR_FORMAT, "QC declared parameter width differs from source");
    }
    for (size_t i = 0; i < out->global_count; ++i) {
        qa_json_id entry = qa_json_at(doc, globals, i);
        name = string(doc, qa_json_get(doc, entry, "name"), error);
        const qa_qc_definition *def = name ? qa_qc_program_find_global(program, name) : NULL; free(name);
        if (!def || def->offset < 28 || !value(doc, qa_json_get(doc, entry, "value"), available, console, &out->globals[i].value, error) ||
            def->type != source_type(&out->globals[i].value))
            return application_fail(error, QA_ERROR_FORMAT, "QC declared global type differs from source");
        uint32_t width = def->type == QA_QC_VECTOR ? 3u : 1u;
        for (size_t j = 0; j < i; ++j) {
            const qa_qc_definition *previous = out->globals[j].definition;
            if (def->offset < previous->offset + (previous->type == QA_QC_VECTOR ? 3u : 1u) && previous->offset < def->offset + width)
                return application_fail(error, QA_ERROR_FORMAT, "QC declaration globals overlap");
        }
        out->globals[i].definition = def;
    }
    return true;
}
static bool calls(const qa_json_document *doc, qa_json_id node, bool optional, const qa_qc_program *program,
                    uint64_t available, application_qc_calls *out, qa_error *error)
{
    if (!array(doc, node, optional, error)) return false;
    out->count = qa_json_size(doc, node);
    out->values = out->count ? calloc(out->count, sizeof(*out->values)) : NULL;
    if (out->count && !out->values) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC source calls");
    for (size_t i = 0; i < out->count; ++i)
        if (!call(doc, qa_json_at(doc, node, i), program, available, false, &out->values[i], error)) return false;
    return true;
}
static bool fields(const qa_json_document *doc, qa_json_id node, application_provider *provider,
                     struct application_qc_profile *profile, qa_error *error)
{
    if (!array(doc, node, false, error)) return false;
    profile->field_count = qa_json_size(doc, node);
    profile->fields = profile->field_count ? calloc(profile->field_count, sizeof(*profile->fields)) : NULL;
    if (profile->field_count && !profile->fields) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC declared fields");
    static const char *names[] = {"private", "constant", "health", "origin", "velocity", "angles", "bounds-min", "bounds-max",
        "think", "nextthink", "classname", "view-offset", "client-flags", "client-input", "inventory"};
    unsigned thinkers = 0, deadlines = 0;
    for (size_t i = 0; i < profile->field_count; ++i) {
        qa_json_id row = qa_json_at(doc, node, i);
        application_qc_bound_field *field = &profile->fields[i]; field->scale = 1;
        char *name = string(doc, qa_json_get(doc, row, "field"), error);
        field->definition = name ? qa_qc_program_find_field(provider->state.qc.program, name) : NULL; free(name);
        size_t kind = 0;
        while (kind < sizeof(names) / sizeof(names[0]) && !qa_json_string_equal(doc, qa_json_get(doc, row, "binding"), names[kind])) ++kind;
        if (!field->definition || kind == sizeof(names) / sizeof(names[0]))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "QC actor field binding is missing or not implemented");
        field->kind = (application_qc_field_kind)kind;
        qa_qc_value_type type = field->definition->type;
        if (field->kind == QC_FIELD_CONSTANT) {
            if (!value(doc, qa_json_get(doc, row, "value"), 0, false, &field->constant, error)) return false;
            type = source_type(&field->constant);
        } else if (field->kind == QC_FIELD_THINK) { type = QA_QC_FUNCTION; ++thinkers; }
        else if (field->kind == QC_FIELD_CLASSNAME) type = QA_QC_STRING;
        else if (field->kind == QC_FIELD_HEALTH || field->kind == QC_FIELD_NEXTTHINK ||
            field->kind == QC_FIELD_INVENTORY || field->kind == QC_FIELD_CLIENT_FLAGS) type = QA_QC_FLOAT;
        else if (field->kind != QC_FIELD_PRIVATE && field->kind != QC_FIELD_INPUT) type = QA_QC_VECTOR;
        if (field->kind == QC_FIELD_NEXTTHINK) ++deadlines;
        if (field->kind == QC_FIELD_INPUT) {
            name = string(doc, qa_json_get(doc, row, "input"), error);
            bool ok = name && input(name, &field->input) && field->input >= QC_INPUT_ANGLES &&
                field->input <= QC_INPUT_UP; free(name);
            if (!ok) return application_fail(error, QA_ERROR_FORMAT, "QC field names an invalid client input");
            type = field->input == QC_INPUT_ANGLES ? QA_QC_VECTOR : QA_QC_FLOAT;
            qa_json_id update = qa_json_get(doc, row, "update");
            field->nonzero = qa_json_string_equal(doc, update, "nonzero");
            if ((!field->nonzero && !qa_json_string_equal(doc, update, "always")) || (field->nonzero && field->input == QC_INPUT_ANGLES))
                return application_fail(error, QA_ERROR_FORMAT, "QC input update policy is invalid");
            qa_json_id scale = qa_json_get(doc, row, "scale");
            if (scale != QA_JSON_NONE && (!number(doc, scale, &field->scale, error) || field->scale == 0 || field->input == QC_INPUT_ANGLES))
                return application_fail(error, QA_ERROR_FORMAT, "QC input scalar scale is invalid");
        }
        if (field->kind == QC_FIELD_CLIENT_FLAGS) {
            qa_json_id grounded = qa_json_get(doc, row, "grounded"), mask = qa_json_get(doc, row, "privateMask"); uint64_t bits = 0;
            if ((grounded != QA_JSON_NONE && (!qa_json_bool(doc, grounded, &field->grounded, error) || !field->grounded)) ||
                (mask != QA_JSON_NONE && !qa_json_u64(doc, mask, &bits, error)) || bits > 0x7fffff ||
                (bits & (8u | 128u | (field->grounded ? 512u : 0u))))
                return application_fail(error, QA_ERROR_FORMAT, "QC private client flags overlap canonical state");
            field->private_mask = (uint32_t)bits;
        }
        if (field->kind == QC_FIELD_INVENTORY) {
            name = string(doc, qa_json_get(doc, row, "item"), error);
            bool ok = name && *name && qa_strings_intern_cstr(qa_session_strings(provider->application->session), name, &field->item, error);
            free(name); if (!ok) return false;
        }
        if (type != field->definition->type) return application_fail(error, QA_ERROR_FORMAT, "QC declared actor field type differs from source");
        uint32_t width = type == QA_QC_VECTOR ? 3u : 1u;
        for (size_t j = 0; j < i; ++j) {
            const qa_qc_definition *previous = profile->fields[j].definition;
            if (field->definition->offset < previous->offset + (previous->type == QA_QC_VECTOR ? 3u : 1u) &&
                previous->offset < field->definition->offset + width)
                return application_fail(error, QA_ERROR_FORMAT, "QC declared actor fields overlap");
        }
    }
    return (thinkers == deadlines && thinkers <= 1) || application_fail(error, QA_ERROR_FORMAT, "QC think and nextthink declarations must be paired");
}
static bool bindings(const qa_json_document *doc, qa_json_id node, const qa_qc_program *program,
                       struct application_qc_profile *profile, qa_error *error)
{
    if (!array(doc, node, true, error)) return false;
    profile->input_count = qa_json_size(doc, node);
    profile->input = profile->input_count ? calloc(profile->input_count, sizeof(*profile->input)) : NULL;
    if (profile->input_count && !profile->input) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC client input bindings");
    uint64_t available = (UINT64_C(1) << (QC_INPUT_UP + 1)) - 1;
    for (size_t i = 0; i < profile->input_count; ++i) {
        application_qc_input_binding *binding = &profile->input[i]; qa_json_id row = qa_json_at(doc, node, i);
        qa_json_id scope = qa_json_get(doc, row, "scope"), phase = qa_json_get(doc, row, "phase");
        binding->movement_slice = qa_json_string_equal(doc, scope, "movement-slice"); binding->before = qa_json_string_equal(doc, phase, "before");
        if ((!binding->movement_slice && !qa_json_string_equal(doc, scope, "client-command")) ||
            (!binding->before && !qa_json_string_equal(doc, phase, "after")) ||
            !calls(doc, qa_json_get(doc, row, "calls"), false, program, available & ~(UINT64_C(1) << QC_INPUT_OTHER), &binding->calls, error))
            return application_fail(error, QA_ERROR_FORMAT, "QC client input binding is invalid");
        qa_json_id outputs = qa_json_get(doc, row, "outputs");
        if (!array(doc, outputs, true, error) || (!binding->before && qa_json_size(doc, outputs)))
            return application_fail(error, QA_ERROR_FORMAT, "QC after-input binding cannot publish outputs");
        binding->output_count = qa_json_size(doc, outputs);
        binding->outputs = binding->output_count ? calloc(binding->output_count, sizeof(*binding->outputs)) : NULL;
        if (binding->output_count && !binding->outputs) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC input outputs");
        for (size_t j = 0; j < binding->output_count; ++j) {
            application_qc_output *output = &binding->outputs[j]; qa_json_id out = qa_json_at(doc, outputs, j), kind = qa_json_get(doc, out, "kind");
            if (qa_json_string_equal(doc, kind, "field")) {
                char *name = string(doc, qa_json_get(doc, out, "field"), error);
                for (size_t k = 0; name && k < profile->field_count; ++k)
                    if (profile->fields[k].kind == QC_FIELD_INPUT && strcmp(profile->fields[k].definition->name, name) == 0) output->field = &profile->fields[k];
                free(name); if (!output->field) return application_fail(error, QA_ERROR_FORMAT, "QC input output lacks its declared source field");
            } else if (qa_json_string_equal(doc, kind, "handler")) {
                char *name = string(doc, qa_json_get(doc, out, "function"), error);
                bool found = name && qa_qc_program_find_function(program, name, &output->function); free(name);
                const qa_qc_function *fn = found ? qa_qc_program_function(program, output->function) : NULL;
                const qa_qc_definition *self = qa_qc_program_find_global(program, "self");
                qa_json_id inputs = qa_json_get(doc, out, "inputs");
                if (!fn || !output->function || fn->first_statement < 0 || fn->named_builtin || !self || self->type != QA_QC_ENTITY ||
                    !array(doc, inputs, false, error) || !qa_json_size(doc, inputs))
                    return application_fail(error, QA_ERROR_FORMAT, "QC consume output has no original source handler");
                for (size_t k = 0; k < qa_json_size(doc, inputs); ++k) {
                    application_qc_input_id id; name = string(doc, qa_json_at(doc, inputs, k), error);
                    bool ok = name && input(name, &id) && id > QC_INPUT_ANGLES && id <= QC_INPUT_UP &&
                        !(output->consume & (UINT64_C(1) << id)); free(name);
                    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "QC handler consumption inputs are invalid");
                    output->consume |= UINT64_C(1) << id;
                }
            } else return application_fail(error, QA_ERROR_FORMAT, "QC input output kind is unknown");
            for (size_t k = 0; k < j; ++k)
                if ((output->field && output->field == binding->outputs[k].field) ||
                    (output->function && output->function == binding->outputs[k].function))
                    return application_fail(error, QA_ERROR_FORMAT, "Duplicate QC input output");
        }
    }
    return true;
}
static bool selected_weapon(const qa_json_document *doc, qa_json_id node,
                              application_provider *provider, struct application_qc_profile *profile,
                              qa_error *error)
{
    if (node == QA_JSON_NONE) return true;
    if (qa_json_type(doc, node) != QA_JSON_OBJECT)
        return application_fail(error, QA_ERROR_FORMAT, "QC selected weapon requires a declared field/value mapping");
    char *name = string(doc, qa_json_get(doc, node, "field"), error);
    if (!name) return false;
    profile->weapon_field = qa_qc_program_find_field(provider->state.qc.program, name);
    free(name);
    if (!profile->weapon_field || profile->weapon_field->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_FORMAT, "QC selected weapon field is not an original source float");
    qa_json_id values = qa_json_get(doc, node, "values");
    if (!array(doc, values, false, error)) return false;
    profile->weapon_count = qa_json_size(doc, values);
    if (!profile->weapon_count || profile->weapon_count > SIZE_MAX / sizeof(*profile->weapon_values))
        return application_fail(error, QA_ERROR_FORMAT, "QC selected weapon has no bounded source values");
    profile->weapon_values = calloc(profile->weapon_count, sizeof(*profile->weapon_values));
    if (!profile->weapon_values) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC selected weapon mapping");
    for (size_t i = 0; i < profile->weapon_count; ++i) {
        qa_json_id row = qa_json_at(doc, values, i);
        application_qc_weapon_value *value = profile->weapon_values + i;
        if (!number(doc, qa_json_get(doc, row, "value"), &value->value, error)) return false;
        char *item = string(doc, qa_json_get(doc, row, "item"), error);
        if (!item) return false;
        const char *colon = strchr(item, ':');
        bool ok = colon && colon != item && colon[1] &&
            qa_strings_intern_cstr(qa_session_strings(provider->application->session), item, &value->item, error);
        free(item);
        if (!ok) return application_fail(error, QA_ERROR_FORMAT, "QC selected weapon item requires a canonical namespace");
        qa_json_id ammo = qa_json_get(doc, row, "ammo");
        if (ammo != QA_JSON_NONE) {
            value->ammo_declared = true;
            if (qa_json_type(doc, ammo) != QA_JSON_NULL) {
                char *ammo_name = string(doc, ammo, error);
                if (!ammo_name) return false;
                const char *separator = strchr(ammo_name, ':');
                bool admitted = separator && separator != ammo_name && separator[1] &&
                    qa_strings_intern_cstr(qa_session_strings(provider->application->session),ammo_name,&value->ammo,error);
                free(ammo_name);
                if (!admitted) return application_fail(error, QA_ERROR_FORMAT,
                    "QC selected weapon ammunition requires its declared canonical namespace or null");
            }
        }
        qa_json_id label = qa_json_get(doc, row, "label"), bit = qa_json_get(doc, row, "bit"),
            impulse = qa_json_get(doc, row, "impulse");
        if (label != QA_JSON_NONE || bit != QA_JSON_NONE || impulse != QA_JSON_NONE) {
            uint64_t declared_bit = 0; double declared_impulse = 0;
            value->label = string(doc, label, error);
            if (!value->label || !*value->label || !qa_json_u64(doc, bit, &declared_bit, error) ||
                !declared_bit || declared_bit > UINT32_MAX ||
                !qa_json_number(doc, impulse, &declared_impulse, error) ||
                !isfinite(declared_impulse) || trunc(declared_impulse) != declared_impulse ||
                declared_impulse < INT32_MIN || declared_impulse > INT32_MAX)
                return application_fail(error, QA_ERROR_FORMAT, "QC weapon UI requires its authored label, bit and integer impulse");
            value->bit = (uint32_t)declared_bit; value->impulse = (int32_t)declared_impulse;
            value->ui_declared = true;
            qa_json_id via=qa_json_get(doc,row,"via");
            if(via!=QA_JSON_NONE) {
                uint64_t declared_via=0;
                if(!qa_json_u64(doc,via,&declared_via,error) || !declared_via || declared_via>UINT32_MAX)
                    return application_fail(error,QA_ERROR_FORMAT,"QC weapon transition requires its authored source bit");
                value->via=(uint32_t)declared_via;
            }
            for (size_t j = 0; j < i; ++j)
                if (profile->weapon_values[j].ui_declared && profile->weapon_values[j].bit == value->bit)
                    return application_fail(error, QA_ERROR_FORMAT, "QC weapon UI repeats a declared source bit");
        }
        for (size_t j = 0; j < i; ++j)
            if (profile->weapon_values[j].value == value->value)
                return application_fail(error, QA_ERROR_FORMAT, "QC selected weapon repeats a source value");
    }
    return true;
}

static const application_qc_bound_field *client_output_field(const qa_json_document *doc,
    qa_json_id node, const struct application_qc_profile *profile, bool vector, qa_error *error)
{
    char *name = string(doc, node, error);
    if (!name) return NULL;
    const application_qc_bound_field *found = NULL;
    for (size_t i = 0; i < profile->field_count; ++i)
        if (!strcmp(profile->fields[i].definition->name, name)) { found = profile->fields + i; break; }
    free(name);
    if (!found || found->definition->type != (vector ? QA_QC_VECTOR : QA_QC_FLOAT) ||
        (found->kind != QC_FIELD_PRIVATE &&
            !(vector && (found->kind == QC_FIELD_VIEW || found->kind == QC_FIELD_MIN || found->kind == QC_FIELD_MAX)) &&
            !(!vector && found->kind == QC_FIELD_CLIENT_FLAGS))) {
        application_fail(error, QA_ERROR_FORMAT, "QC client output requires its declared original field authority");
        return NULL;
    }
    return found;
}

static bool client_outputs(const qa_json_document *doc, qa_json_id node,
    struct application_qc_profile *profile, qa_error *error)
{
    if (!array(doc, node, true, error)) return false;
    size_t count = qa_json_size(doc, node);
    if (count > APPLICATION_CLIENT_OUTPUT_COUNT)
        return application_fail(error, QA_ERROR_FORMAT, "QC client output channels are duplicated");
    profile->client_output_count = count;
    static const char *channels[] = {"view-offset", "movement-mode", "stance", "body-shape"};
    for (size_t i = 0; i < count; ++i) {
        qa_json_id row = qa_json_at(doc, node, i), kind = qa_json_get(doc, row, "kind");
        size_t channel = 0;
        while (channel < APPLICATION_CLIENT_OUTPUT_COUNT && !qa_json_string_equal(doc, kind, channels[channel])) ++channel;
        if (channel == APPLICATION_CLIENT_OUTPUT_COUNT || (profile->client_output_channels & (1u << channel)))
            return application_fail(error, QA_ERROR_FORMAT, "QC client output kind is unknown or duplicated");
        application_qc_client_output *output = profile->client_outputs + i;
        output->channel = (application_client_output_channel)channel;
        profile->client_output_channels |= (uint8_t)(1u << channel);
        if (channel == APPLICATION_CLIENT_BODY_SHAPE) {
            output->field = client_output_field(doc, qa_json_get(doc, row, "min"), profile, true, error);
            output->maximum = client_output_field(doc, qa_json_get(doc, row, "max"), profile, true, error);
            if (!output->field || !output->maximum) return false;
            if (output->field->kind != QC_FIELD_MIN || output->maximum->kind != QC_FIELD_MAX)
                return application_fail(error, QA_ERROR_FORMAT, "QC body shape requires original declared mins and maxs");
            continue;
        }
        qa_json_id height = qa_json_get(doc, row, "height");
        output->height = channel == APPLICATION_CLIENT_VIEW_OFFSET && height != QA_JSON_NONE;
        output->field = client_output_field(doc, output->height ? height : qa_json_get(doc, row, "field"),
            profile, channel == APPLICATION_CLIENT_VIEW_OFFSET && !output->height, error);
        if (!output->field) return false;
        if (channel == APPLICATION_CLIENT_VIEW_OFFSET) {
            if (output->field->kind == QC_FIELD_CLIENT_FLAGS)
                return application_fail(error, QA_ERROR_FORMAT, "QC view height cannot own canonical client flags");
            continue;
        }
        qa_json_id mask = qa_json_get(doc, row, "mask");
        output->masked = mask != QA_JSON_NONE;
        if (output->masked) {
            uint64_t bits;
            if (!qa_json_u64(doc, mask, &bits, error)) return false;
            if (!bits || bits > UINT32_MAX)
                return application_fail(error, QA_ERROR_FORMAT, "QC client output mask exceeds its source word");
            output->mask = (uint32_t)bits;
        }
        if (output->field->kind == QC_FIELD_CLIENT_FLAGS &&
            (!output->masked || (output->mask & ~output->field->private_mask)))
            return application_fail(error, QA_ERROR_FORMAT, "QC flag outputs require an explicit source-private mask");
        qa_json_id values = qa_json_get(doc, row, "values");
        if (!array(doc, values, false, error)) return false;
        output->value_count = qa_json_size(doc, values);
        if (!output->value_count || output->value_count > SIZE_MAX / sizeof(*output->values))
            return application_fail(error, QA_ERROR_FORMAT, "QC client output needs a bounded source value mapping");
        output->values = calloc(output->value_count, sizeof(*output->values));
        if (!output->values) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC client output mapping");
        for (size_t j = 0; j < output->value_count; ++j) {
            qa_json_id entry = qa_json_at(doc, values, j);
            application_qc_client_output_value *mapping = output->values + j;
            if (!qa_json_number(doc, qa_json_get(doc, entry, "value"), &mapping->value, error)) return false;
            if (!isfinite(mapping->value) || (output->masked &&
                (mapping->value < 0 || mapping->value > UINT32_MAX || trunc(mapping->value) != mapping->value ||
                    ((uint32_t)mapping->value & output->mask) != (uint32_t)mapping->value)))
                return application_fail(error, QA_ERROR_FORMAT, "QC client output value leaves its declared source mask");
            for (size_t k = 0; k < j; ++k)
                if (output->values[k].value == mapping->value)
                    return application_fail(error, QA_ERROR_FORMAT, "QC client output repeats a source value");
            if (channel == APPLICATION_CLIENT_MOVEMENT_MODE) {
                qa_json_id mode = qa_json_get(doc, entry, "mode");
                if (qa_json_string_equal(doc, mode, "normal")) mapping->output.mode = QA_MOVEMENT_MODE_NORMAL;
                else if (qa_json_string_equal(doc, mode, "noclip")) mapping->output.mode = QA_MOVEMENT_MODE_NOCLIP;
                else if (qa_json_string_equal(doc, mode, "freeze")) mapping->output.mode = QA_MOVEMENT_MODE_FREEZE;
                else return application_fail(error, QA_ERROR_FORMAT, "QC movement output mode is unknown");
            } else if (!qa_json_bool(doc, qa_json_get(doc, entry, "crouched"), &mapping->output.crouched, error)) return false;
        }
    }
    return true;
}

bool application_qc_authored_map_ready(const application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !provider->state.qc.program)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC authored map has no compiled source owner");
    static const struct { const char *name; qa_qc_value_type type; } fields[] = {
        {"classname", QA_QC_STRING}, {"model", QA_QC_STRING},
        {"modelindex", QA_QC_FLOAT}, {"solid", QA_QC_FLOAT},
        {"movetype", QA_QC_FLOAT}, {"flags", QA_QC_FLOAT},
        {"owner", QA_QC_ENTITY}, {"think", QA_QC_FUNCTION},
        {"nextthink", QA_QC_FLOAT}
    }, globals[] = {
        {"self", QA_QC_ENTITY}, {"other", QA_QC_ENTITY},
        {"mapname", QA_QC_STRING}, {"time", QA_QC_FLOAT}
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const qa_qc_definition *field = qa_qc_program_find_field(provider->state.qc.program, fields[i].name);
        if (!field || field->type != fields[i].type) {
            qa_error_set(error, QA_ERROR_FORMAT, i,
                "QC authored map field %s is missing or has a different type", fields[i].name);
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(globals) / sizeof(*globals); ++i) {
        const qa_qc_definition *global = qa_qc_program_find_global(provider->state.qc.program, globals[i].name);
        if (!global || global->type != globals[i].type) {
            qa_error_set(error, QA_ERROR_FORMAT, i,
                "QC authored map global %s is missing or has a different type", globals[i].name);
            return false;
        }
    }
    return true;
}

bool application_qc_qualify(application_provider *provider, qa_error *error)
{
    if (!provider || !provider->state.qc.program || provider->state.qc.qualified || !provider->launch->selection.artifact)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QC qualification owner");
    if (!provider->launch->declaration) return true;
    qa_json_document *doc;
    if (!qa_json_parse(qa_resource_bytes(provider->launch->declaration), &doc, error)) return false;
    struct application_qc_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) { qa_json_destroy(doc); return application_fail(error, QA_ERROR_MEMORY, "Allocating QC qualification"); }
    provider->state.qc.qualified = profile;
    qa_json_id root = qa_json_root(doc), artifact = qa_json_get(doc, root, "program"); uint64_t version = 0;
    char *digest = string(doc, qa_json_get(doc, artifact, "digest"), error);
    char *path = string(doc, qa_json_get(doc, artifact, "path"), error); qa_sha256_digest expected;
    qa_qc_program_info program = qa_qc_program_describe(provider->state.qc.program);
    bool ok = qa_json_type(doc, root) == QA_JSON_OBJECT && qa_json_u64(doc, qa_json_get(doc, root, "version"), &version, error) && version == 1 &&
        qa_json_string_equal(doc, qa_json_get(doc, root, "runtime"), "quakec") && digest && path &&
        strcmp(path, provider->launch->selection.artifact) == 0 && qa_sha256_parse(digest, &expected, error) &&
        qa_sha256_equal(&expected, &program.digest);
    free(digest); free(path);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "QC declaration artifact identity differs");
    if (ok && (provider->launch->roles & QA_ROLE_BIT(QA_ROLE_ENTITIES)))
        ok = application_qc_authored_map_ready(provider, error);
    static const char *pending[] = {"combat", "protection", "items", "pickups", "objectives", "clientPresentation"};
    for (size_t i = 0; ok && i < sizeof(pending) / sizeof(pending[0]); ++i)
        ok = empty(doc, qa_json_get(doc, root, pending[i]), error);
    if (ok) ok = fields(doc, qa_json_get(doc, root, "actorFields"), provider, profile, error);
    qa_json_id callbacks = qa_json_get(doc, root, "callbacks");
    if (ok) ok = array(doc, callbacks, false, error);
    if (ok) {
        profile->callback_count = qa_json_size(doc, callbacks);
        profile->callbacks = profile->callback_count ? calloc(profile->callback_count, sizeof(*profile->callbacks)) : NULL;
        if (profile->callback_count && !profile->callbacks)
            ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QC declared actor callbacks");
    }
    for (size_t i = 0; ok && i < profile->callback_count; ++i) {
        qa_json_id row = qa_json_at(doc, callbacks, i);
        application_qc_callback *callback = profile->callbacks + i;
        qa_json_id operation = qa_json_get(doc, row, "operation"), stage = qa_json_get(doc, row, "stage");
        if (qa_json_string_equal(doc, operation, "actor.think")) callback->operation = Q3_MOD_THINK;
        else if (qa_json_string_equal(doc, operation, "actor.touch")) callback->operation = Q3_MOD_TOUCH;
        else { ok = application_fail(error, QA_ERROR_UNSUPPORTED, "QC callback operation has no declared application owner yet"); break; }
        uint64_t available = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_TIME) |
            (UINT64_C(1) << (callback->operation == Q3_MOD_THINK ? QC_INPUT_ELAPSED : QC_INPUT_OTHER));
        if (qa_json_string_equal(doc, stage, "observe")) {
            callback->stage = QA_OPERATION_OBSERVE;
            available |= UINT64_C(1) << QC_INPUT_RESULT;
        } else if (qa_json_string_equal(doc, stage, "replace") &&
            qa_json_string_equal(doc, qa_json_get(doc, row, "result"), "boolean"))
            callback->stage = QA_OPERATION_REPLACE;
        else { ok = application_fail(error, QA_ERROR_FORMAT, "QC actor callback stage differs from its canonical contract"); break; }
        char *id = string(doc, qa_json_get(doc, row, "id"), error);
        const char *colon = id ? strchr(id, ':') : NULL;
        ok = colon && colon != id && colon[1] &&
            qa_strings_intern_cstr(qa_session_strings(provider->application->session), id, &callback->id, error);
        free(id);
        if (!ok && error && error->code == QA_OK)
            application_fail(error, QA_ERROR_FORMAT, "QC callback requires a namespaced identity");
        for (size_t j = 0; ok && j < i; ++j)
            if (profile->callbacks[j].id == callback->id)
                ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate QC declared callback identity");
        if (ok) ok = call(doc, row, provider->state.qc.program, available, false, &callback->call, error);
    }
    qa_json_id commands = qa_json_get(doc, root, "commands");
    if (ok) ok = array(doc, commands, true, error);
    if (ok) {
        profile->command_count = qa_json_size(doc, commands);
        profile->commands = profile->command_count ? calloc(profile->command_count, sizeof(*profile->commands)) : NULL;
        if (profile->command_count && !profile->commands) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QC declared commands");
    }
    for (size_t i = 0; ok && i < profile->command_count; ++i) {
        qa_json_id row = qa_json_at(doc, commands, i);
        application_qc_command *command = &profile->commands[i];
        command->name = string(doc, qa_json_get(doc, row, "name"), error);
        ok = command->name && *command->name &&
            call(doc, row, provider->state.qc.program, 0, true, &command->call, error);
        for (size_t j = 0; ok && j < i; ++j)
            if (application_qc_command_name_equal(command->name, profile->commands[j].name))
                ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate QC declared command");
    }
    if (ok) ok = selected_weapon(doc, qa_json_get(doc, root, "selectedWeapon"), provider, profile, error);
    uint64_t lifecycle = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_TIME);
    if (ok) ok = calls(doc, qa_json_get(doc, root, "initialize"), true, provider->state.qc.program, lifecycle, &profile->initialize, error);
    qa_json_id frame = qa_json_get(doc, root, "frame");
    if (ok && frame != QA_JSON_NONE) {
        profile->frame.count = 1; profile->frame.values = calloc(1, sizeof(*profile->frame.values));
        ok = profile->frame.values && call(doc, frame, provider->state.qc.program, lifecycle | (UINT64_C(1) << QC_INPUT_ELAPSED), false, profile->frame.values, error);
    }
    qa_json_id clients = qa_json_get(doc, root, "clients");
    if (ok && clients != QA_JSON_NONE) {
        uint64_t maximum = 0; profile->clients = true;
        ok = qa_json_u64(doc, qa_json_get(doc, clients, "maximum"), &maximum, error) && maximum > 0 && maximum < 8191;
        profile->maximum_clients = (uint32_t)maximum;
        if (ok) ok = client_outputs(doc, qa_json_get(doc, clients, "outputs"), profile, error) &&
            calls(doc, qa_json_get(doc, clients, "admit"), false, provider->state.qc.program, lifecycle, &profile->admit, error) &&
            calls(doc, qa_json_get(doc, clients, "userinfo"), false, provider->state.qc.program, lifecycle, &profile->userinfo, error) &&
            calls(doc, qa_json_get(doc, clients, "disconnect"), false, provider->state.qc.program, lifecycle, &profile->disconnect, error) &&
            calls(doc, qa_json_get(doc, clients, "frame"), true, provider->state.qc.program, lifecycle | (UINT64_C(1) << QC_INPUT_ELAPSED), &profile->client_frame, error) &&
            bindings(doc, qa_json_get(doc, clients, "input"), provider->state.qc.program, profile, error);
    }
    for (size_t i = 0; ok && i < profile->field_count; ++i)
        if (profile->fields[i].kind == QC_FIELD_INPUT && (!profile->clients || !profile->input_count))
            ok = application_fail(error, QA_ERROR_FORMAT, "QC client input fields require declared input applications");
    qa_json_id cvars = qa_json_get(doc, root, "cvars");
    if (ok) ok = array(doc, cvars, true, error);
    if (ok) {
        profile->cvar_count = qa_json_size(doc, cvars); profile->cvars = profile->cvar_count ? calloc(profile->cvar_count, sizeof(*profile->cvars)) : NULL;
        if (profile->cvar_count && !profile->cvars) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QC declared cvars");
    }
    for (size_t i = 0; ok && i < profile->cvar_count; ++i) {
        qa_json_id row = qa_json_at(doc, cvars, i);
        profile->cvars[i].name = string(doc, qa_json_get(doc, row, "name"), error);
        profile->cvars[i].value = string(doc, qa_json_get(doc, row, "value"), error);
        ok = profile->cvars[i].name && *profile->cvars[i].name && profile->cvars[i].value;
        for (size_t j = 0; ok && j < i; ++j)
            if (strcmp(profile->cvars[j].name, profile->cvars[i].name) == 0) ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate QC declared cvar");
    }
    qa_json_destroy(doc);
    if (!ok) { application_qc_release_qualification(provider); if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid QC declaration"); }
    return ok;
}
bool application_qc_player_map_ready(application_provider *provider, bool primary_character,
                                      bool source_map_owned, qa_error *error)
{
    if (!provider) return application_fail(error, QA_ERROR_ARGUMENT, "QC player provider is absent");
    if (!application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC player provider has unfinished input");
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    if (profile) return profile->clients || application_fail(error, QA_ERROR_UNSUPPORTED, "QC declaration has no client lifecycle");
    return (primary_character && source_map_owned) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "QC players on foreign maps or secondary roles require a qualified client declaration");
}
