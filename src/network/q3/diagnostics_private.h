#ifndef QA_Q3_DIAGNOSTICS_PRIVATE_H
#define QA_Q3_DIAGNOSTICS_PRIVATE_H
#include "qa/network_q3.h"
#include <stdarg.h>
#include <stdio.h>

static inline bool q3_diagnostic_active(const qa_q3_reader *reader)
{ return reader->diagnostics.print && reader->diagnostics.level; }
static inline size_t q3_diagnostic_read_count(const qa_q3_reader *reader)
{ return reader->diagnostics.offset + reader->raw.bit / 8 + (reader->oob ? 0 : 1); }
static inline void q3_diagnostic_add(char *text,size_t capacity,size_t *used,const char *format,...)
{
    if (*used>=capacity-1) return;
    va_list arguments;
    va_start(arguments,format);
    int size=vsnprintf(text+*used,capacity-*used,format,arguments);
    va_end(arguments);
    if (size>0) *used+=(size_t)size<capacity-*used?(size_t)size:capacity-*used-1;
}
static inline void q3_diagnostic_print(const qa_q3_reader *reader,const char *format,...)
{
    char text[256];
    va_list arguments;
    va_start(arguments,format);
    vsnprintf(text,sizeof(text),format,arguments);
    va_end(arguments);
    reader->diagnostics.print(reader->diagnostics.context,text);
}
#endif
