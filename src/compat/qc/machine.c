/* Quake pr_exec.c bytecode execution. */
#include "internal.h"
#include "qa/text.h"

#include <math.h>
#include <stdio.h>

static bool runtime_fail(qa_qc_instance *instance, qa_error *error,
                         const char *message)
{
    uint32_t function = 0, statement = 0;
    if (instance != NULL && instance->frame_count != 0) {
        const qc_frame *frame = &instance->frames[instance->frame_count - 1u];
        function = frame->function;
        statement = frame->statement;
    }
    qa_error_set(error, QA_ERROR_FORMAT, statement,
                 "QuakeC runtime error in function %u: %s", function, message);
    return false;
}

static bool global_words(qa_qc_instance *instance, uint32_t word,
                         uint32_t count, uint32_t output[3], qa_error *error)
{
    if (!qc_global_range(instance, word, count, error)) return false;
    for (uint32_t i = 0; i < count; ++i) output[i] = qc_load_word(instance->globals, word + i);
    return true;
}

static bool store_words(qa_qc_instance *instance, uint32_t word,
                        const uint32_t values[3], uint32_t count,
                        qa_error *error)
{
    return qc_write_global(instance, word, values, count, error);
}

static bool store_float(qa_qc_instance *instance, uint32_t word,
                        float value, qa_error *error)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return qc_write_global(instance, word, &bits, 1, error);
}

static bool read_float(qa_qc_instance *instance, uint32_t word,
                       float *out, qa_error *error)
{
    if (!qc_global_range(instance, word, 1, error)) return false;
    *out = qc_load_float(instance->globals, word);
    return true;
}

static bool read_int(qa_qc_instance *instance, uint32_t word,
                     int32_t *out, qa_error *error)
{
    if (!qc_global_range(instance, word, 1, error)) return false;
    *out = qc_load_int(instance->globals, word);
    return true;
}

static bool read_vector(qa_qc_instance *instance, uint32_t word,
                        qa_vec3 *out, qa_error *error)
{
    if (!qc_global_range(instance, word, 3, error)) return false;
    *out = qa_v3(qc_load_float(instance->globals, word),
                 qc_load_float(instance->globals, word + 1u),
                 qc_load_float(instance->globals, word + 2u));
    return true;
}

static bool write_vector(qa_qc_instance *instance, uint32_t word,
                         qa_vec3 value, qa_error *error)
{
    uint32_t bits[3];
    memcpy(&bits[0], &value.x, 4); memcpy(&bits[1], &value.y, 4);
    memcpy(&bits[2], &value.z, 4);
    return qc_write_global(instance, word, bits, 3, error);
}



static int byte_compare(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return (int)*a - (int)*b;
}

static bool notify_call(qa_qc_instance *instance, qa_qc_call_observer_fn call,
                        const qa_qc_call_event *event, qa_error *error)
{
    if (call == NULL) return true;
    qa_error failure = {0};
    ++instance->callback_depth;
    bool ok = call(instance->options.observers.context, instance, event,
                   &failure);
    --instance->callback_depth;
    if (ok) return true;
    if (failure.code == QA_OK)
        runtime_fail(instance, &failure, "call observer failed");
    if (error != NULL) *error = failure;
    return false;
}

static void capture_staging(const qa_qc_instance *instance, qc_boundary *boundary)
{
    for (uint32_t i = 0; i < 24; ++i)
        boundary->staging[i] = qc_load_word(instance->globals, 4u + i);
    boundary->staged_argument_count = instance->argument_count;
}

static void restore_staging(qa_qc_instance *instance, const qc_boundary *boundary)
{
    for (uint32_t i = 0; i < 24; ++i)
        qc_store_word(instance->globals, 4u + i, boundary->staging[i]);
    instance->argument_count = boundary->argument_count;
}

static void capture_result(const qa_qc_instance *instance, qc_boundary *boundary)
{
    for (uint32_t i = 0; i < 3; ++i)
        boundary->result[i] = qc_load_word(instance->globals, QC_RETURN_WORD + i);
    boundary->result_valid = true;
}

static void restore_result(qa_qc_instance *instance, const qc_boundary *boundary)
{
    if (!boundary->result_valid) return;
    for (uint32_t i = 0; i < 3; ++i)
        qc_store_word(instance->globals, QC_RETURN_WORD + i, boundary->result[i]);
}

static bool enter_frame(qa_qc_instance *instance, uint32_t function,
                        uint32_t argument_count, qa_error *error)
{
    const qa_qc_function *fn = qa_qc_program_function(instance->program, function);
    if (fn == NULL || function == 0 || fn->first_statement < 0
        || fn->named_builtin)
        return runtime_fail(instance, error, "invalid interpreted function");
    if (instance->frame_count >= instance->options.call_limit)
        return runtime_fail(instance, error, "call stack overflow");
    if (fn->local_words > instance->options.local_word_limit - instance->local_count)
        return runtime_fail(instance, error, "locals stack overflow");

    qc_frame *frame = &instance->frames[instance->frame_count++];
    *frame = (qc_frame){function, (uint32_t)fn->first_statement,
                        argument_count, instance->local_count, fn->local_words};
    for (uint32_t i = 0; i < fn->local_words; ++i)
        instance->locals[instance->local_count + i] =
            qc_load_word(instance->globals, fn->parameter_start + i);
    instance->local_count += fn->local_words;

    uint32_t destination = fn->parameter_start;
    for (uint32_t parameter = 0; parameter < fn->parameter_count; ++parameter) {
        uint32_t size = fn->parameter_sizes[parameter];
        for (uint32_t i = 0; i < size; ++i)
            qc_store_word(instance->globals, destination + i,
                          qc_load_word(instance->globals,
                                       QC_ARGUMENT_WORD(parameter) + i));
        destination += size;
    }
    instance->argument_count = argument_count;
    return true;
}

