#include "internal.h"
#include "qa/source_save.h"

#define SAVE_HEADER 72u
#define SAVE_RECORD_HEADER 20u

static bool text_valid(const char *text, bool empty)
{
    if (!text) return false;
    size_t length = 0;
    while (text[length]) {
        unsigned char value = (unsigned char)text[length];
        if (value < 32 || value == 127 || ++length > QA_SAVE_NAME_LIMIT) return false;
    }
    return empty || length != 0;
}

bool persistence_owner_valid(const qa_save_owner *owner, qa_error *error)
{
    if (!owner || owner->kind < QA_SAVE_STRINGS || owner->kind > QA_SAVE_PROVIDER ||
        !text_valid(owner->instance, owner->kind != QA_SAVE_PROVIDER) ||
        !text_valid(owner->schema, false) || !text_valid(owner->backend, true) ||
        (owner->kind != QA_SAVE_PROVIDER && owner->instance[0]))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid save owner identity");
    return true;
}

static int provider_compare(const void *left, const void *right)
{
    const qa_save_record *const *a = left, *const *b = right;
    return strcmp((*a)->owner.instance, (*b)->owner.instance);
}

bool persistence_owner_set(const qa_save_record *records, size_t count, qa_error *error)
{
    if (!records || count < QA_SAVE_PROVIDER - 1u || count > QA_SAVE_OWNER_LIMIT)
        return persistence_fail(error, QA_ERROR_FORMAT, "Incomplete or excessive save owner inventory");
    const qa_save_record **providers = malloc(count * sizeof(*providers));
    if (!providers)
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating saved provider identity validation");
    uint64_t seen = 0;
    size_t provider_count = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_save_record *record = records + i;
        ok = persistence_owner_valid(&record->owner, error);
        if (!ok) break;
        if (!record->payload.data || !record->payload.size) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Save owner has no explicit state value");
            break;
        }
        if (record->owner.kind != QA_SAVE_PROVIDER) {
            uint64_t bit = UINT64_C(1) << record->owner.kind;
            if (seen & bit) {
                ok = persistence_fail(error, QA_ERROR_FORMAT, "Duplicate shared save owner");
                break;
            }
            seen |= bit;
        } else providers[provider_count++] = record;
    }
    uint64_t required = (UINT64_C(1) << QA_SAVE_PROVIDER) - 2;
    if (ok && seen != required)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Save omits a required shared owner");
    if (ok && provider_count > 1) {
        qsort(providers, provider_count, sizeof(*providers), provider_compare);
        for (size_t i = 1; i < provider_count; ++i)
            if (!strcmp(providers[i - 1]->owner.instance, providers[i]->owner.instance)) {
                ok = persistence_fail(error, QA_ERROR_FORMAT, "Duplicate saved provider instance");
                break;
            }
    }
    free(providers);
    return ok;
}

