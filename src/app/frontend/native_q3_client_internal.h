#ifndef QA_FRONTEND_NATIVE_Q3_CLIENT_INTERNAL_H
#define QA_FRONTEND_NATIVE_Q3_CLIENT_INTERNAL_H
#include "native_q3_client.h"
#include "native_q3_commands.h"
struct frontend_native_q3 {
    frontend_native_q3 *next;
    qa_frontend *frontend;
    frontend_native_q3_view view;
    frontend_native_q3_composition composition;
    qa_launch_instance_lease *source_lease;
    struct frontend_material_movies *shader_movies;
    qa_console *console;
    qa_command_context command;
    frontend_native_q3_commands *commands;
    char *music_intro, *music_loop, *disconnect;
    bool music_looping, service_released, owns_media, owns_services;
    bool frame_active, constructed;
    size_t callbacks;
    struct frontend_native_q3_video *video;
    struct frontend_native_components *components;
};
bool frontend_native_q3_video_row_close(frontend_native_q3 *,qa_error *);
bool frontend_native_q3_video_row_reopen(frontend_native_q3 *,qa_error *);
bool frontend_native_q3_current(const frontend_native_q3 *);
bool frontend_native_q3_cut(frontend_native_q3 *,const q3n_frame *,qa_error *);
bool frontend_native_q3_core_options(frontend_native_q3 *,q3n_native_options *,qa_error *);
bool frontend_native_q3_service_options(frontend_native_q3 *,qa_native_q3_client_services *,qa_error *);
bool frontend_native_q3_project_settings(frontend_native_q3 *,q3n_native_frame_options *,qa_error *);
bool frontend_native_q3_make_children(frontend_native_q3 *,const qa_application_native_q3_presentation *,qa_error *);
#endif
