/* Port of src/network/services/download-types.ts. GPL-2.0-or-later. */
#include "qa/network/services/download_types.h"
#include <string.h>
/* /\.(?:bsp|ent|lit|loc|mdl|md2|md3|spr|sp2|lmp|wal|pcx|tga|png|jpg|jpeg|bmp|gif|wav|ogg|cin|dm2|txt)$/i */
bool qa_is_served_download_type(const char *path) {
  static const char *const extensions[] = {"bsp", "ent", "lit", "loc", "mdl", "md2", "md3", "spr", "sp2", "lmp", "wal",
    "pcx", "tga", "png", "jpg", "jpeg", "bmp", "gif", "wav", "ogg", "cin", "dm2", "txt"};
  size_t length = strlen(path);
  for (size_t i = 0; i < sizeof extensions / sizeof *extensions; i++) {
    size_t n = strlen(extensions[i]);
    if (length < n + 1 || path[length - n - 1] != '.') continue;
    bool same = true;
    for (size_t j = 0; j < n && same; j++) {
      char c = path[length - n + j];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      same = c == extensions[i][j];
    }
    if (same) return true;
  }
  return false;
}
