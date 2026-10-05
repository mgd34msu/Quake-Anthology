#include "config_userinfo.h"
#include "qa/application_character_selection.h"

bool frontend_config_userinfo_register(qa_cvars *cvars,uint32_t seat,const char *model,qa_error *error)
{
    return qa_application_player_userinfo_register(cvars,seat,model,error);
}
