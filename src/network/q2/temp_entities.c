#include "qa/network_q2_messages.h"
#include "qa/math.h"
#include "q2pro_internal.h"

bool qa_q2_read_dir(qa_net_reader *r, float out[3]) {
    qa_vec3 v;
    uint8_t index = qa_net_read_u8(r);
    if (r->failed) return false;
    if (!out || !qa_byte_normal(index, &v)) return qa_net_reader_fail(r, "Invalid Q2 direction index");
    out[0] = v.x; out[1] = v.y; out[2] = v.z;
    return true;
}

bool qa_q2_write_dir(qa_net_writer *w, const float v[3]) {
    uint8_t index;
    if (!v || !qa_normal_byte((qa_vec3){v[0], v[1], v[2]}, &index))
        return qa_net_writer_fail(w, "Invalid Q2 direction");
    return qa_net_write_u8(w, index);
}

bool qa_q2_game_position_read(qa_q2_codec *c, qa_net_reader *r, float out[3]) {
    if (!c || !out) return qa_net_reader_fail(r, "Missing Q2 game position or codec");
    bool int23 = c->protocol.kind == QA_NET_Q2PRO_36 && qa_q2pro_extensions_v2(c);
    bool floating = c->protocol.kind == QA_NET_Q2REPRO_1038 || c->protocol.kind == QA_NET_Q2PRIVATE_4038 ||
                    c->protocol.kind == QA_NET_Q2KEX_2023;
    if (!int23) return qa_q2_read_vec3(r, out, floating);
    for (unsigned i = 0; i < 3; ++i) {
        int32_t value;
        if (!qa_q2pro_read_int23(r, 0, &value)) return false;
        out[i] = (float)value * 0.125f;
    }
    return true;
}

bool qa_q2_game_position_write(qa_q2_codec *c, qa_net_writer *w, const float v[3]) {
    if (!c || !v) return qa_net_writer_fail(w, "Missing Q2 game position or codec");
    bool int23 = c->protocol.kind == QA_NET_Q2PRO_36 && qa_q2pro_extensions_v2(c);
    bool floating = c->protocol.kind == QA_NET_Q2REPRO_1038 || c->protocol.kind == QA_NET_Q2PRIVATE_4038 ||
                    c->protocol.kind == QA_NET_Q2KEX_2023;
    if (!int23) return qa_q2_write_vec3(w, v, floating);
    for (unsigned i = 0; i < 3; ++i) {
        double value = trunc((double)v[i] * 8.0);
        if (!isfinite(value) || value < -4194304 || value > 4194303)
            return qa_net_writer_fail(w, "Q2 game position exceeds int23");
        if (!qa_q2pro_write_int23(w, (int32_t)value, 0)) return false;
    }
    return true;
}

typedef struct temp_layout { size_t count; qa_q2_temp_field_name fields[7]; } temp_layout;
static const temp_layout impact = {2, {QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_DIRECTION}};
static const temp_layout splash = {4, {QA_Q2_TEMP_COUNT, QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_DIRECTION, QA_Q2_TEMP_COLOR}};
static const temp_layout trail = {2, {QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2}};
static const temp_layout position = {1, {QA_Q2_TEMP_POSITION1}};
static const temp_layout beam = {3, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2}};
static const temp_layout grapple = {4, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2, QA_Q2_TEMP_OFFSET}};
static const temp_layout lightning = {4, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_ENTITY2, QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2}};
static const temp_layout flashlight = {2, {QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_ENTITY1}};
static const temp_layout forcewall = {3, {QA_Q2_TEMP_POSITION1, QA_Q2_TEMP_POSITION2, QA_Q2_TEMP_COLOR}};
static const temp_layout steam = {7, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_COUNT, QA_Q2_TEMP_POSITION1,
                                     QA_Q2_TEMP_DIRECTION, QA_Q2_TEMP_COLOR, QA_Q2_TEMP_ENTITY2, QA_Q2_TEMP_TIME}};
static const temp_layout widow = {2, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_POSITION1}};
static const temp_layout power = {2, {QA_Q2_TEMP_ENTITY1, QA_Q2_TEMP_COUNT}};
static const temp_layout damage = {1, {QA_Q2_TEMP_COUNT}};

