#include "qa/network_q1_qw.h"
#include "qa/network_q1_decoder_save.h"
#include "qa/network_qw_source.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    U_ANGLE1=1, U_ANGLE3=2, U_MODEL=4, U_COLORMAP=8, U_SKIN=16,
    U_EFFECTS=32, U_SOLID=64, U_EXTEND=128,
    U_ORIGIN1=512, U_ORIGIN2=1024, U_ORIGIN3=2048, U_ANGLE2=4096,
    U_FRAME=8192, U_REMOVE=16384, U_MOREBITS=32768,
    X_ENTITY=1, X_MODEL=2, X_FRAME=4, X_ALPHA=8, X_SCALE=16
};
static const unsigned angle_bits[3]={U_ANGLE1,U_ANGLE2,U_ANGLE3};
typedef struct baseline_slot { bool valid; qa_q1_entity entity; } baseline_slot;
typedef struct frame_slot { bool valid; qa_qw_frame frame; } frame_slot;
typedef struct delta_request { bool valid,has_base; uint32_t sequence,base; } delta_request;
struct qa_qw_decoder {
    qa_net_protocol_id protocol;
    uint32_t player_model;
    baseline_slot *baselines;
    size_t baseline_capacity;
    frame_slot frames[QA_QW_UPDATE_BACKUP];
    delta_request requests[QA_QW_UPDATE_BACKUP];
    const char **names;
    size_t names_capacity;
    qa_qw_nail nails[QA_QW_MAX_NAILS];
};
static bool profile_valid(qa_net_protocol_id p)
{ return qa_q1_is_qw(p) && qa_q1_profile_valid(p,NULL); }
static uint32_t entity_limit(qa_net_protocol_id p) { return p.kind==QA_NET_QW28 ? 512 : 65536; }
static uint32_t index_limit(qa_net_protocol_id p) { return p.kind==QA_NET_QW28 ? 256 : 65536; }
static uint32_t precache_limit(qa_net_protocol_id p) { return p.kind==QA_NET_QW28 ? 256 : 8192; }
static bool error(qa_error *e, qa_status status, const char *message)
{ qa_error_set(e,status,0,"%s",message); return false; }
static bool finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static bool entity_valid(qa_net_protocol_id p, const qa_q1_entity *s)
{
    return s && s->number<entity_limit(p) && s->model<index_limit(p) && s->frame<index_limit(p) &&
        s->colormap<=255 && s->skin<=255 && s->effects<=255 && finite3(s->origin) && finite3(s->angles) &&
        !s->step && s->lerp_finish==0 &&
        (p.kind==QA_NET_QW29 || (s->alpha==QA_Q1_ALPHA_DEFAULT && s->scale==QA_Q1_SCALE_DEFAULT));
}
static bool frame_valid(qa_net_protocol_id p, const qa_qw_frame *f)
{
    if (!f || f->sequence>UINT32_C(0x7fffffff) || f->count>QA_QW_MAX_PACKET_ENTITIES) return false;
    uint32_t previous=0;
    for (size_t i=0;i<f->count;++i) {
        if (!entity_valid(p,&f->entities[i]) || f->entities[i].number<=previous) return false;
        previous=f->entities[i].number;
    }
    return true;
}
qa_qw_decoder *qa_qw_decoder_create(qa_net_protocol_id p, qa_error *e)
{
    if (!profile_valid(p)) { error(e,QA_ERROR_ARGUMENT,"Invalid QuakeWorld protocol"); return NULL; }
    qa_qw_decoder *d=calloc(1,sizeof(*d));
    if (!d) { error(e,QA_ERROR_MEMORY,"Allocating QuakeWorld decoder"); return NULL; }
    d->protocol=p; return d;
}
void qa_qw_decoder_destroy(qa_qw_decoder *d)
{ if (d) { free(d->baselines); free(d->names); free(d); } }
bool qa_qw_decoder_reset(qa_qw_decoder *d, qa_net_protocol_id p, qa_error *e)
{
    if (!d || !profile_valid(p)) return error(e,QA_ERROR_ARGUMENT,"Invalid QuakeWorld reset");
    d->protocol=p; d->player_model=0;
    if (d->baseline_capacity) memset(d->baselines,0,d->baseline_capacity*sizeof(*d->baselines));
    memset(d->frames,0,sizeof(d->frames)); memset(d->requests,0,sizeof(d->requests));
    return true;
}
qa_net_protocol_id qa_qw_decoder_protocol(const qa_qw_decoder *d) { return d->protocol; }
uint32_t qa_qw_decoder_player_model(const qa_qw_decoder *d) { return d->player_model; }
bool qa_qw_decoder_set_player_model(qa_qw_decoder *d, uint32_t model, qa_error *e)
{
    if (!d || model>=precache_limit(d->protocol)) return error(e,QA_ERROR_ARGUMENT,"Invalid QuakeWorld player model");
    d->player_model=model; return true;
}
bool qa_qw_decoder_set_baseline(qa_qw_decoder *d, const qa_q1_entity *s, qa_error *e)
{
    if (!d || !entity_valid(d->protocol,s) || s->effects || s->qw_flags)
        return error(e,QA_ERROR_ARGUMENT,"Unrepresentable QuakeWorld baseline");
    if (s->number>=d->baseline_capacity) {
        size_t capacity=d->baseline_capacity ? d->baseline_capacity : 512;
        while (capacity<=s->number) capacity*=2;
        baseline_slot *next=realloc(d->baselines,capacity*sizeof(*next));
        if (!next) return error(e,QA_ERROR_MEMORY,"Allocating QuakeWorld baselines");
        memset(next+d->baseline_capacity,0,(capacity-d->baseline_capacity)*sizeof(*next));
        d->baselines=next; d->baseline_capacity=capacity;
    }
    d->baselines[s->number]=(baseline_slot){.valid=true,.entity=*s}; return true;
}
const qa_q1_entity *qa_qw_decoder_baseline(const qa_qw_decoder *d, uint32_t number)
{ return d && number<d->baseline_capacity && d->baselines[number].valid ? &d->baselines[number].entity : NULL; }
const qa_qw_frame *qa_qw_decoder_frame(const qa_qw_decoder *d, uint32_t sequence)
{
    if (!d || sequence>UINT32_C(0x7fffffff)) return NULL;
    const frame_slot *s=&d->frames[sequence&(QA_QW_UPDATE_BACKUP-1)];
    return s->valid && s->frame.sequence==sequence ? &s->frame : NULL;
}
bool qa_qw_decoder_store_frame(qa_qw_decoder *d, const qa_qw_frame *f, qa_error *e)
{
    if (!d || !frame_valid(d->protocol,f)) return error(e,QA_ERROR_ARGUMENT,"Invalid QuakeWorld frame");
    d->frames[f->sequence&(QA_QW_UPDATE_BACKUP-1)]=(frame_slot){.valid=true,.frame=*f};
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        uint32_t age=(f->sequence-d->frames[i].frame.sequence)&UINT32_C(0x7fffffff);
        if (age>=QA_QW_UPDATE_BACKUP && age<UINT32_C(0x40000000)) d->frames[i].valid=false;
    }
    return true;
}
void qa_qw_decoder_delta_request(qa_qw_decoder *d, uint32_t sequence, bool has_base, uint32_t base)
{
    if (!d || sequence>UINT32_C(0x7fffffff) || (has_base && base>UINT32_C(0x7fffffff))) return;
    if (!has_base) base=0;
    d->requests[sequence&(QA_QW_UPDATE_BACKUP-1)]=(delta_request){true,has_base,sequence,base};
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        uint32_t age=(sequence-d->requests[i].sequence)&UINT32_C(0x7fffffff);
        if (age>=QA_QW_UPDATE_BACKUP && age<UINT32_C(0x40000000)) d->requests[i].valid=false;
    }
}
static uint32_t read_index(qa_net_reader *r, qa_net_protocol_id p)
{ return p.kind==QA_NET_QW28 ? qa_net_read_u8(r) : qa_net_read_u16(r); }
static bool write_index(qa_net_writer *w, qa_net_protocol_id p, uint32_t index)
{
    if (index>=index_limit(p)) return qa_net_writer_fail(w,"QuakeWorld index exceeds protocol width");
    return p.kind==QA_NET_QW28 ? qa_net_write_u8(w,(uint8_t)index) : qa_net_write_u16(w,(uint16_t)index);
}
static void read_vector(qa_net_reader *r, qa_net_protocol_id p, float v[3], bool angles)
{ for (unsigned i=0;i<3;++i) v[i]=angles ? qa_q1_read_angle(r,p) : qa_q1_read_coord(r,p); }
static void write_vector(qa_net_writer *w, qa_net_protocol_id p, const float v[3], bool angles)
{ for (unsigned i=0;i<3;++i) { if (angles) qa_q1_write_angle(w,p,v[i]); else qa_q1_write_coord(w,p,v[i]); } }
static float read_float(qa_net_reader *r)
{
    float value=qa_net_read_f32(r);
    if (!isfinite(value)) qa_net_reader_fail(r,"Non-finite QuakeWorld scalar");
    return value;
}
static bool write_float(qa_net_writer *w, float value)
{ return isfinite(value) ? qa_net_write_f32(w,value) : qa_net_writer_fail(w,"Non-finite QuakeWorld scalar"); }

