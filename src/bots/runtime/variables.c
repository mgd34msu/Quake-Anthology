#include "internal.h"

bool qa_bot_runtime_variable_get_from(qa_bot_runtime *r, void *context,
                                       bool (*byte)(void *, size_t, uint8_t *, qa_error *),
                                       const char **out, qa_error *e) {
    if (!r || r->busy || r->restore_pending || !out)
        return bot_runtime_fail(e, "bot variable owner is absent or active");
    r->busy = true;
    const qa_bot_variable *variable;
    bool ok = qa_bot_library_variable_find_from(r->library, context, byte, &variable, e);
    if (ok) *out = variable ? variable->string : "";
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_variable_set_from(qa_bot_runtime *r, void *context,
                                       bool (*byte)(void *, size_t, uint8_t *, qa_error *),
                                       bool (*read_name)(void *, const char **, qa_error *),
                                       bool (*read_value)(void *, const char **, qa_error *),
                                       qa_error *e) {
    if (!r || r->busy || r->restore_pending || !read_name || !read_value)
        return bot_runtime_fail(e, "bot variable owner or source readers are unavailable");
    r->busy = true;
    const qa_bot_variable *existing;
    const char *name, *value;
    bool ok = qa_bot_library_variable_find_from(r->library, context, byte, &existing, e);
    if (ok) {
        if (existing) name = existing->name;
        else ok = read_name(context, &name, e);
    }
    if (ok) ok = read_value(context, &value, e);
    if (ok) ok = qa_bot_library_variable_set(r->library, name, value, e);
    r->busy = false;
    return ok;
}
