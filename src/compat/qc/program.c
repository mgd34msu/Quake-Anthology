#include "internal.h"
#include "qa/hash.h"
#include <stdio.h>

#define QC_PROGRAM_VERSION 6
#define QC_NETQUAKE_SYSTEM_CRC 5927
#define QC_QUAKEWORLD_SYSTEM_CRC 54730
#define QC_PROGRAM_HEADER_BYTES 60u

typedef struct qc_section {
    size_t offset, bytes;
    uint32_t count;
} qc_section;

char *qc_strdup(const char *text, qa_error *error)
{
    if (text == NULL) {
        qc_fail(error, QA_ERROR_ARGUMENT, 0, "missing string");
        return NULL;
    }
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        qc_fail(error, QA_ERROR_MEMORY, 0, "string size overflow");
        return NULL;
    }
    char *copy = malloc(length + 1u);
    if (copy == NULL) {
        qc_fail(error, QA_ERROR_MEMORY, 0, "allocating string copy");
        return NULL;
    }
    memcpy(copy, text, length + 1u);
    return copy;
}

bool qc_span(size_t offset, size_t count, size_t stride, size_t size,
             size_t *bytes, qa_error *error)
{
    if (bytes == NULL || stride == 0u)
        return qc_fail(error, QA_ERROR_ARGUMENT, offset, "invalid byte span request");
    if (offset > size || count > (size - offset) / stride)
        return qc_fail(error, QA_ERROR_FORMAT, offset, "byte span exceeds input");
    *bytes = count * stride;
    return true;
}

static bool read_section(qa_bytes input, size_t header_offset, size_t stride,
                         qc_section *out, qa_error *error)
{
    int32_t stored_offset = qa_load_i32le(input.data + header_offset);
    int32_t stored_count = qa_load_i32le(input.data + header_offset + 4u);
    if (stored_offset < 0)
        return qc_fail(error, QA_ERROR_FORMAT, header_offset,
                       "negative program section offset");
    if (stored_count < 0)
        return qc_fail(error, QA_ERROR_FORMAT, header_offset + 4u,
                       "negative program section count");
    size_t bytes;
    if (!qc_span((size_t)stored_offset, (size_t)stored_count, stride,
                 input.size, &bytes, error))
        return false;
    *out = (qc_section){(size_t)stored_offset, bytes,
                        (uint32_t)stored_count};
    return true;
}

static bool allocate_records(void **out, uint32_t count, size_t stride,
                             qa_error *error)
{
    if (count == 0u) {
        *out = NULL;
        return true;
    }
    if ((size_t)count > SIZE_MAX / stride)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "program table size overflow");
    void *records = calloc((size_t)count, stride);
    if (records == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "allocating program table");
    *out = records;
    return true;
}

static bool allocate_program_tables(qa_qc_program *program,
                                    qc_section statements,
                                    qc_section globals, qc_section fields,
                                    qc_section functions, qa_error *error)
{
    void *records;
    if (!allocate_records(&records, statements.count,
                          sizeof(*program->statements), error))
        return false;
    program->statements = records;
    if (!allocate_records(&records, globals.count,
                          sizeof(*program->globals), error))
        return false;
    program->globals = records;
    if (!allocate_records(&records, fields.count,
                          sizeof(*program->fields), error))
        return false;
    program->fields = records;
    if (!allocate_records(&records, functions.count,
                          sizeof(*program->functions), error))
        return false;
    program->functions = records;
    return true;
}

static bool copy_section(uint8_t **out, qa_bytes input, qc_section section,
                         qa_error *error)
{
    if (section.bytes == 0u) {
        *out = NULL;
        return true;
    }
    uint8_t *copy = malloc(section.bytes);
    if (copy == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, section.offset,
                       "allocating program section");
    memcpy(copy, input.data + section.offset, section.bytes);
    *out = copy;
    return true;
}

static bool program_string(const qa_qc_program *program, int32_t offset,
                           size_t record_offset, const char **out,
                           qa_error *error)
{
    if (offset < 0 || (uint32_t)offset >= program->string_bytes)
        return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                       "invalid program string offset");
    const uint8_t *begin = program->strings + (uint32_t)offset;
    size_t available = (size_t)program->string_bytes - (uint32_t)offset;
    if (memchr(begin, 0, available) == NULL)
        return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                       "unterminated program string");
    *out = (const char *)begin;
    return true;
}