static void leave_frame(qa_qc_instance *instance)
{
    qc_frame *frame = &instance->frames[instance->frame_count - 1u];
    const qa_qc_function *fn = qa_qc_program_function(instance->program,
                                                       frame->function);
    for (uint32_t i = 0; i < frame->local_count; ++i)
        qc_store_word(instance->globals, fn->parameter_start + i,
                      instance->locals[frame->local_base + i]);
    instance->local_count = frame->local_base;
    --instance->frame_count;
    instance->argument_count = instance->frame_count == 0 ? 0
        : instance->frames[instance->frame_count - 1u].argument_count;
}

static bool state_opcode(qa_qc_instance *instance,
                         const qa_qc_statement *statement, qa_error *error)
{
    const qa_qc_definition *self = instance->program->engine_globals.self;
    const qa_qc_definition *time = instance->program->engine_globals.time;
    const qa_qc_definition *nextthink = instance->program->engine_fields.nextthink;
    const qa_qc_definition *frame = instance->program->engine_fields.frame;
    const qa_qc_definition *think = instance->program->engine_fields.think;
    if (self == NULL || time == NULL || nextthink == NULL || frame == NULL || think == NULL)
        return runtime_fail(instance, error, "STATE needs self/time/frame/think fields");
    if (self->type != QA_QC_ENTITY || time->type != QA_QC_FLOAT
        || nextthink->type != QA_QC_FLOAT || frame->type != QA_QC_FLOAT
        || think->type != QA_QC_FUNCTION)
        return runtime_fail(instance, error,
                            "STATE globals or fields have incompatible types");
    int32_t reference;
    float now, frame_value;
    int32_t think_value;
    if (!read_int(instance, self->offset, &reference, error)
        || !read_float(instance, time->offset, &now, error)
        || !read_float(instance, statement->a, &frame_value, error)
        || !read_int(instance, statement->b, &think_value, error)) return false;
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    qc_slot binding = instance->slots[slot];
    uint32_t bits;
    float next = now + 0.1f;
    memcpy(&bits, &next, 4);
    if (!qc_write_entity(instance, slot, nextthink->offset, &bits, 1, error)) return false;
    if (instance->slots[slot].kind != binding.kind
        || ((binding.kind == QA_QC_SLOT_OWNED
             || binding.kind == QA_QC_SLOT_BORROWED)
            && !qa_actor_id_equal(instance->slots[slot].actor, binding.actor)))
        return runtime_fail(instance, error,
                            "STATE entity changed during nextthink store");
    memcpy(&bits, &frame_value, 4);
    if (!qc_write_entity(instance, slot, frame->offset, &bits, 1, error)) return false;
    if (instance->slots[slot].kind != binding.kind
        || ((binding.kind == QA_QC_SLOT_OWNED
             || binding.kind == QA_QC_SLOT_BORROWED)
            && !qa_actor_id_equal(instance->slots[slot].actor, binding.actor)))
        return runtime_fail(instance, error,
                            "STATE entity changed during frame store");
    bits = (uint32_t)think_value;
    if (!qc_write_entity(instance, slot, think->offset, &bits, 1, error)) return false;
    if (instance->slots[slot].kind != binding.kind
        || ((binding.kind == QA_QC_SLOT_OWNED
             || binding.kind == QA_QC_SLOT_BORROWED)
            && !qa_actor_id_equal(instance->slots[slot].actor, binding.actor)))
        return runtime_fail(instance, error,
                            "STATE entity changed during think store");
    return true;
}

