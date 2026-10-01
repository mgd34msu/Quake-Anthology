#include "guest_q3_grapple_profile.h"

#include <stdlib.h>
#include <string.h>

struct application_q3_grapple_profile {
    qa_qvm_image *image;
    char *path;
    const application_q3_grapple_definition *definition;
};
static const int32_t threewave_fire[] = {0};
static const application_q3_grapple_word threewave_movement[] = {{232, 10}, {236, 0}};
static const application_q3_grapple_cvar threewave_cvars[] = {
    {"g_gametype", "10"}, {"g_lithium", "0"}, {"p_enablePortal", "0"}};
static const application_q3_grapple_attachment threewave_attachments[] = {
    {"models/weapons2/grapple/grapple_hand.md3", "tag_hook"}};
typedef struct profile_card {
    const char *digest;
    application_q3_grapple_definition definition;
} profile_card;
static const profile_card cards[] = {
    {"sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337", {
        .id = "threewave-1.7", .title = "Threewave CTF (Quake 3)", .entity_stride = 876, .client_stride = 944,
        .fields = {.inuse = 520, .client = 516, .parent = 600, .target = 768, .mover = UINT32_MAX,
            .health = 732, .takedamage = 736, .hook = 816, .event_time = 552, .free_after_event = 556},
        .globals = {1077712, 1077708, 1091860, 1091720, 1091768},
        .callbacks = {210993, 211210, 217563, 215035, 215169, 177663, 0,
            16897, 29990, 0, 162405, 197341, 35535},
        .fire_arguments = threewave_fire, .fire_argument_count = 1, .movement_bytes = 240,
        .movement_words = threewave_movement, .movement_word_count = 2,
        .initial_cvars = threewave_cvars, .initial_cvar_count = 3,
        .pulling_flag = 2048, .event_lifetime_ms = 100, .damage_method = 29,
        .presentation = {.projectile_model = "models/weapons2/grapple/grapple_hook.md3",
            .view_model = "models/weapons2/grapple/grap.md3", .weapon_index = 11,
            .anchor_path = "models/weapons2/shotgun/shotgun_hand.md3", .anchor_tag = "tag_weapon",
            .anchor_offset = {5, 0, -1}, .fov_above = 90, .fov_scale = -.2f,
            .attachments = threewave_attachments, .attachment_count = 1,
            .cable_flight = "models/weapons2/grapple/grapple1_cord_s.md3",
            .cable_pull = "models/weapons2/grapple/grapple1_cord_p.md3",
            .cable_hold = "models/weapons2/grapple/grapple1_cord_f.md3", .cable_segment_length = 14,
            .fire_sound = "sound/cctf/grapple/grapple_fire.wav", .attach_sound = "sound/cctf/grapple/grapple_hit.wav",
            .pull_sound = "sound/cctf/grapple/grapple_pull.wav", .hang_sound = "sound/cctf/grapple/grapple_hang.wav"}}},
    {"sha256:b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e", {
        .id = "lrctf-1.2", .title = "LRCTF (Quake 3)", .entity_stride = 856, .client_stride = 872,
        .fields = {.inuse = 520, .client = 516, .parent = 600, .target = 784, .mover = 836,
            .health = 748, .takedamage = 752, .hook = 840, .event_time = 552, .free_after_event = 556},
        .globals = {998864, 998860, 1008980, 1008836, 1008884},
        .callbacks = {178797, 179014, 183076, 183171, 183171, 151164, 183577,
            5362, 15874, 184569, 140358, 169306, 22369},
        .movement_bytes = 16, .pulling_flag = 2048, .event_lifetime_ms = 300, .damage_method = 23,
        .presentation = {.projectile_model = "models/weapons3/hook/hook1.md3",
            .view_model = "models/weapons3/hook/bit1.md3", .weapon_index = 10,
            .anchor_path = "models/weapons2/shotgun/shotgun_hand.md3", .anchor_tag = "tag_weapon",
            .fov_above = 90, .fov_scale = -.2f, .cable_shader = true, .cable_path = "grapplerope", .cable_width = 16,
            .fire_sound = "sound/grapple/midevil/grfire.wav", .attach_sound = "sound/grapple/midevil/grhit.wav"}}}
};

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static bool field(uint32_t offset, size_t minimum, uint32_t extent)
{ return offset % 4 == 0 && offset >= minimum && extent >= 4 && offset <= extent - 4; }
static bool qualify(const qa_qvm_image *image, qa_qvm_abi abi,
    const application_q3_grapple_definition *p, qa_error *error)
{
    size_t entity = qa_qvm_shared_entity_bytes(abi), player = qa_qvm_player_bytes(abi);
    size_t memory = qa_qvm_image_memory_size(image);
    if (!entity || !player || p->entity_stride % 4 || p->client_stride % 4 ||
        p->entity_stride < entity || p->client_stride < player ||
        p->entity_stride > memory || p->client_stride > memory)
        return fail(error, QA_ERROR_FORMAT, "Grapple records leave their original ABI and image");
    const uint32_t fields[] = {p->fields.inuse, p->fields.client, p->fields.parent, p->fields.target,
        p->fields.health, p->fields.takedamage, p->fields.event_time, p->fields.free_after_event};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (!field(fields[i], entity, p->entity_stride))
            return fail(error, QA_ERROR_FORMAT, "Grapple private field leaves its entity record");
    if ((p->fields.mover != UINT32_MAX && !field(p->fields.mover, entity, p->entity_stride)) ||
        !field(p->fields.hook, player, p->client_stride))
        return fail(error, QA_ERROR_FORMAT, "Grapple mover or hook leaves its private record");
    const uint32_t globals[] = {p->globals.time, p->globals.frame, p->globals.movement,
        p->globals.forward, p->globals.forward + 4, p->globals.forward + 8, p->globals.ground_plane};
    for (size_t i = 0; i < sizeof(globals) / sizeof(globals[0]); ++i)
        if (!qa_qvm_qualify_global_word(image, globals[i], error)) return false;
    size_t count = 0;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    const uint32_t callbacks[] = {p->callbacks.allocate, p->callbacks.free, p->callbacks.fire,
        p->callbacks.release, p->callbacks.force_release, p->callbacks.missile, p->callbacks.think,
        p->callbacks.pull, p->callbacks.damage, p->callbacks.same_team, p->callbacks.player_move};
    for (size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); ++i)
        if (!callbacks[i] || callbacks[i] >= count || code[callbacks[i]].opcode != QA_QVM_ENTER)
            return fail(error, QA_ERROR_FORMAT, "Grapple callback is not its original source function");
    const uint32_t optional[] = {p->callbacks.follow, p->callbacks.move_mover_hooks};
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i)
        if (optional[i] && (optional[i] >= count || code[optional[i]].opcode != QA_QVM_ENTER))
            return fail(error, QA_ERROR_FORMAT, "Grapple optional callback leaves its original source function");
    uint32_t scratch;
    return qa_qvm_source_scratch_qualify(image, p->movement_bytes < 64 ? 64 : p->movement_bytes,
        &scratch, error);
}

