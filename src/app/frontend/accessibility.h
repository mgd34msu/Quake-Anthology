#ifndef QA_FRONTEND_ACCESSIBILITY_H
#define QA_FRONTEND_ACCESSIBILITY_H
#include "internal.h"
#include "qa/ui_preferences.h"
enum { FRONTEND_ACCESSIBILITY = 100 };
bool frontend_accessibility_create(frontend_seat *, qa_error *);
bool frontend_accessibility_sync(qa_frontend *, qa_error *);
#endif
