#include "internal.h"

bool qa_bot_weights_scale(qa_bot_weights *w, const char *name, float scale, qa_error *e) {
    if (w == NULL || name == NULL || !isfinite(scale)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy scale request");
        return false;
    }
    int32_t index = qa_bot_weights_find(w, name);
    if (index < 0)
        return true;
    scale = fmaxf(0, fminf(1, scale));
    const qa_bot_weight_definition *d = w->view.weights + index;
    for (uint32_t i = d->root; i < d->end; ++i) {
        const qa_bot_weight_node *n = w->view.nodes + i;
        if (n->child != QA_BOT_NO_INDEX || !n->balanced)
            continue;
        qa_bot_weight_value *v = w->values + i;
        v->weight = (v->minimum + v->maximum) * scale;
        if (v->weight < v->minimum)
            v->weight = v->minimum;
        else if (v->weight > v->maximum)
            v->weight = v->maximum;
    }
    return true;
}
bool qa_bot_weights_scale_range(qa_bot_weights *w, float scale, qa_error *e) {
    if (w == NULL || !isfinite(scale)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy range scale");
        return false;
    }
    scale = fmaxf(0, fminf(100, scale));
    for (size_t i = 0; i < w->view.node_count; ++i) {
        const qa_bot_weight_node *n = w->view.nodes + i;
        if (n->child != QA_BOT_NO_INDEX || !n->balanced)
            continue;
        qa_bot_weight_value *v = w->values + i;
        float middle = (v->minimum + v->maximum) * 0.5f;
        v->maximum = middle + (v->maximum - middle) * scale;
        v->minimum = middle + (v->minimum - middle) * scale;
        if (v->maximum < v->minimum)
            v->maximum = v->minimum;
    }
    return true;
}
bool qa_bot_weights_evolve(qa_bot_weights *w, const qa_bot_random_source *random, qa_error *e) {
    if (w == NULL || random == NULL || random->next == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy evolution source");
        return false;
    }
    for (size_t i = 0; i < w->view.node_count; ++i) {
        const qa_bot_weight_node *n = w->view.nodes + i;
        if (n->child != QA_BOT_NO_INDEX || !n->balanced)
            continue;
        qa_bot_weight_value *v = w->values + i;
        bool leap = bot_random(random) < 0.01f;
        double centered = 2 * ((double)bot_random(random) - 0.5);
        float range = v->maximum - v->minimum;
        v->weight = (float)(v->weight + centered * range * (leap ? 1 : 0.5));
        if (v->weight < v->minimum)
            v->minimum = v->weight;
        else if (v->weight > v->maximum)
            v->maximum = v->weight;
    }
    return true;
}
typedef struct breed_frame {
    uint32_t first, second, output;
    unsigned stage;
    bool second_only;
} breed_frame;
bool qa_bot_weights_interbreed(qa_bot_weights *out, const qa_bot_weights *parent1,
                               const qa_bot_weights *parent2, bool *matched, qa_error *e) {
    if (out == NULL || parent1 == NULL || parent2 == NULL || matched == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid fuzzy interbreed request");
        return false;
    }
    *matched = false;
    if (parent1->view.weight_count != parent2->view.weight_count ||
        parent1->view.weight_count != out->view.weight_count)
        return true;
    breed_frame *stack = NULL;
    size_t capacity = 0;
    bool all = true;
    for (size_t weight = 0; weight < out->view.weight_count; ++weight) {
        if (!bot_grow((void **)&stack, &capacity, 1, sizeof(*stack), e)) {
            free(stack);
            return false;
        }
        size_t count = 1;
        stack[0] =
            (breed_frame){parent1->view.weights[weight].root, parent2->view.weights[weight].root,
                          out->view.weights[weight].root, 0, false};
        while (count != 0) {
            breed_frame *f = stack + count - 1;
            const qa_bot_weights *first = f->second_only ? parent2 : parent1;
            const qa_bot_weight_node *a = first->view.nodes + f->first,
                                     *b = parent2->view.nodes + f->second,
                                     *c = out->view.nodes + f->output;
            if (f->stage == 0) {
                f->stage = 1;
                if (a->child != QA_BOT_NO_INDEX) {
                    if (b->child == QA_BOT_NO_INDEX || c->child == QA_BOT_NO_INDEX) {
                        all = false;
                        break;
                    }
                    breed_frame child = {b->child, b->child, c->child, 0, true};
                    if (!bot_grow((void **)&stack, &capacity, count + 1, sizeof(*stack), e)) {
                        free(stack);
                        return false;
                    }
                    stack[count++] = child;
                    continue;
                }
                if (a->balanced) {
                    if (!b->balanced || !c->balanced) {
                        all = false;
                        break;
                    }
                    qa_bot_weight_value *value = out->values + f->output;
                    value->weight =
                        (first->values[f->first].weight + parent2->values[f->second].weight) * 0.5f;
                    if (value->weight > value->maximum)
                        value->maximum = value->weight;
                    if (value->weight > value->minimum)
                        value->minimum = value->weight;
                }
            }
            if (a->next == QA_BOT_NO_INDEX) {
                --count;
                continue;
            }
            if (b->next == QA_BOT_NO_INDEX || c->next == QA_BOT_NO_INDEX) {
                all = false;
                break;
            }
            f->first = a->next;
            f->second = b->next;
            f->output = c->next;
            f->stage = 0;
        }
    }
    free(stack);
    *matched = all;
    return true;
}
