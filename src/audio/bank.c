#include "bank_private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

qa_audio_asset *qa_audio_asset_retain(qa_audio_asset *asset) {
    if (asset == NULL)
        return NULL;
    unsigned references = atomic_load_explicit(&asset->references, memory_order_relaxed);
    do {
        if (references == 0 || references == UINT_MAX)
            return NULL;
    } while (!atomic_compare_exchange_weak_explicit(&asset->references, &references, references + 1,
                                                    memory_order_relaxed, memory_order_relaxed));
    return asset;
}

void qa_audio_asset_release(qa_audio_asset *asset) {
    /* Final release uses the VFS owner thread or its external synchronization.
     * Atomic asset references do not make resource-pool teardown concurrent. */
    if (asset == NULL ||
        atomic_fetch_sub_explicit(&asset->references, 1, memory_order_acq_rel) != 1)
        return;
    qa_audio_sample_release(asset->sample);
    qa_resource_release(asset->resource);
    qa_vfs_destroy(asset->files);
    free(asset);
}

qa_audio_sample *qa_audio_asset_sample(const qa_audio_asset *asset) {
    return asset != NULL ? asset->sample : NULL;
}

qa_resource *qa_audio_asset_resource(const qa_audio_asset *asset) {
    return asset != NULL ? asset->resource : NULL;
}

const qa_vfs *qa_audio_asset_files(const qa_audio_asset *asset) {
    return asset != NULL ? asset->files : NULL;
}

qa_mount_id qa_audio_asset_mount(const qa_audio_asset *asset) {
    return asset != NULL ? asset->mount : 0;
}

const char *qa_audio_asset_name(const qa_audio_asset *asset) {
    return asset != NULL ? asset->name : NULL;
}

qa_audio_family qa_audio_asset_family(const qa_audio_asset *asset) {
    return asset != NULL ? asset->family : QA_AUDIO_Q3;
}
const qa_vfs *qa_audio_bank_files(const qa_audio_bank *bank) {
    return bank ? bank->view : NULL;
}

bool qa_audio_bank_create(qa_vfs *view, qa_audio_bank **out, qa_error *error) {
    if (view == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid audio bank content view or destination");
        return false;
    }
    qa_audio_bank *bank = calloc(1, sizeof(*bank));
    if (bank == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating audio bank");
        return false;
    }
    bank->view = view;
    bank->registration = 1;
    bank->lookup_generation = qa_vfs_lookup_generation(view);
    *out = bank;
    return true;
}

static uint64_t bank_name_hash(const char *prefix, const char *path, qa_audio_family family) {
    uint64_t hash = UINT64_C(14695981039346656037) ^ (uint64_t)family;
    while (*prefix) { hash ^= (uint8_t)*prefix++; hash *= UINT64_C(1099511628211); }
    while (*path) { hash ^= (uint8_t)*path++; hash *= UINT64_C(1099511628211); }
    return hash;
}
static void bank_name_insert(qa_audio_bank *bank, size_t index) {
    const qa_audio_asset *asset = bank->entries[index].asset;
    size_t slot = (size_t)bank_name_hash("", asset->name, asset->family) & (bank->name_capacity - 1);
    while (bank->names[slot]) slot = (slot + 1) & (bank->name_capacity - 1);
    bank->names[slot] = index + 1;
}
static void bank_name_rebuild(qa_audio_bank *bank) {
    if (!bank->name_capacity) return;
    memset(bank->names, 0, bank->name_capacity * sizeof(*bank->names));
    for (size_t i = 0; i < bank->count; ++i) bank_name_insert(bank, i);
}
static bool bank_name_reserve(qa_audio_bank *bank, size_t count, qa_error *error) {
    if (!count || count <= bank->name_capacity / 2) return true;
    size_t capacity = bank->name_capacity ? bank->name_capacity : 32;
    while (count > capacity / 2) {
        if (capacity > SIZE_MAX / sizeof(*bank->names) / 2) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio name index overflows storage"); return false;
        }
        capacity *= 2;
    }
    size_t *names = calloc(capacity, sizeof(*names));
    if (!names) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating audio name index"); return false; }
    free(bank->names); bank->names = names; bank->name_capacity = capacity;
    bank_name_rebuild(bank); return true;
}
static bank_entry *bank_name_find(qa_audio_bank *bank, const char *name, qa_audio_family family) {
    if (!bank->count) return NULL;
    const char *path = name[0] == '#' ? name + 1 : name;
    const char *prefix = name[0] != '#' && strncmp(name, "sound/", 6) ? "sound/" : "";
    size_t prefix_length = strlen(prefix);
    size_t slot = (size_t)bank_name_hash(prefix, path, family) & (bank->name_capacity - 1);
    while (bank->names[slot]) {
        bank_entry *entry = bank->entries + bank->names[slot] - 1;
        const qa_audio_asset *asset = entry->asset;
        if (asset->family == family && !strncmp(asset->name, prefix, prefix_length) &&
            !strcmp(asset->name + prefix_length, path)) return entry;
        slot = (slot + 1) & (bank->name_capacity - 1);
    }
    return NULL;
}

