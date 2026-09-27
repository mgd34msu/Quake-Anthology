#include "internal.h"

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
        free(w->values);
        free(w);
    }
}
const qa_bot_weights_view *qa_bot_weights_read(const qa_bot_weights *w) {
    return w == NULL ? NULL : &w->view;
}
int32_t qa_bot_weights_find(const qa_bot_weights *w, const char *name) {
    if (w == NULL || name == NULL)
        return -1;
    for (size_t i = 0; i < w->view.weight_count; ++i)
        if (strcmp(w->view.weights[i].name, name) == 0)
            return (int32_t)i;
    return -1;
}
bool qa_bot_weights_load(qa_bot_library *library, const char *path, qa_bot_weights **out,
                         qa_error *e) {
    if (library == NULL || path == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy weight resource path/output");
        return false;
    }
    if (!bot_reload_characters(library)) {
        for (qa_bot_weights *w = library->weights; w != NULL; w = w->next)
            if (strcmp(w->view.path, path) == 0) {
                qa_bot_weights_retain(w);
                *out = w;
                return true;
            }
    }
    qa_bot_weights *w;
    if (!bot_weights_parse(library, path, &w, e))
        return false;
    if (!bot_reload_characters(library)) {
        qa_bot_weights_retain(w);
        w->next = library->weights;
        library->weights = w;
    }
    *out = w;
    return true;
}
bool qa_bot_weights_clone(const qa_bot_weights *source, qa_bot_weights **out, qa_error *e) {
    if (source == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing fuzzy weight clone source/output");
        return false;
    }
    qa_bot_weights *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating fuzzy weight clone");
        return false;
    }
    atomic_init(&w->references, 1);
    w->topology = source->topology;
    atomic_fetch_add_explicit(&w->topology->references, 1, memory_order_relaxed);
    if (!bot_grow((void **)&w->values, &w->value_capacity, source->view.node_count,
                  sizeof(*w->values), e)) {
        qa_bot_weights_release(w);
        return false;
    }
    if (source->view.node_count != 0)
        memcpy(w->values, source->values, source->view.node_count * sizeof(*w->values));
    bot_weights_view(w);
    *out = w;
    return true;
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
    qa_bot_weights *w = calloc(1, sizeof(*w));
    bot_weight_topology *t = calloc(1, sizeof(*t));
    if (w == NULL || t == NULL) {
        free(w);
        free(t);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restored fuzzy weights");
        return false;
    }
    atomic_init(&w->references, 1);
    atomic_init(&t->references, 1);
    w->topology = t;
    t->maximum_inventory_index = -1;
    t->path =
        bot_string(&t->arena, (qa_bytes){(const uint8_t *)source->path, strlen(source->path)}, e);
    if (t->path == NULL ||
        !bot_grow((void **)&t->nodes, &t->node_capacity, source->node_count, sizeof(*t->nodes),
                  e) ||
        !bot_grow((void **)&w->values, &w->value_capacity, source->node_count, sizeof(*w->values),
                  e))
        goto fail;
    for (size_t i = 0; i < source->weight_count; ++i) {
        t->weights[i] = source->weights[i];
        const char *name = source->weights[i].name;
        t->weights[i].name =
            bot_string(&t->arena, (qa_bytes){(const uint8_t *)name, strlen(name)}, e);
        if (t->weights[i].name == NULL)
            goto fail;
    }
    if (source->node_count != 0) {
        memcpy(t->nodes, source->nodes, source->node_count * sizeof(*t->nodes));
        memcpy(w->values, source->values, source->node_count * sizeof(*w->values));
    }
    t->node_count = source->node_count;
    t->weight_count = source->weight_count;
    for (size_t i = 0; i < t->node_count; ++i)
        if (t->nodes[i].inventory > t->maximum_inventory_index)
            t->maximum_inventory_index = t->nodes[i].inventory;
    bot_weights_view(w);
    *out = w;
    return true;
