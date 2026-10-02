#ifndef QA_FRONTEND_REMOTE_Q3_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q3_PRIVATE_H

#include "remote_q3_client.h"

typedef struct frontend_remote_q3_services frontend_remote_q3_services;
typedef struct frontend_remote_q3_frame frontend_remote_q3_frame;
struct frontend_remote_q3 {
    frontend_remote_q3 *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    frontend_remote_q3_resources resources;
    qa_resource *map;
    struct frontend_material_movies *shader_movies;
    frontend_remote_q3_services *services;
    frontend_remote_q3_modules *modules;
    frontend_remote_q3_transport *transport;
    frontend_remote_q3_frame *frames;
    frontend_remote_q3_runtime *runtime;
    struct frontend_remote_q3_compiled_video *compiled_video;
    size_t users;
    uint64_t video_generation;
    bool resources_ready, constructing, retiring, importing;
};

#endif