static const temp_layout *layout(uint8_t type, bool extended) {
    switch (type) {
    case QA_Q2_TE_BLOOD: case QA_Q2_TE_GUNSHOT: case QA_Q2_TE_SPARKS:
    case QA_Q2_TE_BULLET_SPARKS: case QA_Q2_TE_SCREEN_SPARKS: case QA_Q2_TE_SHIELD_SPARKS:
    case QA_Q2_TE_SHOTGUN: case QA_Q2_TE_BLASTER: case QA_Q2_TE_GREENBLOOD:
    case QA_Q2_TE_BLASTER2: case QA_Q2_TE_FLECHETTE: case QA_Q2_TE_HEATBEAM_SPARKS:
    case QA_Q2_TE_HEATBEAM_STEAM: case QA_Q2_TE_MOREBLOOD: case QA_Q2_TE_ELECTRIC_SPARKS:
    case QA_Q2_TE_BLUEHYPERBLASTER_2: case QA_Q2_TE_BERSERK_SLAM: return &impact;
    case QA_Q2_TE_SPLASH: case QA_Q2_TE_LASER_SPARKS:
    case QA_Q2_TE_WELDING_SPARKS: case QA_Q2_TE_TUNNEL_SPARKS: return &splash;
    case QA_Q2_TE_BLUEHYPERBLASTER: case QA_Q2_TE_RAILTRAIL: case QA_Q2_TE_RAILTRAIL2:
    case QA_Q2_TE_BUBBLETRAIL: case QA_Q2_TE_DEBUGTRAIL: case QA_Q2_TE_BUBBLETRAIL2:
    case QA_Q2_TE_BFG_LASER: case QA_Q2_TE_BFG_ZAP: return &trail;
    case QA_Q2_TE_GRENADE_EXPLOSION: case QA_Q2_TE_GRENADE_EXPLOSION_WATER:
    case QA_Q2_TE_EXPLOSION2: case QA_Q2_TE_PLASMA_EXPLOSION: case QA_Q2_TE_ROCKET_EXPLOSION:
    case QA_Q2_TE_ROCKET_EXPLOSION_WATER: case QA_Q2_TE_EXPLOSION1: case QA_Q2_TE_EXPLOSION1_NP:
    case QA_Q2_TE_EXPLOSION1_BIG: case QA_Q2_TE_BFG_EXPLOSION: case QA_Q2_TE_BFG_BIGEXPLOSION:
    case QA_Q2_TE_BOSSTPORT: case QA_Q2_TE_PLAIN_EXPLOSION: case QA_Q2_TE_CHAINFIST_SMOKE:
    case QA_Q2_TE_TRACKER_EXPLOSION: case QA_Q2_TE_TELEPORT_EFFECT: case QA_Q2_TE_DBALL_GOAL:
    case QA_Q2_TE_WIDOWSPLASH: case QA_Q2_TE_NUKEBLAST:
    case QA_Q2_TE_EXPLOSION1_NL: case QA_Q2_TE_EXPLOSION2_NL: return &position;
    case QA_Q2_TE_PARASITE_ATTACK: case QA_Q2_TE_MEDIC_CABLE_ATTACK: case QA_Q2_TE_HEATBEAM:
    case QA_Q2_TE_MONSTER_HEATBEAM: case QA_Q2_TE_GRAPPLE_CABLE_2: case QA_Q2_TE_LIGHTNING_BEAM: return &beam;
    case QA_Q2_TE_GRAPPLE_CABLE: return &grapple;
    case QA_Q2_TE_LIGHTNING: return &lightning;
    case QA_Q2_TE_FLASHLIGHT: return &flashlight;
    case QA_Q2_TE_FORCEWALL: return &forcewall;
    case QA_Q2_TE_STEAM: return &steam;
    case QA_Q2_TE_WIDOWBEAMOUT: return &widow;
    case QA_Q2_TE_POWER_SPLASH: return &power;
    case QA_Q2_TE_Q2PRO_DAMAGE_DEALT: return extended ? &damage : NULL;
    default: return NULL;
    }
}

