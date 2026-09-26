#include "qa/network_q1_session.h"
#include "qa/network_q1_qw.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool command(qa_net_writer *w, const char *text)
{
    return qa_net_write_u8(w,4) && qa_net_write_string(w,text);
}
static bool command_text(qa_net_writer *w, const char *prefix, const char *value, const char *suffix)
{
    return qa_net_write_u8(w,4) && qa_net_write_data(w,prefix,strlen(prefix)) &&
        qa_net_write_data(w,value,strlen(value)) && qa_net_write_string(w,suffix);
}
bool qa_nq_signon_receive(qa_nq_signon *s, uint8_t stage, qa_net_writer *w)
{
    if (!s || stage<=s->stage || stage>4) return qa_net_writer_fail(w,"Invalid NetQuake signon stage");
    if (!s->name || !s->spawn_parameters || strpbrk(s->name,"\"\r\n") || strpbrk(s->spawn_parameters,"\r\n;"))
        return qa_net_writer_fail(w,"Invalid NetQuake seat identity");
    char text[80];
    switch (stage) {
    case 1: command(w,"prespawn"); break;
    case 2:
        command_text(w,"name \"",s->name,"\"\n");
        snprintf(text,sizeof(text),"color %u %u\n",(unsigned)(s->color>>4),(unsigned)(s->color&15));
        command(w,text);
        if (s->has_extension_flags) {
            snprintf(text,sizeof(text),"ex_flags %" PRIu32 "\n",s->extension_flags); command(w,text);
        }
        command_text(w,"spawn ",s->spawn_parameters,"");
        break;
    case 3: command(w,"begin"); break;
    case 4: break;
    default: return qa_net_writer_fail(w,"Invalid NetQuake signon stage");
    }
    if (w->failed) return false;
    s->stage=stage; return true;
}
void qa_nq_signon_first_entity(qa_nq_signon *s) { if (s && s->stage==3) s->stage=4; }

struct qa_qw_signon {
    qa_qw_signon_host host;
    qa_qw_download download;
    uint64_t offset;
    bool downloading, spawned, donor_wide;
};
void qa_qw_signon_close_download(qa_qw_signon *s)
{
    if (!s || !s->downloading) return;
    qa_qw_download download=s->download;
    s->download=(qa_qw_download){0}; s->downloading=false; s->offset=0;
    download.close(download.state);
}
bool qa_qw_signon_create(const qa_qw_signon_host *host, bool wide, qa_qw_signon **out, qa_error *e)
{
    if (!host || !out || !host->server_data || !host->names || !host->buffers ||
        !host->accepts_checksum || !host->spawn || !host->begin || !host->disconnect || !host->open_download) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Incomplete QW signon host"); return false;
    }
    qa_qw_signon *s=calloc(1,sizeof(*s));
    if (!s) { qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating QW signon"); return false; }
    s->host=*host; s->donor_wide=wide; *out=s; return true;
}
void qa_qw_signon_destroy(qa_qw_signon *s) { if (s) { qa_qw_signon_close_download(s); free(s); } }
bool qa_qw_signon_spawned(const qa_qw_signon *s) { return s && s->spawned; }
static bool emit_writer(qa_net_writer *w, qa_q1_emit_fn emit, void *user, qa_error *e)
{
    if (w->failed) return false;
    return emit(user,(qa_bytes){w->data,qa_net_writer_size(w)},e);
}
static bool serverdata_write(qa_net_writer *w, const qa_qw_serverdata *d)
{
    qa_qw_service message={.kind=QA_QW_SERVER_DATA};
    message.data.server=*d;
    return qa_qw_service_write(w,d->protocol,&message,NULL);
}

