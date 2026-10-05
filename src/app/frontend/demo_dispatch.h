#ifndef QA_FRONTEND_DEMO_DISPATCH_H
#define QA_FRONTEND_DEMO_DISPATCH_H
#include "qa/frontend.h"
#include "demo_service.h"
typedef struct frontend_demo_dispatch frontend_demo_dispatch;
bool frontend_demo_dispatch_create(qa_frontend *,frontend_demo_dispatch **,qa_error *);
frontend_demo_service *frontend_demo_dispatch_service(frontend_demo_dispatch *,uint32_t physical_seat);
bool frontend_demo_dispatch_register(frontend_demo_dispatch *,qa_console *,uint64_t dispatch_owner,uint64_t lifetime_owner,qa_error *);
bool frontend_demo_dispatch_unregister(frontend_demo_dispatch *,qa_console *,uint64_t dispatch_owner,uint64_t lifetime_owner,qa_error *);
bool frontend_demo_dispatch_command(frontend_demo_dispatch *,const qa_command_invocation *,bool *handled,qa_error *);
/* Actual manual launch/adoption cancels only the retained attract policy. */
void frontend_demo_dispatch_manual_game(frontend_demo_dispatch *);
bool frontend_demo_dispatch_stage(frontend_demo_dispatch *,const frontend_demo_request *,qa_error *);
bool frontend_demo_dispatch_execute(frontend_demo_dispatch *,qa_error *);
bool frontend_demo_dispatch_advance(frontend_demo_dispatch *,uint64_t elapsed_ns,uint64_t frame,qa_error *);
bool frontend_demo_dispatch_publish(frontend_demo_dispatch *,qa_error *);
bool frontend_demo_dispatch_sources_returned(frontend_demo_dispatch *,qa_error *);
bool frontend_demo_dispatch_capture_ready(const frontend_demo_dispatch *,qa_error *);
bool frontend_demo_dispatch_stop(frontend_demo_dispatch *,qa_error *);
bool frontend_demo_dispatch_destroy(frontend_demo_dispatch **,qa_error *);
#endif
