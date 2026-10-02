#include "internal.h"

#define SAVE_HEADER 88u
#define SAVE_RECORD_HEADER 56u
#define SAVE_DIGEST 32u

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
        !owner->schema_version || !text_valid(owner->instance, owner->kind != QA_SAVE_PROVIDER) ||
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
    for (size_t i = 0; i < image->count; ++i) {
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

bool qa_save_image_create(const qa_save_metadata *metadata, const qa_save_record *records,
                          size_t count, qa_save_image **out, qa_error *error)
{
    if (!metadata || !out || (unsigned)metadata->purpose > QA_SAVE_DEMO_KEYFRAME)
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
        uint8_t *payload = malloc(records[i].payload.size);
        copy->payload = (qa_bytes){payload, records[i].payload.size};
        if (!copy->owner.instance || !copy->owner.schema || !copy->owner.backend || !payload) {
            image_free(image);
            return persistence_fail(error, QA_ERROR_MEMORY, "Copying save owner record");
        }
        memcpy(payload, records[i].payload.data, records[i].payload.size);
    }
    *out = image;
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

bool qa_save_image_encode(const qa_save_image *image, qa_buffer *out, qa_error *error)
{
    if (!image || image->retiring || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid or retiring save encode owner");
    size_t size = SAVE_HEADER + SAVE_DIGEST;
    for (size_t i = 0; i < image->count; ++i) {
        const qa_save_record *record = image->records + i;
        if (!size_add(&size, SAVE_RECORD_HEADER, error) ||
            !size_add(&size, strlen(record->owner.instance), error) ||
            !size_add(&size, strlen(record->owner.schema), error) ||
            !size_add(&size, strlen(record->owner.backend), error) ||
            !size_add(&size, record->payload.size, error)) return false;
    }
    qa_buffer buffer = {malloc(size), size};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating encoded save image");
    qa_net_writer writer;
    qa_net_writer_init(&writer, buffer.data, size - SAVE_DIGEST, error);
    qa_net_write_data(&writer, "QASV\r\n\032\n", 8);
    qa_net_write_u32(&writer, QA_SAVE_VERSION);
    qa_net_write_u32(&writer, SAVE_HEADER);
    qa_net_write_u64(&writer, size);
    qa_net_write_u32(&writer, (uint32_t)image->metadata.purpose);
    qa_net_write_u32(&writer, (uint32_t)image->count);
    qa_net_write_u64(&writer, image->metadata.elapsed_ns);
    qa_net_write_u64(&writer, image->metadata.configuration_generation);
    qa_net_write_u64(&writer, image->metadata.world_generation);
    qa_net_write_data(&writer, image->metadata.composition.bytes, 32);
    for (size_t i = 0; i < image->count; ++i) {
        const qa_save_record *record = image->records + i;
        qa_net_write_u32(&writer, record->owner.kind);
        qa_net_write_u32(&writer, record->owner.schema_version);
        qa_net_write_u16(&writer, (uint16_t)strlen(record->owner.instance));
        qa_net_write_u16(&writer, (uint16_t)strlen(record->owner.schema));
        qa_net_write_u16(&writer, (uint16_t)strlen(record->owner.backend));
        qa_net_write_u16(&writer, 0);
        qa_net_write_u64(&writer, record->payload.size);
        qa_net_write_data(&writer, record->owner.content.bytes, 32);
        qa_net_write_data(&writer, record->owner.instance, strlen(record->owner.instance));
        qa_net_write_data(&writer, record->owner.schema, strlen(record->owner.schema));
        qa_net_write_data(&writer, record->owner.backend, strlen(record->owner.backend));
        qa_net_write_data(&writer, record->payload.data, record->payload.size);
    }
    if (writer.failed || qa_net_writer_size(&writer) != size - SAVE_DIGEST) {
        qa_buffer_free(&buffer);
        return persistence_fail(error, QA_ERROR_FORMAT, "Save image size disagrees with codec");
    }
    qa_sha256_digest digest;
    qa_sha256((qa_bytes){buffer.data, size - SAVE_DIGEST}, &digest);
    memcpy(buffer.data + size - SAVE_DIGEST, digest.bytes, 32);
    *out = buffer;
    return true;
}

static bool read_text(qa_net_reader *reader, uint16_t size, char **out)
{
    if (size > QA_SAVE_NAME_LIMIT || size > qa_net_reader_remaining(reader))
        return qa_net_reader_fail(reader, "Save owner name exceeds record");
    char *text = malloc((size_t)size + 1);
    if (!text) {
        qa_error_set(reader->error, QA_ERROR_MEMORY, reader->bit / 8, "Allocating save owner name");
        reader->failed = true;
        return false;
    }
    if (!qa_net_read_data(reader, text, size)) { free(text); return false; }
    text[size] = 0;
    if (memchr(text, 0, size)) { free(text); return qa_net_reader_fail(reader, "NUL in save owner name"); }
    *out = text;
    return true;
}

bool qa_save_image_decode(qa_bytes bytes, qa_save_image **out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < SAVE_HEADER + SAVE_DIGEST)
        return persistence_fail(error, QA_ERROR_FORMAT, "Truncated save image");
    if (memcmp(bytes.data, "QASV\r\n\032\n", 8))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid shared save signature");
    if (qa_load_u32le(bytes.data + 8) != QA_SAVE_VERSION)
        return persistence_fail(error, QA_ERROR_UNSUPPORTED, "Save version requires an explicit migration codec");
    if (qa_load_u32le(bytes.data + 12) != SAVE_HEADER || qa_load_u64le(bytes.data + 16) != bytes.size)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid save image extent");
    qa_sha256_digest actual, saved;
    qa_sha256((qa_bytes){bytes.data, bytes.size - SAVE_DIGEST}, &actual);
    memcpy(saved.bytes, bytes.data + bytes.size - SAVE_DIGEST, 32);
    if (!qa_sha256_equal(&actual, &saved))
        return persistence_fail(error, QA_ERROR_FORMAT, "Save image digest mismatch");
    qa_net_reader reader;
    qa_net_reader_init(&reader, (qa_bytes){bytes.data, bytes.size - SAVE_DIGEST}, error);
    reader.bit = 24u * 8u;
    qa_save_metadata metadata = {0};
    metadata.purpose = (qa_save_purpose)qa_net_read_u32(&reader);
    uint32_t count = qa_net_read_u32(&reader);
    metadata.elapsed_ns = qa_net_read_u64(&reader);
    metadata.configuration_generation = qa_net_read_u64(&reader);
    metadata.world_generation = qa_net_read_u64(&reader);
    qa_net_read_data(&reader, metadata.composition.bytes, 32);
    if (reader.failed || (unsigned)metadata.purpose > QA_SAVE_DEMO_KEYFRAME ||
        count < QA_SAVE_PROVIDER - 1u || count > QA_SAVE_OWNER_LIMIT ||
        count > qa_net_reader_remaining(&reader) / SAVE_RECORD_HEADER)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid save record inventory");
    qa_save_image *image = calloc(1, sizeof(*image));
    if (!image) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded save image");
    image->records = calloc(count, sizeof(*image->records));
    if (!image->records) {
        image_free(image);
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded save records");
    }
    image->metadata = metadata;
    image->count = count;
    for (size_t i = 0; i < count && !reader.failed; ++i) {
        qa_save_record *record = image->records + i;
        record->owner.kind = (qa_save_owner_kind)qa_net_read_u32(&reader);
        record->owner.schema_version = qa_net_read_u32(&reader);
        uint16_t instance_size = qa_net_read_u16(&reader);
        uint16_t schema_size = qa_net_read_u16(&reader);
        uint16_t backend_size = qa_net_read_u16(&reader);
        uint16_t flags = qa_net_read_u16(&reader);
        uint64_t payload_size = qa_net_read_u64(&reader);
        qa_net_read_data(&reader, record->owner.content.bytes, 32);
        char *instance = NULL, *schema = NULL, *backend = NULL;
        bool names = !reader.failed && !flags &&
            read_text(&reader, instance_size, &instance) &&
            read_text(&reader, schema_size, &schema) &&
            read_text(&reader, backend_size, &backend);
        record->owner.instance = instance;
        record->owner.schema = schema;
        record->owner.backend = backend;
        if (!names || !payload_size || payload_size > qa_net_reader_remaining(&reader)) {
            if (!reader.failed) qa_net_reader_fail(&reader, "Invalid saved owner payload extent");
            break;
        }
        uint8_t *payload = malloc((size_t)payload_size);
        if (!payload) {
            qa_error_set(error, QA_ERROR_MEMORY, reader.bit / 8, "Allocating saved owner payload");
            reader.failed = true;
            break;
        }
        record->payload = (qa_bytes){payload, (size_t)payload_size};
        qa_net_read_data(&reader, payload, (size_t)payload_size);
    }
    if (!qa_net_reader_finish(&reader) || !persistence_owner_set(image->records, count, error)) {
        image_free(image);
        return false;
    }
    *out = image;
    return true;
}
