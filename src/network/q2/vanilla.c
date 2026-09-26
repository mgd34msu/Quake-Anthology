#include "classic_internal.h"

static const qa_q2_entity zero_entity;
static const qa_q2_player zero_player;

static bool signed_word_pattern(uint32_t value) {
    return value <= UINT16_MAX || value >= UINT32_C(0xffff8000);
}

static bool scaled_fits(float value, double scale, double low, double high) {
    double wire = trunc((double)value * scale);
    return isfinite(wire) && wire >= low && wire <= high;
}

bool qa_q2_classic_read_serverdata_body(qa_net_reader *r, qa_q2_serverdata *out) {
    qa_q2_serverdata d = {0};
    d.servercount = qa_net_read_i32(r);
    d.attractloop = qa_net_read_u8(r) != 0;
    qa_net_read_string(r, d.gamedir, sizeof(d.gamedir));
    d.clientnum = qa_net_read_i16(r);
    qa_net_read_string(r, d.levelname, sizeof(d.levelname));
    d.clientnums[0] = d.clientnum;
    d.client_count = 1;
    if (r->failed) return false;
    *out = d;
    return true;
}

bool qa_q2_classic_write_serverdata_body(qa_net_writer *w, const qa_q2_serverdata *d,
                                       uint32_t version) {
    if (!d || d->clientnum < INT16_MIN || d->clientnum > INT16_MAX || d->client_count > 1)
        return qa_net_writer_fail(w, "Invalid classic Q2 client slot");
    if (!memchr(d->gamedir, 0, sizeof(d->gamedir)) ||
        !memchr(d->levelname, 0, sizeof(d->levelname)))
        return qa_net_writer_fail(w, "Unterminated Q2 server data string");
    return qa_net_write_u8(w, Q2C_SVC_SERVERDATA) && qa_net_write_u32(w, version) &&
           qa_net_write_i32(w, d->servercount) && qa_net_write_u8(w, d->attractloop ? 1 : 0) &&
           qa_net_write_string(w, d->gamedir) && qa_net_write_i16(w, (int16_t)d->clientnum) &&
           qa_net_write_string(w, d->levelname);
}

static bool read_serverdata(qa_q2_codec *c, qa_net_reader *r, qa_q2_serverdata *d) {
    (void)c;
    if (!d) return qa_net_reader_fail(r, "Missing Q2 server data output");
    return qa_q2_classic_read_serverdata_body(r, d);
}

static bool write_serverdata(qa_q2_codec *c, qa_net_writer *w, const qa_q2_serverdata *d) {
    (void)c;
    return qa_q2_classic_write_serverdata_body(w, d, 34);
}

bool qa_q2_classic_read_entity_header(qa_q2_codec *c, qa_net_reader *r,
                                    uint32_t *number, uint64_t *bits) {
    uint32_t n;
    uint64_t b;
    if (!number || !bits) return qa_net_reader_fail(r, "Missing Q2 entity header output");
    if (!qa_q2_read_entity_header_common(c, r, &n, &b)) return false;
    if (n >= Q2C_MAX_EDICTS || (b & ~UINT64_C(0x0fffdfff)) || (!n && b))
        return qa_net_reader_fail(r, "Invalid classic Q2 entity header");
    *number = n;
    *bits = b;
    return true;
}

static uint32_t read_word(qa_net_reader *r, bool unsigned_words) {
    return unsigned_words ? qa_net_read_u16(r) : (uint32_t)(int32_t)qa_net_read_i16(r);
}

static uint32_t read_width(qa_net_reader *r, uint32_t bits, uint32_t byte_bit,
                           uint32_t word_bit, bool unsigned_words) {
    if ((bits & (byte_bit | word_bit)) == (byte_bit | word_bit)) return qa_net_read_u32(r);
    if (bits & byte_bit) return qa_net_read_u8(r);
    return read_word(r, unsigned_words);
}

