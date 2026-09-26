#include "qa/network_q2.h"
#include <stdlib.h>
#include <zlib.h>
bool qa_q2_zpacket_read(qa_net_reader *r,qa_buffer *out) {
    uint16_t compressed=qa_net_read_u16(r),expected=qa_net_read_u16(r);
    qa_bytes bytes;
    if(!out)return qa_net_reader_fail(r,"Missing Q2 zpacket output");
    if(!qa_net_read_bytes(r,compressed,&bytes))return false;
    uint8_t *data=malloc(expected?expected:1);
    if(!data)return qa_net_reader_fail(r,"Q2 zpacket allocation failed");
    z_stream z= {
        0
    };
    z.next_in=(Bytef*)bytes.data;
    z.avail_in=compressed;
    z.next_out=data;
    z.avail_out=expected?expected:1;
    int status=inflateInit2(&z,-MAX_WBITS);
    if(status!=Z_OK) {
        free(data);
        return qa_net_reader_fail(r,"Q2 zpacket inflater initialization failed");
    }
    status=inflate(&z,Z_FINISH);
    bool valid=status==Z_STREAM_END&&z.total_out==expected&&z.total_in==compressed;
    inflateEnd(&z);
    if(!valid) {
        free(data);
        return qa_net_reader_fail(r,"Malformed Q2 zpacket");
    }
    out->data=data;
    out->size=expected;
    return true;
}
bool qa_q2_zpacket_wrap(qa_bytes bytes,size_t limit,uint8_t opcode,qa_buffer*out,bool*wrapped,qa_error*error) {
    if(!out||!wrapped||(bytes.size&&!bytes.data)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 zpacket input");
        return false;
    }
    *wrapped=false;
    if(bytes.size<21||bytes.size>65535||limit<5||bytes.data[0]==12)return true;
    size_t capacity=compressBound((uLong)bytes.size)+5;
    uint8_t *data=malloc(capacity);
    if(!data) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Q2 zpacket allocation failed");
        return false;
    }
    z_stream z= {
        0
    };
    z.next_in=(Bytef*)bytes.data;
    z.avail_in=(uInt)bytes.size;
    z.next_out=data+5;
    z.avail_out=(uInt)(capacity-5);
    int status=deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY);
    if(status!=Z_OK) {
        free(data);
        qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 deflater initialization failed");
        return false;
    }
    status=deflate(&z,Z_FINISH);
    size_t size=(size_t)z.total_out;
    deflateEnd(&z);
    if(status!=Z_STREAM_END) {
        free(data);
        qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 deflate failed");
        return false;
    }
    if(size>65535||size+5>=bytes.size||size+5>limit) {
        free(data);
        return true;
    }
    data[0]=opcode;
    data[1]=(uint8_t)size;
    data[2]=(uint8_t)(size>>8);
    data[3]=(uint8_t)bytes.size;
    data[4]=(uint8_t)(bytes.size>>8);
    out->data=data;
    out->size=size+5;
    *wrapped=true;
    return true;
}
