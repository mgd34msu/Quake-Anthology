#include "source_fuzzy_operations.h"
#include <stdlib.h>
#include <string.h>

typedef struct fuzzy_frame {
    uint32_t pointer,right;
    unsigned stage;
    bool undecided;
    float left;
} fuzzy_frame;
typedef struct fuzzy_stack {fuzzy_frame *frames;size_t count,capacity;} fuzzy_stack;
static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool push(fuzzy_stack *stack,uint32_t pointer,bool undecided,qa_error *error) {
    if(stack->count==stack->capacity) {
        size_t capacity=stack->capacity?stack->capacity*2:16;
        if(capacity<stack->capacity || capacity>SIZE_MAX/sizeof(*stack->frames))
            return fail(error,"Fuzzy traversal storage exceeds its native extent");
        fuzzy_frame *frames=realloc(stack->frames,capacity*sizeof(*frames));
        if(!frames) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy source traversal");return false;}
        stack->frames=frames;stack->capacity=capacity;
    }
    stack->frames[stack->count++]=(fuzzy_frame){.pointer=pointer,.undecided=undecided};return true;
}
static size_t terminated(qa_bytes bytes) {
    const uint8_t *zero=bytes.size?memchr(bytes.data,0,bytes.size):NULL;
    return zero?(size_t)(zero-bytes.data):bytes.size;
}
bool bot_fuzzy_find(const bot_fuzzy_config *config,qa_bytes name,int32_t *out,qa_error *error) {
    if(!out || (name.size && !name.data)) return fail(error,"Fuzzy lookup requires its source name/output");
    size_t length=terminated(name);*out=-1;
    for(int32_t index=0;;++index) {
        int32_t count;uint32_t pointer;qa_bytes stored;
        if(!bot_fuzzy_config_count(config,&count,error)) return false;
        if(index>=count) return true;
        if(!bot_fuzzy_config_pointer_read(config,index,false,&pointer,error) ||
           !bot_fuzzy_name_read(config->heap,pointer,&stored,error)) return false;
        if(stored.size==length && (!length || !memcmp(stored.data,name.data,length))) {*out=index;return true;}
    }
}
static bool inventory_read(const qa_bot_inventory_view *inventory,int32_t index,int32_t *out,qa_error *error) {
    if(inventory->read) return inventory->read(inventory->context,index,out,error);
    if(index<0 || (size_t)index>=inventory->count) return fail(error,"Fuzzy inventory index exceeds its source observation");
    *out=inventory->data[index];return true;
}
static int32_t signed_word(uint32_t word) {
    return word<=INT32_MAX?(int32_t)word:-1-(int32_t)(UINT32_MAX-word);
}
static bool leaf(bot_fuzzy_heap *heap,uint32_t pointer,bool undecided,
    const qa_bot_random_source *random,fuzzy_stack *stack,float *last,qa_error *error) {
    bot_fuzzy_separator separator;uint32_t child;
    if(!bot_fuzzy_separator_bind(heap,pointer,&separator,error) ||
       !bot_fuzzy_separator_word_read(&separator,BOT_FUZZY_CHILD,&child,error)) return false;
    if(child) return push(stack,child,undecided,error);
    if(!undecided) return bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_WEIGHT,last,error);
    float maximum,minimum,base;
    if(!bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_MAXIMUM,&maximum,error) ||
       !bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_MINIMUM,&minimum,error) ||
       !bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_MINIMUM,&base,error)) return false;
    float range=maximum-minimum;
    float unit=(float)(random->next(random->context)&UINT32_C(0x7fff))/32767.0f;
    float scaled=unit*range;*last=base+scaled;return true;
}
bool bot_fuzzy_evaluate(const bot_fuzzy_config *config,int32_t index,const qa_bot_inventory_view *inventory,
    const qa_bot_random_source *random,float *out,qa_error *error) {
    if(!config || !inventory || !out || (!inventory->read && inventory->count && !inventory->data) ||
       (random && !random->next)) return fail(error,"Fuzzy evaluation requires actual source inputs/output");
    int32_t count;uint32_t root;
    if(!bot_fuzzy_config_count(config,&count,error)) return false;
    if(index<0 || index>=count) return fail(error,"Fuzzy weight index exceeds its source count");
    if(!bot_fuzzy_config_pointer_read(config,index,true,&root,error)) return false;
    if(!root) return fail(error,"Fuzzy weight has no source separator");
    fuzzy_stack stack={0};float last=0;bool ok=push(&stack,root,random!=NULL,error);
    while(ok && stack.count) {
        fuzzy_frame *frame=&stack.frames[stack.count-1];
        if(frame->stage==1) {--stack.count;continue;}
        bot_fuzzy_separator separator;
        if(!bot_fuzzy_separator_bind(config->heap,frame->pointer,&separator,error)) {ok=false;break;}
        if(frame->stage==2) {
            frame->left=last;frame->stage=3;
            bot_fuzzy_separator right;uint32_t child;
            ok=bot_fuzzy_separator_bind(config->heap,frame->right,&right,error) &&
                bot_fuzzy_separator_word_read(&right,BOT_FUZZY_CHILD,&child,error);
            if(ok) ok=leaf(config->heap,frame->right,frame->undecided && !child,random,&stack,&last,error);
            continue;
        }
        int32_t inventory_index,value,threshold;
        ok=bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_INVENTORY,&inventory_index,error) &&
            inventory_read(inventory,inventory_index,&value,error) &&
            bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_THRESHOLD,&threshold,error);
        if(!ok) break;
        if(frame->stage==3) {
            bot_fuzzy_separator right;int32_t upper;
            ok=bot_fuzzy_separator_bind(config->heap,frame->right,&right,error) &&
                bot_fuzzy_separator_integer_read(&right,BOT_FUZZY_THRESHOLD,&upper,error);
            if(!ok) break;
            int32_t numerator=signed_word((uint32_t)value-(uint32_t)threshold);
            int32_t denominator=signed_word((uint32_t)upper-(uint32_t)threshold);
            if(!denominator) {ok=fail(error,"Fuzzy interpolation thresholds divide by zero");break;}
            float scale=(float)((int64_t)numerator/(int64_t)denominator);
            float left=scale*frame->left,inverse=1.0f-scale,right_value=inverse*last;
            last=left+right_value;--stack.count;continue;
        }
        if(value<threshold) {
            frame->stage=1;ok=leaf(config->heap,frame->pointer,frame->undecided,random,&stack,&last,error);continue;
        }
        uint32_t next;
        ok=bot_fuzzy_separator_word_read(&separator,BOT_FUZZY_NEXT,&next,error);
        if(!ok) break;
        if(!next) {
            ok=bot_fuzzy_separator_float_read(&separator,BOT_FUZZY_WEIGHT,&last,error);
            --stack.count;continue;
        }
        bot_fuzzy_separator right;
        ok=bot_fuzzy_separator_integer_read(&separator,BOT_FUZZY_INVENTORY,&inventory_index,error) &&
            inventory_read(inventory,inventory_index,&value,error) &&
            bot_fuzzy_separator_bind(config->heap,next,&right,error) &&
            bot_fuzzy_separator_integer_read(&right,BOT_FUZZY_THRESHOLD,&threshold,error);
        if(!ok) break;
        if(value<threshold) {
            frame->right=next;frame->stage=2;
            ok=leaf(config->heap,frame->pointer,frame->undecided,random,&stack,&last,error);
        } else frame->pointer=next;
    }
    free(stack.frames);if(ok) *out=last;return ok;
}
bool bot_fuzzy_separator_tree_free(bot_fuzzy_heap *heap,uint32_t pointer,qa_error *error) {
    if(!pointer) return true;
    fuzzy_stack stack={0};bool ok=push(&stack,pointer,false,error);
    while(ok && stack.count) {
        fuzzy_frame *frame=&stack.frames[stack.count-1];bot_fuzzy_separator separator;uint32_t next;
        ok=bot_fuzzy_separator_bind(heap,frame->pointer,&separator,error);
        if(!ok) break;
        if(frame->stage<2) {
            bot_fuzzy_word field=frame->stage?BOT_FUZZY_NEXT:BOT_FUZZY_CHILD;
            ++frame->stage;ok=bot_fuzzy_separator_word_read(&separator,field,&next,error);
            if(ok && next) ok=push(&stack,next,false,error);
        } else {
            ok=bot_fuzzy_pointer_free(heap,frame->pointer,BOT_FUZZY_SEPARATOR,error);--stack.count;
        }
    }
    free(stack.frames);return ok;
}
bool bot_fuzzy_config_free(bot_fuzzy_config *config,qa_error *error) {
    if(!config) return fail(error,"Fuzzy release requires its genuine configuration");
    for(int32_t index=0;;++index) {
        int32_t count;uint32_t pointer;
        if(!bot_fuzzy_config_count(config,&count,error)) return false;
        if(index>=count) break;
        if(!bot_fuzzy_config_pointer_read(config,index,true,&pointer,error) ||
           !bot_fuzzy_separator_tree_free(config->heap,pointer,error) ||
           !bot_fuzzy_config_pointer_read(config,index,false,&pointer,error)) return false;
        if(pointer && !bot_fuzzy_pointer_free(config->heap,pointer,BOT_FUZZY_NAME,error)) return false;
    }
    return qa_bot_memory_free(config->heap->memory,config->allocation,error);
}
typedef enum fuzzy_mutation {FUZZY_SCALE,FUZZY_RANGE,FUZZY_EVOLVE} fuzzy_mutation;
static bool mutate_leaf(const bot_fuzzy_separator *separator,fuzzy_mutation mutation,float scale,
    const qa_bot_random_source *random,qa_error *error) {
    int32_t balanced;
    if(!bot_fuzzy_separator_integer_read(separator,BOT_FUZZY_BALANCED,&balanced,error)) return false;
    if(balanced!=1) return true;
    float first,second,value,limit;
    if(mutation==FUZZY_SCALE) {
        if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&first,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&second,error)) return false;
        float sum=first+second;
        if(!bot_fuzzy_separator_float_write(separator,BOT_FUZZY_WEIGHT,sum*scale,error)) return false;
    } else if(mutation==FUZZY_RANGE) {
        if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&first,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&second,error)) return false;
        float sum=first+second,midpoint=sum*.5f;
        if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&value,error)) return false;
        float distance=value-midpoint,scaled=distance*scale;
        if(!bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MAXIMUM,midpoint+scaled,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&value,error)) return false;
        distance=value-midpoint;scaled=distance*scale;
        if(!bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MINIMUM,midpoint+scaled,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&value,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&limit,error)) return false;
        return !(value<limit) || bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MAXIMUM,limit,error);
    } else {
        bool leap=(float)(random->next(random->context)&UINT32_C(0x7fff))/32767.0f<.01;
        float unit=(float)(random->next(random->context)&UINT32_C(0x7fff))/32767.0f;
        double centered=2.0*((double)unit-.5);
        if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&first,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&second,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_WEIGHT,&value,error)) return false;
        float range=first-second;
        double delta=centered*(double)range*(leap?1.0:.5);
        if(!bot_fuzzy_separator_float_write(separator,BOT_FUZZY_WEIGHT,(float)((double)value+delta),error)) return false;
    }
    if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_WEIGHT,&value,error) ||
       !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MINIMUM,&limit,error)) return false;
    if(value<limit) return bot_fuzzy_separator_float_write(separator,
        mutation==FUZZY_EVOLVE?BOT_FUZZY_MINIMUM:BOT_FUZZY_WEIGHT,
        mutation==FUZZY_EVOLVE?value:limit,error);
    if(!bot_fuzzy_separator_float_read(separator,BOT_FUZZY_WEIGHT,&value,error) ||
       !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_MAXIMUM,&limit,error)) return false;
    return !(value>limit) || bot_fuzzy_separator_float_write(separator,
        mutation==FUZZY_EVOLVE?BOT_FUZZY_MAXIMUM:BOT_FUZZY_WEIGHT,
        mutation==FUZZY_EVOLVE?value:limit,error);
}
static bool mutate_tree(bot_fuzzy_config *config,uint32_t root,fuzzy_mutation mutation,float scale,
    const qa_bot_random_source *random,qa_error *error) {
    fuzzy_stack stack={0};bool ok=push(&stack,root,false,error);
    while(ok && stack.count) {
        fuzzy_frame *frame=&stack.frames[stack.count-1];bot_fuzzy_separator separator;uint32_t pointer;
        ok=bot_fuzzy_separator_bind(config->heap,frame->pointer,&separator,error);
        if(!ok) break;
        if(!frame->stage) {
            frame->stage=1;ok=bot_fuzzy_separator_word_read(&separator,BOT_FUZZY_CHILD,&pointer,error);
            if(ok && pointer) ok=push(&stack,pointer,false,error);
            else if(ok) ok=mutate_leaf(&separator,mutation,scale,random,error);
        } else {
            ok=bot_fuzzy_separator_word_read(&separator,BOT_FUZZY_NEXT,&pointer,error);
            if(ok && pointer) {frame->pointer=pointer;frame->stage=0;}
            else --stack.count;
        }
    }
    free(stack.frames);return ok;
}
static bool mutate_config(bot_fuzzy_config *config,int32_t selected,fuzzy_mutation mutation,float scale,
    const qa_bot_random_source *random,qa_error *error) {
    for(int32_t index=selected<0?0:selected;;++index) {
        int32_t count;uint32_t root;
        if(!bot_fuzzy_config_count(config,&count,error)) return false;
        if(index>=count) return true;
        if(!bot_fuzzy_config_pointer_read(config,index,true,&root,error)) return false;
        if(!root) return fail(error,"Fuzzy weight has no source separator");
        if(!mutate_tree(config,root,mutation,scale,random,error)) return false;
        if(selected>=0) return true;
    }
}
bool bot_fuzzy_scale(bot_fuzzy_config *config,qa_bytes name,float scale,qa_error *error) {
    if(scale<0) scale=0;else if(scale>1) scale=1;
    int32_t index;
    if(!bot_fuzzy_find(config,name,&index,error)) return false;
    return index<0 || mutate_config(config,index,FUZZY_SCALE,scale,NULL,error);
}
bool bot_fuzzy_scale_range(bot_fuzzy_config *config,float scale,qa_error *error) {
    if(scale<0) scale=0;else if(scale>100) scale=100;
    return mutate_config(config,-1,FUZZY_RANGE,scale,NULL,error);
}
bool bot_fuzzy_evolve(bot_fuzzy_config *config,const qa_bot_random_source *random,qa_error *error) {
    if(!random || !random->next) return fail(error,"Fuzzy evolution requires its genuine random source");
    return mutate_config(config,-1,FUZZY_EVOLVE,0,random,error);
}
typedef struct fuzzy_breed_frame {
    uint32_t first,second,output;
    bool after_child,first_is_second;
} fuzzy_breed_frame;
static bool breed_report(const bot_fuzzy_reporter *reporter,const char *message,qa_error *error) {
    return !reporter || !reporter->report || reporter->report(reporter->context,message,error);
}
static bool breed_tree(const bot_fuzzy_config *first,const bot_fuzzy_config *second,
    bot_fuzzy_config *output,uint32_t one,uint32_t two,uint32_t destination,
    const bot_fuzzy_reporter *reporter,qa_error *error) {
    fuzzy_breed_frame *frames=NULL;size_t count=0,capacity=0;bool ok=true;
    fuzzy_breed_frame pending={.first=one,.second=two,.output=destination};
    for(;;) {
        if(count==capacity) {
            size_t next=capacity?capacity*2:16;
            if(next<capacity || next>SIZE_MAX/sizeof(*frames)) {ok=fail(error,"Fuzzy interbreed traversal exceeds native storage");break;}
            fuzzy_breed_frame *grown=realloc(frames,next*sizeof(*frames));
            if(!grown) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source interbreed traversal");ok=false;break;}
            frames=grown;capacity=next;
        }
        frames[count++]=pending;
        while(ok && count) {
            fuzzy_breed_frame *frame=&frames[count-1];bot_fuzzy_separator a,b,c;uint32_t child;
            ok=bot_fuzzy_separator_bind(frame->first_is_second?second->heap:first->heap,frame->first,&a,error) &&
                bot_fuzzy_separator_bind(second->heap,frame->second,&b,error) &&
                bot_fuzzy_separator_bind(output->heap,frame->output,&c,error);
            if(!ok) break;
            if(!frame->after_child) {
                ok=bot_fuzzy_separator_word_read(&a,BOT_FUZZY_CHILD,&child,error);
                if(!ok) break;
                if(child) {
                    uint32_t second_child,output_child;
                    ok=bot_fuzzy_separator_word_read(&b,BOT_FUZZY_CHILD,&second_child,error) &&
                        bot_fuzzy_separator_word_read(&c,BOT_FUZZY_CHILD,&output_child,error);
                    if(!ok) break;
                    if(!second_child || !output_child) {ok=breed_report(reporter,"cannot interbreed weight configs, unequal child",error);count=0;break;}
                    /* The source deliberately uses parent2 for both nested inputs. */
                    frame->after_child=true;pending=(fuzzy_breed_frame){second_child,second_child,output_child,false,true};
                    break;
                }
                int32_t balanced;
                ok=bot_fuzzy_separator_integer_read(&a,BOT_FUZZY_BALANCED,&balanced,error);
                if(!ok) break;
                if(balanced==1) {
                    int32_t second_balanced,output_balanced;
                    ok=bot_fuzzy_separator_integer_read(&b,BOT_FUZZY_BALANCED,&second_balanced,error) &&
                        bot_fuzzy_separator_integer_read(&c,BOT_FUZZY_BALANCED,&output_balanced,error);
                    if(!ok) break;
                    if(second_balanced!=1 || output_balanced!=1) {ok=breed_report(reporter,"cannot interbreed weight configs, unequal balance",error);count=0;break;}
                    float left,right,value,limit;
                    ok=bot_fuzzy_separator_float_read(&a,BOT_FUZZY_WEIGHT,&left,error) &&
                        bot_fuzzy_separator_float_read(&b,BOT_FUZZY_WEIGHT,&right,error);
                    if(!ok) break;
                    float sum=left+right;
                    if(ok) ok=bot_fuzzy_separator_float_write(&c,BOT_FUZZY_WEIGHT,sum/2.0f,error) &&
                        bot_fuzzy_separator_float_read(&c,BOT_FUZZY_WEIGHT,&value,error) &&
                        bot_fuzzy_separator_float_read(&c,BOT_FUZZY_MAXIMUM,&limit,error);
                    if(ok && value>limit) ok=bot_fuzzy_separator_float_write(&c,BOT_FUZZY_MAXIMUM,value,error);
                    if(ok) ok=bot_fuzzy_separator_float_read(&c,BOT_FUZZY_WEIGHT,&value,error) &&
                        bot_fuzzy_separator_float_read(&c,BOT_FUZZY_MINIMUM,&limit,error);
                    if(ok && value>limit) ok=bot_fuzzy_separator_float_write(&c,BOT_FUZZY_MINIMUM,value,error);
                    if(!ok) break;
                }
            }
            uint32_t next,second_next,output_next;
            ok=bot_fuzzy_separator_word_read(&a,BOT_FUZZY_NEXT,&next,error);
            if(!ok) break;
            if(!next) {--count;continue;}
            ok=bot_fuzzy_separator_word_read(&b,BOT_FUZZY_NEXT,&second_next,error) &&
                bot_fuzzy_separator_word_read(&c,BOT_FUZZY_NEXT,&output_next,error);
            if(!ok) break;
            if(!second_next || !output_next) {ok=breed_report(reporter,"cannot interbreed weight configs, unequal next",error);count=0;break;}
            *frame=(fuzzy_breed_frame){next,second_next,output_next,false,frame->first_is_second};
        }
        if(!ok || !count) break;
    }
    free(frames);return ok;
}
bool bot_fuzzy_interbreed(const bot_fuzzy_config *first,const bot_fuzzy_config *second,
    bot_fuzzy_config *output,const bot_fuzzy_reporter *reporter,qa_error *error) {
    if(!first || !second || !output)
        return fail(error,"Fuzzy interbreed requires genuine module configurations");
    int32_t one,two,destination;
    if(!bot_fuzzy_config_count(first,&one,error) || !bot_fuzzy_config_count(second,&two,error)) return false;
    if(one!=two) return breed_report(reporter,"cannot interbreed weight configs, unequal numweights",error);
    if(!bot_fuzzy_config_count(first,&one,error) || !bot_fuzzy_config_count(output,&destination,error)) return false;
    if(one!=destination) return breed_report(reporter,"cannot interbreed weight configs, unequal numweights",error);
    for(int32_t index=0;;++index) {
        uint32_t a,b,c;
        if(!bot_fuzzy_config_count(first,&one,error)) return false;
        if(index>=one) return true;
        if(!bot_fuzzy_config_pointer_read(first,index,true,&a,error) ||
           !bot_fuzzy_config_pointer_read(second,index,true,&b,error) ||
           !bot_fuzzy_config_pointer_read(output,index,true,&c,error)) return false;
        if(!a || !b || !c) return fail(error,"Fuzzy interbreed weight has no genuine separator");
        if(!breed_tree(first,second,output,a,b,c,reporter,error)) return false;
    }
}
