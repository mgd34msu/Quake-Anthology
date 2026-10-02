#include "internal.h"

static bool json_object(const qa_json_document *document, qa_json_id id, const char *name,
                        qa_error *error) {
    if (id == QA_JSON_NONE || qa_json_type(document, id) != QA_JSON_OBJECT ||
        !qa_json_size(document, id)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "native declaration %s must be a nonempty object",
                     name);
        return false;
    }
    return true;
}

static bool normalize_path(qa_bytes input, char **out, qa_error *error) {
    if (!input.size || !input.data || input.data[0] == '/' || input.data[0] == '\\')
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native declaration artifact path must be relative");
    char *normalized = malloc(input.size + 1u);
    if (!normalized)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating normalized native artifact path");
    size_t component = 0;
    for (size_t index = 0; index < input.size; ++index) {
        uint8_t byte = input.data[index];
        if (!byte || byte == ':') {
            free(normalized);
            return native_fail(error, QA_ERROR_FORMAT, index,
                               "native artifact path contains a forbidden byte");
        }
        if (byte == '\\')
            byte = '/';
        if (byte == '/') {
            if (!component || (component == 1u && normalized[index - 1u] == '.') ||
                (component == 2u && normalized[index - 2u] == '.' &&
                 normalized[index - 1u] == '.')) {
                free(normalized);
                return native_fail(error, QA_ERROR_FORMAT, index,
                                   "native artifact path has an invalid component");
            }
            component = 0;
        } else {
            if (byte >= 'A' && byte <= 'Z')
                byte = (uint8_t)(byte + ('a' - 'A'));
            ++component;
        }
        normalized[index] = (char)byte;
    }
    if (!component || (component == 1u && normalized[input.size - 1u] == '.') ||
        (component == 2u && normalized[input.size - 2u] == '.' &&
         normalized[input.size - 1u] == '.')) {
        free(normalized);
        return native_fail(error, QA_ERROR_FORMAT, input.size,
                           "native artifact path has an invalid final component");
    }
    normalized[input.size] = 0;
    *out = normalized;
    return true;
}

static bool digest_text(qa_bytes text, qa_sha256_digest *out, qa_error *error) {
    if (text.size != 71 || memcmp(text.data, "sha256:", 7))
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native artifact digest must be lowercase sha256 text");
    char encoded[72];
    memcpy(encoded, text.data, text.size);
    encoded[text.size] = 0;
    for (size_t index = 7; index < text.size; ++index) {
        uint8_t byte = text.data[index];
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f')))
            return native_fail(error, QA_ERROR_FORMAT, index,
                               "native artifact digest is not lowercase hexadecimal");
    }
    return qa_sha256_parse(encoded, out, error);
}

static bool required_primary(const qa_json_document *document, qa_json_id primary,
                             qa_error *error) {
    static const char *required[] = {"weapons", "player",  "commands", "inventory",
                                     "drop",    "pickups", "world"};
    if (!json_object(document, primary, "primary", error))
        return false;
    for (size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index) {
        qa_json_id section = qa_json_get(document, primary, required[index]);
        if (!json_object(document, section, required[index], error))
            return false;
    }
    return true;
}

