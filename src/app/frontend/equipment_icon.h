#ifndef QA_FRONTEND_EQUIPMENT_ICON_H
#define QA_FRONTEND_EQUIPMENT_ICON_H
#include "qa/material.h"
#include "qa/vfs.h"

bool frontend_equipment_icon_key(const char *path,const char *lump,char *,size_t,qa_error *);
bool frontend_equipment_icon_load(qa_bytes,qa_game_family,qa_vfs *,qa_scene_resources *,
    qa_material_library *,const qa_material **,qa_resource **,qa_error *);
#endif
