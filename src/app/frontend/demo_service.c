#include "demo_service.h"
#include "qa/network_q2_mvd.h"
#include "qa/network_q3.h"
#include "qa/vfs.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_demo_reader {
    qa_buffer bytes;
    qa_net_reader cursor;
    qa_q2_mvd_framer *mvd;
    frontend_demo_format format;
    frontend_demo_end end;
    int32_t forced_track;
};
typedef struct demo_recording {
    frontend_demo_record_source source;
    frontend_demo_sink sink;
    qa_fs_opened_file *file;
    qa_fs_root *root;
    char *path;
    uint64_t position;
    size_t packets;
    float angles[3], seconds;
    unsigned calls;
    bool attached, faulted, footer_written, preparing, failure_reported;
    qa_buffer seed;
    qa_error failure;
} demo_recording;
enum { DEMO_PENDING_RECORD, DEMO_PENDING_PLAY, DEMO_PENDING_COUNT };
struct frontend_demo_service {
    frontend_demo_service_options options;
    frontend_demo_request pending[DEMO_PENDING_COUNT];
    char *pending_name[DEMO_PENDING_COUNT], *pending_script[DEMO_PENDING_COUNT];
    bool queued[DEMO_PENDING_COUNT], busy;
    demo_recording *recording, *server, *preparing;
    frontend_demo_reader *reader;
    frontend_demo_playback_source playback;
    qa_command_context playback_context;
    char *playback_path, *playback_script;
    bool timedemo;
};