static char *text_copy(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static void image_free(qa_save_image *image)
{
    if (!image) return;
    for (size_t i = 0; image->records && i < image->count; ++i) {
        free((void *)image->records[i].owner.instance);
        free((void *)image->records[i].owner.schema);
        free((void *)image->records[i].owner.backend);
        free((void *)image->records[i].payload.data);
    }
    free(image->records);
    free(image);
}

bool qa_save_image_destroy_checked(qa_save_image **pointer, qa_error *error)
{
    if (!pointer) return persistence_fail(error, QA_ERROR_ARGUMENT, "Save image retirement needs its owning pointer");
    qa_save_image *image = *pointer;
    if (!image) return true;
    image->retiring = true;
    if (image->native_resources &&
        !image->native_release(&image->native_resources, error)) return false;
    if (image->native_resources)
        return persistence_fail(error, QA_ERROR_FORMAT, "Native save graph release retained an owner after reporting success");
    image_free(image); *pointer = NULL;
    return true;
}

bool qa_save_image_native_attach(qa_save_image *image, qa_native_resource_inventory *inventory,
    qa_save_native_release_fn release, qa_error *error)
{
    if (!image || !inventory || !release || image->retiring || image->native_resources)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save image attachment requires its actual unowned native graph");
    image->native_resources = inventory; image->native_release = release;
    return true;
}

const qa_native_resource_inventory *qa_save_image_native_read(const qa_save_image *image)
{ return image && !image->retiring ? image->native_resources : NULL; }

static bool image_create(const qa_save_metadata *metadata, const qa_save_record *records,
                         size_t count, bool adopt, qa_save_image **out, qa_error *error)
{
    if (!metadata || !out || (unsigned)metadata->purpose > QA_SAVE_DEMO_KEYFRAME ||
        !memchr(metadata->map, 0, sizeof(metadata->map)) ||
        !memchr(metadata->game, 0, sizeof(metadata->game)))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save image metadata/output");
    if (!persistence_owner_set(records, count, error)) return false;
    qa_save_image *image = calloc(1, sizeof(*image));
    if (!image) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating save image");
    image->records = calloc(count, sizeof(*image->records));
    if (!image->records) {
        image_free(image);
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating save owner records");
    }
    image->metadata = *metadata;
    image->count = count;
    for (size_t i = 0; i < count; ++i) {
        qa_save_record *copy = image->records + i;
        copy->owner = records[i].owner;
        copy->owner.instance = text_copy(records[i].owner.instance);
        copy->owner.schema = text_copy(records[i].owner.schema);
        copy->owner.backend = text_copy(records[i].owner.backend);
        uint8_t *payload = adopt ? NULL : malloc(records[i].payload.size);
        copy->payload = (qa_bytes){payload, records[i].payload.size};
        if (!copy->owner.instance || !copy->owner.schema || !copy->owner.backend || (!adopt && !payload)) {
            image_free(image);
            return persistence_fail(error, QA_ERROR_MEMORY, "Copying save owner record");
        }
        if (!adopt) memcpy(payload, records[i].payload.data, records[i].payload.size);
    }
    if (adopt) for (size_t i = 0; i < count; ++i) image->records[i].payload = records[i].payload;
    *out = image;
    return true;
}

bool qa_save_image_create(const qa_save_metadata *metadata, const qa_save_record *records,
                          size_t count, qa_save_image **out, qa_error *error)
{ return image_create(metadata, records, count, false, out, error); }

bool persistence_image_create_owned(const qa_save_metadata *metadata, qa_save_record *records,
                                    size_t count, qa_save_image **out, qa_error *error)
{
    if (!image_create(metadata, records, count, true, out, error)) return false;
    for (size_t i = 0; i < count; ++i) records[i].payload = (qa_bytes){0};
    return true;
}

const qa_save_metadata *qa_save_image_metadata(const qa_save_image *image)
{ return image ? &image->metadata : NULL; }
size_t qa_save_image_record_count(const qa_save_image *image)
{ return image ? image->count : 0; }
const qa_save_record *qa_save_image_record_at(const qa_save_image *image, size_t index)
{ return image && index < image->count ? image->records + index : NULL; }
const qa_save_record *qa_save_image_find(const qa_save_image *image, qa_save_owner_kind kind,
                                        const char *instance)
{
    if (!image || !instance) return NULL;
    for (size_t i = 0; i < image->count; ++i)
        if (image->records[i].owner.kind == kind &&
            !strcmp(image->records[i].owner.instance, instance)) return image->records + i;
    return NULL;
}

static bool size_add(size_t *size, size_t add, qa_error *error)
{
    if (add > SIZE_MAX - *size)
        return persistence_fail(error, QA_ERROR_MEMORY, "Save image size overflow");
    *size += add;
    return true;
}

static bool codec_fail(qa_source_save_io *io, qa_status code, const char *message)
{
    io->failed = true;
    return persistence_fail(io->error, code, message);
}

static bool record_text(qa_source_save_io *io, uint16_t length, const char **value)
{
    if (length > QA_SAVE_NAME_LIMIT)
        return codec_fail(io, QA_ERROR_FORMAT, "Save owner name exceeds record");
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)*value, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return codec_fail(io, QA_ERROR_FORMAT, "Save owner name exceeds record");
    char *text = malloc((size_t)length + 1);
    if (!text) return codec_fail(io, QA_ERROR_MEMORY, "Allocating save owner name");
    if (!qa_source_save_bytes(io, text, length)) { free(text); return false; }
    text[length] = 0;
    if (memchr(text, 0, length)) {
        free(text);
        return codec_fail(io, QA_ERROR_FORMAT, "NUL in save owner name");
    }
    *value = text;
    return true;
}