static bool callback_document(qa_native_declaration *declaration, const char *path,
    const qa_native_module *module, qa_error *error)
{
    const qa_json_document *d = declaration->document;
    qa_json_id root = qa_json_root(d), program = qa_json_get(d, root, "program"),
        target = qa_json_get(d, root, "target"), api = qa_json_get(d, target, "api"),
        abi = qa_json_get(d, target, "abi");
    qa_buffer declared_path = {0}, digest = {0};
    char *normalized = NULL;
    qa_sha256_digest expected;
    uint64_t version, pointer_bytes;
    bool classic = module->info.profile == QA_NATIVE_Q2_GAME_API3;
    bool ok = qa_json_u64(d, qa_json_get(d, root, "version"), &version, error) && version == 1 &&
        qa_json_string_equal(d, qa_json_get(d, root, "runtime"), "native") &&
        qa_json_string(d, qa_json_get(d, program, "path"), &declared_path, error) &&
        normalize_path((qa_bytes){declared_path.data, declared_path.size}, &normalized, error) &&
        !strcmp(normalized, path) &&
        qa_json_string(d, qa_json_get(d, program, "digest"), &digest, error) &&
        digest_text((qa_bytes){digest.data, digest.size}, &expected, error) &&
        qa_sha256_equal(&expected, &module->info.image.digest) &&
        qa_json_string_equal(d, qa_json_get(d, api, "kind"),
            classic ? "q2-classic-game" : "q2-rerelease-game") &&
        qa_json_u64(d, qa_json_get(d, api, "version"), &version, error) &&
        version == (classic ? 3u : 2023u) &&
        qa_json_string_equal(d, qa_json_get(d, abi, "kind"),
            classic ? "windows-i386" : "windows-x86-64") &&
        qa_json_string_equal(d, qa_json_get(d, abi, "image"), classic ? "pe32" : "pe32+") &&
        qa_json_string_equal(d, qa_json_get(d, abi, "call"), classic ? "cdecl" : "microsoft-x64") &&
        qa_json_u64(d, qa_json_get(d, abi, "pointerBytes"), &pointer_bytes, error) &&
        pointer_bytes == (classic ? 4u : 8u) &&
        module->info.image.target.os == QA_NATIVE_OS_WINDOWS &&
        module->info.image.target.abi == (classic ? QA_NATIVE_ABI_CDECL_I386 : QA_NATIVE_ABI_MICROSOFT_X64);
    static const char *arrays[] = {"actorRecords", "initialize", "project", "release", "callbacks", "cvars"};
    for (size_t i = 0; ok && i < sizeof(arrays) / sizeof(arrays[0]); ++i)
        ok = qa_json_type(d, qa_json_get(d, root, arrays[i])) == QA_JSON_ARRAY;
    qa_json_id spawn = qa_json_get(d, root, "spawnEntities"), entity = qa_json_get(d, root, "entityRecord");
    if (ok) ok = (qa_json_type(d, spawn) == QA_JSON_NULL || qa_json_type(d, spawn) == QA_JSON_STRING) &&
        (qa_json_type(d, entity) == QA_JSON_NULL || qa_json_type(d, entity) == QA_JSON_STRING);
    free(normalized); qa_buffer_free(&declared_path); qa_buffer_free(&digest);
    if (!ok && (!error || error->code == QA_OK))
        native_fail(error, QA_ERROR_FORMAT, 0, "native callbacks differ from the acquired artifact or declared target");
    declaration->callbacks = ok;
    return ok;
}

static bool executable_pe_rva(const qa_native_module *module, uint32_t rva) {
    qa_bytes bytes = {module->bytes, module->size};
    if (bytes.size < 0x40)
        return false;
    uint32_t pe = qa_load_u32le(bytes.data + 0x3c);
    if (pe > bytes.size || bytes.size - pe < 24u)
        return false;
    const uint8_t *coff = bytes.data + pe + 4u;
    uint16_t count = qa_load_u16le(coff + 2u);
    uint16_t optional = qa_load_u16le(coff + 16u);
    size_t table = (size_t)pe + 24u + optional;
    size_t table_bytes;
    if (!native_size_multiply(count, 40u, &table_bytes) || table > bytes.size ||
        table_bytes > bytes.size - table)
        return false;
    for (uint16_t index = 0; index < count; ++index) {
        const uint8_t *section = bytes.data + table + (size_t)index * 40u;
        uint32_t virtual_size = qa_load_u32le(section + 8u);
        uint32_t virtual_address = qa_load_u32le(section + 12u);
        uint32_t raw_size = qa_load_u32le(section + 16u);
        uint32_t extent = virtual_size > raw_size ? virtual_size : raw_size;
        if ((qa_load_u32le(section + 36u) & 0x20000000u) && rva >= virtual_address &&
            rva - virtual_address < extent)
            return true;
    }
    return false;
}

