#include "remote_q1_hud.h"
#include "remote_q1_private.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct frontend_remote_q1_hud_storage {
    qa_hud_value vitals[3], ammo[4];
    qa_hud_score scores[256];
};
void remote_q1_hud_clear(frontend_remote_q1 *row)
{ if(row) { free(row->hud); row->hud=NULL; } }
bool frontend_remote_q1_hud_read(frontend_remote_q1 *row,const qa_hud_frame *frame,
    qa_hud_data *out,qa_error *error)
{
    if(!row || !frame || !out || row->busy || !remote_q1_mutable(row) || !remote_q1_live(row,error) ||
        frame->seat!=row->options.domain.physical_seat)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Remote Q1 HUD lost its actual CLIENT seat");
    *out=(qa_hud_data){.source_vitals=true};
    if(!row->loaded || !row->has_data || !row->view_entity) return true;
    qa_actor_id viewer;
    if(!remote_q1_actor_read(row,row->view_entity,&viewer,error) ||
        !qa_actor_id_equal(viewer,frame->actor))
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Remote Q1 HUD selected another received actor");
    if(!row->hud) {
        row->hud=calloc(1,sizeof(*row->hud));
        if(!row->hud) return remote_q1_fail(error,QA_ERROR_MEMORY,"Retaining received Q1 HUD rows");
    }
    frontend_remote_q1_hud_storage *hud=row->hud;
    const qa_q1_clientdata *data=&row->data;
    hud->vitals[0]=(qa_hud_value){.label="Health",.value=data->health,.warning=data->health<=25};
    hud->vitals[1]=(qa_hud_value){.label="Armor",.value=data->armor};
    hud->vitals[2]=(qa_hud_value){.label="Ammo",.value=data->ammo,.warning=data->ammo==0};
    static const char *const labels[]={"Shells","Nails","Rockets","Cells"};
    uint32_t counts[]={data->shells,data->nails,data->rockets,data->cells};
    for(unsigned i=0;i<4;++i) hud->ammo[i]=(qa_hud_value){.label=labels[i],.value=counts[i]};
    size_t scores=0;
    for(uint32_t i=0;i<256;++i) {
        const remote_q1_client *client=row->clients+i;
        if(!client->present) continue;
        hud->scores[scores++]=(qa_hud_score){.name=client->name?client->name:"",
            .team="",.score=client->frags,.ping=client->has_ping?client->ping:0,
            .local=i+1==row->view_entity};
    }
    static const struct { uint32_t bit; const char *name; } weapons[]={
        {4096,"q1:weapon/axe"},{1,"q1:weapon/shotgun"},{2,"q1:weapon/supershotgun"},
        {4,"q1:weapon/nailgun"},{8,"q1:weapon/supernailgun"},{16,"q1:weapon/grenadelauncher"},
        {32,"q1:weapon/rocketlauncher"},{64,"q1:weapon/lightning"}};
    qa_item_id selected=0;
    for(size_t i=0;i<sizeof(weapons)/sizeof(*weapons);++i) {
        bool axe=i==0 && !data->weapon && data->weapon_model && data->weapon_model<=row->model_count &&
            !strcmp(row->models[data->weapon_model-1],"progs/v_axe.mdl");
        if(data->weapon!=weapons[i].bit && !axe) continue;
        selected=qa_strings_find(qa_session_strings(qa_application_session(row->options.domain.application)),
            (qa_bytes){(const uint8_t *)weapons[i].name,strlen(weapons[i].name)});
        break;
    }
    *out=(qa_hud_data){.vitals=hud->vitals,.vital_count=3,.bars=hud->ammo,.bar_count=4,
        .scores=hud->scores,.score_count=scores,.source_vitals=true,.selected_weapon=selected,
        .crosshair_visible=data->health>0,.crosshair_color={1,1,1,1}};
    return remote_q1_live(row,error);
}
