#include "q1_help.h"
#include "menu_art.h"
#include <math.h>
#include <stdlib.h>

enum { HELP_PAGES=6 };
struct frontend_q1_help {
    frontend_seat *seat;
    qa_scene_resources *source;
    qa_scene_resources *open_source;
    const qa_scene_image *pages[HELP_PAGES];
    qa_ui_control control;
    unsigned page;
};
static double now(const frontend_q1_help *help)
{ return (double)help->seat->frontend->time_ns/1000000.0; }
static void pages_release(frontend_q1_help *help)
{
    for (unsigned i=0;i<HELP_PAGES;++i) {
        qa_scene_image_release(help->pages[i]);help->pages[i]=NULL;
    }
    qa_scene_resources_destroy(help->open_source);help->open_source=NULL;
}
static void closed(void *context,uint32_t seat)
{ frontend_q1_help *help=context;(void)seat;pages_release(help); }
static bool opened(void *context,uint32_t seat,qa_error *error)
{
    frontend_q1_help *help=context;(void)seat;
    qa_scene_resources *images=help->source;
    if (!images && !frontend_menu_art_q1_source(help->seat,&images,error))return false;
    pages_release(help);help->page=0;
    if (!qa_scene_resources_retain(images,error))return false;
    help->open_source=images;
    for (unsigned i=0;i<HELP_PAGES;++i)
        if (!frontend_menu_art_q1_help_page(images,i,help->pages+i,error)) {
            pages_release(help);return false;
        }
    return true;
}
static bool draw(void *context,uint32_t seat,qa_scene_frame *frame,qa_scene_rect viewport,
    qa_scene_rect_f rectangle,qa_error *error)
{
    frontend_q1_help *help=context;(void)seat;(void)rectangle;
    const qa_scene_image *image=help->pages[help->page];
    if (!image)return true;
    float scale=fminf((float)viewport.width/320,(float)viewport.height/200);
    qa_scene_rect_f picture={(float)viewport.x+((float)viewport.width-320*scale)*.5f,
        (float)viewport.y+((float)viewport.height-200*scale)*.5f,320*scale,200*scale};
    return qa_scene_frame_picture(frame,qa_scene_white(help->seat->frontend->ui_images),viewport,
        viewport,(qa_vec4){0,0,1,1},(qa_vec4){0,0,0,1},error) &&
        qa_scene_frame_picture_f(frame,image,viewport,picture,(qa_vec4){0,0,1,1},
            (qa_vec4){1,1,1,1},error);
}
static bool input(void *context,uint32_t seat,const qa_input_event *event,bool *consumed,qa_error *error)
{
    frontend_q1_help *help=context;(void)seat;
    *consumed=true;
    if (event->kind!=QA_INPUT_EVENT_KEY || !event->down)return true;
    switch (event->input.code) {
    case QA_KEY_UP:case QA_KEY_RIGHT:case QA_KEY_KP_UP:case QA_KEY_KP_RIGHT:
        help->page=(help->page+1)%HELP_PAGES;break;
    case QA_KEY_DOWN:case QA_KEY_LEFT:case QA_KEY_KP_DOWN:case QA_KEY_KP_LEFT:
        help->page=(help->page+HELP_PAGES-1)%HELP_PAGES;break;
    case QA_KEY_ESCAPE:
        return qa_ui_close_all(help->seat->ui,now(help),error) &&
            frontend_menu_open(help->seat,FRONTEND_HOME,error);
    default:break;
    }
    return true;
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *error)
{
    frontend_q1_help *help=context;(void)seat;(void)error;
    *out=(qa_ui_menu){.id=FRONTEND_Q1_HELP,.title="",.controls=&help->control,.count=1,
        .fullscreen=true,.picture_only=true};return true;
}
bool frontend_q1_help_create(frontend_seat *seat,qa_error *error)
{
    if (seat->q1_help)return true;
    frontend_q1_help *help=calloc(1,sizeof(*help));
    if (!help)return frontend_fail(error,QA_ERROR_MEMORY,"Creating Quake help pages");
    help->seat=seat;
    help->control=(qa_ui_control){.id=1,.kind=QA_UI_OWNER_DRAW,.rect={0,0,640,480},
        .enabled=true,.visible=true,.context=help,.value.owner={.draw=draw,.input=input}};
    if (!qa_ui_register(seat->ui,&(qa_ui_menu_registration){.id=FRONTEND_Q1_HELP,
        .context=help,.factory=factory,.open=opened,.close=closed},error)) {
        free(help);return false;
    }
    seat->q1_help=help;return true;
}
bool frontend_q1_help_destroy(frontend_seat *seat,qa_error *error)
{
    frontend_q1_help *help=seat->q1_help;
    if (!help)return true;
    if (!qa_ui_unregister(seat->ui,FRONTEND_Q1_HELP,now(help),error))return false;
    pages_release(help);free(help);seat->q1_help=NULL;return true;
}
bool frontend_q1_help_open(frontend_seat *seat,qa_error *error)
{
    return frontend_q1_help_create(seat,error) &&
        qa_ui_close_all(seat->ui,(double)seat->frontend->time_ns/1000000.0,error) &&
        frontend_menu_open(seat,FRONTEND_Q1_HELP,error);
}
void frontend_q1_help_bind_source(frontend_seat *seat,qa_scene_resources *images)
{ if (seat->q1_help)seat->q1_help->source=images; }
void frontend_q1_help_forget_source(frontend_seat *seat,const qa_scene_resources *images)
{ if (seat->q1_help && seat->q1_help->source==images)seat->q1_help->source=NULL; }
