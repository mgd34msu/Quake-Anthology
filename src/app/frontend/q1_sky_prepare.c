#include "q1_sky_private.h"

typedef struct sky_policy_row {
    frontend_q1_sky_selection *source;
    frontend_q1_sky_selection candidate;
    qa_scene_resource_policy *bank;
    qa_scene_resources *source_bank;
    const qa_scene_image *original[6];
    uint8_t original_found;
} sky_policy_row;
struct frontend_q1_sky_policy {
    frontend_q1_sky *owner;
    frontend_q1_sky_selection *baseline, *retained;
    sky_policy_row *rows;
    size_t count;
    uint64_t sequence;
    bool sealed, published;
};
static bool current(const frontend_q1_sky_policy *ticket)
{
    if (!ticket || !ticket->owner || ticket->owner->pending != ticket || ticket->owner->busy ||
        !frontend_q1_sky_current(ticket->owner) || ticket->baseline != ticket->owner->baseline ||
        ticket->retained != ticket->owner->retained || ticket->sequence != ticket->owner->next_sequence) return false;
    frontend_q1_sky_selection *row = ticket->baseline ? ticket->baseline : ticket->retained;
    for (size_t i = 0; i < ticket->count; ++i) {
        const sky_policy_row *saved = ticket->rows + i;
        if (row != saved->source || !frontend_q1_sky_selection_current(ticket->owner, row) ||
            row->bank != saved->source_bank || row->name != saved->candidate.name ||
            row->provider != saved->candidate.provider || !qa_actor_id_equal(row->recipient, saved->candidate.recipient) ||
            row->sequence != saved->candidate.sequence || row->map_revision != saved->candidate.map_revision ||
            row->found != (ticket->published ? saved->candidate.found : saved->original_found)) return false;
        for (unsigned face = 0; face < 6; ++face)
            if (row->images[face] != (ticket->published ? saved->candidate.images[face] : saved->original[face])) return false;
        row = i == 0 && ticket->baseline ? ticket->retained : row->next;
    }
    return row == NULL;
}
static void dispose(frontend_q1_sky_policy *ticket)
{
    for (size_t i = 0; i < ticket->count; ++i)
        for (unsigned face = 0; face < 6; ++face)
            qa_scene_image_release(ticket->published ? ticket->rows[i].original[face] : ticket->rows[i].candidate.images[face]);
    ticket->owner->pending = NULL; free(ticket->rows); free(ticket);
}
bool frontend_q1_sky_policy_prepare(frontend_q1_sky *owner,
    qa_scene_resource_policy *const *banks, size_t count, frontend_q1_sky_policy **out, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !frontend_q1_sky_current(owner) || !owner->frontend->resource_inventory ||
        !out || *out || (count && !banks))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky preparation requires its actual held image inventory");
    size_t rows = owner->baseline ? 1 : 0;
    for (const frontend_q1_sky_selection *row = owner->retained; row; row = row->next) {
        if (rows == SIZE_MAX / sizeof(sky_policy_row))
            return frontend_fail(error, QA_ERROR_MEMORY, "Q1 sky policy rows exceed storage");
        ++rows;
    }
    frontend_q1_sky_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 sky policy");
    ticket->rows = rows ? calloc(rows, sizeof(*ticket->rows)) : NULL;
    if (rows && !ticket->rows) { free(ticket); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 sky image rows"); }
    ticket->owner = owner; ticket->baseline = owner->baseline; ticket->retained = owner->retained;
    ticket->sequence = owner->next_sequence; owner->pending = ticket; *out = ticket;
    frontend_q1_sky_selection *row = owner->baseline ? owner->baseline : owner->retained;
    for (size_t i = 0; i < rows; ++i) {
        sky_policy_row *saved = ticket->rows + i;
        saved->source = row; saved->source_bank = row->bank; saved->original_found = row->found;
        memcpy(saved->original, row->images, sizeof(saved->original));
        saved->candidate = *row; memset(saved->candidate.images, 0, sizeof(saved->candidate.images));
        saved->candidate.found = 0; ++ticket->count;
        for (size_t j = 0; j < count; ++j) if (qa_scene_resource_policy_source(banks[j]) == row->bank) {
            if (saved->bank) { frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky policy repeats its actual image bank"); goto failed; }
            saved->bank = banks[j];
        }
        qa_scene_resources *destination = qa_scene_resource_policy_destination(saved->bank);
        if (!destination || !frontend_q1_sky_selection_current(owner, row)) {
            frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky policy lacks its actual provider bank"); goto failed;
        }
        saved->candidate.bank = destination;
        if (!frontend_q1_sky_selection_load(&saved->candidate, destination, error)) goto failed;
        row = i == 0 && owner->baseline ? owner->retained : row->next;
    }
    if (!current(ticket)) { frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky policy changed during image preparation"); goto failed; }
    return true;
failed:
    dispose(ticket); *out = NULL; return false;
}
bool frontend_q1_sky_policy_ready(frontend_q1_sky_policy *ticket, qa_error *error)
{
    if (!current(ticket) || ticket->published || ticket->sealed)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared Q1 sky lost its actual source bindings");
    ticket->sealed = true; return true;
}
bool frontend_q1_sky_policy_ready_is(const frontend_q1_sky_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return false;
    for (size_t i = 0; i < ticket->count; ++i)
        if (!qa_scene_resource_policy_ready_is(ticket->rows[i].bank)) return false;
    return true;
}
void frontend_q1_sky_policy_publish(frontend_q1_sky_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return;
    for (size_t i = 0; i < ticket->count; ++i) {
        ticket->rows[i].source->found = ticket->rows[i].candidate.found;
        memcpy(ticket->rows[i].source->images, ticket->rows[i].candidate.images, sizeof(ticket->rows[i].source->images));
    }
    ticket->published = true;
}
static bool end(frontend_q1_sky_policy **out, bool published, qa_error *error)
{
    if (!out || !*out) return true;
    if (!current(*out) || (*out)->published != published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky policy cleanup retains its actual nonterminal owner");
    dispose(*out); *out = NULL; return true;
}
bool frontend_q1_sky_policy_finish(frontend_q1_sky_policy **out, qa_error *error)
{ return end(out, true, error); }
bool frontend_q1_sky_policy_abort(frontend_q1_sky_policy **out, qa_error *error)
{ return end(out, false, error); }
