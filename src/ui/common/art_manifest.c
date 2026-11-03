/* Port of src/ui/common/art-manifest.ts. GPL-2.0-or-later. */
#include "qa/ui/common/art_manifest.h"

const qa_menu_art_file qa_ui_menu_background = {
  "assets/ui/menu-background.png", 1536, 1024,
  "04c4fd787c4b47f1ba27ec293c361cdf1c87406fe4d1f0005b8ba6fbe1287ffa",
};
const qa_menu_art_frame qa_ui_menu_panel_art = {
  { "assets/ui/menu-panel.png", 1254, 1254,
    "179d84c501384fc88443a9a415a06195270306706eb7e4ca5dc68ed06043f2a0" },
  1254, 1254, { { 0, 0 }, { 1, 1 } }, 160, 160, 160, 160, 0.2,
};
const qa_menu_art_frame qa_ui_menu_focus_art = {
  { "assets/ui/menu-focus.png", 2172, 724,
    "65b98e03aaba9f26edc36ffa0e769e2932b5b85ea994a09447edf3bfc6ba4312" },
  2172, 633, { { 0, 68.0 / 724 }, { 1, 701.0 / 724 } }, 96, 96, 96, 96, 0.0625,
};
const qa_menu_art_file qa_ui_main_menu_background = {
  "assets/ui/main-menu-background.png", 1672, 941,
  "53630f6ca2d86492e983f7e54b54ce913cbebe58df6bd847c316753ef306a718",
};
const qa_menu_art_file qa_ui_splash_title = {
  "assets/ui/title.png", 1536, 1024,
  "ca15d94fcdcd748449eddb73dd280a8af267f9d7d3aadd036de6e704f5988708",
};
const qa_menu_art_file qa_ui_menu_art_files[QA_UI_MENU_ART_FILE_COUNT] = {
  { "assets/ui/menu-background.png", 1536, 1024,
    "04c4fd787c4b47f1ba27ec293c361cdf1c87406fe4d1f0005b8ba6fbe1287ffa" },
  { "assets/ui/menu-panel.png", 1254, 1254,
    "179d84c501384fc88443a9a415a06195270306706eb7e4ca5dc68ed06043f2a0" },
  { "assets/ui/menu-focus.png", 2172, 724,
    "65b98e03aaba9f26edc36ffa0e769e2932b5b85ea994a09447edf3bfc6ba4312" },
  { "assets/ui/main-menu-background.png", 1672, 941,
    "53630f6ca2d86492e983f7e54b54ce913cbebe58df6bd847c316753ef306a718" },
  { "assets/ui/title.png", 1536, 1024,
    "ca15d94fcdcd748449eddb73dd280a8af267f9d7d3aadd036de6e704f5988708" },
};
