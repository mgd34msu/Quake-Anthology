#include "weapon_behavior_tool.h"
#include "qa/json_writer.h"
#include "qa/qc.h"
#include <string.h>

typedef struct qc_inspection {
    qa_json_writer *writer;
    const qa_qc_program *program;
} qc_inspection;

static bool writer_current(const qa_json_writer *writer, qa_error *error)
{
    if (writer->failed && error) *error = writer->failure;
    return !writer->failed;
}

static bool think_binding(void *context, const qa_qc_think_binding *binding, qa_error *error)
{
    const qc_inspection *inspection = context;
    qa_json_writer *writer = inspection->writer;
    const qa_qc_function *producer = qa_qc_program_function(inspection->program, binding->producer_function);
    const qa_qc_function *think = qa_qc_program_function(inspection->program, binding->think_function);
    qa_json_writer_object(writer);
    qa_json_writer_key(writer, "producer"); qa_json_writer_string(writer, producer->name);
    qa_json_writer_key(writer, "producerIndex"); qa_json_writer_u64(writer, binding->producer_function);
    qa_json_writer_key(writer, "think"); qa_json_writer_string(writer, think->name);
    qa_json_writer_key(writer, "thinkIndex"); qa_json_writer_u64(writer, binding->think_function);
    qa_json_writer_key(writer, "statement"); qa_json_writer_u64(writer, binding->statement);
    qa_json_writer_end(writer);
    return writer_current(writer, error);
}

static bool inspect_qc(qa_json_writer *writer, const qa_qc_program *program, qa_error *error)
{
    qa_qc_program_info info = qa_qc_program_describe(program);
    qa_json_writer_key(writer, "format"); qa_json_writer_string(writer, "QuakeC");
    qa_json_writer_key(writer, "statementCount"); qa_json_writer_u64(writer, info.statement_count);
    qa_json_writer_key(writer, "entityFieldWords"); qa_json_writer_u64(writer, info.entity_field_words);
    qa_json_writer_key(writer, "callbacks"); qa_json_writer_array(writer);
    for (uint32_t i = 1; i < info.function_count; ++i) {
        const qa_qc_function *function = qa_qc_program_function(program, i);
        if (function->first_statement < 0 || function->parameter_count || function->named_builtin) continue;
        qa_json_writer_object(writer);
        qa_json_writer_key(writer, "name"); qa_json_writer_string(writer, function->name);
        qa_json_writer_key(writer, "index"); qa_json_writer_u64(writer, i);
        qa_json_writer_key(writer, "statement"); qa_json_writer_u64(writer, (uint32_t)function->first_statement);
        qa_json_writer_end(writer);
    }
    qa_json_writer_end(writer);
    qa_json_writer_key(writer, "thinkAssignments"); qa_json_writer_array(writer);
    qc_inspection inspection = {writer, program};
    bool okay = qa_qc_program_inspect_think_bindings(program, think_binding, &inspection, error);
    qa_json_writer_end(writer);
    return okay && writer_current(writer, error);
}

static void inspect_qvm(qa_json_writer *writer, const qa_qvm_image *image)
{
    qa_qvm_image_info info = qa_qvm_image_describe(image);
    qa_json_writer_key(writer, "format"); qa_json_writer_string(writer, "QVM");
    qa_json_writer_key(writer, "instructionCount"); qa_json_writer_u64(writer, info.instruction_count);
    qa_json_writer_key(writer, "dataBytes");
    qa_json_writer_u64(writer, (uint64_t)info.data_length + info.literal_length + info.bss_length);
    qa_json_writer_key(writer, "entries"); qa_json_writer_array(writer);
    size_t count;
    const qa_qvm_instruction *instructions = qa_qvm_image_instructions(image, &count);
    for (size_t i = 0; i < count; ++i) {
        if (instructions[i].opcode != QA_QVM_ENTER) continue;
        qa_json_writer_object(writer);
        qa_json_writer_key(writer, "instructionIndex"); qa_json_writer_u64(writer, i);
        qa_json_writer_key(writer, "byteOffset"); qa_json_writer_u64(writer, instructions[i].byte_offset);
        qa_json_writer_end(writer);
    }
    qa_json_writer_end(writer);
}

static bool image_range(void *context, const qa_native_image_range *range, qa_error *error)
{
    qa_json_writer *writer = context;
    qa_json_writer_object(writer);
    qa_json_writer_key(writer, "kind");
    qa_json_writer_string(writer, range->kind == QA_NATIVE_IMAGE_SECTION ? "section" : "segment");
    qa_json_writer_key(writer, "name");
    if (range->name) qa_json_writer_string(writer, range->name); else qa_json_writer_null(writer);
    qa_json_writer_key(writer, "address"); qa_json_writer_u64(writer, range->address);
    qa_json_writer_key(writer, "bytes"); qa_json_writer_u64(writer, range->bytes);
    qa_json_writer_key(writer, "fileOffset"); qa_json_writer_u64(writer, range->file_offset);
    qa_json_writer_key(writer, "fileBytes"); qa_json_writer_u64(writer, range->file_bytes);
    qa_json_writer_key(writer, "readable"); qa_json_writer_bool(writer, range->readable);
    qa_json_writer_key(writer, "writable"); qa_json_writer_bool(writer, range->writable);
    qa_json_writer_key(writer, "executable"); qa_json_writer_bool(writer, range->executable);
    qa_json_writer_end(writer);
    return writer_current(writer, error);
}

