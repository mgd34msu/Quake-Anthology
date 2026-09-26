#define _GNU_SOURCE
#include "qa/text.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unicode/ucasemap.h>

/* Immutable process-lifetime locale shared by parsers and decoding threads.
 * No setlocale calls: menus/localization cannot change asset number parsing. */
static once_flag numeric_once=ONCE_FLAG_INIT;
static locale_t numeric_locale;
static void open_numeric_locale(void) { numeric_locale=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0); }

bool qa_parse_number(qa_bytes input, double *out, qa_error *error) {
    if (!out || !input.size || !input.data || input.size==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid numeric token"); return false;
    }
    call_once(&numeric_once,open_numeric_locale);
    if (!numeric_locale) { qa_error_set(error,QA_ERROR_MEMORY,0,"opening numeric locale"); return false; }
    char local[128], *text=local;
    if (input.size>=sizeof(local)) {
        text=malloc(input.size+1);
        if (!text) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating numeric token"); return false; }
    }
    memcpy(text,input.data,input.size); text[input.size]=0;
    char *end;
    double value=strtod_l(text,&end,numeric_locale);
    bool valid=end!=text;
    while (*end==' ' || (*end>='\t' && *end<='\r')) ++end;
    valid=valid && (size_t)(end-text)==input.size;
    if (text!=local) free(text);
    if (!valid) { qa_error_set(error,QA_ERROR_FORMAT,0,"invalid numeric token"); return false; }
    *out=value; return true;
}

bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error) {
    if (!out || (!input.data && input.size) || input.size > INT32_MAX) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid Unicode lowercase input"); return false;
    }
    UErrorCode status=U_ZERO_ERROR;
    UCaseMap *map=ucasemap_open("",0,&status);
    if (U_FAILURE(status) || !map) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"opening Unicode case map: %s",u_errorName(status)); return false;
    }
    const char *source=input.size ? (const char *)input.data : "";
    int32_t needed=ucasemap_utf8ToLower(map,NULL,0,source,(int32_t)input.size,&status);
    if ((U_FAILURE(status) && status!=U_BUFFER_OVERFLOW_ERROR) || needed<0 || needed==INT32_MAX) {
        ucasemap_close(map);
        qa_error_set(error,QA_ERROR_FORMAT,0,"lowercasing UTF-8: %s",u_errorName(status)); return false;
    }
    uint8_t *data=malloc((size_t)needed+1);
    if (!data) { ucasemap_close(map); qa_error_set(error,QA_ERROR_MEMORY,0,"allocating lowercase text"); return false; }
    status=U_ZERO_ERROR;
    int32_t written=ucasemap_utf8ToLower(map,(char *)data,needed+1,source,(int32_t)input.size,&status);
    ucasemap_close(map);
    if (U_FAILURE(status) || written<0 || written>needed) {
        free(data); qa_error_set(error,QA_ERROR_FORMAT,0,"lowercasing UTF-8: %s",u_errorName(status)); return false;
    }
    data[written]=0;
    *out=(qa_buffer){data,(size_t)written}; return true;
}
