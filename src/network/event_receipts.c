#include "event_receipts.h"

void qa_event_receipts_reset(qa_event_receipts *receipts, uint64_t first)
{
    *receipts = (qa_event_receipts){.through = first};
}
size_t qa_event_receipts_count(const qa_event_receipts *receipts)
{ return receipts ? receipts->count : 0; }
bool qa_event_receipts_full(const qa_event_receipts *receipts)
{ return receipts && receipts->count == QA_EVENT_RECEIPTS_CAPACITY; }
bool qa_event_receipts_submit(qa_event_receipts *receipts, uint64_t first,
    uint64_t after, uint64_t token)
{
    if (after <= first) return true;
    if (token) {
        qa_event_receipt *last = receipts->count ? receipts->spans +
            (receipts->head + receipts->count - 1) % QA_EVENT_RECEIPTS_CAPACITY : NULL;
        if (last && last->token == token) {
            if (after > last->after) last->after = after;
        } else {
            if (qa_event_receipts_full(receipts)) return false;
            size_t slot = (receipts->head + receipts->count) % QA_EVENT_RECEIPTS_CAPACITY;
            receipts->spans[slot] = (qa_event_receipt){first, after, token};
            ++receipts->count;
        }
    }
    if (after > receipts->through) receipts->through = after;
    return true;
}
uint64_t qa_event_receipts_retired(qa_event_receipts *receipts, uint64_t acknowledged)
{
    while (receipts->count) {
        const qa_event_receipt *first = receipts->spans + receipts->head;
        if (first->token > acknowledged) return first->first;
        receipts->head = (receipts->head + 1) % QA_EVENT_RECEIPTS_CAPACITY;
        --receipts->count;
    }
    return receipts->through;
}