static bool fail(qa_error *error, qa_status status, const char *message) {
    qa_error_set(error,status,0,"%s",message);
    return false;
}
static char *copy_text(const char *text, qa_error *error) {
    if (!text) return NULL;
    size_t length=strlen(text);
    char *copy=malloc(length+1);
    if (!copy) { fail(error,QA_ERROR_MEMORY,"Copying demo request"); return NULL; }
    memcpy(copy,text,length+1);
    return copy;
}
static bool suffix(const char *name,const char *extension) {
    size_t a=strlen(name),b=strlen(extension);
    if (a<b) return false;
    for (size_t i=0;i<b;++i) {
        unsigned char c=(unsigned char)name[a-b+i],d=(unsigned char)extension[i];
        if (c>='A'&&c<='Z') c=(unsigned char)(c+'a'-'A');
        if (d>='A'&&d<='Z') d=(unsigned char)(d+'a'-'A');
        if (c!=d) return false;
    }
    return true;
}
static bool demo_directory(const char *name,bool fold) {
    if(strlen(name)<6)return false;
    for(size_t i=0;i<6;++i) {
        unsigned char c=(unsigned char)name[i];
        if(fold&&c>='A'&&c<='Z')c=(unsigned char)(c+'a'-'A');
        if(c!=(unsigned char)"demos/"[i])return false;
    }
    return true;
}
static const char *q3_suffix(const char *name) {
    const char *dot=strrchr(name,'.');
    if(!dot||strlen(dot)<5)return NULL;
    unsigned char d=(unsigned char)dot[1],m=(unsigned char)dot[2];
    if(d>='A'&&d<='Z')d=(unsigned char)(d+'a'-'A');
    if(m>='A'&&m<='Z')m=(unsigned char)(m+'a'-'A');
    if(d!='d'||m!='m'||dot[3]!='_')return NULL;
    for(const char *p=dot+4;*p;++p)if(*p<'0'||*p>'9')return NULL;
    return dot;
}
static uint32_t q3_protocol(const char *dot) {
    if(!dot)return 0;
    uint32_t value=0;
    for(const char *p=dot+4;*p;++p) {
        value=value*10+(uint32_t)(*p-'0');
        if(value>68)return 0;
    }
    return value>=66?value:0;
}
static void print_path(frontend_demo_service *s,const char *prefix,const char *path) {
    if(!s->options.print)return;
    size_t a=strlen(prefix),b=strlen(path);
    if(a>SIZE_MAX-b-2)return;
    char *line=malloc(a+b+2);
    if(!line)return;
    memcpy(line,prefix,a);memcpy(line+a,path,b);line[a+b]='\n';line[a+b+1]=0;
    s->options.print(s->options.context,line);
    free(line);
}
static const char *extension(frontend_demo_format format) {
    switch(format) {
    case FRONTEND_DEMO_NQ:return ".dem";
    case FRONTEND_DEMO_QW:return ".qwd";
    case FRONTEND_DEMO_Q2:case FRONTEND_DEMO_Q2_SERVER:return ".dm2";
    case FRONTEND_DEMO_Q3:return ".dm_68";
    case FRONTEND_DEMO_MVD:return ".mvd";
    }
    return NULL;
}
static char *resource_name(const char *name,frontend_demo_format format,bool recording,
    qa_error *error) {
    char *normalized=qa_vfs_normalize_path(name,error);
    if (!normalized) return NULL;
    const char *ext=extension(format);
    const char *slash=strrchr(normalized,'/'),*dot=strrchr(normalized,'.');
    bool add=recording?!suffix(normalized,ext):format!=FRONTEND_DEMO_Q3&&(!dot||(slash&&dot<slash));
    bool directory=(format==FRONTEND_DEMO_Q2||format==FRONTEND_DEMO_Q2_SERVER||
        format==FRONTEND_DEMO_Q3||format==FRONTEND_DEMO_MVD)&&!demo_directory(normalized,recording);
    size_t n=strlen(normalized),e=add?strlen(ext):0,p=directory?6:0;
    if (n>SIZE_MAX-e-p-1) { free(normalized); fail(error,QA_ERROR_MEMORY,"Demo path overflow"); return NULL; }
    char *result=malloc(n+e+p+1);
    if (!result) { free(normalized); fail(error,QA_ERROR_MEMORY,"Allocating demo path"); return NULL; }
    if (p) memcpy(result,"demos/",p);
    memcpy(result+p,normalized,n);
    if (e) memcpy(result+p+n,ext,e);
    result[p+n+e]=0;
    free(normalized);
    return result;
}
static frontend_demo_format selected_format(const char *name,frontend_demo_format fallback) {
    if (suffix(name,".dem")) return FRONTEND_DEMO_NQ;
    if (suffix(name,".qwd")) return FRONTEND_DEMO_QW;
    if (suffix(name,".dm2")) return FRONTEND_DEMO_Q2;
    if (suffix(name,".mvd")) return FRONTEND_DEMO_MVD;
    if(q3_suffix(name))return FRONTEND_DEMO_Q3;
    return fallback;
}
static void reader_destroy(frontend_demo_reader *reader) {
    if (!reader) return;
    qa_q2_mvd_framer_destroy(reader->mvd);
    qa_buffer_free(&reader->bytes);
    free(reader);
}
static bool reader_create(qa_buffer *bytes,frontend_demo_format format,
    frontend_demo_reader **out,qa_error *error) {
    frontend_demo_reader *r=calloc(1,sizeof(*r));
    if (!r) return fail(error,QA_ERROR_MEMORY,"Allocating demo reader");
    r->bytes=*bytes;
    *bytes=(qa_buffer){0};
    if(r->bytes.size>SIZE_MAX/8) {
        reader_destroy(r);
        return fail(error,QA_ERROR_FORMAT,"Demo cursor extent overflow");
    }
    r->format=format;
    r->forced_track=-1;
    qa_net_reader_init(&r->cursor,(qa_bytes){r->bytes.data,r->bytes.size},error);
    bool okay=true;
    if (format==FRONTEND_DEMO_NQ) okay=qa_q1_demo_read_header(&r->cursor,&r->forced_track);
    else if (format==FRONTEND_DEMO_MVD) okay=qa_q2_mvd_framer_create(true,QA_Q2_MVD_MESSAGE_BYTES,&r->mvd,error);
    r->cursor.error=NULL;
    if (!okay) { reader_destroy(r); return false; }
    *out=r;
    return true;
}
int32_t frontend_demo_forced_track(const frontend_demo_reader *reader) {
    return reader?reader->forced_track:-1;
}
qa_bytes frontend_demo_reader_bytes(const frontend_demo_reader *reader) {
    return reader?(qa_bytes){reader->bytes.data,reader->bytes.size}:(qa_bytes){0};
}
bool frontend_demo_read_next(frontend_demo_reader *r,
    bool (*sequence)(void *,int32_t,qa_error *),void *context,
    frontend_demo_packet *packet,bool *present,frontend_demo_end *end,qa_error *error) {
    if (!r||!packet||!present||!end) return fail(error,QA_ERROR_ARGUMENT,"Missing demo reader output");
    *present=false;
    *end=r->end;
    if (r->end!=FRONTEND_DEMO_RUNNING) return true;
    r->cursor.error=error;
    size_t remaining=qa_net_reader_remaining(&r->cursor);
    frontend_demo_packet next={.format=r->format};
    bool okay=true;
    if (r->format==FRONTEND_DEMO_MVD) {
        size_t consumed=0;
        qa_bytes message={0};
        qa_bytes input={remaining?r->bytes.data+r->cursor.bit/8:NULL,remaining};
        okay=qa_q2_mvd_framer_push(r->mvd,input,&consumed,present,&message,error);
        r->cursor.bit+=consumed*8;
        next.value.message=message;
        if (okay&&!*present) {
            okay=qa_q2_mvd_framer_finish(r->mvd,true,error);
            if (okay) r->end=FRONTEND_DEMO_TERMINATOR;
        }
    } else if (!remaining) r->end=FRONTEND_DEMO_EOF;
    else if (r->format==FRONTEND_DEMO_NQ) {
        okay=qa_q1_demo_read_record(&r->cursor,64000,&next.value.nq);
        *present=okay;
    } else if (r->format==FRONTEND_DEMO_QW) {
        okay=qa_qw_demo_read_record(&r->cursor,1450,&next.value.qw);
        *present=okay;
    } else {
        if (r->format==FRONTEND_DEMO_Q3&&remaining<4) {
            r->cursor.bit=r->bytes.size*8;
            r->end=FRONTEND_DEMO_TRUNCATED;
        } else {
            int32_t number=0;
            if (r->format==FRONTEND_DEMO_Q3) {
                number=qa_net_read_i32(&r->cursor);
                if (sequence&&!sequence(context,number,error)) okay=false;
                if (okay&&remaining<8) {
                    r->cursor.bit=r->bytes.size*8;
                    r->end=FRONTEND_DEMO_TRUNCATED;
                }
            }
            if (okay&&r->end==FRONTEND_DEMO_RUNNING) {
                int32_t length=qa_net_read_i32(&r->cursor);
                if (r->cursor.failed) okay=false;
                else if (length==-1) r->end=FRONTEND_DEMO_TERMINATOR;
                else if (length<0||(r->format==FRONTEND_DEMO_Q3&&(uint32_t)length>QA_Q3_MESSAGE_BYTES))
                    okay=qa_net_reader_fail(&r->cursor,"Invalid native demo message length");
                else if (r->format==FRONTEND_DEMO_Q3&&(size_t)length>qa_net_reader_remaining(&r->cursor)) {
                    r->cursor.bit=r->bytes.size*8;
                    r->end=FRONTEND_DEMO_TRUNCATED;
                } else {
                    qa_bytes payload={0};
                    okay=qa_net_read_bytes(&r->cursor,(size_t)length,&payload);
                    if (r->format==FRONTEND_DEMO_Q3) {
                        next.value.q3.sequence=number;
                        next.value.q3.message=payload;
                    } else next.value.message=payload;
                    *present=okay;
                }
            }
        }
    }
    r->cursor.error=NULL;
    if (okay&&*present) *packet=next;
    *end=r->end;
    return okay;
}
bool frontend_demo_peek_message(const frontend_demo_reader *reader,qa_bytes *out,qa_error *error) {
    if(!reader||!out||(reader->format!=FRONTEND_DEMO_Q2&&reader->format!=FRONTEND_DEMO_Q2_SERVER))
        return fail(error,QA_ERROR_ARGUMENT,"Q2 protocol admission requires its native demo reader");
    frontend_demo_reader peek=*reader;
    frontend_demo_packet packet;
    bool present=false;
    frontend_demo_end end=FRONTEND_DEMO_RUNNING;
    if(!frontend_demo_read_next(&peek,NULL,NULL,&packet,&present,&end,error))return false;
    if(!present)return fail(error,QA_ERROR_FORMAT,"Q2 demo has no initial protocol message");
    *out=packet.value.message;
    return true;
}

