#include "value_internal.h"
#include "frame_internal.h"
#include "qa/text.h"

#include <stdlib.h>
#include <string.h>

void qa_unified_inputs_free(qa_unified_input_batch *batch)
{
    if (!batch) return;
    for (size_t i = 0; i < 64; ++i) {
        qa_buffer_free(batch->providers + i);
        qa_buffer_free(batch->weapons + i);
    }
    *batch = (qa_unified_input_batch){0};
}
bool qa_unified_inputs_check(qa_unified_input_batch *batch, size_t *bytes, qa_error *error)
{
    if (!batch || !batch->epoch || batch->count > 64 ||
        !qa_unified_record_measure(&qa_unified_inputs_layout, batch, bytes, error)) return false;
    for (size_t i = 0; i < batch->count; ++i) {
        qa_unified_input *input = batch->commands + i;
        if (input->sequence > QA_UNIFIED_SAFE_INTEGER || (i && input->sequence <= batch->commands[i - 1].sequence)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified input sequence is outside its actual ordered Source domain"); return false;
        }
        qa_buffer provider = batch->providers[i], weapon = batch->weapons[i];
        if ((!input->has_arsenal && (provider.size || weapon.size)) ||
            (input->has_arsenal && (!provider.size || memchr(provider.data, 0, provider.size) ||
                memchr(weapon.data, 0, weapon.size) || !qa_utf8_valid((qa_bytes){provider.data, provider.size}) ||
                !qa_utf8_valid((qa_bytes){weapon.data, weapon.size})))) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Unified arsenal input lost its actual UTF-8 provider selection"); return false;
        }
        input->arsenal.provider = (qa_bytes){provider.data, provider.size};
        input->arsenal.weapon = (qa_bytes){weapon.data, weapon.size};
    }
    return true;
}
bool qa_unified_inputs_read(const qa_unified_document *document, qa_unified_input_batch *out, qa_error *error)
{
    const qa_unified_input_batch *source = qa_unified_document_inputs(document);
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unified input read requires its actual typed batch"); return false;
    }
    qa_unified_input_batch copy = {0}; size_t bytes;
    if (!qa_unified_record_clone(&qa_unified_inputs_layout, source, &copy, error) ||
        !qa_unified_inputs_check(&copy, &bytes, error)) { qa_unified_inputs_free(&copy); return false; }
    for (size_t i = 0; i < copy.count; ++i) {
        copy.provider_capacity[i] = copy.providers[i].size;
        copy.weapon_capacity[i] = copy.weapons[i].size;
    }
    *out = copy; return true;
}
static bool copy_bytes(qa_bytes source, qa_buffer *out, qa_error *error)
{
    if (!source.size) return true;
    if (!source.data || source.size > 8192) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unified input text exceeds its actual provider extent"); return false;
    }
    out->data = malloc(source.size);
    if (!out->data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying actual Unified input selection"); return false; }
    memcpy(out->data, source.data, source.size); out->size = source.size; return true;
}
bool qa_unified_inputs_copy(uint32_t epoch, const qa_unified_input *inputs, size_t count,
    qa_unified_input_batch *out, qa_error *error)
{
    if (!out || !epoch || count > 64 || (count && !inputs)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid actual Unified input producer"); return false;
    }
    qa_unified_input_batch batch={.epoch=epoch,.count=count};
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        batch.commands[i] = inputs[i];
        if (inputs[i].has_arsenal) okay = copy_bytes(inputs[i].arsenal.provider, batch.providers + i, error) &&
            copy_bytes(inputs[i].arsenal.weapon, batch.weapons + i, error);
        batch.provider_capacity[i] = batch.providers[i].size;
        batch.weapon_capacity[i] = batch.weapons[i].size;
    }
    size_t measured;
    if (okay) okay=qa_unified_inputs_check(&batch,&measured,error);
    if (okay) *out=batch; else qa_unified_inputs_free(&batch);
    return okay;
}
bool qa_unified_inputs_document(uint32_t epoch, const qa_unified_input *inputs, size_t count,
    qa_unified_document **out, qa_error *error)
{
    if (!out || *out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Unified input document output"); return false; }
    qa_unified_input_batch *batch=calloc(1,sizeof(*batch));
    if (!batch) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual Unified input batch"); return false; }
    bool okay=qa_unified_inputs_copy(epoch,inputs,count,batch,error);
    if (okay) okay = qa_unified_document_create_inputs(&batch, out, error);
    if (batch) { qa_unified_inputs_free(batch); free(batch); }
    return okay;
}