bool qa_qw_read_baseline(qa_net_reader *r, qa_net_protocol_id p, qa_q1_entity *out)
{
    if (!out || !profile_valid(p)) return qa_net_reader_fail(r,"Invalid QuakeWorld baseline protocol");
    qa_q1_entity s; qa_q1_entity_init(&s);
    s.model=read_index(r,p); s.frame=read_index(r,p);
    s.colormap=qa_net_read_u8(r); s.skin=qa_net_read_u8(r);
    if (p.kind==QA_NET_QW29) { s.alpha=qa_net_read_u8(r); s.scale=qa_net_read_u8(r); }
    for (unsigned i=0;i<3;++i) { s.origin[i]=qa_q1_read_coord(r,p); s.angles[i]=qa_q1_read_angle(r,p); }
    if (r->failed) return false;
    *out=s; return true;
}
bool qa_qw_write_baseline(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_entity *s)
{
    if (!profile_valid(p) || !entity_valid(p,s) || s->effects || s->qw_flags)
        return qa_net_writer_fail(w,"Unrepresentable QuakeWorld baseline");
    write_index(w,p,s->model); write_index(w,p,s->frame);
    qa_net_write_u8(w,(uint8_t)s->colormap); qa_net_write_u8(w,(uint8_t)s->skin);
    if (p.kind==QA_NET_QW29) { qa_net_write_u8(w,s->alpha); qa_net_write_u8(w,s->scale); }
    for (unsigned i=0;i<3;++i) { qa_q1_write_coord(w,p,s->origin[i]); qa_q1_write_angle(w,p,s->angles[i]); }
    return !w->failed;
}
bool qa_qw_read_player(qa_net_reader *r, qa_net_protocol_id p, uint32_t player_model, qa_qw_player *out)
{
    if (!out || !profile_valid(p)) return qa_net_reader_fail(r,"Invalid QuakeWorld player protocol");
    qa_qw_player s={0}; s.slot=qa_net_read_u8(r); s.flags=qa_net_read_u16(r);
    if (s.slot>=32 || (s.flags&~4095u)) return qa_net_reader_fail(r,"Invalid QuakeWorld player slot or flags");
    read_vector(r,p,s.origin,false); s.frame=qa_net_read_u8(r);
    if (s.flags&QA_QW_PF_MSEC) s.msec=qa_net_read_u8(r);
    qa_qw_command zero={0};
    if (s.flags&QA_QW_PF_COMMAND) qa_qw_read_delta_command(r,&zero,&s.command);
    for (unsigned i=0;i<3;++i) if (s.flags&((unsigned)QA_QW_PF_VELOCITY1<<i)) s.velocity[i]=qa_net_read_i16(r);
    s.model=s.flags&QA_QW_PF_MODEL ? read_index(r,p) : player_model;
    if (s.flags&QA_QW_PF_SKIN) s.skin=qa_net_read_u8(r);
    if (s.flags&QA_QW_PF_EFFECTS) s.effects=qa_net_read_u8(r);
    if (s.flags&QA_QW_PF_WEAPONFRAME) s.weapon_frame=qa_net_read_u8(r);
    if (r->failed) return false;
    *out=s; return true;
}
bool qa_qw_write_player(qa_net_writer *w, qa_net_protocol_id p, const qa_qw_player *s)
{
    if (!profile_valid(p) || !s || s->slot>=32 || (s->flags&~4095u) || s->frame>255 ||
        s->model>=index_limit(p) || s->skin>255 || s->effects>255 || s->weapon_frame>255 ||
        !finite3(s->origin) || !finite3(s->velocity))
        return qa_net_writer_fail(w,"Unrepresentable QuakeWorld player");
    if ((!(s->flags&QA_QW_PF_MSEC) && s->msec) ||
        (!(s->flags&QA_QW_PF_SKIN) && s->skin) ||
        (!(s->flags&QA_QW_PF_EFFECTS) && s->effects) ||
        (!(s->flags&QA_QW_PF_WEAPONFRAME) && s->weapon_frame))
        return qa_net_writer_fail(w,"QuakeWorld player flags omit nondefault fields");
    const qa_qw_command *command=&s->command;
    if (!(s->flags&QA_QW_PF_COMMAND) && (command->angles[0]!=0 || command->angles[1]!=0 ||
        command->angles[2]!=0 || command->forward || command->side || command->up ||
        command->msec || command->buttons || command->impulse))
        return qa_net_writer_fail(w,"QuakeWorld player flags omit command");
    for (unsigned i=0;i<3;++i) if (s->flags&((unsigned)QA_QW_PF_VELOCITY1<<i)) {
        if (s->velocity[i]<INT16_MIN || s->velocity[i]>INT16_MAX)
            return qa_net_writer_fail(w,"QuakeWorld player velocity exceeds short range");
    } else if (s->velocity[i]!=0) return qa_net_writer_fail(w,"QuakeWorld player flags omit velocity");
    qa_net_write_u8(w,s->slot); qa_net_write_u16(w,s->flags);
    write_vector(w,p,s->origin,false); qa_net_write_u8(w,(uint8_t)s->frame);
    if (s->flags&QA_QW_PF_MSEC) qa_net_write_u8(w,s->msec);
    qa_qw_command zero={0};
    if (s->flags&QA_QW_PF_COMMAND) qa_qw_write_delta_command(w,&zero,&s->command);
    for (unsigned i=0;i<3;++i) if (s->flags&((unsigned)QA_QW_PF_VELOCITY1<<i)) qa_net_write_i16(w,(int16_t)s->velocity[i]);
    if (s->flags&QA_QW_PF_MODEL) write_index(w,p,s->model);
    if (s->flags&QA_QW_PF_SKIN) qa_net_write_u8(w,(uint8_t)s->skin);
    if (s->flags&QA_QW_PF_EFFECTS) qa_net_write_u8(w,(uint8_t)s->effects);
    if (s->flags&QA_QW_PF_WEAPONFRAME) qa_net_write_u8(w,(uint8_t)s->weapon_frame);
    return !w->failed;
}