bool qa_q2_classic_read_entity_fields(qa_net_reader *r, const qa_q2_entity *from,
                                     uint32_t number, uint64_t bits, qa_q2_entity *out,
                                     bool unsigned_words, bool long_solid) {
    if (!out || !number || number >= Q2C_MAX_EDICTS ||
        (bits & (~UINT64_C(0x0fffdfff) | Q2C_U_REMOVE)))
        return qa_net_reader_fail(r, "Invalid classic Q2 entity delta");
    const qa_q2_entity *f = from ? from : &zero_entity;
    qa_q2_entity t = *f;
    uint32_t b = (uint32_t)bits;
    t.number = number;
    memcpy(t.old_origin, f->origin, sizeof(t.old_origin));
    if (b & Q2C_U_MODEL) t.modelindex = qa_net_read_u8(r);
    if (b & Q2C_U_MODEL2) t.modelindex2 = qa_net_read_u8(r);
    if (b & Q2C_U_MODEL3) t.modelindex3 = qa_net_read_u8(r);
    if (b & Q2C_U_MODEL4) t.modelindex4 = qa_net_read_u8(r);
    if (b & Q2C_U_FRAME8) t.frame = qa_net_read_u8(r);
    if (b & Q2C_U_FRAME16) t.frame = read_word(r, unsigned_words);
    if (b & (Q2C_U_SKIN8 | Q2C_U_SKIN16))
        t.skinnum = read_width(r, b, Q2C_U_SKIN8, Q2C_U_SKIN16, unsigned_words);
    if (b & (Q2C_U_EFFECTS8 | Q2C_U_EFFECTS16))
        t.effects = read_width(r, b, Q2C_U_EFFECTS8, Q2C_U_EFFECTS16, unsigned_words);
    if (b & (Q2C_U_RENDERFX8 | Q2C_U_RENDERFX16))
        t.renderfx = read_width(r, b, Q2C_U_RENDERFX8, Q2C_U_RENDERFX16, unsigned_words);
    static const uint32_t origin_bits[3] = {Q2C_U_ORIGIN1, Q2C_U_ORIGIN2, Q2C_U_ORIGIN3};
    static const uint32_t angle_bits[3] = {Q2C_U_ANGLE1, Q2C_U_ANGLE2, Q2C_U_ANGLE3};
    for (unsigned i = 0; i < 3; ++i)
        if (b & origin_bits[i]) t.origin[i] = qa_q2_read_coord(r);
    for (unsigned i = 0; i < 3; ++i)
        if (b & angle_bits[i]) t.angles[i] = qa_q2_read_angle8(r);
    if (b & Q2C_U_OLDORIGIN) qa_q2_read_vec3(r, t.old_origin, false);
    if (b & Q2C_U_SOUND) t.sound = qa_net_read_u8(r);
    t.event = (b & Q2C_U_EVENT) ? qa_net_read_u8(r) : 0;
    if (b & Q2C_U_SOLID)
        t.solid = long_solid ? qa_net_read_u32(r) : (uint32_t)(int32_t)qa_net_read_i16(r);
    if (r->failed) return false;
    *out = t;
    return true;
}

static uint32_t width_bits(uint32_t value, uint32_t byte_bit, uint32_t word_bit,
                           uint32_t word_limit) {
    return value < 256 ? byte_bit : value < word_limit ? word_bit : byte_bit | word_bit;
}

static bool write_width(qa_net_writer *w, uint32_t value, uint32_t bits,
                         uint32_t byte_bit, uint32_t word_bit) {
    if ((bits & (byte_bit | word_bit)) == (byte_bit | word_bit)) return qa_net_write_u32(w, value);
    if (bits & byte_bit) return qa_net_write_u8(w, (uint8_t)value);
    if (bits & word_bit) return qa_net_write_u16(w, (uint16_t)value);
    return true;
}

