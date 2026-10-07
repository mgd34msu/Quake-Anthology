#include "cvars_conversion.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool number_text(double value, char text[64], qa_error *error)
{
    if (!isfinite(value))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar conversion requires a finite value");
    return qa_format_fixed(value, 6, text, 64, error);
}
static double number(const qac_cvar_conversion_input *in, const char *text)
{ return qac_number(text ? text : "", in->options->dialect); }
static bool result_number(qac_cvar_conversion_output *out, double value, qa_error *error)
{
    if (!number_text(value, out->text, error)) return false;
    out->value = out->text;
    return true;
}
static bool named(const qac_cvar_conversion_input *in,const char *name)
{ return in->binding && qac_equal(qa_cvar_catalog_string(in->binding->name),name); }
static uint16_t second_color(const qa_cvar_catalog_conversion *conversion)
{ return qa_cvar_catalog_operands[conversion->operand_first+conversion->operand_count-1].row_index; }
static bool family(double type, bool team)
{
    if (team) return type >= 3 && type <= 7;
    return type >= 0 && type <= 7 && type != 2;
}
static double map_value(const qa_cvar_catalog_conversion *c, double value,
    qa_cvar_catalog_map_direction direction, double fallback)
{
    for (size_t i = 0; i < c->map_count; ++i) {
        const qa_cvar_catalog_map *m = &qa_cvar_catalog_maps[c->map_first + i];
        if (m->direction == direction &&
            (direction == QA_CATALOG_ALIAS_TO_CANONICAL ? m->alias_value : m->canonical_value) == value)
            return direction == QA_CATALOG_ALIAS_TO_CANONICAL ? m->canonical_value : m->alias_value;
    }
    return fallback;
}
static bool change(qac_cvar_conversion_output *out, uint16_t row, double value, qa_error *error)
{
    if (out->change_count == sizeof(out->changes) / sizeof(*out->changes))
        return qac_fail(error, QA_ERROR_MEMORY, "cvar composite exceeds its declared operands");
    qac_cvar_change *next = &out->changes[out->change_count];
    next->row = row;
    if (!number_text(value, next->text, error)) return false;
    next->value = next->text;
    ++out->change_count;
    return true;
}
static uint32_t operand_bits(const qac_cvar_conversion_input *in,
    const qa_cvar_catalog_operand *operand)
{ return in->options->role==QA_CVAR_ROLE_ENGINE?operand->unified_bit:operand->bits[in->options->dialect]; }
static bool composite_read(const qac_cvar_conversion_input *in,
    qac_cvar_conversion_output *out, qa_error *error)
{
    const qa_cvar_catalog_conversion *c = in->conversion;
    uint32_t bits = 0;
    for (size_t i = 0; i < c->operand_count; ++i) {
        const qa_cvar_catalog_operand *op = &qa_cvar_catalog_operands[c->operand_first + i];
        double value = number(in, in->operand(in->user, op->row_index));
        bool set = op->predicate ? value > 0 : value != 0;
        if (op->inverted) set = !set;
        if (set) bits |= operand_bits(in, op);
    }
    return result_number(out, bits, error);
}
static bool composite_write(const qac_cvar_conversion_input *in,
    qac_cvar_conversion_output *out, qa_error *error)
{
    const qa_cvar_catalog_conversion *c = in->conversion;
    double parsed = number(in, in->value);
    if (!isfinite(parsed) || parsed < INT32_MIN || parsed > UINT32_MAX)
        return qac_fail(error, QA_ERROR_ARGUMENT, "dmflags requires a representable Source bit word");
    uint32_t bits = parsed < 0 ? (uint32_t)(int32_t)parsed : (uint32_t)parsed;
    for (size_t i = 0; i < c->operand_count; ++i) {
        const qa_cvar_catalog_operand *op = &qa_cvar_catalog_operands[c->operand_first + i];
        uint32_t mask = operand_bits(in, op);
        if (!mask) continue;
        bool set = (bits & mask) != 0;
        if (op->inverted) set = !set;
        double value = set ? 1 : 0;
        if (set && op->predicate) {
            double current = number(in, in->operand(in->user, op->row_index));
            if (current > value) value = current;
        }
        if (!change(out, op->row_index, value, error)) return false;
    }
    return true;
}

