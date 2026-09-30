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
    static const char *const actions[QA_INPUT_ACTION_COUNT] = {
        [QA_INPUT_FORWARD]="+forward", [QA_INPUT_BACK]="+back",
        [QA_INPUT_MOVE_LEFT]="+moveleft", [QA_INPUT_MOVE_RIGHT]="+moveright",
        [QA_INPUT_MOVE_UP]="+moveup", [QA_INPUT_MOVE_DOWN]="+movedown",
        [QA_INPUT_TURN_LEFT]="+left", [QA_INPUT_TURN_RIGHT]="+right",
        [QA_INPUT_LOOK_UP]="+lookup", [QA_INPUT_LOOK_DOWN]="+lookdown",
        [QA_INPUT_JUMP]="+moveup", [QA_INPUT_CROUCH]="+movedown",
        [QA_INPUT_ATTACK]="+attack", [QA_INPUT_USE]="+use", [QA_INPUT_HOLSTER]="+holster",
        [QA_INPUT_WALK]="+speed", [QA_INPUT_STRAFE]="+strafe", [QA_INPUT_KLOOK]="+klook",
        [QA_INPUT_MLOOK]="+mlook", [QA_INPUT_BUTTON0]="+button0", [QA_INPUT_BUTTON1]="+button1",
        [QA_INPUT_BUTTON2]="+button2", [QA_INPUT_BUTTON3]="+button3", [QA_INPUT_BUTTON4]="+button4",
        [QA_INPUT_BUTTON5]="+button5", [QA_INPUT_BUTTON6]="+button6", [QA_INPUT_BUTTON7]="+button7",
        [QA_INPUT_BUTTON8]="+button8", [QA_INPUT_BUTTON9]="+button9", [QA_INPUT_BUTTON10]="+button10",
        [QA_INPUT_BUTTON11]="+button11", [QA_INPUT_BUTTON12]="+button12",
        [QA_INPUT_BUTTON13]="+button13", [QA_INPUT_BUTTON14]="+button14",
        [QA_INPUT_SCORES]="+scores", [QA_INPUT_NEXT_WEAPON]="weapnext",
        [QA_INPUT_PREVIOUS_WEAPON]="weapprev", [QA_INPUT_MENU]="togglemenu"
    };
    return !binding ? "" : binding->kind == QA_BIND_COMMAND ? binding->command : actions[binding->action];
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
    if (ui && code == 34) {
        const char *command = binding_command(qa_input_seat_binding(seat, physical(q3_integer(call, 0))));
        size_t length = strlen(command);
        char *copy = qa_arena_alloc(&host->scratch, length + 1, 1, error);
        if (!copy) return Q3_FAILED;
        memcpy(copy, command, length + 1);
        return q3_write_string(call, call->arguments[1], copy, q3_integer(call, 2), error) ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_buffer text = {0}; bool ok;
    if (ui && code == 35) {
        ok = q3_string(call, call->arguments[1], &text, error);
        if (ok) {
            qa_input_binding binding = {.input = physical(q3_integer(call, 0)), .kind = QA_BIND_COMMAND,
                                        .command = (const char *)text.data};
            ok = qa_input_seat_bind(seat, &binding, error);
        }
    } else if (!ui) {
        ok = q3_string(call, call->arguments[0], &text, error); *result = -1;
        if (ok) for (size_t i = 0; i < qa_input_seat_binding_count(seat); ++i) {
            const qa_input_binding *binding = qa_input_seat_binding_at(seat, i);
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
        ok = host->options.common.clipboard(host->options.common.context, &text, error);
        if (ok && (text.size == SIZE_MAX || (text.size && !text.data)))
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 clipboard returned invalid text");
        if (ok) {
            char *copy = qa_arena_alloc(&host->scratch, text.size + 1, 1, error);
            if (!copy) ok = false;
            else {
                if (text.size) memcpy(copy, text.data, text.size); copy[text.size] = 0;
                ok = q3_write_string(call, call->arguments[0], copy, q3_integer(call, 1), error);
            }
        }
    }
    qa_buffer_free(&text); return ok ? Q3_COMPLETED : Q3_FAILED;
}
