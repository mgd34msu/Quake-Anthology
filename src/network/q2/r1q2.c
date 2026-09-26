#include "classic_internal.h"

enum {
    R1Q2_UCMD = 1904, R1Q2_LONG_SOLID = 1905,
    CM_ANGLE1 = 1u << 0, CM_ANGLE2 = 1u << 1, CM_ANGLE3 = 1u << 2,
    CM_FORWARD = 1u << 3, CM_SIDE = 1u << 4, CM_UP = 1u << 5,
    CM_BUTTONS = 1u << 6, CM_IMPULSE = 1u << 7,
    BUTTON_FORWARD = 1u << 2, BUTTON_SIDE = 1u << 3, BUTTON_UP = 1u << 4,
    BUTTON_ANGLE1 = 1u << 5, BUTTON_ANGLE2 = 1u << 6,
    BUTTON_PACKED = BUTTON_FORWARD | BUTTON_SIDE | BUTTON_UP | BUTTON_ANGLE1 | BUTTON_ANGLE2
};

static uint32_t revision(const qa_q2_codec *c) {
    return c->protocol.revision ? c->protocol.revision : R1Q2_LONG_SOLID;
}

static bool read_serverdata(qa_q2_codec *c, qa_net_reader *r, qa_q2_serverdata *out) {
    (void)c;
    if (!out) return qa_net_reader_fail(r, "Missing R1Q2 server data output");
    qa_q2_serverdata d;
    if (!qa_q2_classic_read_serverdata_body(r, &d)) return false;
    qa_net_read_u8(r);
    int16_t minor = qa_net_read_i16(r);
    qa_net_read_u8(r);
    d.strafejump_hack = qa_net_read_u8(r) != 0;
    if (r->failed) return false;
    if (minor < 0) return qa_net_reader_fail(r, "Invalid R1Q2 protocol revision");
    d.protocol_revision = (uint32_t)minor;
    *out = d;
    return true;
}

static bool write_serverdata(qa_q2_codec *c, qa_net_writer *w, const qa_q2_serverdata *d) {
    if (!d) return qa_net_writer_fail(w, "Missing R1Q2 server data");
    uint32_t minor = d->protocol_revision ? d->protocol_revision : revision(c);
    if (minor > INT16_MAX) return qa_net_writer_fail(w, "Invalid R1Q2 protocol revision");
    return qa_q2_classic_write_serverdata_body(w, d, 35) && qa_net_write_u8(w, 0) &&
           qa_net_write_u16(w, (uint16_t)minor) && qa_net_write_u8(w, 0) &&
           qa_net_write_u8(w, d->strafejump_hack ? 1 : 0);
}

static bool read_entity(qa_q2_codec *c, qa_net_reader *r, const qa_q2_entity *f,
                         uint32_t n, uint64_t b, qa_q2_entity *t) {
    return qa_q2_classic_read_entity_fields(r, f, n, b, t, true, revision(c) >= R1Q2_LONG_SOLID);
}

static bool write_entity(qa_q2_codec *c, qa_net_writer *w, const qa_q2_entity *f,
                          const qa_q2_entity *t, bool force, bool fresh) {
    return qa_q2_classic_write_entity_fields(c, w, f, t, force, fresh, true,
                                            revision(c) >= R1Q2_LONG_SOLID);
}

static bool read_player(qa_q2_codec *c, qa_net_reader *r, const qa_q2_player *f, qa_q2_player *t) {
    uint16_t flags = qa_net_read_u16(r);
    uint8_t extra;
    if (c->frame_player_pending) {
        c->frame_player_pending = false;
        extra = (uint8_t)c->frame_extra;
    } else {
        extra = qa_net_read_u8(r);
    }
    return qa_q2_classic_read_player_fields(r, f, t, flags, extra, true);
}

static bool write_player(qa_q2_codec *c, qa_net_writer *w, const qa_q2_player *f,
                          const qa_q2_player *t) {
    (void)c;
    if (!qa_q2_classic_player_valid(w, t)) return false;
    qa_q2_classic_player_delta d = qa_q2_classic_player_changes(f, t, true);
    return qa_net_write_u16(w, d.flags) && qa_net_write_u8(w, d.extra) &&
           qa_q2_classic_write_player_fields(w, t, d, true);
}

