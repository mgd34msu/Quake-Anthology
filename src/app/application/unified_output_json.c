#include "unified_output_json.h"

#include <stdlib.h>
#include <string.h>

bool application_unified_json_append(application_unified_json *out, qa_bytes bytes, qa_error *error)
{
    const size_t limit = 32u * 1024u * 1024u;
    if (!out || (bytes.size && !bytes.data) || out->bytes.size > limit || bytes.size > limit - out->bytes.size) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified Source output exceeds its document extent");
        return false;
    }
    size_t size = out->bytes.size + bytes.size;
    if (size > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 1024;
        while (capacity < size) capacity = capacity > limit / 2 ? limit : capacity * 2;
        uint8_t *data = realloc(out->bytes.data, capacity);
        if (!data) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Unified Source output");
            return false;
        }
        out->bytes.data = data;
        out->capacity = capacity;
    }
    if (bytes.size) memcpy(out->bytes.data + out->bytes.size, bytes.data, bytes.size);
    out->bytes.size = size;
    return true;
}

bool application_unified_json_text(application_unified_json *out, const char *text, qa_error *error)
{
    return text && application_unified_json_append(out, (qa_bytes){(const uint8_t *)text, strlen(text)}, error);
}

bool application_unified_json_string(application_unified_json *out, const char *text, qa_error *error)
{
    if (!text) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified Source string has no owner");
        return false;
    }
    qa_buffer quoted = {0};
    bool ok = qa_json_quote((qa_bytes){(const uint8_t *)text, strlen(text)}, &quoted, error) &&
        application_unified_json_append(out, (qa_bytes){quoted.data, quoted.size}, error);
    qa_buffer_free(&quoted);
    return ok;
}

bool application_unified_json_percent_encoded(application_unified_json *out,
    const char *text, qa_error *error)
{
    if (!text) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified Source identity has no text owner");
        return false;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("-_.!~*'()", *p) != NULL;
        char escaped[3] = {'%', hex[*p >> 4], hex[*p & 15]};
        if (!application_unified_json_append(out, plain ? (qa_bytes){p, 1} :
            (qa_bytes){(const uint8_t *)escaped, 3}, error)) return false;
    }
    return true;
}

bool application_unified_json_number(application_unified_json *out, double value, qa_error *error)
{
    qa_buffer number = {0};
    bool ok = qa_unified_checkpoint_number(value, &number, error) &&
        application_unified_json_append(out, (qa_bytes){number.data, number.size}, error);
    qa_buffer_free(&number);
    return ok;
}

bool application_unified_json_natural(application_unified_json *out, uint64_t value, qa_error *error)
{
    if (value > QA_UNIFIED_SAFE_INTEGER) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified Source identity exceeds the wire integer domain");
        return false;
    }
    return application_unified_json_number(out, (double)value, error);
}

bool application_unified_json_actor(application_unified_json *out, qa_actor_id actor, qa_error *error)
{
    return application_unified_json_text(out, "{\"slot\":", error) &&
        application_unified_json_natural(out, actor.slot, error) &&
        application_unified_json_text(out, ",\"generation\":", error) &&
        application_unified_json_natural(out, actor.generation, error) &&
        application_unified_json_text(out, "}", error);
}

bool application_unified_json_vector(application_unified_json *out, qa_vec3 value, qa_error *error)
{
    return application_unified_json_text(out, "{\"x\":", error) &&
        application_unified_json_number(out, value.x, error) &&
        application_unified_json_text(out, ",\"y\":", error) &&
        application_unified_json_number(out, value.y, error) &&
        application_unified_json_text(out, ",\"z\":", error) &&
        application_unified_json_number(out, value.z, error) &&
        application_unified_json_text(out, "}", error);
}

bool application_unified_json_bounds(application_unified_json *out, qa_bounds value, qa_error *error)
{
    return application_unified_json_text(out, "{\"min\":", error) &&
        application_unified_json_vector(out, value.mins, error) &&
        application_unified_json_text(out, ",\"max\":", error) &&
        application_unified_json_vector(out, value.maxs, error) &&
        application_unified_json_text(out, "}", error);
}

bool application_unified_json_body(application_unified_json *out, const qa_body_state *body, qa_error *error)
{
    return application_unified_json_text(out, "{\"origin\":", error) &&
        application_unified_json_vector(out, body->origin, error) &&
        application_unified_json_text(out, ",\"angles\":", error) &&
        application_unified_json_vector(out, body->angles, error) &&
        application_unified_json_text(out, ",\"velocity\":", error) &&
        application_unified_json_vector(out, body->velocity, error) &&
        application_unified_json_text(out, ",\"bounds\":", error) &&
        application_unified_json_bounds(out, body->bounds, error) &&
        application_unified_json_text(out, ",\"ground\":", error) &&
        (body->ground.registry ? application_unified_json_actor(out, body->ground, error) :
            application_unified_json_text(out, "null", error)) &&
        application_unified_json_text(out, "}", error);
}

bool application_unified_json_document(application_unified_json *out,
    const qa_unified_document *document, qa_error *error)
{
    if (!document) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unified Source child document is absent");
        return false;
    }
    return application_unified_json_append(out,
        qa_json_source(qa_unified_document_json(document), qa_unified_document_root(document)), error);
}

void application_unified_json_dispose(application_unified_json *out)
{
    if (!out) return;
    qa_buffer_free(&out->bytes);
    out->capacity = 0;
}
