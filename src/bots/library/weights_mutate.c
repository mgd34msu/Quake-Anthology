#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_operations.h"

static bool open(qa_bot_weights *weights,qa_error *error) {
    if(!weights || !weights->source) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Fuzzy operation requires its actual source configuration");return false;
    }
    return bot_fuzzy_owned_open(weights->source,error);
}
bool qa_bot_weights_scale(qa_bot_weights *weights,const char *name,float scale,qa_error *error) {
    if(!name) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Fuzzy scale requires its actual weight name");return false;}
    return open(weights,error) &&
        bot_fuzzy_scale(&weights->source->source,(qa_bytes){(const uint8_t *)name,strlen(name)},scale,error);
}
bool qa_bot_weights_scale_range(qa_bot_weights *weights,float scale,qa_error *error) {
    return open(weights,error) && bot_fuzzy_scale_range(&weights->source->source,scale,error);
}
bool qa_bot_weights_evolve(qa_bot_weights *weights,const qa_bot_random_source *random,qa_error *error) {
    qa_bot_weights_retain(weights);
    bool ok=open(weights,error) && bot_fuzzy_evolve(&weights->source->source,random,error);
    qa_bot_weights_release(weights);return ok;
}
typedef struct source_breed_report {
    void *context;
    bool (*report)(void *,const char *,qa_error *);
    bool matched;
} source_breed_report;
static bool source_report(void *context,const char *message,qa_error *error) {
    source_breed_report *report=context;report->matched=false;
    return !report->report || report->report(report->context,message,error);
}
bool qa_bot_weights_interbreed_report(qa_bot_weights *out,const qa_bot_weights *first,
    const qa_bot_weights *second,void *context,bool (*report)(void *,const char *,qa_error *),
    bool *matched,qa_error *error) {
    if(!out || !first || !second || !out->source || !first->source || !second->source || !matched) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Fuzzy interbreed requires actual source configurations");return false;
    }
    *matched=false;source_breed_report state={context,report,true};bot_fuzzy_reporter reporter={&state,source_report};
    qa_bot_weights_retain(out);qa_bot_weights_retain((qa_bot_weights *)first);qa_bot_weights_retain((qa_bot_weights *)second);
    bool ok=open(out,error) && bot_fuzzy_owned_open(first->source,error) && bot_fuzzy_owned_open(second->source,error) &&
        bot_fuzzy_interbreed(&first->source->source,&second->source->source,&out->source->source,&reporter,error);
    if(ok) *matched=state.matched;
    qa_bot_weights_release(out);qa_bot_weights_release((qa_bot_weights *)first);qa_bot_weights_release((qa_bot_weights *)second);
    return ok;
}
bool qa_bot_weights_interbreed(qa_bot_weights *out,const qa_bot_weights *first,
    const qa_bot_weights *second,bool *matched,qa_error *error) {
    return qa_bot_weights_interbreed_report(out,first,second,NULL,NULL,matched,error);
}
