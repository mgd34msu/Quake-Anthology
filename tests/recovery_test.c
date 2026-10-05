#define _POSIX_C_SOURCE 200809L
#include "qa/frontend.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_q1_save.h"
#include "qa/q1_save.h"
#include "qa/text.h"
#include "qa/ui_library.h"
#include "../src/app/frontend/internal.h"
#include "../src/app/frontend/save_commands.h"
#include "../src/app/frontend/constructor.h"
#include "../src/app/frontend/remote_q1_client.h"
#include "qa/bsp.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *text)
{ qa_error_set(error,QA_ERROR_FORMAT,0,"%s",text);return false; }
static bool frames(qa_frontend **slot,unsigned count,qa_error *error)
{
    for (unsigned i=0;i<count;++i)
        if (!qa_frontend_step(*slot,UINT64_C(16666667),error) ||
            !frontend_save_commands_drain(slot,error)) return false;
    return true;
}
static bool key(qa_frontend *f,SDL_Keycode symbol,SDL_Scancode scan,bool down,qa_error *error)
{
    qa_display_info info;
    if (!qa_display_info_get(f->display,&info,error)) return false;
    SDL_Event event;SDL_zero(event);event.type=down?SDL_KEYDOWN:SDL_KEYUP;
    event.key.windowID=info.window_id;event.key.state=down?SDL_PRESSED:SDL_RELEASED;
    event.key.keysym.sym=symbol;event.key.keysym.scancode=scan;
    return qa_input_platform_event(f->input,&event,(double)f->wall_time_ns/1000000.0,NULL,error);
}
static bool command(qa_frontend *f,const char *text,qa_error *error)
{
    qa_command_context context=qa_seat_console_context_read(f->seats[0].console);
    return qa_console_execute_now(qa_seat_console_recipient_read(f->seats[0].console),&context,text,error);
}
static bool source_health(const qa_q1_save_data *data,double *health,bool *god,qa_error *error)
{
    if (data->entity_count < 2) return fail(error,"Original single-player Source has no actual player edict");
    bool found_health=false,found_flags=false;
    for (size_t i=0;i<data->entities[1].count;++i) {
        const qa_q1_save_pair *field=data->entities[1].pairs+i;
        double value;
        if (!strcmp(field->key,"health")) {
            if (!qa_parse_atof(field->value,&value,error)) return false;
            *health=value;found_health=true;
        } else if (!strcmp(field->key,"flags")) {
            if (!qa_parse_atof(field->value,&value,error)) return false;
            *god=(qa_source_float_to_i32((float)value)&64)!=0;found_flags=true;
        }
    }
    return (found_health && found_flags) || fail(error,"Original Source player edict lacks real health/flags");
}
static bool observation(qa_frontend *f,qa_buffer *source,const char *directory,
    bool record,qa_error *error)
{
    uint32_t logical;qa_actor_id actor;qa_body_state body;
    qa_application_control_view control;
    if (!frontend_seat_launch_id_read(f,0,&logical) ||
        !qa_application_player_actor(f->application,logical,&actor) ||
        !qa_world_body_read(qa_application_world(f->application),actor,&body,error) ||
        !qa_application_control_read(f->application,actor,&control)) return false;
    qa_q1_save_data *data=NULL;double health=0;bool god=false;
    qa_q1_save_client client;
    bool okay=frontend_remote_q1_save_client_read(f,0,&client,error) &&
        qa_application_q1_save_capture(f->application,&client,&data,error) &&
        source_health(data,&health,&god,error);
    if (!okay) { qa_q1_save_destroy(data);return false; }
    printf("STATE logical=%u elapsed=%llu origin=%.9g,%.9g,%.9g health=%.9g god=%d input_sequence=%llu\n",
        logical,(unsigned long long)qa_session_elapsed(qa_application_session(f->application)),
        body.origin.x,body.origin.y,body.origin.z,health,god,
        (unsigned long long)control.command_sequence);fflush(stdout);
    if (!god || health!=73 || control.command_sequence==0)
        okay=fail(error,"Recovery did not retain actual completed cheat/input effects");
    if (okay) {
        char path[4096];int length=snprintf(path,sizeof(path),"%s/expected-control.txt",directory);
        if (length<0 || (size_t)length>=sizeof(path)) okay=fail(error,"Recovery control observation path is too long");
        else {
            FILE *file=fopen(path,record?"w":"r");
            if (!file) okay=fail(error,"Opening actual recovery control observation failed");
            else if (record) {
                okay=fprintf(file,"%.9g %.9g %.9g %" PRIu64 "\n",body.origin.x,body.origin.y,body.origin.z,
                    control.command_sequence)>0;
                if (fclose(file)) okay=false;
            } else {
                qa_vec3 expected;uint64_t sequence;
                okay=fscanf(file,"%f %f %f %" SCNu64,&expected.x,&expected.y,&expected.z,&sequence)==4;
                if (fclose(file)) okay=false;
                if (okay) {
                    okay=expected.x==body.origin.x && expected.y==body.origin.y && expected.z==body.origin.z &&
                        sequence==control.command_sequence;
                    printf("CONTROL_EQUAL origin=%d sequence=%" PRIu64 "/%" PRIu64 " equal=%d\n",
                        expected.x==body.origin.x && expected.y==body.origin.y && expected.z==body.origin.z,
                        sequence,control.command_sequence,okay);fflush(stdout);
                }
            }
            if (!okay && error->code==QA_OK) fail(error,"Recovered actual player origin/input sequence differs");
        }
    }
    if (okay) okay=qa_q1_save_encode(data,source,error);
    qa_q1_save_destroy(data);return okay;
}
static bool write_observation(const char *directory,const char *name,qa_bytes bytes,qa_error *error)
{
    char path[2048];int size=snprintf(path,sizeof(path),"%s/%s",directory,name);
    if (size<0 || (size_t)size>=sizeof(path)) return fail(error,"Recovery observation path is too long");
    FILE *file=fopen(path,"wb");
    bool okay=file && fwrite(bytes.data,1,bytes.size,file)==bytes.size;
    if (file && fclose(file)) okay=false;
    return okay || fail(error,"Writing actual Source observation failed");
}
static bool compare_expected(const char *directory,qa_bytes actual,qa_error *error)
{
    char path[2048];int size=snprintf(path,sizeof(path),"%s/expected-source.sav",directory);
    if (size<0 || (size_t)size>=sizeof(path)) return fail(error,"Recovery observation path is too long");
    FILE *file=fopen(path,"rb");
    if (!file) return fail(error,"Actual Source observation is missing");
    size_t offset=0;int byte;bool same=true;
    while ((byte=fgetc(file))!=EOF) {
        if (offset>=actual.size || (uint8_t)byte!=actual.data[offset]) {
            if (same) fprintf(stderr,"SOURCE_DIFFERENCE offset=%zu expected=%u actual=%u\n",offset,
                (unsigned)(uint8_t)byte,offset<actual.size?(unsigned)actual.data[offset]:UINT32_MAX);
            same=false;
        }
        ++offset;
    }
    bool okay=!ferror(file) && same && offset==actual.size;
    if (fclose(file)) okay=false;
    printf("SOURCE_EQUAL expected_bytes=%zu actual_bytes=%zu equal=%d\n",offset,actual.size,okay);fflush(stdout);
    if (!okay) (void)write_observation(directory,"actual-source.sav",actual,error);
    return okay || fail(error,"Recovered original Source globals/entities differ from the completed frame");
}
static int recovery_run(const char *root,const char *binary,const char *user_root,const char *phase)
{
    qa_frontend_options options;qa_frontend_options_default(&options);
    const char *roots[]={root};options.application.content_root=root;options.application.user_root=user_root;
    options.application.install_roots=roots;options.application.install_root_count=1;
    options.menu=true;options.audio=false;options.seats=1;options.display.hidden=false;
    options.display.width=640;options.display.height=480;
    options.display.backend=QA_DISPLAY_CPU;
    options.network_port=(uint16_t)(40000u+(unsigned)getpid()%20000u);
    options.native_bootstrap=strdup(binary);
    if (!options.native_bootstrap) return EXIT_FAILURE;
    options.application.native_bootstrap=options.native_bootstrap;
    qa_frontend *f=NULL;qa_error error={0};qa_buffer observed={0};int status=1;
    if (!qa_frontend_create(&options,&f,&error)) goto done;
    for (unsigned i=0;(frontend_constructor_pending(f) || qa_application_startup_pending(f->application)) && i<600;++i)
        if (!frames(&f,1,&error)) goto done;
    if (frontend_constructor_pending(f) || qa_application_startup_pending(f->application) || !frames(&f,4,&error)) goto done;
    bool available=false;
    if (!frontend_save_commands_recovery_available(f,&available,&error)) goto done;
    printf("AVAILABLE phase=%s interrupted=%d\n",phase,available);fflush(stdout);
    if (!strcmp(phase,"record")) {
        if (available) { fail(&error,"Record phase requires a fresh private user root");goto done; }
        if (!qa_ui_library_select_preset(f->seats[0].library,"q1-classic-id1",NULL,&error) ||
            !qa_ui_library_select_mode(f->seats[0].library,QA_MODE_SINGLE_PLAYER,&error)) goto done;
        qa_launch_draft *draft=qa_ui_library_draft(f->seats[0].library);
        const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_draft_choices(draft),
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        if (!entities) { fail(&error,"Selected UI draft has no actual ENTITIES provider");goto done; }
        if (!qa_launch_select_original(draft,entities->instance,&error) ||
            !qa_ui_library_apply(f->seats[0].library,&error)) goto done;
        for (unsigned i=0;i<600;++i) {
            if (!frames(&f,1,&error)) goto done;
            if (!qa_application_startup_pending(f->application) && !f->startup_launch &&
                qa_application_get_state(f->application)==QA_APPLICATION_RUNNING) break;
        }
        const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(f->application));
        const qa_launch_binding *world=qa_launch_binding_for(choices,(qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        const qa_launch_instance *original=world?qa_launch_snapshot_find(qa_application_launch(f->application),world->instance):NULL;
        qa_application_map_view map;qa_bsp_view bsp;
        if (!original || original->selection.runtime!=QA_PROGRAM_QUAKEC || !original->selection.artifact || !original->artifact ||
            !qa_resource_bytes(original->artifact).size || !qa_application_map_read(f->application,&map) ||
            !map.resource || !qa_bsp_open(qa_resource_bytes(map.resource),&bsp,&error) ||
            bsp.family!=QA_BSP_Q1 || !qa_bsp_record_count(&bsp,QA_BSP_MODELS) ||
            !bsp.lumps[QA_BSP_ENTITIES].bytes.size) {
            fail(&error,"Recovery requires its genuine original progs and retail BSP");goto done;
        }
        printf("LIVE_RECOVERY_SOURCE artifact=%s map=%s bsp=%s\n",original->selection.artifact,map.name,qa_bsp_format_name(bsp.format));fflush(stdout);
        if (qa_application_startup_pending(f->application) || !frames(&f,20,&error) ||
            !qa_ui_close_all(f->seats[0].ui,(double)f->time_ns/1000000.0,&error) ||
            !command(f,"god",&error) || !frames(&f,1,&error) ||
            !command(f,"give h 73",&error) || !frames(&f,1,&error)) goto done;
        qa_application_camera_view before,after;qa_actor_id actor;uint32_t logical;
        if (!frontend_seat_launch_id_read(f,0,&logical) || !qa_application_player_actor(f->application,logical,&actor) ||
            !qa_application_control_camera(f->application,actor,&before) ||
            !key(f,SDLK_w,SDL_SCANCODE_W,true,&error) || !frames(&f,24,&error) ||
            !key(f,SDLK_w,SDL_SCANCODE_W,false,&error) || !frames(&f,64,&error) ||
            !qa_application_control_camera(f->application,actor,&after)) goto done;
        float distance=qa_vec_length(qa_vec_sub(before.origin,after.origin));
        printf("REAL_MOVEMENT distance=%g held=%d\n",distance,qa_input_seat_has_held(f->seats[0].input));fflush(stdout);
        if (distance<1 || qa_input_seat_has_held(f->seats[0].input) ||
            !observation(f,&observed,user_root,!strcmp(phase,"record"),&error) || !write_observation(user_root,"expected-source.sav",(qa_bytes){observed.data,observed.size},&error)) goto done;
        if (!command(f,"give h 13",&error)) goto done;
        qa_q1_save_data *tail=NULL;double tail_health=0;bool tail_god=false;
        qa_q1_save_client client;
        bool tail_okay=frontend_remote_q1_save_client_read(f,0,&client,&error) &&
            qa_application_q1_save_capture(f->application,&client,&tail,&error) &&
            source_health(tail,&tail_health,&tail_god,&error);
        qa_q1_save_destroy(tail);
        if (!tail_okay || tail_health!=13 || !tail_god) {
            fail(&error,"Unfinished actual Source command was not executed");goto done;
        }
        puts("UNCLEAN_EXIT unfinished_health=13 expected_completed_health=73");fflush(stdout);
        _Exit(0);
    }
    if (!available || !qa_ui_open(f->seats[0].ui,FRONTEND_LOAD,(double)f->time_ns/1000000.0,&error)) goto done;
    qa_ui_state menu;
    if (!qa_ui_state_read(f->seats[0].ui,&menu,&error) || menu.menu!=FRONTEND_LOAD || menu.control!=1) {
        fail(&error,"Load Game did not expose the actual Recover row first");goto done;
    }
    if (!key(f,SDLK_RETURN,SDL_SCANCODE_RETURN,true,&error) || !frames(&f,1,&error) ||
        !key(f,SDLK_RETURN,SDL_SCANCODE_RETURN,false,&error) ||
        !observation(f,&observed,user_root,!strcmp(phase,"record"),&error) || !compare_expected(user_root,(qa_bytes){observed.data,observed.size},&error)) goto done;
    if (!command(f,"quit",&error)) goto done;
    status=0;
done:
    qa_buffer_free(&observed);
    if (status) fprintf(stderr,"LIVE_RECOVERY_FAILURE phase=%s code=%d message=%s\n",phase,error.code,error.message);
    qa_error cleanup={0};bool closed=true;
    if (f) closed=qa_frontend_shutdown(&f,&cleanup) && qa_frontend_destroy(f,&cleanup);
    if (!closed) { fprintf(stderr,"CLEANUP %d %s\n",cleanup.code,cleanup.message);status=1; }
    if (!f) qa_frontend_options_destroy(&options);
    return status;
}

/* The normal suite never opens a game window. This existing live-case switch
 * selects the real record/crash/fresh-process menu recovery path explicitly. */
bool test_recovery_child(int argc,char **argv,int *status);
bool test_recovery_child(int argc,char **argv,int *status)
{
    if (argc<2 || strcmp(argv[1],"--qa-recovery-test")) return false;
    if (argc!=6 || (strcmp(argv[2],"record") && strcmp(argv[2],"recover"))) *status=EXIT_FAILURE;
    else *status=recovery_run(argv[3],argv[4],argv[5],argv[2]);
    return true;
}

static void recovery_child(const char *self,const char *phase,const char *root,
    const char *binary,const char *user_root)
{
    fflush(NULL);
    pid_t child=fork();
    if (child<0) {
        fprintf(stderr,"LIVE_RECOVERY_FAILURE phase=%s fork=%s\n",phase,strerror(errno));exit(EXIT_FAILURE);
    }
    if (!child) {
        if (setpgid(0,0)!=0) _exit(EXIT_FAILURE);
        char *args[]={(char *)self,"--qa-recovery-test",(char *)phase,(char *)root,
            (char *)binary,(char *)user_root,NULL};
        execvp(self,args);
        fprintf(stderr,"LIVE_RECOVERY_FAILURE phase=%s exec=%s\n",phase,strerror(errno));_exit(EXIT_FAILURE);
    }
    (void)setpgid(child,child);
    struct timespec began,now;
    bool timed=clock_gettime(CLOCK_MONOTONIC,&began)==0;
    int status=0;
    for (;;) {
        pid_t waited=waitpid(child,&status,WNOHANG);
        if (waited==child) break;
        if ((waited<0 && errno!=EINTR) || !timed || clock_gettime(CLOCK_MONOTONIC,&now)!=0 ||
            now.tv_sec-began.tv_sec>=100) {
            (void)kill(-child,SIGKILL);(void)kill(child,SIGKILL);
            while (waitpid(child,&status,0)<0 && errno==EINTR) {}
            fprintf(stderr,"LIVE_RECOVERY_FAILURE phase=%s deadline/wait user_root=%s\n",phase,user_root);exit(EXIT_FAILURE);
        }
        struct timespec pause={.tv_nsec=10000000};(void)nanosleep(&pause,NULL);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)!=EXIT_SUCCESS) {
        fprintf(stderr,"LIVE_RECOVERY_FAILURE phase=%s child_status=%d user_root=%s\n",phase,status,user_root);exit(EXIT_FAILURE);
    }
}

