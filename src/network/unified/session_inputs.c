#include "qa/network_unified_session.h"
#include "value_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool number(const qa_unified_document *d, qa_json_id object, const char *name, double *out, qa_error *e)
{
    return qa_unified_document_number(d, qa_json_get(qa_unified_document_json(d), object, name), out, e);
}

static bool vector(const qa_unified_document *d, qa_json_id object, const char *name, qa_unified_vec3 *out, qa_error *e)
{
    qa_json_id id = qa_json_get(qa_unified_document_json(d), object, name);
    return number(d, id, "x", &out->x, e) && number(d, id, "y", &out->y, e) && number(d, id, "z", &out->z, e);
}

static bool words(const qa_unified_document *d, qa_json_id object, const char *name, double out[3], qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(d);
    qa_json_id id = qa_json_get(json, object, name);
    for (size_t i = 0; i < 3; ++i)
        if (!qa_unified_document_number(d, qa_json_at(json, id, i), &out[i], e)) return false;
    return true;
}

static bool command_read(const qa_unified_document *d, qa_json_id id, qa_unified_movement *m, qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(d);
    qa_json_id kind = qa_json_get(json, id, "kind");
    if (qa_json_string_equal(json, kind, "q1-netquake")) {
        m->kind = QA_MOVEMENT_NETQUAKE;
        return number(d,id,"acknowledgedServerTimeSeconds",&m->data.nq.acknowledged_seconds,e) &&
            vector(d,id,"viewAngles",&m->data.nq.angles,e) && number(d,id,"forwardMove",&m->data.nq.forward,e) &&
            number(d,id,"sideMove",&m->data.nq.side,e) && number(d,id,"upMove",&m->data.nq.up,e) &&
            number(d,id,"buttons",&m->data.nq.buttons,e) && number(d,id,"impulse",&m->data.nq.impulse,e);
    }
    if (qa_json_string_equal(json, kind, "q1-quakeworld")) {
        m->kind = QA_MOVEMENT_QUAKEWORLD;
        return number(d,id,"milliseconds",&m->data.qw.milliseconds,e) && vector(d,id,"angles",&m->data.qw.angles,e) &&
            number(d,id,"forwardMove",&m->data.qw.forward,e) && number(d,id,"sideMove",&m->data.qw.side,e) &&
            number(d,id,"upMove",&m->data.qw.up,e) && number(d,id,"buttons",&m->data.qw.buttons,e) &&
            number(d,id,"impulse",&m->data.qw.impulse,e);
    }
    if (qa_json_string_equal(json, kind, "q2-classic")) {
        m->kind = QA_MOVEMENT_Q2_CLASSIC;
        return number(d,id,"milliseconds",&m->data.q2.milliseconds,e) && words(d,id,"angleShorts",m->data.q2.angle_shorts,e) &&
            number(d,id,"forwardMove",&m->data.q2.forward,e) && number(d,id,"sideMove",&m->data.q2.side,e) &&
            number(d,id,"upMove",&m->data.q2.up,e) && number(d,id,"buttons",&m->data.q2.buttons,e) &&
            number(d,id,"impulse",&m->data.q2.impulse,e) && number(d,id,"lightLevel",&m->data.q2.light_level,e);
    }
    if (qa_json_string_equal(json, kind, "q2-rerelease")) {
        m->kind = QA_MOVEMENT_Q2_RERELEASE;
        return number(d,id,"milliseconds",&m->data.q2r.milliseconds,e) && vector(d,id,"angles",&m->data.q2r.angles,e) &&
            number(d,id,"forwardMove",&m->data.q2r.forward,e) && number(d,id,"sideMove",&m->data.q2r.side,e) &&
            number(d,id,"buttons",&m->data.q2r.buttons,e) && number(d,id,"serverFrame",&m->data.q2r.server_frame,e);
    }
    m->kind = QA_MOVEMENT_Q3;
    return number(d,id,"serverTimeMilliseconds",&m->data.q3.server_time_ms,e) && words(d,id,"angleWords",m->data.q3.angle_words,e) &&
        number(d,id,"buttons",&m->data.q3.buttons,e) && number(d,id,"weapon",&m->data.q3.weapon,e) &&
        number(d,id,"forwardMove",&m->data.q3.forward,e) && number(d,id,"rightMove",&m->data.q3.right,e) &&
        number(d,id,"upMove",&m->data.q3.up,e);
}

void qa_unified_inputs_free(qa_unified_input_batch *b)
{
    if (!b) return;
    for (size_t i = 0; i < 64; ++i) { qa_buffer_free(&b->providers[i]); qa_buffer_free(&b->weapons[i]); }
    *b = (qa_unified_input_batch){0};
}