bool qa_qc_program_statement_access(const qa_qc_program *program, uint32_t index,
    qa_qc_statement_access *out, qa_error *error)
{
    typedef struct operand_widths { uint8_t ra, rb, wb, wc, result, arguments; } operand_widths;
    static const operand_widths effects[] = {
#define QA_QC_OPCODE(name, number, ra, rb, wb, wc, result, arguments) [QA_QC_##name] = {ra, rb, wb, wc, result, arguments},
QA_QC_OPCODE_LIST(QA_QC_OPCODE)
#undef QA_QC_OPCODE
    };
    const qa_qc_statement *statement = qa_qc_program_statement(program, index);
    if (!statement || !out || (uint32_t)statement->opcode >= sizeof(effects) / sizeof(*effects))
        return qc_fail(error, QA_ERROR_ARGUMENT, index, "invalid compiled QuakeC statement effect request");
    *out = (qa_qc_statement_access){0};
    const operand_widths *effect = effects + statement->opcode;
    for (uint32_t i = 0; i < effect->ra; ++i) out->read[out->read_count++] = (uint32_t)statement->a + i;
    for (uint32_t i = 0; i < effect->rb; ++i) out->read[out->read_count++] = (uint32_t)statement->b + i;
    for (uint32_t i = 0; i < (uint32_t)effect->arguments * 3; ++i) out->read[out->read_count++] = 4 + i;
    for (uint32_t i = 0; i < effect->wb; ++i) out->write[out->write_count++] = (uint32_t)statement->b + i;
    for (uint32_t i = 0; i < effect->wc; ++i) out->write[out->write_count++] = (uint32_t)statement->c + i;
    for (uint32_t i = 0; i < effect->result; ++i) out->write[out->write_count++] = 1 + i;
    uint32_t words = qa_qc_program_describe(program).global_words;
    for (uint32_t i = 0; i < out->read_count; ++i)
        if (out->read[i] >= words) return qc_fail(error, QA_ERROR_FORMAT, index, "QuakeC statement reads outside its authored globals");
    for (uint32_t i = 0; i < out->write_count; ++i)
        if (out->write[i] >= words) return qc_fail(error, QA_ERROR_FORMAT, index, "QuakeC statement writes outside its authored globals");
    return true;
}