static qa_qc_value_type definition_type(uint16_t native_type)
{
    switch (native_type) {
    case 0: return QA_QC_VOID;
    case 1: return QA_QC_STRING;
    case 2: return QA_QC_FLOAT;
    case 3: return QA_QC_VECTOR;
    case 4: return QA_QC_ENTITY;
    case 5: return QA_QC_FIELD;
    case 6: return QA_QC_FUNCTION;
    case 7: return QA_QC_POINTER;
    default: return QA_QC_OPAQUE;
    }
}

static bool read_definitions(qa_qc_program *program, qa_bytes input,
                             qc_section section, bool fields,
                             qa_error *error)
{
    qa_qc_definition *definitions = fields ? program->fields : program->globals;
    uint32_t limit = fields ? program->info.entity_field_words
                            : program->info.global_words;
    for (uint32_t index = 0; index < section.count; ++index) {
        size_t record_offset = section.offset + (size_t)index * 8u;
        const uint8_t *record = input.data + record_offset;
        uint16_t raw_type = qa_load_u16le(record);
        uint16_t offset = qa_load_u16le(record + 2u);
        const char *name;
        if (!program_string(program, qa_load_i32le(record + 4u),
                            record_offset + 4u, &name, error))
            return false;
        uint16_t native_type = raw_type & UINT16_C(0x7fff);
        bool save = (raw_type & UINT16_C(0x8000)) != 0;
        qa_qc_value_type type = definition_type(native_type);
        if (fields && save)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                           "entity field definition has a save flag");
        uint32_t width = type == QA_QC_VECTOR ? 3u : 1u;
        if ((uint32_t)offset > limit || width > limit - (uint32_t)offset)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset + 2u,
                           "program definition exceeds its storage");
        definitions[index] = (qa_qc_definition){
            .type = type,
            .native_type = native_type,
            .offset = offset,
            .save = save,
            .name = name
        };
    }
    return true;
}

static bool read_statements(qa_qc_program *program, qa_bytes input,
                            qc_section section, qa_error *error)
{
    for (uint32_t index = 0; index < section.count; ++index) {
        size_t record_offset = section.offset + (size_t)index * 8u;
        const uint8_t *record = input.data + record_offset;
        uint16_t opcode = qa_load_u16le(record);
        if (opcode > (uint16_t)QA_QC_BITOR)
            return qc_fail(error, QA_ERROR_UNSUPPORTED, record_offset,
                           "unsupported QuakeC opcode");
        /* Operands remain raw words. IF, IFNOT and GOTO consumers interpret
         * their displacement operand as a signed 16-bit value. */
        program->statements[index] = (qa_qc_statement){
            .opcode = (qa_qc_opcode)opcode,
            .a = qa_load_u16le(record + 2u),
            .b = qa_load_u16le(record + 4u),
            .c = qa_load_u16le(record + 6u)
        };
    }
    return true;
}

