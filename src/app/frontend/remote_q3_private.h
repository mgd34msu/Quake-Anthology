#ifndef QA_FRONTEND_REMOTE_Q3_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q3_PRIVATE_H

#include "remote_q3_client.h"

typedef struct frontend_remote_q3_services frontend_remote_q3_services;
struct frontend_remote_q3 {
    frontend_remote_q3 *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    frontend_remote_q3_resources resources;
    qa_resource *map;
    frontend_remote_q3_services *services;
    size_t users;
    bool resources_ready, constructing;
};

#endif
