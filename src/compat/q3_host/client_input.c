#include "internal.h"
#include "qa/input.h"
#include "qa/field.h"

static qa_physical_input physical(int32_t key)
{
    if (key >= QA_KEY_MOUSE1 && key <= QA_KEY_MOUSE5)
        return (qa_physical_input){.kind = QA_PHYSICAL_MOUSE,
            .code = qa_input_mouse_button((unsigned)(key - QA_KEY_MOUSE1 + 1))};
    return (qa_physical_input){.kind = QA_PHYSICAL_KEY, .code = (uint32_t)key};
}

static const char *binding_command(const qa_input_binding *binding)
{
    if (!binding) return "";
    if (binding->kind == QA_BIND_COMMAND) return binding->command;
    switch (binding->action) {
    case QA_INPUT_JUMP: return "+moveup";
    case QA_INPUT_CROUCH: return "+movedown";
    case QA_INPUT_NEXT_WEAPON: return "weapnext";
    case QA_INPUT_PREVIOUS_WEAPON: return "weapprev";
    case QA_INPUT_MENU: return "togglemenu";
    default: return qa_input_action_command(binding->action);
    }
}

static bool equal_fold(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned first = (uint8_t)*a++, second = (uint8_t)*b++;
        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (second >= 'A' && second <= 'Z') second += 'a' - 'A';
        if (first != second) return false;
    }
    return *a == *b;
}

static bool dictionary(qa_q3_host *host,qa_input_seat **out,qa_error *error)
{
    qa_input_seat *physical=host->options.seat,*bindings=physical;
    if (host->options.input.bindings && !host->options.input.bindings(
        host->options.input.context,physical,&bindings,error)) return false;
    if (!bindings || qa_input_seat_ordinal(bindings)!=qa_input_seat_ordinal(physical))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 binding dictionary lost its actual physical seat");
    *out=bindings; return true;
}

q3_service_result q3_client_input(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    if (role == QA_QVM_GAME) return Q3_UNHANDLED;
    bool ui = role == QA_QVM_UI;
    int32_t code = call->service;
    if (ui ? code < 33 || code > 42 : code < 60 || code > 63) return Q3_UNHANDLED;
    qa_q3_host *host = call->host;
    qa_input_seat *seat = host->options.seat;
    if (!seat) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 input seat is unbound"); return Q3_FAILED; }
    uint64_t owner = host->options.input_owner ? host->options.input_owner : host->options.service_owner;
    if (code == (ui ? 36 : 60)) {
        *result = qa_input_seat_key_down(seat, physical(q3_integer(call, 0))); return Q3_COMPLETED;
    }
    if (code == (ui ? 40 : 61)) {
        *result = (int32_t)qa_input_seat_catcher(seat, owner); return Q3_COMPLETED;
    }
    if (code == (ui ? 41 : 62))
        return qa_input_seat_set_catcher(seat, owner, (uint32_t)q3_integer(call, 0), error) ? Q3_COMPLETED : Q3_FAILED;
    if (ui && (code == 37 || code == 38)) {
        qa_text_field *field = host->options.console_field;
        if (!field) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 console field is unbound"); return Q3_FAILED; }
        if (code == 37) *result = qa_text_field_overstrike(field);
        else qa_text_field_set_overstrike(field, q3_integer(call, 0) != 0);
        return Q3_COMPLETED;
    }
    if (ui && code == 39) {
        if (!host->options.common.milliseconds && !host->options.session) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 input release clock is unbound"); return Q3_FAILED;
        }
        double time = host->options.common.milliseconds ?
            host->options.common.milliseconds(host->options.common.context) :
            (double)qa_session_elapsed(host->options.session) / 1000000.0;
        return qa_input_seat_release(seat, time, error) ? Q3_COMPLETED : Q3_FAILED;
    }
    if (ui && code == 33) {
        char name[32];
        return q3_write_string(call, call->arguments[1], qa_input_key_name(q3_integer(call, 0), name),
                                q3_integer(call, 2), error) ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_input_seat *bindings=NULL;
    if ((ui && (code==34 || code==35)) || (!ui && code==63))
        if (!dictionary(host,&bindings,error)) return Q3_FAILED;
    if (ui && code == 34) {
        const char *command = binding_command(qa_input_seat_binding(bindings, physical(q3_integer(call, 0))));
        return q3_write_string(call, call->arguments[1], command, q3_integer(call, 2), error) ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_bytes text = {0}; bool ok;
    if (ui && code == 35) {
        ok = q3_string(call, call->arguments[1], &text, error);
        if (ok) {
            qa_input_binding binding = {.input = physical(q3_integer(call, 0)), .kind = QA_BIND_COMMAND,
                                        .command = (const char *)text.data};
            ok = qa_input_seat_bind(bindings, &binding, error);
        }
    } else if (!ui) {
        ok = q3_string(call, call->arguments[0], &text, error); *result = -1;
        if (ok) for (size_t i = 0; i < qa_input_seat_binding_count(bindings); ++i) {
            const qa_input_binding *binding = qa_input_seat_binding_at(bindings, i);
            if (!equal_fold(binding_command(binding), (const char *)text.data)) continue;
            if (binding->input.kind == QA_PHYSICAL_KEY) *result = (int32_t)binding->input.code;
            else if (binding->input.kind == QA_PHYSICAL_MOUSE)
                *result = QA_KEY_MOUSE1 + (int32_t)qa_input_mouse_button(binding->input.code) - 1;
            break;
        }
    } else {
        if (!host->options.common.clipboard) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 clipboard owner is unbound"); return Q3_FAILED;
        }
        qa_buffer clipboard = {0};
        ok = host->options.common.clipboard(host->options.common.context, &clipboard, error);
        if (ok && (clipboard.size == SIZE_MAX || (clipboard.size && !clipboard.data)))
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 clipboard returned invalid text");
        if (ok) {
            char *copy = qa_arena_alloc(&host->scratch, clipboard.size + 1, 1, error);
            if (!copy) ok = false;
            else {
                if (clipboard.size) memcpy(copy, clipboard.data, clipboard.size);
                copy[clipboard.size] = 0;
                ok = q3_write_string(call, call->arguments[0], copy, q3_integer(call, 1), error);
            }
        }
        qa_buffer_free(&clipboard);
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