static bool execute_statement(qa_qc_instance *instance,
                              const qa_qc_statement *s, bool *returned,
                              bool *pc_set,
                              qa_error *error)
{
    float af, bf;
    int32_t ai, bi;
    qa_vec3 av, bv, cv;
    uint32_t words[3], slot, field;
    const char *as, *bs;
    qc_frame *frame = &instance->frames[instance->frame_count - 1u];
    *returned = false;
    *pc_set = false;

    switch (s->opcode) {
    case QA_QC_ADD_F: case QA_QC_SUB_F: case QA_QC_MUL_F: case QA_QC_DIV_F:
        if (!read_float(instance, s->a, &af, error) || !read_float(instance, s->b, &bf, error)) return false;
        if (s->opcode == QA_QC_ADD_F) af += bf;
        else if (s->opcode == QA_QC_SUB_F) af -= bf;
        else if (s->opcode == QA_QC_MUL_F) af *= bf;
        else af /= bf;
        return store_float(instance, s->c, af, error);
    case QA_QC_ADD_V: case QA_QC_SUB_V:
        if (!read_vector(instance, s->a, &av, error) || !read_vector(instance, s->b, &bv, error)) return false;
        cv = s->opcode == QA_QC_ADD_V ? qa_vec_add(av, bv) : qa_vec_sub(av, bv);
        return write_vector(instance, s->c, cv, error);
    case QA_QC_MUL_V:
        if (!read_vector(instance, s->a, &av, error) || !read_vector(instance, s->b, &bv, error)) return false;
        return store_float(instance, s->c, qa_vec_dot(av, bv), error);
    case QA_QC_MUL_FV:
        if (!read_float(instance, s->a, &af, error) || !read_vector(instance, s->b, &bv, error)) return false;
        return write_vector(instance, s->c, qa_vec_scale(bv, af), error);
    case QA_QC_MUL_VF:
        if (!read_vector(instance, s->a, &av, error) || !read_float(instance, s->b, &bf, error)) return false;
        return write_vector(instance, s->c, qa_vec_scale(av, bf), error);
    case QA_QC_EQ_F: case QA_QC_NE_F: case QA_QC_LE: case QA_QC_GE: case QA_QC_LT: case QA_QC_GT:
        if (!read_float(instance, s->a, &af, error) || !read_float(instance, s->b, &bf, error)) return false;
        if (s->opcode == QA_QC_EQ_F) af = af == bf;
        else if (s->opcode == QA_QC_NE_F) af = af != bf;
        else if (s->opcode == QA_QC_LE) af = af <= bf;
        else if (s->opcode == QA_QC_GE) af = af >= bf;
        else if (s->opcode == QA_QC_LT) af = af < bf;
        else af = af > bf;
        return store_float(instance, s->c, af, error);
    case QA_QC_EQ_V: case QA_QC_NE_V:
        if (!read_vector(instance, s->a, &av, error) || !read_vector(instance, s->b, &bv, error)) return false;
        ai = av.x == bv.x && av.y == bv.y && av.z == bv.z;
        return store_float(instance, s->c,
                           (float)(s->opcode == QA_QC_EQ_V ? ai : !ai), error);
    case QA_QC_EQ_S: case QA_QC_NE_S:
        if (!read_int(instance, s->a, &ai, error) || !read_int(instance, s->b, &bi, error)
            || !qc_strings_get(&instance->strings, ai, &as, error)
            || !qc_strings_get(&instance->strings, bi, &bs, error)) return false;
        ai = byte_compare(as, bs);
        return store_float(instance, s->c,
                           s->opcode == QA_QC_EQ_S ? (float)(ai == 0) : (float)ai,
                           error);
    case QA_QC_EQ_E: case QA_QC_EQ_FN: case QA_QC_NE_E: case QA_QC_NE_FN:
        if (!read_int(instance, s->a, &ai, error) || !read_int(instance, s->b, &bi, error)) return false;
        return store_float(instance, s->c,
                           (float)((s->opcode == QA_QC_EQ_E || s->opcode == QA_QC_EQ_FN)
                                   ? ai == bi : ai != bi), error);
    case QA_QC_NOT_F:
        return read_float(instance, s->a, &af, error)
            && store_float(instance, s->c, af == 0.0f, error);
    case QA_QC_NOT_V:
        return read_vector(instance, s->a, &av, error)
            && store_float(instance, s->c, av.x == 0 && av.y == 0 && av.z == 0, error);
    case QA_QC_NOT_S:
        if (!read_int(instance, s->a, &ai, error) || !qc_strings_get(&instance->strings, ai, &as, error)) return false;
        return store_float(instance, s->c, ai == 0 || *as == '\0', error);
    case QA_QC_NOT_ENT:
        if (!read_int(instance, s->a, &ai, error) || !qc_entity_slot(instance, ai, &slot, error)) return false;
        return store_float(instance, s->c, slot == 0, error);
    case QA_QC_NOT_FN:
        return read_int(instance, s->a, &ai, error)
            && store_float(instance, s->c, ai == 0, error);
    case QA_QC_AND: case QA_QC_OR:
        if (!read_float(instance, s->a, &af, error) || !read_float(instance, s->b, &bf, error)) return false;
        return store_float(instance, s->c, s->opcode == QA_QC_AND
                           ? af != 0 && bf != 0 : af != 0 || bf != 0, error);
    case QA_QC_BITAND: case QA_QC_BITOR:
        if (!read_float(instance, s->a, &af, error) || !read_float(instance, s->b, &bf, error)) return false;
        ai = s->opcode == QA_QC_BITAND ? qa_source_float_to_i32(af) & qa_source_float_to_i32(bf)
                                      : qa_source_float_to_i32(af) | qa_source_float_to_i32(bf);
        return store_float(instance, s->c, (float)ai, error);
    case QA_QC_STORE_F: case QA_QC_STORE_S: case QA_QC_STORE_ENT:
    case QA_QC_STORE_FLD: case QA_QC_STORE_FN:
        if (!qc_global_range(instance, s->a, 1, error)) return false;
        return qc_write_global(instance, s->b,
            (const uint32_t *)(const void *)(instance->globals + (size_t)s->a * 4u),
            1, error);
    case QA_QC_STORE_V:
        if (!qc_global_range(instance, s->a, 3, error)) return false;
        return qc_write_global(instance, s->b,
            (const uint32_t *)(const void *)(instance->globals + (size_t)s->a * 4u),
            3, error);
    case QA_QC_LOAD_F: case QA_QC_LOAD_S: case QA_QC_LOAD_ENT:
    case QA_QC_LOAD_FLD: case QA_QC_LOAD_FN: case QA_QC_LOAD_V:
        if (!read_int(instance, s->a, &ai, error) || !read_int(instance, s->b, &bi, error)
            || bi < 0 || !qc_entity_slot(instance, ai, &slot, error)) return false;
        field = (uint32_t)bi;
        ai = s->opcode == QA_QC_LOAD_V ? 3 : 1;
        if (!qc_prepare_entity_access(instance, slot, field, (uint32_t)ai,
                                      QA_QC_ENTITY_READ, error)) return false;
        for (int32_t i = 0; i < ai; ++i) words[i] = qc_load_word(qc_entity_words(instance, slot), field + (uint32_t)i);
        return store_words(instance, s->c, words, (uint32_t)ai, error);
    case QA_QC_ADDRESS: {
        if (!read_int(instance, s->a, &ai, error) || !read_int(instance, s->b, &bi, error)
            || bi < 0 || !qc_entity_slot(instance, ai, &slot, error)
            || !qc_entity_range(instance, slot, (uint32_t)bi, 1, error)) return false;
        bool active = instance->options.host.server_active == NULL
            || instance->options.host.server_active(
                   instance->options.host.context);
        if (slot == 0 && active)
            return runtime_fail(instance, error,
                                "assignment to world entity");
        int64_t pointer = (int64_t)ai + instance->layout.variables_offset_bytes + (int64_t)bi * 4;
        if (pointer > INT32_MAX) return runtime_fail(instance, error, "entity pointer overflow");
        words[0] = (uint32_t)(int32_t)pointer;
        return store_words(instance, s->c, words, 1, error);
    }
    case QA_QC_STOREP_F: case QA_QC_STOREP_S: case QA_QC_STOREP_ENT:
    case QA_QC_STOREP_FLD: case QA_QC_STOREP_FN: case QA_QC_STOREP_V:
        if (!read_int(instance, s->b, &bi, error)) return false;
        ai = s->opcode == QA_QC_STOREP_V ? 3 : 1;
        if (!qc_pointer(instance, bi, (uint32_t)ai, &slot, &field, error)
            || !global_words(instance, s->a, (uint32_t)ai, words, error)) return false;
        return qc_write_entity(instance, slot, field, words, (uint32_t)ai, error);
    case QA_QC_IF: case QA_QC_IFNOT:
        if (!read_int(instance, s->a, &ai, error)) return false;
        if ((s->opcode == QA_QC_IF && ai != 0) || (s->opcode == QA_QC_IFNOT && ai == 0))
            frame->statement = (uint32_t)((int64_t)frame->statement + (int16_t)s->b);
        else ++frame->statement;
        *pc_set = true;
        return true;
    case QA_QC_GOTO:
        frame->statement = (uint32_t)((int64_t)frame->statement + (int16_t)s->a);
        *pc_set = true;
        return true;
    case QA_QC_CALL0: case QA_QC_CALL1: case QA_QC_CALL2: case QA_QC_CALL3:
    case QA_QC_CALL4: case QA_QC_CALL5: case QA_QC_CALL6: case QA_QC_CALL7:
    case QA_QC_CALL8:
        if (!read_int(instance, s->a, &ai, error) || ai <= 0) return runtime_fail(instance, error, "invalid function call");
        if (!qc_machine_call(instance, (uint32_t)ai,
                             (uint32_t)(s->opcode - QA_QC_CALL0), error)) return false;
        ++frame->statement;
        *pc_set = true;
        return true;
    case QA_QC_STATE:
        if (!state_opcode(instance, s, error)) return false;
        ++frame->statement;
        *pc_set = true;
        return true;
    case QA_QC_DONE: case QA_QC_RETURN:
        if (!qc_global_range(instance, s->a, 3, error)
            || !qc_write_global(instance, QC_RETURN_WORD,
                (const uint32_t *)(const void *)(instance->globals + (size_t)s->a * 4u),
                3, error)) return false;
        *returned = true;
        return true;
    }
    return runtime_fail(instance, error, "unsupported opcode");
}