bool qa_q2_classic_write_entity_fields(qa_q2_codec *c, qa_net_writer *w,
                                      const qa_q2_entity *from, const qa_q2_entity *t,
                                      bool force, bool fresh, bool r1_skin, bool long_solid) {
    if (!t || !t->number || t->number >= Q2C_MAX_EDICTS)
        return qa_net_writer_fail(w, "Invalid classic Q2 entity number");
    if (t->effects > UINT32_MAX)
        return qa_net_writer_fail(w, "Classic Q2 cannot encode 64-bit entity effects");
    if (t->modelindex > UINT8_MAX || t->modelindex2 > UINT8_MAX || t->modelindex3 > UINT8_MAX ||
        t->modelindex4 > UINT8_MAX || t->sound > UINT8_MAX || t->event > UINT8_MAX ||
        (r1_skin ? t->frame > UINT16_MAX : !signed_word_pattern(t->frame)) ||
        (!long_solid && !signed_word_pattern(t->solid)))
        return qa_net_writer_fail(w, "Classic Q2 entity fields exceed wire ranges");
    if (t->alpha != 0 || t->scale != 0 || t->loop_volume != 0 || t->loop_attenuation != 0 ||
        t->instance_bits || t->owner || t->old_frame || t->morefx)
        return qa_net_writer_fail(w, "Classic Q2 cannot represent extended entity fields");
    for (unsigned i = 0; i < 3; ++i)
        if (!scaled_fits(t->origin[i], 8, INT16_MIN, INT16_MAX) || !isfinite(t->angles[i]) ||
            ((fresh || (t->renderfx & Q2C_RF_BEAM)) && !scaled_fits(t->old_origin[i], 8, INT16_MIN, INT16_MAX)))
            return qa_net_writer_fail(w, "Classic Q2 entity coordinates exceed wire range");
    const qa_q2_entity *f = from ? from : &zero_entity;
    uint32_t b = t->number >= 256 ? Q2C_U_NUMBER16 : 0;
    static const uint32_t origin_bits[3] = {Q2C_U_ORIGIN1, Q2C_U_ORIGIN2, Q2C_U_ORIGIN3};
    static const uint32_t angle_bits[3] = {Q2C_U_ANGLE1, Q2C_U_ANGLE2, Q2C_U_ANGLE3};
    for (unsigned i = 0; i < 3; ++i) {
        if (t->origin[i] != f->origin[i]) b |= origin_bits[i];
        if (t->angles[i] != f->angles[i]) b |= angle_bits[i];
    }
    if (t->skinnum != f->skinnum)
        b |= width_bits(t->skinnum, Q2C_U_SKIN8, Q2C_U_SKIN16, r1_skin ? 32768 : 65536);
    if (t->frame != f->frame) b |= t->frame < 256 ? Q2C_U_FRAME8 : Q2C_U_FRAME16;
    if (t->effects != f->effects)
        b |= width_bits((uint32_t)t->effects, Q2C_U_EFFECTS8, Q2C_U_EFFECTS16, 32768);
    if (t->renderfx != f->renderfx)
        b |= width_bits(t->renderfx, Q2C_U_RENDERFX8, Q2C_U_RENDERFX16, 32768);
    if (t->solid != f->solid) b |= Q2C_U_SOLID;
    if (t->event) b |= Q2C_U_EVENT;
    if (t->modelindex != f->modelindex) b |= Q2C_U_MODEL;
    if (t->modelindex2 != f->modelindex2) b |= Q2C_U_MODEL2;
    if (t->modelindex3 != f->modelindex3) b |= Q2C_U_MODEL3;
    if (t->modelindex4 != f->modelindex4) b |= Q2C_U_MODEL4;
    if (t->sound != f->sound) b |= Q2C_U_SOUND;
    if (fresh || (t->renderfx & Q2C_RF_BEAM)) b |= Q2C_U_OLDORIGIN;
    if (!b && !force) return !w->failed;
    if (!qa_q2_write_entity_header_common(c, w, t->number, b)) return false;
    if (b & Q2C_U_MODEL) qa_net_write_u8(w, (uint8_t)t->modelindex);
    if (b & Q2C_U_MODEL2) qa_net_write_u8(w, (uint8_t)t->modelindex2);
    if (b & Q2C_U_MODEL3) qa_net_write_u8(w, (uint8_t)t->modelindex3);
    if (b & Q2C_U_MODEL4) qa_net_write_u8(w, (uint8_t)t->modelindex4);
    if (b & Q2C_U_FRAME8) qa_net_write_u8(w, (uint8_t)t->frame);
    if (b & Q2C_U_FRAME16) qa_net_write_u16(w, (uint16_t)t->frame);
    write_width(w, t->skinnum, b, Q2C_U_SKIN8, Q2C_U_SKIN16);
    write_width(w, (uint32_t)t->effects, b, Q2C_U_EFFECTS8, Q2C_U_EFFECTS16);
    write_width(w, t->renderfx, b, Q2C_U_RENDERFX8, Q2C_U_RENDERFX16);
    for (unsigned i = 0; i < 3; ++i)
        if (b & origin_bits[i]) qa_q2_write_coord(w, t->origin[i]);
    for (unsigned i = 0; i < 3; ++i)
        if (b & angle_bits[i]) qa_q2_write_angle8(w, t->angles[i]);
    if (b & Q2C_U_OLDORIGIN) qa_q2_write_vec3(w, t->old_origin, false);
    if (b & Q2C_U_SOUND) qa_net_write_u8(w, (uint8_t)t->sound);
    if (b & Q2C_U_EVENT) qa_net_write_u8(w, (uint8_t)t->event);
    if (b & Q2C_U_SOLID) {
        if (long_solid) qa_net_write_u32(w, t->solid);
        else qa_net_write_u16(w, (uint16_t)t->solid);
    }
    return !w->failed;
}