static bool write_bytes(demo_recording *recording,qa_bytes bytes,qa_error *error) {
    if (recording->faulted) { if(error)*error=recording->failure; return false; }
    if (recording->position>UINT64_MAX-bytes.size) {
        recording->faulted=true;
        fail(error,QA_ERROR_IO,"Demo file position overflow");
        if(error)recording->failure=*error;
        return false;
    }
    size_t offset=0;
    while (offset<bytes.size) {
        size_t completed=0;
        if (recording->position>UINT64_MAX-offset||
            !qa_fs_opened_file_write(recording->file,recording->position+offset,
                (qa_bytes){bytes.data+offset,bytes.size-offset},&completed,error)) {
            recording->faulted=true;
            if(error)recording->failure=*error;
            return false;
        }
        if (!completed||completed>bytes.size-offset) {
            recording->faulted=true;
            fail(error,QA_ERROR_IO,"Demo write made incomplete progress");
            if(error)recording->failure=*error;
            return false;
        }
        offset+=completed;
    }
    recording->position+=bytes.size;
    return true;
}
static bool record_append(void *owner,const frontend_demo_packet *packet,qa_error *error) {
    demo_recording *r=owner;
    if (!r||!packet||r->calls||(!r->file&&!r->preparing)||r->footer_written)
        return fail(error,QA_ERROR_ARGUMENT,"Demo packet lost its actual recording source");
    if(r->faulted){if(error)*error=r->failure;return false;}
    if(packet->format!=r->source.format||!r->source.current(r->source.owner)) {
        r->faulted=true;
        fail(error,QA_ERROR_ARGUMENT,"Demo packet source protocol changed");
        if(error)r->failure=*error;
        return false;
    }
    size_t payload=packet->format==FRONTEND_DEMO_NQ?packet->value.nq.message.size:
        packet->format==FRONTEND_DEMO_QW&&packet->value.qw.kind==QA_QW_DEMO_PACKET?packet->value.qw.data.packet.size:
        packet->format==FRONTEND_DEMO_QW?0:
        packet->format==FRONTEND_DEMO_Q3?packet->value.q3.message.size:packet->value.message.size;
    if (payload>INT32_MAX||payload>SIZE_MAX-64||
        (packet->format==FRONTEND_DEMO_Q3&&payload>QA_Q3_MESSAGE_BYTES)) {
        r->faulted=true;
        fail(error,QA_ERROR_FORMAT,"Demo packet extent overflow");
        if(error)r->failure=*error;
        return false;
    }
    uint8_t local[128];
    size_t capacity=payload+64;
    uint8_t *storage=capacity>sizeof(local)?malloc(capacity):local;
    if (!storage) {
        r->faulted=true;
        fail(error,QA_ERROR_MEMORY,"Framing native demo packet");
        if(error)r->failure=*error;
        return false;
    }
    qa_net_writer writer;
    qa_net_writer_init(&writer,storage,capacity,error);
    bool okay=true;
    switch(packet->format) {
    case FRONTEND_DEMO_NQ:
        okay=qa_q1_demo_write_record(&writer,&packet->value.nq);
        break;
    case FRONTEND_DEMO_QW:
        okay=qa_qw_demo_write_record(&writer,&packet->value.qw);
        break;
    case FRONTEND_DEMO_Q3:
        okay=qa_net_write_i32(&writer,packet->value.q3.sequence)&&
            qa_net_write_i32(&writer,(int32_t)payload)&&
            qa_net_write_data(&writer,packet->value.q3.message.data,payload);
        break;
    case FRONTEND_DEMO_MVD:
        okay=qa_q2_mvd_write_framed(&writer,packet->value.message);
        break;
    case FRONTEND_DEMO_Q2:case FRONTEND_DEMO_Q2_SERVER:
        okay=qa_net_write_i32(&writer,(int32_t)payload)&&
            qa_net_write_data(&writer,packet->value.message.data,payload);
        break;
    }
    ++r->calls;
    if(okay&&r->preparing) {
        size_t size=qa_net_writer_size(&writer);
        if(size>SIZE_MAX-r->seed.size)okay=fail(error,QA_ERROR_MEMORY,"Demo initial state extent overflow");
        else {
            uint8_t *bytes=realloc(r->seed.data,r->seed.size+size);
            if(!bytes)okay=fail(error,QA_ERROR_MEMORY,"Retaining actual initial demo packets");
            else {
                memcpy(bytes+r->seed.size,storage,size);
                r->seed=(qa_buffer){bytes,r->seed.size+size};
            }
        }
    } else if (okay) okay=write_bytes(r,(qa_bytes){storage,qa_net_writer_size(&writer)},error);
    --r->calls;
    if (storage!=local) free(storage);
    if (!okay) {
        r->faulted=true;
        if(error)r->failure=*error;
        return false;
    }
    if(packet->format==FRONTEND_DEMO_NQ)memcpy(r->angles,packet->value.nq.angles,sizeof(r->angles));
    if(packet->format==FRONTEND_DEMO_QW)r->seconds=packet->value.qw.seconds;
    ++r->packets;
    return true;
}
static bool record_close(demo_recording **owner,bool footer,qa_error *error) {
    demo_recording *r=*owner;
    if (!r) return true;
    if (r->calls) return fail(error,QA_ERROR_ARGUMENT,"Demo recording callback is entered");
    if (r->attached) {
        if (!r->source.detach(r->source.owner,&r->sink,error)) return false;
        r->attached=false;
    }
    bool okay=true;
    if (r->file&&footer&&!r->faulted&&!r->footer_written) {
        uint8_t bytes[96];
        qa_net_writer writer;
        qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
        switch(r->source.format) {
        case FRONTEND_DEMO_NQ: {
            uint8_t disconnect=2;
            qa_q1_demo_record record={.message={&disconnect,1}};
            memcpy(record.angles,r->angles,sizeof(record.angles));
            okay=qa_q1_demo_write_record(&writer,&record);
            break;
        }
        case FRONTEND_DEMO_QW: {
            static const uint8_t disconnect[]={255,255,255,255,2,'E','n','d','O','f','D','e','m','o',0};
            qa_qw_demo_record record={.kind=QA_QW_DEMO_PACKET,.seconds=r->seconds,
                .data.packet={disconnect,sizeof(disconnect)}};
            okay=qa_qw_demo_write_record(&writer,&record);
            break;
        }
        case FRONTEND_DEMO_Q2:okay=qa_net_write_i32(&writer,-1);break;
        case FRONTEND_DEMO_Q3:okay=qa_net_write_i32(&writer,-1)&&qa_net_write_i32(&writer,-1);break;
        case FRONTEND_DEMO_MVD:okay=qa_q2_mvd_write_terminator(&writer);break;
        case FRONTEND_DEMO_Q2_SERVER:break;
        }
        if (okay) okay=write_bytes(r,(qa_bytes){bytes,qa_net_writer_size(&writer)},error);
        r->footer_written=true;
        if (okay) okay=qa_fs_opened_file_flush(r->file,error);
    }
    qa_error cleanup={0};
    if (r->file&&!qa_fs_opened_file_close(&r->file,okay?error:&cleanup)) return false;
    if (r->source.owner) {
        if(!r->source.release)return fail(error,QA_ERROR_ARGUMENT,"Partial recording source has no release owner");
        if(!r->source.release(&r->source.owner,okay?error:&cleanup))return false;
        if(r->source.owner)return fail(error,QA_ERROR_ARGUMENT,"Recording source retained its released owner");
    }
    qa_fs_root_close(r->root);
    qa_buffer_free(&r->seed);
    free(r->path);
    free(r);
    *owner=NULL;
    return okay;
}
static bool playback_close(frontend_demo_service *s,qa_error *error) {
    if(s->playback.owner) {
        if(!s->playback.release)return fail(error,QA_ERROR_ARGUMENT,"Partial playback source has no release owner");
        if(!s->playback.release(&s->playback.owner,error))return false;
        if(s->playback.owner)return fail(error,QA_ERROR_ARGUMENT,"Playback source retained its released owner");
    }
    s->playback=(frontend_demo_playback_source){0};
    reader_destroy(s->reader);
    s->reader=NULL;
    free(s->playback_path);s->playback_path=NULL;
    free(s->playback_script);s->playback_script=NULL;
    return true;
}
static bool record_failed(frontend_demo_service *,demo_recording **,qa_error *);
static bool record_finish(frontend_demo_service *s,demo_recording **owner,
    bool server,qa_error *error) {
    demo_recording *r=*owner;
    if(!r)return true;
    if(r->faulted)return record_failed(s,owner,error);
    char *path=copy_text(r->path,error);
    if(r->path&&!path)return false;
    bool okay=record_close(owner,true,error);
    if(okay&&path)print_path(s,server?"Completed server footage ":"Completed demo ",path);
    free(path);
    return okay;
}
static bool record_failed(frontend_demo_service *s,demo_recording **owner,qa_error *error) {
    demo_recording *r=*owner;
    if(!r||!r->faulted)return true;
    if(!r->failure_reported&&s->options.print) {
        char line[sizeof(r->failure.message)+32];
        snprintf(line,sizeof(line),"Demo recording failed: %s\n",r->failure.message);
        r->failure_reported=true;
        s->options.print(s->options.context,line);
    }
    return record_close(owner,false,error);
}
static bool create_parents(qa_fs_root *root,char *path,qa_error *error) {
    for(char *p=path;*p;++p)if(*p=='/') {
        *p=0;
        bool okay=qa_fs_root_create_directory(root,path,error);
        *p='/';
        if(!okay)return false;
    }
    return true;
}
static bool record_begin(frontend_demo_service *s,const frontend_demo_request *request,qa_error *error) {
    bool server=request->action==FRONTEND_DEMO_SERVER_RECORD;
    demo_recording **destination=server?&s->server:&s->recording;
    if(server&&*destination)return fail(error,QA_ERROR_ARGUMENT,"Already doing a serverrecord");
    if(!record_close(&s->preparing,false,error))return false;
    demo_recording *r=calloc(1,sizeof(*r));
    if(!r)return fail(error,QA_ERROR_MEMORY,"Allocating native demo recording");
    s->preparing=r;
    r->preparing=true;
    r->sink=(frontend_demo_sink){r,record_append};
    bool okay=s->options.record_source(s->options.context,&request->source,request->action,&r->source,error);
    if(okay&&(!r->source.owner||!r->source.root||!r->source.current||!r->source.seed||
        !r->source.attach||!r->source.detach||!r->source.release||!extension(r->source.format)))
        okay=fail(error,QA_ERROR_ARGUMENT,"Demo recording has no actual retained source feed");
    if(okay)okay=qa_net_protocol_valid(r->source.protocol,error);
    if(okay&&((request->action==FRONTEND_DEMO_RERECORD&&r->source.format!=FRONTEND_DEMO_QW)||
        (server&&r->source.format!=FRONTEND_DEMO_Q2_SERVER)||
        (request->action==FRONTEND_DEMO_MVD_RECORD&&r->source.format!=FRONTEND_DEMO_MVD)))
        okay=fail(error,QA_ERROR_ARGUMENT,"Requested demo recording source format differs");
    if(okay&&(!request->name&&r->source.format!=FRONTEND_DEMO_Q3))
        okay=fail(error,QA_ERROR_ARGUMENT,"Usage: record <name>");
    if(okay) {
        okay=r->source.seed(r->source.owner,&r->sink,error);
        if(okay&&!r->packets)okay=fail(error,QA_ERROR_ARGUMENT,"Recording requires matching initial source state");
        if(okay&&request->action==FRONTEND_DEMO_RERECORD) {
            qa_buffer_free(&r->seed);
            r->packets=0;
            r->seconds=0;
        }
    }
    if(okay&&!server) {
        okay=record_finish(s,destination,false,error);
    }
    if(okay){*destination=r;s->preparing=NULL;}
    if(okay) {
        r->root=r->source.root;qa_fs_root_retain(r->root);
        for(unsigned index=0;index<10000;++index) {
            char automatic[16];
            if(!request->name)snprintf(automatic,sizeof(automatic),"demo%04u",index);
            r->path=resource_name(request->name?request->name:automatic,r->source.format,true,error);
            if(!r->path||!create_parents(r->root,r->path,error)){okay=false;break;}
            bool opened=false;
            okay=qa_fs_root_opened_file(r->root,r->path,QA_FS_OPENED_WRITE,QA_FS_CREATE_NEW,&r->file,&opened,error);
            if(okay||request->name||r->file)break;
            qa_fs_native_error native={0};
            bool collision=qa_fs_opened_native_error_read(&native)&&native.available&&
                (((native.platform==1||native.platform==3)&&native.code==(uint32_t)EEXIST)||
                 (native.platform==2&&(native.code==80||native.code==183)));
            if(!collision)break;
            if(index==9999) {
                okay=fail(error,QA_ERROR_IO,"No unused demo0000-demo9999 recording name remains");
                break;
            }
            free(r->path);r->path=NULL;
        }
    }
    if(okay) {
        uint8_t header[16];qa_net_writer w;qa_net_writer_init(&w,header,sizeof(header),error);
        if(r->source.format==FRONTEND_DEMO_NQ)okay=qa_q1_demo_write_header(&w,r->source.forced_track);
        if(r->source.format==FRONTEND_DEMO_MVD)okay=qa_q2_mvd_write_magic(&w);
        if(okay)okay=write_bytes(r,(qa_bytes){header,qa_net_writer_size(&w)},error);
        if(okay)okay=write_bytes(r,(qa_bytes){r->seed.data,r->seed.size},error);
        qa_buffer_free(&r->seed);
        r->preparing=false;
    }
    if(okay&&request->action==FRONTEND_DEMO_RERECORD) {
        if(!r->source.reconnect)okay=fail(error,QA_ERROR_ARGUMENT,"QW source has no actual reconnect recording feed");
        else {
            frontend_demo_packet initial={.format=FRONTEND_DEMO_QW,
                .value.qw={.kind=QA_QW_DEMO_SEQUENCES}};
            okay=record_append(r,&initial,error)&&r->source.reconnect(r->source.owner,
                &request->source,&r->sink,&r->attached,error);
            if(okay&&!r->attached)okay=fail(error,QA_ERROR_ARGUMENT,"QW reconnect did not retain its recording sink");
        }
    } else if(okay) {
        okay=r->source.attach(r->source.owner,&r->sink,&r->attached,error);
        if(okay&&!r->attached)okay=fail(error,QA_ERROR_ARGUMENT,"Recording source did not retain its sink");
    }
    if(!okay) {
        qa_error cleanup={0};
        r->faulted=true;
        if(error)r->failure=*error;
        (void)record_close(s->preparing?&s->preparing:destination,false,&cleanup);
        return false;
    }
    print_path(s,server?"Recording server footage ":"Recording ",r->path);
    return true;
}