static bool executable_elf_rva(const qa_native_module *module, uint32_t rva) {
    qa_bytes bytes = {module->bytes, module->size};
    bool elf32 = module->info.image.format == QA_NATIVE_IMAGE_ELF32;
    size_t header = elf32 ? 52u : 64u;
    if (bytes.size < header)
        return false;
    uint64_t table = elf32 ? qa_load_u32le(bytes.data + 28u) : qa_load_u64le(bytes.data + 32u);
    uint16_t stride = qa_load_u16le(bytes.data + (elf32 ? 42u : 54u));
    uint16_t count = qa_load_u16le(bytes.data + (elf32 ? 44u : 56u));
    uint64_t address = module->info.image.preferred_base + rva;
    for (uint16_t index = 0; index < count; ++index) {
        uint64_t offset = table + (uint64_t)index * stride;
        if (!native_u64_fits_size(offset) || (size_t)offset > bytes.size ||
            stride > bytes.size - (size_t)offset)
            return false;
        const uint8_t *program = bytes.data + (size_t)offset;
        if (qa_load_u32le(program) != 1u)
            continue;
        uint32_t flags = qa_load_u32le(program + (elf32 ? 24u : 4u));
        uint64_t start = elf32 ? qa_load_u32le(program + 8u) : qa_load_u64le(program + 16u);
        uint64_t size = elf32 ? qa_load_u32le(program + 20u) : qa_load_u64le(program + 40u);
        if ((flags & 1u) && address >= start && address - start < size)
            return true;
    }
    return false;
}

static bool executable_rva(const qa_native_module *module, uint32_t rva) {
    return module->info.image.format == QA_NATIVE_IMAGE_PE32 ||
                   module->info.image.format == QA_NATIVE_IMAGE_PE32_PLUS
               ? executable_pe_rva(module, rva)
               : executable_elf_rva(module, rva);
}

static bool append_path(char **out, const char *prefix, qa_bytes component, bool array,
                        qa_error *error) {
    size_t prefix_size = strlen(prefix), escaped = 0;
    if (!array) {
        for (size_t at = 0; at < component.size; ++at)
            escaped += component.data[at] == '~' || component.data[at] == '/' ? 2u : 1u;
    } else {
        escaped = component.size;
    }
    size_t size;
    if (!native_size_add(prefix_size, escaped + 2u, &size))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native declaration path length overflows");
    char *path = malloc(size);
    if (!path)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native declaration path");
    memcpy(path, prefix, prefix_size);
    size_t used = prefix_size;
    path[used++] = '/';
    for (size_t at = 0; at < component.size; ++at) {
        uint8_t byte = component.data[at];
        if (!array && byte == '~') {
            path[used++] = '~';
            path[used++] = '0';
        } else if (!array && byte == '/') {
            path[used++] = '~';
            path[used++] = '1';
        } else {
            path[used++] = (char)byte;
        }
    }
    path[used] = 0;
    *out = path;
    return true;
}

static bool append_region(qa_native_declaration *declaration, const qa_native_module *module,
                          const char *path, uint64_t entry, uint64_t join, bool has_frame,
                          uint64_t frame_entry, uint64_t frame_exit, qa_error *error) {
    if (entry > UINT32_MAX || join > UINT32_MAX || entry == join ||
        (has_frame &&
         (frame_entry > UINT32_MAX || frame_exit > UINT32_MAX || frame_entry == frame_exit)))
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native declaration has invalid region boundaries");
    if (!executable_rva(module, (uint32_t)entry) || !executable_rva(module, (uint32_t)join) ||
        (has_frame && (!executable_rva(module, (uint32_t)frame_entry) ||
                       !executable_rva(module, (uint32_t)frame_exit))))
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native declaration region leaves executable sections");
    if (declaration->region_count == UINT32_MAX)
        return native_fail(error, QA_ERROR_MEMORY, 0, "native declaration has too many regions");
    size_t bytes;
    if (!native_size_multiply(declaration->region_count + 1u, sizeof(*declaration->regions),
                              &bytes))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native declaration region index overflows");
    native_region_slot *regions = realloc(declaration->regions, bytes);
    if (!regions)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native declaration regions");
    declaration->regions = regions;
    native_region_slot *slot = &regions[declaration->region_count];
    memset(slot, 0, sizeof(*slot));
    slot->path = native_strdup(path, error);
    if (!slot->path)
        return false;
    slot->definition = (qa_native_declared_region){.id = (uint32_t)declaration->region_count,
                                                   .path = slot->path,
                                                   .entry_rva = (uint32_t)entry,
                                                   .join_rva = (uint32_t)join,
                                                   .has_frame = has_frame,
                                                   .frame_entry_rva = (uint32_t)frame_entry,
                                                   .frame_exit_rva = (uint32_t)frame_exit};
    ++declaration->region_count;
    return true;
}

