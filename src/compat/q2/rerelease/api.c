/* Port of src/compat/q2/rerelease/api.ts. Entry specs transcribed
 * mechanically from the TS entry() lists. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/api.h"
#include <stdlib.h>
#include <string.h>

const qa_native_call_abi qa_compat_q2_rerelease_abi = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
const qa_guest_value_layout qa_compat_q2_rr_pointer_layout = {false, "pointer", {NULL, 0, 0, 0, NULL, 0}};
const qa_guest_value_layout qa_compat_q2_rr_int_layout = {false, "int32", {NULL, 0, 0, 0, NULL, 0}};
const qa_guest_value_layout qa_compat_q2_rr_uint_layout = {false, "uint32", {NULL, 0, 0, 0, NULL, 0}};
static qa_guest_value_layout scalar_layout(const char *storage) {
  qa_guest_value_layout v; memset(&v, 0, sizeof v); v.storage = (qa_guest_storage)storage; return v;
}
static qa_guest_value_layout layout_for(char code) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_guest_value_layout v; memset(&v, 0, sizeof v);
  switch (code) {
    case 'P': return scalar_layout("pointer");
    case 'I': return scalar_layout("int32");
    case 'U': return scalar_layout("uint32");
    case 'Q': return scalar_layout("uint64");
    case 'F': return scalar_layout("float32");
    case 'B': return scalar_layout("uint8");
    case 'H': return scalar_layout("int16");
    case 'W': return scalar_layout("uint16");
    case 'T': v.is_aggregate = true; v.layout = qa_compat_q2_rerelease_trace_layout; return v;
    case 'R': v.is_aggregate = true; v.layout = qa_compat_q2_rerelease_rectangle_layout; return v;
    default: v.is_aggregate = true; v.layout = qa_compat_q2_rerelease_vec2_layout; return v;
  }
}
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_signature(const qa_guest_value_layout *p, size_t n, bool is_void, qa_guest_value_layout result) {
  qa_compat_q2_guest_call_signature s; memset(&s, 0, sizeof s);
  s.abi = qa_compat_q2_rerelease_abi; s.parameters = p; s.parameter_count = n; s.result_is_void = is_void; s.result = result; s.variadic = false; return s;
}
typedef struct { const char *name; const char *params; char result; } spec_t;
static const spec_t game_import_specs[] = {
  {"Broadcast_Print", "IP", 'v'},
  {"Com_Print", "P", 'v'},
  {"Client_Print", "PIP", 'v'},
  {"Center_Print", "PP", 'v'},
  {"sound", "PBIFFF", 'v'},
  {"positioned_sound", "PPBIFFF", 'v'},
  {"local_sound", "PPPBIFFFU", 'v'},
  {"configstring", "IP", 'v'},
  {"get_configstring", "I", 'P'},
  {"Com_Error", "P", 'v'},
  {"modelindex", "P", 'I'},
  {"soundindex", "P", 'I'},
  {"imageindex", "P", 'I'},
  {"setmodel", "PP", 'v'},
  {"trace", "PPPPPU", 'T'},
  {"clip", "PPPPPU", 'T'},
  {"pointcontents", "P", 'U'},
  {"inPVS", "PPB", 'B'},
  {"inPHS", "PPB", 'B'},
  {"SetAreaPortalState", "IB", 'v'},
  {"AreasConnected", "II", 'B'},
  {"linkentity", "P", 'v'},
  {"unlinkentity", "P", 'v'},
  {"BoxEdicts", "PPPQIPP", 'Q'},
  {"multicast", "PIB", 'v'},
  {"unicast", "PBU", 'v'},
  {"WriteChar", "I", 'v'},
  {"WriteByte", "I", 'v'},
  {"WriteShort", "I", 'v'},
  {"WriteLong", "I", 'v'},
  {"WriteFloat", "F", 'v'},
  {"WriteString", "P", 'v'},
  {"WritePosition", "P", 'v'},
  {"WriteDir", "P", 'v'},
  {"WriteAngle", "F", 'v'},
  {"WriteEntity", "P", 'v'},
  {"TagMalloc", "QI", 'P'},
  {"TagFree", "P", 'v'},
  {"FreeTags", "I", 'v'},
  {"cvar", "PPU", 'P'},
  {"cvar_set", "PP", 'P'},
  {"cvar_forceset", "PP", 'P'},
  {"argc", "", 'I'},
  {"argv", "I", 'P'},
  {"args", "", 'P'},
  {"AddCommandString", "P", 'v'},
  {"DebugGraph", "FI", 'v'},
  {"GetExtension", "P", 'P'},
  {"Bot_RegisterEdict", "P", 'v'},
  {"Bot_UnRegisterEdict", "P", 'v'},
  {"Bot_MoveToPoint", "PPF", 'I'},
  {"Bot_FollowActor", "PP", 'I'},
  {"GetPathToGoal", "PP", 'B'},
  {"Loc_Print", "PIPPQ", 'v'},
  {"Draw_Line", "PPPFB", 'v'},
  {"Draw_Point", "PFPFB", 'v'},
  {"Draw_Circle", "PFPFB", 'v'},
  {"Draw_Bounds", "PPPFB", 'v'},
  {"Draw_Sphere", "PFPFB", 'v'},
  {"Draw_OrientedWorldText", "PPPFFB", 'v'},
  {"Draw_StaticWorldText", "PPPPFFB", 'v'},
  {"Draw_Cylinder", "PFFPFB", 'v'},
  {"Draw_Ray", "PPFFPFB", 'v'},
  {"Draw_Arrow", "PPFPPFB", 'v'},
  {"ReportMatchDetails_Multicast", "B", 'v'},
  {"ServerFrame", "", 'U'},
  {"SendToClipBoard", "P", 'v'},
  {"Info_ValueForKey", "PPPQ", 'Q'},
  {"Info_RemoveKey", "PP", 'B'},
  {"Info_SetValueForKey", "PPP", 'B'},
};
static const spec_t game_export_specs[] = {
  {"PreInit", "", 'v'},
  {"Init", "", 'v'},
  {"Shutdown", "", 'v'},
  {"SpawnEntities", "PPP", 'v'},
  {"WriteGameJson", "BP", 'P'},
  {"ReadGameJson", "P", 'v'},
  {"WriteLevelJson", "BP", 'P'},
  {"ReadLevelJson", "P", 'v'},
  {"CanSave", "", 'B'},
  {"ClientChooseSlot", "PPBPQB", 'P'},
  {"ClientConnect", "PPPB", 'B'},
  {"ClientBegin", "P", 'v'},
  {"ClientUserinfoChanged", "PP", 'v'},
  {"ClientDisconnect", "P", 'v'},
  {"ClientCommand", "P", 'v'},
  {"ClientThink", "PP", 'v'},
  {"RunFrame", "B", 'v'},
  {"PrepFrame", "", 'v'},
  {"ServerCommand", "", 'v'},
  {"Pmove", "P", 'v'},
  {"GetExtension", "P", 'P'},
  {"Bot_SetWeapon", "PIB", 'v'},
  {"Bot_TriggerEdict", "PP", 'v'},
  {"Bot_UseItem", "PI", 'v'},
  {"Bot_GetItemID", "P", 'I'},
  {"Edict_ForceLookAtPoint", "PP", 'v'},
  {"Bot_PickedUpItem", "PP", 'B'},
  {"Entity_IsVisibleToPlayer", "PP", 'B'},
  {"GetShadowLightData", "I", 'P'},
};
static const spec_t cgame_import_specs[] = {
  {"Com_Print", "P", 'v'},
  {"get_configstring", "I", 'P'},
  {"Com_Error", "P", 'v'},
  {"TagMalloc", "QI", 'P'},
  {"TagFree", "P", 'v'},
  {"FreeTags", "I", 'v'},
  {"cvar", "PPU", 'P'},
  {"cvar_set", "PP", 'P'},
  {"cvar_forceset", "PP", 'P'},
  {"AddCommandString", "P", 'v'},
  {"GetExtension", "P", 'P'},
  {"CL_FrameValid", "", 'B'},
  {"CL_FrameTime", "", 'F'},
  {"CL_ClientTime", "", 'Q'},
  {"CL_ClientRealTime", "", 'Q'},
  {"CL_ServerFrame", "", 'I'},
  {"CL_ServerProtocol", "", 'I'},
  {"CL_GetClientName", "I", 'P'},
  {"CL_GetClientPic", "I", 'P'},
  {"CL_GetClientDogtag", "I", 'P'},
  {"CL_GetKeyBinding", "P", 'P'},
  {"Draw_RegisterPic", "P", 'B'},
  {"Draw_GetPicSize", "PPP", 'v'},
  {"SCR_DrawChar", "IIIIB", 'v'},
  {"SCR_DrawPic", "IIIIP", 'v'},
  {"SCR_DrawColorPic", "IIIIPP", 'v'},
  {"SCR_SetAltTypeface", "B", 'v'},
  {"SCR_DrawFontString", "PIIIPBI", 'v'},
  {"SCR_MeasureFontString", "PI", 'V'},
  {"SCR_FontLineHeight", "I", 'F'},
  {"CL_GetTextInput", "PP", 'B'},
  {"CL_GetWarnAmmoCount", "I", 'I'},
  {"Localize", "PPQ", 'P'},
  {"SCR_DrawBind", "IPPIII", 'I'},
  {"CL_InAutoDemoLoop", "", 'B'},
};
static const spec_t cgame_export_specs[] = {
  {"Init", "", 'v'},
  {"Shutdown", "", 'v'},
  {"DrawHUD", "IPRRIIP", 'v'},
  {"TouchPics", "", 'v'},
  {"LayoutFlags", "P", 'H'},
  {"GetActiveWeaponWheelWeapon", "P", 'I'},
  {"GetOwnedWeaponWheelWeapons", "P", 'U'},
  {"GetWeaponWheelAmmoCount", "PI", 'H'},
  {"GetPowerupWheelCount", "PI", 'H'},
  {"GetHitMarkerDamage", "P", 'H'},
  {"Pmove", "P", 'v'},
  {"ParseConfigString", "IP", 'v'},
  {"ParseCenterPrint", "PIB", 'v'},
  {"ClearNotify", "I", 'v'},
  {"ClearCenterprint", "I", 'v'},
  {"NotifyMessage", "IPB", 'v'},
  {"GetMonsterFlashOffset", "WP", 'v'},
  {"GetExtension", "P", 'P'},
};
static qa_compat_q2_rerelease_api_entry *game_imports, *game_exports, *cgame_imports, *cgame_exports;
static qa_guest_layout game_import_layout, game_export_layout, cgame_import_layout, cgame_export_layout;
static bool built = false;
static qa_compat_q2_rerelease_api_entry *build_entries(const spec_t *specs, size_t count) {
  qa_compat_q2_rerelease_api_entry *entries = calloc(count, sizeof *entries);
  if (!entries) qa_range_error("Rerelease api out of memory");
  for (size_t i = 0; i < count; i++) {
    size_t n = strlen(specs[i].params);
    qa_guest_value_layout *params = calloc(n ? n : 1, sizeof *params);
    if (!params) qa_range_error("Rerelease api out of memory");
    for (size_t j = 0; j < n; j++) params[j] = layout_for(specs[i].params[j]);
    entries[i].name = specs[i].name;
    entries[i].signature = qa_compat_q2_rerelease_signature(params, n, specs[i].result == 'v',
        specs[i].result == 'v' ? qa_compat_q2_rr_int_layout : layout_for(specs[i].result));
  }
  return entries;
}
static qa_guest_layout table_layout(bool imports, const char *kind, const qa_compat_q2_rerelease_api_entry *entries, size_t count) {
  const char **names = malloc((count ? count : 1) * sizeof *names);
  if (!names) qa_range_error("Rerelease api out of memory");
  for (size_t i = 0; i < count; i++) names[i] = entries[i].name;
  qa_guest_layout layout = imports ? qa_compat_q2_rerelease_import_table_layout(kind, names, count)
                                   : qa_compat_q2_rerelease_export_table_layout(kind, names, count);
  free(names); return layout;
}
void qa_compat_q2_rerelease_api_ensure(void) {
  if (built) return;
  built = true;
  qa_compat_q2_rerelease_layouts_ensure();
  game_imports = build_entries(game_import_specs, sizeof game_import_specs / sizeof game_import_specs[0]);
  game_exports = build_entries(game_export_specs, sizeof game_export_specs / sizeof game_export_specs[0]);
  cgame_imports = build_entries(cgame_import_specs, sizeof cgame_import_specs / sizeof cgame_import_specs[0]);
  cgame_exports = build_entries(cgame_export_specs, sizeof cgame_export_specs / sizeof cgame_export_specs[0]);
  game_import_layout = table_layout(true, "game", game_imports, sizeof game_import_specs / sizeof game_import_specs[0]);
  game_export_layout = table_layout(false, "game", game_exports, sizeof game_export_specs / sizeof game_export_specs[0]);
  cgame_import_layout = table_layout(true, "cgame", cgame_imports, sizeof cgame_import_specs / sizeof cgame_import_specs[0]);
  cgame_export_layout = table_layout(false, "cgame", cgame_exports, sizeof cgame_export_specs / sizeof cgame_export_specs[0]);
}
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_game_imports(size_t *n) { qa_compat_q2_rerelease_api_ensure(); if (n) *n = sizeof game_import_specs / sizeof game_import_specs[0]; return game_imports; }
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_game_exports(size_t *n) { qa_compat_q2_rerelease_api_ensure(); if (n) *n = sizeof game_export_specs / sizeof game_export_specs[0]; return game_exports; }
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_cgame_imports(size_t *n) { qa_compat_q2_rerelease_api_ensure(); if (n) *n = sizeof cgame_import_specs / sizeof cgame_import_specs[0]; return cgame_imports; }
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_cgame_exports(size_t *n) { qa_compat_q2_rerelease_api_ensure(); if (n) *n = sizeof cgame_export_specs / sizeof cgame_export_specs[0]; return cgame_exports; }
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_find_entry(const qa_compat_q2_rerelease_api_entry *entries, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (strcmp(entries[i].name, name) == 0) return &entries[i];
  return NULL;
}
const qa_guest_layout *qa_compat_q2_rerelease_get_game_import_layout(void) { qa_compat_q2_rerelease_api_ensure(); return &game_import_layout; }
const qa_guest_layout *qa_compat_q2_rerelease_get_game_export_layout(void) { qa_compat_q2_rerelease_api_ensure(); return &game_export_layout; }
const qa_guest_layout *qa_compat_q2_rerelease_get_cgame_import_layout(void) { qa_compat_q2_rerelease_api_ensure(); return &cgame_import_layout; }
const qa_guest_layout *qa_compat_q2_rerelease_get_cgame_export_layout(void) { qa_compat_q2_rerelease_api_ensure(); return &cgame_export_layout; }
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_api_signature(void) {
  static const qa_guest_value_layout p[] = {{false, "pointer", {NULL, 0, 0, 0, NULL, 0}}};
  return qa_compat_q2_rerelease_signature(p, 1, false, qa_compat_q2_rr_pointer_layout);
}
