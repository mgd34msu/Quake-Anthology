#include "qa/network_q1.h"
#include <string.h>

static bool punctuation(unsigned char c)
{
    return c=='{' || c=='}' || c=='(' || c==')' || c=='\'' || c==':';
}
bool qa_q1_token(const char **cursor, bool qw, char *out, size_t capacity,
                  bool *present, qa_error *e)
{
    if (!cursor || !*cursor || !out || !capacity || !present) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Quake command tokenizer input"); return false;
    }
    const unsigned char *s=(const unsigned char *)*cursor;
    for (;;) {
        while (*s && (*s<=32 || *s>=128)) ++s;
        if (s[0]!='/' || s[1]!='/') break;
        while (*s && *s!='\n') ++s;
    }
    *present=*s!=0; out[0]=0;
    if (!*present) { *cursor=(const char *)s; return true; }
    bool quoted=*s=='"';
    if (quoted) ++s;
    size_t used=0;
    if (!quoted && !qw && punctuation(*s)) {
        if (capacity<2) { qa_error_set(e,QA_ERROR_FORMAT,0,"Command token exceeds capacity"); return false; }
        out[used++]=(char)*s++;
    } else {
        while (*s && (quoted ? *s!='"' : (*s>32 && *s<128 && (qw || !punctuation(*s))))) {
            if (used+1>=capacity) { qa_error_set(e,QA_ERROR_FORMAT,used,"Command token exceeds capacity"); return false; }
            out[used++]=(char)*s++;
        }
        if (quoted) {
            if (*s!='"') { qa_error_set(e,QA_ERROR_FORMAT,used,"Unterminated quoted command token"); return false; }
            ++s;
        }
    }
    out[used]=0; *cursor=(const char *)s; return true;
}