bool qa_q2_classic_write_entity_remove(qa_q2_codec *c, qa_net_writer *w, uint32_t n) {
    if (!n || n >= Q2C_MAX_EDICTS) return qa_net_writer_fail(w, "Invalid Q2 removed entity number");
    return qa_q2_write_entity_header_common(c, w, n, Q2C_U_REMOVE);
}

static bool read_entity(qa_q2_codec *c, qa_net_reader *r, const qa_q2_entity *f,
                         uint32_t n, uint64_t b, qa_q2_entity *t) {
    (void)c;
    return qa_q2_classic_read_entity_fields(r, f, n, b, t, false, false);
}

static bool write_entity(qa_q2_codec *c, qa_net_writer *w, const qa_q2_entity *f,
                          const qa_q2_entity *t, bool force, bool fresh) {
    return qa_q2_classic_write_entity_fields(c, w, f, t, force, fresh, false, false);
}

static bool different3(const float a[3], const float b[3]) {
    return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}

bool qa_q2_classic_player_valid(qa_net_writer *w, const qa_q2_player *t) {
    if (!t) return qa_net_writer_fail(w, "Missing classic Q2 player state");
    if (t->pmove.type < 0 || t->pmove.type > UINT8_MAX || t->pmove.time < 0 || t->pmove.time > UINT8_MAX ||
        t->pmove.flags < 0 || t->pmove.flags > UINT8_MAX || t->pmove.gravity < INT16_MIN ||
        t->pmove.gravity > INT16_MAX || t->gunindex > UINT8_MAX || t->gunframe > UINT8_MAX ||
        t->rdflags > UINT8_MAX || !scaled_fits(t->fov, 1, 0, UINT8_MAX))
        return qa_net_writer_fail(w, "Classic Q2 player fields exceed wire ranges");
    if (t->clientnum || t->pmove.viewheight || t->pmove.float_delta_angles || t->gunskin || t->gunrate ||
        t->team_id || t->fog.density || t->fog.sky_factor || t->fog.height_density || t->fog.height_falloff ||
        t->fog.height_start_distance || t->fog.height_end_distance)
        return qa_net_writer_fail(w, "Classic Q2 cannot represent extended player fields");
    for (unsigned i = 0; i < 3; ++i) {
        if (t->pmove.origin[i] < INT16_MIN || t->pmove.origin[i] > INT16_MAX ||
            t->pmove.velocity[i] < INT16_MIN || t->pmove.velocity[i] > INT16_MAX ||
            !isfinite(t->viewangles[i]) || !scaled_fits(t->viewoffset[i], 4, INT8_MIN, INT8_MAX) ||
            !scaled_fits(t->kick_angles[i], 4, INT8_MIN, INT8_MAX) ||
            !scaled_fits(t->gunoffset[i], 4, INT8_MIN, INT8_MAX) ||
            !scaled_fits(t->gunangles[i], 4, INT8_MIN, INT8_MAX))
            return qa_net_writer_fail(w, "Classic Q2 player vectors exceed wire ranges");
        if (t->fog.color[i] || t->fog.height_start_color[i] || t->fog.height_end_color[i])
            return qa_net_writer_fail(w, "Classic Q2 cannot represent player fog");
    }
    for (unsigned i = 0; i < 4; ++i)
        if (!scaled_fits(t->blend[i], 255, 0, UINT8_MAX) || t->damage_blend[i] != 0)
            return qa_net_writer_fail(w, "Classic Q2 cannot represent player blend");
    for (unsigned i = Q2C_STATS; i < QA_Q2_MAX_STATS; ++i)
        if (t->stats[i]) return qa_net_writer_fail(w, "Classic Q2 cannot represent extra stats");
    return !w->failed;
}

