#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_view.h"

typedef struct view_frame {uint32_t pointer,node;unsigned stage;} view_frame;
static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool project_tree(bot_fuzzy_config *config,uint32_t root,bot_weight_topology *topology,
    qa_bot_weight_value **values,size_t *value_capacity,uint32_t *out,qa_error *error) {
    view_frame *frames=NULL;size_t count=0,capacity=0;uint32_t pending=root;
    uint32_t parent=QA_BOT_NO_INDEX;bool child_link=false,ok=true;
    for(;;) {
        for(size_t index=0;index<count;++index) if(frames[index].pointer==pending) {
            ok=fail(error,"Fuzzy source graph contains a cycle");break;
        }
        if(!ok) break;
        if(topology->node_count>=UINT32_MAX) {ok=fail(error,"Fuzzy public projection exceeds its node index domain");break;}
        uint32_t node=(uint32_t)topology->node_count;
        ok=bot_grow((void **)&topology->nodes,&topology->node_capacity,topology->node_count+1,sizeof(*topology->nodes),error) &&
            bot_grow((void **)values,value_capacity,topology->node_count+1,sizeof(**values),error) &&
            bot_grow((void **)&frames,&capacity,count+1,sizeof(*frames),error);
        if(!ok) break;
        bot_fuzzy_separator separator;
        qa_bot_weight_node *row=&topology->nodes[node];qa_bot_weight_value *value=&(*values)[node];int32_t balanced;
        *row=(qa_bot_weight_node){.child=QA_BOT_NO_INDEX,.next=QA_BOT_NO_INDEX};
        ok=bot_fuzzy_separator_bind(config->heap,pending,&separator,error) &&
            bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_INVENTORY,&row->inventory,error) &&
            bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_THRESHOLD,&row->threshold,error) &&
            bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_BALANCED,&balanced,error) &&
            bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_WEIGHT,&value->weight,error) &&
            bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_MINIMUM,&value->minimum,error) &&
            bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_MAXIMUM,&value->maximum,error);
        if(!ok) break;
        row->balanced=balanced==1;
        if(row->inventory>topology->maximum_inventory_index) topology->maximum_inventory_index=row->inventory;
        ++topology->node_count;
        if(parent==QA_BOT_NO_INDEX) *out=node;
        else if(child_link) topology->nodes[parent].child=node;
        else topology->nodes[parent].next=node;
        frames[count++]=(view_frame){pending,node,0};
        bool descend=false;
        while(count) {
            view_frame *frame=&frames[count-1];
            if(frame->stage==2) {--count;continue;}
            separator=(bot_fuzzy_separator){config->heap,frame->pointer};
            child_link=frame->stage++==0;parent=frame->node;
            ok=bot_fuzzy_separator_word_read(&separator,child_link?BOT_FUZZY_CHILD:BOT_FUZZY_NEXT,&pending,error);
            if(!ok) break;
            if(pending) {descend=true;break;}
        }
        if(!ok || !descend) break;
    }
    free(frames);return ok;
}
bool bot_weights_source_view(qa_bot_weights *weights,qa_error *error) {
    if(!weights || !weights->source || !bot_fuzzy_owned_open(weights->source,error))
        return fail(error,"Fuzzy view requires its actual open configuration");
    bot_weight_topology *topology=calloc(1,sizeof(*topology));qa_bot_weight_value *values=NULL;size_t value_capacity=0;
    if(!topology) {qa_error_set(error,QA_ERROR_MEMORY,0,"Projecting source fuzzy metadata");return false;}
    atomic_init(&topology->references,1);topology->maximum_inventory_index=-1;
    const char *path=weights->source->path;
    topology->path=bot_string(&topology->arena,(qa_bytes){(const uint8_t *)path,strlen(path)},error);
    bool ok=topology->path!=NULL;
    bot_fuzzy_config *config=&weights->source->source;
    for(int32_t index=0;ok;++index) {
        int32_t count;uint32_t name,root;qa_bytes text;
        ok=bot_fuzzy_config_count(config,&count,error);
        if(!ok || index>=count) break;
        ok=bot_fuzzy_config_pointer_read(config,index,false,&name,error) &&
            bot_fuzzy_name_read(config->heap,name,&text,error) &&
            bot_fuzzy_config_pointer_read(config,index,true,&root,error);
        if(!ok) break;
        qa_bot_weight_definition *definition=&topology->weights[index];
        definition->name=bot_string(&topology->arena,text,error);
        ok=definition->name!=NULL;
        if(ok && !root) ok=fail(error,"Fuzzy weight has no genuine source separator");
        if(ok) ok=project_tree(config,root,topology,&values,&value_capacity,&definition->root,error);
        if(!ok) break;
        definition->end=(uint32_t)topology->node_count;++topology->weight_count;
    }
    if(!ok) {bot_weight_topology_release(topology);free(values);return false;}
    bot_weight_topology_release(weights->topology);free(weights->values);
    weights->topology=topology;weights->values=values;weights->value_capacity=value_capacity;
    bot_weights_view(weights);return true;
}
