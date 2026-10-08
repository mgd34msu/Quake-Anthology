#include "socket_events.h"

bool qa_net_socket_read(qa_socket socket,uint8_t *bytes,size_t capacity,
    size_t *size,bool *eof,qa_error *error)
{
    *size=0; *eof=false;
#if defined(_WIN32)
    int count=recv(socket,(char *)bytes,(int)capacity,0);
#else
    ssize_t count=recv(socket,bytes,capacity,0);
#endif
    if(count<0) {
        int code=qa_socket_error();
        if(qa_socket_again(code) || qa_socket_interrupted(code)) return true;
        qa_error_set(error,QA_ERROR_IO,0,"Reading transport control socket failed");
        return false;
    }
    *eof=count==0;
    *size=(size_t)count;
    return true;
}
bool qa_net_socket_connected(qa_socket socket,bool *ready,qa_error *error)
{
    *ready=false;
    int state=qa_socket_connect_ready(socket);
    if(state<0 && qa_socket_interrupted(qa_socket_error())) return true;
    int status=0; qa_socklen length=(qa_socklen)sizeof(status);
    if(state<0 || (state && (getsockopt(socket,SOL_SOCKET,SO_ERROR,(char *)&status,&length)!=0 || status))) {
        qa_error_set(error,QA_ERROR_IO,0,"Connecting transport control socket failed");
        return false;
    }
    *ready=state!=0;
    return true;
}