qa_q2_classic_player_delta qa_q2_classic_player_changes(const qa_q2_player *from,
                                                       const qa_q2_player *t, bool split) {
    const qa_q2_player *f = from ? from : &zero_player;
    qa_q2_classic_player_delta d = {Q2C_PS_WEAPONINDEX, 0, 0};
    if (t->pmove.type != f->pmove.type) d.flags |= Q2C_PS_TYPE;
    if (t->pmove.origin[0] != f->pmove.origin[0] || t->pmove.origin[1] != f->pmove.origin[1])
        d.flags |= Q2C_PS_ORIGIN;
    if (t->pmove.origin[2] != f->pmove.origin[2]) {
        if (split) d.extra |= Q2C_EPS_ORIGIN_Z;
        else d.flags |= Q2C_PS_ORIGIN;
    }
    if (t->pmove.velocity[0] != f->pmove.velocity[0] || t->pmove.velocity[1] != f->pmove.velocity[1])
        d.flags |= Q2C_PS_VELOCITY;
    if (t->pmove.velocity[2] != f->pmove.velocity[2]) {
        if (split) d.extra |= Q2C_EPS_VELOCITY_Z;
        else d.flags |= Q2C_PS_VELOCITY;
    }
    if (t->pmove.time != f->pmove.time) d.flags |= Q2C_PS_TIME;
    if (t->pmove.flags != f->pmove.flags) d.flags |= Q2C_PS_FLAGS;
    if (t->pmove.gravity != f->pmove.gravity) d.flags |= Q2C_PS_GRAVITY;
    for (unsigned i = 0; i < 3; ++i)
        if (t->pmove.delta_angles[i] != f->pmove.delta_angles[i]) d.flags |= Q2C_PS_DELTA_ANGLES;
    if (different3(t->viewoffset, f->viewoffset)) d.flags |= Q2C_PS_VIEWOFFSET;
    if (t->viewangles[0] != f->viewangles[0] || t->viewangles[1] != f->viewangles[1])
        d.flags |= Q2C_PS_VIEWANGLES;
    if (t->viewangles[2] != f->viewangles[2]) {
        if (split) d.extra |= Q2C_EPS_VIEWANGLE_Z;
        else d.flags |= Q2C_PS_VIEWANGLES;
    }
    if (different3(t->kick_angles, f->kick_angles)) d.flags |= Q2C_PS_KICKANGLES;
    for (unsigned i = 0; i < 4; ++i)
        if (t->blend[i] != f->blend[i]) d.flags |= Q2C_PS_BLEND;
    if (t->fov != f->fov) d.flags |= Q2C_PS_FOV;
    if (t->rdflags != f->rdflags) d.flags |= Q2C_PS_RDFLAGS;
    if (t->gunframe != f->gunframe) d.flags |= Q2C_PS_WEAPONFRAME;
    if (split) {
        if (different3(t->gunoffset, f->gunoffset)) d.extra |= Q2C_EPS_GUNOFFSET;
        if (different3(t->gunangles, f->gunangles)) d.extra |= Q2C_EPS_GUNANGLES;
    }
    for (unsigned i = 0; i < Q2C_STATS; ++i)
        if (t->stats[i] != f->stats[i]) d.stats |= UINT32_C(1) << i;
    if (d.stats) d.extra |= Q2C_EPS_STATS;
    return d;
}

