#ifndef QA_NETWORK_SERVICE_SAVE_FIELDS_H
#define QA_NETWORK_SERVICE_SAVE_FIELDS_H
#include "q3/save_fields.h"

static inline bool service_save_protocol(qa_net_writer *w, qa_net_protocol_id v)
{
    return qa_net_protocol_valid(v, w->error) && qa_net_write_u32(w, v.kind) &&
        qa_net_write_u32(w, v.revision) && qa_net_write_u32(w, v.flags);
}
static inline bool service_restore_protocol(qa_net_reader *r, qa_net_protocol_id *v)
{
    v->kind = (qa_net_protocol)qa_net_read_u32(r); v->revision = qa_net_read_u32(r);
    v->flags = qa_net_read_u32(r);
    return !r->failed && qa_net_protocol_valid(*v, r->error);
}
static inline bool service_save_text(qa_net_writer *w, const char *v, size_t capacity)
{
    return v && memchr(v, 0, capacity) ? qa_net_write_string(w, v) :
        qa_net_writer_fail(w, "Unterminated network service text");
}
static inline bool service_restore_text(qa_net_reader *r, char **v, size_t maximum)
{
    char *owned = calloc(maximum + 1, 1);
    if (!owned) {
        qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Restoring network service text"); r->failed = true; return false;
    }
    *v = owned;
    return qa_net_read_string(r, owned, maximum + 1) && *owned;
}
static inline bool service_address_valid(const qa_net_address *v)
{
    char text[256]; return qa_net_address_format(v, text, sizeof(text), NULL);
}
#endif
