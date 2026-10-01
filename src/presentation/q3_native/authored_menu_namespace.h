#ifndef QA_Q3_AUTHORED_MENU_NAMESPACE_H
#define QA_Q3_AUTHORED_MENU_NAMESPACE_H
#define Q_stricmp q3menu_stricmp
#define Q_strupr q3menu_strupr
#define Q_strncpyz q3menu_strncpyz
#define Q_strcat q3menu_strcat
#define COM_ParseExt q3menu_parse
#define Com_Printf q3menu_print
#define Com_Error q3menu_error
#define va q3menu_format
#define AnglesToAxis q3menu_angles
#define AxisClear q3menu_axis_clear
#define trap_PC_AddGlobalDefine q3menu_define
#define trap_PC_LoadSource q3menu_source_open
#define trap_PC_FreeSource q3menu_source_close
#define trap_PC_ReadToken q3menu_source_token
#define trap_PC_SourceFileAndLine q3menu_source_position
#endif