static bool inspect_native(qa_json_writer *writer, qa_bytes bytes, qa_error *error)
{
    qa_native_image_layout layout;
    qa_json_writer_key(writer, "ranges"); qa_json_writer_array(writer);
    if (!qa_native_image_inspect_layout(bytes, &layout, image_range, writer, error)) return false;
    qa_json_writer_end(writer);
    const char *formats[] = {"PE32", "PE32+", "ELF32", "ELF64"};
    const char *abis[] = {"cdecl-i386", "microsoft-x64", "system-v-i386", "system-v-x64", "aapcs64"};
    qa_json_writer_key(writer, "format"); qa_json_writer_string(writer, formats[layout.image.format]);
    qa_json_writer_key(writer, "abi"); qa_json_writer_string(writer, abis[layout.image.target.abi]);
    qa_json_writer_key(writer, "preferredBase"); qa_json_writer_u64(writer, layout.image.preferred_base);
    qa_json_writer_key(writer, "imageBytes"); qa_json_writer_u64(writer, layout.image.image_bytes);
    qa_json_writer_key(writer, "entryPoint"); qa_json_writer_u64(writer, layout.entry_point);
    return writer_current(writer, error);
}

bool qa_weapon_behavior_inspect(const qa_frontend_options *options, const char *key,
    const char *artifact, FILE *stream, qa_error *error)
{
    qa_resource_pool *pool = qa_resource_pool_create(error);
    if (!pool) return false;
    qa_catalog *catalog = NULL;
    qa_vfs *vfs = NULL;
    qa_resource *resource = NULL;
    qa_qc_program *qc = NULL;
    qa_qvm_image *qvm = NULL;
    qa_json_writer writer = {0};
    qa_buffer output = {0};
    qa_catalog_options discovery = {.resources = pool,
        .content_root = options->application.content_root, .user_root = options->application.user_root,
        .install_roots = options->application.install_roots, .install_root_count = options->application.install_root_count,
        .generation = options->application.catalog_generation, .discover_mods = true};
    bool okay = qa_catalog_discover(&discovery, &catalog, error);
    const qa_product *product = okay ? qa_catalog_find(catalog, key) : NULL;
    if (okay && !product) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Installed product not found: %s", key); okay = false;
    }
    if (okay && !artifact) artifact = product->program;
    for (size_t i = 0; okay && !artifact && i < qa_catalog_weapon_behavior_count(catalog); ++i) {
        const qa_catalog_weapon_behavior *behavior = qa_catalog_weapon_behavior_at(catalog, i);
        if (behavior->product == product->id) artifact = behavior->artifact_path;
    }
    if (okay && !artifact) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Product has no discovered program; select a mounted path with --artifact");
        okay = false;
    }
    if (okay) okay = qa_catalog_open(catalog, product->id, &vfs, error) &&
        qa_vfs_acquire(vfs, artifact, &resource, NULL, error);
    if (okay) {
        qa_bytes bytes = qa_resource_bytes(resource);
        qa_error qc_error = {0}, qvm_error = {0};
        bool is_qc = qa_qc_program_load(bytes, artifact, &qc, &qc_error);
        bool is_qvm = false;
        if (!is_qc && qc_error.code == QA_ERROR_MEMORY) { if (error) *error = qc_error; okay = false; }
        if (okay && !is_qc) {
            is_qvm = qa_qvm_image_load(bytes, &qvm, &qvm_error);
            if (!is_qvm && qvm_error.code == QA_ERROR_MEMORY) { if (error) *error = qvm_error; okay = false; }
        }
        if (okay) {
            qa_json_writer_object(&writer);
            qa_json_writer_key(&writer, "product"); qa_json_writer_string(&writer, product->key);
            qa_json_writer_key(&writer, "artifact"); qa_json_writer_string(&writer, artifact);
            qa_json_writer_key(&writer, "scope");
            qa_json_writer_string(&writer, "Compiled entry locations and image ranges; projectile roles and private layouts require source-backed declarations.");
            if (is_qc) okay = inspect_qc(&writer, qc, error);
            else if (is_qvm) inspect_qvm(&writer, qvm);
            else okay = inspect_native(&writer, bytes, error);
            if (okay) {
                qa_json_writer_key(&writer, "declared"); qa_json_writer_array(&writer);
                for (size_t i = 0; i < qa_catalog_weapon_behavior_count(catalog); ++i) {
                    const qa_catalog_weapon_behavior *behavior = qa_catalog_weapon_behavior_at(catalog, i);
                    if (behavior->product != product->id || strcmp(behavior->artifact_path, artifact)) continue;
                    qa_json_writer_object(&writer);
                    qa_json_writer_key(&writer, "id"); qa_json_writer_string(&writer, behavior->id);
                    qa_json_writer_key(&writer, "title"); qa_json_writer_string(&writer, behavior->title);
                    if (behavior->unavailable) {
                        qa_json_writer_key(&writer, "unavailable"); qa_json_writer_string(&writer, behavior->unavailable);
                    }
                    qa_json_writer_end(&writer);
                }
                qa_json_writer_end(&writer);
                qa_json_writer_end(&writer);
                okay = qa_json_writer_finish(&writer, &output, error);
            }
        }
    }
    if (okay && (fwrite(output.data, 1, output.size, stream) != output.size || fputc('\n', stream) == EOF)) {
        qa_error_set(error, QA_ERROR_IO, 0, "Writing weapon behavior inspection"); okay = false;
    }
    qa_buffer_free(&output);
    qa_json_writer_destroy(&writer);
    qa_qvm_image_release(qvm);
    qa_qc_program_destroy(qc);
    qa_resource_release(resource);
    qa_vfs_destroy(vfs);
    qa_catalog_release(catalog);
    qa_resource_pool_destroy(pool);
    return okay;
}
