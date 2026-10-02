#include "internal.h"
#include <stdio.h>

typedef struct chat_integrity_string {
    qa_bot_memory_allocation allocation;
} chat_integrity_string;
static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void store(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(8*index));
}
static bool integrity_cell(qa_bot_memory *memory,const chat_integrity_string *strings,size_t count,
    uint32_t pointer,qa_bot_memory_span *out,qa_error *error) {
    if(!pointer || pointer>count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat integrity pointer has no true temporary allocation");return false;}
    if(!qa_bot_memory_bytes(memory,strings[pointer-1].allocation,out,error)) return false;
    if(out->size<9 || !memchr(out->data+8,0,out->size-8)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat integrity allocation has an invalid source string");return false;
    }
    return true;
}
static bool integrity_seen(qa_bot_memory *memory,const chat_integrity_string *strings,size_t count,
    uint32_t head,const char *name,bool *out,qa_error *error) {
    *out=false;
    for(size_t visited=0;head;++visited) {
        qa_bot_memory_span bytes;
        if(visited>=count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat integrity temporary list cycles");return false;}
        if(!integrity_cell(memory,strings,count,head,&bytes,error)) return false;
        uint32_t source=word(bytes.data);
        qa_bot_memory_span string;
        if(!integrity_cell(memory,strings,count,source,&string,error)) return false;
        if(!strcmp((const char *)string.data+8,name)) {*out=true;return true;}
        head=word(bytes.data+4);
    }
    return true;
}

bool qa_bot_chat_check_integrity(qa_bot_chat_system *system, qa_bot_chat_asset *asset,
                                 qa_error *e) {
    if (!system || system->retired || !asset ||
        (asset->view.kind != QA_BOT_CHAT_INITIAL && asset->view.kind != QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat integrity input");
        return false;
    }
    if(asset->initial_source && !chat_initial_asset_refresh(asset,e)) return false;
    qa_bot_memory *memory=asset->initial_source?asset->initial_source->memory:
        asset->packed_source?asset->packed_source->memory:system->library?system->library->memory:NULL;
    bool own_memory=memory==NULL;
    if(own_memory && !qa_bot_memory_create(NULL,&memory,e)) return false;
    chat_integrity_string *missing=NULL;uint32_t head=0;
    size_t count = 0, capacity = 0;
    uint64_t revision = system->revision;
    ++system->references;
    qa_bot_chat_asset_retain(asset);
    bool ok = true;
    bool graph=asset->packed_source!=NULL;bot_chat_graph_messages cursor={0};
    for (size_t message = 0; ok && !system->retired && system->revision == revision &&
                             (graph || message < asset->view.message_count);
         ++message) {
        const char *text;
        if(graph && !bot_chat_graph_message_next(asset,&cursor,e)) {ok=false;break;}
        if(graph && !cursor.message) break;
        if(!(graph?bot_chat_graph_message_text(asset,cursor.message,&text,e):
            chat_asset_message_text(asset,(uint32_t)message,&text,e))) {ok=false;break;}
        for (size_t i = 0; ok && !system->retired && system->revision == revision;) {
            /* Each iteration follows a possible RNG/Print/FILE callback. The
             * source getter must qualify the allocation again at that stage. */
            if(!(graph?bot_chat_graph_message_text(asset,cursor.message,&text,e):
                chat_asset_message_text(asset,(uint32_t)message,&text,e))) {ok=false;break;}
            size_t extent=strlen(text);
            if(i>=extent) break;
            if (text[i++] != 1)
                continue;
            char kind = text[i];
            if (kind != 'r' && kind != 'v') {
                if(extent>SIZE_MAX-96) {qa_error_set(e,QA_ERROR_MEMORY,0,"Chat integrity diagnostic exceeds native extent");ok=false;break;}
                char *diagnostic=malloc(extent+96);
                if(!diagnostic) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining actual chat integrity diagnostic");ok=false;break;}
                (void)snprintf(diagnostic,extent+96,"BotCheckChatMessageIntegrety: message \"%s\" invalid escape char",text);
                ok=chat_print(system,QA_SCRIPT_FATAL,diagnostic,e);free(diagnostic);
                continue;
            }
            size_t start = ++i;
            while (text[i] && text[i] != 1)
                ++i;
            size_t size = i - start;
            char *key=malloc(size+1);
            if(!key) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining source integrity argument");ok=false;break;}
            memcpy(key, text + start, size);
            key[size] = 0;
            if (text[i] == 1)
                ++i;
            if(kind!='r') {free(key);continue;}
            const char *random;
            if(!chat_random_string(system,key,&random,e)) {free(key);ok=false;break;}
            if(system->retired || system->revision!=revision) {free(key);break;}
            if(random) {free(key);continue;}
            bool seen = false;
            ok=integrity_seen(memory,missing,count,head,key,&seen,e);
            if(!ok || seen) {free(key);continue;}
            if (system->log) {
                int length=snprintf(NULL,0,"%s = {\"%s\"} //MISSING RANDOM\r\n",key,key);
                char *line=length>=0?malloc((size_t)length+1):NULL;
                if(!line) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining source integrity log line");ok=false;}
                else {
                    (void)snprintf(line,(size_t)length+1,"%s = {\"%s\"} //MISSING RANDOM\r\n",key,key);
                    ok=qa_bot_log_write(system->log,line,e);free(line);
                }
                if(!ok || system->retired || system->revision!=revision) {free(key);break;}
            }
            if (!bot_grow((void **)&missing, &capacity, count + 1, sizeof(*missing), e)) {
                ok = false;
                free(key);break;
            }
            if(size>UINT32_MAX-9 || count>=UINT32_MAX) {
                qa_error_set(e,QA_ERROR_MEMORY,0,"Source integrity allocation exceeds source word extent");
                ok=false;free(key);break;
            }
            qa_bot_memory_allocation allocation;
            ok=qa_bot_memory_allocate(memory,(uint32_t)size+9,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,e);
            if(ok) {
                missing[count++]=(chat_integrity_string){allocation};qa_bot_memory_span bytes;
                ok=qa_bot_memory_bytes(memory,allocation,&bytes,e);
                if(ok) {
                    store(bytes.data,(uint32_t)count);memcpy(bytes.data+8,key,size+1);
                    store(bytes.data+4,head);head=(uint32_t)count;
                    static const char prefix[]="missing random string ";
                    char *diagnostic=malloc(sizeof(prefix)+size);
                    if(!diagnostic) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining missing random source report");ok=false;}
                    else {
                        memcpy(diagnostic,prefix,sizeof(prefix)-1);memcpy(diagnostic+sizeof(prefix)-1,key,size+1);
                        ok=chat_print(system,QA_SCRIPT_WARNING,diagnostic,e);free(diagnostic);
                    }
                }
            }
            free(key);
        }
    }
    /* A thrown service failure retains reached heap nodes. Only normal source
     * return walks the raw next word and performs the actual frees. */
    while(ok && head) {
        qa_bot_memory_span bytes;
        ok=integrity_cell(memory,missing,count,head,&bytes,e);
        if(ok) {uint32_t next=word(bytes.data+4);ok=qa_bot_memory_free(memory,missing[head-1].allocation,e);head=next;}
    }
    free(missing);
    qa_bot_chat_asset_release(asset);
    chat_system_release(system);
    if(own_memory) (void)qa_bot_memory_release(memory,NULL);
    return ok;
}