typedef struct entity_header { uint32_t number; unsigned bits,extra; } entity_header;
static bool read_entity_header(qa_net_reader *r, qa_net_protocol_id p, uint16_t word, entity_header *h)
{
    h->number=word&511; h->bits=word&~511u; h->extra=0;
    if (h->bits&U_MOREBITS) h->bits|=qa_net_read_u8(r);
    if (h->bits&U_EXTEND) {
        if (p.kind!=QA_NET_QW29) return qa_net_reader_fail(r,"Extended entity in QW28 packet");
        h->extra=qa_net_read_u8(r);
        if (h->extra&~31u) return qa_net_reader_fail(r,"Unknown QuakeWorld entity extension");
        if (h->extra&X_ENTITY) h->number|=(uint32_t)qa_net_read_u8(r)<<9;
    }
    if (!h->number || h->number>=entity_limit(p)) return qa_net_reader_fail(r,"QuakeWorld entity number exceeds protocol");
    if (((h->extra&X_MODEL) && !(h->bits&U_MODEL)) || ((h->extra&X_FRAME) && !(h->bits&U_FRAME)))
        return qa_net_reader_fail(r,"QuakeWorld high entity field has no low byte");
    if ((h->bits&U_REMOVE) && ((h->bits&~(unsigned)(U_REMOVE|U_MOREBITS|U_EXTEND)) || (h->extra&~(unsigned)X_ENTITY)))
        return qa_net_reader_fail(r,"Entity removal contains update fields");
    return !r->failed;
}
static bool read_entity(qa_net_reader *r, qa_net_protocol_id p, const entity_header *h,
                         const qa_q1_entity *base, qa_q1_entity *s)
{
    if (base) *s=*base; else qa_q1_entity_init(s);
    s->number=h->number; s->qw_flags=h->bits;
    if (h->bits&U_MODEL) s->model=qa_net_read_u8(r);
    if (h->bits&U_FRAME) s->frame=qa_net_read_u8(r);
    if (h->bits&U_COLORMAP) s->colormap=qa_net_read_u8(r);
    if (h->bits&U_SKIN) s->skin=qa_net_read_u8(r);
    if (h->bits&U_EFFECTS) s->effects=qa_net_read_u8(r);
    for (unsigned i=0;i<3;++i) {
        if (h->bits&((unsigned)U_ORIGIN1<<i)) s->origin[i]=qa_q1_read_coord(r,p);
        if (h->bits&angle_bits[i]) s->angles[i]=qa_q1_read_angle(r,p);
    }
    if (h->extra&X_MODEL) s->model|=(uint32_t)qa_net_read_u8(r)<<8;
    if (h->extra&X_FRAME) s->frame|=(uint32_t)qa_net_read_u8(r)<<8;
    if (h->extra&X_ALPHA) s->alpha=qa_net_read_u8(r);
    if (h->extra&X_SCALE) s->scale=qa_net_read_u8(r);
    return !r->failed;
}
static bool write_entity_header(qa_net_writer *w, uint32_t number, unsigned bits, unsigned extra)
{
    if (number>=512) extra|=X_ENTITY;
    if (extra) bits|=U_EXTEND;
    if (bits&255) bits|=U_MOREBITS;
    qa_net_write_u16(w,(uint16_t)((number&511)|(bits&~511u)));
    if (bits&U_MOREBITS) qa_net_write_u8(w,(uint8_t)bits);
    if (bits&U_EXTEND) qa_net_write_u8(w,(uint8_t)extra);
    if (extra&X_ENTITY) qa_net_write_u8(w,(uint8_t)(number>>9));
    return !w->failed;
}
static bool write_entity(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_entity *from,
                          const qa_q1_entity *to, bool force)
{
    qa_q1_entity empty; qa_q1_entity_init(&empty);
    if (!from) from=&empty;
    unsigned bits=0,extra=0;
    for (unsigned i=0;i<3;++i) {
        double difference=(double)to->origin[i]-(double)from->origin[i];
        if (difference < -0.1 || difference > 0.1) bits|=(unsigned)U_ORIGIN1<<i;
        if (to->angles[i]!=from->angles[i]) bits|=angle_bits[i];
    }
    if (to->model!=from->model) { bits|=U_MODEL; if (to->model>255) extra|=X_MODEL; }
    if (to->frame!=from->frame) { bits|=U_FRAME; if (to->frame>255) extra|=X_FRAME; }
    if (to->colormap!=from->colormap) bits|=U_COLORMAP;
    if (to->skin!=from->skin) bits|=U_SKIN;
    if (to->effects!=from->effects) bits|=U_EFFECTS;
    if (to->alpha!=from->alpha) extra|=X_ALPHA;
    if (to->scale!=from->scale) extra|=X_SCALE;
    if (to->qw_flags&U_SOLID) bits|=U_SOLID;
    /* A header without SOLID explicitly clears the previous solid state. */
    force=force || ((to->qw_flags^from->qw_flags)&U_SOLID)!=0;
    if (!bits && !extra && !force) return true;
    write_entity_header(w,to->number,bits,extra);
    if (bits&U_MODEL) qa_net_write_u8(w,(uint8_t)to->model);
    if (bits&U_FRAME) qa_net_write_u8(w,(uint8_t)to->frame);
    if (bits&U_COLORMAP) qa_net_write_u8(w,(uint8_t)to->colormap);
    if (bits&U_SKIN) qa_net_write_u8(w,(uint8_t)to->skin);
    if (bits&U_EFFECTS) qa_net_write_u8(w,(uint8_t)to->effects);
    for (unsigned i=0;i<3;++i) {
        if (bits&((unsigned)U_ORIGIN1<<i)) qa_q1_write_coord(w,p,to->origin[i]);
        if (bits&angle_bits[i]) qa_q1_write_angle(w,p,to->angles[i]);
    }
    if (extra&X_MODEL) qa_net_write_u8(w,(uint8_t)(to->model>>8));
    if (extra&X_FRAME) qa_net_write_u8(w,(uint8_t)(to->frame>>8));
    if (extra&X_ALPHA) qa_net_write_u8(w,to->alpha);
    if (extra&X_SCALE) qa_net_write_u8(w,to->scale);
    return !w->failed;
}
static const qa_qw_frame *delta_frame(const qa_qw_decoder *d, uint32_t sequence, uint8_t requested)
{
    const delta_request *request=&d->requests[sequence&(QA_QW_UPDATE_BACKUP-1)];
    if (request->valid && request->sequence==sequence) {
        uint32_t age=(sequence-request->base)&UINT32_C(0x7fffffff);
        return request->has_base && (uint8_t)request->base==requested && age>0 && age<63 ?
            qa_qw_decoder_frame(d,request->base) : NULL;
    }
    const qa_qw_frame *best=NULL; uint32_t best_age=UINT32_MAX;
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        const frame_slot *slot=&d->frames[i]; uint32_t age=(sequence-slot->frame.sequence)&UINT32_C(0x7fffffff);
        if (slot->valid && (uint8_t)slot->frame.sequence==requested && age>0 && age<QA_QW_UPDATE_BACKUP && age<best_age) {
            best=&slot->frame; best_age=age;
        }
    }
    return best;
}
static bool read_entities(qa_net_reader *r, qa_qw_decoder *d, uint32_t sequence, bool delta, qa_qw_service *m)
{
    uint8_t requested=delta ? qa_net_read_u8(r) : 0;
    const qa_qw_frame *previous=delta ? delta_frame(d,sequence,requested) : NULL;
    qa_qw_frame frame; qa_qw_frame *f=&frame;
    memset(f,0,sizeof(*f)); f->sequence=sequence; uint32_t last=0;
    size_t old=0,old_count=previous ? previous->count : 0;
    for (;;) {
        uint16_t word=qa_net_read_u16(r);
        if (r->failed) return false;
        if (!word) break;
        entity_header h;
        if (!read_entity_header(r,d->protocol,word,&h)) return false;
        if (h.number<=last) return qa_net_reader_fail(r,"Unsorted QuakeWorld packet entities");
        last=h.number;
        while (old<old_count && previous->entities[old].number<h.number) {
            if (f->count==QA_QW_MAX_PACKET_ENTITIES) return qa_net_reader_fail(r,"QuakeWorld packet entities overflow");
            f->entities[f->count++]=previous->entities[old++];
        }
        bool exists=old<old_count && previous->entities[old].number==h.number;
        if (h.bits&U_REMOVE) {
            if (!delta) return qa_net_reader_fail(r,"Entity removal in full QuakeWorld frame");
            if (exists) ++old;
            continue;
        }
        qa_q1_entity value;
        const qa_q1_entity *base=exists ? &previous->entities[old++] : qa_qw_decoder_baseline(d,h.number);
        if (!read_entity(r,d->protocol,&h,base,&value)) return false;
        if (f->count==QA_QW_MAX_PACKET_ENTITIES) return qa_net_reader_fail(r,"QuakeWorld packet entities overflow");
        f->entities[f->count++]=value;
    }
    while (old<old_count) {
        if (f->count==QA_QW_MAX_PACKET_ENTITIES) return qa_net_reader_fail(r,"QuakeWorld packet entities overflow");
        f->entities[f->count++]=previous->entities[old++];
    }
    if (delta && !previous) {
        m->kind=QA_QW_INVALID_DELTA;
        m->data.invalid_delta.sequence=sequence; m->data.invalid_delta.requested=requested;
        return true;
    }
    m->kind=QA_QW_PACKET_ENTITIES; m->data.packet.delta=delta;
    m->data.packet.from_sequence=previous ? previous->sequence : 0;
    if (!qa_qw_decoder_store_frame(d,f,r->error)) { r->failed=true; return false; }
    m->data.packet.frame=qa_qw_decoder_frame(d,sequence);
    return true;
}
static bool write_entities(qa_net_writer *w, qa_net_protocol_id p, const qa_qw_service *m, const qa_qw_decoder *d)
{
    const qa_qw_frame *to=m->data.packet.frame;
    if (!frame_valid(p,to)) return qa_net_writer_fail(w,"Invalid QuakeWorld output frame");
    if (d && (d->protocol.kind!=p.kind || d->protocol.flags!=p.flags))
        return qa_net_writer_fail(w,"QuakeWorld frame context protocol mismatch");
    const qa_qw_frame *from=m->data.packet.delta ? qa_qw_decoder_frame(d,m->data.packet.from_sequence) : NULL;
    if (m->data.packet.delta && !from) return qa_net_writer_fail(w,"Missing QuakeWorld output delta frame");
    qa_net_write_u8(w,from ? 48 : 47);
    if (from) qa_net_write_u8(w,(uint8_t)from->sequence);
    size_t old=0,next=0,old_count=from ? from->count : 0;
    while (old<old_count || next<to->count) {
        const qa_q1_entity *a=old<old_count ? &from->entities[old] : NULL;
        const qa_q1_entity *b=next<to->count ? &to->entities[next] : NULL;
        if (a && (!b || a->number<b->number)) { write_entity_header(w,a->number,U_REMOVE,0); ++old; }
        else if (b && (!a || b->number<a->number)) {
            write_entity(w,p,qa_qw_decoder_baseline(d,b->number),b,true); ++next;
        } else { write_entity(w,p,a,b,false); ++old; ++next; }
        if (w->failed) return false;
    }
    return qa_net_write_u16(w,0);
}