fail:
    qa_bot_weights_release(w);
    return false;
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
    if (w != NULL) {
        free(w->frames);
        free(w);
    }
}
static bool leaf(const qa_bot_weights *w, uint32_t node, bool undecided,
                 const qa_bot_random_source *random, qa_bot_weight_workspace *work, float *last,
                 qa_error *e) {
    uint32_t child = w->view.nodes[node].child;
    if (child != QA_BOT_NO_INDEX) {
        if (!bot_grow((void **)&work->frames, &work->capacity, work->count + 1,
                      sizeof(*work->frames), e))
            return false;
        work->frames[work->count++] = (bot_weight_frame){.root = child, .undecided = undecided};
    } else {
        const qa_bot_weight_value *v = w->values + node;
        *last = undecided ? v->minimum + bot_random(random) * (v->maximum - v->minimum) : v->weight;
    }
    return true;
}
static int32_t signed_bits(uint32_t value) {
    int32_t out;
    memcpy(&out, &value, sizeof(out));
    return out;
}
bool qa_bot_weights_evaluate(const qa_bot_weights *w, uint32_t weight, const int32_t *inventory,
                             size_t inventory_count, const qa_bot_random_source *random,
                             qa_bot_weight_workspace *work, float *out, qa_error *e) {
    if (w == NULL || weight >= w->view.weight_count || work == NULL || out == NULL ||
        (inventory_count != 0 && inventory == NULL) || (random != NULL && random->next == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, weight, "Invalid fuzzy evaluation request");
        return false;
    }
    if (!bot_grow((void **)&work->frames, &work->capacity, 1, sizeof(*work->frames), e))
        return false;
    work->count = 1;
    work->frames[0] =
        (bot_weight_frame){.root = w->view.weights[weight].root, .undecided = random != NULL};
    float last = 0;
    while (work->count != 0) {
        bot_weight_frame *f = work->frames + work->count - 1;
        if (f->stage == 1) {
            --work->count;
            continue;
        }
        if (f->stage == 2) {
            f->left = last;
            f->stage = 3;
            uint32_t right = f->right;
            bool undecided = f->undecided && w->view.nodes[right].child == QA_BOT_NO_INDEX;
            if (!leaf(w, right, undecided, random, work, &last, e))
                return false;
            continue;
        }
        if (f->stage == 3) {
            last = f->scale * f->left + (1 - f->scale) * last;
            --work->count;
            continue;
        }
        uint32_t index = f->root;
        for (;;) {
            const qa_bot_weight_node *n = w->view.nodes + index;
            if (n->inventory < 0 || (size_t)n->inventory >= inventory_count) {
                qa_error_set(e, QA_ERROR_ARGUMENT, index,
                             "Fuzzy inventory index is outside observation");
                return false;
            }
            int32_t v = inventory[n->inventory];
            if (v < n->threshold) {
                f->stage = 1;
                if (!leaf(w, index, f->undecided, random, work, &last, e))
                    return false;
                break;
            }
            if (n->next == QA_BOT_NO_INDEX) {
                last = w->values[index].weight;
                --work->count;
                break;
            }
            const qa_bot_weight_node *next = w->view.nodes + n->next;
            if (v < next->threshold) {
                int32_t numerator = signed_bits((uint32_t)v - (uint32_t)n->threshold),
                        denominator =
                            signed_bits((uint32_t)next->threshold - (uint32_t)n->threshold);
                if (denominator == 0) {
                    qa_error_set(e, QA_ERROR_FORMAT, index,
                                 "Fuzzy interpolation thresholds divide by zero");
                    return false;
                }
                f->scale = (float)((int64_t)numerator / (int64_t)denominator);
                f->right = n->next;
                f->stage = 2;
                if (!leaf(w, index, f->undecided, random, work, &last, e))
                    return false;
                break;
            }
            index = n->next;
        }
    }
    *out = last;
    return true;
}