static bool read_frame_header(qa_q2_codec *c, qa_net_reader *r, qa_q2_frame_header *out) {
    if (!out) return qa_net_reader_fail(r, "Missing R1Q2 frame output");
    qa_q2_frame_header h = {0};
    c->frame_player_pending = false;
    uint32_t packed = qa_net_read_u32(r);
    uint32_t offset = packed >> 27;
    h.serverframe = (int32_t)(packed & UINT32_C(0x07ffffff));
    h.deltaframe = offset == 31 ? -1 : h.serverframe - (int32_t)offset;
    uint8_t suppress = qa_net_read_u8(r);
    uint32_t extra = (c->frame_extra >> 1) | (suppress >> 4);
    h.suppress_count = suppress & 15;
    h.areabytes = qa_net_read_u8(r);
    if (!qa_net_read_data(r, h.areabits, h.areabytes)) return false;
    if (extra & ~UINT32_C(0x3f)) return qa_net_reader_fail(r, "Unsupported R1Q2 frame extra flags");
    *out = h;
    c->frame_extra = extra;
    c->frame_player_pending = true;
    return true;
}

static bool write_frame(qa_q2_codec *c, qa_net_writer *w, const qa_q2_frame_header *h,
                         const qa_q2_player *f, const qa_q2_player *t,
                         qa_q2_write_entities_fn entities, void *user) {
    (void)c;
    if (!h || !t || !entities || h->areabytes > UINT8_MAX)
        return qa_net_writer_fail(w, "Invalid R1Q2 frame");
    if (!qa_q2_classic_player_valid(w, t)) return false;
    int64_t distance = (int64_t)h->serverframe - h->deltaframe;
    if (h->deltaframe != -1 && (distance < 0 || distance > 30))
        return qa_net_writer_fail(w, "R1Q2 frame delta exceeds its five-bit window");
    uint32_t offset = h->deltaframe == -1 ? 31 : (uint32_t)distance;
    uint32_t packed = ((uint32_t)h->serverframe & UINT32_C(0x07ffffff)) | (offset << 27);
    qa_q2_classic_player_delta d = qa_q2_classic_player_changes(f, t, true);
    uint8_t opcode = (uint8_t)(Q2C_SVC_FRAME | ((d.extra & 0xf0u) << 1));
    uint8_t suppress = (uint8_t)((h->suppress_count & 15u) | ((d.extra & 15u) << 4));
    if (!qa_net_write_u8(w, opcode) || !qa_net_write_u32(w, packed) ||
        !qa_net_write_u8(w, suppress) || !qa_net_write_u8(w, (uint8_t)h->areabytes) ||
        !qa_net_write_data(w, h->areabits, h->areabytes) || !qa_net_write_u16(w, d.flags) ||
        !qa_q2_classic_write_player_fields(w, t, d, true)) return false;
    if (!entities(user, w, w->error)) return qa_net_writer_fail(w, "R1Q2 frame entity encoding failed");
    return !w->failed;
}

static bool packed_byte(float value, int divisor) {
    float quotient = value / (float)divisor;
    return quotient >= INT8_MIN && quotient <= INT8_MAX && truncf(quotient) == quotient;
}

static bool command_move(float value) {
    return isfinite(value) && value >= INT16_MIN && value <= INT16_MAX && truncf(value) == value;
}

static bool read_usercmd(qa_q2_codec *c, qa_net_reader *r, const qa_q2_usercmd *from,
                         qa_q2_usercmd *out) {
    if (!out) return qa_net_reader_fail(r, "Missing R1Q2 command output");
    qa_q2_usercmd t = {0};
    if (from) t = *from;
    bool compressed = revision(c) >= R1Q2_UCMD;
    uint8_t b = qa_net_read_u8(r);
    uint8_t buttons = compressed && (b & CM_BUTTONS) ? qa_net_read_u8(r) : 0;
    if (b & CM_ANGLE1)
        t.angles[0] = (buttons & BUTTON_ANGLE1) ? (int16_t)(qa_net_read_i8(r) * 64) : qa_net_read_i16(r);
    if (b & CM_ANGLE2)
        t.angles[1] = (buttons & BUTTON_ANGLE2) ? (int16_t)(qa_net_read_i8(r) * 256) : qa_net_read_i16(r);
    if (b & CM_ANGLE3) t.angles[2] = qa_net_read_i16(r);
    if (b & CM_FORWARD)
        t.forwardmove = (buttons & BUTTON_FORWARD) ? (int16_t)(qa_net_read_i8(r) * 5) : qa_net_read_i16(r);
    if (b & CM_SIDE)
        t.sidemove = (buttons & BUTTON_SIDE) ? (int16_t)(qa_net_read_i8(r) * 5) : qa_net_read_i16(r);
    if (b & CM_UP)
        t.upmove = (buttons & BUTTON_UP) ? (int16_t)(qa_net_read_i8(r) * 5) : qa_net_read_i16(r);
    if (!compressed && (b & CM_BUTTONS)) buttons = qa_net_read_u8(r);
    if (b & CM_IMPULSE) t.impulse = qa_net_read_u8(r);
    t.msec = qa_net_read_u8(r);
    t.lightlevel = qa_net_read_u8(r);
    if (b & CM_BUTTONS) t.buttons = (uint8_t)(buttons & (uint8_t)~BUTTON_PACKED);
    if (r->failed) return false;
    *out = t;
    return true;
}