#define MOVE_FIELDS(X) X(gravity) X(stop_speed) X(max_speed) X(spectator_max_speed) \
    X(accelerate) X(air_accelerate) X(water_accelerate) X(friction) X(water_friction) X(entity_gravity)
static void read_movement(qa_net_reader *r, qa_qw_movevars *v)
{
#define READ_MOVE(field) v->field=read_float(r);
    MOVE_FIELDS(READ_MOVE)
#undef READ_MOVE
}
static void write_movement(qa_net_writer *w, const qa_qw_movevars *v)
{
#define WRITE_MOVE(field) write_float(w,v->field);
    MOVE_FIELDS(WRITE_MOVE)
#undef WRITE_MOVE
}
#undef MOVE_FIELDS
static bool read_list(qa_net_reader *r, qa_qw_decoder *d, qa_qw_service *m)
{
    uint32_t limit=precache_limit(d->protocol), first=read_index(r,d->protocol);
    if (first>=limit) return qa_net_reader_fail(r,"QuakeWorld precache start exceeds protocol");
    size_t count=0; uint32_t player_model=d->player_model;
    for (;;) {
        const char *name;
        if (!qa_q1_read_cstring(r,&name)) return false;
        if (!*name) break;
        if (count>=limit-first-1) return qa_net_reader_fail(r,"QuakeWorld precache overflow");
        if (count==d->names_capacity) {
            size_t capacity=d->names_capacity ? d->names_capacity*2 : 32;
            const char **names=realloc(d->names,capacity*sizeof(*names));
            if (!names) { error(r->error,QA_ERROR_MEMORY,"Allocating QuakeWorld precache names"); r->failed=true; return false; }
            d->names=names; d->names_capacity=capacity;
        }
        d->names[count++]=name;
        if (m->kind==QA_QW_MODEL_LIST && !strcmp(name,"progs/player.mdl")) player_model=first+(uint32_t)count;
    }
    uint32_t next=read_index(r,d->protocol);
    if (next>=limit) return qa_net_reader_fail(r,"QuakeWorld precache continuation exceeds protocol");
    if (r->failed) return false;
    m->data.list.first=first; m->data.list.next=next;
    m->data.list.count=count; m->data.list.names=d->names;
    d->player_model=player_model; return true;
}
bool qa_qw_service_read(qa_net_reader *r, qa_qw_decoder *d, uint32_t sequence, qa_qw_service *out)
{
    if (!d || !out || sequence>UINT32_C(0x7fffffff) || (r->bit&7))
        return qa_net_reader_fail(r,"Invalid QuakeWorld service reader");
    if (r->failed) return false;
    qa_qw_service m={0}; qa_net_protocol_id p=d->protocol;
    uint8_t op=qa_net_read_u8(r);
    switch (op) {
    case 1: m.kind=QA_QW_NOP; break;
    case 2: m.kind=QA_QW_DISCONNECT; break;
    case 3: case 38:
        m.kind=QA_QW_STAT; m.data.stat.index=qa_net_read_u8(r);
        m.data.stat.value=op==3 ? qa_net_read_u8(r) : qa_net_read_i32(r); break;
    case 5: case 39:
        m.kind=op==5 ? QA_QW_SET_VIEW : QA_QW_MUZZLE_FLASH;
        m.data.entity=qa_net_read_u16(r); break;
    case 6: {
        m.kind=QA_QW_SOUND; qa_q1_sound *s=&m.data.sound;
        uint16_t word=qa_net_read_u16(r);
        s->entity=(word>>3)&1023; s->channel=word&7;
        s->volume=word&32768 ? qa_net_read_u8(r) : 255;
        s->attenuation=word&16384 ? (float)qa_net_read_u8(r)/64.0f : 1;
        s->index=read_index(r,p); read_vector(r,p,s->origin,false); break;
    }
    case 8:
        m.kind=QA_QW_PRINT; m.data.text.level=qa_net_read_u8(r);
        qa_q1_read_cstring(r,&m.data.text.value); break;
    case 9: case 26: case 31:
        m.kind=op==9 ? QA_QW_STUFFTEXT : op==26 ? QA_QW_CENTER_PRINT : QA_QW_FINALE;
        qa_q1_read_cstring(r,&m.data.text.value); break;
    case 10: m.kind=QA_QW_SET_ANGLE; read_vector(r,p,m.data.angles,true); break;
    case 11: {
        m.kind=QA_QW_SERVER_DATA; qa_qw_serverdata *s=&m.data.server;
        if (!qa_q1_read_protocol(r,true,&s->protocol)) return false;
        s->server_count=qa_net_read_i32(r); qa_q1_read_cstring(r,&s->game_directory);
        uint8_t slot=qa_net_read_u8(r); s->player_slot=slot&127; s->spectator=(slot&128)!=0;
        qa_q1_read_cstring(r,&s->level); read_movement(r,&s->movement); break;
    }
    case 12:
        m.kind=QA_QW_LIGHT_STYLE; m.data.light_style.index=qa_net_read_u8(r);
        qa_q1_read_cstring(r,&m.data.light_style.value); break;
    case 14: case 36:
        m.kind=op==14 ? QA_QW_FRAGS : QA_QW_PING;
        m.data.score.slot=qa_net_read_u8(r); m.data.score.value=qa_net_read_i16(r); break;
    case 16: {
        m.kind=QA_QW_STOP_SOUND; uint16_t word=qa_net_read_u16(r);
        m.data.stop_sound.entity=(uint16_t)(word>>3); m.data.stop_sound.channel=(uint8_t)(word&7); break;
    }
    case 19:
        m.kind=QA_QW_DAMAGE; m.data.damage.armor=qa_net_read_u8(r); m.data.damage.blood=qa_net_read_u8(r);
        read_vector(r,p,m.data.damage.origin,false); break;
    case 20: case 22: {
        m.kind=op==20 ? QA_QW_STATIC : QA_QW_BASELINE;
        uint32_t number=op==22 ? qa_net_read_u16(r) : 0;
        if (number>=entity_limit(p)) return qa_net_reader_fail(r,"QuakeWorld baseline number exceeds protocol");
        if (!qa_qw_read_baseline(r,p,&m.data.baseline)) return false;
        m.data.baseline.number=number; break;
    }
    case 23: m.kind=QA_QW_TEMPORARY_ENTITY; qa_q1_read_temp(r,p,&m.data.temporary); break;
    case 24: m.kind=QA_QW_PAUSE; m.data.paused=qa_net_read_u8(r)!=0; break;
    case 27: m.kind=QA_QW_KILLED_MONSTER; break;
    case 28: m.kind=QA_QW_FOUND_SECRET; break;
    case 29:
        m.kind=QA_QW_STATIC_SOUND; read_vector(r,p,m.data.sound.origin,false);
        m.data.sound.index=read_index(r,p); m.data.sound.volume=qa_net_read_u8(r);
        m.data.sound.attenuation=(float)qa_net_read_u8(r)/64.0f; break;
    case 30:
        m.kind=QA_QW_INTERMISSION; read_vector(r,p,m.data.intermission.origin,false);
        read_vector(r,p,m.data.intermission.angles,true); break;
    case 32: m.kind=QA_QW_CD_TRACK; m.data.byte=qa_net_read_u8(r); break;
    case 33: m.kind=QA_QW_SELL_SCREEN; break;
    case 34: case 35: m.kind=QA_QW_KICK; m.data.kick=op==34 ? -2 : -4; break;
    case 37:
        m.kind=QA_QW_ENTER_TIME; m.data.enter_time.slot=qa_net_read_u8(r);
        m.data.enter_time.seconds=read_float(r); break;
    case 40:
        m.kind=QA_QW_USERINFO; m.data.userinfo.slot=qa_net_read_u8(r);
        m.data.userinfo.user_id=qa_net_read_i32(r); qa_q1_read_cstring(r,&m.data.userinfo.value); break;
    case 41: {
        m.kind=QA_QW_DOWNLOAD; int16_t length=qa_net_read_i16(r); m.data.download.percent=qa_net_read_u8(r);
        if (length<-1) return qa_net_reader_fail(r,"Invalid QuakeWorld download length");
        m.data.download.missing=length==-1;
        if (length>=0 && m.data.download.percent>100) return qa_net_reader_fail(r,"Invalid QuakeWorld download percentage");
        if (length>=0) qa_net_read_bytes(r,(size_t)length,&m.data.download.bytes);
        break;
    }
    case 42: m.kind=QA_QW_PLAYER; qa_qw_read_player(r,p,d->player_model,&m.data.player); break;
    case 43:
        m.kind=QA_QW_NAILS; m.data.nails.count=qa_net_read_u8(r);
        m.data.nails.items=d->nails;
        for (size_t i=0;i<m.data.nails.count;++i) {
            uint8_t b[6]; if (!qa_net_read_data(r,b,sizeof(b))) return false;
            qa_qw_nail *n=d->nails+i;
            n->origin[0]=(float)(((b[0]|((b[1]&15)<<8))<<1)-4096);
            n->origin[1]=(float)((((b[1]>>4)|(b[2]<<4))<<1)-4096);
            n->origin[2]=(float)(((b[3]|((b[4]&15)<<8))<<1)-4096);
            n->pitch=(float)(360*(b[4]>>4)/16); n->yaw=(float)(360*b[5]/256);
        }
        break;
    case 44: m.kind=QA_QW_CHOKE_COUNT; m.data.byte=qa_net_read_u8(r); break;
    case 45: case 46:
        m.kind=op==45 ? QA_QW_MODEL_LIST : QA_QW_SOUND_LIST;
        if (!read_list(r,d,&m)) return false;
        break;
    case 47: case 48:
        if (!read_entities(r,d,sequence,op==48,&m)) return false;
        break;
    case 49: case 50:
        m.kind=op==49 ? QA_QW_MAX_SPEED : QA_QW_ENTITY_GRAVITY; m.data.scalar=read_float(r); break;
    case 51: case 52:
        m.kind=op==51 ? QA_QW_SET_INFO : QA_QW_SERVER_INFO;
        if (op==51) m.data.info.slot=qa_net_read_u8(r);
        qa_q1_read_cstring(r,&m.data.info.key); qa_q1_read_cstring(r,&m.data.info.value); break;
    case 53:
        m.kind=QA_QW_PACKET_LOSS; m.data.packet_loss.slot=qa_net_read_u8(r);
        m.data.packet_loss.percent=qa_net_read_u8(r); break;
    default: return qa_net_reader_fail(r,"Unknown QuakeWorld service opcode");
    }
    if (r->failed) return false;
    if (m.kind==QA_QW_SERVER_DATA) qa_qw_decoder_reset(d,m.data.server.protocol,NULL);
    if (m.kind==QA_QW_BASELINE && !qa_qw_decoder_set_baseline(d,&m.data.baseline,r->error)) { r->failed=true; return false; }
    *out=m; return true;
}