static bool fresh(qa_qw_signon *s, qa_q1_emit_fn emit, void *user, qa_error *e)
{
    qa_qw_serverdata data;
    if (!s->host.server_data(s->host.user,&data,e)) return false;
    if (data.protocol.kind==QA_NET_QW29 && !s->donor_wide) {
        s->host.disconnect(s->host.user,"Client does not support donor QuakeWorld protocol 29"); return true;
    }
    uint8_t bytes[1450]; qa_net_writer w; qa_net_writer_init(&w,bytes,sizeof(bytes),e);
    if (!serverdata_write(&w,&data) || !emit_writer(&w,emit,user,e)) return false;
    s->spawned=false; return true;
}
static bool path_valid(const char *s)
{
    if (!*s || strchr(s,'\\') || strchr(s,':')) return false;
    for (;;) {
        const char *end=strchr(s,'/'); size_t n=end ? (size_t)(end-s) : strlen(s);
        if (!n || (n==1 && s[0]=='.') || (n==2 && s[0]=='.' && s[1]=='.')) return false;
        if (!end) return true;
        s=end+1;
    }
}
static bool next_download(qa_qw_signon *s, qa_q1_emit_fn emit, void *user, qa_error *e)
{
    uint8_t bytes[772]; qa_net_writer w; qa_net_writer_init(&w,bytes,sizeof(bytes),e);
    qa_net_write_u8(&w,41);
    if (!s->downloading) {
        qa_net_write_i16(&w,-1); qa_net_write_u8(&w,0);
        return emit_writer(&w,emit,user,e);
    }
    uint64_t remaining=s->download.size-s->offset;
    size_t count=remaining<768 ? (size_t)remaining : 768;
    uint8_t payload[768];
    if (count && !s->download.read(s->download.state,s->offset,payload,count,e)) {
        qa_qw_signon_close_download(s); return false;
    }
    uint64_t next=s->offset+count;
    uint8_t percent=s->download.size ? (uint8_t)((long double)next*100.0L/(long double)s->download.size) : 100;
    qa_net_write_u16(&w,(uint16_t)count); qa_net_write_u8(&w,percent);
    qa_net_write_data(&w,payload,count);
    if (!emit_writer(&w,emit,user,e)) return false;
    s->offset=next;
    if (next==s->download.size) qa_qw_signon_close_download(s);
    return true;
}
static bool number(const char *s, int64_t minimum, int64_t maximum, int64_t *out)
{
    if (!s || !*s) return false;
    char *end; errno=0;
    intmax_t n=strtoimax(s,&end,10);
    if (errno==ERANGE || *end || n<minimum || n>maximum) return false;
    *out=(int64_t)n; return true;
}
bool qa_qw_signon_command(qa_qw_signon *s, const char *text, qa_q1_emit_fn emit, void *user,
                          bool *handled, qa_error *e)
{
    if (!s || !text || !emit || !handled) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid QW signon command"); return false; }
    *handled=false;
    const char *cursor=text; char args[4][1024]; size_t argc=0;
    for (unsigned i=0;i<4;++i) {
        bool present;
        if (!qa_q1_token(&cursor,true,args[i],sizeof(args[i]),&present,e)) return false;
        if (!present) break;
        ++argc;
    }
    if (!argc) return true;
    const char *op=args[0];
    if (!strcmp(op,"new")) { *handled=true; return s->spawned ? true : fresh(s,emit,user,e); }
    if (!strcmp(op,"download")) {
        *handled=true; qa_qw_signon_close_download(s);
        if (argc>1 && path_valid(args[1])) {
            bool found=false; qa_qw_download download={0};
            if (!s->host.open_download(s->host.user,args[1],&found,&download,e)) return false;
            if (found) {
                if (!download.read || !download.close) {
                    if (download.close) download.close(download.state);
                    qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid download source"); return false;
                }
                s->download=download; s->downloading=true;
            }
        }
        return next_download(s,emit,user,e);
    }
    if (!strcmp(op,"nextdl")) { *handled=true; return s->downloading ? next_download(s,emit,user,e) : true; }
    bool sounds=!strcmp(op,"soundlist"), models=!strcmp(op,"modellist"), prespawn=!strcmp(op,"prespawn"),
        spawn=!strcmp(op,"spawn"), begin=!strcmp(op,"begin");
    if (!sounds && !models && !prespawn && !spawn && !begin) return true;
    *handled=true;
    if (s->spawned) return true;
    qa_qw_serverdata data;
    if (!s->host.server_data(s->host.user,&data,e)) return false;
    if (!qa_q1_profile_valid(data.protocol,e) || !qa_q1_is_qw(data.protocol)) return false;
    int64_t server_count=0,start=0;
    if (argc<2 || !number(args[1],INT32_MIN,INT32_MAX,&server_count) || server_count!=data.server_count)
        return fresh(s,emit,user,e);
    bool valid_start=argc<3 || number(args[2],0,INT32_MAX,&start);
    uint8_t bytes[1450]; qa_net_writer w; qa_net_writer_init(&w,bytes,sizeof(bytes),e);
    char command_line[100];
    if (sounds || models) {
        const char *const *names=NULL; size_t count=0;
        if (!s->host.names(s->host.user,models,&names,&count,e)) return false;
        uint32_t limit=data.protocol.kind==QA_NET_QW28?256:8192;
        if (count>=limit || (count && !names)) { qa_error_set(e,QA_ERROR_FORMAT,0,"Precache exceeds QW dialect"); return false; }
        if (!valid_start || (uint64_t)start>count || start>=limit) return fresh(s,emit,user,e);
        qa_net_write_u8(&w,sounds?46:45);
        if (data.protocol.kind==QA_NET_QW28) qa_net_write_u8(&w,(uint8_t)start);
        else qa_net_write_u16(&w,(uint16_t)start);
        size_t next=(size_t)start;
        while (next<count && qa_net_writer_size(&w)<725) {
            if (!names[next] || !*names[next]) return qa_net_writer_fail(&w,"Empty QW precache name");
            if (!qa_net_write_string(&w,names[next])) return false;
            ++next;
        }
        qa_net_write_u8(&w,0);
        uint16_t continuation=next<count?(uint16_t)next:0;
        if (data.protocol.kind==QA_NET_QW28) qa_net_write_u8(&w,(uint8_t)continuation);
        else qa_net_write_u16(&w,continuation);
        return emit_writer(&w,emit,user,e);
    }
    if (prespawn) {
        const qa_bytes *buffers=NULL; size_t count=0;
        if (!s->host.buffers(s->host.user,&buffers,&count,e)) return false;
        if (count && !buffers) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing QW signon buffers"); return false; }
        size_t index=valid_start && (uint64_t)start<count ? (size_t)start : 0;
        int64_t checksum=0;
        if (index==0 && ((argc>=4 && !number(args[3],INT32_MIN,UINT32_MAX,&checksum)) ||
            !s->host.accepts_checksum(s->host.user,(uint32_t)checksum))) {
            s->host.disconnect(s->host.user,"Map model file does not match"); return true;
        }
        if (count) qa_net_write_data(&w,buffers[index].data,buffers[index].size);
        if (index+1>=count) snprintf(command_line,sizeof(command_line),"cmd spawn %" PRId32 " 0\n",data.server_count);
        else snprintf(command_line,sizeof(command_line),"cmd prespawn %" PRId32 " %zu\n",data.server_count,index+1);
        qa_net_write_u8(&w,9); qa_net_write_string(&w,command_line);
        return emit_writer(&w,emit,user,e);
    }
    if (spawn) {
        if (!valid_start || start>32) return fresh(s,emit,user,e);
        if (!s->host.spawn(s->host.user,(uint8_t)start,emit,user,e)) return false;
        qa_net_write_u8(&w,9); qa_net_write_string(&w,"skins\n");
        return emit_writer(&w,emit,user,e);
    }
    if (!s->host.begin(s->host.user,e)) return false;
    s->spawned=true; return true;
}

