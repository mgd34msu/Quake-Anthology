#include "qa/network_q1.h"
#include <math.h>
#include <string.h>

void qa_qw_history_init(qa_qw_history *h) { if (h) memset(h,0,sizeof(*h)); }
bool qa_qw_history_record(qa_qw_history *h, uint32_t sequence, double sent,
                           const qa_qw_command *c, qa_error *e)
{
    if (!h || !c || sequence>0x7fffffff || !isfinite(sent)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid QuakeWorld history frame"); return false;
    }
    for (unsigned i=0;i<3;++i) if (!isfinite(c->angles[i])) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Non-finite QuakeWorld history angle"); return false;
    }
    h->frames[sequence&63]=(qa_qw_history_frame){sequence,sent,*c,true};
    for (unsigned i=0;i<QA_QW_UPDATE_BACKUP;++i) {
        qa_qw_history_frame *f=&h->frames[i];
        uint32_t age=(sequence-f->sequence)&0x7fffffff;
        if (f->valid && age>=64 && age<0x40000000) f->valid=false;
    }
    return true;
}
const qa_qw_history_frame *qa_qw_history_get(const qa_qw_history *h, uint32_t sequence)
{
    if (!h || sequence>0x7fffffff) return NULL;
    const qa_qw_history_frame *f=&h->frames[sequence&63];
    return f->valid && f->sequence==sequence ? f : NULL;
}
void qa_qw_history_acknowledge(qa_qw_history *h, uint32_t sequence, double received)
{
    const qa_qw_history_frame *f=qa_qw_history_get(h,sequence);
    if (!f || !isfinite(received)) return;
    double latency=received-f->sent_seconds;
    if (latency<0 || latency>1) return;
    h->latency_seconds=latency<h->latency_seconds ? latency : h->latency_seconds+0.001;
}
bool qa_qw_history_bundle(const qa_qw_history *h, uint32_t sequence, uint8_t loss,
                           qa_qw_move *out, qa_error *e)
{
    const qa_qw_history_frame *a=qa_qw_history_get(h,(sequence-2)&0x7fffffff);
    const qa_qw_history_frame *b=qa_qw_history_get(h,(sequence-1)&0x7fffffff);
    const qa_qw_history_frame *c=qa_qw_history_get(h,sequence);
    if (!out || !a || !b || !c) {
        qa_error_set(e,QA_ERROR_NOT_FOUND,sequence,"Three recorded commands are required"); return false;
    }
    *out=(qa_qw_move){a->command,b->command,c->command,loss}; return true;
}
bool qa_qw_history_replayable(const qa_qw_history *h, uint32_t ack, uint32_t outgoing)
{
    if (!h || ack>0x7fffffff || outgoing>0x7fffffff) return false;
    uint32_t count=(outgoing-ack)&0x7fffffff;
    if (count>=63 || count<2) return false;
    for (uint32_t i=1;i<count;++i)
        if (!qa_qw_history_get(h,(ack+i)&0x7fffffff)) return false;
    return true;
}
double qa_qw_prediction_time(const qa_qw_history *h, double now, double push)
{
    if (!h || !isfinite(now) || !isfinite(push)) return now;
    return fmin(now,now-h->latency_seconds-fmin(0,push)/1000.0);
}
void qa_qw_prediction_interpolate(const float a[3], const float av[3],
                                   const float b[3], const float bv[3],
                                   double at, double bt, double target,
                                   float out[3], float velocity[3])
{
    bool teleport=false;
    for (unsigned i=0;i<3;++i) if (fabsf(a[i]-b[i])>128) teleport=true;
    double f=bt==at ? 0 : fmax(0,fmin(1,(target-at)/(bt-at)));
    for (unsigned i=0;i<3;++i) {
        out[i]=teleport ? b[i] : (float)((double)a[i]+f*((double)b[i]-a[i]));
        velocity[i]=teleport ? bv[i] : (float)((double)av[i]+f*((double)bv[i]-av[i]));
    }
}