static bool collect_regions(qa_native_declaration *declaration, const qa_native_module *module,
                            qa_json_id value, const char *path, unsigned depth, qa_error *error) {
    if (depth > 64u)
        return native_fail(error, QA_ERROR_FORMAT, depth, "native declaration nesting is too deep");
    qa_json_kind kind = qa_json_type(declaration->document, value);
    if (kind == QA_JSON_OBJECT) {
        qa_json_id entry_id = qa_json_get(declaration->document, value, "entry");
        qa_json_id join_id = qa_json_get(declaration->document, value, "join");
        if (join_id != QA_JSON_NONE) {
            uint64_t entry, join, frame_entry = 0, frame_exit = 0;
            if (entry_id == QA_JSON_NONE ||
                !qa_json_u64(declaration->document, entry_id, &entry, error) ||
                !qa_json_u64(declaration->document, join_id, &join, error))
                return native_fail(error, QA_ERROR_FORMAT, 0,
                                   "native declaration region requires entry and join");
            qa_json_id frame = qa_json_get(declaration->document, value, "frame");
            bool has_frame = frame != QA_JSON_NONE;
            if (has_frame && (qa_json_type(declaration->document, frame) != QA_JSON_OBJECT ||
                              !qa_json_u64(declaration->document,
                                           qa_json_get(declaration->document, frame, "entry"),
                                           &frame_entry, error) ||
                              !qa_json_u64(declaration->document,
                                           qa_json_get(declaration->document, frame, "exit"),
                                           &frame_exit, error)))
                return native_fail(error, QA_ERROR_FORMAT, 0,
                                   "native declaration region frame is invalid");
            if (!append_region(declaration, module, path, entry, join, has_frame, frame_entry,
                               frame_exit, error))
                return false;
        }
        size_t count = qa_json_size(declaration->document, value);
        for (size_t index = 0; index < count; ++index) {
            qa_json_id key_id = qa_json_key_at(declaration->document, value, index);
            qa_buffer key = {0};
            if (!qa_json_string(declaration->document, key_id, &key, error))
                return false;
            char *child_path = NULL;
            bool ok =
                append_path(&child_path, path, (qa_bytes){key.data, key.size}, false, error) &&
                collect_regions(declaration, module,
                                qa_json_at(declaration->document, value, index), child_path,
                                depth + 1u, error);
            free(child_path);
            qa_buffer_free(&key);
            if (!ok)
                return false;
        }
    } else if (kind == QA_JSON_ARRAY) {
        size_t count = qa_json_size(declaration->document, value);
        for (size_t index = 0; index < count; ++index) {
            char encoded[32];
            int length = snprintf(encoded, sizeof(encoded), "%zu", index);
            if (length < 0 || (size_t)length >= sizeof(encoded))
                return native_fail(error, QA_ERROR_MEMORY, index,
                                   "native declaration array index overflows");
            char *child_path = NULL;
            bool ok =
                append_path(&child_path, path, (qa_bytes){(const uint8_t *)encoded, (size_t)length},
                            true, error) &&
                collect_regions(declaration, module,
                                qa_json_at(declaration->document, value, index), child_path,
                                depth + 1u, error);
            free(child_path);
            if (!ok)
                return false;
        }
    }
    return true;
}

