#include "remote_q1_sky_policy.h"
#include "remote_q1_private.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct remote_sky_row {
    frontend_remote_q1 *owner;
    frontend_remote_q1_domain domain;
    qa_vfs *mounts;
    const qa_resource *map;
    qa_scene_resources *images;
    qa_scene_resource_policy *bank;
    char *name;
    uint64_t revision, map_generation;
    uint8_t found;
    const qa_scene_image *before[6], *after[6];
} remote_sky_row;
struct frontend_remote_q1_sky_policy {
    qa_frontend *frontend;
    const void *inventory;
    remote_sky_row *rows;
    size_t count;
    bool sealed, published;
};
static bool current(const frontend_remote_q1_sky_policy *ticket)
{
    if (!ticket || ticket->frontend->resource_inventory != ticket->inventory || !ticket->inventory ||
        frontend_remote_q1_count(ticket->frontend) != ticket->count) return false;
    for (size_t i = 0; i < ticket->count; ++i) {
        const remote_sky_row *saved = ticket->rows + i;
        const frontend_remote_q1 *row = frontend_remote_q1_at(ticket->frontend, i);
        if (row != saved->owner || row->busy || row->sky_policy != ticket ||
            !remote_q1_domain_equal(&saved->domain, &row->options.domain) || row->content.mounts != saved->mounts ||
            row->map != saved->map || row->images != saved->images || row->skybox != saved->name ||
            row->revision != saved->revision || row->map_generation != saved->map_generation || row->sky_found != saved->found) return false;
        for (unsigned j = 0; j < 6; ++j)
            if (row->sky_images[j] != (ticket->published ? saved->after[j] : saved->before[j])) return false;
    }
    return true;
}
static void dispose(frontend_remote_q1_sky_policy *ticket)
{
    for (size_t i = 0; i < ticket->count; ++i) {
        remote_sky_row *saved = ticket->rows + i;
        for (unsigned j = 0; j < 6; ++j) qa_scene_image_release(ticket->published ? saved->before[j] : saved->after[j]);
        saved->owner->sky_policy = NULL;
    }
    free(ticket->rows); free(ticket);
}
bool frontend_remote_q1_sky_policy_prepare(qa_frontend *f, qa_scene_resource_policy *const *banks,
    size_t bank_count, frontend_remote_q1_sky_policy **out, qa_error *error)
{
    if (!f || !f->resource_inventory || f->capture || !out || *out || (bank_count && !banks)) return false;
    size_t count = frontend_remote_q1_count(f);
    if (count > SIZE_MAX / sizeof(remote_sky_row)) return false;
    for (size_t i = 0; i < count; ++i) if (!frontend_remote_q1_idle(frontend_remote_q1_at(f, i))) return false;
    frontend_remote_q1_sky_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining remote Q1 sky policy");
    ticket->rows = count ? calloc(count, sizeof(*ticket->rows)) : NULL;
    if (count && !ticket->rows) { free(ticket); return false; }
    ticket->frontend = f; ticket->inventory = f->resource_inventory; *out = ticket;
    for (size_t i = 0; i < count; ++i) {
        frontend_remote_q1 *row = frontend_remote_q1_at(f, i); remote_sky_row *saved = ticket->rows + i;
        *saved = (remote_sky_row){.owner = row, .domain = row->options.domain, .mounts = row->content.mounts,
            .map = row->map, .images = row->images, .name = row->skybox, .revision = row->revision,
            .map_generation = row->map_generation, .found = row->sky_found};
        memcpy(saved->before, row->sky_images, sizeof(saved->before));
        row->sky_policy = ticket; ++ticket->count;
        bool has_images = false;
        for (unsigned j = 0; j < 6; ++j) if (saved->before[j]) has_images = true;
        if (!has_images) continue;
        for (size_t j = 0; j < bank_count; ++j) if (qa_scene_resource_policy_source(banks[j]) == row->images) {
            if (saved->bank) goto failed;
            saved->bank = banks[j];
        }
        if (!saved->bank) goto failed;
        for (unsigned j = 0; j < 6; ++j) if (saved->before[j]) {
            qa_scene_image *mapped = NULL;
            if (!qa_scene_resource_policy_image(saved->bank, saved->before[j], &mapped, error)) goto failed;
            saved->after[j] = mapped;
        }
    }
    if (!current(ticket)) goto failed;
    return true;
failed:
    dispose(ticket); *out = NULL;
    if (!error || error->code == QA_OK) remote_q1_fail(error, QA_ERROR_ARGUMENT, "Remote sky lost its actual prepared bank");
    return false;
}
bool frontend_remote_q1_sky_policy_ready(frontend_remote_q1_sky_policy *ticket, qa_error *error)
{
    if (!current(ticket) || ticket->published || ticket->sealed)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Remote sky preparation left its retained source");
    ticket->sealed = true; return true;
}
bool frontend_remote_q1_sky_policy_ready_is(const frontend_remote_q1_sky_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return false;
    for (size_t i = 0; i < ticket->count; ++i)
        if (ticket->rows[i].bank && !qa_scene_resource_policy_ready_is(ticket->rows[i].bank)) return false;
    return true;
}
void frontend_remote_q1_sky_policy_publish(frontend_remote_q1_sky_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return;
    for (size_t i = 0; i < ticket->count; ++i)
        memcpy(ticket->rows[i].owner->sky_images, ticket->rows[i].after, sizeof(ticket->rows[i].after));
    ticket->published = true;
}
static bool end(frontend_remote_q1_sky_policy **out, bool published, qa_error *error)
{
    if (!out || !*out) return true;
    if (!current(*out) || (*out)->published != published)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Remote sky retains a nonterminal policy owner");
    dispose(*out); *out = NULL; return true;
}
bool frontend_remote_q1_sky_policy_finish(frontend_remote_q1_sky_policy **out, qa_error *error)
{ return end(out, true, error); }
bool frontend_remote_q1_sky_policy_abort(frontend_remote_q1_sky_policy **out, qa_error *error)
{ return end(out, false, error); }
