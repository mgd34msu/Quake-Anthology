#include "qa/network_q3.h"

#include <float.h>
#include <limits.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Q3 state deltas require binary32 floats");

typedef struct net_field { size_t offset; int bits; } net_field;
#define ENTITY(field, width) { offsetof(qa_q3_entity, field), width }
static const net_field entity_fields[] = {
    ENTITY(pos.time, 32), ENTITY(pos.base[0], 0), ENTITY(pos.base[1], 0),
    ENTITY(pos.delta[0], 0), ENTITY(pos.delta[1], 0), ENTITY(pos.base[2], 0),
    ENTITY(apos.base[1], 0), ENTITY(pos.delta[2], 0), ENTITY(apos.base[0], 0),
    ENTITY(event, 10), ENTITY(angles2[1], 0), ENTITY(eType, 8),
    ENTITY(torsoAnim, 8), ENTITY(eventParm, 8), ENTITY(legsAnim, 8),
    ENTITY(groundEntityNum, 10), ENTITY(pos.type, 8), ENTITY(eFlags, 19),
    ENTITY(otherEntityNum, 10), ENTITY(weapon, 8), ENTITY(clientNum, 8),
    ENTITY(angles[1], 0), ENTITY(pos.duration, 32), ENTITY(apos.type, 8),
    ENTITY(origin[0], 0), ENTITY(origin[1], 0), ENTITY(origin[2], 0),
    ENTITY(solid, 24), ENTITY(powerups, 16), ENTITY(modelindex, 8),
    ENTITY(otherEntityNum2, 10), ENTITY(loopSound, 8), ENTITY(generic1, 8),
    ENTITY(origin2[2], 0), ENTITY(origin2[0], 0), ENTITY(origin2[1], 0),
    ENTITY(modelindex2, 8), ENTITY(angles[0], 0), ENTITY(time, 32),
    ENTITY(apos.time, 32), ENTITY(apos.duration, 32), ENTITY(apos.base[2], 0),
    ENTITY(apos.delta[0], 0), ENTITY(apos.delta[1], 0), ENTITY(apos.delta[2], 0),
    ENTITY(time2, 32), ENTITY(angles[2], 0), ENTITY(angles2[0], 0),
    ENTITY(angles2[2], 0), ENTITY(constantLight, 32), ENTITY(frame, 16)
};
#undef ENTITY
#define PLAYER(field, width) { offsetof(qa_q3_player, field), width }
static const net_field player_fields[] = {
    PLAYER(commandTime, 32), PLAYER(origin[0], 0), PLAYER(origin[1], 0),
    PLAYER(bobCycle, 8), PLAYER(velocity[0], 0), PLAYER(velocity[1], 0),
    PLAYER(viewangles[1], 0), PLAYER(viewangles[0], 0), PLAYER(weaponTime, -16),
    PLAYER(origin[2], 0), PLAYER(velocity[2], 0), PLAYER(legsTimer, 8),
    PLAYER(pmTime, -16), PLAYER(eventSequence, 16), PLAYER(torsoAnim, 8),
    PLAYER(movementDir, 4), PLAYER(events[0], 8), PLAYER(legsAnim, 8),
    PLAYER(events[1], 8), PLAYER(pmFlags, 16), PLAYER(groundEntityNum, 10),
    PLAYER(weaponState, 4), PLAYER(eFlags, 16), PLAYER(externalEvent, 10),
    PLAYER(gravity, 16), PLAYER(speed, 16), PLAYER(deltaAngles[1], 16),
    PLAYER(externalEventParm, 8), PLAYER(viewheight, -8), PLAYER(damageEvent, 8),
    PLAYER(damageYaw, 8), PLAYER(damagePitch, 8), PLAYER(damageCount, 8),
    PLAYER(generic1, 8), PLAYER(pmType, 8), PLAYER(deltaAngles[0], 16),
    PLAYER(deltaAngles[2], 16), PLAYER(torsoTimer, 12), PLAYER(eventParms[0], 8),
    PLAYER(eventParms[1], 8), PLAYER(clientNum, 8), PLAYER(weapon, 5),
    PLAYER(viewangles[2], 0), PLAYER(grapplePoint[0], 0), PLAYER(grapplePoint[1], 0),
    PLAYER(grapplePoint[2], 0), PLAYER(jumppadEnt, 10), PLAYER(loopSound, 16)
};
#undef PLAYER
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
_Static_assert(ARRAY_COUNT(entity_fields) == 51, "Q3 entity wire field count");
_Static_assert(ARRAY_COUNT(player_fields) == 48, "Q3 player wire field count");

static int32_t signed_word(uint32_t word)
{
    return word <= INT32_MAX ? (int32_t)word : -1 - (int32_t)(UINT32_MAX - word);
}

