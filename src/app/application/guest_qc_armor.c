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
static bool typed_word(const qa_qc_program *program, uint32_t offset, qa_qc_value_type type)
{
    qa_qc_program_info info = qa_qc_program_describe(program);
    for (uint32_t i = 0; i < info.global_count; ++i) {
        const qa_qc_definition *definition = qa_qc_program_global(program, i);
        if (definition->offset == offset && definition->type == type) return true;
    }
    return false;
}
static bool typed_local(const qa_qc_program *program, const qa_qc_function *function,
    uint32_t offset, qa_qc_value_type type)
{
    return offset >= function->parameter_start && offset - function->parameter_start < function->local_words &&
        typed_word(program, offset, type);
}
typedef struct armor_analysis {
    const qa_qc_program *program;
    qa_qc_program_info info;
    uint8_t *written, *named, *visiting;
    const application_qc_armor_stage *stage;
    bool indirect_entity_writes, indirect_input_writes;
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
static void analysis_free(armor_analysis *analysis)
{ free(analysis->written); free(analysis->named); free(analysis->visiting); }
static bool bit(const uint8_t *set, uint32_t index)
{ return (set[index / 8] & (uint8_t)(1u << (index % 8))) != 0; }
static void mark(uint8_t *set, uint32_t index, bool value)
{
    uint8_t flag = (uint8_t)(1u << (index % 8));
    if (value) set[index / 8] |= flag;
    else set[index / 8] &= (uint8_t)~flag;
}
static bool collect_writes(armor_analysis *analysis, uint32_t start, uint32_t end,
    uint32_t local_start, uint32_t local_words, bool in_callee, uint8_t *writes, qa_error *error)
{
    for (uint32_t i = start; i < end; ++i) {
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, error)) return false;
        for (uint32_t j = 0; j < access.write_count; ++j)
            if (access.write[j] < local_start || access.write[j] - local_start >= local_words) {
                mark(writes, access.write[j], true);
                if (in_callee && (access.write[j] == analysis->stage->target || access.write[j] == analysis->stage->damage ||
                    access.write[j] == analysis->stage->region.saved_word)) analysis->indirect_input_writes = true;
            }
        const qa_qc_statement *statement = qa_qc_program_statement(analysis->program, i);
        if (in_callee && ((statement->opcode >= QA_QC_STOREP_F && statement->opcode <= QA_QC_STOREP_FN) ||
            statement->opcode == QA_QC_STATE)) analysis->indirect_entity_writes = true;
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
            qa_qc_program_function_end(analysis->program, function), function->parameter_start, function->local_words, true, writes, error);
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
                ok = enqueue(&flow, entered->first_statement, qa_qc_program_function_end(analysis->program, entered), true,
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
            site->statement >= qa_qc_program_function_end(program, caller) || !global || global->type != QA_QC_FUNCTION)
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
    analysis->stage = stage; analysis->indirect_entity_writes = false; analysis->indirect_input_writes = false;
    analysis->visiting[stage->region.function] = 1;
    bool ok = collect_writes(analysis, stage->region.entry, stage->region.exit, 0, 0, false, dirty, error);
    analysis->visiting[stage->region.function] = 0;
    if (!ok) return false;
    mark(dirty, stage->region.saved_word, false);
    for (uint32_t i = 28; i < analysis->info.global_words; ++i)
        if (bit(dirty, i) && analysis->named[i] &&
            (i < function->parameter_start || i - function->parameter_start >= function->local_words))
            return reject(error, "QC armor replacement changes a live declared global outside its frame");
    return flow_safe(analysis, stage->region.exit, qa_qc_program_function_end(analysis->program, function), dirty, false, 0, error);
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
static bool armor_entry(const qa_qc_program *program, const application_qc_armor_stage *stage)
{
    const qa_qc_statement *entry = qa_qc_program_statement(program, stage->region.entry);
    const qa_qc_definition *armor = qa_qc_program_find_global(program, "armortype");
    return entry && entry->a == stage->target &&
        ((entry->opcode == QA_QC_LOAD_F && armor && entry->b == armor->offset) ||
        ((entry->opcode == QA_QC_STORE_F || entry->opcode == QA_QC_STORE_ENT ||
          entry->opcode == QA_QC_STORE_V) && entry->b == 4));
}
static const qa_qc_function *armor_frame(const qa_qc_program *program,
    const application_qc_armor_stage *stage)
{
    const qa_qc_function *function = qa_qc_program_function(program, stage->region.function);
    if (!function || function->first_statement <= 0 || function->named_builtin ||
        stage->region.entry < (uint32_t)function->first_statement || stage->region.exit <= stage->region.entry ||
        stage->region.exit >= qa_qc_program_function_end(program, function) ||
        !typed_local(program, function, stage->target, QA_QC_ENTITY) ||
        !typed_local(program, function, stage->damage, QA_QC_FLOAT) ||
        !typed_local(program, function, stage->region.saved_word, QA_QC_FLOAT) ||
        stage->target == stage->damage || stage->target == stage->region.saved_word || stage->damage == stage->region.saved_word)
        return NULL;
    return function;
}
static bool qualify(armor_analysis *analysis, application_qc_armor_stage *stage, qa_error *error)
{
    const qa_qc_program *program = analysis->program;
    const qa_qc_function *function = armor_frame(program, stage);
    if (!function) return reject(error, "QC armor region differs from its actual typed source frame");
    stage->region.replaceable = false; stage->region.saved_scope = QA_QC_INLINE_NOT_STANDALONE;
    if (!armor_entry(program, stage))
        return reject(error, "QC armor entry must precede its actual source armor reads or call staging");
    const qa_qc_statement *join = qa_qc_program_statement(program, stage->region.exit);
    if (join->opcode != QA_QC_SUB_F || join->a != stage->damage || join->b != stage->region.saved_word)
        return reject(error, "QC armor join must subtract actual savings from incoming damage");
    bool ok = control_safe(analysis, stage, error);
    size_t width = ((size_t)analysis->info.global_words + 7) / 8;
    uint8_t *dirty = ok ? calloc(width, 1) : NULL;
    if (ok && !dirty) ok = application_fail(error, QA_ERROR_MEMORY, "Owning QC armor private word proof");
    qa_error proof = {0};
    if (ok) {
        stage->region.replaceable = replacement_safe(analysis, stage, function, dirty, &proof);
        if (!stage->region.replaceable && proof.code == QA_ERROR_MEMORY) { if (error) *error = proof; ok = false; }
    }
    if (ok && stage->region.replaceable) {
        proof = (qa_error){0};
        if (standalone_safe(analysis, stage, function, dirty, width, &proof)) stage->region.saved_scope = QA_QC_INLINE_FRAME;
        else if (proof.code == QA_ERROR_MEMORY) { if (error) *error = proof; ok = false; }
    }
    free(dirty); return ok;
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
        !word(doc, qa_json_get(doc, node, "saved"), &stage->region.saved_word, error) || !armor_frame(program, stage))
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
    armor_analysis analysis = {0};
    bool ok = analysis_create(program, &analysis, error);
    if (ok) ok = qualify(&analysis, stage, error);
    analysis_free(&analysis); return ok;
}
static bool field_word(const armor_analysis *analysis, const char *name, uint32_t *word_offset)
{
    const qa_qc_definition *global = qa_qc_program_find_global(analysis->program, name);
    const qa_qc_definition *field = qa_qc_program_find_field(analysis->program, name);
    int32_t offset;
    if (!global || global->type != QA_QC_FIELD || !field || field->type != QA_QC_FLOAT ||
        analysis->written[global->offset] ||
        !qa_qc_program_initial_int(analysis->program, global->offset, &offset, NULL) ||
        offset < 0 || (uint32_t)offset != field->offset) return false;
    *word_offset = global->offset; return true;
}
static bool closed_entry(const qa_qc_program *program, const qa_qc_function *function,
    const application_qc_armor_stage *stage)
{
    uint32_t end = qa_qc_program_function_end(program, function);
    for (uint32_t i = (uint32_t)function->first_statement; i < end; ++i) {
        if (i >= stage->region.entry && i < stage->region.exit) continue;
        const qa_qc_statement *statement = qa_qc_program_statement(program, i);
        int64_t target;
        if (statement->opcode == QA_QC_GOTO) target = (int64_t)i + (int16_t)statement->a;
        else if (statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT)
            target = (int64_t)i + (int16_t)statement->b;
        else continue;
        if (target > stage->region.entry && target < stage->region.exit) return false;
    }
    return true;
}
static bool constant_float(const armor_analysis *analysis, uint32_t word_offset, float *value)
{
    int32_t bits;
    if (analysis->written[word_offset] || !typed_word(analysis->program, word_offset, QA_QC_FLOAT) ||
        !qa_qc_program_initial_int(analysis->program, word_offset, &bits, NULL)) return false;
    memcpy(value, &bits, sizeof(*value)); return isfinite(*value);
}
static bool constant_bits(const armor_analysis *analysis, uint32_t word_offset, uint32_t *bits)
{
    float value;
    if (!constant_float(analysis, word_offset, &value) || value < 0 ||
        (double)value > UINT32_MAX || value != floorf(value)) return false;
    *bits = (uint32_t)value; return true;
}
static uint32_t armor_item_mask(const armor_analysis *analysis, bool second)
{
    static const char *names[2][3] = {{"IT_ARMOR1", "IT_ARMOR2", "IT_ARMOR3"},
        {"IT2_ARMOR1", "IT2_ARMOR2", "IT2_ARMOR3"}};
    uint32_t mask = 0;
    for (size_t i = 0; i < 3; ++i) {
        const qa_qc_definition *definition = qa_qc_program_find_global(analysis->program, names[second ? 1 : 0][i]);
        uint32_t bits;
        if (!definition || !constant_bits(analysis, definition->offset, &bits) || !bits || (mask & bits)) return 0;
        mask |= bits;
    }
    return mask;
}
typedef enum armor_value_kind {
    ARMOR_UNKNOWN, ARMOR_TYPE, ARMOR_VALUE, ARMOR_MULTIPLIED, ARMOR_PRODUCT,
    ARMOR_ZERO, ARMOR_SPENT, ARMOR_VALUE_ADDRESS, ARMOR_TYPE_ADDRESS,
    ARMOR_ITEMS, ARMOR_ITEMS2, ARMOR_ITEMS_ADDRESS, ARMOR_ITEMS2_ADDRESS,
    ARMOR_MASK, ARMOR_REMOVED, ARMOR_REMOVED2, ARMOR_CLEARED, ARMOR_CLEARED2
} armor_value_kind;
typedef struct armor_value { armor_value_kind kind; uint32_t mask; } armor_value;
static armor_value copied_value(const armor_analysis *analysis, const armor_value *words, uint32_t offset)
{
    armor_value value = words[offset]; float constant;
    if (value.kind == ARMOR_UNKNOWN && constant_float(analysis, offset, &constant) && constant == 0)
        value.kind = ARMOR_ZERO;
    return value;
}
static bool mask_value(const armor_analysis *analysis, const armor_value *words,
    uint32_t offset, uint32_t *mask)
{
    if (words[offset].kind != ARMOR_MASK) return constant_bits(analysis, offset, mask);
    *mask = words[offset].mask; return true;
}
static bool armor_dataflow(const armor_analysis *analysis, const application_qc_armor_stage *stage,
    uint32_t armor, uint32_t value, uint32_t items, uint32_t items2, uint32_t item_mask,
    uint32_t item_mask2, armor_value *words, bool *field_writes_safe, qa_error *error)
{
    memset(words, 0, (size_t)analysis->info.global_words * sizeof(*words));
    bool savings = false, spent = false; *field_writes_safe = true;
    bool type_written = false, items_written = false, items2_written = false;
    for (uint32_t i = stage->region.entry; i < stage->region.exit; ++i) {
        const qa_qc_statement *s = qa_qc_program_statement(analysis->program, i);
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, error)) return false;
        armor_value result = {0}, copied[3] = {{0}};
        if (s->opcode == QA_QC_LOAD_F && s->a == stage->target)
            result.kind = s->b == armor ? ARMOR_TYPE : s->b == value ? ARMOR_VALUE :
                s->b == items ? ARMOR_ITEMS : s->b == items2 ? ARMOR_ITEMS2 : ARMOR_UNKNOWN;
        else if (s->opcode == QA_QC_ADDRESS && s->a == stage->target)
            result.kind = s->b == value ? ARMOR_VALUE_ADDRESS : s->b == armor ? ARMOR_TYPE_ADDRESS :
                s->b == items ? ARMOR_ITEMS_ADDRESS : s->b == items2 ? ARMOR_ITEMS2_ADDRESS : ARMOR_UNKNOWN;
        else if (s->opcode == QA_QC_MUL_F &&
            ((words[s->a].kind == ARMOR_TYPE && s->b == stage->damage) ||
             (words[s->b].kind == ARMOR_TYPE && s->a == stage->damage))) result.kind = ARMOR_MULTIPLIED;
        else if (s->opcode == QA_QC_SUB_F) {
            if (words[s->a].kind == ARMOR_VALUE && s->b == stage->region.saved_word) result.kind = ARMOR_SPENT;
            else if (words[s->a].kind == ARMOR_ITEMS && words[s->b].kind == ARMOR_REMOVED) result.kind = ARMOR_CLEARED;
            else if (words[s->a].kind == ARMOR_ITEMS2 && words[s->b].kind == ARMOR_REMOVED2) result.kind = ARMOR_CLEARED2;
        } else if (s->opcode == QA_QC_BITOR) {
            uint32_t a, b;
            if (mask_value(analysis, words, s->a, &a) && mask_value(analysis, words, s->b, &b))
                result = (armor_value){.kind = ARMOR_MASK, .mask = a | b};
        } else if (s->opcode == QA_QC_BITAND) {
            uint32_t mask;
            if ((words[s->a].kind == ARMOR_ITEMS && mask_value(analysis, words, s->b, &mask) && mask == item_mask && item_mask) ||
                (words[s->b].kind == ARMOR_ITEMS && mask_value(analysis, words, s->a, &mask) && mask == item_mask && item_mask))
                result.kind = ARMOR_REMOVED;
            else if ((words[s->a].kind == ARMOR_ITEMS2 && mask_value(analysis, words, s->b, &mask) && mask == item_mask2 && item_mask2) ||
                (words[s->b].kind == ARMOR_ITEMS2 && mask_value(analysis, words, s->a, &mask) && mask == item_mask2 && item_mask2))
                result.kind = ARMOR_REMOVED2;
        } else if (s->opcode == QA_QC_STORE_F || s->opcode == QA_QC_STORE_ENT || s->opcode == QA_QC_STORE_V) {
            for (uint32_t j = 0; j < access.write_count; ++j) copied[j] = copied_value(analysis, words, s->a + j);
            result = copied[0];
        } else if (s->opcode == QA_QC_CALL1 && words[4].kind == ARMOR_MULTIPLIED) {
            const qa_qc_function *function = callee(analysis, s, NULL, error);
            if (function && !function->named_builtin && function->first_statement == -38) result.kind = ARMOR_PRODUCT;
        }
        for (uint32_t j = 0; j < access.write_count; ++j) if (access.write[j] == stage->region.saved_word) {
            armor_value assigned = s->opcode == QA_QC_STORE_V ? copied[j] : j ? (armor_value){0} : result;
            if (assigned.kind != ARMOR_PRODUCT && assigned.kind != ARMOR_VALUE && assigned.kind != ARMOR_ZERO) return false;
            savings = savings || assigned.kind == ARMOR_PRODUCT;
        }
        if (s->opcode >= QA_QC_STOREP_F && s->opcode <= QA_QC_STOREP_FN) {
            armor_value source = copied_value(analysis, words, s->a);
            bool safe = false;
            if (s->opcode == QA_QC_STOREP_F) {
                if (words[s->b].kind == ARMOR_VALUE_ADDRESS && source.kind == ARMOR_SPENT) { safe = !spent; spent = true; }
                else if (words[s->b].kind == ARMOR_TYPE_ADDRESS && source.kind == ARMOR_ZERO) { safe = !type_written; type_written = true; }
                else if (words[s->b].kind == ARMOR_ITEMS_ADDRESS && source.kind == ARMOR_CLEARED) { safe = !items_written; items_written = true; }
                else if (words[s->b].kind == ARMOR_ITEMS2_ADDRESS && source.kind == ARMOR_CLEARED2) { safe = !items2_written; items2_written = true; }
            }
            *field_writes_safe = *field_writes_safe && safe;
        }
        for (uint32_t j = 0; j < access.write_count; ++j)
            words[access.write[j]] = s->opcode == QA_QC_STORE_V ? copied[j] : j ? (armor_value){0} : result;
    }
    return savings && spent;
}
static const qa_qc_statement *previous_write(const armor_analysis *analysis, uint32_t start,
    uint32_t before, uint32_t word_offset, uint32_t *index)
{
    while (before > start) {
        const qa_qc_statement *s = qa_qc_program_statement(analysis->program, --before);
        qa_qc_statement_access access;
        if (!access_at(analysis, before, &access, NULL)) return NULL;
        for (uint32_t i = 0; i < access.write_count; ++i)
            if (access.write[i] == word_offset) { if (index) *index = before; return s; }
        if (called(s->opcode) || s->opcode == QA_QC_GOTO || s->opcode == QA_QC_IF ||
            s->opcode == QA_QC_IFNOT || s->opcode == QA_QC_RETURN || s->opcode == QA_QC_DONE) return NULL;
    }
    return NULL;
}
static const qa_qc_statement *value_source(const armor_analysis *analysis, uint32_t start,
    uint32_t before, uint32_t word_offset, uint32_t *index)
{
    const qa_qc_statement *s;
    uint32_t at;
    while ((s = previous_write(analysis, start, before, word_offset, &at)) != NULL) {
        if (s->opcode != QA_QC_STORE_F && s->opcode != QA_QC_STORE_ENT && s->opcode != QA_QC_STORE_V) {
            if (index) *index = at;
            return s;
        }
        before = at; word_offset = s->a + (s->opcode == QA_QC_STORE_V ? word_offset - s->b : 0);
    }
    return NULL;
}
static bool field_source(const armor_analysis *analysis, uint32_t start, uint32_t before,
    uint32_t word_offset, qa_qc_opcode opcode, uint32_t target, uint32_t field)
{
    const qa_qc_statement *s = value_source(analysis, start, before, word_offset, NULL);
    return s && s->opcode == opcode && s->a == target && s->b == field;
}
static bool zero_guard(const armor_analysis *analysis, uint32_t start, uint32_t at,
    uint32_t target, uint32_t armor)
{
    const qa_qc_statement *branch = qa_qc_program_statement(analysis->program, at);
    uint32_t producer;
    const qa_qc_statement *s = value_source(analysis, start, at, branch->a, &producer);
    if (!s) return false;
    if (s->opcode == QA_QC_LOAD_F && s->a == target && s->b == armor)
        return branch->opcode == QA_QC_IFNOT;
    if ((s->opcode != QA_QC_NE_F || branch->opcode != QA_QC_IFNOT) &&
        (s->opcode != QA_QC_EQ_F || branch->opcode != QA_QC_IF)) return false;
    float value;
    return (constant_float(analysis, s->a, &value) && value == 0 &&
        field_source(analysis, start, producer, s->b, QA_QC_LOAD_F, target, armor)) ||
        (constant_float(analysis, s->b, &value) && value == 0 &&
        field_source(analysis, start, producer, s->a, QA_QC_LOAD_F, target, armor));
}
static bool scale_site(const armor_analysis *analysis, const qa_qc_function *function,
    uint32_t at, uint32_t target_slot, uint32_t armor, float *scale)
{
    uint32_t start = (uint32_t)function->first_statement;
    const qa_qc_statement *argument = previous_write(analysis, start, at, target_slot, NULL);
    if (!argument || (argument->opcode != QA_QC_STORE_ENT && argument->opcode != QA_QC_STORE_F &&
        argument->opcode != QA_QC_STORE_V) || argument->b != target_slot ||
        !typed_word(analysis->program, argument->a, QA_QC_ENTITY)) return false;
    uint32_t target = argument->a, write = at;
    const qa_qc_statement *store = NULL;
    while (write > start) {
        const qa_qc_statement *s = qa_qc_program_statement(analysis->program, --write);
        if (called(s->opcode) || s->opcode == QA_QC_IF || s->opcode == QA_QC_IFNOT ||
            s->opcode == QA_QC_GOTO || s->opcode == QA_QC_RETURN || s->opcode == QA_QC_DONE) return false;
        if (s->opcode >= QA_QC_STOREP_F && s->opcode <= QA_QC_STOREP_FN) { store = s; break; }
    }
    if (!store || store->opcode != QA_QC_STOREP_F ||
        !field_source(analysis, start, write, store->b, QA_QC_ADDRESS, target, armor)) return false;
    uint32_t product;
    const qa_qc_statement *multiply = value_source(analysis, start, write, store->a, &product);
    if (!multiply || multiply->opcode != QA_QC_MUL_F ||
        !((field_source(analysis, start, product, multiply->a, QA_QC_LOAD_F, target, armor) &&
           constant_float(analysis, multiply->b, scale)) ||
          (field_source(analysis, start, product, multiply->b, QA_QC_LOAD_F, target, armor) &&
           constant_float(analysis, multiply->a, scale))) || *scale < 0) return false;
    uint32_t saved = UINT32_MAX, captured = product;
    while (captured > start) {
        const qa_qc_statement *s = qa_qc_program_statement(analysis->program, --captured);
        if (called(s->opcode) || s->opcode == QA_QC_IF || s->opcode == QA_QC_IFNOT ||
            s->opcode == QA_QC_GOTO || (s->opcode >= QA_QC_STOREP_F && s->opcode <= QA_QC_STOREP_FN)) return false;
        if (s->opcode == QA_QC_STORE_F && typed_local(analysis->program, function, s->b, QA_QC_FLOAT) &&
            field_source(analysis, start, captured, s->a, QA_QC_LOAD_F, target, armor)) { saved = s->b; break; }
        if (s->opcode == QA_QC_LOAD_F && s->a == target && s->b == armor &&
            typed_local(analysis->program, function, s->c, QA_QC_FLOAT)) { saved = s->c; break; }
    }
    if (saved == UINT32_MAX) return false;
    uint32_t skip = UINT32_MAX, end = qa_qc_program_function_end(analysis->program, function);
    for (uint32_t i = captured + 1; i < end; ++i) {
        const qa_qc_statement *s = qa_qc_program_statement(analysis->program, i);
        qa_qc_statement_access access;
        if (!access_at(analysis, i, &access, NULL)) return false;
        for (uint32_t j = 0; j < access.write_count; ++j)
            if (access.write[j] == saved || access.write[j] == target) return false;
        if (i <= at) continue;
        if (s->opcode == QA_QC_IF || s->opcode == QA_QC_IFNOT) {
            int64_t destination = (int64_t)i + (int16_t)s->b;
            if (skip != UINT32_MAX || destination <= i || destination >= end ||
                !zero_guard(analysis, at + 1, i, target, armor)) return false;
            skip = (uint32_t)destination;
        } else if (called(s->opcode) || s->opcode == QA_QC_GOTO || s->opcode == QA_QC_RETURN ||
            s->opcode == QA_QC_DONE || s->opcode == QA_QC_STATE) return false;
        else if (s->opcode >= QA_QC_STOREP_F && s->opcode <= QA_QC_STOREP_FN)
            return s->opcode == QA_QC_STOREP_F && s->a == saved &&
                (skip == UINT32_MAX || skip == i + 1) &&
                field_source(analysis, at + 1, i, s->b, QA_QC_ADDRESS, target, armor);
    }
    return false;
}
static bool scales_compile(const armor_analysis *analysis, uint32_t damage, uint32_t target_slot,
    uint32_t armor, application_qc_armor_stage *stage, qa_error *error)
{
    size_t count = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t f = 1; f < analysis->info.function_count; ++f) {
            const qa_qc_function *function = qa_qc_program_function(analysis->program, f);
            if (function->first_statement <= 0 || function->named_builtin) continue;
            uint32_t end = qa_qc_program_function_end(analysis->program, function);
            for (uint32_t i = (uint32_t)function->first_statement; i < end; ++i) {
                const qa_qc_statement *s = qa_qc_program_statement(analysis->program, i);
                uint32_t callee_index; float scale;
                if (s->opcode != QA_QC_CALL4 || !callee(analysis, s, &callee_index, NULL) ||
                    callee_index != damage || !scale_site(analysis, function, i, target_slot, armor, &scale)) continue;
                if (pass) stage->scales[stage->scale_count++] =
                    (application_qc_armor_scale){.caller = f, .statement = i, .scale = scale};
                else ++count;
            }
        }
        if (!pass) {
            if (!count) return true;
            if (count > SIZE_MAX / sizeof(*stage->scales) ||
                !(stage->scales = calloc(count, sizeof(*stage->scales))))
                return application_fail(error, QA_ERROR_MEMORY, "Owning compiled QC armor call sites");
        }
    }
    return true;
}
bool application_qc_armor_compile(const qa_qc_program *program, const application_qc_call *call,
    application_qc_armor_stage *stage, bool *found, qa_error *error)
{
    if (!program || !call || !stage || !found)
        return reject(error, "QC armor discovery requires its actual compiled damage call");
    *stage = (application_qc_armor_stage){0}; *found = false;
    uint32_t index;
    const qa_qc_function *function = qa_qc_program_find_function(program, "T_Damage", &index);
    if (!function || function->first_statement <= 0 || function->named_builtin || index != call->function ||
        function->parameter_count != call->argument_count || call->argument_count != 4) return true;
    application_qc_armor_stage candidate = {.region.function = index};
    uint32_t offset = function->parameter_start, target_slot = 0;
    bool target_found = false, damage_found = false;
    for (size_t i = 0; i < call->argument_count; ++i) {
        if (function->parameter_sizes[i] != 1) return true;
        if (call->arguments[i].kind == QC_VALUE_INPUT && call->arguments[i].source == QC_INPUT_SELF) {
            if (target_found) return true;
            candidate.target = offset; target_slot = 4 + 3 * (uint32_t)i; target_found = true;
        }
        if (call->arguments[i].kind == QC_VALUE_INPUT && call->arguments[i].source == QC_INPUT_AMOUNT) {
            if (damage_found) return true;
            candidate.damage = offset; damage_found = true;
        }
        offset += function->parameter_sizes[i];
    }
    if (!target_found || !damage_found) return true;
    armor_analysis analysis = {0};
    qa_error failure = {0};
    bool ok = analysis_create(program, &analysis, &failure);
    armor_value *words = NULL;
    if (!ok) {
        analysis_free(&analysis);
        if (failure.code == QA_ERROR_MEMORY) { if (error) *error = failure; return false; }
        return true;
    }
    uint32_t armor, value, items = UINT32_MAX, items2 = UINT32_MAX;
    if (!field_word(&analysis, "armortype", &armor) || !field_word(&analysis, "armorvalue", &value)) goto done;
    (void)field_word(&analysis, "items", &items); (void)field_word(&analysis, "items2", &items2);
    uint32_t item_mask = armor_item_mask(&analysis, false), item_mask2 = armor_item_mask(&analysis, true);
    words = calloc(analysis.info.global_words, sizeof(*words));
    if (!words) { ok = application_fail(error, QA_ERROR_MEMORY, "Owning compiled QC armor values"); goto done; }
    uint32_t start = (uint32_t)function->first_statement, end = qa_qc_program_function_end(program, function);
    for (uint32_t join = start; join < end; ++join) {
        const qa_qc_statement *s = qa_qc_program_statement(program, join);
        if (s->opcode != QA_QC_SUB_F || s->a != candidate.damage ||
            !typed_local(program, function, s->b, QA_QC_FLOAT) || s->b == candidate.damage) continue;
        candidate.region.exit = join; candidate.region.saved_word = s->b;
        for (uint32_t entry = join; entry > start;) {
            candidate.region.entry = --entry;
            if (!armor_entry(program, &candidate) || !closed_entry(program, function, &candidate)) continue;
            qa_error proof = {0}; bool field_writes_safe;
            if (!armor_dataflow(&analysis, &candidate, armor, value, items, items2,
                item_mask, item_mask2, words, &field_writes_safe, &proof) ||
                !qualify(&analysis, &candidate, &proof)) {
                if (proof.code == QA_ERROR_MEMORY) { if (error) *error = proof; ok = false; goto done; }
                continue;
            }
            if (analysis.indirect_input_writes) continue;
            if (!field_writes_safe || analysis.indirect_entity_writes) {
                candidate.region.replaceable = false; candidate.region.saved_scope = QA_QC_INLINE_NOT_STANDALONE;
            }
            if (*found) { *found = false; application_qc_armor_free(stage); goto done; }
            *stage = candidate; *found = true; break;
        }
    }
    if (*found) ok = scales_compile(&analysis, index, target_slot, armor, stage, error);
done:
    free(words); analysis_free(&analysis);
    if (!ok || !*found) { application_qc_armor_free(stage); *found = false; }
    return ok;
}
bool application_qc_region_private_writes_dead(const qa_qc_program *program,
    const qa_qc_inline_region *region, uint32_t damage_word, const uint32_t *scratch_words,
    size_t count, qa_error *error)
{
    const qa_qc_function *function = program && region ? qa_qc_program_function(program, region->function) : NULL;
    if (!function || function->first_statement <= 0 || function->named_builtin || (count && !scratch_words) ||
        region->entry < (uint32_t)function->first_statement || region->exit <= region->entry ||
        region->exit >= qa_qc_program_function_end(program, function) || (!typed_local(program, function, damage_word, QA_QC_FLOAT) &&
        !(region->saved_scope == QA_QC_INLINE_GLOBAL && region->saved_word == damage_word && damage_word >= 28 &&
          (damage_word < function->parameter_start || damage_word - function->parameter_start >= function->local_words) &&
          typed_word(program, damage_word, QA_QC_FLOAT))))
        return reject(error, "QC private-write proof requires its actual typed source continuation");
    armor_analysis analysis = {0};
    bool ok = analysis_create(program, &analysis, error);
    size_t width = ((size_t)analysis.info.global_words + 7) / 8;
    uint8_t *dirty = ok ? calloc(width, 1) : NULL;
    if (ok && !dirty) ok = application_fail(error, QA_ERROR_MEMORY, "Owning QC private continuation proof");
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t offset = scratch_words[i];
        if (offset >= analysis.info.global_words || offset == damage_word || (offset >= 28 && analysis.named[offset] &&
            (offset < function->parameter_start || offset - function->parameter_start >= function->local_words)))
            ok = reject(error, "QC scratch write is not private to its actual source frame");
        else mark(dirty, offset, true);
    }
    if (ok) ok = flow_safe(&analysis, region->exit, qa_qc_program_function_end(program, function), dirty, false, 0, error);
    free(dirty); analysis_free(&analysis); return ok;
}