bool qa_native_declaration_load(qa_bytes json, const char *artifact_path,
                                const qa_native_module *module, qa_native_declaration **out,
                                qa_error *error) {
    if (!artifact_path || !module || !out || (!json.data && json.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native declaration bytes, artifact and module are required");
    if (module->info.profile != QA_NATIVE_Q2_GAME_API3 &&
        module->info.profile != QA_NATIVE_Q2_GAME_API2023)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native primary declarations apply only to Q2 game modules");
    char *selected_path = NULL;
    if (!normalize_path((qa_bytes){(const uint8_t *)artifact_path, strlen(artifact_path)},
                        &selected_path, error))
        return false;
    qa_native_declaration *declaration = calloc(1, sizeof(*declaration));
    if (!declaration || (json.size && !(declaration->json = malloc(json.size)))) {
        free(declaration);
        free(selected_path);
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native declaration");
    }
    if (json.size)
        memcpy(declaration->json, json.data, json.size);
    declaration->json_size = json.size;
    qa_bytes owned_json = {declaration->json, declaration->json_size};
    if (!qa_json_parse(owned_json, &declaration->document, error)) {
        free(selected_path);
        qa_native_declaration_destroy(declaration);
        return false;
    }
    qa_json_document *document = declaration->document;
    qa_json_id root = qa_json_root(document);
    if (qa_json_get(document, root, "runtime") != QA_JSON_NONE) {
        bool valid = callback_document(declaration, selected_path, module, error);
        if (valid) {
            declaration->primary = root;
            qa_sha256(owned_json, &declaration->digest);
            valid = collect_regions(declaration, module, root, "", 0, error);
        }
        free(selected_path);
        if (!valid) { qa_native_declaration_destroy(declaration); return false; }
        *out = declaration;
        return true;
    }
    int64_t version;
    qa_json_id modules;
    bool valid = qa_json_type(document, root) == QA_JSON_OBJECT &&
                 qa_json_i64(document, qa_json_get(document, root, "version"), &version, error) &&
                 version == 1 &&
                 (modules = qa_json_get(document, root, "modules")) != QA_JSON_NONE &&
                 qa_json_type(document, modules) == QA_JSON_ARRAY;
    if (!valid) {
        if (!error || !error->message[0])
            qa_error_set(error, QA_ERROR_FORMAT, 0,
                         "native declaration requires version 1 and a modules array");
        free(selected_path);
        qa_native_declaration_destroy(declaration);
        return false;
    }
    size_t module_count = qa_json_size(document, modules);
    char **paths = calloc(module_count ? module_count : 1u, sizeof(*paths));
    if (!paths) {
        free(selected_path);
        qa_native_declaration_destroy(declaration);
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native declaration path index");
    }
    qa_json_id selected_primary = QA_JSON_NONE;
    for (size_t index = 0; index < module_count && valid; ++index) {
        qa_json_id entry = qa_json_at(document, modules, index);
        if (qa_json_type(document, entry) != QA_JSON_OBJECT) {
            qa_error_set(error, QA_ERROR_FORMAT, index,
                         "native declaration module must be an object");
            valid = false;
            break;
        }
        qa_buffer path_text = {0}, digest = {0};
        int64_t api_version;
        qa_json_id primary = qa_json_get(document, entry, "primary");
        valid = qa_json_string(document, qa_json_get(document, entry, "artifactPath"), &path_text,
                               error) &&
                qa_json_string(document, qa_json_get(document, entry, "artifactDigest"), &digest,
                               error) &&
                qa_json_i64(document, qa_json_get(document, entry, "apiVersion"), &api_version,
                            error) &&
                normalize_path((qa_bytes){path_text.data, path_text.size}, &paths[index], error) &&
                required_primary(document, primary, error);
        for (size_t previous = 0; valid && previous < index; ++previous) {
            if (!strcmp(paths[previous], paths[index])) {
                qa_error_set(error, QA_ERROR_FORMAT, index,
                             "native declaration repeats artifact path %s", paths[index]);
                valid = false;
            }
        }
        qa_sha256_digest declared;
        if (valid && !digest_text((qa_bytes){digest.data, digest.size}, &declared, error))
            valid = false;
        if (valid && !strcmp(paths[index], selected_path)) {
            int64_t expected_api = module->info.profile == QA_NATIVE_Q2_GAME_API3 ? 3 : 2023;
            if (api_version != expected_api) {
                qa_error_set(error, QA_ERROR_FORMAT, index,
                             "native declaration API does not match its selected module");
                valid = false;
            } else if (!qa_sha256_equal(&declared, &module->info.image.digest)) {
                qa_error_set(error, QA_ERROR_FORMAT, index,
                             "native declaration digest does not match its selected module");
                valid = false;
            } else {
                selected_primary = primary;
            }
        }
        qa_buffer_free(&path_text);
        qa_buffer_free(&digest);
    }
    if (valid && selected_primary == QA_JSON_NONE) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "native declaration has no entry for %s",
                     selected_path);
        valid = false;
    }
    if (valid) {
        qa_bytes primary_source = qa_json_source(document, selected_primary);
        declaration->primary = selected_primary;
        declaration->primary_offset = (size_t)(primary_source.data - declaration->json);
        declaration->primary_size = primary_source.size;
        qa_sha256(owned_json, &declaration->digest);
        valid = collect_regions(declaration, module, selected_primary, "", 0, error);
    }
    for (size_t index = 0; index < module_count; ++index)
        free(paths[index]);
    free(paths);
    free(selected_path);
    if (!valid) {
        qa_native_declaration_destroy(declaration);
        return false;
    }
    *out = declaration;
    return true;
}