static uint32_t field_word(const void *state, const net_field *field)
{
    uint32_t word;
    memcpy(&word, (const unsigned char *)state + field->offset, sizeof(word));
    return word;
}

static void set_word(void *state, const net_field *field, uint32_t word)
{
    unsigned char *destination = (unsigned char *)state + field->offset;
    if (field->bits == 0) memcpy(destination, &word, sizeof(word));
    else {
        int32_t value = signed_word(word);
        memcpy(destination, &value, sizeof(value));
    }
}

static size_t last_changed(const net_field *fields, size_t count,
                           const void *from, const void *to)
{
    while (count != 0 && field_word(from, &fields[count - 1]) == field_word(to, &fields[count - 1]))
        --count;
    return count;
}

static bool write_field(qa_q3_writer *writer, const net_field *field,
                        const void *from, const void *to, bool entity)
{
    uint32_t value = field_word(to, field);
    bool changed = value != field_word(from, field);
    if (!qa_q3_write_bits(writer, changed ? 1u : 0u, 1)) return false;
    if (!changed) return true;
    float scalar = 0;
    if (field->bits == 0) memcpy(&scalar, &value, sizeof(scalar));
    if (entity) {
        bool zero = field->bits == 0 ? scalar == 0.0f : value == 0;
        if (!qa_q3_write_bits(writer, zero ? 0u : 1u, 1)) return false;
        if (zero) return true;
    }
    if (field->bits != 0) return qa_q3_write_bits(writer, value, field->bits);
    /* Range first, so neither NaNs nor out-of-range values reach the cast. */
    bool compact = scalar >= -4096.0f && scalar < 4096.0f;
    int32_t integer = compact ? (int32_t)scalar : 0;
    compact = compact && scalar == (float)integer;
    if (!qa_q3_write_bits(writer, compact ? 0u : 1u, 1)) return false;
    return qa_q3_write_bits(writer, compact ? (uint32_t)(integer + 4096) : value,
                            compact ? 13 : 32);
}

static bool read_field(qa_q3_reader *reader, const net_field *field, void *state, bool entity)
{
    if (qa_q3_read_bits(reader, 1) == 0) return !reader->raw.failed;
    uint32_t value;
    if (entity && qa_q3_read_bits(reader, 1) == 0) value = 0;
    else if (field->bits != 0) value = qa_q3_read_bits(reader, field->bits);
    else if (qa_q3_read_bits(reader, 1) == 0) {
        float scalar = (float)((int32_t)qa_q3_read_bits(reader, 13) - 4096);
        memcpy(&value, &scalar, sizeof(value));
    } else value = qa_q3_read_bits(reader, 32);
    if (reader->raw.failed) return false;
    set_word(state, field, value);
    return true;
}

static bool valid_entity(int32_t number)
{
    return number >= 0 && number < QA_Q3_ENTITIES;
}

bool qa_q3_write_entity(qa_q3_writer *writer, const qa_q3_entity *from,
                         const qa_q3_entity *to, bool force)
{
    if (writer == NULL || writer->raw.failed) return false;
    if (to == NULL) {
        if (from == NULL) return true;
        if (!valid_entity(from->number))
            return qa_net_writer_fail(&writer->raw, "Invalid Q3 removed entity number");
        return qa_q3_write_bits(writer, (uint32_t)from->number, 10) &&
               qa_q3_write_bits(writer, 1, 1);
    }
    if (!valid_entity(to->number))
        return qa_net_writer_fail(&writer->raw, "Invalid Q3 entity number");
    const qa_q3_entity zero = {0};
    if (from == NULL) from = &zero;
    size_t last = last_changed(entity_fields, ARRAY_COUNT(entity_fields), from, to);
    if (last == 0 && !force) return true;
    if (!qa_q3_write_bits(writer, (uint32_t)to->number, 10) ||
        !qa_q3_write_bits(writer, 0, 1) ||
        !qa_q3_write_bits(writer, last == 0 ? 0u : 1u, 1)) return false;
    if (last == 0) return true;
    if (!qa_q3_write_bits(writer, (uint32_t)last, 8)) return false;
    for (size_t index = 0; index < last; ++index)
        if (!write_field(writer, &entity_fields[index], from, to, true)) return false;
    return true;
}

bool qa_q3_read_entity(qa_q3_reader *reader, const qa_q3_entity *from,
                        int32_t number, qa_q3_entity *out)
{
    if (reader == NULL || reader->raw.failed) return false;
    if (out == NULL || !valid_entity(number))
        return qa_net_reader_fail(&reader->raw, "Invalid Q3 entity destination");
    qa_q3_entity result = from == NULL ? (qa_q3_entity){0} : *from;
    result.number = number;
    if (qa_q3_read_bits(reader, 1) != 0) result = (qa_q3_entity){ .number = QA_Q3_ENTITY_NONE };
    else if (qa_q3_read_bits(reader, 1) != 0) {
        size_t last = qa_q3_read_bits(reader, 8);
        if (last > ARRAY_COUNT(entity_fields))
            return qa_net_reader_fail(&reader->raw, "Invalid Q3 entity field count");
        for (size_t index = 0; index < last; ++index)
            if (!read_field(reader, &entity_fields[index], &result, true)) return false;
    }
    if (reader->raw.failed) return false;
    *out = result;
    return true;
}