static void read_quarter3(qa_net_reader *r, float v[3]) {
    for (unsigned i = 0; i < 3; ++i) v[i] = (float)qa_net_read_i8(r) * 0.25f;
}

static bool write_quarter3(qa_net_writer *w, const float v[3]) {
    for (unsigned i = 0; i < 3; ++i)
        if (!qa_q2_write_scaled(w, v[i], 4.0f, 8, true)) return false;
    return true;
}

bool qa_q2_classic_read_player_fields(qa_net_reader *r, const qa_q2_player *from,
                                     qa_q2_player *out, uint16_t flags, uint8_t extra,
                                     bool split) {
    if (!out || (flags & 0x8000u) || (extra & 0xc0u))
        return qa_net_reader_fail(r, "Invalid classic Q2 player flags");
    qa_q2_player t = from ? *from : zero_player;
    unsigned components = split ? 2 : 3;
    if (flags & Q2C_PS_TYPE) t.pmove.type = qa_net_read_u8(r);
    if (flags & Q2C_PS_ORIGIN)
        for (unsigned i = 0; i < components; ++i) t.pmove.origin[i] = qa_net_read_i16(r);
    if (split && (extra & Q2C_EPS_ORIGIN_Z)) t.pmove.origin[2] = qa_net_read_i16(r);
    if (flags & Q2C_PS_VELOCITY)
        for (unsigned i = 0; i < components; ++i) t.pmove.velocity[i] = qa_net_read_i16(r);
    if (split && (extra & Q2C_EPS_VELOCITY_Z)) t.pmove.velocity[2] = qa_net_read_i16(r);
    if (flags & Q2C_PS_TIME) t.pmove.time = qa_net_read_u8(r);
    if (flags & Q2C_PS_FLAGS) t.pmove.flags = qa_net_read_u8(r);
    if (flags & Q2C_PS_GRAVITY) t.pmove.gravity = qa_net_read_i16(r);
    if (flags & Q2C_PS_DELTA_ANGLES)
        for (unsigned i = 0; i < 3; ++i) t.pmove.delta_angles[i] = qa_net_read_i16(r);
    if (flags & Q2C_PS_VIEWOFFSET) read_quarter3(r, t.viewoffset);
    if (flags & Q2C_PS_VIEWANGLES)
        for (unsigned i = 0; i < components; ++i) t.viewangles[i] = qa_q2_read_angle16(r);
    if (split && (extra & Q2C_EPS_VIEWANGLE_Z)) t.viewangles[2] = qa_q2_read_angle16(r);
    if (flags & Q2C_PS_KICKANGLES) read_quarter3(r, t.kick_angles);
    if (flags & Q2C_PS_WEAPONINDEX) t.gunindex = qa_net_read_u8(r);
    if (flags & Q2C_PS_WEAPONFRAME) t.gunframe = qa_net_read_u8(r);
    if (split ? (extra & Q2C_EPS_GUNOFFSET) != 0 : (flags & Q2C_PS_WEAPONFRAME) != 0)
        read_quarter3(r, t.gunoffset);
    if (split ? (extra & Q2C_EPS_GUNANGLES) != 0 : (flags & Q2C_PS_WEAPONFRAME) != 0)
        read_quarter3(r, t.gunangles);
    if (flags & Q2C_PS_BLEND)
        for (unsigned i = 0; i < 4; ++i) t.blend[i] = (float)qa_net_read_u8(r) / 255.0f;
    if (flags & Q2C_PS_FOV) t.fov = (float)qa_net_read_u8(r);
    if (flags & Q2C_PS_RDFLAGS) t.rdflags = qa_net_read_u8(r);
    if (!split || (extra & Q2C_EPS_STATS)) {
        uint32_t stats = qa_net_read_u32(r);
        for (unsigned i = 0; i < Q2C_STATS; ++i)
            if (stats & (UINT32_C(1) << i)) t.stats[i] = qa_net_read_i16(r);
    }
    if (r->failed) return false;
    *out = t;
    return true;
}