static bool attenuation_byte(qa_net_writer *w, float attenuation, uint8_t *out)
{
    if (!isfinite(attenuation) || attenuation<0 || attenuation*64.0f>255) {
        qa_net_writer_fail(w,"QuakeWorld attenuation exceeds byte range");
        return false;
    }
    *out=(uint8_t)(attenuation*64.0f); return true;
}
static bool write_nails(qa_net_writer *w, const qa_qw_service *m)
{
    if (m->data.nails.count>QA_QW_MAX_NAILS || (m->data.nails.count && !m->data.nails.items))
        return qa_net_writer_fail(w,"Invalid QuakeWorld nails");
    for (size_t i=0;i<m->data.nails.count;++i) {
        const qa_qw_nail *n=&m->data.nails.items[i];
        if (!finite3(n->origin) || !isfinite(n->pitch) || !isfinite(n->yaw))
            return qa_net_writer_fail(w,"Non-finite QuakeWorld nail");
        for (unsigned axis=0;axis<3;++axis) {
            float shifted=n->origin[axis]+4096.0f;
            int32_t packed=qa_source_float_to_i32(shifted)>>1;
            if (packed<0 || packed>4095) return qa_net_writer_fail(w,"QuakeWorld nail exceeds coordinate range");
        }
    }
    return qa_qw_source_write_nails(w,m->data.nails.items,m->data.nails.count);
}
static bool write_list(qa_net_writer *w, qa_net_protocol_id p, const qa_qw_service *m)
{
    uint32_t first=m->data.list.first,limit=precache_limit(p);
    size_t count=m->data.list.count;
    if (first>=limit || count>=limit-first || m->data.list.next>=limit || (count && !m->data.list.names))
        return qa_net_writer_fail(w,"QuakeWorld precache list exceeds protocol");
    qa_net_write_u8(w,m->kind==QA_QW_MODEL_LIST ? 45 : 46); write_index(w,p,first);
    for (size_t i=0;i<count;++i) {
        if (!m->data.list.names[i] || !*m->data.list.names[i]) return qa_net_writer_fail(w,"Empty QuakeWorld precache name");
        qa_net_write_string(w,m->data.list.names[i]);
    }
    qa_net_write_u8(w,0); write_index(w,p,m->data.list.next); return !w->failed;
}
bool qa_qw_service_write(qa_net_writer *w, qa_net_protocol_id p, const qa_qw_service *m, const qa_qw_decoder *d)
{
    if (!m || !profile_valid(p) || (w->bit&7)) return qa_net_writer_fail(w,"Invalid QuakeWorld service writer");
    if (w->failed) return false;
    switch (m->kind) {
    case QA_QW_NOP: qa_net_write_u8(w,1); break;
    case QA_QW_DISCONNECT: qa_net_write_u8(w,2); break;
    case QA_QW_STAT: {
        bool byte=m->data.stat.value>=0 && m->data.stat.value<=255;
        qa_net_write_u8(w,byte ? 3 : 38); qa_net_write_u8(w,m->data.stat.index);
        if (byte) qa_net_write_u8(w,(uint8_t)m->data.stat.value); else qa_net_write_i32(w,m->data.stat.value);
        break;
    }
    case QA_QW_SET_VIEW: case QA_QW_MUZZLE_FLASH:
        qa_net_write_u8(w,m->kind==QA_QW_SET_VIEW ? 5 : 39); qa_net_write_u16(w,m->data.entity); break;
    case QA_QW_SOUND: {
        const qa_q1_sound *s=&m->data.sound; uint8_t attenuation;
        if (s->entity>=1024 || s->channel>=8 || s->index>=index_limit(p))
            return qa_net_writer_fail(w,"QuakeWorld sound exceeds protocol");
        if (!attenuation_byte(w,s->attenuation,&attenuation)) return false;
        uint16_t word=(uint16_t)((s->entity<<3)|s->channel);
        if (s->volume!=255) word|=32768;
        if (s->attenuation!=1) word|=16384;
        qa_net_write_u8(w,6); qa_net_write_u16(w,word);
        if (word&32768) qa_net_write_u8(w,s->volume);
        if (word&16384) qa_net_write_u8(w,attenuation);
        write_index(w,p,s->index); write_vector(w,p,s->origin,false); break;
    }
    case QA_QW_PRINT:
        qa_net_write_u8(w,8); qa_net_write_u8(w,m->data.text.level); qa_net_write_string(w,m->data.text.value); break;
    case QA_QW_STUFFTEXT: case QA_QW_CENTER_PRINT: case QA_QW_FINALE:
        qa_net_write_u8(w,m->kind==QA_QW_STUFFTEXT ? 9 : m->kind==QA_QW_CENTER_PRINT ? 26 : 31);
        qa_net_write_string(w,m->data.text.value); break;
    case QA_QW_SET_ANGLE: qa_net_write_u8(w,10); write_vector(w,p,m->data.angles,true); break;
    case QA_QW_SERVER_DATA: {
        const qa_qw_serverdata *s=&m->data.server;
        if (!profile_valid(s->protocol) || s->player_slot>=128)
            return qa_net_writer_fail(w,"Invalid QuakeWorld server data");
        qa_net_write_u8(w,11); qa_q1_write_protocol(w,s->protocol); qa_net_write_i32(w,s->server_count);
        qa_net_write_string(w,s->game_directory); qa_net_write_u8(w,(uint8_t)(s->player_slot|(s->spectator ? 128 : 0)));
        qa_net_write_string(w,s->level); write_movement(w,&s->movement); break;
    }
    case QA_QW_LIGHT_STYLE:
        qa_net_write_u8(w,12); qa_net_write_u8(w,m->data.light_style.index);
        qa_net_write_string(w,m->data.light_style.value); break;
    case QA_QW_FRAGS: case QA_QW_PING:
        qa_net_write_u8(w,m->kind==QA_QW_FRAGS ? 14 : 36);
        qa_net_write_u8(w,m->data.score.slot); qa_net_write_i16(w,m->data.score.value); break;
    case QA_QW_STOP_SOUND:
        if (m->data.stop_sound.entity>=8192 || m->data.stop_sound.channel>=8)
            return qa_net_writer_fail(w,"QuakeWorld stopped sound exceeds channel word");
        qa_net_write_u8(w,16); qa_net_write_u16(w,(uint16_t)((m->data.stop_sound.entity<<3)|m->data.stop_sound.channel)); break;
    case QA_QW_DAMAGE:
        qa_net_write_u8(w,19); qa_net_write_u8(w,m->data.damage.armor); qa_net_write_u8(w,m->data.damage.blood);
        write_vector(w,p,m->data.damage.origin,false); break;
    case QA_QW_STATIC: case QA_QW_BASELINE:
        if (m->kind==QA_QW_STATIC && m->data.baseline.number)
            return qa_net_writer_fail(w,"Static QuakeWorld entity has a dynamic number");
        qa_net_write_u8(w,m->kind==QA_QW_STATIC ? 20 : 22);
        if (m->kind==QA_QW_BASELINE) {
            if (m->data.baseline.number>=entity_limit(p)) return qa_net_writer_fail(w,"QuakeWorld baseline exceeds protocol");
            qa_net_write_u16(w,(uint16_t)m->data.baseline.number);
        }
        qa_qw_write_baseline(w,p,&m->data.baseline); break;
    case QA_QW_TEMPORARY_ENTITY: qa_net_write_u8(w,23); qa_q1_write_temp(w,p,&m->data.temporary); break;
    case QA_QW_PAUSE: qa_net_write_u8(w,24); qa_net_write_u8(w,m->data.paused ? 1 : 0); break;
    case QA_QW_KILLED_MONSTER: qa_net_write_u8(w,27); break;
    case QA_QW_FOUND_SECRET: qa_net_write_u8(w,28); break;
    case QA_QW_STATIC_SOUND: {
        const qa_q1_sound *s=&m->data.sound; uint8_t attenuation;
        if (s->entity || s->channel) return qa_net_writer_fail(w,"Static QuakeWorld sound has entity channel");
        if (!attenuation_byte(w,s->attenuation,&attenuation)) return false;
        qa_net_write_u8(w,29); write_vector(w,p,s->origin,false); write_index(w,p,s->index);
        qa_net_write_u8(w,s->volume); qa_net_write_u8(w,attenuation); break;
    }
    case QA_QW_INTERMISSION:
        qa_net_write_u8(w,30); write_vector(w,p,m->data.intermission.origin,false);
        write_vector(w,p,m->data.intermission.angles,true); break;
    case QA_QW_CD_TRACK: qa_net_write_u8(w,32); qa_net_write_u8(w,m->data.byte); break;
    case QA_QW_SELL_SCREEN: qa_net_write_u8(w,33); break;
    case QA_QW_KICK:
        if (m->data.kick!=-2 && m->data.kick!=-4) return qa_net_writer_fail(w,"Invalid QuakeWorld kick");
        qa_net_write_u8(w,m->data.kick==-2 ? 34 : 35); break;
    case QA_QW_ENTER_TIME:
        qa_net_write_u8(w,37); qa_net_write_u8(w,m->data.enter_time.slot); write_float(w,m->data.enter_time.seconds); break;
    case QA_QW_USERINFO:
        qa_net_write_u8(w,40); qa_net_write_u8(w,m->data.userinfo.slot);
        qa_net_write_i32(w,m->data.userinfo.user_id); qa_net_write_string(w,m->data.userinfo.value); break;
    case QA_QW_DOWNLOAD:
        if (!m->data.download.missing && (m->data.download.bytes.size>QA_QW_DOWNLOAD_BLOCK || m->data.download.percent>100))
            return qa_net_writer_fail(w,"Invalid QuakeWorld download block size or percentage");
        qa_net_write_u8(w,41);
        qa_net_write_i16(w,(int16_t)(m->data.download.missing ? -1 : (int)m->data.download.bytes.size));
        qa_net_write_u8(w,m->data.download.missing ? 0 : m->data.download.percent);
        if (!m->data.download.missing) qa_net_write_data(w,m->data.download.bytes.data,m->data.download.bytes.size);
        break;
    case QA_QW_PLAYER: qa_net_write_u8(w,42); qa_qw_write_player(w,p,&m->data.player); break;
    case QA_QW_NAILS: return write_nails(w,m);
    case QA_QW_CHOKE_COUNT: qa_net_write_u8(w,44); qa_net_write_u8(w,m->data.byte); break;
    case QA_QW_MODEL_LIST: case QA_QW_SOUND_LIST: return write_list(w,p,m);
    case QA_QW_PACKET_ENTITIES: return write_entities(w,p,m,d);
    case QA_QW_INVALID_DELTA: return qa_net_writer_fail(w,"Invalid delta is a receive-only event");
    case QA_QW_MAX_SPEED: case QA_QW_ENTITY_GRAVITY:
        qa_net_write_u8(w,m->kind==QA_QW_MAX_SPEED ? 49 : 50); write_float(w,m->data.scalar); break;
    case QA_QW_SET_INFO: case QA_QW_SERVER_INFO:
        qa_net_write_u8(w,m->kind==QA_QW_SET_INFO ? 51 : 52);
        if (m->kind==QA_QW_SET_INFO) qa_net_write_u8(w,m->data.info.slot);
        qa_net_write_string(w,m->data.info.key); qa_net_write_string(w,m->data.info.value); break;
    case QA_QW_PACKET_LOSS:
        qa_net_write_u8(w,53); qa_net_write_u8(w,m->data.packet_loss.slot); qa_net_write_u8(w,m->data.packet_loss.percent); break;
    default: return qa_net_writer_fail(w,"Unknown QuakeWorld service kind");
    }
    return !w->failed;
}

