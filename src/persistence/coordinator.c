#include "internal.h"

bool qa_save_capture(void *context, const qa_save_capture_ops *ops, qa_save_purpose purpose,
                      qa_save_image **out, qa_error *error)
{
    if (!ops || !ops->begin || !ops->capture || !ops->validate || !ops->end || !out || *out ||
        (unsigned)purpose > QA_SAVE_DEMO_KEYFRAME)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save capture producer");
    qa_save_metadata metadata = {0};
    const qa_save_owner *owners = NULL;
    size_t count = 0;
    if (!ops->begin(context, purpose, &metadata, &owners, &count, error)) return false;
    bool ok = owners && count >= QA_SAVE_PROVIDER - 1u && count <= QA_SAVE_OWNER_LIMIT &&
              metadata.purpose == purpose;
    qa_save_record *records = ok ? calloc(count, sizeof(*records)) : NULL;
    qa_save_image *image = NULL;
    if (!ok) persistence_fail(error, QA_ERROR_FORMAT, "Capture producer returned incomplete owner inventory");
    else if (!records) ok = persistence_fail(error, QA_ERROR_MEMORY, "Allocating captured owner records");
    for (size_t i = 0; ok && i < count; ++i) {
        records[i].owner = owners[i];
        qa_buffer payload = {0};
        ok = persistence_owner_valid(owners + i, error) && ops->capture(context, owners + i, &payload, error);
        records[i].payload = (qa_bytes){payload.data, payload.size};
    }
    if (ok) ok = qa_save_image_create(&metadata, records, count, &image, error);
    if (ok && ops->attach) ok = ops->attach(context, image, error);
    if (ok) ok = ops->validate(context, image, error);
    if (records) for (size_t i = 0; i < count; ++i) free((void *)records[i].payload.data);
    free(records);
    ops->end(context);
    if (!ok) {
        qa_error cleanup = {0};
        if (!qa_save_image_destroy_checked(&image, &cleanup)) {
            *out = image;
            if (error && error->code == QA_OK) *error = cleanup;
        }
        return false;
    }
    *out = image;
    return true;
}

bool qa_save_restore(void *context, const qa_save_restore_ops *ops,
                      const qa_save_image *image, qa_error *error)
{
    if (!ops || !ops->create || !ops->restore || !ops->finish || !ops->publish ||
        !ops->discard || !image)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save restore producer");
    void *candidate = NULL;
    if (!ops->create(context, image, &candidate, error)) {
        if (candidate) ops->discard(context, candidate);
        return false;
    }
    if (!candidate) return persistence_fail(error, QA_ERROR_FORMAT, "Restore producer returned no candidate");
    static const qa_save_owner_kind order[] = {
        QA_SAVE_STRINGS, QA_SAVE_RESOURCES, QA_SAVE_CONFIGURATION, QA_SAVE_SESSION,
        QA_SAVE_ACTORS, QA_SAVE_PROVIDER, QA_SAVE_ROSTER, QA_SAVE_MODES, QA_SAVE_EQUIPMENT,
        QA_SAVE_WORLD, QA_SAVE_COMBAT, QA_SAVE_INVENTORY,
        QA_SAVE_PICKUPS, QA_SAVE_TARGETS, QA_SAVE_CAMPAIGN,
        QA_SAVE_PROGRESSION,
        QA_SAVE_CONTROLS, QA_SAVE_CVARS, QA_SAVE_CONNECTIONS, QA_SAVE_COMMANDS, QA_SAVE_EVENTS,
        QA_SAVE_NAVIGATION, QA_SAVE_BOTS, QA_SAVE_PREDICTION,
        QA_SAVE_PRESENTATION, QA_SAVE_AUDIO, QA_SAVE_INPUT, QA_SAVE_MEDIA,
        QA_SAVE_APPLICATION
    };
    bool ok = true;
    for (size_t stage = 0; ok && stage < sizeof(order) / sizeof(*order); ++stage)
        for (size_t i = 0; ok && i < image->count; ++i)
            if (image->records[i].owner.kind == order[stage])
                ok = ops->restore(context, candidate, image->records + i, error);
    if (ok) ok = ops->finish(context, candidate, image, error);
    if (ok) ok = ops->publish(context, candidate, error);
    if (!ok) ops->discard(context, candidate);
    return ok;
}

bool qa_save_slot_name(const char *name, qa_error *error)
{
    if (!name || !*name || name[0] == '/' || strlen(name) < 5)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save requires a contained .sav name");
    const char *component = name;
    for (const char *cursor = name;; ++cursor) {
        unsigned char value = (unsigned char)*cursor;
        if (value == '\\' || value == ':' || (value && value < 32))
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save slot path");
        if (value != '/' && value != 0) continue;
        size_t length = (size_t)(cursor - component);
        if (!length || (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.'))
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save slot path component");
        size_t stem = length;
        if (length >= 4 && component[length - 4] == '.' &&
            (component[length - 3] == 's' || component[length - 3] == 'S') &&
            (component[length - 2] == 'a' || component[length - 2] == 'A') &&
            (component[length - 1] == 'v' || component[length - 1] == 'V')) stem -= 4;
        static const char reserved[] = "current";
        bool current = stem == sizeof(reserved) - 1;
        for (size_t i = 0; current && i < stem; ++i) {
            unsigned char letter = (unsigned char)component[i];
            if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
            current = letter == (unsigned char)reserved[i];
        }
        if (current) return persistence_fail(error, QA_ERROR_ARGUMENT, "The current slot is reserved for transitions");
        if (!value) {
            if (stem == length) return persistence_fail(error, QA_ERROR_ARGUMENT, "Save slot must end in .sav");
            return true;
        }
        component = cursor + 1;
    }
}

bool qa_save_write(qa_fs_root *root, const char *name, const qa_save_image *image,
                   uint64_t nonce, qa_error *error)
{
    if (!root || !qa_save_slot_name(name, error)) return false;
    qa_buffer bytes = {0};
    if (!qa_save_image_encode(image, &bytes, error)) return false;
    bool ok = qa_fs_root_replace(root, name, (qa_bytes){bytes.data, bytes.size}, nonce, error);
    qa_buffer_free(&bytes);
    return ok;
}

bool qa_save_read(qa_fs_root *root, const char *name, qa_save_image **out, qa_error *error)
{
    if (!root || !out || !qa_save_slot_name(name, error)) return false;
    qa_fs_file *file = NULL;
    qa_fs_identity identity;
    if (!qa_fs_root_file_open(root, name, &file, &identity, error)) return false;
    qa_buffer bytes = {0};
    bool ok = qa_fs_file_read_snapshot(file, &identity, &bytes, error);
    qa_fs_file_close(file);
    if (ok) ok = qa_save_image_decode((qa_bytes){bytes.data, bytes.size}, out, error);
    qa_buffer_free(&bytes);
    return ok;
}
