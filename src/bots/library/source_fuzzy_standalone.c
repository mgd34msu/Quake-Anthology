#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_view.h"
#include "source_fuzzy_standalone.h"

bool bot_weights_standalone_create(const qa_bot_weights_view *view,qa_bot_weights **out,qa_error *error) {
    qa_bot_weights *weights=calloc(1,sizeof(*weights));
    bot_fuzzy_owned *config=calloc(1,sizeof(*config));
    bot_fuzzy_heap *heap=calloc(1,sizeof(*heap));qa_bot_memory *memory=NULL;
    if(!weights || !config || !heap) {
        free(weights);free(config);free(heap);qa_error_set(error,QA_ERROR_MEMORY,0,"Constructing standalone source weights");return false;
    }
    atomic_init(&weights->references,1);config->references=1;weights->source=config;weights->standalone_heap=heap;
    bool ok=qa_bot_memory_create(NULL,&memory,error);
    if(ok) ok=bot_fuzzy_heap_bind(heap,memory,error);
    if(ok) heap->standalone_users=1;
    if(memory) (void)qa_bot_memory_release(memory,NULL);
    size_t path_size=strlen(view->path);
    if(ok) {
        config->path=malloc(path_size+1);
        if(!config->path) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining standalone fuzzy path");ok=false;}
        else memcpy(config->path,view->path,path_size+1);
    }
    if(ok) ok=bot_fuzzy_config_allocate(heap,(qa_bytes){(const uint8_t *)view->path,path_size},&config->source,error);
    uint32_t *pointers=NULL;
    if(ok && view->node_count) {
        if(view->node_count>SIZE_MAX/sizeof(*pointers)) {qa_error_set(error,QA_ERROR_MEMORY,0,"Standalone separator map exceeds native extent");ok=false;}
        else {
            pointers=malloc(view->node_count*sizeof(*pointers));
            if(!pointers) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining standalone separator aliases");ok=false;}
        }
    }
    for(size_t index=0;ok && index<view->weight_count;++index) {
        const qa_bot_weight_definition *definition=&view->weights[index];uint32_t name;
        ok=bot_fuzzy_name_allocate(heap,(qa_bytes){(const uint8_t *)definition->name,strlen(definition->name)},&name,error) &&
            bot_fuzzy_config_pointer_write(&config->source,(int32_t)index,false,name,error);
        for(uint32_t node=definition->root;ok && node<definition->end;++node) {
            bot_fuzzy_separator separator;
            ok=bot_fuzzy_separator_allocate(heap,&separator,error);
            if(ok) pointers[node]=separator.pointer;
        }
        for(uint32_t node=definition->root;ok && node<definition->end;++node) {
            bot_fuzzy_separator separator={heap,pointers[node]};const qa_bot_weight_node *row=&view->nodes[node];
            const qa_bot_weight_value *value=&view->values[node];
            ok=bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_INVENTORY,(uint32_t)row->inventory,error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_THRESHOLD,(uint32_t)row->threshold,error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_BALANCED,row->balanced?1:0,error) &&
                bot_fuzzy_separator_float_write(&separator,BOT_FUZZY_WEIGHT,value->weight,error) &&
                bot_fuzzy_separator_float_write(&separator,BOT_FUZZY_MINIMUM,value->minimum,error) &&
                bot_fuzzy_separator_float_write(&separator,BOT_FUZZY_MAXIMUM,value->maximum,error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_CHILD,row->child==QA_BOT_NO_INDEX?0:pointers[row->child],error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_NEXT,row->next==QA_BOT_NO_INDEX?0:pointers[row->next],error);
        }
        if(ok) ok=bot_fuzzy_config_pointer_write(&config->source,(int32_t)index,true,pointers[definition->root],error) &&
            bot_fuzzy_config_count_write(&config->source,(int32_t)index+1,error);
    }
    free(pointers);
    if(ok) ok=bot_weights_source_view(weights,error);
    if(!ok) {qa_bot_weights_release(weights);return false;}
    *out=weights;return true;
}