static bool read_functions(qa_qc_program *program, qa_bytes input,
                           qc_section section, qa_error *error)
{
    for (uint32_t index = 0; index < section.count; ++index) {
        size_t record_offset = section.offset + (size_t)index * 36u;
        const uint8_t *record = input.data + record_offset;
        int32_t first_statement = qa_load_i32le(record);
        int32_t parameter_start = qa_load_i32le(record + 4u);
        int32_t local_words = qa_load_i32le(record + 8u);
        int32_t parameter_count = qa_load_i32le(record + 24u);
        const char *name, *file;
        if (!program_string(program, qa_load_i32le(record + 16u),
                            record_offset + 16u, &name, error)
            || !program_string(program, qa_load_i32le(record + 20u),
                               record_offset + 20u, &file, error))
            return false;
        if (parameter_start < 0 || local_words < 0
            || parameter_count < 0 || parameter_count > 8)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                           "invalid function locals or parameters");
        if (first_statement == INT32_MIN)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                           "invalid QuakeC builtin number");
        uint64_t locals_end = (uint64_t)(uint32_t)parameter_start
                            + (uint32_t)local_words;
        if (locals_end > program->info.global_words)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset + 4u,
                           "function locals exceed global storage");
        if ((int64_t)first_statement >= program->info.statement_count)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset,
                           "function entry point exceeds statement table");

        bool named_builtin = index > 0u && first_statement == 0
                          && parameter_start == 0 && local_words == 0;
        uint32_t parameter_words = 0u;
        qa_qc_function *function = &program->functions[index];
        for (int32_t parameter = 0; parameter < parameter_count; ++parameter) {
            uint8_t width = record[28u + (size_t)parameter];
            if (first_statement >= 0 && !named_builtin
                && width != 1u && width != 3u)
                return qc_fail(error, QA_ERROR_FORMAT,
                               record_offset + 28u + (size_t)parameter,
                               "invalid interpreted function parameter size");
            function->parameter_sizes[parameter] = width;
            parameter_words += width;
        }
        /* Retail qcc can emit parameter words beyond local_words. The source
         * VM saves only local_words, then writes those parameters separately. */
        if (first_statement >= 0 && !named_builtin
            && (uint64_t)(uint32_t)parameter_start + parameter_words
                   > program->info.global_words)
            return qc_fail(error, QA_ERROR_FORMAT, record_offset + 4u,
                           "function parameters exceed global storage");

        function->first_statement = first_statement;
        function->parameter_start = (uint32_t)parameter_start;
        function->local_words = (uint32_t)local_words;
        function->name = name;
        function->file = file;
        function->parameter_count = (uint8_t)parameter_count;
        function->named_builtin = named_builtin;
    }
    return true;
}

static bool index_names(qa_qc_program *program, qa_error *error)
{
    const uint32_t counts[] = {program->info.global_count,
        program->info.field_count, program->info.function_count};
    for (size_t kind = 0; kind < 3; ++kind) {
        qc_name_index *index = &program->names[kind];
        void *ordinals;
        if (!qa_strings_create(&index->names, error) ||
            !allocate_records(&ordinals, counts[kind], sizeof(*index->ordinals), error))
            return false;
        index->ordinals = ordinals;
        size_t unique = 0;
        for (uint32_t ordinal = 0; ordinal < counts[kind]; ++ordinal) {
            const char *name = kind == 0 ? program->globals[ordinal].name :
                kind == 1 ? program->fields[ordinal].name : program->functions[ordinal].name;
            qa_string_id id;
            if (!qa_strings_intern_cstr(index->names, name, &id, error)) return false;
            if (id > unique) {
                index->ordinals[id - 1u] = ordinal;
                ++unique;
            }
        }
    }
    return true;
}

static bool find_name(const qc_name_index *index, const char *name, uint32_t *ordinal)
{
    qa_string_id id = qa_strings_find(index->names,
        (qa_bytes){(const uint8_t *)name, strlen(name)});
    if (id == QA_STRING_NONE) return false;
    *ordinal = index->ordinals[id - 1u];
    return true;
}

void qa_qc_program_destroy(qa_qc_program *program)
{
    if (program == NULL) return;
    free(program->source);
    free(program->statements);
    free(program->globals);
    free(program->fields);
    free(program->functions);
    free(program->strings);
    free(program->initial_globals);
    for (size_t kind = 0; kind < 3; ++kind) {
        qa_strings_destroy(program->names[kind].names);
        free(program->names[kind].ordinals);
    }
    free(program);
}

