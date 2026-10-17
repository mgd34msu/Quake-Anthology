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
    bool ok = owners && count && count <= QA_SAVE_OWNER_LIMIT &&
              metadata.purpose == purpose;
    qa_save_record *records = ok ? calloc(count, sizeof(*records)) : NULL;
    qa_save_image *image = NULL;
    if (!ok) persistence_fail(error, QA_ERROR_FORMAT, "Capture producer returned incomplete owner inventory");
    else if (!records) ok = persistence_fail(error, QA_ERROR_MEMORY, "Allocating captured owner records");
    /* State codecs register the installed resources they actually reference.
     * Write that one unit manifest only after all those references exist. */
    for (unsigned pass = 0; ok && pass < 2; ++pass)
        for (size_t i = 0; ok && i < count; ++i) {
            if ((owners[i].kind == QA_SAVE_RESOURCES) != (pass != 0)) continue;
            records[i].owner = owners[i];
            qa_buffer payload = {0};
            ok = persistence_owner_valid(owners + i, error) && ops->capture(context, owners + i, &payload, error);
            records[i].payload = (qa_bytes){payload.data, payload.size};
        }
    if (ok) ok = persistence_image_create_owned(&metadata, records, count, &image, error);
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

struct qa_save_restore_operation {
    void *context, *candidate;
    qa_save_restore_ops ops;
    const qa_save_image *image;
    size_t stage, record;
    bool created, finished, published, failed;
};

static const qa_save_owner_kind restore_order[] = {
    QA_SAVE_STRINGS, QA_SAVE_RESOURCES, QA_SAVE_CONFIGURATION, QA_SAVE_SESSION,
    QA_SAVE_ACTORS, QA_SAVE_PROVIDER, QA_SAVE_ROSTER, QA_SAVE_MODES, QA_SAVE_EQUIPMENT,
    QA_SAVE_WORLD, QA_SAVE_COMBAT, QA_SAVE_INVENTORY, QA_SAVE_PICKUPS, QA_SAVE_TARGETS,
    QA_SAVE_PROGRESSION, QA_SAVE_CONTROLS, QA_SAVE_CVARS, QA_SAVE_EVENTS,
    QA_SAVE_NAVIGATION, QA_SAVE_BOTS, QA_SAVE_APPLICATION
};

bool qa_save_restore_begin(void *context, const qa_save_restore_ops *ops,
    const qa_save_image *image, qa_save_restore_operation **out, qa_error *error)
{
    if (!ops || !ops->create || !ops->restore || !ops->finish || !ops->publish ||
        !ops->discard || !image || !out || *out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save restore producer");
    qa_save_restore_operation *operation = calloc(1, sizeof(*operation));
    if (!operation)
        return persistence_fail(error, QA_ERROR_MEMORY, "Retaining save restore operation");
    operation->context = context;
    operation->ops = *ops;
    operation->image = image;
    *out = operation;
    return true;
}

bool qa_save_restore_advance(qa_save_restore_operation *operation, uint64_t quantum,
    bool *complete, qa_error *error)
{
    if (!operation || !complete || operation->failed)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save restore operation is unavailable");
    *complete = operation->published;
    if (*complete) return true;
    bool okay = true;
    if (!operation->created) {
        okay = operation->ops.create(operation->context, operation->image,
            &operation->candidate, error);
        if (okay && !operation->candidate)
            okay = persistence_fail(error, QA_ERROR_FORMAT, "Restore producer returned no candidate");
        if (!okay) goto failed;
        operation->created = true;
        if (quantum) return true;
    }
    while (operation->stage < sizeof(restore_order) / sizeof(*restore_order)) {
        qa_save_owner_kind kind = restore_order[operation->stage];
        while (operation->record < operation->image->count) {
            const qa_save_record *record = operation->image->records + operation->record;
            if (record->owner.kind != kind || (kind != QA_SAVE_PROVIDER &&
                !qa_save_shared_state_kind(kind, operation->image->metadata.purpose))) {
                ++operation->record;
                continue;
            }
            bool restored = false;
            okay = operation->ops.restore(operation->context, operation->candidate,
                record, quantum, &restored, error);
            if (!okay) goto failed;
            if (restored) ++operation->record;
            if (quantum || !restored) return true;
        }
        ++operation->stage;
        operation->record = 0;
    }
    if (!operation->finished) {
        okay = operation->ops.finish(operation->context, operation->candidate,
            operation->image, error);
        if (!okay) goto failed;
        operation->finished = true;
        if (quantum) return true;
    }
    okay = operation->ops.publish(operation->context, operation->candidate, error);
    if (!okay) goto failed;
    operation->published = true;
    *complete = true;
    return true;
failed:
    operation->failed = true;
    return false;
}

void qa_save_restore_destroy(qa_save_restore_operation **slot)
{
    qa_save_restore_operation *operation = slot ? *slot : NULL;
    if (!operation) return;
    if (operation->candidate && !operation->published)
        operation->ops.discard(operation->context, operation->candidate);
    free(operation);
    *slot = NULL;
}

bool qa_save_restore(void *context, const qa_save_restore_ops *ops,
    const qa_save_image *image, qa_error *error)
{
    qa_save_restore_operation *operation = NULL;
    bool okay = qa_save_restore_begin(context, ops, image, &operation, error), complete = false;
    while (okay && !complete)
        okay = qa_save_restore_advance(operation, 0, &complete, error);
    qa_save_restore_destroy(&operation);
    return okay;
}

bool qa_save_slot_name(const char *name, qa_error *error)
{
    if (!name || !*name || name[0] == '/')
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save requires a contained file or directory name");
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
        if (!value) return true;
        component = cursor + 1;
    }
}

bool qa_save_write(qa_fs_root *root, const char *name, const qa_save_image *image,
                   uint64_t nonce, qa_error *error)
{
    if (!root || !qa_save_slot_name(name, error)) return false;
    qa_bytes bytes = {0};
    if (!qa_save_image_encode(image, &bytes, error)) return false;
    return qa_fs_root_replace(root, name, bytes, nonce, error);
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
