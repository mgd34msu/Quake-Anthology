#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_assets_save.h"

bool bot_save_weights_fields(qa_source_save_io *io, const qa_bot_weights *source,
                             qa_bot_weights **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_weights_view view = reading ? (qa_bot_weights_view){0} : *qa_bot_weights_read(source);
    qa_bot_weight_definition *definitions = NULL;
    qa_bot_weight_node *nodes = NULL;
    qa_bot_weight_value *values = NULL;
    bool ok = bot_save_text(io, &view.path) && view.path &&
        qa_source_save_count(io, &view.weight_count, 128) &&
        qa_source_save_count(io, &view.node_count, UINT32_MAX - 1u) &&
        qa_source_save_i32(io, &view.maximum_inventory_index);
    if (ok && reading) {
        if (view.node_count > SIZE_MAX / sizeof(*nodes) ||
            view.node_count > SIZE_MAX / sizeof(*values))
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Bot fuzzy weight extent exceeds memory");
        size_t remaining = io->input.size - io->offset;
        if (ok && (view.weight_count > remaining / 17 ||
            view.node_count > (remaining - view.weight_count * 17) / 29))
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot fuzzy weight tables");
        if (ok && view.weight_count)
            definitions = calloc(view.weight_count, sizeof(*definitions));
        if (ok && view.node_count) {
            nodes = calloc(view.node_count, sizeof(*nodes));
            values = calloc(view.node_count, sizeof(*values));
        }
        if (ok && ((view.weight_count && !definitions) || (view.node_count && (!nodes || !values))))
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot fuzzy weight fields");
        view.weights = definitions; view.nodes = nodes; view.values = values;
    }
    for (size_t i = 0; ok && i < view.weight_count; ++i) {
        qa_bot_weight_definition definition = reading ? (qa_bot_weight_definition){0} : view.weights[i];
        ok = bot_save_text(io, &definition.name);
        if (reading)
            definitions[i] = definition;
        if (ok)
            ok = definition.name && qa_source_save_u32(io, &definition.root) &&
                qa_source_save_u32(io, &definition.end);
        if (reading)
            definitions[i] = definition;
    }
    for (size_t i = 0; ok && i < view.node_count; ++i) {
        qa_bot_weight_node node = reading ? (qa_bot_weight_node){0} : view.nodes[i];
        qa_bot_weight_value value = reading ? (qa_bot_weight_value){0} : view.values[i];
        ok = qa_source_save_i32(io, &node.inventory) && qa_source_save_i32(io, &node.threshold) &&
            qa_source_save_u32(io, &node.child) && qa_source_save_u32(io, &node.next) &&
            qa_source_save_bool(io, &node.balanced) && qa_source_save_f32(io, &value.weight) &&
            qa_source_save_f32(io, &value.minimum) && qa_source_save_f32(io, &value.maximum);
        if (reading) {
            nodes[i] = node;
            values[i] = value;
        }
    }
    qa_bot_weights *candidate = NULL;
    if (ok && reading) {
        ok = qa_bot_weights_restore(&view, &candidate, io->error);
        if (!ok)
            io->failed = true;
        if (ok && qa_bot_weights_read(candidate)->maximum_inventory_index != view.maximum_inventory_index)
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Bot fuzzy inventory extent differs from its topology");
    }
    if (reading) {
        if (definitions)
            for (size_t i = 0; i < view.weight_count; ++i)
                free((void *)definitions[i].name);
        free(definitions); free(nodes); free(values); free((void *)view.path);
        if (ok)
            *out = candidate;
        else
            qa_bot_weights_release(candidate);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot fuzzy weight fields");
    return ok;
}

bool bot_save_character_fields(qa_source_save_io *io, const qa_bot_character *source,
                               qa_bot_character **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_character_view view = reading ? (qa_bot_character_view){0} : *qa_bot_character_read(source);
    bool ok = bot_save_text(io, &view.path) && view.path && qa_source_save_f32(io, &view.skill);
    for (size_t i = 0; ok && i < QA_BOT_CHARACTERISTICS; ++i) {
        qa_bot_character_value *value = view.values + i;
        uint32_t kind = (uint32_t)value->kind;
        ok = qa_source_save_u32(io, &kind);
        value->kind = (qa_bot_character_value_kind)kind;
        if (!ok)
            break;
        switch (value->kind) {
        case QA_BOT_CHARACTER_UNSET: break;
        case QA_BOT_CHARACTER_INTEGER: ok = qa_source_save_i32(io, &value->data.integer); break;
        case QA_BOT_CHARACTER_FLOAT: ok = qa_source_save_f32(io, &value->data.number); break;
        case QA_BOT_CHARACTER_STRING: ok = bot_save_text(io, &value->data.string) && value->data.string; break;
        default: ok = bot_save_fail(io, QA_ERROR_FORMAT, "Unknown bot character value kind"); break;
        }
    }
    qa_bot_character *candidate = NULL;
    if (ok && reading) {
        ok = qa_bot_character_restore(&view, &candidate, io->error);
        if (!ok)
            io->failed = true;
    }
    if (reading) {
        for (size_t i = 0; i < QA_BOT_CHARACTERISTICS; ++i)
            if (view.values[i].kind == QA_BOT_CHARACTER_STRING)
                free((void *)view.values[i].data.string);
        free((void *)view.path);
        if (ok)
            *out = candidate;
        else
            qa_bot_character_release(candidate);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot character fields");
    return ok;
}

static const uint8_t weight_magic[8] = {'Q', 'A', 'B', 'W', 'E', 'I', 'G', 0};
static const uint8_t character_magic[8] = {'Q', 'A', 'B', 'C', 'H', 'A', 'R', 0};

bool qa_bot_weights_save_capture(const qa_bot_weights *source, qa_buffer *out, qa_error *error)
{
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot weight capture owner/output");
        return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, weight_magic) &&
        bot_save_weights_fields(&io, source, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_weights_save_restore(qa_bytes bytes, qa_bot_weights **out, qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot weight restore requires an empty output");
        return false;
    }
    qa_source_save_io io = {0}; qa_bot_weights *candidate = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, weight_magic) &&
        bot_save_weights_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_weights_release(candidate); return false; }
    *out = candidate;
    return true;
}

bool qa_bot_character_save_capture(const qa_bot_character *source, qa_buffer *out, qa_error *error)
{
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot character capture owner/output");
        return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, character_magic) &&
        bot_save_character_fields(&io, source, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_character_save_restore(qa_bytes bytes, qa_bot_character **out, qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot character restore requires an empty output");
        return false;
    }
    qa_source_save_io io = {0}; qa_bot_character *candidate = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, character_magic) &&
        bot_save_character_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { qa_bot_character_release(candidate); return false; }
    *out = candidate;
    return true;
}
