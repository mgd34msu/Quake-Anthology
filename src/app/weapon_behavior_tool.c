#include "weapon_behavior_tool.h"
#include "qa/json_writer.h"
#include "qa/qc.h"
#include "qa/platform_services.h"
#include <stdlib.h>
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

bool qa_weapon_behavior_tool_arguments(int *argc, char **argv,
    qa_weapon_behavior_tool_command *command, bool *handled, bool *help, qa_error *error)
{
    *handled = *argc > 1 && !strcmp(argv[1], "weapon-behavior");
    *help = false;
    if (!*handled) return true;
    for (int i = 2; i < *argc; ++i)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { *help = true; return true; }
    if (*argc < 4 || !argv[3][0] || argv[3][0] == '-') {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "weapon-behavior requires an action and installed product"); return false;
    }
    *command = (qa_weapon_behavior_tool_command){.product = argv[3]};
    if (!strcmp(argv[2], "inspect")) command->action = QA_WEAPON_INSPECT;
    else if (!strcmp(argv[2], "declare")) command->action = QA_WEAPON_DECLARE_QC;
    else if (!strcmp(argv[2], "declare-qvm")) command->action = QA_WEAPON_DECLARE_QVM;
    else if (!strcmp(argv[2], "declare-native")) command->action = QA_WEAPON_DECLARE_NATIVE;
    else { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown weapon-behavior action: %s", argv[2]); return false; }
    int count = 1;
    for (int i = 4; i < *argc; ++i) {
        const char **field = NULL;
        if (!strcmp(argv[i], "--artifact")) field = &command->artifact;
        else if (!strcmp(argv[i], "--id")) field = &command->id;
        else if (!strcmp(argv[i], "--title")) field = &command->title;
        else if (!strcmp(argv[i], "--role")) field = &command->role;
        else if (!strcmp(argv[i], "--fire")) field = &command->fire;
        else if (!strcmp(argv[i], "--activate")) field = &command->activate;
        else if (!strcmp(argv[i], "--profile")) field = &command->profile;
        if (field) {
            if (*field || i + 1 == *argc || !argv[i + 1][0] || argv[i + 1][0] == '-') {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Option %s requires one value", argv[i]); return false;
            }
            *field = argv[++i];
        } else if (!strcmp(argv[i], "--content")) argv[count++] = "--content-root";
        else if (!strcmp(argv[i], "--user-content")) argv[count++] = "--user-content-root";
        else argv[count++] = argv[i];
    }
    bool qc_fields = command->id || command->title || command->role || command->fire || command->activate;
    bool profile_action = command->action == QA_WEAPON_DECLARE_QVM || command->action == QA_WEAPON_DECLARE_NATIVE;
    if ((command->action == QA_WEAPON_INSPECT && qc_fields) ||
        (command->action == QA_WEAPON_DECLARE_QC && (!command->id || !command->role || !command->fire)) ||
        (profile_action && (!command->profile || qc_fields)) || (!profile_action && command->profile)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "declare needs --id, --role and --fire; declare-qvm/native need only --profile");
        return false;
    }
    *argc = count; argv[count] = NULL;
    return true;
}

static bool write_output(FILE *stream, qa_bytes output, qa_error *error)
{
    if (fwrite(output.data, 1, output.size, stream) == output.size && fputc('\n', stream) != EOF) return true;
    qa_error_set(error, QA_ERROR_IO, 0, "Writing weapon behavior result"); return false;
}

static bool inspect_product(qa_catalog *catalog, const qa_product *product, qa_vfs *vfs,
    const char *artifact, FILE *stream, qa_error *error)
{
    qa_resource *resource = NULL;
    qa_qc_program *qc = NULL;
    qa_qvm_image *qvm = NULL;
    qa_json_writer writer = {0};
    qa_buffer output = {0};
    bool okay = true;
    if (!artifact) artifact = product->program;
    for (size_t i = 0; okay && !artifact && i < qa_catalog_weapon_behavior_count(catalog); ++i) {
        const qa_catalog_weapon_behavior *behavior = qa_catalog_weapon_behavior_at(catalog, i);
        if (behavior->product == product->id) artifact = behavior->artifact_path;
    }
    if (okay && !artifact) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Product has no discovered program; select a mounted path with --artifact");
        okay = false;
    }
    if (okay) okay = qa_vfs_acquire(vfs, artifact, &resource, NULL, error);
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
                    if (behavior->product != product->id || !behavior->artifact_path || strcmp(behavior->artifact_path, artifact)) continue;
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
    if (okay) okay = write_output(stream, (qa_bytes){output.data, output.size}, error);
    qa_buffer_free(&output);
    qa_json_writer_destroy(&writer);
    qa_qvm_image_release(qvm);
    qa_qc_program_destroy(qc);
    qa_resource_release(resource);
    return okay;
}

static bool text_field(const qa_json_document *document, qa_json_id object, const char *key,
    qa_buffer *out, qa_error *error)
{
    if (!qa_json_string(document, qa_json_get(document, object, key), out, error)) return false;
    if (out->size && !memchr(out->data, 0, out->size)) return true;
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Declaration %s requires nonempty text without NUL", key); return false;
}