static void save_entity(qa_net_writer *w, const qa_q1_entity *s)
{
    qa_net_write_u32(w,s->number); qa_net_write_u32(w,s->model); qa_net_write_u32(w,s->frame);
    qa_net_write_u32(w,s->colormap); qa_net_write_u32(w,s->skin); qa_net_write_u32(w,s->effects);
    for (unsigned i=0;i<3;++i) qa_net_write_f32(w,s->origin[i]);
    for (unsigned i=0;i<3;++i) qa_net_write_f32(w,s->angles[i]);
    qa_net_write_u8(w,s->alpha); qa_net_write_u8(w,s->scale); qa_net_write_u32(w,s->qw_flags);
}
static bool restore_entity(qa_net_reader *r, qa_net_protocol_id p, qa_q1_entity *s)
{
    qa_q1_entity_init(s);
    s->number=qa_net_read_u32(r); s->model=qa_net_read_u32(r); s->frame=qa_net_read_u32(r);
    s->colormap=qa_net_read_u32(r); s->skin=qa_net_read_u32(r); s->effects=qa_net_read_u32(r);
    for (unsigned i=0;i<3;++i) s->origin[i]=read_float(r);
    for (unsigned i=0;i<3;++i) s->angles[i]=read_float(r);
    s->alpha=qa_net_read_u8(r); s->scale=qa_net_read_u8(r); s->qw_flags=qa_net_read_u32(r);
    if (!entity_valid(p,s)) return qa_net_reader_fail(r,"Invalid entity in QuakeWorld checkpoint");
    return !r->failed;
}
bool qa_qw_decoder_save(qa_net_writer *w, const qa_qw_decoder *d)
{
    if (!d || (w->bit&7)) return qa_net_writer_fail(w,"Invalid QuakeWorld checkpoint writer");
    uint32_t baselines=0; uint8_t frames=0,requests=0;
    for (size_t i=0;i<d->baseline_capacity;++i) if (d->baselines[i].valid) ++baselines;
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        if (d->frames[i].valid) ++frames;
        if (d->requests[i].valid) ++requests;
    }
    qa_net_write_u32(w,UINT32_C(0x43445751));
    qa_q1_write_protocol(w,d->protocol); qa_net_write_u32(w,d->player_model); qa_net_write_u32(w,baselines);
    for (size_t i=0;i<d->baseline_capacity && !w->failed;++i)
        if (d->baselines[i].valid) save_entity(w,&d->baselines[i].entity);
    qa_net_write_u8(w,frames);
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP && !w->failed;++i) if (d->frames[i].valid) {
        const qa_qw_frame *f=&d->frames[i].frame;
        qa_net_write_u32(w,f->sequence); qa_net_write_u8(w,(uint8_t)f->count);
        for (size_t j=0;j<f->count;++j) save_entity(w,&f->entities[j]);
    }
    qa_net_write_u8(w,requests);
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) if (d->requests[i].valid) {
        const delta_request *q=&d->requests[i];
        qa_net_write_u32(w,q->sequence); qa_net_write_u8(w,q->has_base ? 1 : 0); qa_net_write_u32(w,q->base);
    }
    return !w->failed;
}
bool qa_qw_decoder_restore(qa_net_reader *r, qa_qw_decoder *d)
{
    if (!d || (r->bit&7)) return qa_net_reader_fail(r,"Invalid QuakeWorld checkpoint reader");
    uint32_t magic=qa_net_read_u32(r);
    if (magic!=UINT32_C(0x43445751)) return qa_net_reader_fail(r,"Unsupported QuakeWorld checkpoint");
    qa_net_protocol_id p;
    if (!qa_q1_read_protocol(r,true,&p)) return false;
    qa_qw_decoder *next=qa_qw_decoder_create(p,r->error);
    if (!next) { r->failed=true; return false; }
    uint32_t model=qa_net_read_u32(r),baselines=qa_net_read_u32(r);
    if (!qa_qw_decoder_set_player_model(next,model,r->error)) { r->failed=true; goto fail; }
    if (baselines>entity_limit(p) || (size_t)baselines>qa_net_reader_remaining(r)/54) {
        qa_net_reader_fail(r,"Invalid QuakeWorld checkpoint baseline count"); goto fail;
    }
    for (uint32_t i=0;i<baselines;++i) {
        qa_q1_entity s;
        if (!restore_entity(r,p,&s)) goto fail;
        if (qa_qw_decoder_baseline(next,s.number)) { qa_net_reader_fail(r,"Duplicate QuakeWorld checkpoint baseline"); goto fail; }
        if (!qa_qw_decoder_set_baseline(next,&s,r->error)) { r->failed=true; goto fail; }
    }
    uint8_t frames=qa_net_read_u8(r);
    if (frames>QA_QW_UPDATE_BACKUP) { qa_net_reader_fail(r,"Too many QuakeWorld checkpoint frames"); goto fail; }
    for (unsigned i=0;i<frames;++i) {
        uint32_t sequence=qa_net_read_u32(r); size_t count=qa_net_read_u8(r);
        frame_slot *slot=&next->frames[sequence&(QA_QW_UPDATE_BACKUP-1)];
        if (slot->valid || count>QA_QW_MAX_PACKET_ENTITIES) { qa_net_reader_fail(r,"Invalid QuakeWorld checkpoint frame"); goto fail; }
        slot->frame.sequence=sequence; slot->frame.count=count;
        for (size_t j=0;j<count;++j) if (!restore_entity(r,p,&slot->frame.entities[j])) goto fail;
        if (!frame_valid(p,&slot->frame)) { qa_net_reader_fail(r,"Unsorted QuakeWorld checkpoint frame"); goto fail; }
        slot->valid=true;
    }
    uint8_t requests=qa_net_read_u8(r);
    if (requests>QA_QW_UPDATE_BACKUP) { qa_net_reader_fail(r,"Too many QuakeWorld checkpoint delta requests"); goto fail; }
    for (unsigned i=0;i<requests;++i) {
        uint32_t sequence=qa_net_read_u32(r); uint8_t present=qa_net_read_u8(r); uint32_t base=qa_net_read_u32(r);
        delta_request *q=&next->requests[sequence&(QA_QW_UPDATE_BACKUP-1)];
        if (q->valid || present>1 || sequence>UINT32_C(0x7fffffff) || base>UINT32_C(0x7fffffff)) {
            qa_net_reader_fail(r,"Invalid QuakeWorld checkpoint delta request"); goto fail;
        }
        *q=(delta_request){true,present!=0,sequence,base};
    }
    if (r->failed) goto fail;
    free(d->baselines); free(d->names); *d=*next; free(next); return true;
fail:
    qa_qw_decoder_destroy(next); return false;
}