static void bank_clear_entries(qa_audio_bank *bank) {
    for (size_t i = 0; i < bank->count; ++i)
        qa_audio_asset_release(bank->entries[i].asset);
    bank->count = 0;
    bank_name_rebuild(bank);
}

void qa_audio_bank_sync(qa_audio_bank *bank) {
    if (!bank) return;
    uint64_t generation = qa_vfs_lookup_generation(bank->view);
    if (bank->lookup_generation == generation) return;
    bank_clear_entries(bank);
    bank->lookup_generation = generation;
}

void qa_audio_bank_clear(qa_audio_bank *bank) {
    if (!bank) return;
    bank_clear_entries(bank);
    bank->registration = 1;
    bank->lookup_generation = qa_vfs_lookup_generation(bank->view);
}

void qa_audio_bank_destroy(qa_audio_bank *bank) {
    if (bank == NULL)
        return;
    qa_audio_bank_clear(bank);
    free(bank->entries);
    free(bank->names);
    free(bank);
}

void qa_audio_bank_begin(qa_audio_bank *bank) {
    if (bank == NULL)
        return;
    if (bank->registration == UINT64_MAX) {
        for (size_t i = 0; i < bank->count; ++i)
            bank->entries[i].touched = 0;
        bank->registration = 1;
    } else {
        ++bank->registration;
    }
}

void qa_audio_bank_end(qa_audio_bank *bank) {
    if (bank == NULL)
        return;
    size_t retained = 0;
    for (size_t i = 0; i < bank->count; ++i) {
        if (bank->entries[i].touched == bank->registration)
            bank->entries[retained++] = bank->entries[i];
        else
            qa_audio_asset_release(bank->entries[i].asset);
    }
    bank->count = retained;
    bank_name_rebuild(bank);
}

static bank_entry *bank_find(const qa_audio_bank *bank, uint64_t resource_id,
                             qa_audio_family family) {
    if (bank == NULL)
        return NULL;
    for (size_t i = 0; i < bank->count; ++i) {
        qa_audio_asset *asset = bank->entries[i].asset;
        if (asset->resource_id == resource_id && asset->family == family)
            return &bank->entries[i];
    }
    return NULL;
}

qa_audio_asset *qa_audio_bank_get(const qa_audio_bank *bank, uint64_t resource_id,
                                  qa_audio_family family) {
    const bank_entry *entry = bank_find(bank, resource_id, family);
    return entry != NULL ? entry->asset : NULL;
}

static bool bank_reserve(qa_audio_bank *bank, qa_error *error) {
    if (bank->count < bank->capacity)
        return true;
    size_t maximum = SIZE_MAX / sizeof(*bank->entries);
    if (bank->capacity == maximum) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "audio bank entry count overflows storage");
        return false;
    }
    size_t capacity = bank->capacity == 0            ? 16
                      : bank->capacity > maximum / 2 ? maximum
                                                     : bank->capacity * 2;
    bank_entry *entries = realloc(bank->entries, capacity * sizeof(*entries));
    if (entries == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating audio bank entries");
        return false;
    }
    bank->entries = entries;
    bank->capacity = capacity;
    return true;
}

static bool bank_acquire(qa_audio_bank *bank, const char *path, qa_resource **out,
                         qa_mount_id *mount, qa_error *error) {
    qa_error acquired_error = {0};
    if (qa_vfs_acquire(bank->view, path, out, mount, &acquired_error))
        return true;
    if (acquired_error.code == QA_ERROR_NOT_FOUND)
        return true;
    if (error != NULL)
        *error = acquired_error;
    return false;
}