struct qa_qw_precache { qa_net_protocol_id protocol; int32_t server_count; char **models, **sounds; size_t model_count,sound_count; };
static void free_names(char **names, size_t count)
{
    for (size_t i=0;i<count;++i) free(names[i]);
    free(names);
}
bool qa_qw_precache_create(qa_net_protocol_id p, qa_qw_precache **out, qa_error *e)
{
    if (!out || !qa_q1_profile_valid(p,e) || !qa_q1_is_qw(p)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid QW precache dialect"); return false;
    }
    qa_qw_precache *s=calloc(1,sizeof(*s));
    if (!s) { qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating QW precache"); return false; }
    s->protocol=p; *out=s; return true;
}
void qa_qw_precache_destroy(qa_qw_precache *s)
{
    if (s) { free_names(s->models,s->model_count); free_names(s->sounds,s->sound_count); free(s); }
}
static bool precache_command(const qa_qw_precache *s, qa_net_writer *w, const char *op, bool number_argument, uint32_t n)
{
    char text[100];
    if (number_argument) snprintf(text,sizeof(text),"%s %" PRId32 " %" PRIu32,op,s->server_count,n);
    else snprintf(text,sizeof(text),"%s %" PRId32,op,s->server_count);
    return command(w,text);
}
bool qa_qw_precache_reset(qa_qw_precache *s, qa_net_protocol_id p, int32_t count, qa_net_writer *w)
{
    if (!s || !qa_q1_profile_valid(p,NULL) || !qa_q1_is_qw(p)) return qa_net_writer_fail(w,"Invalid QW precache reset");
    qa_qw_precache next={.protocol=p,.server_count=count};
    if (!precache_command(&next,w,"soundlist",true,0)) return false;
    free_names(s->models,s->model_count); free_names(s->sounds,s->sound_count);
    *s=next; return true;
}
bool qa_qw_precache_list(qa_qw_precache *s, bool models, uint32_t first,
                         const char *const *names, size_t count, uint32_t next, qa_net_writer *w)
{
    if (!s) return qa_net_writer_fail(w,"Missing QW precache state");
    char ***target=models?&s->models:&s->sounds; size_t *old_count=models?&s->model_count:&s->sound_count;
    size_t limit=s->protocol.kind==QA_NET_QW28?256:8192;
    if (first>*old_count || first>=limit || count>=limit-first || next>=limit || (count && !names) ||
        (next && next!=(size_t)first+count)) return qa_net_writer_fail(w,"Invalid QW precache continuation");
    size_t total=first+count;
    char **replacement=total ? calloc(total,sizeof(*replacement)) : NULL;
    if (total && !replacement) { qa_error_set(w->error,QA_ERROR_MEMORY,0,"Allocating QW precache list"); w->failed=true; return false; }
    for (size_t i=0;i<first;++i) replacement[i]=(*target)[i];
    for (size_t i=0;i<count;++i) {
        if (!names[i] || !*names[i]) {
            for (size_t j=first;j<first+i;++j) free(replacement[j]);
            free(replacement);
            return qa_net_writer_fail(w,"Empty QW precache name");
        }
        size_t length=strlen(names[i]); replacement[first+i]=malloc(length+1);
        if (!replacement[first+i]) {
            for (size_t j=first;j<first+i;++j) free(replacement[j]);
            free(replacement);
            qa_error_set(w->error,QA_ERROR_MEMORY,0,"Allocating QW precache name"); w->failed=true; return false;
        }
        memcpy(replacement[first+i],names[i],length+1);
    }
    if (next && !precache_command(s,w,models?"modellist":"soundlist",true,next)) {
        for (size_t j=first;j<total;++j) free(replacement[j]);
        free(replacement); return false;
    }
    for (size_t i=first;i<*old_count;++i) free((*target)[i]);
    free(*target); *target=replacement; *old_count=total; return true;
}
const char *qa_qw_precache_name(const qa_qw_precache *s, bool models, size_t i)
{
    if (!s) return NULL;
    return models ? (i<s->model_count?s->models[i]:NULL) : (i<s->sound_count?s->sounds[i]:NULL);
}
size_t qa_qw_precache_count(const qa_qw_precache *s, bool models) { return s ? (models?s->model_count:s->sound_count) : 0; }
bool qa_qw_precache_sounds_ready(const qa_qw_precache *s, qa_net_writer *w)
{
    return s ? precache_command(s,w,"modellist",true,0) : qa_net_writer_fail(w,"Missing QW precache state");
}
bool qa_qw_precache_models_ready(const qa_qw_precache *s, uint32_t checksum, qa_net_writer *w)
{
    if (!s) return qa_net_writer_fail(w,"Missing QW precache state");
    char text[100]; snprintf(text,sizeof(text),"prespawn %" PRId32 " 0 %" PRIu32,s->server_count,checksum);
    return command(w,text);
}
bool qa_qw_precache_skins_ready(const qa_qw_precache *s, qa_net_writer *w)
{
    return s ? precache_command(s,w,"begin",false,0) : qa_net_writer_fail(w,"Missing QW precache state");
}
bool qa_qw_choose_protocol(uint32_t requested, bool needs_wide, qa_net_protocol_id *out, qa_error *e)
{
    if (requested!=0 && requested!=28 && requested!=29) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Unknown QW selection"); return false; }
    bool wide=requested==29 || (!requested && needs_wide);
    return qa_q1_profile(wide?29:28,wide?130:0,out,e);
}
