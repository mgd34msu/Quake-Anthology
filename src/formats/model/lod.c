/* Q3 LOD registration order and aliasing semantics.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <stdio.h>
void qa_model_lods_free(qa_model_lod_set *set) {
    if (!set)
        return;
    for (unsigned i = 0; i < 3; ++i) {
        free(set->paths[i]);
        qa_model_free(&set->models[i]);
    }
    memset(set, 0, sizeof(*set));
}
const qa_model *qa_model_at_lod(const qa_model_lod_set *set, uint32_t slot) {
    if (!set || slot >= 3)
        return NULL;
    for (unsigned depth = 0; depth < 3; ++depth) {
        if (set->states[slot] == QA_MODEL_LOD_LOADED)
            return &set->models[slot];
        if (set->states[slot] != QA_MODEL_LOD_ALIAS || set->aliases[slot] >= 3)
            return NULL;
        slot = set->aliases[slot];
    }
    return NULL;
}
bool qa_model_md3_lods(const char *path, qa_model_read_file read_file, void *context,
                       qa_model_lod_set *out, qa_error *error) {
    if (!path || !read_file || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "LOD path, reader and output are required");
        return false;
    }
    model_reader allocator = {{0}, 0, error, true};
    qa_model_lod_set set = {0};
    size_t length = strlen(path);
    const char *dot = strrchr(path, '.');
    size_t stem = dot ? (size_t)(dot - path) : length;
    if (length == SIZE_MAX || stem > SIZE_MAX - 7) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "LOD path exceeds capacity");
        return false;
    }
    set.paths[0] = model_alloc(&allocator, length + 1, 1);
    set.paths[1] = model_alloc(&allocator, stem + 7, 1);
    set.paths[2] = model_alloc(&allocator, stem + 7, 1);
    if (!allocator.ok)
        goto fail;
    memcpy(set.paths[0], path, length + 1);
    for (unsigned i = 1; i < 3; ++i) {
        memcpy(set.paths[i], path, stem);
        memcpy(set.paths[i] + stem, i == 1 ? "_1.md3" : "_2.md3", 7);
    }
    for (int slot = 2; slot >= 0; --slot) {
        qa_buffer bytes = {0};
        qa_error local = {0};
        if (!read_file(context, set.paths[slot], &bytes, &local)) {
            qa_buffer_free(&bytes);
            if (local.code == QA_ERROR_NOT_FOUND)
                continue;
            if (error)
                *error = local;
            goto fail;
        }
        set.load_order[set.load_count++] = (uint32_t)slot;
        if (bytes.size >= 4 && memcmp(bytes.data, "IDP3", 4)) {
            qa_buffer_free(&bytes);
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unknown MD3 LOD dispatch identifier");
            goto fail;
        }
        bool loaded = qa_model_load_owned(&bytes, &set.models[slot], &local);
        qa_buffer_free(&bytes);
        if (loaded) {
            set.states[slot] = QA_MODEL_LOD_LOADED;
            ++set.lod_count;
            size_t model_size = qa_load_u32le(set.models[slot].source.data + 104);
            if (model_size > SIZE_MAX - set.byte_length) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "LOD byte total overflows");
                goto fail;
            }
            set.byte_length += model_size;
            continue;
        }
        if (local.code == QA_ERROR_MEMORY || slot == 0 || !set.lod_count) {
            if (error)
                *error = local;
            goto fail;
        }
        set.states[slot] = QA_MODEL_LOD_INVALID;
        for (int lower = slot - 1; lower >= 0; --lower) {
            set.states[lower] = QA_MODEL_LOD_ALIAS;
            set.aliases[lower] = (uint32_t)lower + 1;
            ++set.lod_count;
        }
        break;
    }
    if (!set.lod_count) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "no model LOD exists");
        goto fail;
    }
    *out = set;
    return true;
fail:
    qa_model_lods_free(&set);
    return false;
}
