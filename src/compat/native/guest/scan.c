#include "scan.h"
#include "internal.h"
#include "qa/text.h"
#include <fenv.h>

typedef struct scan_directive {
    char code, literal;
    uint64_t width;
    size_t bytes;
    bool space, suppressed, long_float;
} scan_directive;
struct guest_scan_program {
    scan_directive *directives;
    size_t count, arguments;
    bool c23;
};
static bool scan_space(unsigned char c)
{ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static bool scan_parse(const char *format, const qa_native_target *target,
    guest_scan_program *program, qa_error *error)
{
    size_t capacity = 0;
    for (size_t at = 0; format[at]; ) {
        char c = format[at++]; scan_directive directive = {.width = UINT64_MAX, .bytes = 4};
        if (scan_space((unsigned char)c)) directive.space = true;
        else if (c != '%') directive.literal = c;
        else if (format[at] == '%') { ++at; directive.literal = '%'; }
        else {
            if (format[at] == '*') { ++at; directive.suppressed = true; }
            if (format[at] >= '0' && format[at] <= '9') {
                directive.width = 0;
                do {
                    unsigned digit = (unsigned)(format[at++] - '0');
                    if (directive.width > (UINT64_MAX - digit) / 10) goto unsupported;
                    directive.width = directive.width * 10 + digit;
                } while (format[at] >= '0' && format[at] <= '9');
                if (!directive.width) goto unsupported;
            }
            char modifier = 0;
            if (format[at] == 'h') {
                modifier = 'h'; ++at; directive.bytes = 2;
                if (format[at] == 'h') { ++at; directive.bytes = 1; }
            } else if (format[at] == 'l') {
                modifier = 'l'; ++at;
                directive.bytes = target->os == QA_NATIVE_OS_WINDOWS ? 4 : target->pointer_bytes;
                if (format[at] == 'l') { ++at; directive.bytes = 8; modifier = 'q'; }
            } else if (format[at] == 'j' || format[at] == 'z' || format[at] == 't') {
                modifier = format[at++];
                directive.bytes = modifier == 'j' ? 8 : target->pointer_bytes;
            }
            c = format[at]; if (!c) goto unsupported; ++at;
            if (strchr("fFeEgG", c) && (!modifier || modifier == 'l')) {
                directive.code = 'f'; directive.long_float = modifier == 'l';
                directive.bytes = directive.long_float ? 8 : 4;
            } else if (strchr("diuoxXn", c)) directive.code = c == 'X' ? 'x' : c;
            else if (program->c23 && (c == 'b' || c == 'B')) directive.code = 'b';
            else goto unsupported;
            if (!directive.suppressed) ++program->arguments;
        }
        if (!guest_grow((void **)&program->directives, &capacity, program->count + 1,
            sizeof(*program->directives), error)) return false;
        program->directives[program->count++] = directive; continue;
unsupported:
        return guest_fail(error, QA_ERROR_UNSUPPORTED, at, "Unsupported guest scanf conversion or width");
    }
    return true;
}
void guest_scan_destroy(guest_scan_program *program)
{ if (program) { free(program->directives); free(program); } }
size_t guest_scan_argument_count(const guest_scan_program *program)
{ return program->arguments; }
bool guest_scan_prepare(const qa_native_guest *guest, uint64_t format, bool c23,
    guest_scan_program **out, qa_error *error)
{
    char *text = NULL; size_t capacity = 0, count = 0;
    for (;;) {
        char byte;
        if (!qa_native_guest_read(guest, format + count, &byte, 1, error) ||
            !guest_grow((void **)&text, &capacity, count + 1, 1, error)) {
            free(text); return false;
        }
        text[count++] = byte;
        if (!byte) break;
    }
    guest_scan_program *program = calloc(1, sizeof(*program));
    if (!program) { free(text); return guest_fail(error, QA_ERROR_MEMORY, 0, "Preparing guest scanner"); }
    program->c23 = c23;
    bool okay = scan_parse(text, &guest->options.image.target, program, error);
    free(text);
    if (!okay) { guest_scan_destroy(program); return false; }
    *out = program; return true;
}
typedef struct scan_input { qa_native_guest *guest; uint64_t address, capacity, cursor; } scan_input;
static bool scan_peek(scan_input *input, unsigned char *out, qa_error *error)
{
    *out = 0;
    if (input->cursor >= input->capacity) return true;
    return qa_native_guest_read(input->guest, input->address + input->cursor, out, 1, error);
}
static bool scan_store(qa_native_guest *guest, uint64_t address, size_t bytes,
    uint64_t bits, qa_error *error)
{
    uint8_t data[8];
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(bits >> (i * 8));
    return qa_native_guest_write(guest, address, (qa_bytes){data, bytes}, error);
}
static int scan_digit(unsigned char c)
{ return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99; }
static bool scan_take(scan_input *input, char **token, size_t *count, size_t *capacity,
    unsigned char c, qa_error *error)
{
    if (!guest_grow((void **)token, capacity, *count + 2, 1, error)) return false;
    (*token)[(*count)++] = (char)c; (*token)[*count] = 0; ++input->cursor; return true;
}
bool guest_scan_execute(const guest_scan_program *program, qa_native_guest *guest,
    uint64_t address, uint64_t capacity, guest_scan_destination_fn destination_read,
    void *context, int32_t *out, qa_error *error)
{
    scan_input input = {guest, address, capacity, 0};
    size_t count = program->count; int32_t assigned = 0;
    bool converted = false, okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        scan_directive directive = program->directives[i]; unsigned char c;
        if (directive.code == 'n') {
            if (!directive.suppressed) {
                uint64_t destination;
                okay = destination_read(context, &destination, error) &&
                    scan_store(guest, destination, directive.bytes, input.cursor, error);
            }
            continue;
        }
        if (!scan_peek(&input, &c, error)) { okay = false; break; }
        if (directive.space || directive.code) {
            while (scan_space(c)) { ++input.cursor; if (!scan_peek(&input, &c, error)) { okay = false; break; } }
            if (!okay) break;
        }
        if (directive.space) continue;
        if (directive.literal) {
            if (c != (unsigned char)directive.literal) { if (!converted && !c) assigned = -1; break; }
            ++input.cursor; continue;
        }
        if (!c) { if (!converted) assigned = -1; break; }
        uint64_t start = input.cursor; char *token = NULL; size_t token_count = 0, token_capacity = 0;
        bool negative = c == '-';
        if (c == '+' || c == '-') {
            if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { free(token); okay = false; break; }
        }
        if (input.cursor - start >= directive.width) c = 0;
        bool valid = true;
        int base = directive.code == 'o' ? 8 : directive.code == 'x' ? 16 : directive.code == 'b' ? 2 : 10;
        size_t digits = 0;
        if (directive.code == 'f') {
            if (c == 'i' || c == 'I' || c == 'n' || c == 'N') {
                while (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && token_count < 9) {
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
                const char *word = token ? token + (negative || token[0] == '+') : "";
                bool nonfinite = strlen(word) >= 3 &&
                    (((word[0] | 32) == 'i' && (word[1] | 32) == 'n' && (word[2] | 32) == 'f') ||
                     ((word[0] | 32) == 'n' && (word[1] | 32) == 'a' && (word[2] | 32) == 'n'));
                if (okay && nonfinite) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, input.address + start, "Guest scanf nonfinite text is not implemented");
                free(token); break;
            }
            while (c >= '0' && c <= '9') {
                ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                if (input.cursor - start >= directive.width) c = 0;
            }
            const char *unsigned_token = token ? token + (negative || token[0] == '+') : "";
            if (okay && !strcmp(unsigned_token, "0") && (c == 'x' || c == 'X')) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, input.address + start, "Guest scanf hexadecimal floating input is not implemented");
            if (okay && c == '.') {
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                while (okay && c >= '0' && c <= '9') {
                    ++digits;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
            }
            if (okay && (c == 'e' || c == 'E')) {
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                if (okay && (c == '+' || c == '-')) {
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                    if (input.cursor - start >= directive.width) c = 0;
                }
                size_t exponent_digits = 0;
                while (okay && c >= '0' && c <= '9') {
                    ++exponent_digits;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
                if (!exponent_digits) valid = false;
            }
            if (!digits) valid = false;
        } else {
            if ((directive.code == 'i' || directive.code == 'x' || directive.code == 'b') && c == '0') {
                if (directive.code == 'i') base = 8;
                ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                if (okay && (((c == 'x' || c == 'X') && directive.code != 'b') ||
                    ((c == 'b' || c == 'B') && (directive.code == 'b' ||
                        (program->c23 && directive.code == 'i'))))) {
                    base = c == 'b' || c == 'B' ? 2 : 16; digits = 0;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                    if (input.cursor - start >= directive.width) c = 0;
                }
            }
            while (okay && scan_digit(c) < base) {
                ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                if (input.cursor - start >= directive.width) c = 0;
            }
            if (!digits) valid = false;
        }
        if (!okay || !valid) { free(token); break; }
        converted = true;
        if (!directive.suppressed) {
            uint64_t destination;
            if (!destination_read(context, &destination, error)) { free(token); okay = false; break; }
            if (!destination) { free(token); okay = guest_fail(error,QA_ERROR_ARGUMENT,0,"Guest scanf destination is null"); break; }
            if (directive.code == 'f') {
                fenv_t saved; bool held = feholdexcept(&saved) == 0;
                if (!held || fesetround(FE_TONEAREST) != 0) { free(token); okay = guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "host cannot provide nearest decimal scan rounding"); if (held) fesetenv(&saved); break; }
                uint64_t bits = 0;
                if (directive.long_float) { double value; okay = qa_parse_atof(token, &value, error); if (okay) memcpy(&bits, &value, 8); }
                else { float value; uint32_t encoded; okay = qa_parse_atof_float(token, &value, error); if (okay) { memcpy(&encoded, &value, 4); bits = encoded; } }
                if (fesetenv(&saved) != 0) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "host decimal scan environment restore failed");
                if (okay) okay = scan_store(guest, destination, directive.long_float ? 8 : 4, bits, error);
            } else {
                const char *at = token + (negative || token[0] == '+');
                if ((base == 16 && at[0] == '0' && (at[1] == 'x' || at[1] == 'X')) ||
                    (base == 2 && at[0] == '0' && (at[1] == 'b' || at[1] == 'B'))) at += 2;
                uint64_t value = 0; while (*at) { value = value * (uint64_t)base + (uint32_t)scan_digit((unsigned char)*at++); }
                if (negative) value = UINT64_C(0) - value;
                okay = scan_store(guest, destination, directive.bytes, value, error);
            }
            if (okay) ++assigned;
        }
        free(token);
    }
    *out = assigned;
    return okay;
}
