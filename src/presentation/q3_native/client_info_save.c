#include "client_info_internal.h"

bool q3n_client_handles_valid(const q3n_clients *owner, const q3n_client_info *ci, qa_error *error)
{
    const qa_q3_presentation_assets *a = owner->options.assets;
    if (!a || (a->busy && (!a->capturing || a->codec_busy)))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 media handle observation requires the captured registry");
    for (size_t i = 0; i < 3; ++i) {
        int32_t model = ci->models[i], skin = ci->skins[i];
        if (model < 0 || (size_t)model > a->model_count || (model && !a->models[model - 1]) ||
            skin < 0 || (size_t)skin > a->skin_count || (skin && !a->skins[skin - 1]))
            return q3n_client_fail(error, QA_ERROR_FORMAT, "Native Q3 client media handle holder is absent");
        if (ci->info_valid && (!model || !skin || a->models[model - 1]->world))
            return q3n_client_fail(error, QA_ERROR_FORMAT, "Valid native Q3 client has incomplete body media");
    }
    if (ci->icon < 0 || (size_t)ci->icon > a->shader_count ||
        (ci->icon && !a->shaders[ci->icon - 1]) || (ci->info_valid && !ci->icon))
        return q3n_client_fail(error, QA_ERROR_FORMAT, "Native Q3 client icon holder is absent");
    for (size_t i = 0; i < 32; ++i)
        if (ci->sounds[i] < 0 || (size_t)ci->sounds[i] > a->sound_count ||
            (ci->sounds[i] && !a->sounds[ci->sounds[i] - 1]))
            return q3n_client_fail(error, QA_ERROR_FORMAT, "Native Q3 client custom sound holder is absent");
    return true;
}
static bool text(qa_source_save_io *io, char **value)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &length, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *value = malloc(length + 1);
        if (!*value) return q3n_client_fail(io->error, QA_ERROR_MEMORY, "Restoring native Q3 animation receipt");
        (*value)[length] = 0;
    }
    return qa_source_save_bytes(io, *value, length) && !memchr(*value, 0, length);
}
static bool animation_fields(qa_source_save_io *io, qa_player_animation_config *config)
{
    uint32_t footstep = config->footsteps, gender = config->gender;
    if (!qa_source_save_u32(io, &footstep) || footstep > QA_FOOTSTEP_ENERGY ||
        !qa_source_save_u32(io, &gender) || gender > QA_MODEL_NEUTER ||
        !qa_source_save_bool(io, &config->fixed_legs) || !qa_source_save_bool(io, &config->fixed_torso)) return false;
    config->footsteps = (qa_model_footstep)footstep; config->gender = (qa_model_gender)gender;
    for (size_t i = 0; i < 3; ++i)
        if (!qa_source_save_f32(io, &config->head_offset[i])) return false;
    for (size_t i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i) {
        qa_player_animation *a = &config->animations[i];
        if (!qa_source_save_i32(io, &a->first_frame) || !qa_source_save_i32(io, &a->num_frames) ||
            !qa_source_save_i32(io, &a->loop_frames) || !qa_source_save_i32(io, &a->frame_lerp) ||
            !qa_source_save_i32(io, &a->initial_lerp) || !qa_source_save_bool(io, &a->reversed) ||
            !qa_source_save_bool(io, &a->flipflop) || !qa_source_save_bool(io, &a->present)) return false;
    }
    return true;
}
static bool parser_fields(qa_source_save_io *io, qa_common_parser *parser)
{
    if (!qa_source_save_count(io, &parser->token_length, QA_COMMON_TOKEN_CAPACITY - 1) ||
        !qa_source_save_bytes(io, parser->token, parser->token_length) ||
        !qa_source_save_count(io, &parser->name_length, QA_COMMON_TOKEN_CAPACITY - 1) ||
        !qa_source_save_bytes(io, parser->name, parser->name_length) ||
        !qa_source_save_i32(io, &parser->line) || parser->line < 0) return false;
    parser->token[parser->token_length] = 0; parser->name[parser->name_length] = 0;
    return true;
}
static bool fixed_text(qa_source_save_io *io, char *value, size_t size)
{ return qa_source_save_bytes(io, value, size) && memchr(value, 0, size); }
static bool color_fields(qa_source_save_io *io, qa_vec3 *value)
{
    return qa_source_save_vec3(io, value) && (value->x == 0 || value->x == 1) &&
        (value->y == 0 || value->y == 1) && (value->z == 0 || value->z == 1);
}
static bool client_fields(qa_source_save_io *io, q3n_client_info *ci)
{
    if (!qa_source_save_bool(io, &ci->observed) || !qa_source_save_bool(io, &ci->info_valid) ||
        !qa_source_save_bool(io, &ci->deferred) || !qa_source_save_bool(io, &ci->new_anims) ||
        !qa_source_save_bool(io, &ci->team_leader) || !qa_source_save_u32(io, &ci->physical_client) ||
        !qa_source_save_u64(io, &ci->configstring_revision) || !qa_source_save_u64(io, &ci->media_revision) ||
        !fixed_text(io, ci->name, sizeof(ci->name)) || !fixed_text(io, ci->model_name, sizeof(ci->model_name)) ||
        !fixed_text(io, ci->skin_name, sizeof(ci->skin_name)) || !fixed_text(io, ci->head_model_name, sizeof(ci->head_model_name)) ||
        !fixed_text(io, ci->head_skin_name, sizeof(ci->head_skin_name)) || !fixed_text(io, ci->red_team, sizeof(ci->red_team)) ||
        !fixed_text(io, ci->blue_team, sizeof(ci->blue_team)) || !qa_source_save_i32(io, &ci->team) || ci->team < 0 || ci->team > 3 ||
        !qa_source_save_i32(io, &ci->bot_skill) || !qa_source_save_i32(io, &ci->handicap) ||
        !qa_source_save_i32(io, &ci->wins) || !qa_source_save_i32(io, &ci->losses) ||
        !qa_source_save_i32(io, &ci->team_task) || !color_fields(io, &ci->color1) || !color_fields(io, &ci->color2)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!qa_source_save_i32(io, &ci->models[i]) || !qa_source_save_i32(io, &ci->skins[i])) return false;
    if (!qa_source_save_i32(io, &ci->icon)) return false;
    for (size_t i = 0; i < 32; ++i) if (!qa_source_save_i32(io, &ci->sounds[i])) return false;
    q3n_client_dynamic *d = &ci->dynamic;
    if (!qa_source_save_i32(io, &d->score) || !qa_source_save_i32(io, &d->location) ||
        !qa_source_save_i32(io, &d->health) || !qa_source_save_i32(io, &d->armor) ||
        !qa_source_save_i32(io, &d->cur_weapon) || !qa_source_save_i32(io, &d->powerups) ||
        !qa_source_save_i32(io, &d->medkit_usage_time) || !qa_source_save_i32(io, &d->invulnerability_start_time) ||
        !qa_source_save_i32(io, &d->invulnerability_stop_time) || !qa_source_save_i32(io, &d->breath_puff_time)) return false;
    return animation_fields(io, &ci->animations);
}
static bool holder_fields(qa_source_save_io *io, q3n_clients *owner,
    q3n_animation_holder *holder, const q3n_client_refs *refs)
{
    bool read = io->direction == QA_SOURCE_SAVE_READ, present = holder->resource != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    if (!refs || !refs->resource_encode || !refs->resource_decode)
        return q3n_client_fail(io->error, QA_ERROR_ARGUMENT, "Native Q3 animation codec requires the aggregate resource dictionary");
    uint64_t id = 0;
    if ((!read && !refs->resource_encode(refs->context, holder->resource, &id, io->error)) ||
        !qa_source_save_u64(io, &id) || !id || !qa_source_save_u64(io, &holder->receipt.mount) ||
        !qa_source_save_u64(io, &holder->receipt.resource_id) ||
        !text(io, &holder->receipt.path) || !text(io, &holder->receipt.lookup_path) ||
        !text(io, &holder->receipt.link_source) || !text(io, &holder->receipt.link_target)) return false;
    if (read) {
        const qa_resource *resource = NULL;
        if (!refs->resource_decode(refs->context, id, &resource, io->error) || !resource) return false;
        holder->resource = (qa_resource *)resource; qa_resource_retain(holder->resource);
    }
    return holder->receipt.resource_id == qa_resource_id(holder->resource) &&
        qa_resource_pool_find(qa_vfs_resources(owner->options.content), holder->receipt.resource_id) == holder->resource &&
        qa_vfs_acquisition_valid(owner->options.content, &holder->receipt, io->error);
}
static bool fields(qa_source_save_io *io, q3n_clients *owner, const q3n_client_refs *refs)
{
    uint8_t magic[4] = {'Q', '3', 'C', 'I'}; uint32_t schema = 1, product = owner->options.product;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q3CI", 4) ||
        !qa_source_save_u32(io, &schema) || schema != 1 || !qa_source_save_u32(io, &product) ||
        product != (uint32_t)owner->options.product || !qa_source_save_u64(io, &owner->next_media_revision) ||
        !parser_fields(io, &owner->animation_parser)) return false;
    for (uint32_t i = 0; i < 64; ++i) {
        q3n_client_info *ci = &owner->clients[i]; q3n_animation_holder *holder = &owner->holders[i];
        if (!client_fields(io, ci) || ci->physical_client != i || ci->media_revision > owner->next_media_revision ||
            (ci->info_valid && (!ci->observed || !ci->media_revision)) || (!ci->info_valid && ci->deferred) ||
            !holder_fields(io, owner, holder, refs) || (ci->info_valid != (holder->resource != NULL)) ||
            !q3n_client_handles_valid(owner, ci, io->error)) return false;
        if (ci->info_valid) for (size_t j = 0; j < QA_PLAYER_ANIMATION_COUNT; ++j)
            if (j != 31 && !ci->animations.animations[j].present) return false;
    }
    return true;
}
static bool codec_ready(const q3n_clients *owner, qa_error *error)
{
    const qa_q3_presentation_assets *assets = owner ? owner->options.assets : NULL;
    return q3n_clients_idle(owner) && assets && assets->capturing && assets->busy == 1 && !assets->codec_busy ? true :
        q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client codec requires the genuine backend capture lease");
}
bool q3n_clients_checkpoint(const q3n_clients *borrowed, const q3n_client_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !codec_ready(borrowed, error)) return false;
    q3n_clients *owner = (q3n_clients *)borrowed; owner->busy = true;
    q3n_clients saved = *owner; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &saved, refs) && qa_source_save_finish(&io, out);
    if (!ok && error && error->code == QA_OK) q3n_client_fail(error, QA_ERROR_FORMAT, "Native Q3 client media checkpoint is inconsistent");
    qa_source_save_dispose(&io); owner->busy = false; return ok;
}
bool q3n_clients_restore(q3n_clients *owner, const q3n_client_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!codec_ready(owner, error)) return false;
    if (owner->next_media_revision) return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client restore requires an empty candidate");
    for (uint32_t i = 0; i < 64; ++i) if (owner->clients[i].observed || owner->holders[i].resource)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client restore requires empty physical rows");
    owner->busy = true;
    q3n_clients *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) { owner->busy = false; return q3n_client_fail(error, QA_ERROR_MEMORY, "Allocating native Q3 client restore candidate"); }
    candidate->options = owner->options; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, candidate, refs) && qa_source_save_finish(&io, NULL);
    if (ok) {
        memcpy(owner->clients, candidate->clients, sizeof(owner->clients));
        memcpy(owner->holders, candidate->holders, sizeof(owner->holders));
        memset(candidate->holders, 0, sizeof(candidate->holders)); owner->next_media_revision = candidate->next_media_revision;
        owner->animation_parser = candidate->animation_parser;
    } else if (error && error->code == QA_OK) q3n_client_fail(error, QA_ERROR_FORMAT, "Saved native Q3 client media ownership is inconsistent");
    qa_source_save_dispose(&io); q3n_clients_destroy(candidate); owner->busy = false; return ok;
}
