/* Port of src/app/bootstrap/server-map.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_MAP_H
#define QA_APP_BOOTSTRAP_SERVER_MAP_H
/* TS serverMapPath(name): a map name supplied by a server as a "maps/<name>.bsp"
 * resource path. Malloc'd; raises what normalizeResourcePath raises. */
char *qa_app_server_map_path(const char *name);
#endif