static bool decoder_admitted(const qa_qw_decoder *decoder, qa_net_protocol_id protocol)
{
    return decoder && profile_valid(protocol) && decoder->protocol.kind==protocol.kind &&
        decoder->protocol.revision==protocol.revision && decoder->protocol.flags==protocol.flags;
}
bool qa_qw_decoder_server_cut(const qa_qw_decoder *decoder, qa_net_protocol_id protocol,
    uint32_t outgoing_sequence, qa_error *failure)
{
    if (!decoder_admitted(decoder,protocol) || outgoing_sequence>UINT32_C(0x80000000) ||
        decoder->player_model || decoder->names || decoder->names_capacity)
        return error(failure,QA_ERROR_FORMAT,"QuakeWorld server frame owner contains client-only state");
    for (size_t i=0;i<decoder->baseline_capacity;++i) {
        const baseline_slot *slot=&decoder->baselines[i];
        if (slot->valid && (!i || slot->entity.number!=i ||
            !entity_valid(protocol,&slot->entity) || slot->entity.effects || slot->entity.qw_flags))
            return error(failure,QA_ERROR_FORMAT,"QuakeWorld server baseline differs from its physical source row");
    }
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        const frame_slot *slot=&decoder->frames[i];
        if (decoder->requests[i].valid || (slot->valid &&
            (!frame_valid(protocol,&slot->frame) || slot->frame.sequence>=outgoing_sequence ||
             (slot->frame.sequence&(QA_QW_UPDATE_BACKUP-1))!=i)))
            return error(failure,QA_ERROR_FORMAT,"QuakeWorld server frame history exceeds its transmitted channel cut");
    }
    return true;
}
bool qa_qw_decoder_checkpoint(const qa_qw_decoder *decoder, qa_net_protocol_id protocol,
    qa_buffer *out, qa_error *failure)
{
    if (!out || !decoder_admitted(decoder,protocol))
        return error(failure,QA_ERROR_ARGUMENT,"QuakeWorld decoder requires admitted source dialect");
    size_t capacity=32;
    for (size_t i=0;i<decoder->baseline_capacity;++i) if (decoder->baselines[i].valid) capacity+=54;
    for (size_t i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        if (decoder->frames[i].valid) capacity+=5+54*decoder->frames[i].frame.count;
        if (decoder->requests[i].valid) capacity+=9;
    }
    uint8_t *data=malloc(capacity);
    if (!data) return error(failure,QA_ERROR_MEMORY,"Encoding QuakeWorld decoder continuation");
    qa_net_writer writer; qa_net_writer_init(&writer,data,capacity,failure);
    if (!qa_qw_decoder_save(&writer,decoder)) { free(data); return false; }
    *out=(qa_buffer){data,qa_net_writer_size(&writer)}; return true;
}
bool qa_qw_decoder_restore_checkpoint(qa_bytes bytes, qa_net_protocol_id protocol, qa_qw_decoder **out, qa_error *failure)
{
    if (!out || *out || !profile_valid(protocol) || (bytes.size && !bytes.data))
        return error(failure,QA_ERROR_ARGUMENT,"QuakeWorld decoder restore requires admitted source and empty output");
    qa_net_reader reader; qa_net_reader_init(&reader,bytes,failure);
    qa_net_protocol_id saved;
    if (qa_net_read_u32(&reader)!=UINT32_C(0x43445751) ||
        !qa_q1_read_protocol(&reader,true,&saved)) return qa_net_reader_fail(&reader,"Invalid QuakeWorld decoder continuation schema");
    if (saved.kind!=protocol.kind || saved.revision!=protocol.revision || saved.flags!=protocol.flags)
        return qa_net_reader_fail(&reader,"QuakeWorld decoder source admission differs");
    qa_qw_decoder *candidate=qa_qw_decoder_create(protocol,failure);
    if (!candidate) return false;
    qa_net_reader_init(&reader,bytes,failure);
    if (!qa_qw_decoder_restore(&reader,candidate) || !qa_net_reader_finish(&reader) ||
        !decoder_admitted(candidate,protocol)) { qa_qw_decoder_destroy(candidate); return false; }
    *out=candidate; return true;
}
