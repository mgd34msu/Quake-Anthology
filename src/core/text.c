/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/text.h"

#include <stdlib.h>
#include <unicode/ucasemap.h>

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
