#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_chat_system_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'C', 'S', 'Y', 'S', 0};

bool qa_bot_chat_system_state_index(const qa_bot_chat_system *system, const qa_bot_chat *state, size_t *out)
{
    if (!system || !state || !out) return false;
    size_t index = 0;
    for (const qa_bot_chat *current = system->states; current; current = current->next, ++index)
        if (current == state) { *out = index; return true; }
    return false;
}
void qa_bot_chat_restored_states_free(qa_bot_chat_restored_states *states)
{
    if (!states) return;
    free(states->states); *states = (qa_bot_chat_restored_states){0};
}
static bool asset_field(qa_source_save_io *io, const qa_bot_chat_asset_save_refs *refs,
                        qa_bot_chat_asset **asset, qa_bot_chat_asset_kind kind)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = !reading && *asset != NULL;
    uint64_t key = 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (reading) *asset = NULL; return true; }
    bool ok = reading || ((*asset)->view.kind == kind && refs->encode(refs->context, *asset, &key, io->error));
    if (ok) ok = qa_source_save_u64(io, &key);
    if (ok && reading) {
        qa_bot_chat_asset *borrowed = NULL;
        ok = refs->decode(refs->context, key, &borrowed, io->error) && borrowed && borrowed->view.kind == kind;
        if (ok) { qa_bot_chat_asset_retain(borrowed); *asset = borrowed; }
    }
    if (!ok && !io->failed) {
        if (io->error && io->error->code != QA_OK) io->failed = true;
        else return bot_save_fail(io, QA_ERROR_FORMAT, "Unqualified shared bot chat asset");
    }
    return ok;
}
static bool message_fields(qa_source_save_io *io, qa_bot_console_message *message)
{
    bool ok = qa_source_save_u32(io, &message->handle) && message->handle <= 8192 &&
        qa_source_save_f32(io, &message->time) && isfinite(message->time) &&
        qa_source_save_i32(io, &message->type) && qa_source_save_bytes(io, message->text, sizeof(message->text)) &&
        memchr(message->text, 0, sizeof(message->text));
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot console message");
    return ok;
}
static bool state_fields(qa_source_save_io *io, qa_bot_chat *state, const qa_bot_chat_asset_save_refs *refs)
{
    bool ok = asset_field(io, refs, &state->initial, QA_BOT_CHAT_INITIAL) &&
        qa_source_save_i32(io, &state->client) && qa_source_save_u32(io, &state->gender) && state->gender <= 2 &&
        qa_source_save_u32(io, &state->last_handle) && state->last_handle <= 8192 &&
        qa_source_save_u32(io, &state->first_console) && qa_source_save_u32(io, &state->last_console) &&
        qa_source_save_count(io, &state->console_count, UINT32_MAX - 1u) &&
        qa_source_save_u64(io, &state->initial_revision) && state->initial_revision &&
        qa_source_save_bytes(io, state->name, sizeof(state->name)) && memchr(state->name, 0, sizeof(state->name)) &&
        qa_source_save_bytes(io, state->message, sizeof(state->message)) && memchr(state->message, 0, sizeof(state->message));
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid private bot chat state");
    return ok;
}
static bool topology_valid(const qa_bot_chat_system *system, qa_error *error)
{
    size_t capacity = system->console_capacity;
    if (capacity >= UINT32_MAX || system->console_count > capacity || (capacity && !system->console) ||
        !system->revision || system->options.console_capacity >= UINT32_MAX)
        goto invalid;
    uint8_t *seen = capacity ? calloc(capacity, 1) : NULL;
    if (capacity && !seen) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Validating bot chat pool ownership"); return false; }
    bool ok = true;
    size_t occupied = 0;
    const qa_bot_chat *previous_state = NULL;
    for (const qa_bot_chat *state = system->states; ok && state; state = state->next) {
        if (state->system != system || state->retired || state->references != 1 || state->previous != previous_state) {
            ok = false; break;
        }
        size_t count = 0;
        uint32_t previous = QA_BOT_NO_INDEX;
        for (uint32_t cell = state->first_console; ok && cell != QA_BOT_NO_INDEX;) {
            if (cell >= capacity || seen[cell] || system->console[cell].previous != previous ||
                !system->console[cell].message.handle) { ok = false; break; }
            seen[cell] = 1; previous = cell; cell = system->console[cell].next; ++count;
        }
        if (count != state->console_count || previous != state->last_console || count > capacity - occupied) {
            ok = false; break;
        }
        occupied += count; previous_state = state;
    }
    if (occupied != system->console_count) ok = false;
    for (uint32_t cell = system->free_console; ok && cell != QA_BOT_NO_INDEX;) {
        if (cell >= capacity || seen[cell] || system->console[cell].previous != QA_BOT_NO_INDEX) { ok = false; break; }
        seen[cell] = 1; cell = system->console[cell].next;
    }
    for (size_t i = 0; ok && i < capacity; ++i) if (!seen[i]) ok = false;
    free(seen);
    if (ok) return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid bot chat state/message/free partition"); return false;
}
static bool system_fields(qa_source_save_io *io, qa_bot_chat_system *system,
                          const qa_bot_chat_asset_save_refs *refs, size_t *state_count)
{
    return asset_field(io, refs, &system->options.synonyms, QA_BOT_CHAT_SYNONYMS) &&
        asset_field(io, refs, &system->options.randoms, QA_BOT_CHAT_RANDOMS) &&
        asset_field(io, refs, &system->options.matches, QA_BOT_CHAT_MATCHES) &&
        asset_field(io, refs, &system->options.replies, QA_BOT_CHAT_REPLIES) &&
        qa_source_save_count(io, &system->options.console_capacity, UINT32_MAX - 1u) &&
        qa_source_save_bool(io, &system->options.debug) && qa_source_save_bool(io, &system->options.console_unavailable) &&
        qa_source_save_u64(io, &system->revision) && system->revision &&
        qa_source_save_count(io, &system->console_capacity, UINT32_MAX - 1u) &&
        qa_source_save_count(io, &system->console_count, system->console_capacity) &&
        qa_source_save_u32(io, &system->free_console) && qa_source_save_count(io, state_count, SIZE_MAX);
}
static void scratch_clear(qa_bot_chat_system *scratch)
{
    while (scratch->states) {
        qa_bot_chat *state = scratch->states; scratch->states = state->next;
        qa_bot_chat_asset_release(state->initial); free(state);
    }
    qa_bot_chat_asset_release(scratch->options.synonyms); qa_bot_chat_asset_release(scratch->options.randoms);
    qa_bot_chat_asset_release(scratch->options.matches); qa_bot_chat_asset_release(scratch->options.replies);
    free(scratch->console);
}
bool qa_bot_chat_system_capture(const qa_bot_chat_system *system, const qa_bot_chat_asset_save_refs *refs,
                                qa_buffer *out, qa_error *error)
{
    if (!system || !out || !refs || !refs->encode || system->retired || qa_bot_chat_system_active(system)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot chat system is absent, retired or borrowed"); return false;
    }
    if (!topology_valid(system, error)) return false;
    size_t count = 0;
    for (const qa_bot_chat *state = system->states; state; state = state->next) ++count;
    qa_source_save_io io = {0}; qa_bot_chat_system view = *system;
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) && system_fields(&io, &view, refs, &count);
    for (size_t i = 0; ok && i < system->console_capacity; ++i) {
        chat_console_cell cell = system->console[i];
        ok = message_fields(&io, &cell.message) && qa_source_save_u32(&io, &cell.next) && qa_source_save_u32(&io, &cell.previous);
    }
    for (const qa_bot_chat *state = system->states; ok && state; state = state->next) {
        qa_bot_chat value = *state; ok = state_fields(&io, &value, refs);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_bot_chat_system_restore_bytes(qa_bot_chat_system *system, qa_bytes bytes, const qa_bot_chat_asset_save_refs *refs,
                                      qa_bot_chat_restored_states *out, qa_error *error)
{
    if (!system || system->states || system->console_count || system->retired || qa_bot_chat_system_active(system) ||
        !refs || !refs->decode || !out || out->states || out->count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot chat restore requires empty detached owner and state mapping"); return false;
    }
    qa_bot_chat_system scratch = {.references = 1};
    qa_bot_chat_restored_states states = {0}; qa_source_save_io io = {0};
    system->restoring = true;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        system_fields(&io, &scratch, refs, &states.count);
    if (ok && (scratch.console_capacity > SIZE_MAX / sizeof(*scratch.console) ||
        states.count > SIZE_MAX / sizeof(*states.states) || states.count == SIZE_MAX ||
        scratch.console_capacity > (io.input.size - io.offset) / 276 ||
        states.count > (io.input.size - io.offset - scratch.console_capacity * 276) / 325))
        ok = bot_save_fail(&io, QA_ERROR_FORMAT, "Truncated or oversized bot chat storage");
    if (ok && scratch.console_capacity && !(scratch.console = calloc(scratch.console_capacity, sizeof(*scratch.console))))
        ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring physical bot chat console cells");
    if (ok && states.count && !(states.states = calloc(states.count, sizeof(*states.states))))
        ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot chat state mapping");
    for (size_t i = 0; ok && i < scratch.console_capacity; ++i) {
        chat_console_cell *cell = scratch.console + i;
        ok = message_fields(&io, &cell->message) && qa_source_save_u32(&io, &cell->next) && qa_source_save_u32(&io, &cell->previous);
    }
    qa_bot_chat **tail = &scratch.states;
    qa_bot_chat *previous = NULL;
    for (size_t i = 0; ok && i < states.count; ++i) {
        qa_bot_chat *state = calloc(1, sizeof(*state));
        if (!state) { ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring private bot chat state"); break; }
        state->system = &scratch; state->references = 1; state->previous = previous;
        *tail = state; tail = &state->next; previous = state; states.states[i] = state;
        ++scratch.references;
        ok = state_fields(&io, state, refs);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL) && topology_valid(&scratch, error);
    if (ok) {
        qa_bot_chat_asset_release(system->options.synonyms); qa_bot_chat_asset_release(system->options.randoms);
        qa_bot_chat_asset_release(system->options.matches); qa_bot_chat_asset_release(system->options.replies);
        free(system->console);
        system->options = scratch.options; system->console = scratch.console;
        system->free_console = scratch.free_console; system->console_count = scratch.console_count;
        system->console_capacity = scratch.console_capacity; system->states = scratch.states;
        system->references = scratch.references; system->revision = scratch.revision;
        for (qa_bot_chat *state = system->states; state; state = state->next) state->system = system;
        scratch.options = (qa_bot_chat_options){0}; scratch.console = NULL; scratch.states = NULL;
        *out = states; states = (qa_bot_chat_restored_states){0};
    }
    system->restoring = false;
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid bot chat owner continuation");
    scratch_clear(&scratch); qa_bot_chat_restored_states_free(&states); qa_source_save_dispose(&io);
    return ok;
}