bool qac_cvar_read_conversion(const qac_cvar_conversion_input *in,
    qac_cvar_conversion_output *out, qa_error *error)
{
    *out = (qac_cvar_conversion_output){.value = in->value};
    const qa_cvar_catalog_conversion *c = in->conversion;
    if (!c || (c->role_scope == QA_CATALOG_CGAME && in->options->role != QA_CVAR_ROLE_CGAME)) return true;
    double value = number(in, in->value);
    if (c->detail_required && in->detail &&
        (c->operation != QA_CATALOG_OP_DEATHMATCH || family(value, false)) &&
        (c->operation != QA_CATALOG_OP_TEAMPLAY || family(value, true))) { out->value = in->detail; return true; }
    switch ((qa_cvar_catalog_operation)c->operation) {
    case QA_CATALOG_OP_KHZ_HZ:
        if (value == 11) value = 11025;
        else if (value == 22) value = 22050;
        else if (value == 44) value = 44100;
        else if (value == 48) value = 48000;
        else value *= 1000;
        break;
    case QA_CATALOG_OP_SKILL: value = fmin(3, fmax(0, value - 1)); break;
    case QA_CATALOG_OP_VIEW_SIZE:
        if (in->options->dialect != QA_CONSOLE_Q1 && in->options->dialect != QA_CONSOLE_QW) value = fmin(100, value);
        break;
    case QA_CATALOG_OP_DEATHMATCH: value = family(value, false) ? 1 : 0; break;
    case QA_CATALOG_OP_COOP: value = value == 9; break;
    case QA_CATALOG_OP_TEAMPLAY: value = family(value, true) ? 1 : 0; break;
    case QA_CATALOG_OP_CTF: value = value == 4; break;
    case QA_CATALOG_OP_AUTOSWITCH:
        if (named(in,"qts_weapon_autoswitch")) { out->value=value==0?"never":"always"; return true; }
        value=value==0?3:1; break;
    case QA_CATALOG_OP_INPUT_GRAB:
    case QA_CATALOG_OP_OLD_RAIL:
    case QA_CATALOG_OP_NO_EXIT: value=value==0; break;
    case QA_CATALOG_OP_NO_SKINS: value=value!=0; break;
    case QA_CATALOG_OP_DOWNLOAD:
        if (in->options->dialect==QA_CONSOLE_Q2_RERELEASE && value<0) value=0;
        break;
    case QA_CATALOG_OP_MUSIC_MUTE:
#ifdef __linux__
        if (in->options->dialect==QA_CONSOLE_Q2) value=value!=0;
#endif
        break;
    case QA_CATALOG_OP_FORCE_RESPAWN: value = value > 0; break;
    case QA_CATALOG_OP_NEEDPASS:
        if (in->options->dialect == QA_CONSOLE_Q3) value = value != 0;
        break;
    case QA_CATALOG_OP_SEX:
        out->value = qac_equal(in->value, "neuter") ? "none" : in->value;
        return true;
    case QA_CATALOG_OP_QW_SKIN:
        if (in->options->dialect == QA_CONSOLE_QW) {
            const char *slash = strrchr(in->value, '/');
            out->value = slash ? slash + 1 : in->value;
        }
        return true;
    case QA_CATALOG_OP_COLOR:
        value = map_value(c, value, QA_CATALOG_CANONICAL_TO_ALIAS, value);
        break;
    case QA_CATALOG_OP_PLAYER_COLORS: {
        double bottom = number(in, in->operand(in->user, second_color(c)));
        value = map_value(c, value, QA_CATALOG_CANONICAL_TO_ALIAS, value) * 16 +
            map_value(c, bottom, QA_CATALOG_CANONICAL_TO_ALIAS, bottom);
        break;
    }
    case QA_CATALOG_OP_SPECTATOR:
        if (in->options->dialect == QA_CONSOLE_QW) return true;
        value = *in->value && strcmp(in->value, "0");
        break;
    case QA_CATALOG_OP_NONE:
        if (c->operand_count && c->kind == QA_CATALOG_COMPOSITE) return composite_read(in, out, error);
        if (c->kind == QA_CATALOG_IDENTITY || c->kind == QA_CATALOG_SIDE_SCOPE || c->kind == QA_CATALOG_BIT_VIEW) return true;
        if (c->kind == QA_CATALOG_RECIPROCAL) value = 1 / value;
        else if (c->kind == QA_CATALOG_BOOL_INVERT) value = value == 0;
        else if (c->kind == QA_CATALOG_LINEAR || c->kind == QA_CATALOG_CONSUMER_UNITS)
            value = value * c->scale + c->offset;
        else return qac_fail(error, QA_ERROR_ARGUMENT, "cvar conversion requires its Source-owned policy");
        break;
    case QA_CATALOG_OP_CLEAR_COLOR: return true;
    case QA_CATALOG_OP_FULLSCREEN:
    case QA_CATALOG_OP_VIDEO_MODE:
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar conversion requires its Source-owned video policy");
    default:
        if (c->kind == QA_CATALOG_BOOL_INVERT) value = value == 0;
        else if (c->kind == QA_CATALOG_LINEAR) value = value * c->scale + c->offset;
        break;
    }
    return result_number(out, value, error);
}

