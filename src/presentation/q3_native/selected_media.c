/* SelectedQ3WeaponPresenter resources; SDK cg_weapons.c/cg_players.c,
 * id Software, GPL-2.0-or-later. */
#include "selected_media_internal.h"
#include <stdio.h>

const qa_q3_item *q3n_selected_media_item(qa_q3_product product, int32_t weapon)
{
    if (weapon <= 0 || weapon >= 14) return NULL;
    size_t count; const qa_q3_item *items = qa_q3_items(product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == QA_Q3_ITEM_WEAPON && items[i].tag == weapon) return &items[i];
    return NULL;
}
bool q3n_selected_media_path(const char *gun, const char *suffix, char out[128], qa_error *e)
{
    /* SelectedQ3WeaponPresenter strips the final extension, after the last '/'. */
    const char *slash = strrchr(gun, '/'), *dot = strrchr(gun, '.');
    size_t stem = dot && (!slash || dot > slash) ? (size_t)(dot - gun) : strlen(gun);
    size_t tail = strlen(suffix);
    if (stem >= 128 || tail >= 128 - stem)
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 weapon path exceeds its source storage");
    memcpy(out, gun, stem); memcpy(out + stem, suffix, tail + 1); return true;
}
static bool current(const q3n_selected_media_request *r, qa_error *e)
{ return r->current(r->context) || q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 equipment observation was superseded"); }
static unsigned char lower(unsigned char c)
{ return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static bool same(const char *a, const char *b)
{
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) { ++a; ++b; }
    return !*a && !*b;
}
static int32_t word(uint32_t value)
{ return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value); }
static int32_t integer(const char *s)
{
    while (*s && (signed char)*s <= 32) ++s;
    bool negative = *s == '-'; if (*s == '+' || *s == '-') ++s;
    uint32_t value = 0;
    while (*s >= '0' && *s <= '9') value = value * 10u + (uint32_t)(*s++ - '0');
    return word(negative ? 0u - value : value);
}
static float number(const char *s)
{
    while (*s && (signed char)*s <= 32) ++s;
    bool negative = *s == '-'; if (*s == '+' || *s == '-') ++s;
    float value = 0;
    while (*s >= '0' && *s <= '9') {
        float product = (float)((double)value * 10.0);
        value = (float)((double)product + (*s++ - '0'));
    }
    if (*s == '.') {
        ++s; float fraction = 0.1f;
        while (*s >= '0' && *s <= '9') {
            float product = (float)((double)(*s++ - '0') * fraction);
            value = (float)((double)value + product);
            fraction = (float)((double)fraction * 0.1f);
        }
    }
    return negative ? -value : value;
}
static bool warning(const char *format, const char *first, const char *second, qa_error *e)
{
    char message[1024];
    int size = snprintf(message, sizeof(message), format, first, second);
    return size >= 0 && (size_t)size < sizeof(message) ? true :
        q3p_fail(e, QA_ERROR_FORMAT, "CG_Printf exceeds its 1024-byte source buffer");
}
static bool parse_animation(qa_bytes bytes, qa_player_animation_config *out, qa_error *e)
{
    if (!bytes.size || bytes.size > 19998)
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 weapon animation configuration is empty or too long");
    qa_common_parser parser = {0}; qa_common_cursor cursor;
    if (!qa_common_cursor_init(&cursor, bytes, QA_COMMON_TERMINATED, e)) return false;
    qa_player_animation_config config = {0};
    for (;;) {
        qa_common_cursor_state previous = qa_common_cursor_capture(&cursor);
        if (!qa_common_parse(&parser, &cursor, true, e)) return false;
        const char *token = parser.token;
        if (same(token, "footsteps")) {
            if (!qa_common_parse(&parser, &cursor, true, e)) return false;
            const char *names[] = {"normal", "boot", "flesh", "mech", "energy"};
            bool known = same(token, "default");
            if (known) config.footsteps = QA_FOOTSTEP_NORMAL;
            for (unsigned i = 0; i < 5; ++i)
                if (same(token, names[i])) { config.footsteps = (qa_model_footstep)i; known = true; }
            if (!known && !warning("Bad footsteps parm in %s: %s\n", "models/players/sarge/animation.cfg", token, e)) return false;
        } else if (same(token, "headoffset")) {
            for (unsigned i = 0; i < 3; ++i) {
                if (!qa_common_parse(&parser, &cursor, true, e)) return false;
                config.head_offset[i] = number(token);
            }
        } else if (same(token, "sex")) {
            if (!qa_common_parse(&parser, &cursor, true, e)) return false;
            unsigned char first = lower((unsigned char)*token);
            config.gender = first == 'f' ? QA_MODEL_FEMALE : first == 'n' ? QA_MODEL_NEUTER : QA_MODEL_MALE;
        } else if (same(token, "fixedlegs")) config.fixed_legs = true;
        else if (same(token, "fixedtorso")) config.fixed_torso = true;
        else {
            if (*token >= '0' && *token <= '9') {
                if (!qa_common_cursor_restore(&cursor, previous, e)) return false;
                break;
            }
            if (!warning("unknown token '%s' is %s\n", token, "models/players/sarge/animation.cfg", e)) return false;
            if (previous.offset == cursor.offset && previous.ended == cursor.ended)
                return q3p_fail(e, QA_ERROR_FORMAT, "CG animation prelude reached the source nonprogress cycle");
        }
    }
    int32_t skip = 0; unsigned i = 0;
    for (; i < 31; ++i) {
        qa_player_animation *a = &config.animations[i];
        if (!qa_common_parse(&parser, &cursor, true, e)) return false;
        if (!*parser.token) {
            if (i >= 25) { *a = config.animations[6]; a->reversed = a->flipflop = false; a->present = true; continue; }
            break;
        }
        a->first_frame = integer(parser.token);
        if (i == 13) skip = word((uint32_t)a->first_frame - (uint32_t)config.animations[6].first_frame);
        if (i >= 13 && i < 25) a->first_frame = word((uint32_t)a->first_frame - (uint32_t)skip);
        if (!qa_common_parse(&parser, &cursor, true, e)) return false;
        if (!*parser.token) break;
        a->num_frames = integer(parser.token);
        if (a->num_frames < 0) { a->num_frames = word(0u - (uint32_t)a->num_frames); a->reversed = true; }
        if (!qa_common_parse(&parser, &cursor, true, e)) return false;
        if (!*parser.token) break;
        a->loop_frames = integer(parser.token);
        if (!qa_common_parse(&parser, &cursor, true, e)) return false;
        if (!*parser.token) break;
        float fps = number(parser.token); if (fps == 0) fps = 1;
        float lerp = (float)(1000.0 / (double)fps);
        a->frame_lerp = a->initial_lerp = lerp >= -2147483648.0f && lerp < 2147483648.0f ? (int32_t)lerp : INT32_MIN;
        a->present = true;
    }
    if (i != 31) return q3p_fail(e, QA_ERROR_FORMAT, "Error parsing selected Q3 weapon animation configuration");
    config.animations[32] = config.animations[13]; config.animations[32].reversed = true;
    config.animations[33] = config.animations[14]; config.animations[33].reversed = true;
    config.animations[34] = (qa_player_animation){0, 16, 16, 66, 66, false, false, true};
    config.animations[35] = (qa_player_animation){16, 5, 0, 50, 50, false, false, true};
    config.animations[36] = (qa_player_animation){16, 5, 1, 66, 66, true, false, true};
    *out = config; return true;
}
static bool copy_receipt(const qa_vfs_acquisition *from, qa_vfs_acquisition *out, qa_error *e)
{
    qa_vfs_acquisition copy = {.mount = from->mount, .resource_id = from->resource_id};
    const char *sources[] = {from->path, from->lookup_path, from->link_source, from->link_target};
    char **targets[] = {&copy.path, &copy.lookup_path, &copy.link_source, &copy.link_target};
    for (unsigned i = 0; i < 4; ++i) if (sources[i]) {
        size_t length = strlen(sources[i]) + 1; *targets[i] = malloc(length);
        if (!*targets[i]) { qa_vfs_acquisition_dispose(&copy); return q3p_fail(e, QA_ERROR_MEMORY, "Retaining selected CHARACTER animation receipt"); }
        memcpy(*targets[i], sources[i], length);
    }
    *out = copy; return true;
}
static bool same_text(const char *a, const char *b)
{ return a && b ? !strcmp(a, b) : a == b; }
static bool same_receipt(const qa_vfs_acquisition *a, const qa_vfs_acquisition *b)
{
    return a->mount == b->mount && a->resource_id == b->resource_id && same_text(a->path, b->path) &&
        same_text(a->lookup_path, b->lookup_path) && same_text(a->link_source, b->link_source) &&
        same_text(a->link_target, b->link_target);
}
static bool same_config(const qa_player_animation_config *a, const qa_player_animation_config *b)
{
    if (a->footsteps != b->footsteps || a->gender != b->gender || a->fixed_legs != b->fixed_legs ||
        a->fixed_torso != b->fixed_torso || memcmp(a->head_offset, b->head_offset, sizeof(a->head_offset))) return false;
    for (unsigned i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i) {
        const qa_player_animation *x = &a->animations[i], *y = &b->animations[i];
        if (x->first_frame != y->first_frame || x->num_frames != y->num_frames || x->loop_frames != y->loop_frames ||
            x->frame_lerp != y->frame_lerp || x->initial_lerp != y->initial_lerp || x->reversed != y->reversed ||
            x->flipflop != y->flipflop || x->present != y->present) return false;
    }
    return true;
}
static bool animation(q3n_selected_media *o, const q3n_selected_media_request *r, qa_error *e)
{
    const q3n_selected_animation *character = r->character;
    if (character && (!character->content || !character->resource || !character->receipt || !character->config ||
        character->receipt->resource_id != qa_resource_id(character->resource) ||
        qa_resource_pool_find(qa_vfs_resources(character->content), character->receipt->resource_id) != character->resource ||
        !qa_vfs_acquisition_retained(character->content, character->receipt, e)))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected CHARACTER animation lacks its real source receipt");
    if (o->animation_resource) {
        if ((character != NULL) != o->character || (character &&
            (character->content != o->animation_content || character->resource != o->animation_resource ||
                !same_receipt(character->receipt, &o->animation_receipt) || !same_config(character->config, &o->animation_config))))
            return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 presenter changed its immutable CHARACTER animation owner");
        return qa_vfs_acquisition_retained(o->animation_content, &o->animation_receipt, e) && current(r, e);
    }
    qa_resource *resource = NULL; qa_vfs_acquisition receipt = {0}; qa_player_animation_config config = {0};
    qa_vfs *content = character ? character->content : o->options.content;
    bool ok;
    if (character) {
        ok = copy_receipt(character->receipt, &receipt, e);
        if (ok) { resource = (qa_resource *)character->resource; qa_resource_retain(resource); config = *character->config; }
    } else {
        ok = qa_vfs_acquire_receipt(content, "models/players/sarge/animation.cfg", &resource, &receipt, e) && current(r, e);
        if (ok) ok = parse_animation(qa_resource_bytes(resource), &config, e);
    }
    if (ok) ok = current(r, e);
    if (ok) {
        o->animation_content = content; o->animation_resource = resource; o->animation_receipt = receipt;
        o->animation_config = config; o->character = character != NULL; return true;
    }
    qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return false;
}
bool q3n_selected_media_create(const q3n_selected_media_options *options, q3n_selected_media **out, qa_error *e)
{
    if (!options || !out || *out || !options->content || !options->assets ||
        options->assets->options.provider.mounts != options->content ||
        options->assets->options.provider.family != QA_SCENE_Q3 || options->assets->options.select ||
        (options->product != QA_Q3_ARENA && options->product != QA_Q3_TEAM_ARENA))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media requires its genuine selected content registry");
    q3n_selected_media *o = calloc(1, sizeof(*o));
    if (!o) return q3p_fail(e, QA_ERROR_MEMORY, "Allocating selected Q3 weapon media cache");
    o->options = *options; *out = o; return true;
}
bool q3n_selected_media_idle(const q3n_selected_media *o) { return o && !o->busy; }
void q3n_selected_media_destroy(q3n_selected_media *o)
{
    if (!q3n_selected_media_idle(o)) return;
    qa_resource_release(o->animation_resource); qa_vfs_acquisition_dispose(&o->animation_receipt); free(o);
}
bool q3n_selected_media_options_read(const q3n_selected_media *o, q3n_selected_media_options *out, qa_error *e)
{
    if (!q3n_selected_media_idle(o) || !out) return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media inventory requires its idle owner");
    *out = o->options; return true;
}
static q3n_selected_weapon_media tuple(const q3n_selected_media *o, const q3n_selected_media_row *row)
{
    return (q3n_selected_weapon_media){.assets = o->options.assets, .gun = row->gun, .hands = row->hands,
        .barrel = row->barrel, .flash = row->flash, .invisibility = o->invisibility,
        .battle_weapon = o->battle_weapon, .quad_weapon = o->quad_weapon};
}
static bool model(q3n_selected_media *o, const q3n_selected_media_request *r,
    const char *path, int32_t *out, bool required, qa_error *e)
{
    return qa_q3_register_model(o->options.assets, path, out, e) && current(r, e) &&
        (!required || *out || q3p_fail(e, QA_ERROR_NOT_FOUND, "Model is absent from selected Q3 equipment content"));
}
bool q3n_selected_media_prepare(q3n_selected_media *o, const q3n_selected_media_request *r,
    q3n_selected_weapon_media *out, qa_error *e)
{
    if (!q3n_selected_media_idle(o) || !r || !r->current || !out || !qa_q3_assets_idle(o->options.assets))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media admission requires its genuine idle observation and registry");
    const qa_q3_item *item = q3n_selected_media_item(o->options.product, r->weapon);
    if (!item || !item->model || !*item->model) return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 weapon has no authored product model");
    if (!current(r, e)) return false;
    o->busy = true; q3n_selected_media_row row = o->rows[r->weapon]; bool ok = true; char path[128];
    if (!row.world_ready) {
        ok = model(o, r, item->model, &row.gun, true, e) &&
            q3n_selected_media_path(item->model, "_barrel.md3", path, e) && model(o, r, path, &row.barrel, false, e) &&
            q3n_selected_media_path(item->model, "_flash.md3", path, e) && model(o, r, path, &row.flash, false, e);
        if (ok) row.world_ready = true;
    }
    if (ok && !o->shaders_ready) {
        int32_t invisibility, battle, quad;
        ok = qa_q3_register_shader(o->options.assets, "powerups/invisibility", true, &invisibility, e) && current(r, e) &&
            qa_q3_register_shader(o->options.assets, "powerups/battleWeapon", true, &battle, e) && current(r, e) &&
            qa_q3_register_shader(o->options.assets, "powerups/quadWeapon", true, &quad, e) && current(r, e);
        if (ok) { o->invisibility = invisibility; o->battle_weapon = battle; o->quad_weapon = quad; o->shaders_ready = true; }
    }
    if (ok && r->view_required) {
        if (!row.view_ready) {
            ok = q3n_selected_media_path(item->model, "_hand.md3", path, e) && model(o, r, path, &row.hands, false, e);
            if (ok && !row.hands) { row.hands_fallback = true;
                ok = model(o, r, "models/weapons2/shotgun/shotgun_hand.md3", &row.hands, true, e); }
        }
        if (ok) ok = animation(o, r, e);
        if (ok) row.view_ready = true;
    }
    if (ok) ok = current(r, e);
    if (ok) { o->rows[r->weapon] = row; *out = tuple(o, &row); }
    o->busy = false; return ok;
}
bool q3n_selected_media_read(const q3n_selected_media *o, int32_t weapon,
    bool view_required, q3n_selected_weapon_media *out, qa_error *e)
{
    if (!q3n_selected_media_idle(o) || !out || !q3n_selected_media_item(o->options.product, weapon))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media read requires its exact authored weapon row");
    const q3n_selected_media_row *row = &o->rows[weapon];
    if (!row->world_ready || !o->shaders_ready || (view_required && (!row->view_ready || !o->animation_resource)))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 weapon media has not completed actual admission");
    if (!q3n_selected_media_valid(o, false, e)) return false;
    *out = tuple(o, row); return true;
}
bool q3n_selected_media_animation(const q3n_selected_media *o, q3n_selected_animation *out,
    bool *character, qa_error *e)
{
    if (!q3n_selected_media_idle(o) || !out || !character)
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 animation inventory requires its idle genuine owner");
    *out = (q3n_selected_animation){0}; *character = o->character;
    if (!o->animation_resource) return true;
    if (!qa_vfs_acquisition_retained(o->animation_content, &o->animation_receipt, e)) return false;
    *out = (q3n_selected_animation){o->animation_content, o->animation_resource, &o->animation_receipt, &o->animation_config};
    return true;
}
