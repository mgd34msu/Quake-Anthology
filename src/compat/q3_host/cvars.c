#include "internal.h"

/* Registry handles are nonnegative size_t values admitted through INT32_MAX.
 * This opaque signed source token cannot alias any real private handle. */
enum { ENGINE_CHEATS_HANDLE = -2 };

static bool engine_name(q3_call *call, const char *name)
{
    qa_q3_host_options *options = &call->host->options;
    const char *engine = "sv_cheats";
    const unsigned char *key = (const unsigned char *)name;
    while (*key && *engine) {
        unsigned char character = *key++;
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != (unsigned char)*engine++) return false;
    }
    return !*key && !*engine && options->role == QA_QVM_GAME && options->engine_cvars;
}

static qa_cvars *named_owner(q3_call *call, const char *name)
{
    return engine_name(call, name) ? call->host->options.engine_cvars : call->host->options.cvars;
}

static bool update(q3_call *call, uint64_t pointer, qa_error *error)
{
    uint8_t input[272];
    if (!q3_read(call, pointer, input, sizeof(input), error)) return false;
    int32_t handle = qa_load_i32le(input);
    const qa_cvar_view *view = handle == ENGINE_CHEATS_HANDLE &&
        call->host->options.role == QA_QVM_GAME && call->host->options.engine_cvars
        ? qa_cvars_find(call->host->options.engine_cvars, "sv_cheats")
        : handle >= 0 ? qa_cvars_handle(call->host->options.cvars, (size_t)handle) : NULL;
    if (!view || (uint32_t)view->modification_count == qa_load_u32le(input + 4)) return true;
    size_t length = strlen(view->value);
    uint32_t modification = (uint32_t)view->modification_count;
    int32_t integer = view->integer;
    uint32_t bits; memcpy(&bits, &view->number, sizeof(bits));
    uint8_t value[256] = {0};
    if (length <= 255) memcpy(value, view->value, length);
    if (!q3_write_word(call, pointer + 4, modification, error)) return false;
    if (length > 255)
        return q3_fail(error, QA_ERROR_FORMAT, length, "Cvar_Update exceeds MAX_CVAR_VALUE_STRING");
    return q3_write(call, pointer + 16, (qa_bytes){value, sizeof(value)}, error) &&
           q3_write_word(call, pointer + 8, bits, error) &&
           q3_write_word(call, pointer + 12, (uint32_t)integer, error);
}

static bool register_vm(q3_call *call, qa_error *error)
{
    qa_buffer name = {0}, value = {0};
    bool ok = q3_string(call, call->arguments[1], &name, error) &&
              q3_string(call, call->arguments[2], &value, error);
    qa_cvars *cvars = ok ? named_owner(call, (const char *)name.data) : NULL;
    bool engine = ok && engine_name(call, (const char *)name.data);
    if (ok) ok = qa_cvars_register(cvars, (const char *)name.data,
                                    (const char *)value.data, (uint32_t)call->arguments[3],
                                    engine ? 0 : call->host->options.service_owner, NULL, error);
    if (ok && !engine) ok = qa_cvars_retain_shared(cvars, (const char *)name.data, error);
    if (ok && call->arguments[0]) {
        const qa_cvar_view *view = qa_cvars_find(cvars, (const char *)name.data);
        uint8_t admitted[272];
        if (!view || (!engine && view->handle > INT32_MAX))
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 cvar handle exceeds its source width");
        else {
            uint32_t handle = engine ? (uint32_t)ENGINE_CHEATS_HANDLE : (uint32_t)view->handle;
            ok = q3_read(call, call->arguments[0], admitted, sizeof(admitted), error) &&
                 q3_write_word(call, call->arguments[0], handle, error) &&
                 q3_write_word(call, call->arguments[0] + 4, UINT32_MAX, error) &&
                 update(call, call->arguments[0], error);
        }
    }
    qa_buffer_free(&name); qa_buffer_free(&value);
    return ok;
}

q3_service_result q3_cvars(q3_call *call, int32_t *result, qa_error *error)
{
    bool ui = call->host->options.role == QA_QVM_UI;
    bool game = call->host->options.role == QA_QVM_GAME;
    int32_t trap = call->service;
    if (ui ? !((trap >= 3 && trap <= 9) || trap == 50 || trap == 51)
           : !(trap >= 3 && trap <= (game ? 7 : 6))) return Q3_UNHANDLED;
    qa_cvars *cvars = call->host->options.cvars;
    if (!cvars) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 cvar owner is unbound");
        return Q3_FAILED;
    }
    if (trap == (ui ? 50 : 3)) return register_vm(call, error) ? Q3_COMPLETED : Q3_FAILED;
    if (trap == (ui ? 51 : 4)) return update(call, call->arguments[0], error) ? Q3_COMPLETED : Q3_FAILED;
    if (ui && trap == 9) {
        qa_buffer info = {0};
        bool ok = qa_cvars_info(cvars, (uint32_t)call->arguments[0], 8192, &info, error) &&
                  q3_write_string(call, call->arguments[1], (const char *)info.data,
                                    q3_integer(call, 2), error);
        qa_buffer_free(&info);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_buffer name = {0}, value = {0};
    if (!q3_string(call, call->arguments[0], &name, error)) return Q3_FAILED;
    const char *key = (const char *)name.data;
    cvars = named_owner(call, key);
    const qa_cvar_view *view = qa_cvars_find(cvars, key);
    bool ok = true;
    if (trap == (ui ? 3 : 5)) {
        ok = call->arguments[1] ?
                 q3_string(call, call->arguments[1], &value, error) &&
                 qa_cvars_set(cvars, key, (const char *)value.data, true, error) :
                 qa_cvars_reset(cvars, key, true, error);
    } else if (trap == (ui ? 5 : game ? 7 : 6)) {
        if (view) ok = q3_write_string(call, call->arguments[1], view->value, q3_integer(call, 2), error);
        else {
            const uint8_t zero = 0;
            ok = q3_write(call, call->arguments[1], (qa_bytes){&zero, 1}, error);
        }
    } else if (ui && trap == 4) {
        float number = view ? view->number : 0;
        memcpy(result, &number, sizeof(number));
    } else if (game && trap == 6) {
        if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
            float number = view ? view->number : 0;
            memcpy(result, &number, sizeof(number));
        } else *result = view ? view->integer : 0;
    } else if (ui && trap == 6) {
        ok = qa_cvars_set_number(cvars, key, q3_float(call, 1), error);
    } else if (ui && trap == 7) {
        ok = qa_cvars_reset(cvars, key, false, error);
    } else if (ui && trap == 8) {
        ok = q3_string(call, call->arguments[1], &value, error) &&
             qa_cvars_register(cvars, key, (const char *)value.data,
                                (uint32_t)call->arguments[2], call->host->options.service_owner, NULL, error) &&
             qa_cvars_retain_shared(cvars, key, error);
    }
    qa_buffer_free(&name); qa_buffer_free(&value);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