bool qa_q2_classic_write_player_fields(qa_net_writer *w, const qa_q2_player *t,
                                      qa_q2_classic_player_delta d, bool split) {
    unsigned components = split ? 2 : 3;
    if (d.flags & Q2C_PS_TYPE) qa_net_write_u8(w, (uint8_t)t->pmove.type);
    if (d.flags & Q2C_PS_ORIGIN)
        for (unsigned i = 0; i < components; ++i) qa_net_write_u16(w, (uint16_t)t->pmove.origin[i]);
    if (split && (d.extra & Q2C_EPS_ORIGIN_Z)) qa_net_write_u16(w, (uint16_t)t->pmove.origin[2]);
    if (d.flags & Q2C_PS_VELOCITY)
        for (unsigned i = 0; i < components; ++i) qa_net_write_u16(w, (uint16_t)t->pmove.velocity[i]);
    if (split && (d.extra & Q2C_EPS_VELOCITY_Z)) qa_net_write_u16(w, (uint16_t)t->pmove.velocity[2]);
    if (d.flags & Q2C_PS_TIME) qa_net_write_u8(w, (uint8_t)t->pmove.time);
    if (d.flags & Q2C_PS_FLAGS) qa_net_write_u8(w, (uint8_t)t->pmove.flags);
    if (d.flags & Q2C_PS_GRAVITY) qa_net_write_u16(w, (uint16_t)t->pmove.gravity);
    if (d.flags & Q2C_PS_DELTA_ANGLES)
        for (unsigned i = 0; i < 3; ++i) qa_net_write_i16(w, t->pmove.delta_angles[i]);
    if (d.flags & Q2C_PS_VIEWOFFSET) write_quarter3(w, t->viewoffset);
    if (d.flags & Q2C_PS_VIEWANGLES)
        for (unsigned i = 0; i < components; ++i) qa_q2_write_angle16(w, t->viewangles[i]);
    if (split && (d.extra & Q2C_EPS_VIEWANGLE_Z)) qa_q2_write_angle16(w, t->viewangles[2]);
    if (d.flags & Q2C_PS_KICKANGLES) write_quarter3(w, t->kick_angles);
    if (d.flags & Q2C_PS_WEAPONINDEX) qa_net_write_u8(w, (uint8_t)t->gunindex);
    if (d.flags & Q2C_PS_WEAPONFRAME) qa_net_write_u8(w, (uint8_t)t->gunframe);
    if (split ? (d.extra & Q2C_EPS_GUNOFFSET) != 0 : (d.flags & Q2C_PS_WEAPONFRAME) != 0)
        write_quarter3(w, t->gunoffset);
    if (split ? (d.extra & Q2C_EPS_GUNANGLES) != 0 : (d.flags & Q2C_PS_WEAPONFRAME) != 0)
        write_quarter3(w, t->gunangles);
    if (d.flags & Q2C_PS_BLEND)
        for (unsigned i = 0; i < 4; ++i) qa_q2_write_scaled(w, t->blend[i], 255.0f, 8, false);
    if (d.flags & Q2C_PS_FOV) qa_q2_write_scaled(w, t->fov, 1.0f, 8, false);
    if (d.flags & Q2C_PS_RDFLAGS) qa_net_write_u8(w, (uint8_t)t->rdflags);
    if (!split || (d.extra & Q2C_EPS_STATS)) {
        qa_net_write_u32(w, d.stats);
        for (unsigned i = 0; i < Q2C_STATS; ++i)
            if (d.stats & (UINT32_C(1) << i)) qa_net_write_i16(w, t->stats[i]);
    }
    return !w->failed;
}