typedef struct slot_field { size_t offset; int width; } slot_field;
static const slot_field slot_fields[] = {
    { offsetof(qa_q3_player, stats), 16 },
    { offsetof(qa_q3_player, persistant), 16 },
    { offsetof(qa_q3_player, ammo), 16 },
    { offsetof(qa_q3_player, powerups), 32 }
};

static const int32_t *slots(const qa_q3_player *player, const slot_field *field)
{
    return (const int32_t *)((const unsigned char *)player + field->offset);
}

static uint32_t slot_mask(const int32_t *from, const int32_t *to)
{
    uint32_t mask = 0;
    for (unsigned index = 0; index < 16; ++index)
        if (from[index] != to[index]) mask |= UINT32_C(1) << index;
    return mask;
}

static bool valid_product(qa_q3_product product)
{
    return product == QA_Q3_ARENA || product == QA_Q3_TEAM_ARENA;
}

bool qa_q3_write_player(qa_q3_writer *writer, const qa_q3_player *from, const qa_q3_player *to)
{
    if (writer == NULL || writer->raw.failed) return false;
    if (to == NULL || !valid_product(to->product) || (from != NULL && from->product != to->product))
        return qa_net_writer_fail(&writer->raw, "Invalid Q3 player delta product");
    const qa_q3_player zero = { .product = to->product };
    if (from == NULL) from = &zero;
    size_t last = last_changed(player_fields, ARRAY_COUNT(player_fields), from, to);
    if (!qa_q3_write_bits(writer, (uint32_t)last, 8)) return false;
    for (size_t index = 0; index < last; ++index)
        if (!write_field(writer, &player_fields[index], from, to, false)) return false;
    uint32_t masks[ARRAY_COUNT(slot_fields)], combined = 0;
    for (size_t index = 0; index < ARRAY_COUNT(slot_fields); ++index) {
        masks[index] = slot_mask(slots(from, &slot_fields[index]), slots(to, &slot_fields[index]));
        combined |= masks[index];
    }
    if (!qa_q3_write_bits(writer, combined == 0 ? 0u : 1u, 1)) return false;
    if (combined == 0) return true;
    for (size_t index = 0; index < ARRAY_COUNT(slot_fields); ++index) {
        if (!qa_q3_write_bits(writer, masks[index] == 0 ? 0u : 1u, 1)) return false;
        if (masks[index] == 0) continue;
        if (!qa_q3_write_bits(writer, masks[index], 16)) return false;
        const int32_t *values = slots(to, &slot_fields[index]);
        for (unsigned slot = 0; slot < 16; ++slot)
            if ((masks[index] & (UINT32_C(1) << slot)) != 0 &&
                !qa_q3_write_bits(writer, (uint32_t)values[slot], slot_fields[index].width)) return false;
    }
    return true;
}

bool qa_q3_read_player(qa_q3_reader *reader, const qa_q3_player *from,
                        qa_q3_product product, qa_q3_player *out)
{
    if (reader == NULL || reader->raw.failed) return false;
    if (out == NULL || !valid_product(product) || (from != NULL && from->product != product))
        return qa_net_reader_fail(&reader->raw, "Invalid Q3 player delta product");
    qa_q3_player result = from == NULL ? (qa_q3_player){ .product = product } : *from;
    size_t last = qa_q3_read_bits(reader, 8);
    if (last > ARRAY_COUNT(player_fields))
        return qa_net_reader_fail(&reader->raw, "Invalid Q3 player field count");
    for (size_t index = 0; index < last; ++index)
        if (!read_field(reader, &player_fields[index], &result, false)) return false;
    if (qa_q3_read_bits(reader, 1) != 0) {
        for (size_t index = 0; index < ARRAY_COUNT(slot_fields); ++index) {
            if (qa_q3_read_bits(reader, 1) == 0) continue;
            uint32_t mask = qa_q3_read_bits(reader, 16);
            int32_t *values = (int32_t *)((unsigned char *)&result + slot_fields[index].offset);
            for (unsigned slot = 0; slot < 16; ++slot) {
                if ((mask & (UINT32_C(1) << slot)) == 0) continue;
                /* All three 16-bit arrays are read as signed shorts. */
                int width = slot_fields[index].width == 16 ? -16 : 32;
                values[slot] = signed_word(qa_q3_read_bits(reader, width));
            }
        }
    }
    if (reader->raw.failed) return false;
    *out = result;
    return true;
}