static const qa_qc_inline_region *inline_region_at(
    const qa_qc_instance *instance, uint32_t function, uint32_t statement)
{
    for (size_t i = 0; i < instance->options.inline_region_count; ++i) {
        const qa_qc_inline_region *region = &instance->inline_regions[i];
        if (region->function == function && region->entry == statement)
            return region;
    }
    return NULL;
}

static bool run_statements(qa_qc_instance *instance,
                           qc_inline_boundary *stop,
                           bool *returned, qa_error *error);

static bool enter_inline_boundary(qa_qc_instance *instance,
                                  const qa_qc_inline_region *region,
                                  uint32_t frame_index, qa_error *error)
{
    qc_inline_boundary boundary = {
        .previous = instance->inline_boundary,
        .invocation = ++instance->next_invocation,
        .region = *region,
        .frame_index = frame_index,
        .active = true
    };
    if (boundary.invocation == 0)
        return runtime_fail(instance, error,
                            "inline continuation identities exhausted");
    qa_qc_inline_event event = {
        .region = *region,
        .depth = instance->frame_count
    };
    instance->inline_boundary = &boundary;
    qa_error failure = {0};
    ++instance->callback_depth;
    bool ok = instance->options.observers.inline_boundary(
        instance->options.observers.context, instance, &event,
        (qa_qc_inline_next){instance, boundary.invocation}, &failure);
    --instance->callback_depth;
    instance->inline_boundary = boundary.previous;
    boundary.active = false;
    if (!ok) {
        if (failure.code == QA_OK)
            runtime_fail(instance, &failure, "inline boundary failed");
        if (error != NULL) *error = failure;
        return false;
    }
    if (instance->cancelling != NULL) return true;
    if (!boundary.used || !boundary.completed)
        return runtime_fail(instance, error,
                            "inline boundary did not complete source execution");
    return true;
}

static bool run_statements(qa_qc_instance *instance,
                           qc_inline_boundary *stop,
                           bool *returned, qa_error *error)
{
    *returned = false;
    for (;;) {
        if (instance->cancelling != NULL) return true;
        if (instance->frame_count == 0)
            return runtime_fail(instance, error, "call stack underflow");
        uint32_t frame_index = instance->frame_count - 1u;
        qc_frame *frame = &instance->frames[frame_index];
        if (stop != NULL && frame_index == stop->frame_index) {
            if (frame->function != stop->region.function)
                return runtime_fail(instance, error,
                                    "inline continuation changed source frame");
            if (frame->statement == stop->region.exit) return true;
            if (frame->statement < stop->region.entry
                || frame->statement > stop->region.exit)
                return runtime_fail(instance, error,
                                    "inline source region escaped its continuation");
        }
        if (instance->statement_budget <= 1u) {
            instance->statement_budget = 0;
            return runtime_fail(instance, error, "runaway loop");
        }
        if (frame->statement >= instance->program->info.statement_count)
            return runtime_fail(instance, error, "statement outside program");
        const qa_qc_inline_region *region = inline_region_at(
            instance, frame->function, frame->statement);
        bool same_region = stop != NULL && frame_index == stop->frame_index
            && region != NULL && region->entry == stop->region.entry
            && region->function == stop->region.function;
        if (region != NULL && !same_region
            && instance->options.observers.inline_boundary != NULL) {
            if (!enter_inline_boundary(instance, region, frame_index, error))
                return false;
            continue;
        }
        --instance->statement_budget;
        if (instance->trace_enabled && instance->options.observers.trace != NULL) {
            qa_error failure = {0};
            ++instance->callback_depth;
            bool ok = instance->options.observers.trace(
                instance->options.observers.context, instance,
                frame->function, frame->statement, &failure);
            --instance->callback_depth;
            if (!ok) {
                if (failure.code == QA_OK)
                    runtime_fail(instance, &failure, "trace observer failed");
                if (error != NULL) *error = failure;
                return false;
            }
            if (instance->cancelling != NULL) return true;
        }
        ++instance->profiles[frame->function];
        const qa_qc_statement *statement =
            &instance->program->statements[frame->statement];
        bool statement_returned, pc_set;
        if (!execute_statement(instance, statement, &statement_returned,
                               &pc_set, error)) return false;
        if (instance->cancelling != NULL) return true;
        if (statement_returned) {
            if (stop != NULL)
                return runtime_fail(instance, error,
                                    "inline source region returned before its join");
            *returned = true;
            return true;
        }
        if (instance->frame_count <= frame_index)
            return runtime_fail(instance, error, "call stack underflow");
        frame = &instance->frames[frame_index];
        if (!pc_set) ++frame->statement;
    }
}

