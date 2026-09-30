#include "save_private.h"
#include "qa/source_save.h"

#include <math.h>
#include <string.h>

static bool fields(qa_source_save_io *io, qa_application *app,
                    qa_physics *physics)
{
    uint8_t magic[4] = {'Q', 'A', 'P', 'H'};
    uint32_t version = 1;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        memcmp(magic, "QAPH", sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_actor(io, &physics->world_actor) ||
        !qa_source_save_f32(io, &physics->gravity) ||
        !qa_source_save_f32(io, &physics->max_velocity) ||
        !qa_source_save_f32(io, &physics->stop_speed) ||
        !qa_source_save_vec3(io, &physics->q2r_local_origin))
        return false;
    if (!isfinite(physics->gravity) || !isfinite(physics->max_velocity) ||
        physics->max_velocity < 0 ||
        !isfinite(physics->stop_speed) || !qa_vec_finite(physics->q2r_local_origin) ||
        (physics->world_actor.registry &&
         !qa_actors_get(qa_session_actors(app->session), physics->world_actor)))
        return application_fail(io->error, QA_ERROR_FORMAT,
                                "saved physics has invalid scalar or world identity");
    return true;
}

static bool owner(qa_application *app, qa_error *error)
{
    return (app && app->physics && app->session && app->world &&
            app->physics->world == app->world &&
            !app->physics->push_transaction && !app->physics->q2r_pml_origin &&
            qa_session_safe(app->session) && qa_world_idle(app->world)) ||
           application_fail(error, QA_ERROR_ARGUMENT,
                            "physics continuation requires an idle actual world owner");
}

bool application_physics_capture(qa_application *app, qa_buffer *out,
                                  qa_error *error)
{
    if (!out || !owner(app, error))
        return false;
    qa_source_save_io io = {0};
    qa_physics copy = *app->physics;
    bool ok = qa_source_save_writer(&io, app->session, error) &&
              fields(&io, app, &copy) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_physics_restore(qa_application *app, qa_bytes bytes,
                                  qa_error *error)
{
    if (!owner(app, error))
        return false;
    qa_source_save_io io = {0};
    qa_physics copy = *app->physics;
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) &&
              fields(&io, app, &copy) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        app->physics->world_actor = copy.world_actor;
        app->physics->gravity = copy.gravity;
        app->physics->max_velocity = copy.max_velocity;
        app->physics->stop_speed = copy.stop_speed;
        app->physics->q2r_local_origin = copy.q2r_local_origin;
    }
    return ok;
}
