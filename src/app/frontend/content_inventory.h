#ifndef QA_FRONTEND_CONTENT_INVENTORY_H
#define QA_FRONTEND_CONTENT_INVENTORY_H
#include "qa/frontend.h"
#include "qa/persistence_content.h"
/* Actual external capture visitor. The graph deduplicates installed pointers
 * and retains owners; this enumeration copies no state and runs no factory. */
bool frontend_content_visit(void *, const qa_application *, const qa_application_content_visitor *, qa_error *);
bool frontend_tools_content_visit(const qa_frontend *, const qa_application_content_visitor *, qa_error *);
const qa_vfs *frontend_native_q2_files_at(const qa_frontend *, size_t);
#endif