static bool object_members(qa_json_writer *writer, const qa_json_document *document, qa_json_id object,
    const char *skip, qa_error *error)
{
    for (size_t i = 0; i < qa_json_size(document, object); ++i) {
        qa_buffer key = {0};
        if (!qa_json_string(document, qa_json_key_at(document, object, i), &key, error)) return false;
        const char *name = (const char *)key.data;
        if (strcmp(name, "artifactDigest") && strcmp(name, "digest") && (!skip || strcmp(name, skip))) {
            qa_json_writer_key(writer, name);
            qa_json_writer_value(writer, document, qa_json_at(document, object, i));
        }
        qa_buffer_free(&key);
    }
    return writer_current(writer, error);
}

static bool declare_product(qa_catalog *catalog, const qa_product *product, qa_vfs *vfs,
    const qa_weapon_behavior_tool_command *command, FILE *stream, qa_error *error)
{
    qa_program_kind runtime = command->action == QA_WEAPON_DECLARE_QC ? QA_PROGRAM_QUAKEC :
        command->action == QA_WEAPON_DECLARE_QVM ? QA_PROGRAM_QVM : QA_PROGRAM_NATIVE;
    const char *filename = runtime == QA_PROGRAM_QUAKEC ? "weapon-behaviors.json" :
        runtime == QA_PROGRAM_QVM ? "qvm-weapon-behaviors.json" : "native-weapon-behaviors.json";
    const char *list_name = runtime == QA_PROGRAM_QUAKEC ? "behaviors" : "profiles";
    qa_resource *profile = NULL, *artifact = NULL, *previous = NULL;
    qa_json_document *entry_document = NULL, *old_document = NULL;
    qa_json_writer writer = {0};
    qa_buffer entry_bytes = {0}, output = {0}, id = {0}, title = {0}, path = {0};
    char *normalized = NULL, *explicit_path = NULL, *destination = NULL;
    bool okay = true;
    if (runtime == QA_PROGRAM_QUAKEC) {
        qa_json_writer_object(&writer);
        qa_json_writer_key(&writer, "id"); qa_json_writer_string(&writer, command->id);
        qa_json_writer_key(&writer, "title"); qa_json_writer_string(&writer, command->title ? command->title : command->id);
        qa_json_writer_key(&writer, "role"); qa_json_writer_string(&writer, command->role);
        qa_json_writer_key(&writer, "aspect"); qa_json_writer_string(&writer, "trajectory");
        qa_json_writer_key(&writer, "artifactPath");
        qa_json_writer_string(&writer, command->artifact ? command->artifact : product->program ? product->program : "progs.dat");
        qa_json_writer_key(&writer, "fireFunction"); qa_json_writer_string(&writer, command->fire);
        if (command->activate) {
            qa_json_writer_key(&writer, "activationFunction"); qa_json_writer_string(&writer, command->activate);
        }
        qa_json_writer_end(&writer);
        okay = qa_json_writer_finish(&writer, &entry_bytes, error);
        qa_json_writer_destroy(&writer); writer = (qa_json_writer){0};
    } else okay = qa_vfs_acquire(vfs, command->profile, &profile, NULL, error);
    qa_bytes source = profile ? qa_resource_bytes(profile) : (qa_bytes){entry_bytes.data, entry_bytes.size};
    if (okay) okay = qa_json_parse(QA_JSON_STRICT, source, &entry_document, error);
    qa_json_id entry = qa_json_root(entry_document);
    if (okay) okay = text_field(entry_document, entry, "id", &id, error) &&
        text_field(entry_document, entry, "title", &title, error) &&
        text_field(entry_document, entry, "artifactPath", &path, error);
    if (okay && runtime != QA_PROGRAM_QVM && !qa_catalog_weapon_behavior_id_valid((const char *)id.data)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Trajectory identity requires a namespace: %s", id.data); okay = false;
    }
    qa_builtin_projectile_role role = QA_BUILTIN_ROCKET;
    while (okay && role <= QA_BUILTIN_GRAPPLE && !qa_json_string_equal(entry_document,
        qa_json_get(entry_document, entry, "role"), qa_catalog_weapon_behavior_role_name(role))) ++role;
    if (okay && (role > QA_BUILTIN_GRAPPLE ||
        !qa_json_string_equal(entry_document, qa_json_get(entry_document, entry, "aspect"), "trajectory"))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Declaration has an unknown projectile role or aspect"); okay = false;
    }
    if (okay) { normalized = qa_archive_normalize_path((const char *)path.data, error); okay = normalized != NULL; }
    if (okay && command->artifact) {
        explicit_path = qa_archive_normalize_path(command->artifact, error); okay = explicit_path != NULL;
        if (okay && strcmp(normalized, explicit_path)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "--artifact differs from the authored profile"); okay = false;
        }
    }
    if (okay) okay = qa_vfs_acquire(vfs, normalized, &artifact, NULL, error);
    if (okay) {
        qa_catalog_weapon_behavior behavior = {.product = product->id, .runtime = runtime, .role = role,
            .id = (const char *)id.data, .title = (const char *)title.data,
            .artifact_path = normalized, .artifact_resource = artifact, .entry = source};
        okay = qa_application_weapon_behavior_check(&behavior, error);
    }
    if (okay) {
        qa_error issue = {0};
        if (!qa_vfs_acquire(vfs, filename, &previous, NULL, &issue) && issue.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = issue;
            okay = false;
        }
        if (okay && previous) okay = qa_json_parse(QA_JSON_STRICT, qa_resource_bytes(previous), &old_document, error);
    }
    qa_json_id old_root = qa_json_root(old_document), old_entries = qa_json_get(old_document, old_root, list_name);
    if (okay && old_document) {
        uint64_t version;
        okay = qa_json_u64(old_document, qa_json_get(old_document, old_root, "version"), &version, error) &&
            version == 1 && qa_json_type(old_document, old_entries) == QA_JSON_ARRAY;
        if (!okay && (!error || error->code == QA_OK))
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid existing weapon behavior document");
    }
    if (okay) {
        qa_json_writer_object(&writer);
        if (old_document) okay = object_members(&writer, old_document, old_root, list_name, error);
        else { qa_json_writer_key(&writer, "version"); qa_json_writer_u64(&writer, 1); }
        qa_json_writer_key(&writer, list_name); qa_json_writer_array(&writer);
        for (size_t i = 0; okay && i < qa_json_size(old_document, old_entries); ++i) {
            qa_json_id old = qa_json_at(old_document, old_entries, i);
            if (qa_json_string_equal(old_document, qa_json_get(old_document, old, "id"), (const char *)id.data)) continue;
            qa_json_writer_object(&writer);
            okay = object_members(&writer, old_document, old, NULL, error);
            qa_json_writer_end(&writer);
        }
        qa_json_writer_object(&writer);
        if (okay) okay = object_members(&writer, entry_document, entry, NULL, error);
        qa_json_writer_end(&writer);
        qa_json_writer_end(&writer); qa_json_writer_end(&writer);
        if (okay) okay = qa_json_writer_finish(&writer, &output, error);
    }
    qa_fs_root *root = okay ? qa_catalog_product_write_root(catalog, product->id) : NULL;
    if (okay && !root) { qa_error_set(error, QA_ERROR_IO, 0, "Product has no writable user-content directory"); okay = false; }
    uint64_t nonce = 0;
    if (okay) okay = qa_fs_root_join(root, filename, &destination, error) &&
        qa_platform_entropy_u64(&nonce, error) &&
        qa_fs_root_replace(root, filename, (qa_bytes){output.data, output.size}, nonce, error);
    if (okay) {
        const char *prefix = runtime == QA_PROGRAM_QVM ? "qvm:" : "";
        if (fprintf(stream, "Saved %s%s to %s\nSelect with --weapon-behavior %s/%s%s\n",
            prefix, id.data, destination, product->key, prefix, id.data) < 0) {
            qa_error_set(error, QA_ERROR_IO, 0, "Writing declaration destination"); okay = false;
        }
    }
    free(destination); free(explicit_path); free(normalized);
    qa_buffer_free(&entry_bytes); qa_buffer_free(&output); qa_buffer_free(&id); qa_buffer_free(&title); qa_buffer_free(&path);
    qa_json_writer_destroy(&writer); qa_json_destroy(old_document); qa_json_destroy(entry_document);
    qa_resource_release(previous); qa_resource_release(artifact); qa_resource_release(profile);
    return okay;
}

