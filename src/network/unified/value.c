#include "value_internal.h"

#include <stdlib.h>

bool qa_unified_value_canonical(qa_bytes source, qa_buffer *out, qa_error *e)
{
    if (!out || !source.data || !source.size || source.size > 32u * 1024u * 1024u) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Canonical value requires a bounded complete JSON source"); return false;
    }
    qa_json_document *document = NULL;
    if (!qa_json_parse(source, &document, e)) return false;
    qa_unified_builder builder = {.maximum = 32u * 1024u * 1024u};
    bool ok = qa_unified_canonical(document, qa_json_root(document), &builder, 0, e);
    qa_json_destroy(document);
    if (!ok) { free(builder.data); return false; }
    *out = (qa_buffer){builder.data, builder.size}; return true;
}