void qa_native_declaration_destroy(qa_native_declaration *declaration) {
    if (!declaration)
        return;
    qa_json_destroy(declaration->document);
    for (size_t index = 0; index < declaration->region_count; ++index)
        free(declaration->regions[index].path);
    free(declaration->regions);
    free(declaration->json);
    memset(declaration, 0, sizeof(*declaration));
    free(declaration);
}

const qa_sha256_digest *qa_native_declaration_digest(const qa_native_declaration *declaration) {
    return declaration ? &declaration->digest : NULL;
}

qa_bytes qa_native_declaration_primary(const qa_native_declaration *declaration) {
    if (!declaration || declaration->callbacks)
        return (qa_bytes){0};
    return (qa_bytes){declaration->json + declaration->primary_offset, declaration->primary_size};
}

qa_bytes qa_native_declaration_callbacks(const qa_native_declaration *declaration) {
    return declaration && declaration->callbacks
        ? (qa_bytes){declaration->json, declaration->json_size} : (qa_bytes){0};
}

size_t qa_native_declaration_region_count(const qa_native_declaration *declaration) {
    return declaration ? declaration->region_count : 0;
}

bool qa_native_declaration_region(const qa_native_declaration *declaration, size_t index,
                                  qa_native_declared_region *out, qa_error *error) {
    if (!declaration || !out || index >= declaration->region_count)
        return native_fail(error, QA_ERROR_ARGUMENT, index,
                           "native declaration region index is invalid");
    *out = declaration->regions[index].definition;
    return true;
}

