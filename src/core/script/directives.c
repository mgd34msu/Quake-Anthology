#include "internal.h"

static bool current_condition(qa_script *s, qa_script_location location, script_condition *out,
                              qa_error *e) {
    if (s->condition_count == 0 || script_source_pointer(s) == 0)
        return script_fail(s, location, "Misplaced script conditional", e);
    if (!script_condition_top(s,out,e)) return false;
    if (out->frame != script_current_frame(s))
        return script_fail(s, location, "Misplaced script conditional", e);
    return true;
}
static bool push_condition(qa_script *s, uint32_t type, bool skip, qa_error *e) {
    if (script_source_pointer(s) == 0)
        return script_fail(s, qa_script_position(s), "Conditional after end of source", e);
    return script_condition_push(s,type,skip,script_current_frame(s),e);
}
bool script_evaluate_stream(qa_script *s, qa_script_location location, bool integer_mode,
                            bool dollar, script_eval_value *out, qa_error *e) {
    uint32_t first=0,last=0;size_t count=0;
    unsigned depth = 1;
    bool defined = false;
    script_queued_token item;
    bool found;
    if (dollar) {
        if (!script_raw(s, &item, &found, e))
            return false;
        if (!found || !qa_script_token_is(&item.token, "("))
            return script_fail(s, location, "Dollar evaluation requires (", e);
    }
    for (;;) {
        if (!(dollar ? script_raw(s, &item, &found, e) : script_line_token(s, &item, &found, e)))
            return false;
        if (!found)
            break;
        qa_script_token *t = &item.token;
        if (t->kind == QA_SCRIPT_NAME) {
            if (defined)
                defined = false;
            else if (qa_script_token_is(t, "defined"))
                defined = true;
            else {
                script_macro *m=NULL;
                if(!script_macro_lookup(&s->macros,t->text,&m,e)) return false;
                if (m == NULL) {
                    script_fail(s, t->location, "Undefined name in expression", e);
                    return false;
                }
                if (!script_expand(s, item, m, e))
                    return false;
                if (s->empty_expansion) {
                    script_fail(s, t->location, "Empty macro in expression", e);
                    return false;
                }
                continue;
            }
        } else if (t->kind == QA_SCRIPT_PUNCTUATION || t->kind == QA_SCRIPT_NUMBER) {
            if (dollar) {
                if (qa_script_token_is(t, "(")) {
                    if (depth == UINT_MAX) {
                        script_fail(s, location, "Expression nesting overflow", e);
                        return false;
                    }
                    ++depth;
                } else if (qa_script_token_is(t, ")") && --depth == 0)
                    break;
            }
        } else {
            script_fail(s, t->location, "Invalid token in expression", e);
            return false;
        }
        script_token_record *copied;
        if(!script_heap_copy_token(&s->macros,item,&copied,e)) return false;
        /* The original append checks expansion capacity after CopyToken and
         * checks expression capacity after the new raw link is published. */
        if(count>=s->options.maximum_queued_tokens)
            return script_fail(s,t->location,"Expression queue exceeds configured limit",e);
        uint32_t pointer=copied->pointer;
        if(last) {
            script_token_record *previous=script_heap_token(&s->macros,last);
            if(!script_heap_token_bytes(&s->macros,previous,e)) return false;
            qa_store_u32le(previous->record.bytes+1064,pointer);
        } else first=pointer;
        last=pointer;
        if(++count>s->options.maximum_expression_tokens)
            return script_fail(s,t->location,"Expression token limit exceeded",e);
    }
    if (dollar && depth != 0) {
        script_fail(s, location, "Unterminated dollar expression", e);
        return false;
    }
    if(!script_expression(s,first,integer_mode,out,e)) return false;
    return script_heap_free_chain(&s->macros,first,e);
}
static size_t decimal_word(uint32_t value, char *out, unsigned width) {
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    size_t at = 0;
    while (count + at < width)
        out[at++] = '0';
    while (count != 0)
        out[at++] = digits[--count];
    return at;
}
/* Fixed two decimals from the binary value, with nearest-even rounding and no
 * locale or intermediate decimal conversion. The largest double needs 35 limbs. */
