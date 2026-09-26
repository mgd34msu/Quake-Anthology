#include "qa/network_q1.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>

static float finite_float(qa_net_reader *r)
{
    float f=qa_net_read_f32(r);
    if (!isfinite(f)) qa_net_reader_fail(r,"Non-finite demo scalar");
    return f;
}
static bool write_float(qa_net_writer *w, float f)
{
    return isfinite(f) ? qa_net_write_f32(w,f) : qa_net_writer_fail(w,"Non-finite demo scalar");
}
bool qa_q1_demo_read_header(qa_net_reader *r, int32_t *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing demo track output");
    int64_t track=0;
    bool negative=false, digit=false;
    for (unsigned i=0;i<32;++i) {
        uint8_t c=qa_net_read_u8(r);
        if (r->failed) return false;
        if (!i && c=='-') { negative=true; continue; }
        if (c=='\n') {
            if (!digit) return qa_net_reader_fail(r,"Empty demo track");
            *out=(int32_t)(negative?-track:track); return true;
        }
        if (c<'0' || c>'9') return qa_net_reader_fail(r,"Invalid demo track line");
        digit=true;
        track=track*10+(c-'0');
        if (track>(negative?2147483648LL:2147483647LL)) return qa_net_reader_fail(r,"Demo track overflows int32");
    }
    return qa_net_reader_fail(r,"Demo track line exceeds 31 bytes");
}
bool qa_q1_demo_write_header(qa_net_writer *w, int32_t track)
{
    char line[16];
    int count=snprintf(line,sizeof(line),"%ld\n",(long)track);
    if (count<0 || (size_t)count>=sizeof(line)) return qa_net_writer_fail(w,"Invalid demo track");
    return qa_net_write_data(w,line,(size_t)count);
}
bool qa_q1_demo_read_record(qa_net_reader *r, size_t max, qa_q1_demo_record *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing demo record output");
    qa_q1_demo_record record={0};
    int32_t count=qa_net_read_i32(r);
    if (count<0 || (size_t)count>max) return qa_net_reader_fail(r,"Invalid demo message size");
    for (unsigned i=0;i<3;++i) record.angles[i]=finite_float(r);
    if (!qa_net_read_bytes(r,(size_t)count,&record.message)) return false;
    *out=record; return true;
}
bool qa_q1_demo_write_record(qa_net_writer *w, const qa_q1_demo_record *r)
{
    if (!r || r->message.size>INT32_MAX) return qa_net_writer_fail(w,"Invalid demo record");
    qa_net_write_i32(w,(int32_t)r->message.size);
    for (unsigned i=0;i<3;++i) write_float(w,r->angles[i]);
    return qa_net_write_data(w,r->message.data,r->message.size);
}
bool qa_qw_demo_read_record(qa_net_reader *r, size_t max, qa_qw_demo_record *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing QWD record output");
    qa_qw_demo_record record={0};
    record.seconds=finite_float(r); record.kind=(qa_qw_demo_kind)qa_net_read_u8(r);
    switch (record.kind) {
    case QA_QW_DEMO_COMMAND: {
        qa_qw_command *c=&record.data.input.command;
        c->msec=qa_net_read_u8(r);
        /* Original x86 usercmd layout has three padding bytes. */
        (void)qa_net_read_u8(r); (void)qa_net_read_u8(r); (void)qa_net_read_u8(r);
        for (unsigned i=0;i<3;++i) c->angles[i]=finite_float(r);
        c->forward=qa_net_read_i16(r); c->side=qa_net_read_i16(r); c->up=qa_net_read_i16(r);
        c->buttons=qa_net_read_u8(r); c->impulse=qa_net_read_u8(r);
        for (unsigned i=0;i<3;++i) record.data.input.angles[i]=finite_float(r);
        break;
    }
    case QA_QW_DEMO_PACKET: {
        int32_t count=qa_net_read_i32(r);
        if (count<0 || (size_t)count>max) return qa_net_reader_fail(r,"Invalid QWD packet size");
        if (!qa_net_read_bytes(r,(size_t)count,&record.data.packet)) return false;
        break;
    }
    case QA_QW_DEMO_SEQUENCES:
        record.data.sequences.outgoing=qa_net_read_u32(r);
        record.data.sequences.incoming=qa_net_read_u32(r);
        break;
    default: return qa_net_reader_fail(r,"Unknown QWD record type");
    }
    if (r->failed) return false;
    *out=record; return true;
}
bool qa_qw_demo_write_record(qa_net_writer *w, const qa_qw_demo_record *r)
{
    if (!r || r->kind>QA_QW_DEMO_SEQUENCES) return qa_net_writer_fail(w,"Invalid QWD record");
    write_float(w,r->seconds); qa_net_write_u8(w,(uint8_t)r->kind);
    switch (r->kind) {
    case QA_QW_DEMO_COMMAND: {
        const qa_qw_command *c=&r->data.input.command;
        qa_net_write_u8(w,c->msec);
        qa_net_write_u8(w,0); qa_net_write_u8(w,0); qa_net_write_u8(w,0);
        for (unsigned i=0;i<3;++i) write_float(w,c->angles[i]);
        qa_net_write_i16(w,c->forward); qa_net_write_i16(w,c->side); qa_net_write_i16(w,c->up);
        qa_net_write_u8(w,c->buttons); qa_net_write_u8(w,c->impulse);
        for (unsigned i=0;i<3;++i) write_float(w,r->data.input.angles[i]);
        break;
    }
    case QA_QW_DEMO_PACKET:
        if (r->data.packet.size>INT32_MAX) return qa_net_writer_fail(w,"QWD packet too large");
        qa_net_write_i32(w,(int32_t)r->data.packet.size);
        qa_net_write_data(w,r->data.packet.data,r->data.packet.size);
        break;
    case QA_QW_DEMO_SEQUENCES:
        qa_net_write_u32(w,r->data.sequences.outgoing);
        qa_net_write_u32(w,r->data.sequences.incoming);
        break;
    default: return qa_net_writer_fail(w,"Unknown QWD record type");
    }
    return !w->failed;
}
