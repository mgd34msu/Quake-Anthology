#include "material_movies_private.h"
#include "material_movies_save.h"
#include "qa/binary.h"
#include "qa/media_save.h"
#include "qa/source_save.h"
#include "qa/media_library_prepare.h"
#include "qa/scene_save.h"
#include <math.h>

static bool refs_ready(const frontend_material_movies_refs *refs)
{
    return refs && refs->asset_encode && refs->asset_decode && refs->images.encode && refs->images.decode &&
        refs->frames.image_encode && refs->frames.image_decode &&
        refs->frames.geometry_encode && refs->frames.geometry_decode &&
        refs->frames.material_encode && refs->frames.material_decode &&
        refs->frames.mesh_identity_encode && refs->frames.mesh_identity_decode &&
        refs->frames.light_identity_encode && refs->frames.light_identity_decode;
}
static bool asset_member(const frontend_material_movies *owner, const qa_cinematic_asset *asset)
{
    for (size_t i = 0; i < qa_media_library_record_count(owner->source.media); ++i)
        if (qa_media_library_record_at(owner->source.media, i) == asset) return true;
    return false;
}
static bool image_member(const frontend_material_movies *owner, const qa_scene_image *image)
{
    const qa_scene_resources *banks[] = {owner->source.images}; size_t ordinal = 0;
    return image && qa_scene_image_owner_index(banks, 1, image, &ordinal);
}
static bool private_frame(const frontend_material_movies *owner, const frontend_material_movie_row *row)
{
    const qa_scene_frame *frame = &row->publication;
    return frame->owner == owner->source.frontend->frame.owner && !frame->sequence &&
        !frame->material_order && frame->command_count == 1 && frame->command_capacity >= 1 &&
        frame->commands && frame->commands[0].kind == QA_SCENE_COMMAND_IMAGE &&
        frame->commands[0].data.image == row->initial && frame->image_count == 1 &&
        frame->image_capacity >= 1 && frame->images && frame->images[0] == row->initial &&
        !frame->geometry_count && !frame->geometry_capacity && !frame->geometries &&
        !frame->group_count && !frame->group_capacity && !frame->groups &&
        !frame->sort_group_capacity && !frame->sort_groups &&
        !frame->sort_command_capacity && !frame->sort_commands &&
        !frame->storage.first && !frame->storage.current && frame->storage.block_size == 262144;
}
static bool row_valid(const frontend_material_movies *owner, const frontend_material_movie_row *row)
{
    if (row && row->failed) {
        const qa_scene_frame *frame = &row->publication;
        return row->path && (strchr(row->path, '/') || strchr(row->path, '\\')) &&
            row->failure.code > QA_OK && row->failure.code <= QA_ERROR_NOT_FOUND &&
            memchr(row->failure.message, 0, sizeof(row->failure.message)) &&
            !row->asset && !row->playback && !row->initial && !row->target &&
            frame->owner == owner->source.frontend->frame.owner && !frame->sequence && !frame->material_order &&
            !frame->commands && !frame->command_count && !frame->command_capacity &&
            !frame->images && !frame->image_count && !frame->image_capacity &&
            !frame->geometries && !frame->geometry_count && !frame->geometry_capacity &&
            !frame->groups && !frame->group_count && !frame->group_capacity &&
            !frame->sort_groups && !frame->sort_group_capacity && !frame->sort_commands && !frame->sort_command_capacity &&
            !frame->storage.first && !frame->storage.current && frame->storage.block_size == 262144;
    }
    if (!row || !frontend_material_movie_path_valid(row->path) || !row->target || !asset_member(owner, row->asset) ||
        !image_member(owner, row->initial) || !private_frame(owner, row) ||
        !qa_cinematic_material_owner_is(row->playback, row->asset, row->path, row->target,
            (qa_media_clock){(void *)owner, frontend_material_movie_clock})) return false;
    uint32_t width = 0, height = 0; qa_cinematic_asset_dimensions(row->asset, &width, &height);
    const qa_scene_image *initial = row->initial;
    if (!width || !height || (size_t)height > SIZE_MAX / 4 / width ||
        !initial->name || strcmp(initial->name, row->path) || initial->kind != QA_SCENE_RGBA8 ||
        initial->level_count != 1 || !initial->levels || initial->wrap != QA_SCENE_CLAMP ||
        initial->filter != QA_SCENE_LINEAR || initial->levels[0].width != width ||
        initial->levels[0].height != height || initial->levels[0].bytes != (size_t)width * height * 4 ||
        !initial->levels[0].pixels || initial->border.x || initial->border.y || initial->border.z || initial->border.w != 1)
        return false;
    const uint8_t *pixels = initial->levels[0].pixels;
    for (size_t i = 0; i < initial->levels[0].bytes; ++i) if (pixels[i]) return false;
    qa_cinematic_publication publication = {0};
    return qa_cinematic_publication_read(row->playback, &publication) &&
        image_member(owner, publication.image) &&
        (publication.frame == &row->publication || publication.frame == &owner->source.frontend->frame) &&
        (publication.frame != &row->publication || publication.sequence == row->publication.sequence);
}
static bool registry_matches(const frontend_material_movies *owner)
{
    size_t count = frontend_material_movie_live_count(owner);
    if (qa_material_movies_count(owner->registry) != count) return false;
    size_t j = 0;
    for (size_t i = 0; i < count; ++i) {
        qa_material_movie_record record = {0};
        if (!qa_material_movies_read(owner->registry, i, &record) || !record.enabled) return false;
        while (j < owner->count && owner->rows[j]->failed) ++j;
        if (j == owner->count || owner->rows[j]->playback != record.playback ||
            owner->rows[j]->initial->identity != record.initial) return false;
        ++j;
    }
    return true;
}
static bool receipts_match(const frontend_material_movies *owner)
{
    for (size_t i = 0; i < qa_material_library_record_count(owner->source.materials); ++i) {
        size_t count = qa_material_library_video_receipt_count(owner->source.materials, i);
        for (size_t j = 0; j < count; ++j) {
            const char *source = NULL; const qa_scene_image *image = NULL;
            if (!qa_material_library_video_receipt_read(owner->source.materials, i, j, &source, &image) || !source)
                return false;
            if (owner->cinematic_mode) {
                size_t k=0;
                while (k<owner->cinematic_count &&
                    (strcmp(owner->cinematic_receipts[k].path,source) || owner->cinematic_receipts[k].image!=image)) ++k;
                if (k==owner->cinematic_count) return false;
                continue;
            }
            if (!image) return false;
            /* Compare the actual donor normalization without allocation. */
            bool prefix = !strchr(source, '/') && !strchr(source, '\\');
            size_t k = 0;
            while (k < owner->count && (prefix ?
                strncmp(owner->rows[k]->path, "video/", 6) || strcmp(owner->rows[k]->path + 6, source) :
                strcmp(owner->rows[k]->path, source))) ++k;
            if (k == owner->count || owner->rows[k]->failed || owner->rows[k]->initial != image) return false;
        }
    }
    return true;
}
static bool cinematic_receipts_valid(const frontend_material_movies *owner)
{
    if (!owner->cinematic_mode)
        return !owner->cinematic_source && !owner->cinematic_count && !owner->cinematic_capacity && !owner->cinematic_receipts;
    qa_q3_cinematic_source *source=NULL;
    qa_q3_cinematic_handles_options pool;
    if (owner->count || owner->capacity || owner->rows || owner->next_target!=1 ||
        owner->cinematic_count>owner->cinematic_capacity ||
        (owner->cinematic_capacity && !owner->cinematic_receipts) ||
        !frontend_material_movies_cinematic_read(owner,&source,NULL) || !source ||
        !qa_q3_cinematic_handles_read(qa_q3_cinematic_source_handles(source),&pool)) return false;
    const qa_scene_resources *banks[]={pool.images};
    for (size_t i=0;i<owner->cinematic_count;++i) {
        const frontend_material_movie_cinematic_receipt *receipt=owner->cinematic_receipts+i;
        if (!receipt->path || !receipt->path[0] || receipt->handle < -1 || receipt->handle>=16) return false;
        if (receipt->handle<0) { if (receipt->image) return false; continue; }
        size_t index=0;
        const qa_scene_image *root=qa_scene_source_q3_scratch(pool.images,(size_t)receipt->handle);
        if (!root || !receipt->image || root->identity!=receipt->image->identity ||
            !qa_scene_image_owner_index(banks,1,receipt->image,&index)) return false;
    }
    return true;
}
static bool owner_valid(const frontend_material_movies *owner, bool qualify_receipts)
{
    if (!frontend_material_movies_current(owner) || owner->count > owner->capacity ||
        (owner->capacity && !owner->rows) || !qa_media_library_idle(owner->source.media) ||
        !registry_matches(owner) || !cinematic_receipts_valid(owner)) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        const frontend_material_movie_row *row = owner->rows[i];
        if (!row_valid(owner, row) || (owner->next_target && row->target >= owner->next_target)) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(owner->rows[j]->path, row->path) || (!row->failed && !owner->rows[j]->failed &&
                (owner->rows[j]->target == row->target || owner->rows[j]->initial == row->initial ||
                    owner->rows[j]->playback == row->playback))) return false;
    }
    return !qualify_receipts || receipts_match(owner);
}
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining cold shader movie bytes");
    }
    return qa_source_save_bytes(io, value->data, size);
}
static bool text(qa_source_save_io *io, char **value)
{
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1) || !size || size == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *value = malloc(size + 1);
        if (!*value) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining cold shader path cache");
        (*value)[size] = 0;
    }
    return qa_source_save_bytes(io, *value, size) && !memchr(*value, 0, size);
}
static bool target_encode(void *context, uint64_t target, qa_buffer *out, qa_error *error)
{
    const frontend_material_movie_row *row = context;
    if (!row || row->target != target || !target || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_FORMAT, "Movie target is outside its actual shader row");
    out->data = malloc(8);
    if (!out->data) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining real shader target descriptor");
    out->size = 8; qa_store_u64le(out->data, target); return true;
}
static bool target_decode(void *context, qa_bytes bytes, uint64_t *target, qa_error *error)
{
    const frontend_material_movie_row *row = context;
    if (!row || !target || bytes.size != 8 || !bytes.data || !row->target || qa_load_u64le(bytes.data) != row->target)
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved movie target leaves its imported shader row");
    *target = row->target; return true;
}
static bool movie_encode(void *context, const qa_cinematic *movie, uint64_t *key, qa_error *error)
{
    const frontend_material_movies *owner = context;
    for (size_t i = 0; i < owner->count; ++i) if (owner->rows[i]->playback == movie) { *key = i + 1; return true; }
    return frontend_fail(error, QA_ERROR_FORMAT, "Registry movie is outside the real provider path cache");
}
static bool movie_decode(void *context, uint64_t key, qa_cinematic **out, qa_error *error)
{
    const frontend_material_movies *owner = context;
    if (!key || key > owner->count || !owner->rows[key - 1] || owner->rows[key - 1]->failed || !owner->rows[key - 1]->playback)
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved registry movie has no imported shader row");
    *out = owner->rows[key - 1]->playback; return true;
}
static bool initial_encode(void *context, uint64_t identity, uint64_t *key, qa_error *error)
{
    const frontend_material_movies *owner = context;
    for (size_t i = 0; i < owner->count; ++i) if (!owner->rows[i]->failed && owner->rows[i]->initial->identity == identity) { *key = i + 1; return true; }
    return frontend_fail(error, QA_ERROR_FORMAT, "Movie initial image is outside its real provider rows");
}
static bool initial_decode(void *context, uint64_t key, uint64_t *identity, qa_error *error)
{
    const frontend_material_movies *owner = context;
    if (!key || key > owner->count || !owner->rows[key - 1] || owner->rows[key - 1]->failed || !owner->rows[key - 1]->initial)
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved initial image has no actual imported shader row");
    *identity = owner->rows[key - 1]->initial->identity; return true;
}
static bool frame_encode(void *context, const qa_scene_frame *frame, uint64_t *key, qa_error *error)
{
    const frontend_material_movies *owner = context;
    if (frame != &owner->source.frontend->frame)
        return frontend_fail(error, QA_ERROR_FORMAT, "Registry frame is outside its genuine frontend");
    *key = 1; return true;
}
static bool frame_decode(void *context, uint64_t key, qa_scene_frame **frame, qa_error *error)
{
    const frontend_material_movies *owner = context;
    if (key != 1) return frontend_fail(error, QA_ERROR_FORMAT, "Saved registry frame has no actual candidate frontend");
    *frame = &owner->source.frontend->frame; return true;
}
static qa_material_movies_checkpoint_refs registry_refs(frontend_material_movies *owner)
{
    return (qa_material_movies_checkpoint_refs){owner, movie_encode, movie_decode,
        initial_encode, initial_decode, frame_encode, frame_decode};
}
static bool row_fields(qa_source_save_io *io, frontend_material_movies *owner,
    frontend_material_movie_row *row, const frontend_material_movies_refs *refs, double anchor)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, global = false;
    if (!text(io, &row->path) || !qa_source_save_bool(io, &row->failed)) return false;
    if (row->failed) {
        uint32_t code = row->failure.code;
        if (!qa_source_save_u32(io, &code) || code == QA_OK || code > QA_ERROR_NOT_FOUND ||
            !qa_source_save_count(io, &row->failure.offset, SIZE_MAX) ||
            !qa_source_save_bytes(io, row->failure.message, sizeof(row->failure.message))) return false;
        if (reading) row->failure.code = (qa_status)code;
        return row_valid(owner, row);
    }
    uint64_t asset = 0, initial = 0;
    size_t commands = row->publication.command_capacity, images = row->publication.image_capacity;
    qa_cinematic_publication actual = {0};
    if (!reading) {
        if (!qa_cinematic_publication_read(row->playback, &actual)) return false;
        global = actual.frame == &owner->source.frontend->frame;
    }
    if (!qa_source_save_u64(io, &row->target) || !row->target ||
        (!reading && (!refs->asset_encode(refs->context, row->asset, &asset, io->error) ||
            !refs->images.encode(refs->images.context, row->initial, &initial, io->error))) ||
        !qa_source_save_u64(io, &asset) || !asset || !qa_source_save_u64(io, &initial) || !initial ||
        !qa_source_save_bool(io, &global) ||
        !qa_source_save_count(io, &commands, SIZE_MAX / sizeof(*row->publication.commands)) || !commands ||
        !qa_source_save_count(io, &images, SIZE_MAX / sizeof(*row->publication.images)) || !images) return false;
    if (reading) {
        const qa_cinematic_asset *borrowed = NULL; const qa_scene_image *image = NULL;
        if (!refs->asset_decode(refs->context, asset, row->path, &borrowed, io->error) || !borrowed ||
            !asset_member(owner, borrowed) || !refs->images.decode(refs->images.context, initial, &image, io->error) ||
            !image_member(owner, image)) return false;
        row->asset = (qa_cinematic_asset *)borrowed; qa_cinematic_asset_retain(row->asset);
        row->initial = image; qa_scene_image_retain(image);
    }
    qa_buffer frame = {0}, playback = {0}, publication = {0}; qa_cinematic_checkpoint saved = {0};
    qa_media_checkpoint_refs targets = {row, target_encode, target_decode};
    bool ok = reading || (qa_scene_frame_checkpoint(&row->publication, &refs->frames, &frame, io->error) &&
        qa_cinematic_capture(row->playback, &saved, io->error) &&
        qa_cinematic_checkpoint_encode(&saved, &targets, &playback, io->error) &&
        qa_cinematic_presentation_checkpoint(row->playback,
            global ? &owner->source.frontend->frame : &row->publication, &refs->images, &publication, io->error));
    if (ok) ok = blob(io, &frame) && blob(io, &playback) && blob(io, &publication);
    if (ok && reading) {
        ok = qa_scene_frame_restore(&row->publication, (qa_bytes){frame.data, frame.size}, &refs->frames, io->error) &&
            row->publication.command_count <= commands && row->publication.image_count <= images;
        if (ok) {
            qa_scene_command *cmd = realloc(row->publication.commands, commands * sizeof(*cmd));
            if (!cmd) ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual initial movie command extent");
            else { row->publication.commands = cmd; row->publication.command_capacity = commands; }
        }
        if (ok) {
            const qa_scene_image **held = realloc(row->publication.images, images * sizeof(*held));
            if (!held) ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual initial movie image extent");
            else { row->publication.images = held; row->publication.image_capacity = images; }
        }
        if (ok) ok = qa_cinematic_checkpoint_decode((qa_bytes){playback.data, playback.size}, &targets, &saved, io->error);
        qa_cinematic_options options = frontend_material_movie_options(owner, row->target);
        qa_cinematic_source source = qa_cinematic_asset_source(row->asset); source.name = row->path;
        if (ok) ok = qa_cinematic_restore_qualified(&source, &options, &saved, anchor, &row->playback, io->error) &&
            qa_cinematic_presentation_restore(row->playback,
                global ? &owner->source.frontend->frame : &row->publication, &refs->images,
                (qa_bytes){publication.data, publication.size}, io->error);
    }
    qa_cinematic_checkpoint_free(&saved); qa_buffer_free(&frame); qa_buffer_free(&playback); qa_buffer_free(&publication);
    return ok;
}
static bool cinematic_header(qa_source_save_io *io, frontend_material_movies *owner,
    const frontend_material_movies_refs *refs, qa_q3_cinematic_handles **decoded_pool)
{
    if (!qa_source_save_bool(io,&owner->cinematic_mode)) return false;
    if (!owner->cinematic_mode) return true;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint32_t seat=owner->cinematic_seat;
    qa_buffer descriptor={0};
    bool ok=qa_source_save_u32(io,&seat) && seat!=QA_AUDIO_WORLD;
    if (ok && !reading) ok=refs->cinematic_encode &&
        refs->cinematic_encode(refs->context,owner->cinematic_bus,&descriptor,io->error);
    if (ok) ok=blob(io,&descriptor) && descriptor.size!=0;
    if (ok && reading) {
        uint32_t actual_seat=0; uint64_t bus=0;
        ok=decoded_pool && refs->cinematic_decode &&
            refs->cinematic_decode(refs->context,seat,(qa_bytes){descriptor.data,descriptor.size},
                decoded_pool,&actual_seat,&bus,io->error) && *decoded_pool && actual_seat!=QA_AUDIO_WORLD;
        if (ok) { owner->cinematic_seat=actual_seat; owner->cinematic_bus=bus; }
    }
    qa_buffer_free(&descriptor); return ok;
}
static bool fields(qa_source_save_io *io, frontend_material_movies *owner,
    const frontend_material_movies_refs *refs, qa_q3_cinematic_handles **decoded_pool)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','M','M'}; uint32_t schema = 2;
    size_t count = owner->count, capacity = owner->capacity;
    double anchor = (double)owner->source.frontend->wall_time_ns / 1000000.0;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFMM", 4) ||
        !qa_source_save_u32(io, &schema) || schema != 2 ||
        !cinematic_header(io,owner,refs,decoded_pool) ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(*owner->rows)) ||
        !qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(*owner->rows)) || count > capacity ||
        !qa_source_save_u64(io, &owner->next_target) || !qa_source_save_f64(io, &anchor) ||
        !isfinite(anchor) || anchor != (double)owner->source.frontend->wall_time_ns / 1000000.0 ||
        (owner->cinematic_mode && (count || capacity || owner->next_target!=1))) return false;
    if (reading) {
        if (count > (io->input.size - io->offset) / 42) return false;
        owner->rows = capacity ? calloc(capacity, sizeof(*owner->rows)) : NULL;
        if (capacity && !owner->rows) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual shader path-cache extent");
        owner->count = count; owner->capacity = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            owner->rows[i] = calloc(1, sizeof(*owner->rows[i]));
            if (!owner->rows[i]) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring real shader movie row");
            qa_scene_frame_init(&owner->rows[i]->publication, owner->source.frontend->frame.owner);
        }
        if (!row_fields(io, owner, owner->rows[i], refs, anchor)) return false;
    }
    qa_buffer registry = {0}; qa_material_movies_checkpoint_refs links = registry_refs(owner);
    bool ok = reading || qa_material_movies_checkpoint(owner->registry, &links, &registry, io->error);
    if (ok) ok = blob(io, &registry);
    if (ok && reading) ok = qa_material_movies_restore(owner->source.images,
        (qa_bytes){registry.data, registry.size}, &links, &owner->registry, io->error);
    qa_buffer_free(&registry);
    if (!ok || !owner->cinematic_mode) return ok;
    size_t receipts=owner->cinematic_count, receipt_capacity=owner->cinematic_capacity;
    if (!qa_source_save_count(io,&receipts,SIZE_MAX/sizeof(*owner->cinematic_receipts)) ||
        !qa_source_save_count(io,&receipt_capacity,SIZE_MAX/sizeof(*owner->cinematic_receipts)) || receipts>receipt_capacity) return false;
    if (reading) {
        if (receipts>(io->input.size-io->offset)/21) return false;
        owner->cinematic_receipts=receipt_capacity?calloc(receipt_capacity,sizeof(*owner->cinematic_receipts)):NULL;
        if (receipt_capacity && !owner->cinematic_receipts) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring numeric registration receipt extent");
        owner->cinematic_count=receipts; owner->cinematic_capacity=receipt_capacity;
    }
    for (size_t i=0;i<receipts;++i) {
        frontend_material_movie_cinematic_receipt *receipt=owner->cinematic_receipts+i;
        uint64_t image=0;
        if (!text(io,&receipt->path) || !qa_source_save_i32(io,&receipt->handle) ||
            receipt->handle < -1 || receipt->handle>=16 ||
            (!reading && receipt->image && !refs->images.encode(refs->images.context,receipt->image,&image,io->error)) ||
            !qa_source_save_u64(io,&image) || ((receipt->handle>=0)!=(image!=0))) return false;
        if (reading && image) {
            const qa_scene_image *decoded=NULL;
            if (!refs->images.decode(refs->images.context,image,&decoded,io->error) || !decoded) return false;
            receipt->image=decoded; qa_scene_image_retain(decoded);
        }
    }
    return true;
}
bool frontend_material_movies_checkpoint(const frontend_material_movies *owner,
    const frontend_material_movies_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !refs_ready(refs) || !frontend_material_movies_idle(owner) ||
        owner->restore_pending || !owner_valid(owner,true) ||
        (owner->cinematic_source && !qa_q3_cinematic_handles_idle(qa_q3_cinematic_source_handles(owner->cinematic_source))))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie capture requires its returned complete owner graph");
    frontend_material_movies *held = (frontend_material_movies *)owner; held->busy = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, held, refs,NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); held->busy = false;
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Retained shader movie continuation is inconsistent");
    return ok;
}
bool frontend_material_movies_restore(const frontend_material_movie_source *source,
    const frontend_material_movies_refs *refs, qa_bytes bytes, frontend_material_movies **out, qa_error *error)
{
    if (!out || *out || !refs_ready(refs) || !frontend_material_movie_source_valid(source) ||
        !source->frontend->source_restoring || !qa_media_library_idle(source->media) ||
        !qa_material_library_idle(source->materials))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie import requires its actual isolated provider");
    frontend_material_movies *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Restoring actual provider shader movies");
    owner->source = *source; owner->restore_pending = true; owner->busy = true;
    qa_source_save_io io = {0};
    qa_q3_cinematic_handles *decoded_pool=NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, owner, refs,&decoded_pool) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); owner->busy = false;
    if (ok) ok = qa_material_library_bind_video_start(source->materials, frontend_material_movies_start, owner, error) &&
        frontend_material_movie_link(owner, error);
    if (ok && owner->cinematic_mode) {
        ok=frontend_material_movies_cinematic_attach(owner,decoded_pool,owner->cinematic_seat,owner->cinematic_bus,error);
    }
    if (ok) ok=owner_valid(owner,!owner->cinematic_mode);
    if (!ok) {
        if (owner->cinematic_source && !qa_q3_cinematic_source_destroy(&owner->cinematic_source,error)) {
            *out=owner; return false;
        }
        if (qa_material_library_video_start_is(source->materials, frontend_material_movies_start, owner))
            qa_material_library_set_video_start(source->materials, NULL, NULL);
        if (owner->linked) frontend_material_movie_unlink(owner, NULL);
        bool registry_owned = owner->registry != NULL;
        if (registry_owned) qa_material_movies_destroy(owner->registry);
        for (size_t i = 0; i < owner->count; ++i) {
            if (!registry_owned && owner->rows[i]) qa_cinematic_restore_discard(owner->rows[i]->playback);
            frontend_material_movie_row_free(owner->rows[i]);
        }
        for (size_t i=0;i<owner->cinematic_count;++i) frontend_material_movie_cinematic_receipt_free(owner->cinematic_receipts+i);
        free(owner->cinematic_receipts);
        free(owner->rows); free(owner);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved shader movies leave their real provider receipts");
        return false;
    }
    *out = owner; return true;
}
bool frontend_material_movies_publish_ready(const frontend_material_movies *owner, qa_error *error)
{
    if (!frontend_material_movies_idle(owner) || !owner->restore_pending || !owner_valid(owner,true) ||
        (owner->cinematic_source && !qa_q3_cinematic_handles_idle(qa_q3_cinematic_source_handles(owner->cinematic_source))) ||
        !qa_material_movies_publish_ready(owner->registry, error))
        return frontend_fail(error, QA_ERROR_FORMAT, "Cold shader movie graph lost a qualified actual owner");
    return true;
}
void frontend_material_movies_publish(frontend_material_movies *owner)
{
    if (!frontend_material_movies_idle(owner) || !owner->restore_pending) return;
    qa_material_movies_publish(owner->registry); owner->restore_pending = false;
}