bool qa_weapon_behavior_tool_run(const qa_frontend_options *options,
    const qa_weapon_behavior_tool_command *command, FILE *stream, qa_error *error)
{
    qa_resource_pool *pool = qa_resource_pool_create(error);
    if (!pool) return false;
    qa_catalog *catalog = NULL;
    qa_vfs *vfs = NULL;
    qa_catalog_options discovery = {.resources = pool,
        .content_root = options->application.content_root, .user_root = options->application.user_root,
        .install_roots = options->application.install_roots, .install_root_count = options->application.install_root_count,
        .generation = options->application.catalog_generation, .discover_mods = true};
    bool okay = qa_catalog_discover(&discovery, &catalog, error);
    const qa_product *product = okay ? qa_catalog_find(catalog, command->product) : NULL;
    if (okay && !product) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Installed product not found: %s", command->product); okay = false;
    }
    if (okay) okay = qa_catalog_open(catalog, product->id, &vfs, error);
    if (okay) okay = command->action == QA_WEAPON_INSPECT ?
        inspect_product(catalog, product, vfs, command->artifact, stream, error) :
        declare_product(catalog, product, vfs, command, stream, error);
    qa_vfs_destroy(vfs); qa_catalog_release(catalog); qa_resource_pool_destroy(pool);
    return okay;
}