static bool record_fields(qa_source_save_io *io, qa_save_record *record)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t kind = reading ? 0 : (uint32_t)record->owner.kind;
    uint16_t instance_size = reading ? 0 : (uint16_t)strlen(record->owner.instance);
    uint16_t schema_size = reading ? 0 : (uint16_t)strlen(record->owner.schema);
    uint16_t backend_size = reading ? 0 : (uint16_t)strlen(record->owner.backend);
    uint16_t flags = 0;
    uint64_t payload_size = reading ? 0 : record->payload.size;
    if (!qa_source_save_u32(io, &kind) ||
        !qa_source_save_u16(io, &instance_size) ||
        !qa_source_save_u16(io, &schema_size) ||
        !qa_source_save_u16(io, &backend_size) ||
        !qa_source_save_u16(io, &flags) ||
        !qa_source_save_u64(io, &payload_size)) return false;
    if (flags || !payload_size || payload_size > SIZE_MAX)
        return codec_fail(io, QA_ERROR_FORMAT, "Invalid saved owner payload extent");
    record->owner.kind = (qa_save_owner_kind)kind;
    if (!record_text(io, instance_size, &record->owner.instance) ||
        !record_text(io, schema_size, &record->owner.schema) ||
        !record_text(io, backend_size, &record->owner.backend)) return false;
    if (reading) {
        if (io->offset > io->input.size || payload_size > io->input.size - io->offset)
            return codec_fail(io, QA_ERROR_FORMAT, "Invalid saved owner payload extent");
        uint8_t *payload = malloc((size_t)payload_size);
        if (!payload) return codec_fail(io, QA_ERROR_MEMORY, "Allocating saved owner payload");
        record->payload = (qa_bytes){payload, (size_t)payload_size};
    }
    return qa_source_save_bytes(io, (void *)record->payload.data, (size_t)payload_size);
}

static bool summary_text(qa_source_save_io *io, char *text, size_t capacity)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = reading ? 0 : strlen(text);
    if (!qa_source_save_count(io, &length, capacity - 1) ||
        !qa_source_save_bytes(io, text, length)) return false;
    if (memchr(text, 0, length)) return codec_fail(io, QA_ERROR_FORMAT, "Save summary contains embedded NUL");
    text[length] = 0; return true;
}
static bool header_fields(qa_source_save_io *io, qa_save_image *image, uint64_t extent)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[8] = {'Q','A','S','V','\r','\n',26,'\n'};
    uint32_t reserved = 0, header_size = reading ? 0 :
        SAVE_HEADER + (uint32_t)strlen(image->metadata.map) + (uint32_t)strlen(image->metadata.game);
    uint64_t saved_extent = extent;
    uint32_t purpose = reading ? 0 : (uint32_t)image->metadata.purpose;
    uint32_t count = reading ? 0 : (uint32_t)image->count;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        !qa_source_save_u32(io, &reserved) ||
        !qa_source_save_u32(io, &header_size) ||
        !qa_source_save_u64(io, &saved_extent) ||
        !qa_source_save_u32(io, &purpose) ||
        !qa_source_save_u32(io, &count) ||
        !qa_source_save_u64(io, &image->metadata.elapsed_ns) ||
        !qa_source_save_u64(io, &image->metadata.configuration_generation) ||
        !qa_source_save_u64(io, &image->metadata.world_generation) ||
        !summary_text(io, image->metadata.map, sizeof(image->metadata.map)) ||
        !summary_text(io, image->metadata.game, sizeof(image->metadata.game))) return false;
    if (memcmp(magic, "QASV\r\n\032\n", sizeof(magic)))
        return codec_fail(io, QA_ERROR_FORMAT, "Invalid shared save signature");
    if (header_size != io->offset || saved_extent != extent || io->offset > extent)
        return codec_fail(io, QA_ERROR_FORMAT, "Invalid save image extent");
    if (purpose > QA_SAVE_DEMO_KEYFRAME || count < QA_SAVE_PROVIDER - 1u ||
        count > QA_SAVE_OWNER_LIMIT ||
        count > (extent - io->offset) / SAVE_RECORD_HEADER)
        return codec_fail(io, QA_ERROR_FORMAT, "Invalid save record inventory");
    image->metadata.purpose = (qa_save_purpose)purpose;
    image->count = count;
    return true;
}