bool qa_unified_inputs_read(const qa_unified_document *d, qa_unified_input_batch *out, qa_error *e)
{
    if (!d || !out || qa_unified_document_type(d) != QA_UNIFIED_INPUT_DOCUMENT) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Input batch requires its validated production document"); return false;
    }
    qa_unified_input_batch batch = {0};
    const qa_json_document *json = qa_unified_document_json(d);
    qa_json_id value = qa_json_get(json, qa_unified_document_root(d), "value");
    double epoch;
    bool ok = number(d, value, "epoch", &epoch, e);
    if (ok) batch.epoch = (uint32_t)epoch;
    qa_json_id commands = qa_json_get(json, value, "commands");
    batch.count = qa_json_size(json, commands);
    for (size_t i = 0; ok && i < batch.count; ++i) {
        qa_json_id item = qa_json_at(json, commands, i);
        qa_unified_input *input = &batch.commands[i];
        double sequence;
        ok = number(d, item, "sequence", &sequence, e) &&
            command_read(d, qa_json_get(json, item, "command"), &input->command, e);
        if (ok) input->sequence = (uint64_t)sequence;
        qa_json_id arsenal = qa_json_get(json, item, "arsenal");
        input->has_arsenal = arsenal != QA_JSON_NONE;
        if (ok && input->has_arsenal) {
            ok = qa_json_string(json, qa_json_get(json, arsenal, "provider"), &batch.providers[i], e) &&
                qa_json_bool(json, qa_json_get(json, arsenal, "useHoldable"), &input->arsenal.use_holdable, e);
            qa_json_id weapon = qa_json_get(json, arsenal, "weapon");
            if (ok && qa_json_type(json, weapon) != QA_JSON_NULL) ok = qa_json_string(json, weapon, &batch.weapons[i], e);
            input->arsenal.provider = (qa_bytes){batch.providers[i].data, batch.providers[i].size};
            input->arsenal.weapon = (qa_bytes){batch.weapons[i].data, batch.weapons[i].size};
            input->arsenal.has_impulse = qa_json_get(json, arsenal, "impulse") != QA_JSON_NONE;
            if (ok && input->arsenal.has_impulse) {
                double impulse;
                ok = number(d, arsenal, "impulse", &impulse, e);
                if (ok) input->arsenal.impulse = (uint8_t)impulse;
            }
        }
    }
    if (!ok) { qa_unified_inputs_free(&batch); return false; }
    *out = batch;
    return true;
}

static bool append(qa_unified_builder *b, const char *text, qa_error *e)
{
    return qa_unified_append(b, text, strlen(text), e);
}

static bool value_number(qa_unified_builder *b, double value, qa_error *e)
{
    if (isfinite(value) && (value != 0 || !signbit(value))) {
        char text[32];
        return qa_unified_number_text(value, text, e) &&
            qa_unified_append(b, text, strlen(text), e);
    }
    qa_buffer encoded = {0};
    if (!qa_unified_checkpoint_number(value, &encoded, e)) return false;
    bool ok = qa_unified_append(b, encoded.data, encoded.size, e);
    qa_buffer_free(&encoded);
    return ok;
}

static bool field_number(qa_unified_builder *b, const char *key, double value, qa_error *e)
{
    return append(b, ",\"", e) && append(b, key, e) && append(b, "\":", e) && value_number(b, value, e);
}

static bool field_vector(qa_unified_builder *b, const char *key, qa_unified_vec3 value, qa_error *e)
{
    return append(b, ",\"", e) && append(b, key, e) && append(b, "\":{\"x\":", e) &&
        value_number(b, value.x, e) && field_number(b, "y", value.y, e) && field_number(b, "z", value.z, e) && append(b, "}", e);
}

static bool field_words(qa_unified_builder *b, const char *key, const double value[3], qa_error *e)
{
    return append(b, ",\"", e) && append(b, key, e) && append(b, "\":[", e) && value_number(b, value[0], e) &&
        append(b, ",", e) && value_number(b, value[1], e) && append(b, ",", e) && value_number(b, value[2], e) && append(b, "]", e);
}

static bool text_write(qa_unified_builder *b, qa_bytes text, qa_error *e)
{
    qa_buffer quoted = {0};
    if (!qa_json_quote(text, &quoted, e)) return false;
    bool ok = qa_unified_append(b, quoted.data, quoted.size, e);
    qa_buffer_free(&quoted);
    return ok;
}