bool qc_machine_inline_continue(qa_qc_instance *instance,
                                qc_inline_boundary *boundary,
                                qa_error *error)
{
    bool returned;
    return run_statements(instance, boundary, &returned, error);
}

bool qc_machine_body(qa_qc_instance *instance, uint32_t function,
                     uint32_t argument_count, qa_error *error)
{
    const qa_qc_function *fn = qa_qc_program_function(instance->program, function);
    if (fn == NULL || function == 0) return runtime_fail(instance, error, "invalid function index");
    uint32_t saved_arguments = instance->argument_count;
    instance->argument_count = argument_count;
    if (fn->named_builtin || fn->first_statement < 0) {
        bool ok = qc_builtin_call(instance, fn->first_statement < 0 ? -fn->first_statement : 0,
                                  fn->named_builtin ? fn->name : NULL, error);
        instance->argument_count = saved_arguments;
        return ok;
    }
    uint32_t entry_depth = instance->frame_count;
    if (!enter_frame(instance, function, argument_count, error)) {
        instance->argument_count = saved_arguments;
        return false;
    }
    bool returned;
    bool ok = run_statements(instance, NULL, &returned, error);
    while (instance->frame_count > entry_depth) leave_frame(instance);
    instance->argument_count = saved_arguments;
    return ok;
}

bool qa_qc_call_continue(qa_qc_call_next next, qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_boundary *boundary = instance == NULL ? NULL : instance->boundary;
    if (boundary == NULL || !boundary->active || boundary->used
        || boundary->invocation != next.invocation)
        return runtime_fail(instance, error, "expired or repeated function continuation");
    boundary->used = true;
    restore_staging(instance, boundary);
    boundary->executing = true;
    bool ok = qc_machine_body(instance, boundary->function,
                              boundary->argument_count, error);
    boundary->executing = false;
    if (!ok) {
        if (instance->cancelling == boundary) instance->cancelling = NULL;
        return false;
    }
    if (instance->cancelling != NULL) {
        if (instance->cancelling == boundary) instance->cancelling = NULL;
        return true;
    }
    capture_result(instance, boundary);
    boundary->body_completed = true;
    return true;
}

bool qa_qc_call_completed(qa_qc_call_next next)
{
    const qa_qc_instance *instance = next.instance;
    const qc_boundary *boundary = instance == NULL ? NULL : instance->boundary;
    return boundary != NULL && boundary->active && boundary->invocation == next.invocation
        && boundary->used && boundary->body_completed && boundary->result_valid
        && !boundary->executing && !boundary->cancelled && instance->cancelling == NULL;
}

bool qa_qc_call_skip(qa_qc_call_next next, const uint32_t result[3],
                     qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_boundary *boundary = instance == NULL ? NULL : instance->boundary;
    if (boundary == NULL || !boundary->active || boundary->used
        || boundary->invocation != next.invocation || result == NULL)
        return runtime_fail(instance, error, "expired or invalid function continuation");
    boundary->used = true;
    if (!qc_write_global(instance, QC_RETURN_WORD, result, 3, error)) return false;
    capture_result(instance, boundary);
    return true;
}

bool qa_qc_call_cancel(qa_qc_call_next next, const uint32_t result[3],
                        qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_boundary *boundary = instance == NULL ? NULL : instance->boundary;
    while (boundary != NULL && boundary->invocation != next.invocation)
        boundary = boundary->previous;
    if (boundary == NULL || !boundary->active || !boundary->used
        || !boundary->executing || boundary->cancelled || result == NULL
        || instance->cancelling != NULL)
        return runtime_fail(instance, error,
                            "invalid function cancellation");
    boundary->cancelled = true;
    boundary->result_valid = true;
    for (uint32_t i = 0; i < 3; ++i) boundary->result[i] = result[i];
    instance->cancelling = boundary;
    return true;
}

static qc_boundary *call_staging(qa_qc_call_next next, uint32_t argument,
                                 uint32_t count, qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_boundary *boundary = instance == NULL ? NULL : instance->boundary;
    if (boundary == NULL || !boundary->active || boundary->used
        || boundary->invocation != next.invocation
        || argument >= boundary->argument_count || argument >= 8u
        || count == 0 || count > 3u) {
        runtime_fail(instance, error,
                     "expired or invalid staged function argument");
        return NULL;
    }
    return boundary;
}

