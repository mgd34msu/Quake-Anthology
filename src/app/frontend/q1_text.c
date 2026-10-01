#include "q1_text.h"
#include "qa/q1_text.h"

bool frontend_q1_classic_text(const char *text, const char *const *arguments, size_t count,
    char *out, size_t capacity, qa_error *error)
{
    return qa_q1_classic_text(text, arguments, count, out, capacity, error);
}