bool qa_native_declaration_find_region(const qa_native_declaration *declaration, const char *path,
                                       qa_native_declared_region *out, qa_error *error) {
    if (!declaration || !path || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native declaration, region path and output are required");
    for (size_t index = 0; index < declaration->region_count; ++index) {
        if (!strcmp(declaration->regions[index].path, path)) {
            *out = declaration->regions[index].definition;
            return true;
        }
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "native declaration has no region at %s", path);
    return false;
}

static bool pointer_segment(const char **cursor, char **out, qa_error *error) {
    const char *start = *cursor;
    const char *end = strchr(start, '/');
    size_t encoded = end ? (size_t)(end - start) : strlen(start);
    char *decoded = malloc(encoded + 1u);
    if (!decoded)
        return native_fail(error, QA_ERROR_MEMORY, 0,
                           "allocating native declaration pointer segment");
    size_t used = 0;
    for (size_t index = 0; index < encoded; ++index) {
        if (start[index] != '~') {
            decoded[used++] = start[index];
            continue;
        }
        if (index + 1u >= encoded || (start[index + 1u] != '0' && start[index + 1u] != '1')) {
            free(decoded);
            return native_fail(error, QA_ERROR_ARGUMENT, index,
                               "native declaration JSON pointer escape is invalid");
        }
        decoded[used++] = start[++index] == '0' ? '~' : '/';
    }
    decoded[used] = 0;
    *cursor = end ? end + 1u : start + encoded;
    *out = decoded;
    return true;
}

static bool declaration_value(const qa_native_declaration *declaration, const char *pointer,
                              qa_json_id *out, qa_error *error) {
    if (!declaration || !pointer || !out || (*pointer && *pointer != '/'))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native declaration JSON pointer is invalid");
    qa_json_id value = declaration->primary;
    const char *cursor = *pointer ? pointer + 1u : pointer;
    while (*cursor) {
        char *segment = NULL;
        if (!pointer_segment(&cursor, &segment, error))
            return false;
        qa_json_kind kind = qa_json_type(declaration->document, value);
        if (kind == QA_JSON_OBJECT) {
            value = qa_json_get(declaration->document, value, segment);
        } else if (kind == QA_JSON_ARRAY) {
            char *end = NULL;
            unsigned long long index = strtoull(segment, &end, 10);
            bool valid = segment[0] && end && !*end;
#if SIZE_MAX < ULLONG_MAX
            valid = valid && index <= (unsigned long long)SIZE_MAX;
#endif
            if (!valid || (size_t)index >= qa_json_size(declaration->document, value))
                value = QA_JSON_NONE;
            else
                value = qa_json_at(declaration->document, value, (size_t)index);
        } else {
            value = QA_JSON_NONE;
        }
        free(segment);
        if (value == QA_JSON_NONE) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0,
                         "native declaration JSON pointer was not found: %s", pointer);
            return false;
        }
    }
    *out = value;
    return true;
}

qa_json_kind qa_native_declaration_kind(const qa_native_declaration *declaration,
                                        const char *json_pointer) {
    qa_json_id value;
    return declaration_value(declaration, json_pointer, &value, NULL)
               ? qa_json_type(declaration->document, value)
               : QA_JSON_INVALID;
}

bool qa_native_declaration_u64(const qa_native_declaration *declaration, const char *json_pointer,
                               uint64_t *out, qa_error *error) {
    qa_json_id value;
    return out && declaration_value(declaration, json_pointer, &value, error) &&
           qa_json_u64(declaration->document, value, out, error);
}

bool qa_native_declaration_number(const qa_native_declaration *declaration,
                                  const char *json_pointer, double *out, qa_error *error) {
    qa_json_id value;
    return out && declaration_value(declaration, json_pointer, &value, error) &&
           qa_json_number(declaration->document, value, out, error);
}

bool qa_native_declaration_bool(const qa_native_declaration *declaration, const char *json_pointer,
                                bool *out, qa_error *error) {
    qa_json_id value;
    return out && declaration_value(declaration, json_pointer, &value, error) &&
           qa_json_bool(declaration->document, value, out, error);
}

bool qa_native_declaration_string(const qa_native_declaration *declaration,
                                  const char *json_pointer, qa_buffer *out, qa_error *error) {
    qa_json_id value;
    return out && declaration_value(declaration, json_pointer, &value, error) &&
           qa_json_string(declaration->document, value, out, error);
}

bool qa_native_declaration_source(const qa_native_declaration *declaration,
                                  const char *json_pointer, qa_bytes *out, qa_error *error) {
    qa_json_id value;
    if (!out || !declaration_value(declaration, json_pointer, &value, error))
        return false;
    *out = qa_json_source(declaration->document, value);
    return true;
}
