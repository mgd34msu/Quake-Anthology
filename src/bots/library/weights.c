#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_operations.h"
#include "source_fuzzy_view.h"
#include "source_fuzzy_standalone.h"
#include "qa/bot_assets_save.h"

void bot_weights_view(qa_bot_weights *w) {
    const bot_weight_topology *t = w->topology;
    w->view = (qa_bot_weights_view){t->path,
                                    t->weights,
                                    t->nodes,
                                    w->values,
                                    t->weight_count,
                                    t->node_count,
                                    t->maximum_inventory_index};
}
void bot_weight_topology_release(bot_weight_topology *t) {
    if (t != NULL && atomic_fetch_sub_explicit(&t->references, 1, memory_order_acq_rel) == 1) {
        qa_arena_destroy(&t->arena);
        free(t->nodes);
        free(t);
    }
}
void qa_bot_weights_retain(qa_bot_weights *w) {
    if (w != NULL)
        atomic_fetch_add_explicit(&w->references, 1, memory_order_relaxed);
}
void qa_bot_weights_release(qa_bot_weights *w) {
    if (w != NULL && atomic_fetch_sub_explicit(&w->references, 1, memory_order_acq_rel) == 1) {
        bot_weight_topology_release(w->topology);
        bot_fuzzy_owned_release(w->source);
        if(w->standalone_heap) {
            if(w->standalone_heap->standalone_users>1) --w->standalone_heap->standalone_users;
            else {(void)bot_fuzzy_heap_clear(w->standalone_heap,NULL);free(w->standalone_heap);}
        }
        free(w->values);
        free(w);
    }
}
bool qa_bot_weights_free(qa_bot_weights *weights,qa_error *error) {
    if(!weights || !weights->source) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source weight free requires its actual configuration");return false;
    }
    bot_fuzzy_owned *config=weights->source;
    if(config->store) return bot_fuzzy_store_free(config->store,config,error);
    if(!bot_fuzzy_owned_open(config,error) || !bot_fuzzy_config_free(&config->source,error)) return false;
    config->disposed=true;return true;
}
const qa_bot_weights_view *qa_bot_weights_read(const qa_bot_weights *w) {
    if(w && w->source && !bot_weights_source_view((qa_bot_weights *)w,NULL)) return NULL;
    return w == NULL ? NULL : &w->view;
}
bool qa_bot_weights_find_value(const qa_bot_weights *w,const char *name,int32_t *out,qa_error *error) {
    if(!w || !w->source || !name || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing fuzzy source lookup input/output");return false;}
    return bot_fuzzy_owned_open(w->source,error) &&
        bot_fuzzy_find(&w->source->source,(qa_bytes){(const uint8_t *)name,strlen(name)},out,error);
}
int32_t qa_bot_weights_find(const qa_bot_weights *w, const char *name) {
    int32_t index=-1;(void)qa_bot_weights_find_value(w,name,&index,NULL);return index;
}
bool qa_bot_weights_load_result(qa_bot_library *library, const char *path, qa_bot_weights **out,
                         bool *source_failure,qa_error *e) {
    if(source_failure) *source_failure=false;
    if (library == NULL || path == NULL || out == NULL || !source_failure) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy weight resource path/output");
        return false;
    }
    bot_fuzzy_owned *source=NULL;
    if(!bot_fuzzy_store_load_result(library->fuzzy_store,path,&source,source_failure,e)) return false;
    for(qa_bot_weights *w=library->weights;w;w=w->next) if(w->source==source) {
        bot_fuzzy_owned_release(source);qa_bot_weights_retain(w);*out=w;return true;
    }
    qa_bot_weights *w=calloc(1,sizeof(*w));
    if(!w) {bot_fuzzy_owned_release(source);qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native source weight view");return false;}
    atomic_init(&w->references,1);w->source=source;
    if(!bot_weights_source_view(w,e)) {qa_bot_weights_release(w);return false;}
    qa_bot_weights_retain(w);qa_bot_weights **tail=&library->weights;
    while(*tail) tail=&(*tail)->next;
    *tail=w;*out=w;return true;
}
bool qa_bot_weights_load(qa_bot_library *library,const char *path,qa_bot_weights **out,qa_error *error) {
    bool source_failure;return qa_bot_weights_load_result(library,path,out,&source_failure,error);
}
bool qa_bot_weights_clone(const qa_bot_weights *source, qa_bot_weights **out, qa_error *e) {
    if (source == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing fuzzy weight clone source/output");
        return false;
    }
    qa_buffer encoded={0};
    bool ok=qa_bot_weights_save_capture(source,&encoded,e) &&
        qa_bot_weights_save_restore((qa_bytes){encoded.data,encoded.size},out,e);
    qa_buffer_free(&encoded);return ok;
}
bool qa_bot_weights_restore(const qa_bot_weights_view *source, qa_bot_weights **out, qa_error *e) {
    if (source == NULL || out == NULL || source->path == NULL || source->weight_count > 128 ||
        source->node_count >= UINT32_MAX ||
        (source->weight_count != 0 && source->weights == NULL) ||
        (source->node_count != 0 && (source->nodes == NULL || source->values == NULL)))
        goto invalid;
    uint32_t previous = 0;
    for (size_t i = 0; i < source->weight_count; ++i) {
        const qa_bot_weight_definition *d = source->weights + i;
        if (d->name == NULL || d->root != previous || d->root >= d->end ||
            d->end > source->node_count)
            goto invalid;
        for (uint32_t j = d->root; j < d->end; ++j) {
            const qa_bot_weight_node *n = source->nodes + j;
            if ((n->child != QA_BOT_NO_INDEX && (n->child <= j || n->child >= d->end)) ||
                (n->next != QA_BOT_NO_INDEX && (n->next <= j || n->next >= d->end)))
                goto invalid;
        }
        previous = d->end;
    }
    if (previous != source->node_count)
        goto invalid;
    uint8_t *incoming = calloc(source->node_count == 0 ? 1 : source->node_count, 1);
    if (incoming == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Validating fuzzy weight topology");
        return false;
    }
    bool tree = true;
    for (size_t i = 0; i < source->weight_count && tree; ++i) {
        const qa_bot_weight_definition *d = source->weights + i;
        for (uint32_t j = d->root; j < d->end; ++j) {
            const qa_bot_weight_node *n = source->nodes + j;
            if (j != d->root && incoming[j] != 1) {
                tree = false;
                break;
            }
            if (n->child != QA_BOT_NO_INDEX && ++incoming[n->child] != 1) {
                tree = false;
                break;
            }
            if (n->next != QA_BOT_NO_INDEX && ++incoming[n->next] != 1) {
                tree = false;
                break;
            }
        }
    }
    free(incoming);
    if (!tree)
        goto invalid;
    return bot_weights_standalone_create(source,out,e);
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid fuzzy weight checkpoint topology/value");
    return false;
}
bool qa_bot_weight_workspace_create(qa_bot_weight_workspace **out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing fuzzy workspace output");
        return false;
    }
    qa_bot_weight_workspace *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating fuzzy workspace");
        return false;
    }
    *out = w;
    return true;
}
void qa_bot_weight_workspace_destroy(qa_bot_weight_workspace *w) {
    if(w && !w->busy) {
        free(w->traversal.frames);
        free(w);
    }
}
bool qa_bot_weights_evaluate_view(const qa_bot_weights *w, uint32_t weight,
                                  const qa_bot_inventory_view *inventory,
                                  const qa_bot_random_source *random, qa_bot_weight_workspace *work,
                                  float *out, qa_error *e) {
    if (w == NULL || !w->source || weight>INT32_MAX || work == NULL || work->busy || out == NULL ||
        inventory == NULL ||
        (inventory->read == NULL && inventory->count != 0 && inventory->data == NULL) ||
        (random != NULL && random->next == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, weight, "Invalid or nested fuzzy evaluation request");
        return false;
    }
    qa_bot_weights *retained = (qa_bot_weights *)w;
    qa_bot_weights_retain(retained);
    work->busy = true;
    bool ok = bot_fuzzy_owned_open(w->source,e) &&
        bot_fuzzy_evaluate(&w->source->source,(int32_t)weight,inventory,random,&work->traversal,out,e);
    work->busy = false;
    qa_bot_weights_release(retained);
    return ok;
}
bool qa_bot_weights_evaluate(const qa_bot_weights *w, uint32_t weight, const int32_t *inventory,
                             size_t count, const qa_bot_random_source *random,
                             qa_bot_weight_workspace *work, float *out, qa_error *e) {
    qa_bot_inventory_view view = {.data = inventory, .count = count};
    return qa_bot_weights_evaluate_view(w, weight, &view, random, work, out, e);
}