bool qa_save_image_metadata_read(qa_fs_file *file, const qa_fs_identity *identity,
    qa_save_metadata *out, bool *shared, qa_error *error)
{
    if (!file || !identity || !out || !shared)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save summary requires its admitted file and outputs");
    qa_save_image image = {0};
    uint8_t prefix[SAVE_HEADER + sizeof(image.metadata.map) + sizeof(image.metadata.game) - 2];
    size_t received = 0;
    if (!qa_fs_file_read_prefix(file, identity, prefix, sizeof(prefix), &received, error)) return false;
    if (received < 2 || prefix[0] != 'Q' || prefix[1] != 'A') { *shared = false; return true; }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, (qa_bytes){prefix, received}, error) &&
        header_fields(&io, &image, qa_fs_identity_size(identity));
    qa_source_save_dispose(&io);
    if (!ok) return false;
    *out = image.metadata; *shared = true;
    return true;
}

static bool image_fields(qa_source_save_io *io, qa_save_image *image, uint64_t extent)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!header_fields(io, image, extent)) return false;
    if (reading) {
        image->records = calloc(image->count, sizeof(*image->records));
        if (!image->records)
            return codec_fail(io, QA_ERROR_MEMORY, "Allocating decoded save records");
    }
    for (size_t i = 0; i < image->count; ++i) {
        qa_save_record copy = image->records[i];
        if (!record_fields(io, reading ? image->records + i : &copy)) return false;
    }
    return true;
}

bool qa_save_image_encode(const qa_save_image *image, qa_buffer *out, qa_error *error)
{
    if (!image || image->retiring || !out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid or retiring save encode owner");
    size_t size = SAVE_HEADER + strlen(image->metadata.map) + strlen(image->metadata.game);
    for (size_t i = 0; i < image->count; ++i) {
        const qa_save_record *record = image->records + i;
        if (!size_add(&size, SAVE_RECORD_HEADER, error) ||
            !size_add(&size, strlen(record->owner.instance), error) ||
            !size_add(&size, strlen(record->owner.schema), error) ||
            !size_add(&size, strlen(record->owner.backend), error) ||
            !size_add(&size, record->payload.size, error)) return false;
    }
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    io.output.data = malloc(size); io.capacity = size;
    if (!io.output.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating encoded save image");
    qa_save_image view = *image;
    qa_buffer buffer = {0};
    bool ok = image_fields(&io, &view, size);
    if (ok && io.offset != size)
        ok = codec_fail(&io, QA_ERROR_FORMAT, "Save image size disagrees with codec");
    if (ok) ok = qa_source_save_finish(&io, &buffer);
    qa_source_save_dispose(&io);
    if (!ok) return false;
    *out = buffer;
    return true;
}

bool qa_save_image_decode(qa_bytes bytes, qa_save_image **out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < SAVE_HEADER)
        return persistence_fail(error, QA_ERROR_FORMAT, "Truncated save image");
    qa_source_save_io io = {0};
    if (!qa_source_save_reader(&io, NULL, bytes, error)) return false;
    qa_save_image *image = calloc(1, sizeof(*image));
    if (!image) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded save image");
    bool ok = image_fields(&io, image, bytes.size) && qa_source_save_finish(&io, NULL) &&
        persistence_owner_set(image->records, image->count, error);
    qa_source_save_dispose(&io);
    if (!ok) { image_free(image); return false; }
    *out = image;
    return true;
}
