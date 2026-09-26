#include "qa/network_q1.h"
#include <string.h>

bool qa_q1_read_cstring(qa_net_reader *r, const char **out)
{
    if (!r || !out) return qa_net_reader_fail(r,"Missing Quake string output");
    if (r->failed) return false;
    if (r->bit & 7) return qa_net_reader_fail(r,"Unaligned Quake string");
    size_t start = r->bit/8, count = qa_net_reader_remaining(r);
    const uint8_t *end = count ? memchr(r->bytes.data+start,0,count) : NULL;
    if (!end) return qa_net_reader_fail(r,"Unterminated Quake string");
    *out=(const char *)(r->bytes.data+start);
    r->bit=((size_t)(end-r->bytes.data)+1)*8;
    return true;
}
bool qa_q1_client_read(qa_net_reader *r, qa_net_protocol_id p, uint32_t sequence,
                       bool *moved, qa_q1_client_message *out)
{
    qa_q1_client_message m={0};
    if (!r || !out || !moved) return r ? qa_net_reader_fail(r,"Missing client message state") : false;
    if (!qa_q1_profile_valid(p,NULL) || (r->bit & 7)) return qa_net_reader_fail(r,"Invalid client packet dialect or alignment");
    bool qw=qa_q1_is_qw(p);
    m.op=(qa_q1_client_op)qa_net_read_u8(r);
    switch (m.op) {
    case QA_Q1_CLC_NOP: break;
    case QA_Q1_CLC_DISCONNECT:
        if (qw) return qa_net_reader_fail(r,"QW has no disconnect client opcode");
        break;
    case QA_Q1_CLC_STRING: if (!qa_q1_read_cstring(r,&m.data.text)) return false; break;
    case QA_Q1_CLC_MOVE:
        if (qw) {
            if (*moved) return qa_net_reader_fail(r,"Multiple QW moves in one packet");
            uint8_t checksum=qa_net_read_u8(r);
            size_t start=r->bit/8;
            m.data.qw_move.loss=qa_net_read_u8(r);
            const qa_qw_command zero={0};
            qa_qw_read_delta_command(r,&zero,&m.data.qw_move.oldest);
            qa_qw_read_delta_command(r,&m.data.qw_move.oldest,&m.data.qw_move.previous);
            qa_qw_read_delta_command(r,&m.data.qw_move.previous,&m.data.qw_move.current);
            if (r->failed) return false;
            qa_bytes bytes={r->bytes.data+start,r->bit/8-start};
            if (qa_qw_checksum(bytes,sequence)!=checksum) return qa_net_reader_fail(r,"Invalid QW movement checksum");
        } else if (!qa_q1_read_move(r,p,&m.data.nq_move)) return false;
        break;
    case QA_Q1_CLC_DELTA:
        if (!qw) return qa_net_reader_fail(r,"Delta command requires QuakeWorld");
        m.data.delta=qa_net_read_u8(r); break;
    case QA_Q1_CLC_TELEPORT:
        if (!qw) return qa_net_reader_fail(r,"Spectator teleport requires QuakeWorld");
        for (unsigned i=0;i<3;++i) m.data.teleport[i]=qa_q1_read_coord(r,p);
        break;
    case QA_Q1_CLC_UPLOAD: {
        if (!qw) return qa_net_reader_fail(r,"Upload command requires QuakeWorld");
        int16_t count=qa_net_read_i16(r);
        m.data.upload.percent=qa_net_read_u8(r);
        if (count < 0 || count > 768 || m.data.upload.percent>100)
            return qa_net_reader_fail(r,"Invalid QW upload block");
        if (!qa_net_read_bytes(r,(size_t)count,&m.data.upload.bytes)) return false;
        break;
    }
    default: return qa_net_reader_fail(r,"Unknown Quake client opcode");
    }
    if (r->failed) return false;
    if (qw && m.op==QA_Q1_CLC_MOVE) *moved=true;
    *out=m; return true;
}
bool qa_q1_client_write(qa_net_writer *w, qa_net_protocol_id p, uint32_t sequence,
                        const qa_q1_client_message *m)
{
    if (!m || !qa_q1_profile_valid(p,NULL) || (w->bit & 7))
        return qa_net_writer_fail(w,"Invalid client message dialect or alignment");
    bool qw=qa_q1_is_qw(p);
    if ((qw && m->op==QA_Q1_CLC_DISCONNECT) || (!qw && m->op>QA_Q1_CLC_STRING))
        return qa_net_writer_fail(w,"Client opcode unavailable in dialect");
    qa_net_write_u8(w,(uint8_t)m->op);
    switch (m->op) {
    case QA_Q1_CLC_NOP: case QA_Q1_CLC_DISCONNECT: break;
    case QA_Q1_CLC_STRING:
        if (!m->data.text) return qa_net_writer_fail(w,"Missing client command text");
        qa_net_write_string(w,m->data.text); break;
    case QA_Q1_CLC_MOVE:
        if (qw) {
            size_t checksum_offset=w->bit/8;
            qa_net_write_u8(w,0);
            qa_net_write_u8(w,m->data.qw_move.loss);
            const qa_qw_command zero={0};
            qa_qw_write_delta_command(w,&zero,&m->data.qw_move.oldest);
            qa_qw_write_delta_command(w,&m->data.qw_move.oldest,&m->data.qw_move.previous);
            qa_qw_write_delta_command(w,&m->data.qw_move.previous,&m->data.qw_move.current);
            if (w->failed) return false;
            qa_bytes bytes={w->data+checksum_offset+1,w->bit/8-checksum_offset-1};
            w->data[checksum_offset]=qa_qw_checksum(bytes,sequence);
        } else qa_q1_write_move(w,p,&m->data.nq_move);
        break;
    case QA_Q1_CLC_DELTA: qa_net_write_u8(w,m->data.delta); break;
    case QA_Q1_CLC_TELEPORT:
        for (unsigned i=0;i<3;++i) qa_q1_write_coord(w,p,m->data.teleport[i]);
        break;
    case QA_Q1_CLC_UPLOAD:
        if (m->data.upload.bytes.size>768 || m->data.upload.percent>100)
            return qa_net_writer_fail(w,"Invalid QW upload block");
        qa_net_write_u16(w,(uint16_t)m->data.upload.bytes.size);
        qa_net_write_u8(w,m->data.upload.percent);
        qa_net_write_data(w,m->data.upload.bytes.data,m->data.upload.bytes.size);
        break;
    default: return qa_net_writer_fail(w,"Unknown Quake client opcode");
    }
    return !w->failed;
}
bool qa_qw_replay_commands(qa_qw_command *last, const qa_qw_move *m, uint32_t dropped,
                           bool paused, qa_qw_command_fn apply, void *user, qa_error *e)
{
    if (!last || !m || !apply) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing QW command replay input"); return false; }
    if (!paused) {
        if (dropped<20) {
            for (uint32_t pending=dropped;pending>2;--pending) if (!apply(user,last,e)) return false;
            if (dropped>1 && !apply(user,&m->oldest,e)) return false;
            if (dropped>0 && !apply(user,&m->previous,e)) return false;
        }
        if (!apply(user,&m->current,e)) return false;
    }
    *last=m->current; last->buttons=0; return true;
}
bool qa_qw_split_command(const qa_qw_command *c, qa_qw_command_fn apply, void *user, qa_error *e)
{
    if (!c || !apply) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing QW split command input"); return false; }
    if (c->msec>50) {
        qa_qw_command half=*c; half.msec=(uint8_t)(c->msec/2);
        return qa_qw_split_command(&half,apply,user,e) && qa_qw_split_command(&half,apply,user,e);
    }
    return apply(user,c,e);
}
