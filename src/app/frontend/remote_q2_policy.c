#include "remote_q2_policy.h"
#include "remote_q2_private.h"
#include "capture.h"
#include "remote_q2_effects.h"
#include <stdlib.h>

typedef struct policy_source {
    frontend_remote_q2 *owner;
    frontend_remote_q2_view view;
    remote_q2_picture *pictures;
    qa_scene_resource_policy *bank;
    frontend_remote_q2_effects_policy *effects;
    bool held;
} policy_source;
typedef struct policy_image {
    const qa_scene_image **slot;
    const qa_scene_image *original, *prepared;
} policy_image;
struct frontend_remote_q2_image_policy {
    qa_frontend *frontend;
    frontend_resource_inventory *inventory;
    qa_application *application;
    frontend_remote_q2 *head;
    policy_source *sources;
    policy_image *images;
    size_t source_count, image_count;
    bool complete, sealed, published;
};
static bool current(const frontend_remote_q2_image_policy *ticket)
{
    if (!ticket || ticket->frontend->application != ticket->application || ticket->frontend->stepping ||
        ticket->frontend->resource_inventory != ticket->inventory || ticket->frontend->remote_q2 != ticket->head) return false;
    size_t count = 0;
    for (frontend_remote_q2 *row = ticket->head; row; row = row->next, ++count) {
        if (count == ticket->source_count) return false;
        const policy_source *saved = ticket->sources + count; const frontend_remote_q2_view *view = &saved->view;
        if (!saved->held) {
            if (ticket->complete || row->busy || row->importing || row->image_policy == ticket) return false;
            continue;
        }
        if (row != saved->owner || row->busy || row->importing || row->image_policy != ticket ||
            row->frontend != ticket->frontend || !remote_q2_domain_equal(&row->options.domain, &view->domain) ||
            row->identity != view->identity || row->loading_generation != view->loading_generation ||
            row->content_generation != view->content_generation || row->received_ns != view->received_ns ||
            row->content.catalog != view->content.catalog || row->content.selected != view->content.selected ||
            row->content.base != view->content.base || row->content.mounts != view->content.mounts ||
            row->map != view->map || row->images != view->images || row->materials != view->materials ||
            row->sounds != view->sounds || row->world != view->world || row->geometry != view->geometry ||
            row->media_ready != view->media_ready || row->retired != view->retired || row->pictures != saved->pictures) return false;
    }
    if (count != ticket->source_count) return false;
    for (size_t i = 0; i < ticket->image_count; ++i) {
        if (!ticket->images[i].slot) { if (ticket->complete) return false; continue; }
        if (*ticket->images[i].slot != (ticket->published ? ticket->images[i].prepared : ticket->images[i].original)) return false;
    }
    return true;
}
static void dispose(frontend_remote_q2_image_policy *ticket)
{
    for (size_t i = 0; i < ticket->image_count; ++i)
        qa_scene_image_release(ticket->published ? ticket->images[i].original : ticket->images[i].prepared);
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].owner && ticket->sources[i].owner->image_policy == ticket)
            ticket->sources[i].owner->image_policy = NULL;
    free(ticket->images); free(ticket->sources); free(ticket);
}
bool frontend_remote_q2_image_policy_prepare(qa_frontend *f, qa_scene_resource_policy *const *banks,
    size_t count, frontend_remote_q2_image_policy **out, qa_error *error)
{
    if (!f || !f->application || !f->resource_inventory || f->stepping || !out || *out || (count && !banks))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Remote Q2 picture policy requires its genuine retained roster");
    frontend_remote_q2_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining remote Q2 picture policy");
    ticket->frontend = f; ticket->inventory = f->resource_inventory; ticket->application = f->application;
    ticket->head = f->remote_q2;
    for (frontend_remote_q2 *row = ticket->head; row; row = row->next) {
        if (row->busy || row->importing || row->image_policy || ticket->source_count == SIZE_MAX / sizeof(*ticket->sources)) goto failed;
        ++ticket->source_count;
        if (row->white) {
            if (ticket->image_count == SIZE_MAX / sizeof(*ticket->images)) goto failed;
            ++ticket->image_count;
        }
        for (remote_q2_picture *picture = row->pictures; picture; picture = picture->next) {
            if (ticket->image_count == SIZE_MAX / sizeof(*ticket->images)) goto failed;
            ++ticket->image_count;
        }
    }
    ticket->sources = ticket->source_count ? calloc(ticket->source_count, sizeof(*ticket->sources)) : NULL;
    ticket->images = ticket->image_count ? calloc(ticket->image_count, sizeof(*ticket->images)) : NULL;
    if ((ticket->source_count && !ticket->sources) || (ticket->image_count && !ticket->images)) goto failed;
    size_t row_index = 0, image_index = 0;
    for (frontend_remote_q2 *row = ticket->head; row; row = row->next, ++row_index) {
        policy_source *saved = ticket->sources + row_index; saved->owner = row; saved->pictures = row->pictures;
        if (!frontend_remote_q2_metadata_read(row, &saved->view, error)) goto failed;
        for (size_t i = 0; i < count; ++i) if (row->images && qa_scene_resource_policy_source(banks[i]) == row->images) {
            if (saved->bank) goto failed;
            saved->bank = banks[i];
        }
        if (row->images && !saved->bank) goto failed;
        row->image_policy = ticket; saved->held = true;
        if (row->white) {
            policy_image *image = ticket->images + image_index++;
            image->slot = &row->white; image->original = row->white;
            qa_scene_image *prepared = NULL;
            if (!saved->bank || !qa_scene_resource_policy_image(saved->bank, row->white, &prepared, error)) goto failed;
            image->prepared = prepared;
        }
        for (remote_q2_picture *picture = row->pictures; picture; picture = picture->next, ++image_index) {
            policy_image *image = ticket->images + image_index;
            image->slot = &picture->image; image->original = picture->image;
            qa_scene_image *prepared = NULL;
            if (!saved->bank || !qa_scene_resource_policy_image(saved->bank, picture->image, &prepared, error)) goto failed;
            image->prepared = prepared;
        }
        if (row->effects && (!saved->bank ||
            !frontend_remote_q2_effects_policy_prepare(row->effects, saved->bank, &saved->effects, error))) goto failed;
    }
    ticket->complete = true;
    if (!current(ticket)) goto failed;
    *out = ticket; return true;
failed:
    if (ticket->sources) for (size_t i = 0; i < ticket->source_count; ++i) {
        if (!frontend_remote_q2_effects_policy_abort(&ticket->sources[i].effects, error)) {
            /* Keep the actual parent child and its mapped slots reachable for
             * the enclosing checked cleanup to retry. */
            *out = ticket; return false;
        }
    }
    if (ticket->images) for (size_t i = 0; i < ticket->image_count; ++i) qa_scene_image_release(ticket->images[i].prepared);
    if (ticket->sources) for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].owner && ticket->sources[i].owner->image_policy == ticket) ticket->sources[i].owner->image_policy = NULL;
    free(ticket->images); free(ticket->sources); free(ticket);
    if (!error || error->code == QA_OK) remote_q2_fail(error, QA_ERROR_ARGUMENT, "Remote Q2 picture policy lost its actual image owner");
    return false;
}
bool frontend_remote_q2_image_policy_ready(frontend_remote_q2_image_policy *ticket, qa_error *error)
{
    if (!ticket || !ticket->complete || !current(ticket) || ticket->published) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Remote Q2 picture owners changed during preparation");
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].effects && !frontend_remote_q2_effects_policy_ready(ticket->sources[i].effects, error)) return false;
    ticket->sealed = true; return true;
}
bool frontend_remote_q2_image_policy_ready_is(const frontend_remote_q2_image_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return false;
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].bank && !qa_scene_resource_policy_ready_is(ticket->sources[i].bank)) return false;
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].effects && !frontend_remote_q2_effects_policy_ready_is(ticket->sources[i].effects)) return false;
    return true;
}
void frontend_remote_q2_image_policy_publish(frontend_remote_q2_image_policy *ticket)
{
    if (!current(ticket) || !ticket->sealed || ticket->published) return;
    for (size_t i = 0; i < ticket->image_count; ++i) *ticket->images[i].slot = ticket->images[i].prepared;
    for (size_t i = 0; i < ticket->source_count; ++i)
        frontend_remote_q2_effects_policy_publish(ticket->sources[i].effects);
    ticket->published = true;
}
static bool end(frontend_remote_q2_image_policy **owner, bool published, qa_error *error)
{
    if (!owner || !*owner) return true;
    if (!current(*owner) || (*owner)->published != published)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Remote Q2 picture policy remains owned by its nonterminal roster");
    for (size_t i = 0; i < (*owner)->source_count; ++i) {
        frontend_remote_q2_effects_policy **child = &(*owner)->sources[i].effects;
        if (!(published ? frontend_remote_q2_effects_policy_finish(child, error) :
            frontend_remote_q2_effects_policy_abort(child, error))) return false;
    }
    dispose(*owner); *owner = NULL; return true;
}
bool frontend_remote_q2_image_policy_finish(frontend_remote_q2_image_policy **owner, qa_error *error)
{ return end(owner, true, error); }
bool frontend_remote_q2_image_policy_abort(frontend_remote_q2_image_policy **owner, qa_error *error)
{ return end(owner, false, error); }