static void command_words(const qa_q3_usercmd *command, uint32_t words[8])
{
    for (unsigned index = 0; index < 3; ++index) words[index] = (uint32_t)command->angles[index];
    words[3] = (uint32_t)command->forwardmove;
    words[4] = (uint32_t)command->rightmove;
    words[5] = (uint32_t)command->upmove;
    words[6] = (uint32_t)command->buttons;
    words[7] = command->weapon;
}

static const int command_widths[8] = { 16, 16, 16, 8, 8, 8, 16, 8 };

bool qa_q3_write_usercmd(qa_q3_writer *writer, const qa_q3_usercmd *from,
                         const qa_q3_usercmd *to, const uint32_t *key)
{
    if (writer == NULL || writer->raw.failed) return false;
    if (from == NULL || to == NULL)
        return qa_net_writer_fail(&writer->raw, "Missing Q3 usercmd delta state");
    uint32_t difference = (uint32_t)to->serverTime - (uint32_t)from->serverTime;
    bool small = signed_word(difference) < 256;
    if (!qa_q3_write_bits(writer, small ? 1u : 0u, 1) ||
        !qa_q3_write_bits(writer, small ? difference : (uint32_t)to->serverTime, small ? 8 : 32))
        return false;
    uint32_t previous[8], next[8];
    command_words(from, previous);
    command_words(to, next);
    uint32_t mixed_key = key == NULL ? 0 : *key ^ (uint32_t)to->serverTime;
    if (key != NULL) {
        bool changed = false;
        for (unsigned index = 0; index < 8; ++index) changed |= previous[index] != next[index];
        if (!qa_q3_write_bits(writer, changed ? 1u : 0u, 1)) return false;
        if (!changed) return true;
    }
    for (unsigned index = 0; index < 8; ++index) {
        bool changed = previous[index] != next[index];
        if (!qa_q3_write_bits(writer, changed ? 1u : 0u, 1)) return false;
        if (changed && !qa_q3_write_bits(writer, next[index] ^ mixed_key, command_widths[index])) return false;
    }
    return true;
}

static int8_t movement_byte(uint32_t value)
{
    int32_t byte = (int32_t)(value & 255u);
    return (int8_t)(byte < 128 ? byte : byte - 256);
}

bool qa_q3_read_usercmd(qa_q3_reader *reader, const qa_q3_usercmd *from,
                        const uint32_t *key, qa_q3_usercmd *out)
{
    if (reader == NULL || reader->raw.failed) return false;
    if (from == NULL || out == NULL)
        return qa_net_reader_fail(&reader->raw, "Missing Q3 usercmd delta state");
    qa_q3_usercmd result = *from;
    bool small = qa_q3_read_bits(reader, 1) != 0;
    uint32_t time = qa_q3_read_bits(reader, small ? 8 : 32);
    if (small) time += (uint32_t)from->serverTime;
    result.serverTime = signed_word(time);
    bool changed = key == NULL || qa_q3_read_bits(reader, 1) != 0;
    if (changed) {
        uint32_t values[8];
        command_words(from, values);
        uint32_t mixed_key = key == NULL ? 0 : *key ^ time;
        for (unsigned index = 0; index < 8; ++index) {
            if (qa_q3_read_bits(reader, 1) == 0) continue;
            values[index] = qa_q3_read_bits(reader, command_widths[index]);
            /* kbitmask[bits] intentionally retains one more key bit. */
            values[index] ^= mixed_key & (UINT32_MAX >> (31 - command_widths[index]));
        }
        for (unsigned index = 0; index < 3; ++index) result.angles[index] = signed_word(values[index]);
        result.forwardmove = movement_byte(values[3]);
        result.rightmove = movement_byte(values[4]);
        result.upmove = movement_byte(values[5]);
        result.buttons = signed_word(values[6]);
        result.weapon = (uint8_t)values[7];
    }
    if (reader->raw.failed) return false;
    *out = result;
    return true;
}

static uint32_t arithmetic_shift(uint32_t value, unsigned shift)
{
    uint32_t result = value >> shift;
    return (value & UINT32_C(0x80000000)) == 0 ? result : result | (UINT32_MAX << (32 - shift));
}

uint32_t qa_q3_command_hash(const char *text, size_t maximum)
{
    uint32_t hash = 0;
    if (text == NULL) return hash;
    for (size_t index = 0; index < maximum && text[index] != '\0'; ++index) {
        uint32_t byte = (unsigned char)text[index];
        if (byte >= 128) byte -= 256;
        hash += byte * (119u + (uint32_t)index);
    }
    return hash ^ arithmetic_shift(hash, 10) ^ arithmetic_shift(hash, 20);
}