bool qa_qc_program_load(qa_bytes bytes, const char *source,
                        qa_qc_program **out, qa_error *error)
{
    if (out == NULL || source == NULL || (bytes.size > 0u && bytes.data == NULL))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "invalid QuakeC program input or output");
    if (bytes.size < QC_PROGRAM_HEADER_BYTES)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "truncated QuakeC program header");

    int32_t version = qa_load_i32le(bytes.data);
    int32_t system_crc = qa_load_i32le(bytes.data + 4u);
    if (version != QC_PROGRAM_VERSION)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0,
                       "unsupported QuakeC program version");
    qa_qc_api api;
    if (system_crc == QC_NETQUAKE_SYSTEM_CRC)
        api = QA_QC_API_NETQUAKE;
    else if (system_crc == QC_QUAKEWORLD_SYSTEM_CRC)
        api = QA_QC_API_QUAKEWORLD;
    else
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 4,
                       "unknown QuakeC system layout CRC");

    qc_section statements, globals, fields, functions, strings, values;
    if (!read_section(bytes, 8u, 8u, &statements, error)
        || !read_section(bytes, 16u, 8u, &globals, error)
        || !read_section(bytes, 24u, 8u, &fields, error)
        || !read_section(bytes, 32u, 36u, &functions, error)
        || !read_section(bytes, 40u, 1u, &strings, error)
        || !read_section(bytes, 48u, 4u, &values, error))
        return false;
    int32_t entity_field_words = qa_load_i32le(bytes.data + 56u);
    if (entity_field_words <= 0 || entity_field_words > 65536)
        return qc_fail(error, QA_ERROR_FORMAT, 56u,
                       "invalid QuakeC entity field count");
    if (strings.count == 0u)
        return qc_fail(error, QA_ERROR_FORMAT, strings.offset,
                       "empty QuakeC string table");
    if (values.count < QC_RESERVED_WORDS)
        return qc_fail(error, QA_ERROR_FORMAT, values.offset,
                       "missing reserved QuakeC global words");
    if (statements.count == 0u || functions.count == 0u)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "empty QuakeC function or statement table");

    qa_qc_program *program = calloc(1u, sizeof(*program));
    if (program == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0,
                       "allocating QuakeC program");
    program->source = qc_strdup(source, error);
    program->info = (qa_qc_program_info){
        .api = api,
        .system_crc = (uint32_t)system_crc,
        .entity_field_words = (uint32_t)entity_field_words,
        .global_words = values.count,
        .statement_count = statements.count,
        .global_count = globals.count,
        .field_count = fields.count,
        .function_count = functions.count,
        .file_crc = qa_crc_block(bytes)
    };
    program->string_bytes = strings.count;

    bool allocated = program->source != NULL
        && allocate_program_tables(program, statements, globals, fields,
                                   functions, error)
        && copy_section(&program->strings, bytes, strings, error)
        && copy_section(&program->initial_globals, bytes, values, error);
    if (!allocated) {
        qa_qc_program_destroy(program);
        return false;
    }
    if (program->strings[0] != 0u) {
        qa_qc_program_destroy(program);
        return qc_fail(error, QA_ERROR_FORMAT, strings.offset,
                       "QuakeC string zero must be empty");
    }
    if (!read_definitions(program, bytes, globals, false, error)
        || !read_definitions(program, bytes, fields, true, error)
        || !read_statements(program, bytes, statements, error)
        || !read_functions(program, bytes, functions, error)
        || !index_names(program, error)) {
        qa_qc_program_destroy(program);
        return false;
    }
#define QC_RESOLVE_FIELD(name) program->engine_fields.name = qa_qc_program_find_field(program, #name);
    QA_QC_GAME_FIELD_LIST(QC_RESOLVE_FIELD)
#undef QC_RESOLVE_FIELD
#define QC_RESOLVE_GLOBAL(name) program->engine_globals.name = qa_qc_program_find_global(program, #name);
    QA_QC_ENGINE_GLOBAL_LIST(QC_RESOLVE_GLOBAL)
#undef QC_RESOLVE_GLOBAL
    for (unsigned i = 0; i < 16; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "parm%u", i + 1);
        program->engine_globals.spawn_parameters[i] = qa_qc_program_find_global(program, name);
    }
    *out = program;
    return true;
}

bool qa_qc_program_load_vfs(qa_vfs *vfs, const char *path,
                            qa_qc_program **out, qa_error *error)
{
    if (vfs == NULL || path == NULL || out == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "invalid QuakeC VFS program request");
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(vfs, path, &resource, NULL, error)) return false;
    const char *source = qa_resource_path(resource);
    bool result = qa_qc_program_load(qa_resource_bytes(resource),
                                     source != NULL ? source : path,
                                     out, error);
    qa_resource_release(resource);
    return result;
}

qa_qc_program_info qa_qc_program_describe(const qa_qc_program *program)
{
    return program != NULL ? program->info : (qa_qc_program_info){0};
}
qa_bytes qa_qc_program_strings(const qa_qc_program *program)
{
    return program ? (qa_bytes){program->strings, program->string_bytes} : (qa_bytes){0};
}

