#include "internal.h"

static void diagnostic(qa_bot_runtime *r, const char *message) {
    if (!r->services.diagnostic) return;
    bool busy = r->busy;
    r->busy = true;
    r->services.diagnostic(r->services.context, QA_SCRIPT_ERROR, message);
    r->busy = busy;
}
static const qa_bot_character *profile(qa_bot_runtime *r, uint32_t handle) {
    const qa_bot_character *character = qa_bot_runtime_character(r, handle);
    if (!character) diagnostic(r, "invalid bot character handle");
    return character;
}
bool qa_bot_runtime_character_float(qa_bot_runtime *r, uint32_t handle, uint32_t index,
                                    float *out, qa_error *e) {
    if (!r || !out) return bot_runtime_fail(e, "invalid runtime characteristic output");
    *out = 0;
    const qa_bot_character *character = profile(r, handle);
    if (!character) return true;
    qa_error issue = {0};
    if (!qa_bot_character_float(character, index, out, &issue)) diagnostic(r, issue.message);
    return true;
}
bool qa_bot_runtime_character_integer(qa_bot_runtime *r, uint32_t handle, uint32_t index,
                                      int32_t *out, qa_error *e) {
    if (!r || !out) return bot_runtime_fail(e, "invalid runtime characteristic output");
    *out = 0;
    const qa_bot_character *character = profile(r, handle);
    if (!character) return true;
    qa_error issue = {0};
    if (!qa_bot_character_integer(character, index, out, &issue)) diagnostic(r, issue.message);
    return true;
}
bool qa_bot_runtime_character_string(qa_bot_runtime *r, uint32_t handle, uint32_t index,
                                      const char **out, bool *written, qa_error *e) {
    if (!r || !out || !written)
        return bot_runtime_fail(e, "invalid runtime characteristic string output");
    *written = false;
    const qa_bot_character *character = profile(r, handle);
    if (!character) return true;
    qa_error issue = {0};
    if (qa_bot_character_string(character, index, out, &issue)) *written = true;
    else diagnostic(r, issue.message);
    return true;
}
bool qa_bot_runtime_character_bounded_float(qa_bot_runtime *r, uint32_t handle, uint32_t index,
                                            float minimum, float maximum, float *out, qa_error *e) {
    if (!r || !out) return bot_runtime_fail(e, "invalid runtime characteristic output");
    *out = 0;
    if (!profile(r, handle)) return true;
    if (minimum > maximum) {
        diagnostic(r, "invalid bot characteristic float bounds");
        return true;
    }
    if (!qa_bot_runtime_character_float(r, handle, index, out, e)) return false;
    if (*out < minimum) *out = minimum;
    else if (*out > maximum) *out = maximum;
    return true;
}
bool qa_bot_runtime_character_bounded_integer(qa_bot_runtime *r, uint32_t handle, uint32_t index,
                                              int32_t minimum, int32_t maximum,
                                              int32_t *out, qa_error *e) {
    if (!r || !out) return bot_runtime_fail(e, "invalid runtime characteristic output");
    *out = 0;
    if (!profile(r, handle)) return true;
    if (minimum > maximum) {
        diagnostic(r, "invalid bot characteristic integer bounds");
        return true;
    }
    if (!qa_bot_runtime_character_integer(r, handle, index, out, e)) return false;
    if (*out < minimum) *out = minimum;
    else if (*out > maximum) *out = maximum;
    return true;
}