static bool set_call_argument(qa_qc_call_next next, uint32_t argument,
                              const uint32_t *words, uint32_t count,
                              qa_error *error)
{
    qc_boundary *boundary = call_staging(next, argument, count, error);
    if (boundary == NULL || words == NULL) return false;
    uint32_t offset = argument * 3u;
    for (uint32_t i = 0; i < count; ++i) {
        boundary->staging[offset + i] = words[i];
        qc_store_word(next.instance->globals,
                      QC_ARGUMENT_WORD(argument) + i, words[i]);
    }
    return true;
}

bool qa_qc_call_set_arg_int(qa_qc_call_next next, uint32_t argument,
                            int32_t value, qa_error *error)
{
    uint32_t word = (uint32_t)value;
    return set_call_argument(next, argument, &word, 1u, error);
}

bool qa_qc_call_set_arg_float(qa_qc_call_next next, uint32_t argument,
                              float value, qa_error *error)
{
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return set_call_argument(next, argument, &word, 1u, error);
}

bool qa_qc_call_set_arg_vector(qa_qc_call_next next, uint32_t argument,
                               qa_vec3 value, qa_error *error)
{
    uint32_t words[3];
    memcpy(&words[0], &value.x, 4u);
    memcpy(&words[1], &value.y, 4u);
    memcpy(&words[2], &value.z, 4u);
    return set_call_argument(next, argument, words, 3u, error);
}

bool qa_qc_inline_continue(qa_qc_inline_next next, qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_inline_boundary *boundary = instance == NULL
        ? NULL : instance->inline_boundary;
    if (boundary == NULL || !boundary->active || boundary->used
        || boundary->invocation != next.invocation)
        return runtime_fail(instance, error,
                            "expired or repeated inline continuation");
    if (instance->frame_count <= boundary->frame_index
        || instance->frames[boundary->frame_index].function
            != boundary->region.function)
        return runtime_fail(instance, error,
                            "inline continuation belongs to another frame");
    boundary->used = true;
    boundary->completed = qc_machine_inline_continue(instance, boundary, error);
    return boundary->completed;
}

bool qa_qc_inline_skip_to_join(qa_qc_inline_next next, qa_error *error)
{
    qa_qc_instance *instance = next.instance;
    qc_inline_boundary *boundary = instance == NULL
        ? NULL : instance->inline_boundary;
    if (boundary == NULL || !boundary->active || boundary->used
        || boundary->invocation != next.invocation
        || !boundary->region.replaceable)
        return runtime_fail(instance, error,
                            "invalid inline source replacement");
    if (instance->frame_count <= boundary->frame_index
        || instance->frames[boundary->frame_index].function
            != boundary->region.function)
        return runtime_fail(instance, error,
                            "inline replacement belongs to another frame");
    boundary->used = true;
    boundary->completed = true;
    boundary->skip = true;
    instance->frames[boundary->frame_index].statement = boundary->region.exit;
    return true;
}

static const qa_qc_inline_region *admitted_region(
    const qa_qc_instance *instance, const qa_qc_inline_region *requested)
{
    if (requested == NULL) return NULL;
    for (size_t i = 0; i < instance->options.inline_region_count; ++i) {
        const qa_qc_inline_region *region = &instance->inline_regions[i];
        if (region->function == requested->function
            && region->entry == requested->entry
            && region->exit == requested->exit
            && region->replaceable == requested->replaceable
            && region->saved_scope == requested->saved_scope
            && region->saved_word == requested->saved_word) return region;
    }
    return NULL;
}

bool qa_qc_execute_region(qa_qc_instance *instance,
                          const qa_qc_inline_region *requested,
                          uint32_t argument_count, float *result,
                          qa_error *error)
{
    if (instance == NULL || result == NULL || instance->destroying
        || instance->checkpointing)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "invalid standalone QuakeC region request");
    const qa_qc_inline_region *region = admitted_region(instance, requested);
    if (region == NULL
        || region->saved_scope == QA_QC_INLINE_NOT_STANDALONE)
        return runtime_fail(instance, error,
                            "standalone execution requires an admitted region");
    const qa_qc_function *function = qa_qc_program_function(
        instance->program, region->function);
    if (function == NULL || argument_count != function->parameter_count)
        return runtime_fail(instance, error,
                            "invalid standalone inline argument count");
    if (instance->execution_depth >= instance->options.call_limit)
        return runtime_fail(instance, error,
                            "nested QuakeC entry limit exceeded");
    bool outer = instance->execution_depth == 0;
    if (outer) {
        instance->statement_budget = instance->options.statement_limit;
        instance->trace_enabled = false;
    }
    uint32_t entry_depth = instance->frame_count;
    uint32_t saved_arguments = instance->argument_count;
    uint32_t saved_staging[24];
    for (uint32_t i = 0; i < 24u; ++i)
        saved_staging[i] = qc_load_word(instance->globals, 4u + i);
    uint32_t saved_global = 0;
    if (region->saved_scope == QA_QC_INLINE_GLOBAL)
        saved_global = qc_load_word(instance->globals, region->saved_word);
    ++instance->execution_depth;
    bool ok = enter_frame(instance, region->function, argument_count, error);
    if (ok) {
        uint32_t frame_index = instance->frame_count - 1u;
        instance->frames[frame_index].statement = region->entry;
        qc_inline_boundary stop = {
            .region = *region,
            .frame_index = frame_index,
            .active = true,
            .used = true,
            .completed = true
        };
        bool returned;
        ok = run_statements(instance, &stop, &returned, error);
        if (ok) *result = instance->cancelling == NULL
            ? qc_load_float(instance->globals, region->saved_word) : 0.0f;
    }
    while (instance->frame_count > entry_depth) leave_frame(instance);
    if (region->saved_scope == QA_QC_INLINE_GLOBAL)
        qc_store_word(instance->globals, region->saved_word, saved_global);
    for (uint32_t i = 0; i < 24u; ++i)
        qc_store_word(instance->globals, 4u + i, saved_staging[i]);
    instance->argument_count = saved_arguments;
    --instance->execution_depth;
    if (outer && instance->cancelling != NULL) {
        instance->cancelling = NULL;
        return runtime_fail(instance, error,
                            "function cancellation escaped its boundary");
    }
    return ok;
}

