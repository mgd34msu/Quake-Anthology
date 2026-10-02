#include "source_graph.h"
#include "internal.h"
#include "../save_fields.h"
#include "../memory/internal.h"
#include "qa/bots_allocator_save.h"

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void put(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(index*8));
}
static uint32_t width(bot_chat_graph_kind kind) {
    static const uint32_t widths[]={1,8,16,20,16,12,20};
    unsigned index=(unsigned)kind;
    return index<=BOT_CHAT_GRAPH_REPLY?widths[index]:0;
}
static bool target(const bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    const bot_chat_graph_pointer **out,qa_error *error) {
    if(!pointer || pointer>graph->count || graph->pointers[pointer-1].kind!=kind)
        return fail(error,"Chat pointer does not identify its actual typed graph view");
    *out=&graph->pointers[pointer-1];return true;
}
static bool bytes(const bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    qa_bot_memory_span *out,qa_error *error) {
    const bot_chat_graph_pointer *view;
    if(!target(graph,pointer,kind,&view,error) || !qa_bot_memory_bytes(graph->memory,view->allocation,out,error)) return false;
    if(view->offset>out->size || width(kind)>out->size-view->offset)
        return fail(error,"Chat graph view exceeds its actual HUNK allocation");
    out->data+=view->offset;out->size-=view->offset;return true;
}
void bot_chat_graph_dispose(bot_chat_graph *graph) {
    if(!graph) return;
    free(graph->pointers);*graph=(bot_chat_graph){0};
}
static bool register_view(bot_chat_graph *graph,qa_bot_memory_allocation allocation,
    bot_chat_graph_kind kind,uint32_t offset,uint32_t *out,qa_error *error) {
    if(graph->count>=UINT32_MAX) return fail(error,"Chat graph pointer map exceeds the source word domain");
    if(!bot_grow((void **)&graph->pointers,&graph->capacity,graph->count+1,sizeof(*graph->pointers),error)) return false;
    graph->pointers[graph->count++]=(bot_chat_graph_pointer){.allocation=allocation,.offset=offset,.kind=kind};
    *out=(uint32_t)graph->count;return true;
}
static bool string_view(bot_chat_graph *graph,qa_bot_memory_allocation allocation,uint32_t offset,
    qa_bytes text,uint32_t *out,qa_error *error) {
    qa_bot_memory_span span;
    if(!qa_bot_memory_bytes(graph->memory,allocation,&span,error)) return false;
    if(offset>=span.size || text.size>=span.size-offset) return fail(error,"Chat graph string exceeds its actual HUNK extent");
    if(text.size) memcpy(span.data+offset,text.data,text.size);
    span.data[offset+text.size]=0;
    return register_view(graph,allocation,BOT_CHAT_GRAPH_STRING,offset,out,error);
}
bool bot_chat_graph_new(bot_chat_graph *graph,bot_chat_graph_kind kind,qa_bytes text,uint32_t *out,
    qa_error *error) {
    uint32_t size=width(kind);
    if(!graph || !graph->memory || !out || !size || (text.size && !text.data))
        return fail(error,"Chat graph allocation requires its actual MEMORY/kind/text/output");
    if(kind==BOT_CHAT_GRAPH_STRING || kind==BOT_CHAT_GRAPH_MATCH_STRING || kind==BOT_CHAT_GRAPH_MESSAGE) {
        if(text.size>=UINT32_MAX-size) return fail(error,"Chat graph text exceeds the source allocation extent");
        size=kind==BOT_CHAT_GRAPH_STRING?(uint32_t)text.size+1:size+(uint32_t)text.size+1;
    }
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(graph->memory,size,QA_BOT_MEMORY_HUNK,true,NULL,&allocation,error)) return false;
    if(kind==BOT_CHAT_GRAPH_STRING) return string_view(graph,allocation,0,text,out,error);
    if(!register_view(graph,allocation,kind,0,out,error)) return false;
    if(kind==BOT_CHAT_GRAPH_MATCH_STRING || kind==BOT_CHAT_GRAPH_MESSAGE) {
        uint32_t string;
        if(!string_view(graph,allocation,width(kind),text,&string,error) ||
           !bot_chat_graph_write(graph,*out,kind,0,string,error)) return false;
    }
    return true;
}
bool bot_chat_graph_word(const bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    uint32_t offset,uint32_t *out,qa_error *error) {
    qa_bot_memory_span span;
    if(!bytes(graph,pointer,kind,&span,error)) return false;
    if(offset>span.size || 4>span.size-offset) return fail(error,"Chat graph word exceeds its actual view");
    *out=word(span.data+offset);return true;
}
bool bot_chat_graph_write(bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    uint32_t offset,uint32_t value,qa_error *error) {
    qa_bot_memory_span span;
    if(!bytes(graph,pointer,kind,&span,error)) return false;
    if(offset>span.size || 4>span.size-offset) return fail(error,"Chat graph word exceeds its actual view");
    put(span.data+offset,value);return true;
}
bool bot_chat_graph_link(const bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    uint32_t offset,bot_chat_graph_kind destination,uint32_t *out,qa_error *error) {
    if(!bot_chat_graph_word(graph,pointer,kind,offset,out,error)) return false;
    const bot_chat_graph_pointer *view;
    return !*out || target(graph,*out,destination,&view,error);
}
bool bot_chat_graph_text(const bot_chat_graph *graph,uint32_t pointer,const char **out,qa_error *error) {
    qa_bot_memory_span span;
    if(!bytes(graph,pointer,BOT_CHAT_GRAPH_STRING,&span,error)) return false;
    if(!memchr(span.data,0,span.size)) return fail(error,"Chat graph string has no source terminator");
    *out=(const char *)span.data;return true;
}
static bool free_block(bot_chat_graph *graph,uint32_t pointer,qa_error *error) {
    if(!pointer || pointer>graph->count) return fail(error,"Chat free has no actual graph block");
    return qa_bot_memory_free(graph->memory,graph->pointers[pointer-1].allocation,error);
}
bool bot_chat_graph_free_pieces(bot_chat_graph *graph,uint32_t first,qa_error *error) {
    for(size_t seen=0;first;++seen) {
        if(seen>=graph->count) return fail(error,"Chat match piece list cycles");
        uint32_t next,type;
        if(!bot_chat_graph_link(graph,first,BOT_CHAT_GRAPH_PIECE,12,BOT_CHAT_GRAPH_PIECE,&next,error) ||
           !bot_chat_graph_word(graph,first,BOT_CHAT_GRAPH_PIECE,0,&type,error)) return false;
        if(type==2) {
            uint32_t string;
            if(!bot_chat_graph_link(graph,first,BOT_CHAT_GRAPH_PIECE,4,BOT_CHAT_GRAPH_MATCH_STRING,&string,error)) return false;
            for(size_t count=0;string;++count) {
                if(count>=graph->count) return fail(error,"Chat match string list cycles");
                uint32_t after;
                if(!bot_chat_graph_link(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,4,BOT_CHAT_GRAPH_MATCH_STRING,&after,error) ||
                   !free_block(graph,string,error)) return false;
                string=after;
            }
        }
        if(!free_block(graph,first,error)) return false;
        first=next;
    }
    return true;
}
bool bot_chat_graph_free_root(bot_chat_graph *graph,bot_chat_graph_kind kind,qa_error *error) {
    if(kind!=BOT_CHAT_GRAPH_TEMPLATE && kind!=BOT_CHAT_GRAPH_REPLY) return fail(error,"Chat root has no source graph kind");
    uint32_t root=graph->root;
    for(size_t seen=0;root;++seen) {
        if(seen>=graph->count) return fail(error,"Chat root list cycles");
        uint32_t next;
        if(!bot_chat_graph_link(graph,root,kind,16,kind,&next,error)) return false;
        if(kind==BOT_CHAT_GRAPH_TEMPLATE) {
            uint32_t piece;
            if(!bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_PIECE,&piece,error) ||
               !bot_chat_graph_free_pieces(graph,piece,error)) return false;
        } else {
            uint32_t key;
            if(!bot_chat_graph_link(graph,root,kind,0,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
            for(size_t count=0;key;++count) {
                if(count>=graph->count) return fail(error,"Chat reply key list cycles");
                uint32_t after,piece,string;
                if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,12,BOT_CHAT_GRAPH_KEY,&after,error) ||
                   !bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,8,BOT_CHAT_GRAPH_PIECE,&piece,error) ||
                   !bot_chat_graph_free_pieces(graph,piece,error) ||
                   !bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,4,BOT_CHAT_GRAPH_STRING,&string,error) ||
                   (string && !free_block(graph,string,error)) || !free_block(graph,key,error)) return false;
                key=after;
            }
            uint32_t message;
            if(!bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            for(size_t count=0;message;++count) {
                if(count>=graph->count) return fail(error,"Chat reply message list cycles");
                uint32_t after;
                if(!bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&after,error) ||
                   !free_block(graph,message,error)) return false;
                message=after;
            }
        }
        if(!free_block(graph,root,error)) return false;
        root=next;
    }
    graph->root=0;return true;
}
bool bot_chat_graph_copy(const bot_chat_graph *source,const qa_bot_memory_prepared *memory,
    bot_chat_graph *out,qa_error *error) {
    *out=*source;out->pointers=NULL;out->capacity=out->count;
    if(!source->count) return true;
    out->pointers=malloc(source->count*sizeof(*out->pointers));
    if(!out->pointers) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining chat graph pointer history");return false;}
    memcpy(out->pointers,source->pointers,source->count*sizeof(*out->pointers));
    for(size_t index=0;memory && index<source->count;++index)
        if(!qa_bot_memory_checkpoint_resolve(memory,source->pointers[index].allocation,&out->pointers[index].allocation,error)) return false;
    return true;
}
typedef struct graph_edge {uint32_t offset;bot_chat_graph_kind kind;} graph_edge;
typedef struct graph_visit {uint32_t pointer;size_t edge;} graph_visit;
static size_t edges_for(bot_chat_graph_kind kind,graph_edge edges[3]) {
    size_t count=0;
    switch(kind) {
    case BOT_CHAT_GRAPH_STRING:break;
    case BOT_CHAT_GRAPH_MATCH_STRING: edges[count++]=(graph_edge){0,BOT_CHAT_GRAPH_STRING};edges[count++]=(graph_edge){4,BOT_CHAT_GRAPH_MATCH_STRING};break;
    case BOT_CHAT_GRAPH_PIECE: edges[count++]=(graph_edge){4,BOT_CHAT_GRAPH_MATCH_STRING};edges[count++]=(graph_edge){12,BOT_CHAT_GRAPH_PIECE};break;
    case BOT_CHAT_GRAPH_TEMPLATE: edges[count++]=(graph_edge){12,BOT_CHAT_GRAPH_PIECE};edges[count++]=(graph_edge){16,BOT_CHAT_GRAPH_TEMPLATE};break;
    case BOT_CHAT_GRAPH_KEY: edges[count++]=(graph_edge){4,BOT_CHAT_GRAPH_STRING};edges[count++]=(graph_edge){8,BOT_CHAT_GRAPH_PIECE};edges[count++]=(graph_edge){12,BOT_CHAT_GRAPH_KEY};break;
    case BOT_CHAT_GRAPH_MESSAGE: edges[count++]=(graph_edge){0,BOT_CHAT_GRAPH_STRING};edges[count++]=(graph_edge){8,BOT_CHAT_GRAPH_MESSAGE};break;
    case BOT_CHAT_GRAPH_REPLY: edges[count++]=(graph_edge){0,BOT_CHAT_GRAPH_KEY};edges[count++]=(graph_edge){12,BOT_CHAT_GRAPH_MESSAGE};edges[count++]=(graph_edge){16,BOT_CHAT_GRAPH_REPLY};break;
    }
    return count;
}
static bool topology(const bot_chat_graph *graph,qa_error *error) {
    if(!graph->count) return true;
    if(graph->count>SIZE_MAX/sizeof(graph_visit)) return fail(error,"Chat graph validation exceeds native extent");
    uint8_t *marks=calloc(graph->count,1);graph_visit *stack=calloc(graph->count,sizeof(*stack));
    if(!marks || !stack) {free(marks);free(stack);qa_error_set(error,QA_ERROR_MEMORY,0,"Validating retained source chat graph");return false;}
    bool ok=true;
    for(size_t root=0;ok && root<graph->count;++root) {
        if(marks[root]==2) continue;
        size_t depth=1;stack[0]=(graph_visit){.pointer=(uint32_t)root+1};marks[root]=1;
        while(ok && depth) {
            graph_visit *frame=&stack[depth-1];
            bot_chat_graph_kind kind=graph->pointers[frame->pointer-1].kind;
            graph_edge edges[3];size_t count=edges_for(kind,edges);
            if(kind==BOT_CHAT_GRAPH_STRING) {const char *string;ok=bot_chat_graph_text(graph,frame->pointer,&string,error);}
            if(!ok) break;
            if(frame->edge==count) {marks[frame->pointer-1]=2;--depth;continue;}
            graph_edge edge=edges[frame->edge++];uint32_t next;
            ok=bot_chat_graph_link(graph,frame->pointer,kind,edge.offset,edge.kind,&next,error);
            if(!ok || !next || marks[next-1]==2) continue;
            if(marks[next-1]==1) {ok=fail(error,"Saved chat graph contains a cycle");break;}
            marks[next-1]=1;stack[depth++]=(graph_visit){.pointer=next};
        }
    }
    free(marks);free(stack);return ok;
}
bool bot_chat_graph_fields(qa_source_save_io *io,bot_chat_graph *graph) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint32_t cleanup=(uint32_t)graph->cleanup;
    bool ok=qa_source_save_count(io,&graph->count,UINT32_MAX) && qa_source_save_u32(io,&graph->root) &&
        qa_source_save_u32(io,&graph->unfinished) && qa_source_save_u32(io,&cleanup) && cleanup<=BOT_CHAT_GRAPH_GRAPHS;
    if(reading) graph->cleanup=(bot_chat_graph_cleanup)cleanup;
    if(ok && reading && graph->count) {
        if(io->offset>io->input.size || graph->count>(io->input.size-io->offset)/16 || graph->count>SIZE_MAX/sizeof(*graph->pointers)) ok=false;
        else {graph->pointers=calloc(graph->count,sizeof(*graph->pointers));ok=graph->pointers!=NULL;graph->capacity=graph->count;}
    }
    for(size_t index=0;ok && index<graph->count;++index) {
        bot_chat_graph_pointer *view=&graph->pointers[index];size_t reference=0;uint32_t kind=(uint32_t)view->kind;
        ok=(reading || qa_bot_memory_reference(graph->memory,view->allocation,&reference,io->error)) &&
            qa_source_save_count(io,&reference,SIZE_MAX) && qa_source_save_u32(io,&kind) && kind<=BOT_CHAT_GRAPH_REPLY &&
            qa_source_save_u32(io,&view->offset);
        if(reading) view->kind=(bot_chat_graph_kind)kind;
        if(ok && reading) ok=qa_bot_memory_resolve(graph->memory,reference,&view->allocation,io->error);
        if(ok) {
            bot_memory_record *record=bot_memory_record_get(graph->memory,view->allocation);qa_bot_memory_span span;
            ok=record && record->kind==QA_BOT_MEMORY_HUNK && (view->kind==BOT_CHAT_GRAPH_STRING || !view->offset) &&
                bytes(graph,(uint32_t)index+1,view->kind,&span,io->error);
        }
    }
    if(ok) ok=graph->root<=graph->count && graph->unfinished<=graph->count;
    if(ok) ok=topology(graph,io->error);
    if(!ok && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained chat graph topology");
    return ok;
}