static bool command_write(qa_unified_builder *b, const qa_unified_movement *m, qa_error *e)
{
    switch (m->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return append(b,"{\"kind\":\"q1-netquake\"",e) && field_number(b,"acknowledgedServerTimeSeconds",m->data.nq.acknowledged_seconds,e) &&
            field_vector(b,"viewAngles",m->data.nq.angles,e) && field_number(b,"forwardMove",m->data.nq.forward,e) &&
            field_number(b,"sideMove",m->data.nq.side,e) && field_number(b,"upMove",m->data.nq.up,e) &&
            field_number(b,"buttons",m->data.nq.buttons,e) && field_number(b,"impulse",m->data.nq.impulse,e) && append(b,"}",e);
    case QA_MOVEMENT_QUAKEWORLD:
        return append(b,"{\"kind\":\"q1-quakeworld\"",e) && field_number(b,"milliseconds",m->data.qw.milliseconds,e) &&
            field_vector(b,"angles",m->data.qw.angles,e) && field_number(b,"forwardMove",m->data.qw.forward,e) &&
            field_number(b,"sideMove",m->data.qw.side,e) && field_number(b,"upMove",m->data.qw.up,e) &&
            field_number(b,"buttons",m->data.qw.buttons,e) && field_number(b,"impulse",m->data.qw.impulse,e) && append(b,"}",e);
    case QA_MOVEMENT_Q2_CLASSIC:
        return append(b,"{\"kind\":\"q2-classic\"",e) && field_number(b,"milliseconds",m->data.q2.milliseconds,e) &&
            field_words(b,"angleShorts",m->data.q2.angle_shorts,e) && field_number(b,"forwardMove",m->data.q2.forward,e) &&
            field_number(b,"sideMove",m->data.q2.side,e) && field_number(b,"upMove",m->data.q2.up,e) &&
            field_number(b,"buttons",m->data.q2.buttons,e) && field_number(b,"impulse",m->data.q2.impulse,e) &&
            field_number(b,"lightLevel",m->data.q2.light_level,e) && append(b,"}",e);
    case QA_MOVEMENT_Q2_RERELEASE:
        return append(b,"{\"kind\":\"q2-rerelease\"",e) && field_number(b,"milliseconds",m->data.q2r.milliseconds,e) &&
            field_vector(b,"angles",m->data.q2r.angles,e) && field_number(b,"forwardMove",m->data.q2r.forward,e) &&
            field_number(b,"sideMove",m->data.q2r.side,e) && field_number(b,"buttons",m->data.q2r.buttons,e) &&
            field_number(b,"serverFrame",m->data.q2r.server_frame,e) && append(b,"}",e);
    case QA_MOVEMENT_Q3:
        return append(b,"{\"kind\":\"q3\"",e) && field_number(b,"serverTimeMilliseconds",m->data.q3.server_time_ms,e) &&
            field_words(b,"angleWords",m->data.q3.angle_words,e) && field_number(b,"buttons",m->data.q3.buttons,e) &&
            field_number(b,"weapon",m->data.q3.weapon,e) && field_number(b,"forwardMove",m->data.q3.forward,e) &&
            field_number(b,"rightMove",m->data.q3.right,e) && field_number(b,"upMove",m->data.q3.up,e) && append(b,"}",e);
    }
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Unknown production input movement"); return false;
}

bool qa_unified_inputs_document(uint32_t epoch, const qa_unified_input *inputs, size_t count,
    qa_unified_document **out, qa_error *e)
{
    if (!out || !epoch || count > 64 || (count && !inputs)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid production input producer"); return false;
    }
    qa_unified_builder b = {.maximum = 65536};
    bool ok = append(&b,"{\"schema\":\"qts-input\",\"version\":1,\"value\":{\"epoch\":",e) &&
        value_number(&b, epoch, e) && append(&b,",\"commands\":[",e);
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_unified_input *input = &inputs[i];
        if (input->sequence > QA_UNIFIED_SAFE_INTEGER) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Production input sequence exceeds the source integer domain"); ok = false; break;
        }
        ok = (!i || append(&b,",",e)) && append(&b,"{\"sequence\":",e) && value_number(&b,(double)input->sequence,e) &&
            append(&b,",\"command\":",e) && command_write(&b,&input->command,e);
        if (ok && input->has_arsenal) {
            const qa_unified_arsenal *a = &input->arsenal;
            ok = append(&b,",\"arsenal\":{\"provider\":",e) && text_write(&b,a->provider,e) &&
                append(&b,",\"weapon\":",e) && (a->weapon.size ? text_write(&b,a->weapon,e) : append(&b,"null",e)) &&
                append(&b,a->use_holdable ? ",\"useHoldable\":true" : ",\"useHoldable\":false",e);
            if (ok && a->has_impulse) ok = field_number(&b,"impulse",a->impulse,e);
            ok = ok && append(&b,"}",e);
        }
        ok = ok && append(&b,"}",e);
    }
    ok = ok && append(&b,"]}}",e) && qa_unified_document_create(QA_UNIFIED_INPUT_DOCUMENT,(qa_bytes){b.data,b.size},out,e);
    free(b.data);
    return ok;
}