bool qc_machine_call(qa_qc_instance *instance, uint32_t function,
                     uint32_t argument_count, qa_error *error)
{
    if (argument_count > 8 || qa_qc_program_function(instance->program, function) == NULL
        || function == 0) return runtime_fail(instance, error, "invalid function call");
    uint32_t caller = instance->frame_count == 0 ? 0
        : instance->frames[instance->frame_count - 1u].function;
    uint32_t statement = instance->frame_count == 0 ? 0
        : instance->frames[instance->frame_count - 1u].statement;
    qa_qc_call_event event = {function, caller, statement, argument_count,
                              instance->frame_count};
    qc_boundary boundary = {.previous = instance->boundary,
                            .invocation = ++instance->next_invocation,
                            .function = function, .caller = caller,
                            .statement = statement,
                            .argument_count = argument_count, .active = true};
    if (boundary.invocation == 0) return runtime_fail(instance, error, "continuation identities exhausted");
    capture_staging(instance, &boundary);
    instance->argument_count = argument_count;
    if (!notify_call(instance, instance->options.observers.entered, &event, error)) {
        instance->argument_count = boundary.staged_argument_count;
        return false;
    }
    if (instance->cancelling != NULL) {
        instance->argument_count = boundary.staged_argument_count;
        return true;
    }
    restore_staging(instance, &boundary);

    bool ok;
    if (instance->options.observers.replace != NULL) {
        instance->boundary = &boundary;
        qa_error failure = {0};
        ++instance->callback_depth;
        ok = instance->options.observers.replace(instance->options.observers.context,
                instance, &event, (qa_qc_call_next){instance, boundary.invocation},
                &failure);
        --instance->callback_depth;
        instance->boundary = boundary.previous;
        boundary.active = false;
        if (!ok) {
            if (failure.code == QA_OK)
                runtime_fail(instance, &failure, "function replacement failed");
            if (error != NULL) *error = failure;
            instance->argument_count = boundary.staged_argument_count;
            return false;
        }
        if (instance->cancelling != NULL) {
            instance->argument_count = boundary.staged_argument_count;
            return true;
        }
        if (!boundary.used || !boundary.result_valid) {
            instance->argument_count = boundary.staged_argument_count;
            return runtime_fail(instance, error, "function replacement did not complete its continuation");
        }
        restore_result(instance, &boundary);
    } else {
        ok = qc_machine_body(instance, function, argument_count, error);
        if (!ok) {
            instance->argument_count = boundary.staged_argument_count;
            return false;
        }
        if (instance->cancelling != NULL) {
            instance->argument_count = boundary.staged_argument_count;
            return true;
        }
        capture_result(instance, &boundary);
    }
    restore_staging(instance, &boundary);
    restore_result(instance, &boundary);
    if (!notify_call(instance, instance->options.observers.left, &event, error)) {
        instance->argument_count = boundary.staged_argument_count;
        return false;
    }
    restore_result(instance, &boundary);
    instance->argument_count = boundary.staged_argument_count;
    return true;
}

bool qa_qc_execute(qa_qc_instance *instance, uint32_t function,
                   uint32_t argument_count, qa_error *error)
{
    if (instance == NULL || instance->destroying || instance->checkpointing)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QuakeC instance");
    if (instance->execution_depth >= instance->options.call_limit)
        return runtime_fail(instance, error, "nested QuakeC entry limit exceeded");
    bool outer = instance->execution_depth == 0;
    if (outer) {
        instance->statement_budget = instance->options.statement_limit;
        instance->trace_enabled = false;
    }
    ++instance->execution_depth;
    bool ok = qc_machine_call(instance, function, argument_count, error);
    --instance->execution_depth;
    if (outer && instance->cancelling != NULL) {
        instance->cancelling = NULL;
        return runtime_fail(instance, error,
                            "function cancellation escaped its boundary");
    }
    return ok;
}

bool qa_qc_execute_named(qa_qc_instance *instance, const char *function,
                         uint32_t argument_count, qa_error *error)
{
    if (instance == NULL || function == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "function name is required");
    uint32_t index;
    if (qa_qc_program_find_function(instance->program, function, &index) == NULL)
        return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "QuakeC function was not found");
    return qa_qc_execute(instance, index, argument_count, error);
}