bool qa_audio_bank_register(qa_audio_bank *bank, const char *name, qa_audio_family family,
                            qa_audio_asset **out, qa_error *error) {
    if (bank == NULL || name == NULL || out == NULL ||
        (family != QA_AUDIO_Q1 && family != QA_AUDIO_Q2 && family != QA_AUDIO_Q3)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid sound registration");
        return false;
    }
    qa_audio_bank_sync(bank);
    if (!bank_name_reserve(bank, bank->count, error)) return false;
    bank_entry *registered = bank_name_find(bank, name, family);
    if (registered) {
        qa_audio_asset *asset = qa_audio_asset_retain(registered->asset);
        if (!asset) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Sound asset reference count overflow"); return false; }
        registered->touched = bank->registration; *out = asset; return true;
    }
    const char *path = name;
    char *prefixed = NULL;
    if (name[0] == '#') {
        path = name + 1;
    } else if (strncmp(name, "sound/", 6) != 0) {
        size_t length = strlen(name);
        if (length > SIZE_MAX - 7) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "sound path length overflows storage");
            return false;
        }
        prefixed = malloc(length + 7);
        if (prefixed == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating sound path");
            return false;
        }
        memcpy(prefixed, "sound/", 6);
        memcpy(prefixed + 6, name, length + 1);
        path = prefixed;
    }
    qa_resource *resource = NULL;
    qa_mount_id mount = 0;
    if (!bank_acquire(bank, path, &resource, &mount, error)) {
        free(prefixed);
        return false;
    }
    if (resource == NULL) {
        free(prefixed);
        *out = NULL;
        return true;
    }
    uint64_t resource_id = qa_resource_id(resource);
    bank_entry *prior = bank_find(bank, resource_id, family);
    if (prior != NULL && prior->asset->mount == mount) {
        qa_audio_asset *asset = qa_audio_asset_retain(prior->asset);
        qa_resource_release(resource);
        free(prefixed);
        if (asset == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "sound asset reference count overflow");
            return false;
        }
        prior->touched = bank->registration;
        *out = asset;
        return true;
    }
    size_t length = strlen(path);
    if (length > SIZE_MAX - sizeof(qa_audio_asset) - 1) {
        qa_resource_release(resource);
        free(prefixed);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "sound asset name overflows storage");
        return false;
    }
    qa_audio_asset *asset = malloc(sizeof(*asset) + length + 1);
    if (asset == NULL) {
        qa_resource_release(resource);
        free(prefixed);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating sound asset");
        return false;
    }
    atomic_init(&asset->references, 1);
    asset->sample = NULL;
    asset->resource = resource;
    asset->files = NULL;
    if (!qa_vfs_retain(bank->view, error)) {
        qa_audio_asset_release(asset);
        free(prefixed);
        return false;
    }
    asset->files = bank->view;
    asset->resource_id = resource_id;
    asset->mount = mount;
    asset->family = family;
    memcpy(asset->name, path, length + 1);
    free(prefixed);
    qa_bytes bytes = qa_resource_bytes(resource);
    qa_audio_wav_policy policy = QA_WAV_FORMAT;
    if (bytes.size != 0 && bytes.data[0] == 'R')
        policy = family == QA_AUDIO_Q3 ? QA_WAV_Q3 : QA_WAV_QUAKE;
    asset->policy = policy;
    qa_audio_sample *shared = NULL;
    for (size_t i = 0; i < bank->count; ++i) {
        qa_audio_asset *cached = bank->entries[i].asset;
        if (cached->resource_id == resource_id && cached->policy == policy) {
            shared = cached->sample;
            break;
        }
    }
    if (shared != NULL) {
        asset->sample = qa_audio_sample_retain(shared);
        if (asset->sample == NULL) {
            qa_audio_asset_release(asset);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "sound PCM reference count overflow");
            return false;
        }
    } else if (!qa_audio_decode(bytes, policy, &asset->sample, error)) {
        qa_audio_asset_release(asset);
        return false;
    }
    if (prior == NULL && (!bank_reserve(bank, error) || !bank_name_reserve(bank, bank->count + 1, error))) {
        qa_audio_asset_release(asset);
        return false;
    }
    qa_audio_asset *owned = qa_audio_asset_retain(asset);
    if (owned == NULL) {
        qa_audio_asset_release(asset);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "sound asset reference count overflow");
        return false;
    }
    if (prior != NULL) {
        qa_audio_asset *previous = prior->asset;
        *prior = (bank_entry){asset, bank->registration};
        qa_audio_asset_release(previous);
        bank_name_rebuild(bank);
    } else {
        bank->entries[bank->count] = (bank_entry){asset, bank->registration};
        bank_name_insert(bank, bank->count++);
    }
    *out = owned;
    return true;
}