void test_recovery(const char *self);
void test_recovery(const char *self)
{
    const char *required=getenv("QA_REQUIRE_LIVE"),*selected=getenv("QA_LIVE_CASE");
    if (!required || strcmp(required,"1") || !selected || strcmp(selected,"recovery")) return;
    const char *root=getenv("QA_LIVE_CONTENT_ROOT"),*binary=getenv("QA_LIVE_BINARY");
    struct stat info;
    if (!root || !*root || stat(root,&info)!=0 || !S_ISDIR(info.st_mode) ||
        !binary || binary[0]!='/' || access(binary,X_OK)!=0) {
        fputs("QA_REQUIRE_LIVE=1 QA_LIVE_CASE=recovery requires QA_LIVE_CONTENT_ROOT and an absolute executable QA_LIVE_BINARY\n",stderr);
        exit(EXIT_FAILURE);
    }
    const char *cache=getenv("XDG_CACHE_HOME"),*home=getenv("HOME");
    char user_root[4096];
    int length=cache && *cache?snprintf(user_root,sizeof(user_root),"%s/qa-live-recovery-XXXXXX",cache):
        home && *home?snprintf(user_root,sizeof(user_root),"%s/.cache/qa-live-recovery-XXXXXX",home):-1;
    if (length<0 || (size_t)length>=sizeof(user_root) || !mkdtemp(user_root)) {
        fprintf(stderr,"LIVE_RECOVERY_FAILURE creating isolated user root: %s\n",strerror(errno));exit(EXIT_FAILURE);
    }
    printf("LIVE_RECOVERY_START user_root=%s\n",user_root);fflush(stdout);
    recovery_child(self,"record",root,binary,user_root);
    recovery_child(self,"recover",root,binary,user_root);
    printf("LIVE_RECOVERY_PASS source=equal origin=equal input_sequence=equal unfinished_tail=excluded shutdown=complete user_root=%s\n",user_root);
    fflush(stdout);
}