static unsigned integer_bits(uint8_t type, qa_q2_temp_field_name name) {
    if (name == QA_Q2_TEMP_TIME) return 32;
    if (name == QA_Q2_TEMP_ENTITY1 || name == QA_Q2_TEMP_ENTITY2 || type == QA_Q2_TE_Q2PRO_DAMAGE_DEALT)
        return 16;
    return 8;
}

bool qa_q2_temp_entity_read(qa_q2_codec *c, qa_net_reader *r, bool extended, qa_q2_temp_entity *out) {
    if (!out || r->bit % 8) return qa_net_reader_fail(r, "Invalid Q2 temporary entity output or alignment");
    size_t start = r->bit / 8;
    qa_q2_temp_entity t = {0};
    t.type = qa_net_read_u8(r);
    const temp_layout *s = layout(t.type, extended);
    if (!s) return qa_net_reader_fail(r, "Unknown Q2 temporary entity type");
    t.field_count = s->count;
    for (size_t i = 0; i < t.field_count; ++i) {
        qa_q2_temp_field *f = &t.fields[i];
        f->name = s->fields[i];
        f->kind = f->name >= QA_Q2_TEMP_POSITION1 ? QA_Q2_TEMP_VECTOR : QA_Q2_TEMP_INTEGER;
        if (f->kind == QA_Q2_TEMP_VECTOR) {
            if (!(f->name == QA_Q2_TEMP_DIRECTION ? qa_q2_read_dir(r, f->value.vector) :
                  qa_q2_game_position_read(c, r, f->value.vector))) return false;
        } else {
            unsigned bits = integer_bits(t.type, f->name);
            f->value.integer = bits == 32 ? qa_net_read_i32(r) : bits == 16 ? qa_net_read_i16(r) : qa_net_read_u8(r);
            if (t.type == QA_Q2_TE_STEAM && i == 0 && f->value.integer == -1) --t.field_count;
        }
    }
    if (r->failed) return false;
    t.raw = (qa_bytes){r->bytes.data + start, r->bit / 8 - start};
    *out = t;
    return true;
}

bool qa_q2_temp_entity_write(qa_q2_codec *c, qa_net_writer *w, bool extended, const qa_q2_temp_entity *t) {
    if (!t) return qa_net_writer_fail(w, "Missing Q2 temporary entity");
    const temp_layout *s = layout(t->type, extended);
    if (!s) return qa_net_writer_fail(w, "Unknown Q2 temporary entity type");
    size_t count = s->count;
    if (t->type == QA_Q2_TE_STEAM && t->field_count && t->fields[0].value.integer == -1) --count;
    if (t->field_count != count) return qa_net_writer_fail(w, "Wrong Q2 temporary entity field count");
    for (size_t i = 0; i < count; ++i) {
        qa_q2_temp_field_kind kind = s->fields[i] >= QA_Q2_TEMP_POSITION1 ? QA_Q2_TEMP_VECTOR : QA_Q2_TEMP_INTEGER;
        if (t->fields[i].name != s->fields[i] || t->fields[i].kind != kind)
            return qa_net_writer_fail(w, "Wrong Q2 temporary entity field order or type");
    }
    qa_net_write_u8(w, t->type);
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_temp_field *f = &t->fields[i];
        if (f->kind == QA_Q2_TEMP_VECTOR) {
            if (!(f->name == QA_Q2_TEMP_DIRECTION ? qa_q2_write_dir(w, f->value.vector) :
                  qa_q2_game_position_write(c, w, f->value.vector))) return false;
        } else {
            int32_t value = f->value.integer;
            unsigned bits = integer_bits(t->type, f->name);
            if ((bits == 8 && (value < 0 || value > UINT8_MAX)) ||
                (bits == 16 && (value < INT16_MIN || value > INT16_MAX)))
                return qa_net_writer_fail(w, "Q2 temporary entity integer exceeds wire range");
            if (bits == 32) qa_net_write_i32(w, value);
            else if (bits == 16) qa_net_write_i16(w, (int16_t)value);
            else qa_net_write_u8(w, (uint8_t)value);
        }
    }
    return !w->failed;
}
