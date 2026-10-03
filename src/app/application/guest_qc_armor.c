#include "guest_qc_armor.h"

static bool reject(qa_error *error, const char *message)
{ return application_fail(error, QA_ERROR_FORMAT, message); }
static bool word(const qa_json_document *doc, qa_json_id node, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, node, &value, error) || value > UINT32_MAX)
        return reject(error, "QC armor declaration word exceeds its actual source extent");
    *out = (uint32_t)value; return true;
}
static uint32_t end_of(const qa_qc_program *program, const qa_qc_function *function)
{
    qa_qc_program_info info = qa_qc_program_describe(program);
    uint32_t end = info.statement_count;
    for (uint32_t i = 0; i < info.function_count; ++i) {
        const qa_qc_function *other = qa_qc_program_function(program, i);
        if (other->first_statement > function->first_statement && (uint32_t)other->first_statement < end)
            end = (uint32_t)other->first_statement;
    }
    return end;
}
static bool typed_local(const qa_qc_program *program, const qa_qc_function *function,
    uint32_t offset, qa_qc_value_type type)
{
    if (offset < function->parameter_start || offset - function->parameter_start >= function->local_words)
        return false;
    qa_qc_program_info info = qa_qc_program_describe(program);
    for (uint32_t i = 0; i < info.global_count; ++i) {
        const qa_qc_definition *definition = qa_qc_program_global(program, i);
        if (definition->offset == offset && definition->type == type) return true;
    }
    return false;
}
typedef struct armor_analysis {
    const qa_qc_program *program;
    qa_qc_program_info info;
    uint8_t *written, *named, *visiting;
} armor_analysis;
static bool called(qa_qc_opcode opcode)
{ return opcode >= QA_QC_CALL0 && opcode <= QA_QC_CALL8; }
static const qa_qc_function *callee(const armor_analysis *analysis,
    const qa_qc_statement *statement, uint32_t *index, qa_error *error)
{
    int32_t authored;
    if (analysis->written[statement->a] ||
        !qa_qc_program_initial_int(analysis->program, statement->a, &authored, error) || authored <= 0)
        return NULL;
    const qa_qc_function *function = qa_qc_program_function(analysis->program, (uint32_t)authored);
    const qa_qc_definition *global = function ?
        qa_qc_program_find_global(analysis->program, function->name) : NULL;
    if (!function || !global || global->type != QA_QC_FUNCTION || global->offset != statement->a)
        return NULL;
    if (index) *index = (uint32_t)authored;
    return function;
}
static bool access_at(const armor_analysis *analysis, uint32_t index,
    qa_qc_statement_access *access, qa_error *error)
{
    if (!qa_qc_program_statement_access(analysis->program, index, access, error)) return false;
    const qa_qc_statement *statement = qa_qc_program_statement(analysis->program, index);
    if (called(statement->opcode)) {
        const qa_qc_function *function = callee(analysis, statement, NULL, error);
        if (function && function->parameter_count == (uint32_t)(statement->opcode - QA_QC_CALL0)) {
            access->read_count = 1;
            for (uint32_t i = 0; i < function->parameter_count; ++i)
                for (uint32_t j = 0; j < function->parameter_sizes[i]; ++j)
                    access->read[access->read_count++] = 4 + 3 * i + j;
        }
    }
    return true;
}
static bool analysis_create(const qa_qc_program *program, armor_analysis *analysis, qa_error *error)
{
    *analysis = (armor_analysis){.program = program, .info = qa_qc_program_describe(program)};
    analysis->written = calloc(analysis->info.global_words, 1);
    analysis->named = calloc(analysis->info.global_words, 1);
    analysis->visiting = calloc(analysis->info.function_count, 1);
    if (!analysis->written || !analysis->named || !analysis->visiting)
        return application_fail(error, QA_ERROR_MEMORY, "Owning compiled QC armor qualification");
    for (uint32_t i = 0; i < analysis->info.statement_count; ++i) {
        qa_qc_statement_access access;
        if (!qa_qc_program_statement_access(program, i, &access, error)) return false;
        for (uint32_t j = 0; j < access.write_count; ++j) analysis->written[access.write[j]] = 1;
    }
    for (uint32_t i = 0; i < analysis->info.global_count; ++i) {
        const qa_qc_definition *global = qa_qc_program_global(program, i);
        uint32_t width = global->type == QA_QC_VECTOR ? 3u : 1u;
        if (*global->name) for (uint32_t j = 0; j < width; ++j) analysis->named[global->offset + j] = 1;
    }
    return true;
}
static bool bit(const uint8_t *set, uint32_t index)
{ return (set[index / 8] & (uint8_t)(1u << (index % 8))) != 0; }
static void mark(uint8_t *set, uint32_t index, bool value)
{
    uint8_t flag = (uint8_t)(1u << (index % 8));
    if (value) set[index / 8] |= flag;
    else set[index / 8] &= (uint8_t)~flag;
}
static bool collect_writes(armor_analysis *analysis, uint32_t start, uint32_t end,
    uint32_t local_start, uint32_t local_words, uint8_t *writes, qa_error *error)
{
    for (uint32_t i = start; i < end; ++i) {
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, error)) return false;
        for (uint32_t j = 0; j < access.write_count; ++j)
            if (access.write[j] < local_start || access.write[j] - local_start >= local_words)
                mark(writes, access.write[j], true);
        const qa_qc_statement *statement = qa_qc_program_statement(analysis->program, i);
        if (!called(statement->opcode)) continue;
        uint32_t index;
        const qa_qc_function *function = callee(analysis, statement, &index, error);
        if (!function) return reject(error, "QC armor region calls a mutable or unresolved source function");
        if (function->named_builtin || function->first_statement <= 0) {
            if (!qa_qc_program_function_returns_only(analysis->program, index))
                return reject(error, "QC armor builtin has undeclared source side effects");
            continue;
        }
        if (analysis->visiting[index]) return reject(error, "QC armor region contains a recursive source call");
        analysis->visiting[index] = 1;
        bool ok = collect_writes(analysis, (uint32_t)function->first_statement,
            end_of(analysis->program, function), function->parameter_start, function->local_words, writes, error);
        analysis->visiting[index] = 0;
        if (!ok) return false;
    }
    return true;
}
typedef struct flow_node {
    uint8_t *dirty;
    uint32_t limit, local_start, local_words;
    bool queued;
} flow_node;
typedef struct armor_flow {
    const armor_analysis *analysis;
    flow_node *nodes;
    size_t *queue, width, capacity, head, tail, count;
    uint32_t output;
    bool standalone;
} armor_flow;
static bool enqueue(armor_flow *flow, int64_t statement, uint32_t limit, bool in_callee,
    uint32_t local_start, uint32_t local_words, const uint8_t *dirty, qa_error *error)
{
    if (!in_callee && flow->standalone && statement == limit)
        return !bit(dirty, flow->output) || reject(error, "QC armor result depends on an unstaged private word");
    bool any = false;
    for (size_t i = 0; i < flow->width; ++i) any = any || dirty[i] != 0;
    if (!any) return true;
    if (statement < 0 || (uint64_t)statement >= limit)
        return reject(error, "QC armor private temporary escapes its compiled source continuation");
    size_t key = (size_t)statement * 2 + (in_callee ? 1u : 0u);
    flow_node *node = flow->nodes + key;
    bool changed = false;
    if (!node->dirty) {
        node->dirty = calloc(flow->width, 1);
        if (!node->dirty) return application_fail(error, QA_ERROR_MEMORY, "Owning QC armor dataflow state");
        node->limit = limit; node->local_start = local_start; node->local_words = local_words;
        changed = true;
    } else if (node->limit != limit || node->local_start != local_start || node->local_words != local_words)
        return reject(error, "QC armor has ambiguous overlapping source function frames");
    for (size_t i = 0; i < flow->width; ++i) {
        uint8_t joined = node->dirty[i] | dirty[i];
        changed = changed || joined != node->dirty[i]; node->dirty[i] = joined;
    }
    if (changed && !node->queued) {
        flow->queue[flow->tail] = key; flow->tail = (flow->tail + 1) % flow->capacity;
        ++flow->count; node->queued = true;
    }
    return true;
}
static bool flow_safe(const armor_analysis *analysis, uint32_t start, uint32_t limit,
    const uint8_t *dirty, bool standalone, uint32_t output, qa_error *error)
{
    armor_flow flow = {.analysis = analysis, .width = ((size_t)analysis->info.global_words + 7) / 8,
        .capacity = (size_t)analysis->info.statement_count * 2, .output = output, .standalone = standalone};
    if (!flow.capacity || flow.capacity > SIZE_MAX / sizeof(*flow.nodes) ||
        flow.capacity > SIZE_MAX / sizeof(*flow.queue))
        return application_fail(error, QA_ERROR_MEMORY, "QC armor dataflow extent exceeds host storage");
    flow.nodes = calloc(flow.capacity, sizeof(*flow.nodes));
    flow.queue = malloc(flow.capacity * sizeof(*flow.queue));
    uint8_t *current = malloc(flow.width), *next = malloc(flow.width);
    bool ok = flow.nodes && flow.queue && current && next;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Owning QC armor continuation analysis");
    if (ok) ok = enqueue(&flow, start, limit, false, 0, 0, dirty, error);
    while (ok && flow.count) {
        size_t key = flow.queue[flow.head]; flow.head = (flow.head + 1) % flow.capacity; --flow.count;
        flow_node *node = flow.nodes + key; node->queued = false;
        uint32_t index = (uint32_t)(key / 2); bool in_callee = (key % 2) != 0;
        memcpy(current, node->dirty, flow.width); memcpy(next, current, flow.width);
        qa_qc_statement_access access;
        if (!(ok = access_at(analysis, index, &access, error))) break;
        const qa_qc_statement *statement = qa_qc_program_statement(analysis->program, index);
        bool tainted = false, external_write = false;
        for (uint32_t i = 0; i < access.read_count; ++i) tainted = tainted || bit(current, access.read[i]);
        for (uint32_t i = 0; i < access.write_count; ++i)
            external_write = external_write || (access.write[i] >= 28 &&
                (access.write[i] < node->local_start || access.write[i] - node->local_start >= node->local_words));
        if (tainted && ((in_callee && external_write) || !access.write_count ||
            called(statement->opcode) || statement->opcode == QA_QC_RETURN || statement->opcode == QA_QC_DONE)) {
            ok = reject(error, "QC armor publishes or branches on an unstaged private temporary"); break;
        }
        if (called(statement->opcode)) {
            const qa_qc_function *function = callee(analysis, statement, NULL, error);
            bool private_dirty = false;
            for (uint32_t i = 28; i < analysis->info.global_words; ++i)
                private_dirty = private_dirty || bit(current, i);
            for (uint32_t candidate = 1; private_dirty && ok && candidate < analysis->info.function_count; ++candidate) {
                const qa_qc_function *entered = qa_qc_program_function(analysis->program, candidate);
                if ((function && entered != function) || entered->named_builtin || entered->first_statement <= 0) continue;
                uint32_t parameters = 0;
                for (uint32_t i = 0; i < entered->parameter_count; ++i) parameters += entered->parameter_sizes[i];
                for (uint32_t i = 0; i < parameters; ++i) mark(next, entered->parameter_start + i, false);
                ok = enqueue(&flow, entered->first_statement, end_of(analysis->program, entered), true,
                    entered->parameter_start, entered->local_words, next, error);
                memcpy(next, current, flow.width);
            }
            if (!ok) break;
        }
        for (uint32_t i = 0; i < access.write_count; ++i) mark(next, access.write[i], false);
        if (statement->opcode >= QA_QC_STORE_F && statement->opcode <= QA_QC_STORE_FN) {
            for (uint32_t i = 0; i < access.write_count; ++i)
                mark(next, access.write[i], bit(current, access.read[i]));
        } else if (tainted) for (uint32_t i = 0; i < access.write_count; ++i) mark(next, access.write[i], true);
        if (statement->opcode == QA_QC_RETURN || statement->opcode == QA_QC_DONE) continue;
        int64_t successor = (int64_t)index + 1;
        if (statement->opcode == QA_QC_GOTO) successor = (int64_t)index + (int16_t)statement->a;
        ok = enqueue(&flow, successor, node->limit, in_callee, node->local_start, node->local_words, next, error);
        if (ok && (statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT))
            ok = enqueue(&flow, (int64_t)index + (int16_t)statement->b, node->limit,
                in_callee, node->local_start, node->local_words, next, error);
    }
    if (flow.nodes) for (size_t i = 0; i < flow.capacity; ++i) free(flow.nodes[i].dirty);
    free(flow.nodes); free(flow.queue); free(current); free(next); return ok;
}
void application_qc_armor_free(application_qc_armor_stage *stage)
{
    if (!stage) return;
    free(stage->scales); memset(stage, 0, sizeof(*stage));
}
static bool scales_parse(const qa_qc_program *program, const qa_json_document *doc,
    qa_json_id node, application_qc_armor_stage *stage, qa_error *error)
{
    if (!application_qc_declaration_array(doc, node, true, error)) return false;
    stage->scale_count = qa_json_size(doc, node);
    if (stage->scale_count > SIZE_MAX / sizeof(*stage->scales))
        return application_fail(error, QA_ERROR_MEMORY, "QC armor scale declaration is too large");
    stage->scales = stage->scale_count ? calloc(stage->scale_count, sizeof(*stage->scales)) : NULL;
    if (stage->scale_count && !stage->scales)
        return application_fail(error, QA_ERROR_MEMORY, "Owning QC armor source call sites");
    const qa_qc_function *damage = qa_qc_program_function(program, stage->region.function);
    const qa_qc_definition *global = qa_qc_program_find_global(program, damage->name);
    for (size_t i = 0; i < stage->scale_count; ++i) {
        qa_json_id row = qa_json_at(doc, node, i);
        application_qc_armor_scale *site = stage->scales + i;
        char *name = application_qc_declaration_string(doc, qa_json_get(doc, row, "caller"), error);
        const qa_qc_function *caller = name ? qa_qc_program_find_function(program, name, &site->caller) : NULL;
        free(name);
        if (!caller || caller->first_statement <= 0 || caller->named_builtin ||
            !word(doc, qa_json_get(doc, row, "statement"), &site->statement, error) ||
            !application_qc_declaration_number(doc, qa_json_get(doc, row, "scale"), &site->scale, error) ||
            site->scale < 0 || site->statement < (uint32_t)caller->first_statement ||
            site->statement >= end_of(program, caller) || !global || global->type != QA_QC_FUNCTION)
            return reject(error, "QC armor scale lacks its actual interpreted call site");
        const qa_qc_statement *statement = qa_qc_program_statement(program, site->statement);
        int32_t function;
        if (statement->opcode != QA_QC_CALL4 || statement->a != global->offset ||
            !qa_qc_program_initial_int(program, statement->a, &function, error) ||
            function <= 0 || (uint32_t)function != stage->region.function)
            return reject(error, "QC armor scale call differs from its actual source function");
        for (size_t j = 0; j < i; ++j)
            if (stage->scales[j].statement == site->statement)
                return reject(error, "QC armor scale duplicates a compiled call site");
    }
    return true;
}
static bool input_at(const qa_qc_function *function, const application_qc_call *call,
    uint32_t word_offset, application_qc_input_id input)
{
    uint32_t offset = function->parameter_start;
    for (size_t i = 0; i < call->argument_count; ++i) {
        if (word_offset == offset) return call->arguments[i].kind == QC_VALUE_INPUT &&
            call->arguments[i].source == input;
        offset += function->parameter_sizes[i];
    }
    return false;
}
bool application_qc_armor_inputs(const qa_qc_program *program, const application_qc_call *call,
    const application_qc_armor_stage *stage, bool standalone, qa_error *error)
{
    const qa_qc_function *function = qa_qc_program_function(program, stage->region.function);
    return (function && call->function == stage->region.function &&
        input_at(function, call, stage->target, QC_INPUT_SELF) &&
        input_at(function, call, stage->damage, QC_INPUT_AMOUNT) &&
        (!standalone || !stage->flag_bits || input_at(function, call, stage->flags, QC_INPUT_DAMAGE_FLAGS))) ||
        reject(error, "QC standalone armor must stage its actual target, damage and flags parameters");
}
static bool control_safe(const armor_analysis *analysis, const application_qc_armor_stage *stage,
    qa_error *error)
{
    for (uint32_t i = stage->region.entry; i < stage->region.exit; ++i) {
        const qa_qc_statement *statement = qa_qc_program_statement(analysis->program, i);
        if (statement->opcode == QA_QC_RETURN || statement->opcode == QA_QC_DONE || statement->opcode == QA_QC_STATE)
            return reject(error, "QC armor region exits its function or changes actor state");
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, error)) return false;
        for (uint32_t j = 0; j < access.write_count; ++j)
            if (access.write[j] == stage->target || access.write[j] == stage->damage)
                return reject(error, "QC armor region changes its captured target or incoming damage");
        int64_t target = (int64_t)i + 1;
        if (statement->opcode == QA_QC_GOTO) target = (int64_t)i + (int16_t)statement->a;
        if (target <= i || target > stage->region.exit)
            return reject(error, "QC armor control flow loops or leaves its declared join");
        if (statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT) {
            target = (int64_t)i + (int16_t)statement->b;
            if (target <= i || target > stage->region.exit)
                return reject(error, "QC armor branch loops or leaves its declared join");
        }
    }
    return true;
}
static bool replacement_safe(armor_analysis *analysis, const application_qc_armor_stage *stage,
    const qa_qc_function *function, uint8_t *dirty, qa_error *error)
{
    analysis->visiting[stage->region.function] = 1;
    bool ok = collect_writes(analysis, stage->region.entry, stage->region.exit, 0, 0, dirty, error);
    analysis->visiting[stage->region.function] = 0;
    if (!ok) return false;
    mark(dirty, stage->region.saved_word, false);
    for (uint32_t i = 28; i < analysis->info.global_words; ++i)
        if (bit(dirty, i) && analysis->named[i] &&
            (i < function->parameter_start || i - function->parameter_start >= function->local_words))
            return reject(error, "QC armor replacement changes a live declared global outside its frame");
    return flow_safe(analysis, stage->region.exit, end_of(analysis->program, function), dirty, false, 0, error);
}
static bool standalone_safe(const armor_analysis *analysis, const application_qc_armor_stage *stage,
    const qa_qc_function *function, uint8_t *dirty, size_t width, qa_error *error)
{
    memset(dirty, 0, width);
    uint32_t parameters = 0;
    for (uint32_t i = 0; i < function->parameter_count; ++i) parameters += function->parameter_sizes[i];
    for (uint32_t i = parameters; i < function->local_words; ++i)
        mark(dirty, function->parameter_start + i, true);
    for (uint32_t i = 1; i < 28; ++i) mark(dirty, i, true);
    for (uint32_t i = 0; i < function->parameter_count; ++i)
        for (uint32_t j = 0; j < function->parameter_sizes[i]; ++j) mark(dirty, 4 + 3 * i + j, false);
    for (uint32_t i = stage->region.entry; i < stage->region.exit; ++i) {
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, error)) return false;
        for (uint32_t j = 0; j < access.read_count; ++j)
            if (access.read[j] >= 28 && !analysis->named[access.read[j]]) mark(dirty, access.read[j], true);
    }
    for (uint32_t i = 0; i < parameters; ++i) mark(dirty, function->parameter_start + i, false);
    mark(dirty, stage->region.saved_word, true);
    return flow_safe(analysis, stage->region.entry, stage->region.exit, dirty, true, stage->region.saved_word, error);
}
bool application_qc_armor_parse(const qa_qc_program *program, const qa_json_document *doc,
    qa_json_id node, application_qc_armor_stage *stage, qa_error *error)
{
    if (!program || !stage) return reject(error, "QC armor qualification requires its actual compiled program");
    *stage = (application_qc_armor_stage){0};
    char *name = application_qc_declaration_string(doc, qa_json_get(doc, node, "function"), error);
    const qa_qc_function *function = name ? qa_qc_program_find_function(program, name, &stage->region.function) : NULL;
    free(name);
    if (!function || function->first_statement <= 0 || function->named_builtin ||
        !word(doc, qa_json_get(doc, node, "entry"), &stage->region.entry, error) ||
        !word(doc, qa_json_get(doc, node, "exit"), &stage->region.exit, error) ||
        !word(doc, qa_json_get(doc, node, "target"), &stage->target, error) ||
        !word(doc, qa_json_get(doc, node, "damage"), &stage->damage, error) ||
        !word(doc, qa_json_get(doc, node, "saved"), &stage->region.saved_word, error) ||
        stage->region.entry < (uint32_t)function->first_statement || stage->region.exit <= stage->region.entry ||
        stage->region.exit >= end_of(program, function) ||
        !typed_local(program, function, stage->target, QA_QC_ENTITY) ||
        !typed_local(program, function, stage->damage, QA_QC_FLOAT) ||
        !typed_local(program, function, stage->region.saved_word, QA_QC_FLOAT) ||
        stage->target == stage->damage || stage->target == stage->region.saved_word || stage->damage == stage->region.saved_word)
        return reject(error, "QC armor region differs from its actual typed source frame");
    qa_json_id flags = qa_json_get(doc, node, "flags"), kind = qa_json_get(doc, flags, "kind");
    if (qa_json_string_equal(doc, kind, "bits")) {
        stage->flag_bits = true;
        if (!word(doc, qa_json_get(doc, flags, "word"), &stage->flags, error) ||
            !typed_local(program, function, stage->flags, QA_QC_FLOAT) || stage->flags == stage->target ||
            stage->flags == stage->damage || stage->flags == stage->region.saved_word ||
            !word(doc, qa_json_get(doc, flags, "noArmor"), &stage->no_armor, error) ||
            !word(doc, qa_json_get(doc, flags, "noPowerArmor"), &stage->no_power, error) ||
            !word(doc, qa_json_get(doc, flags, "noRegularArmor"), &stage->no_regular, error) ||
            !word(doc, qa_json_get(doc, flags, "energy"), &stage->energy, error) ||
            stage->no_armor > UINT32_C(0x7fffff) || stage->no_power > UINT32_C(0x7fffff) ||
            stage->no_regular > UINT32_C(0x7fffff) || stage->energy > UINT32_C(0x7fffff))
            return reject(error, "QC armor flags differ from their actual exact source frame");
    } else if (!qa_json_string_equal(doc, kind, "none")) return reject(error, "QC armor source flag declaration is invalid");
    if (!application_qc_statements_validate(doc, qa_json_get(doc, node, "statements"), program,
        stage->region.entry, stage->region.exit, error) ||
        !scales_parse(program, doc, qa_json_get(doc, node, "regularScale"), stage, error)) return false;
    const qa_qc_statement *entry = qa_qc_program_statement(program, stage->region.entry);
    const qa_qc_statement *join = qa_qc_program_statement(program, stage->region.exit);
    const qa_qc_definition *armor = qa_qc_program_find_global(program, "armortype");
    if (entry->a != stage->target || (!(entry->opcode == QA_QC_LOAD_F && armor && entry->b == armor->offset) &&
        !((entry->opcode == QA_QC_STORE_F || entry->opcode == QA_QC_STORE_ENT) && entry->b == 4)))
        return reject(error, "QC armor entry must precede its actual source armor reads or call staging");
    if (join->opcode != QA_QC_SUB_F || join->a != stage->damage || join->b != stage->region.saved_word)
        return reject(error, "QC armor join must subtract actual savings from incoming damage");
    armor_analysis analysis = {0};
    bool ok = analysis_create(program, &analysis, error);
    if (ok) ok = control_safe(&analysis, stage, error);
    size_t width = ((size_t)analysis.info.global_words + 7) / 8;
    uint8_t *dirty = ok ? calloc(width, 1) : NULL;
    if (ok && !dirty) ok = application_fail(error, QA_ERROR_MEMORY, "Owning QC armor private word proof");
    qa_error proof = {0};
    if (ok) {
        stage->region.replaceable = replacement_safe(&analysis, stage, function, dirty, &proof);
        if (!stage->region.replaceable && proof.code == QA_ERROR_MEMORY) { if (error) *error = proof; ok = false; }
    }
    if (ok && stage->region.replaceable) {
        proof = (qa_error){0};
        if (standalone_safe(&analysis, stage, function, dirty, width, &proof)) stage->region.saved_scope = QA_QC_INLINE_FRAME;
        else if (proof.code == QA_ERROR_MEMORY) { if (error) *error = proof; ok = false; }
    }
    free(dirty); free(analysis.written); free(analysis.named); free(analysis.visiting); return ok;
}
