#include "internal.h"
#include "qa/json.h"
#include "qa/text.h"

static void report(qa_model_diagnostic fn, void *ctx, const char *message) {
    if (fn)
        fn(ctx, 0, message);
}
static bool same_name(qa_bytes a, qa_buffer b) {
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}
static bool last_value(const qa_json_document *doc, qa_json_id object, qa_json_id value,
                       const qa_buffer *key) {
    if (memchr(key->data, 0, key->size))
        return true;
    return qa_json_get(doc, object, (const char *)key->data) == value;
}
bool qa_model_animation_scale_json(qa_model_animation *a, qa_bytes json,
                                   qa_model_diagnostic diagnostic, void *context, qa_error *error) {
    if (!a || !a->joint_count || !a->frame_count || (!json.data && json.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "animation and scale JSON are required");
        return false;
    }
    qa_json_document *doc = NULL;
    qa_error json_error = {0};
    if (!qa_json_parse(json, &doc, &json_error)) {
        if (json_error.code == QA_ERROR_MEMORY) {
            if (error)
                *error = json_error;
            return false;
        }
        report(diagnostic, context, "invalid MD5 scale JSON; using existing scales");
        return true;
    }
    qa_json_id root = qa_json_root(doc);
    if (qa_json_type(doc, root) != QA_JSON_OBJECT) {
        report(diagnostic, context, "MD5 scale root is not an object");
        qa_json_destroy(doc);
        return true;
    }
    model_reader r = {{0}, 0, error, true};
    qa_model_scale *scales = NULL;
    size_t count = 0, capacity = 0;
    bool *positions = model_alloc(&r, a->joint_count, sizeof(*positions));
    qa_buffer name = {0}, key = {0};
    if (!r.ok)
        goto fail;
    for (uint32_t i = 0; i < a->joint_count; ++i)
        positions[i] = a->joints[i].scale_positions;
    for (size_t i = 0; i < qa_json_size(doc, root); ++i) {
        qa_json_id entry = qa_json_at(doc, root, i);
        qa_buffer_free(&name);
        if (!qa_json_string(doc, qa_json_key_at(doc, root, i), &name, error))
            goto fail;
        if (!last_value(doc, root, entry, &name))
            continue;
        if (qa_json_type(doc, entry) != QA_JSON_OBJECT) {
            report(diagnostic, context, "MD5 joint scale entry is not an object");
            break;
        }
        uint32_t joint = a->joint_count;
        for (uint32_t j = 0; j < a->joint_count; ++j)
            if (same_name(a->joints[j].bone.text_name, name)) {
                joint = j;
                break;
            }
        if (joint == a->joint_count) {
            report(diagnostic, context, "MD5 scale references an unknown joint");
            continue;
        }
        for (size_t j = 0; j < qa_json_size(doc, entry); ++j) {
            qa_json_id value = qa_json_at(doc, entry, j);
            qa_buffer_free(&key);
            if (!qa_json_string(doc, qa_json_key_at(doc, entry, j), &key, error))
                goto fail;
            if (!last_value(doc, entry, value, &key))
                continue;
            if (key.size == 15 && !memcmp(key.data, "scale_positions", 15)) {
                bool enabled = false;
                if (qa_json_type(doc, value) == QA_JSON_BOOL &&
                    qa_json_bool(doc, value, &enabled, NULL) && enabled)
                    positions[joint] = true;
                continue;
            }
            double frame = 0;
            qa_error number_error = {0};
            bool parsed = qa_parse_number((qa_bytes){key.data, key.size}, &frame, &number_error);
            if (!parsed && number_error.code == QA_ERROR_MEMORY) {
                if (error)
                    *error = number_error;
                goto fail;
            }
            bool valid_key = key.size && !memchr(key.data, 0, key.size) && parsed &&
                             isfinite(frame) && frame >= 0 && frame < a->frame_count &&
                             frame == trunc(frame);
            double scale = 0;
            if (!valid_key || qa_json_type(doc, value) != QA_JSON_NUMBER ||
                !qa_json_number(doc, value, &scale, NULL) || !isfinite((float)scale)) {
                report(diagnostic, context, "invalid MD5 frame scale");
                continue;
            }
            if (count == capacity) {
                size_t grown = capacity ? capacity * 2 : 16;
                if (grown < capacity) {
                    model_fail(&r, "MD5 scale count overflows");
                    goto fail;
                }
                scales = model_grow(&r, scales, capacity, grown, sizeof(*scales));
                if (!r.ok)
                    goto fail;
                capacity = grown;
            }
            scales[count++] = (qa_model_scale){(uint32_t)frame, joint, (float)scale};
        }
    }
    {
        bool ok = qa_model_animation_scales(a, scales, count, positions, a->joint_count, error);
        qa_buffer_free(&name);
        qa_buffer_free(&key);
        free(scales);
        free(positions);
        qa_json_destroy(doc);
        return ok;
    }
fail:
    qa_buffer_free(&name);
    qa_buffer_free(&key);
    free(scales);
    free(positions);
    qa_json_destroy(doc);
    return false;
}