bool qa_audio_bank_sexed(qa_audio_bank *bank, const char *base, const char *model,
                         qa_audio_asset **out, qa_error *error) {
    if (bank == NULL || base == NULL || model == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid player-model sound registration");
        return false;
    }
    if (base[0] != '*')
        return qa_audio_bank_register(bank, base, QA_AUDIO_Q2, out, error);
    size_t model_length = strcspn(model, "/");
    if (model_length == 0) {
        model = "male";
        model_length = 4;
    }
    const char *name = base + 1;
    size_t name_length = strlen(name);
    if (model_length > SIZE_MAX - 11) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "player-model path length overflows storage");
        return false;
    }
    size_t fixed = 11 + model_length;
    if (fixed < 13)
        fixed = 13;
    if (name_length > SIZE_MAX - fixed) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "player sound path length overflows storage");
        return false;
    }
    char *path = malloc(fixed + name_length);
    if (path == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating player sound path");
        return false;
    }
    memcpy(path, "#players/", 9);
    memcpy(path + 9, model, model_length);
    path[9 + model_length] = '/';
    memcpy(path + 10 + model_length, name, name_length + 1);
    qa_audio_asset *asset = NULL;
    bool result = qa_audio_bank_register(bank, path, QA_AUDIO_Q2, &asset, error);
    if (result && asset == NULL) {
        memcpy(path, "player/male/", 12);
        memcpy(path + 12, name, name_length + 1);
        result = qa_audio_bank_register(bank, path, QA_AUDIO_Q2, &asset, error);
    }
    free(path);
    if (result)
        *out = asset;
    return result;
}

bool qa_audio_bank_music(qa_audio_bank *bank, const char *path, qa_vfs_accept_mount accept,
                         void *context, qa_audio_stream **out, qa_error *error) {
    if (bank == NULL || path == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid music resource request");
        return false;
    }
    qa_resource *resource = NULL;
    qa_mount_id mount = 0;
    if (!bank_acquire(bank, path, &resource, &mount, error))
        return false;
    if (resource == NULL) {
        *out = NULL;
        return true;
    }
    if (accept != NULL && !accept(mount, context)) {
        qa_resource_release(resource);
        *out = NULL;
        return true;
    }
    qa_audio_stream *stream;
    if (!qa_audio_stream_open_resource(resource, QA_WAV_FORMAT, &stream, error)) {
        qa_resource_release(resource);
        return false;
    }
    *out = stream;
    return true;
}

static bool music_extension(const char *path, size_t length) {
    if (length < 4 || path[length - 4] != '.')
        return false;
    unsigned char extension[3];
    for (size_t i = 0; i < 3; ++i) {
        unsigned char byte = (unsigned char)path[length - 3 + i];
        extension[i] = byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte + ('a' - 'A')) : byte;
    }
    return !memcmp(extension, "wav", 3) || !memcmp(extension, "ogg", 3);
}

bool qa_audio_bank_music_cue(qa_audio_bank *bank, const char *name, qa_audio_family family,
                             qa_vfs_accept_mount accept, void *context,
                             qa_audio_stream **out, qa_error *error) {
    if (!bank || !name || !*name || !out ||
        (family != QA_AUDIO_Q1 && family != QA_AUDIO_Q2 && family != QA_AUDIO_Q3)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid authored music cue");
        return false;
    }
    size_t length = strlen(name);
    if (length > SIZE_MAX - 11) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "music cue path length overflows storage");
        return false;
    }
    char *path = malloc(length + 11);
    if (!path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating authored music path");
        return false;
    }
    for (size_t i = 0; i < length; ++i)
        path[6 + i] = name[i] == '\\' ? '/' : name[i];
    path[6 + length] = 0;
    if (!strncmp(path + 6, "music/", 6))
        memmove(path, path + 6, length + 1);
    else {
        memcpy(path, "music/", 6);
        length += 6;
    }
    bool explicit = music_extension(path, length);
    const char *extensions[2] = {family == QA_AUDIO_Q3 ? ".wav" : ".ogg",
                                 family == QA_AUDIO_Q3 ? ".ogg" : ".wav"};
    bool ok = true;
    qa_audio_stream *stream = NULL;
    for (size_t i = 0; i < (explicit ? 1u : 2u); ++i) {
        if (!explicit)
            memcpy(path + length, extensions[i], 5);
        if (!qa_audio_bank_music(bank, path, accept, context, &stream, error)) {
            ok = false;
            break;
        }
        if (stream)
            break;
    }
    free(path);
    if (ok)
        *out = stream;
    return ok;
}