static bool read_player(qa_q2_codec *c, qa_net_reader *r, const qa_q2_player *f, qa_q2_player *t) {
    if (c->frame_player_pending) {
        c->frame_player_pending = false;
        if (qa_net_read_u8(r) != Q2C_SVC_PLAYERINFO)
            return qa_net_reader_fail(r, "Expected Q2 frame playerinfo");
    }
    uint16_t flags = qa_net_read_u16(r);
    return qa_q2_classic_read_player_fields(r, f, t, flags, 0, false);
}

static bool write_player(qa_q2_codec *c, qa_net_writer *w, const qa_q2_player *f,
                          const qa_q2_player *t) {
    (void)c;
    if (!qa_q2_classic_player_valid(w, t)) return false;
    qa_q2_classic_player_delta d = qa_q2_classic_player_changes(f, t, false);
    return qa_net_write_u8(w, Q2C_SVC_PLAYERINFO) && qa_net_write_u16(w, d.flags) &&
           qa_q2_classic_write_player_fields(w, t, d, false);
}

static bool read_frame_header(qa_q2_codec *c, qa_net_reader *r, qa_q2_frame_header *out) {
    if (!out) return qa_net_reader_fail(r, "Missing Q2 frame output");
    qa_q2_frame_header h = {0};
    c->frame_player_pending = false;
    h.serverframe = qa_net_read_i32(r);
    h.deltaframe = qa_net_read_i32(r);
    if (!c->demo26) h.suppress_count = qa_net_read_u8(r);
    h.areabytes = qa_net_read_u8(r);
    if (!qa_net_read_data(r, h.areabits, h.areabytes)) return false;
    *out = h;
    c->frame_player_pending = true;
    return true;
}

static bool write_frame(qa_q2_codec *c, qa_net_writer *w, const qa_q2_frame_header *h,
                         const qa_q2_player *f, const qa_q2_player *t,
                         qa_q2_write_entities_fn entities, void *user) {
    if (!h || !t || !entities || h->areabytes > UINT8_MAX)
        return qa_net_writer_fail(w, "Invalid classic Q2 frame");
    if (!qa_q2_classic_player_valid(w, t)) return false;
    if (!qa_net_write_u8(w, Q2C_SVC_FRAME) || !qa_net_write_i32(w, h->serverframe) ||
        !qa_net_write_i32(w, h->deltaframe) || !qa_net_write_u8(w, h->suppress_count) ||
        !qa_net_write_u8(w, (uint8_t)h->areabytes) ||
        !qa_net_write_data(w, h->areabits, h->areabytes) || !write_player(c, w, f, t)) return false;
    if (!entities(user, w, w->error)) return qa_net_writer_fail(w, "Q2 frame entity encoding failed");
    return !w->failed;
}

const qa_q2_codec_ops qa_q2_vanilla_ops = {
    .read_serverdata = read_serverdata,
    .write_serverdata = write_serverdata,
    .read_entity_header = qa_q2_classic_read_entity_header,
    .read_entity = read_entity,
    .write_entity = write_entity,
    .write_entity_remove = qa_q2_classic_write_entity_remove,
    .read_player = read_player,
    .write_player = write_player,
    .read_frame_header = read_frame_header,
    .write_frame = write_frame,
    .read_usercmd = qa_q2_classic_read_usercmd,
    .write_usercmd = qa_q2_classic_write_usercmd
};