static bool fixed_decimal(double value, char out[320], size_t *length) {
    if (!isfinite(value)) {
        const char *text = isnan(value) ? "nan" : "inf";
        memcpy(out, text, 4);
        *length = 3;
        return true;
    }
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    unsigned exponent = (unsigned)((bits >> 52) & 2047);
    uint64_t significand = bits & UINT64_C(0xfffffffffffff);
    if (exponent != 0)
        significand |= UINT64_C(0x10000000000000);
    int shift = exponent == 0 ? -1074 : (int)exponent - 1075;
    uint64_t scaled = significand * 100;
    if (shift < 0) {
        unsigned down = (unsigned)-shift;
        if (down >= 64)
            scaled = 0;
        else {
            uint64_t lower = scaled >> down, mask = (UINT64_C(1) << down) - 1;
            uint64_t remainder = scaled & mask, half = UINT64_C(1) << (down - 1);
            scaled = lower + (remainder > half || (remainder == half && (lower & 1) != 0));
        }
    }
    uint32_t limbs[36] = {0};
    size_t count = 0;
    do {
        limbs[count++] = (uint32_t)(scaled % UINT64_C(1000000000));
        scaled /= UINT64_C(1000000000);
    } while (scaled != 0);
    for (int bit = 0; bit < shift; ++bit) {
        uint64_t carry = 0;
        for (size_t i = 0; i < count; ++i) {
            uint64_t doubled = (uint64_t)limbs[i] * 2 + carry;
            limbs[i] = (uint32_t)(doubled % UINT64_C(1000000000));
            carry = doubled / UINT64_C(1000000000);
        }
        if (carry != 0) {
            if (count == 36)
                return false;
            limbs[count++] = (uint32_t)carry;
        }
    }
    char digits[318];
    size_t size = decimal_word(limbs[count - 1], digits, 0);
    for (size_t i = count - 1; i != 0; --i)
        size += decimal_word(limbs[i - 1], digits + size, 9);
    size_t at = 0;
    if (size <= 2) {
        out[at++] = '0';
        out[at++] = '.';
        if (size == 1)
            out[at++] = '0';
        memcpy(out + at, digits, size);
        at += size;
    } else {
        memcpy(out, digits, size - 2);
        at = size - 2;
        out[at++] = '.';
        out[at++] = digits[size - 2];
        out[at++] = digits[size - 1];
    }
    out[at] = 0;
    *length = at;
    return true;
}
bool script_eval_directive(qa_script *s, qa_script_location location, bool integer_mode,
                           bool dollar, qa_error *e) {
    script_eval_value value;
    if (!script_evaluate_stream(s, location, integer_mode, dollar, &value, e))
        return false;
    double number = integer_mode ? value.integer : value.number, magnitude = fabs(number);
    char text[320];
    size_t size;
    if (integer_mode) {
        if (!qa_format_number(magnitude, text, e))
            return false;
        size = strlen(text);
    } else if (!fixed_decimal(magnitude, text, &size))
        return script_fail(s, location, "Evaluation output overflow", e);
    if (size >= s->options.token_limit)
        return script_fail(s, location, "Evaluation output exceeds token limit", e);
    char *stored = script_string(&s->arena, text, size, e);
    if (stored == NULL)
        return false;
    qa_script_token token = {.kind = QA_SCRIPT_NUMBER,
                             .subtype = QA_SCRIPT_DECIMAL | QA_SCRIPT_LONG |
                                        (integer_mode ? QA_SCRIPT_INTEGER : QA_SCRIPT_FLOAT),
                             .integer = 0,
                             .number = dollar ? number : magnitude,
                             .text = {(const uint8_t *)stored, size},
                             .location = qa_script_position(s)};
    double integral=fmod(trunc(token.number),4294967296.0);
    if(integral<0) integral+=4294967296.0;
    uint32_t integer=(uint32_t)integral;
    memcpy(&token.integer,&integer,4);
    const char *unsupported=dollar?NULL:"#eval and #evalfloat leave numeric fields uninitialized";
    if (!script_push(s, (script_queued_token){.token=token,.unsupported=unsupported}, e))
        return false;
    if (number < 0) {
        token = (qa_script_token){.kind = QA_SCRIPT_PUNCTUATION,
                                  .subtype = QA_SCRIPT_SUB,
                                  .text = script_bytes("-"),
                                  .location = token.location};
        if (!script_push(s, (script_queued_token){.token=token,
                .unsupported="evaluation sign token leaves numeric fields uninitialized"}, e))
            return false;
    }
    return true;
}
static bool include_directive(qa_script *s, qa_script_location location, qa_error *e) {
    script_queued_token item;
    bool found;
    if (!script_line_token(s, &item, &found, e))
        return false;
    if (!found)
        return script_fail(s, location, "Include requires a filename", e);
    qa_script_include request = {.from_path = location.path,
                                 .include_path = s->options.include_path};
    if (item.token.kind == QA_SCRIPT_STRING) {
        qa_bytes value = qa_script_token_value(&item.token);
        request.kind = QA_SCRIPT_INCLUDE_QUOTED;
        request.requested_path = script_string(&s->arena, value.data, value.size, e);
        if (request.requested_path == NULL)
            return false;
    } else if (qa_script_token_is(&item.token, "<")) {
        char *text = NULL;
        size_t size = strlen(s->options.include_path), capacity = 0;
        bool closed = false;
        if (!script_grow((void **)&text, &capacity, size + 1, 1, e))
            return false;
        memcpy(text, s->options.include_path, size);
        for (;;) {
            if (!script_line_token(s, &item, &found, e)) {
                free(text);
                return false;
            }
            if (!found)
                break;
            if (qa_script_token_is(&item.token, ">")) {
                closed = true;
                break;
            }
            if (item.token.text.size > SIZE_MAX - size - 1 ||
                !script_grow((void **)&text, &capacity, size + item.token.text.size + 1, 1, e)) {
                free(text);
                return false;
            }
            memcpy(text + size, item.token.text.data, item.token.text.size);
            size += item.token.text.size;
        }
        if (!closed)
            script_warn(s, location, "Include missing trailing >");
        if (size == strlen(s->options.include_path)) {
            free(text);
            return script_fail(s, location, "Empty system include filename", e);
        }
        request.kind = QA_SCRIPT_INCLUDE_SYSTEM;
        request.requested_path = script_string(&s->arena, text, size, e);
        free(text);
        if (request.requested_path == NULL)
            return false;
    } else
        return script_fail(s, location, "Invalid include filename", e);
    return script_include(s, &request, e);
}
bool script_directive(qa_script *s, script_queued_token hash, qa_error *e) {
    script_queued_token item;
    bool found;
    if (!script_line_token(s, &item, &found, e))
        return false;
    if (!found || item.token.kind != QA_SCRIPT_NAME)
        return script_fail(s, hash.token.location, "Preprocessor directive requires a name", e);
    qa_script_token *name = &item.token;
    qa_script_location location = name->location;
    if (qa_script_token_is(&hash.token, "$")) {
        if (!qa_script_token_is(name, "evalint") && !qa_script_token_is(name, "evalfloat"))
            return script_fail(s, location, "Unknown dollar directive", e);
        return script_eval_directive(s, location, qa_script_token_is(name, "evalint"), true, e);
    }
    if (qa_script_token_is(name, "if")) {
        script_eval_value value;
        return script_evaluate_stream(s, location, true, false, &value, e) &&
               push_condition(s, 1, value.integer == 0, e);
    }
    if (qa_script_token_is(name, "ifdef") || qa_script_token_is(name, "ifndef")) {
        bool invert = qa_script_token_is(name, "ifndef");
        if (!script_line_token(s, &item, &found, e))
            return false;
        if (!found || item.token.kind != QA_SCRIPT_NAME)
            return script_fail(s, location, "Conditional requires a macro name", e);
        script_macro *defined_macro=NULL;
        if(!script_macro_lookup(&s->macros,item.token.text,&defined_macro,e)) return false;
        bool exists=defined_macro!=NULL;
        return push_condition(s, invert ? 16 : 8, invert ? exists : !exists, e);
    }
    if (qa_script_token_is(name, "elif") || qa_script_token_is(name, "else") ||
        qa_script_token_is(name, "endif")) {
        bool end = qa_script_token_is(name, "endif"), otherwise = qa_script_token_is(name, "else");
        script_condition condition;
        if (!current_condition(s, location, &condition, e))
            return false;
        if (!end && condition.was_else)
            return script_fail(s, location, "Conditional branch after #else", e);
        bool previous_skip = condition.skip;
        if (!script_condition_pop(s,e)) return false;
        if (end)
            return true;
        bool skip;
        if (otherwise)
            skip = !previous_skip;
        else {
            script_eval_value value;
            if (!script_evaluate_stream(s, location, true, false, &value, e))
                return false;
            skip = value.integer == 0;
        }
        return push_condition(s,otherwise ? 2 : 4,skip,e);
    }
    if (qa_script_token_is(name, "include"))
        return script_skipping(s) != 0 || include_directive(s, location, e);
    if (qa_script_token_is(name, "define")) {
        if (script_skipping(s) != 0)
            return true;
        return script_define_stream(s,location,e);
    }
    if (qa_script_token_is(name, "undef")) {
        if (script_skipping(s) != 0)
            return true;
        if (!script_line_token(s, &item, &found, e))
            return false;
        if (!found || item.token.kind != QA_SCRIPT_NAME)
            return script_fail(s, location, "Undef requires a macro name", e);
        bool fixed;
        qa_error failure={0};
        (void)script_macro_remove(&s->macros,item.token.text,&fixed,&failure);
        if(failure.code!=QA_OK) {if(e) *e=failure;return false;}
        if (fixed)
            script_warn(s, location, "Cannot undefine fixed macro");
        return true;
    }
    if (qa_script_token_is(name, "eval") || qa_script_token_is(name, "evalfloat"))
        return script_eval_directive(s, location, qa_script_token_is(name, "eval"), false, e);
    if (qa_script_token_is(name, "pragma")) {
        script_warn(s, location, "Pragma directive is unsupported");
        qa_script_token *tokens;
        size_t count;
        if (!script_line(s, &tokens, &count, e))
            return false;
        free(tokens);
        return true;
    }
    if (qa_script_token_is(name, "line"))
        return script_fail(s, location, "Line directive is unsupported", e);
    if (qa_script_token_is(name, "error"))
        return script_fail(s, location, "Script #error directive", e);
    return script_fail(s, location, "Unknown preprocessor directive", e);
}