bool qac_cvar_write_conversion(const qac_cvar_conversion_input *in,
    qac_cvar_conversion_output *out, qa_error *error)
{
    *out = (qac_cvar_conversion_output){.value = in->value};
    const qa_cvar_catalog_conversion *c = in->conversion;
    if (!c || (c->role_scope == QA_CATALOG_CGAME && in->options->role != QA_CVAR_ROLE_CGAME)) return true;
    double value = number(in, in->value), current = number(in, in->current);
    out->detail = c->detail_required != 0;
    switch ((qa_cvar_catalog_operation)c->operation) {
    case QA_CATALOG_OP_KHZ_HZ:
        if (value == 11025) value = 11;
        else if (value == 22050) value = 22;
        else if (value == 44100) value = 44;
        else if (value == 48000) value = 48;
        else value = round(value / 1000);
        break;
    case QA_CATALOG_OP_SKILL: value += 1; break;
    case QA_CATALOG_OP_VIEW_SIZE: return true;
    case QA_CATALOG_OP_DEATHMATCH:
        value = value > 0 ? (family(current, true) ? current : 0) : 8;
        break;
    case QA_CATALOG_OP_COOP: value = value != 0 ? 9 : current == 9 ? 8 : current; break;
    case QA_CATALOG_OP_TEAMPLAY:
        if (in->options->dialect == QA_CONSOLE_Q1 &&
            (value == 1 || value == 2) && c->operand_count &&
            !change(out, qa_cvar_catalog_operands[c->operand_first].row_index, value == 2, error)) return false;
        value = value > 0 ? 3 : family(current, true) ? 0 : current;
        break;
    case QA_CATALOG_OP_CTF:
        if (value == 0) return qac_fail(error, QA_ERROR_ARGUMENT, "ctf zero conversion is not specified by Source policy");
        value = 4;
        break;
    case QA_CATALOG_OP_FORCE_RESPAWN: value = value != 0 ? fmax(current, 1) : 0; break;
    case QA_CATALOG_OP_AUTOSWITCH:
        value = named(in,"qts_weapon_autoswitch") ? (qac_equal(in->value,"never")?0:1) : value == 3 ? 0 : 1;
        break;
    case QA_CATALOG_OP_SHADOWS:
        value = value == 0 ? 0 : current >= 2 && current <= 3 ? current : 1;
        break;
    case QA_CATALOG_OP_OLD_RAIL: value = value == 0; break;
    case QA_CATALOG_OP_INPUT_GRAB: value = value == 0; break;
    case QA_CATALOG_OP_GUN:
    case QA_CATALOG_OP_FOOTSTEPS:
    case QA_CATALOG_OP_LAGOMETER:
    case QA_CATALOG_OP_DRAW_2D:
    case QA_CATALOG_OP_SOUND_BACKEND:
    case QA_CATALOG_OP_BOOL_DETAIL: value = value != 0; break;
    case QA_CATALOG_OP_NO_SKINS:
        value=in->options->dialect==QA_CONSOLE_QW?value==1:value!=0; break;
    case QA_CATALOG_OP_DOWNLOAD:
        if (in->options->dialect==QA_CONSOLE_Q2_RERELEASE && value<0) value=0;
        break;
    case QA_CATALOG_OP_SAME_LEVEL: value = value != 0; break;
    case QA_CATALOG_OP_NO_EXIT: value = value == 0; break;
    case QA_CATALOG_OP_SEX:
        out->value = qac_equal(in->value, "none") ? "neuter" : in->value;
        return true;
    case QA_CATALOG_OP_COLOR:
        value = map_value(c, value, QA_CATALOG_ALIAS_TO_CANONICAL, value);
        break;
    case QA_CATALOG_OP_PLAYER_COLORS: {
        int color = qac_integer(in->value);
        double bottom = map_value(c, color & 15, QA_CATALOG_ALIAS_TO_CANONICAL, color & 15);
        if (!change(out, second_color(c), bottom, error)) return false;
        value = map_value(c, (color >> 4) & 15, QA_CATALOG_ALIAS_TO_CANONICAL, (color >> 4) & 15);
        break;
    }
    case QA_CATALOG_OP_QW_SKIN:
        if (in->options->dialect == QA_CONSOLE_QW) {
            const char *slash = strrchr(in->current, '/');
            size_t prefix = slash ? (size_t)(slash - in->current + 1) : 0;
            size_t suffix=strlen(in->value);
            if (prefix>SIZE_MAX-suffix-1) return qac_fail(error,QA_ERROR_MEMORY,"converted player model exceeds address space");
            char *text=prefix+suffix<sizeof(out->text)?out->text:malloc(prefix+suffix+1);
            if (!text) return qac_fail(error,QA_ERROR_MEMORY,"admitting converted player model");
            if (text!=out->text) out->allocated_value=text;
            memcpy(text, in->current, prefix);
            memcpy(text+prefix,in->value,suffix+1); out->value=text;
        }
        return true;
    case QA_CATALOG_OP_SPECTATOR: return true;
    case QA_CATALOG_OP_MUSIC_MUTE:
#ifdef __linux__
        if (in->options->dialect==QA_CONSOLE_Q2) {
            if (value==0) {
                if (current!=0) { out->detail=true; out->detail_value=in->current; }
                value=0;
            } else {
                out->value=current!=0?in->current:in->detail?in->detail:"1";
                return true;
            }
        }
#endif
        break;
    case QA_CATALOG_OP_CLEAR_COLOR: return true;
    case QA_CATALOG_OP_FULLSCREEN:
    case QA_CATALOG_OP_VIDEO_MODE:
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar conversion requires its Source-owned video policy");
    default:
        if (c->operand_count && c->kind == QA_CATALOG_COMPOSITE) return composite_write(in, out, error);
        if (c->kind == QA_CATALOG_IDENTITY || c->kind == QA_CATALOG_SIDE_SCOPE || c->kind == QA_CATALOG_ENUM_DETAIL || c->kind == QA_CATALOG_BIT_VIEW)
            return true;
        if (c->kind == QA_CATALOG_RECIPROCAL) value = 1 / value;
        else if (c->kind == QA_CATALOG_BOOL_INVERT) value = value == 0;
        else if (c->kind == QA_CATALOG_LINEAR || c->kind == QA_CATALOG_CONSUMER_UNITS)
            value = (value - c->offset) / c->scale;
        else return qac_fail(error, QA_ERROR_ARGUMENT, "cvar conversion requires its Source-owned policy");
        break;
    }
    return result_number(out, value, error);
}