static bool play_begin(frontend_demo_service *s,const frontend_demo_request *request,qa_error *error) {
    frontend_demo_format format=selected_format(request->name,request->fallback);
    char *path=resource_name(request->name,format,false,error);
    if(!path)return false;
    qa_buffer bytes={0};bool found=false,okay=true;
    uint32_t protocol=0;
    if(format==FRONTEND_DEMO_Q3) {
        const char *dot=q3_suffix(path);
        uint32_t requested_protocol=q3_protocol(dot);
        bool explicit_protocol=requested_protocol!=0;
        char *stem=copy_text(path,error);
        if(!stem){free(path);return false;}
        const char *ending=q3_suffix(stem);
        if(ending)stem[ending-stem]=0;
        if(dot&&!explicit_protocol) {
            size_t size=strlen(dot+4)+35;
            char *line=malloc(size);
            if(!line){free(stem);free(path);return fail(error,QA_ERROR_MEMORY,"Reporting demo protocol selection");}
            snprintf(line,size,"Protocol %s not supported for demos\n",dot+4);
            if(s->options.print)s->options.print(s->options.context,line);
            free(line);
        }
        for(uint32_t version=66;version<=68;++version) {
            if(explicit_protocol&&version!=requested_protocol)continue;
            if(!explicit_protocol) {
                free(path);size_t size=strlen(stem)+7;path=malloc(size);
                if(!path){okay=fail(error,QA_ERROR_MEMORY,"Allocating Q3 demo candidate");break;}
                snprintf(path,size,"%s.dm_%u",stem,version);
            }
            okay=s->options.read(s->options.context,&request->source,path,&bytes,&found,error);
            if(!okay||found){protocol=version;break;}
            if(!explicit_protocol)print_path(s,"Not found: ",path);
            qa_buffer_free(&bytes);
        }
        free(stem);
    } else okay=s->options.read(s->options.context,&request->source,path,&bytes,&found,error);
    if(okay&&!found)okay=fail(error,QA_ERROR_NOT_FOUND,"Could not open demo recording");
    if(okay&&format==FRONTEND_DEMO_Q2&&bytes.size>=4&&qa_load_u32le(bytes.data)==QA_Q2_MVD_MAGIC)format=FRONTEND_DEMO_MVD;
    if(okay)okay=record_close(&s->preparing,false,error);
    if(okay)okay=record_finish(s,&s->recording,false,error);
    if(okay)okay=record_finish(s,&s->server,true,error);
    if(okay)okay=playback_close(s,error);
    if(okay) {
        s->playback_script=copy_text(request->source.script,error);
        if(request->source.script&&!s->playback_script)okay=false;
        s->playback_context=request->source;s->playback_context.script=s->playback_script;
        s->playback_path=path;path=NULL;s->timedemo=request->timedemo;
    }
    if(okay)okay=reader_create(&bytes,format,&s->reader,error);
    if(okay)okay=s->options.playback_source(s->options.context,&request->source,format,protocol,
        s->reader,&s->playback,error);
    if(okay&&(!s->playback.owner||!s->playback.current||!s->playback.advance||!s->playback.release))
        okay=fail(error,QA_ERROR_ARGUMENT,"Demo playback has no actual retained CLIENT receiver");
    qa_buffer_free(&bytes);free(path);
    if(!okay) {
        qa_error cleanup={0};
        if(!s->playback.owner||s->playback.release)(void)playback_close(s,&cleanup);
    }
    return okay;
}
bool frontend_demo_service_create(const frontend_demo_service_options *options,
    frontend_demo_service **out,qa_error *error) {
    if(!options||!out||*out||!options->current||!options->read||!options->record_source||!options->playback_source)
        return fail(error,QA_ERROR_ARGUMENT,"Missing actual frontend demo services");
    frontend_demo_service *s=calloc(1,sizeof(*s));
    if(!s)return fail(error,QA_ERROR_MEMORY,"Allocating frontend demo service");
    s->options=*options;*out=s;return true;
}
bool frontend_demo_stage(frontend_demo_service *s,const frontend_demo_request *request,qa_error *error) {
    if(!s||!request||(unsigned)request->action>FRONTEND_DEMO_MVD_STOP||
        (unsigned)request->fallback>FRONTEND_DEMO_Q2_SERVER||
        request->source.origin==QA_COMMAND_REMOTE||
        ((request->action==FRONTEND_DEMO_PLAY||request->action==FRONTEND_DEMO_RERECORD||
          request->action==FRONTEND_DEMO_SERVER_RECORD||request->action==FRONTEND_DEMO_MVD_RECORD)&&!request->name))
        return fail(error,QA_ERROR_ARGUMENT,"Invalid local demo request");
    char *name=copy_text(request->name,error),*script=copy_text(request->source.script,error);
    if((request->name&&!name)||(request->source.script&&!script)){free(name);free(script);return false;}
    size_t kind=request->action==FRONTEND_DEMO_PLAY||request->action==FRONTEND_DEMO_STOP_PLAY?
        DEMO_PENDING_PLAY:DEMO_PENDING_RECORD;
    free(s->pending_name[kind]);free(s->pending_script[kind]);
    s->pending_name[kind]=name;s->pending_script[kind]=script;s->pending[kind]=*request;
    s->pending[kind].name=name;s->pending[kind].source.script=script;s->queued[kind]=true;return true;
}
static bool execute_request(frontend_demo_service *s,const frontend_demo_request *request,qa_error *error) {
    bool okay=s->options.current(s->options.context,&request->source,error);
    if(okay)switch(request->action) {
    case FRONTEND_DEMO_PLAY:okay=play_begin(s,request,error);break;
    case FRONTEND_DEMO_STOP_PLAY:okay=playback_close(s,error);break;
    case FRONTEND_DEMO_RECORD:case FRONTEND_DEMO_RERECORD:
    case FRONTEND_DEMO_SERVER_RECORD:case FRONTEND_DEMO_MVD_RECORD:okay=record_begin(s,request,error);break;
    case FRONTEND_DEMO_STOP_RECORD:okay=record_finish(s,&s->recording,false,error);break;
    case FRONTEND_DEMO_SERVER_STOP:
        if(!s->server&&s->options.print)s->options.print(s->options.context,"Not doing a serverrecord.\n");
        okay=record_finish(s,&s->server,true,error);break;
    case FRONTEND_DEMO_MVD_STOP:
        if(s->recording&&s->recording->source.format==FRONTEND_DEMO_MVD)okay=record_finish(s,&s->recording,false,error);
        else if(s->options.print)s->options.print(s->options.context,"Not recording a multiview demo.\n");
        break;
    }
    return okay;
}
bool frontend_demo_execute(frontend_demo_service *s,qa_error *error) {
    if(!s||s->busy)return fail(error,QA_ERROR_ARGUMENT,"Frontend demo operation is entered");
    if(!frontend_demo_service_pending(s))return true;
    s->busy=true;
    bool okay=true;
    for(size_t kind=0;kind<DEMO_PENDING_COUNT;++kind) {
        if(!s->queued[kind])continue;
        frontend_demo_request request=s->pending[kind];
        char *name=s->pending_name[kind],*script=s->pending_script[kind];
        s->pending_name[kind]=s->pending_script[kind]=NULL;s->queued[kind]=false;
        qa_error failure={0};
        if(!execute_request(s,&request,&failure)) {
            if(okay&&error)*error=failure;
            okay=false;
        }
        free(name);free(script);
    }
    s->busy=false;return okay;
}
static bool sources_returned(frontend_demo_service *s,qa_error *error) {
    bool okay=true;
    okay=record_failed(s,&s->recording,error);
    if(okay)okay=record_failed(s,&s->server,error);
    if(okay)okay=record_close(&s->preparing,false,error);
    if(okay&&s->recording&&!s->recording->source.current(s->recording->source.owner))
        okay=record_finish(s,&s->recording,false,error);
    if(okay&&s->server&&!s->server->source.current(s->server->source.owner))
        okay=record_finish(s,&s->server,true,error);
    return okay;
}
bool frontend_demo_sources_returned(frontend_demo_service *s,qa_error *error) {
    if(!s)return true;
    if(!frontend_demo_service_idle(s))return fail(error,QA_ERROR_ARGUMENT,"Demo source callback is entered");
    s->busy=true;
    bool okay=sources_returned(s,error);
    s->busy=false;return okay;
}
bool frontend_demo_advance(frontend_demo_service *s,uint64_t elapsed,uint64_t frame,qa_error *error) {
    if(!s||s->busy)return fail(error,QA_ERROR_ARGUMENT,"Demo frame is entered");
    s->busy=true;
    bool okay=sources_returned(s,error);
    if(okay&&s->playback.owner) {
        if(!s->playback.current(s->playback.owner))okay=fail(error,QA_ERROR_ARGUMENT,"Demo lost its actual CLIENT receiver");
        frontend_demo_end end=FRONTEND_DEMO_RUNNING;
        if(okay)okay=s->playback.advance(s->playback.owner,s->reader,elapsed,frame,s->timedemo,&end,error);
        if(okay&&end!=FRONTEND_DEMO_RUNNING) {
            qa_command_context source=s->playback_context;
            char *script=copy_text(source.script,error);
            if(source.script&&!script)okay=false;
            source.script=script;
            frontend_demo_format format=s->reader->format;
            if(okay)okay=playback_close(s,error);
            if(okay&&s->options.completed)okay=s->options.completed(s->options.context,&source,format,end,error);
            free(script);
        }
    }
    s->busy=false;return okay;
}
bool frontend_demo_service_idle(const frontend_demo_service *s) {
    return !s||(!s->busy&&(!s->recording||!s->recording->calls)&&
        (!s->server||!s->server->calls)&&(!s->preparing||!s->preparing->calls));
}
bool frontend_demo_service_pending(const frontend_demo_service *s){
    return s&&(s->queued[DEMO_PENDING_RECORD]||s->queued[DEMO_PENDING_PLAY]);
}
bool frontend_demo_service_active(const frontend_demo_service *s){
    return s&&(s->recording||s->server||s->preparing||s->reader||s->playback.owner);
}
const char *frontend_demo_recording_path(const frontend_demo_service *s){return s&&s->recording?s->recording->path:NULL;}
const char *frontend_demo_server_recording_path(const frontend_demo_service *s){return s&&s->server?s->server->path:NULL;}
const char *frontend_demo_playback_path(const frontend_demo_service *s){return s?s->playback_path:NULL;}
bool frontend_demo_service_stop(frontend_demo_service *s,qa_error *error) {
    if(!s)return true;
    if(!frontend_demo_service_idle(s))return fail(error,QA_ERROR_ARGUMENT,"Demo service is entered during retirement");
    s->busy=true;
    bool okay=record_close(&s->preparing,false,error)&&record_close(&s->recording,true,error)&&
        record_close(&s->server,true,error)&&playback_close(s,error);
    s->busy=false;return okay;
}
bool frontend_demo_service_destroy(frontend_demo_service **owner,qa_error *error) {
    if(!owner||!*owner)return true;
    frontend_demo_service *s=*owner;
    if(!frontend_demo_service_stop(s,error))return false;
    for(size_t kind=0;kind<DEMO_PENDING_COUNT;++kind) {
        free(s->pending_name[kind]);free(s->pending_script[kind]);
    }
    free(s);*owner=NULL;return true;
}