const qa_qc_statement *qa_qc_program_statement(const qa_qc_program *program,
                                                uint32_t index)
{
    return program != NULL && index < program->info.statement_count
         ? &program->statements[index] : NULL;
}

const qa_qc_definition *qa_qc_program_global(const qa_qc_program *program,
                                              uint32_t index)
{
    return program != NULL && index < program->info.global_count
         ? &program->globals[index] : NULL;
}

bool qa_qc_program_initial_int(const qa_qc_program *program, uint32_t word,
                               int32_t *out, qa_error *error)
{
    if (program == NULL || out == NULL || word >= program->info.global_words)
        return qc_fail(error, QA_ERROR_ARGUMENT, word, "initial global word is outside its program");
    *out = qa_load_i32le(program->initial_globals + (size_t)word * 4u);
    return true;
}

const qa_qc_definition *qa_qc_program_field(const qa_qc_program *program,
                                             uint32_t index)
{
    return program != NULL && index < program->info.field_count
         ? &program->fields[index] : NULL;
}

const qa_qc_game_fields *qa_qc_program_resolved_fields(const qa_qc_program *program)
{
    return program ? &program->engine_fields : NULL;
}

const qa_qc_engine_globals *qa_qc_program_resolved_globals(const qa_qc_program *program)
{
    return program ? &program->engine_globals : NULL;
}

const qa_qc_function *qa_qc_program_function(const qa_qc_program *program,
                                              uint32_t index)
{
    return program != NULL && index < program->info.function_count
         ? &program->functions[index] : NULL;
}

uint32_t qa_qc_program_function_end(const qa_qc_program *program,
                                    const qa_qc_function *function)
{
    if (!program || !function) return 0;
    uint32_t end = program->info.statement_count;
    for (uint32_t i = 1; i < program->info.function_count; ++i) {
        const qa_qc_function *candidate = &program->functions[i];
        if (candidate->first_statement > function->first_statement
            && (uint32_t)candidate->first_statement < end)
            end = (uint32_t)candidate->first_statement;
    }
    return end;
}

const qa_qc_definition *qa_qc_program_find_global(const qa_qc_program *program,
                                                   const char *name)
{
    if (program == NULL || name == NULL) return NULL;
    uint32_t ordinal;
    return find_name(&program->names[0], name, &ordinal) ? &program->globals[ordinal] : NULL;
}

const qa_qc_definition *qa_qc_program_find_field(const qa_qc_program *program,
                                                  const char *name)
{
    if (program == NULL || name == NULL) return NULL;
    uint32_t ordinal;
    return find_name(&program->names[1], name, &ordinal) ? &program->fields[ordinal] : NULL;
}

const qa_qc_function *qa_qc_program_find_function(const qa_qc_program *program,
                                                   const char *name,
                                                   uint32_t *index_out)
{
    if (program == NULL || name == NULL) return NULL;
    uint32_t ordinal;
    /* First-name wins, including a named null function blocking later rows. */
    if (!find_name(&program->names[2], name, &ordinal) || ordinal == 0u) return NULL;
    if (index_out != NULL) *index_out = ordinal;
    return &program->functions[ordinal];
}

qa_qc_entity_layout qa_qc_default_entity_layout(const qa_qc_program *program,
                                                  qa_qc_profile profile)
{
    if (program == NULL) return (qa_qc_entity_layout){0};
    /* The host profile and compiled system globals are one ABI: rerelease uses
     * the NetQuake ABI/prefix, while QuakeWorld uses its larger edict prefix. */
    bool quakeworld = profile == QA_QC_QUAKEWORLD;
    if ((profile != QA_QC_NETQUAKE && profile != QA_QC_QUAKEWORLD
         && profile != QA_QC_RERELEASE)
        || quakeworld != (program->info.api == QA_QC_API_QUAKEWORLD))
        return (qa_qc_entity_layout){0};
    uint32_t variables = quakeworld ? 104u : 96u;
    return (qa_qc_entity_layout){
        .stride_bytes = variables + program->info.entity_field_words * 4u,
        .variables_offset_bytes = variables,
        .field_words = program->info.entity_field_words
    };
}