static bool mapped(uint32_t **map,size_t index,uint32_t pointer,qa_error *error) {
    if(index>=UINT32_MAX || index>=SIZE_MAX/sizeof(**map)) return fail(error,"Chat projection exceeds its source index domain");
    uint32_t *next=realloc(*map,(index+1)*sizeof(*next));
    if(!next) {qa_error_set(error,QA_ERROR_MEMORY,0,"Projecting actual chat graph identity");return false;}
    *map=next;next[index]=pointer;return true;
}
static bool project_text(const bot_chat_graph *graph,uint32_t pointer,bot_chat_graph_kind kind,
    const char **out,qa_error *error) {
    uint32_t string;
    return bot_chat_graph_link(graph,pointer,kind,0,BOT_CHAT_GRAPH_STRING,&string,error) &&
        bot_chat_graph_text(graph,string,out,error);
}
static bool project_pieces(qa_bot_chat_asset *asset,uint32_t first,qa_bot_chat_range *out,qa_error *error) {
    bot_chat_graph *graph=&asset->packed_source->graph;
    if(asset->view.piece_count>UINT32_MAX) return fail(error,"Chat pieces exceed source range domain");
    *out=(qa_bot_chat_range){.first=(uint32_t)asset->view.piece_count};
    for(size_t seen=0;first;++seen) {
        if(seen>=graph->count) return fail(error,"Chat piece projection cycles");
        uint32_t type,variable;qa_bot_chat_piece piece={0};
        if(!bot_chat_graph_word(graph,first,BOT_CHAT_GRAPH_PIECE,0,&type,error)) return false;
        if(type==1) {
            if(!bot_chat_graph_word(graph,first,BOT_CHAT_GRAPH_PIECE,8,&variable,error)) return false;
            piece.kind=QA_BOT_CHAT_VARIABLE;piece.data.variable=variable;
        } else {
            if(type!=2) return fail(error,"Unknown source match piece type");
            piece.kind=QA_BOT_CHAT_ALTERNATIVES;
            if(asset->view.alternative_count>UINT32_MAX) return fail(error,"Chat alternatives exceed source range domain");
            piece.data.alternatives.first=(uint32_t)asset->view.alternative_count;
            uint32_t string=0;
            if(type==2 && !bot_chat_graph_link(graph,first,BOT_CHAT_GRAPH_PIECE,4,BOT_CHAT_GRAPH_MATCH_STRING,&string,error)) return false;
            for(size_t count=0;string;++count) {
                if(count>=graph->count) return fail(error,"Chat alternative projection cycles");
                const char *text;
                if(!project_text(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,&text,error) ||
                   !mapped(&asset->graph_alternatives,asset->view.alternative_count,string,error) ||
                   !chat_append((void **)&asset->alternatives,&asset->view.alternative_count,&asset->alternative_capacity,
                        sizeof(*asset->alternatives),&text,error)) return false;
                ++piece.data.alternatives.count;
                if(!bot_chat_graph_link(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,4,BOT_CHAT_GRAPH_MATCH_STRING,&string,error)) return false;
            }
        }
        if(!mapped(&asset->graph_pieces,asset->view.piece_count,first,error) ||
           !chat_append((void **)&asset->pieces,&asset->view.piece_count,&asset->piece_capacity,sizeof(piece),&piece,error)) return false;
        ++out->count;
        if(!bot_chat_graph_link(graph,first,BOT_CHAT_GRAPH_PIECE,12,BOT_CHAT_GRAPH_PIECE,&first,error)) return false;
    }
    return true;
}
bool bot_chat_graph_project(qa_bot_chat_asset *asset,qa_error *error) {
    if(!asset || !asset->packed_source) return fail(error,"Chat graph projection requires its actual retained owner");
    bot_chat_graph *graph=&asset->packed_source->graph;
    asset->view.piece_count=asset->view.alternative_count=asset->view.template_count=0;
    asset->view.key_count=asset->view.reply_count=asset->view.message_count=0;
    bot_chat_graph_kind kind=asset->view.kind==QA_BOT_CHAT_MATCHES?BOT_CHAT_GRAPH_TEMPLATE:BOT_CHAT_GRAPH_REPLY;
    uint32_t root=graph->root;
    for(size_t seen=0;root;++seen) {
        if(seen>=graph->count) return fail(error,"Chat root projection cycles");
        if(kind==BOT_CHAT_GRAPH_TEMPLATE) {
            qa_bot_chat_template value={0};uint32_t bits,first;
            if(!bot_chat_graph_word(graph,root,kind,0,&value.context,error) ||
               !bot_chat_graph_word(graph,root,kind,4,&bits,error)) return false;
            memcpy(&value.type,&bits,4);
            if(!bot_chat_graph_word(graph,root,kind,8,&bits,error) ||
               !bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_PIECE,&first,error)) return false;
            memcpy(&value.subtype,&bits,4);
            if(!project_pieces(asset,first,&value.pieces,error) ||
               !mapped(&asset->graph_templates,asset->view.template_count,root,error) ||
               !chat_append((void **)&asset->templates,&asset->view.template_count,&asset->template_capacity,sizeof(value),&value,error)) return false;
        } else {
            qa_bot_chat_reply value={0};uint32_t bits,key,message;
            if(!bot_chat_graph_word(graph,root,kind,4,&bits,error) ||
               !bot_chat_graph_link(graph,root,kind,0,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
            memcpy(&value.priority,&bits,4);
            if(asset->view.key_count>UINT32_MAX || asset->view.message_count>UINT32_MAX)
                return fail(error,"Reply projection exceeds source range domain");
            value.keys.first=(uint32_t)asset->view.key_count;
            for(size_t count=0;key;++count) {
                if(count>=graph->count) return fail(error,"Chat key projection cycles");
                qa_bot_chat_key item={0};uint32_t flags,first;
                if(!bot_chat_graph_word(graph,key,BOT_CHAT_GRAPH_KEY,0,&flags,error)) return false;
                item.mode=flags&1?QA_BOT_CHAT_AND:flags&2?QA_BOT_CHAT_NOT:QA_BOT_CHAT_ANY;
                if(flags&4) item.kind=QA_BOT_CHAT_NAME;
                else if(flags&32) {
                    item.kind=QA_BOT_CHAT_BOT_NAMES;
                    if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,4,BOT_CHAT_GRAPH_STRING,&first,error) ||
                       !bot_chat_graph_text(graph,first,&item.data.text,error)) return false;
                }
                else if(flags&(64|128|256)) {item.kind=QA_BOT_CHAT_GENDER;item.data.gender=flags&64?1u:flags&128?2u:0u;}
                else if(flags&16) {
                    item.kind=QA_BOT_CHAT_PATTERN;
                    if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,8,BOT_CHAT_GRAPH_PIECE,&first,error) ||
                       !project_pieces(asset,first,&item.data.pieces,error)) return false;
                } else {
                    if(!(flags&8)) return fail(error,"Unknown source reply key flags");
                    item.kind=QA_BOT_CHAT_WORD;
                    if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,4,BOT_CHAT_GRAPH_STRING,&first,error) ||
                       !bot_chat_graph_text(graph,first,&item.data.text,error)) return false;
                }
                if(!mapped(&asset->graph_keys,asset->view.key_count,key,error) ||
                   !chat_append((void **)&asset->keys,&asset->view.key_count,&asset->key_capacity,sizeof(item),&item,error)) return false;
                ++value.keys.count;
                if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,12,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
            }
            value.messages.first=(uint32_t)asset->view.message_count;
            if(!bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            for(size_t count=0;message;++count) {
                if(count>=graph->count) return fail(error,"Chat message projection cycles");
                const char *text;
                if(!project_text(graph,message,BOT_CHAT_GRAPH_MESSAGE,&text,error) ||
                   !mapped(&asset->graph_messages,asset->view.message_count,message,error) ||
                   !chat_append((void **)&asset->messages,&asset->view.message_count,&asset->message_capacity,sizeof(text),&text,error)) return false;
                ++value.messages.count;
                if(!bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            }
            if(!mapped(&asset->graph_replies,asset->view.reply_count,root,error) ||
               !chat_append((void **)&asset->replies,&asset->view.reply_count,&asset->reply_capacity,sizeof(value),&value,error)) return false;
        }
        if(!bot_chat_graph_link(graph,root,kind,16,kind,&root,error)) return false;
    }
    if(asset->view.message_count) {
        float *cooldowns=realloc(asset->cooldowns,asset->view.message_count*sizeof(*cooldowns));
        if(!cooldowns) {qa_error_set(error,QA_ERROR_MEMORY,0,"Projecting live reply cooldowns");return false;}
        asset->cooldowns=cooldowns;
        for(uint32_t index=0;index<asset->view.message_count;++index)
            if(!bot_chat_graph_message_time(asset,asset->graph_messages[index],&cooldowns[index],false,error)) return false;
    }
    chat_asset_view(asset);return true;
}
bool bot_chat_graph_message_text(const qa_bot_chat_asset *asset,uint32_t pointer,const char **out,qa_error *error) {
    return project_text(&asset->packed_source->graph,pointer,BOT_CHAT_GRAPH_MESSAGE,out,error);
}
bool bot_chat_graph_message_time(qa_bot_chat_asset *asset,uint32_t pointer,float *value,bool write,qa_error *error) {
    bot_chat_graph *graph=&asset->packed_source->graph;uint32_t bits;
    if(write) {memcpy(&bits,value,4);return bot_chat_graph_write(graph,pointer,BOT_CHAT_GRAPH_MESSAGE,4,bits,error);}
    if(!bot_chat_graph_word(graph,pointer,BOT_CHAT_GRAPH_MESSAGE,4,&bits,error)) return false;
    memcpy(value,&bits,4);return true;
}
bool bot_chat_graph_message_next(const qa_bot_chat_asset *asset,bot_chat_graph_messages *cursor,qa_error *error) {
    const bot_chat_graph *graph=&asset->packed_source->graph;
    if(!cursor->started) {cursor->reply=graph->root;cursor->started=true;}
    else if(cursor->message) {
        if(!bot_chat_graph_link(graph,cursor->message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&cursor->message,error)) return false;
        if(!cursor->message && cursor->reply && !bot_chat_graph_link(graph,cursor->reply,
            BOT_CHAT_GRAPH_REPLY,16,BOT_CHAT_GRAPH_REPLY,&cursor->reply,error)) return false;
        if(cursor->message) {
            if(++cursor->messages>graph->count) return fail(error,"Reply integrity message list cycles");
            return true;
        }
    } else return true;
    while(cursor->reply) {
        if(++cursor->replies>graph->count) return fail(error,"Reply integrity root list cycles");
        if(!bot_chat_graph_link(graph,cursor->reply,BOT_CHAT_GRAPH_REPLY,12,BOT_CHAT_GRAPH_MESSAGE,&cursor->message,error)) return false;
        if(cursor->message) {
            if(++cursor->messages>graph->count) return fail(error,"Reply integrity message list cycles");
            return true;
        }
        if(!bot_chat_graph_link(graph,cursor->reply,BOT_CHAT_GRAPH_REPLY,16,BOT_CHAT_GRAPH_REPLY,&cursor->reply,error)) return false;
    }
    return true;
}