static bool write_usercmd(qa_q2_codec *c, qa_net_writer *w, const qa_q2_usercmd *from,
                          const qa_q2_usercmd *t) {
    static const qa_q2_usercmd zero;
    const qa_q2_usercmd *f = from ? from : &zero;
    if (!t) return qa_net_writer_fail(w, "Missing R1Q2 command");
    if (t->server_frame != f->server_frame)
        return qa_net_writer_fail(w, "R1Q2 command cannot carry a server frame");
    if (t->buttons & BUTTON_PACKED)
        return qa_net_writer_fail(w, "R1Q2 command uses reserved compression buttons");
    if (!command_move(t->forwardmove) || !command_move(t->sidemove) || !command_move(t->upmove))
        return qa_net_writer_fail(w, "R1Q2 command movement requires signed 16-bit integers");
    bool compressed = revision(c) >= R1Q2_UCMD;
    uint8_t b = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (t->angles[i] != f->angles[i]) b |= (uint8_t)(1u << i);
    if (t->forwardmove != f->forwardmove) b |= CM_FORWARD;
    if (t->sidemove != f->sidemove) b |= CM_SIDE;
    if (t->upmove != f->upmove) b |= CM_UP;
    if (t->buttons != f->buttons) b |= CM_BUTTONS;
    if (t->impulse != f->impulse) b |= CM_IMPULSE;
    uint8_t packed = 0;
    if (compressed && (b & CM_BUTTONS)) {
        if ((b & CM_FORWARD) && packed_byte(t->forwardmove, 5)) packed |= BUTTON_FORWARD;
        if ((b & CM_SIDE) && packed_byte(t->sidemove, 5)) packed |= BUTTON_SIDE;
        if ((b & CM_UP) && packed_byte(t->upmove, 5)) packed |= BUTTON_UP;
        /* R1Q2's original angle1 compressor leaves -128 in the short form. */
        if ((b & CM_ANGLE1) && packed_byte(t->angles[0], 64) && t->angles[0] / 64 != INT8_MIN)
            packed |= BUTTON_ANGLE1;
        if ((b & CM_ANGLE2) && packed_byte(t->angles[1], 256)) packed |= BUTTON_ANGLE2;
    }
    qa_net_write_u8(w, b);
    if (compressed && (b & CM_BUTTONS)) qa_net_write_u8(w, (uint8_t)(t->buttons | packed));
    if (b & CM_ANGLE1) {
        if (packed & BUTTON_ANGLE1) qa_net_write_i8(w, (int8_t)(t->angles[0] / 64));
        else qa_net_write_i16(w, t->angles[0]);
    }
    if (b & CM_ANGLE2) {
        if (packed & BUTTON_ANGLE2) qa_net_write_i8(w, (int8_t)(t->angles[1] / 256));
        else qa_net_write_i16(w, t->angles[1]);
    }
    if (b & CM_ANGLE3) qa_net_write_i16(w, t->angles[2]);
    if (b & CM_FORWARD) {
        if (packed & BUTTON_FORWARD) qa_net_write_i8(w, (int8_t)(t->forwardmove / 5));
        else qa_net_write_i16(w, (int16_t)t->forwardmove);
    }
    if (b & CM_SIDE) {
        if (packed & BUTTON_SIDE) qa_net_write_i8(w, (int8_t)(t->sidemove / 5));
        else qa_net_write_i16(w, (int16_t)t->sidemove);
    }
    if (b & CM_UP) {
        if (packed & BUTTON_UP) qa_net_write_i8(w, (int8_t)(t->upmove / 5));
        else qa_net_write_i16(w, (int16_t)t->upmove);
    }
    if (!compressed && (b & CM_BUTTONS)) qa_net_write_u8(w, t->buttons);
    if (b & CM_IMPULSE) qa_net_write_u8(w, t->impulse);
    qa_net_write_u8(w, t->msec);
    qa_net_write_u8(w, t->lightlevel);
    return !w->failed;
}

const qa_q2_codec_ops qa_q2_r1q2_ops = {
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
    .read_usercmd = read_usercmd,
    .write_usercmd = write_usercmd
};
