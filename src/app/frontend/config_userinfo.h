#ifndef QA_FRONTEND_CONFIG_USERINFO_H
#define QA_FRONTEND_CONFIG_USERINFO_H
#include "qa/console.h"
/* Bootstrap identity declarations precede source configuration. The actual
 * module later promotes these same physical records in its client registry. */
bool frontend_config_userinfo_register(qa_cvars *,uint32_t authored_seat,const char *model,qa_error *);
#endif