bool application_q3_grapple_profile_create(qa_qvm_image *image, qa_qvm_role role, qa_qvm_abi abi,
    const char *path, application_q3_grapple_profile **out, qa_error *error)
{
    if (!image || role != QA_QVM_GAME || (unsigned)abi > QA_QVM_Q3_116N ||
        !path || !*path || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Grapple metadata requires its actual GAME artifact and empty owner");
    const application_q3_grapple_definition *definition = NULL;
    for (size_t i = 0; i < sizeof(cards) / sizeof(cards[0]); ++i) {
        qa_sha256_digest digest;
        if (!qa_sha256_parse(cards[i].digest, &digest, error)) return false;
        if (qa_sha256_equal(&digest, qa_qvm_image_digest(image))) { definition = &cards[i].definition; break; }
    }
    if (!definition) return true;
    if (abi != QA_QVM_Q3_MODERN)
        return fail(error, QA_ERROR_FORMAT, "Grapple metadata differs from its exact source ABI");
    if (!qualify(image, abi, definition, error)) return false;
    application_q3_grapple_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) return fail(error, QA_ERROR_MEMORY, "Retaining immutable GAME grapple metadata");
    size_t length = strlen(path) + 1;
    profile->path = malloc(length);
    if (!profile->path) { free(profile); return fail(error, QA_ERROR_MEMORY, "Retaining grapple module identity path"); }
    memcpy(profile->path, path, length);
    qa_qvm_image_retain(image); profile->image = image; profile->definition = definition;
    *out = profile; return true;
}
void application_q3_grapple_profile_destroy(application_q3_grapple_profile *profile)
{
    if (!profile) return;
    qa_qvm_image_release(profile->image); free(profile->path); free(profile);
}
const application_q3_grapple_definition *application_q3_grapple_profile_definition(
    const application_q3_grapple_profile *profile)
{ return profile ? profile->definition : NULL; }
const qa_qvm_image *application_q3_grapple_profile_image(const application_q3_grapple_profile *profile)
{ return profile ? profile->image : NULL; }
const char *application_q3_grapple_profile_path(const application_q3_grapple_profile *profile)
{ return profile ? profile->path : NULL; }
