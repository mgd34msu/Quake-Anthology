#ifndef QA_CATALOG_H
#define QA_CATALOG_H

#include "qa/session.h"
#include "qa/filesystem.h"
#include "qa/vfs.h"
#include "qa/builtin.h"

typedef struct qa_catalog qa_catalog;
typedef uint32_t qa_product_id;
#define QA_PRODUCT_NONE 0u
typedef enum qa_product_edition {
    QA_EDITION_CLASSIC, QA_EDITION_RERELEASE, QA_EDITION_QUAKEWORLD, QA_EDITION_DEMO
} qa_product_edition;
typedef enum qa_content_availability {
    QA_CONTENT_INSTALLED, QA_CONTENT_MISSING, QA_CONTENT_INVALID
} qa_content_availability;
typedef enum qa_program_kind {
    QA_PROGRAM_BUILTIN, QA_PROGRAM_QUAKEC, QA_PROGRAM_QVM, QA_PROGRAM_NATIVE
} qa_program_kind;

typedef struct qa_product {
    qa_product_id id, base;
    const char *key, *identity, *title, *campaign, *directory;
    qa_game_family family;
    qa_product_edition edition;
    qa_content_availability availability;
    bool builtin;
    const char *const *requirements;
    size_t requirement_count;
    /* Installed original artifact, independent of the default execution kind.
     * Stock products default to built-in execution even when this is present. */
    const char *program;
    qa_program_kind program_kind;
    qa_product_id program_product;
} qa_product;
typedef struct qa_catalog_mount {
    /* Catalog identity; scoped VFS views have their own mount identities. */
    qa_mount_id id;
    const char *path;
    qa_archive_kind format;
    bool writable;
    /* NULL until the actual archive payload has been acquired. */
    const qa_sha256_digest *digest;
} qa_catalog_mount;
typedef struct qa_catalog_map {
    const char *path;
    qa_mount_id mount;
    size_t member;
    bool archived;
} qa_catalog_map;
typedef struct qa_catalog_start {
    const char *episode, *bsp, *path, *title, *start_items;
    bool singleplayer, cooperative, capture_the_flag;
} qa_catalog_start;
typedef struct qa_catalog_episode {
    const char *id, *command, *name, *activity;
    bool needs_skill_select;
} qa_catalog_episode;
typedef enum qa_mod_purpose { QA_MOD_ADDITION, QA_MOD_GAME_TYPE } qa_mod_purpose;
typedef struct qa_catalog_mod {
    qa_product_id product;
    const char *key, *id, *title;
    qa_mod_purpose purpose;
    qa_program_kind runtime;
    const char *const *requires, *const *conflicts;
    size_t requires_count, conflicts_count;
    const char *declaration_path, *program_path;
    qa_sha256_digest declaration_digest, program_digest;
    qa_bytes declaration;
    const char *unavailable;
    /* A matching digest is discovery evidence. B25 must qualify the complete
     * declaration against the program before constructing its private state. */
} qa_catalog_mod;
typedef struct qa_catalog_weapon_behavior {
    qa_product_id product;
    const char *id, *title, *artifact_path;
    qa_program_kind runtime;
    qa_builtin_projectile_role role;
    const char *declaration_path;
    qa_sha256_digest declaration_digest, artifact_digest;
    /* Exact detached entry bytes; B25 qualifies its
     * source functions and fields before it is executable. */
    qa_bytes entry;
    const char *unavailable;
} qa_catalog_weapon_behavior;

typedef struct qa_catalog_options {
    qa_resource_pool *resources;
    const char *content_root, *user_root;
    /* Additional physical search locations; product directory names remain
     * logical identities independent of native install folder names. */
    const char *const *install_roots;
    size_t install_root_count;
    uint64_t generation;
    bool discover_mods;
} qa_catalog_options;
bool qa_catalog_discover(const qa_catalog_options *, qa_catalog **, qa_error *);
/* Safe normal-loop discovery, also used after a completed remote download.
 * Builds a fresh snapshot from the configured roots, creates the actual Q3
 * selected/base writable directories and admits an empty selected directory.
 * The existing snapshot and its product/source views remain unchanged. */
bool qa_catalog_discover_remote_q3(const qa_catalog *, qa_product_id base,
    const char *directory, uint64_t generation, qa_catalog **out,
    qa_product_id *selected, qa_error *);
/* Q2 serverdata uses the actual configured classic/rerelease base. Empty
 * gamedir selects that base; an unknown safe name creates a real overlay.
 * Native downloads use product_write_root for selected and base directories. */
bool qa_catalog_discover_remote_q2(const qa_catalog *, qa_product_id base,
    const char *directory, uint64_t generation, qa_catalog **out,
    qa_product_id *selected, qa_error *);
/* Uses the actual selected Q1/QW base's family directory and configured user
 * capability, including QW's genuine id1 ancestry. */
bool qa_catalog_discover_remote_q1(const qa_catalog *, qa_product_id base,
    const char *directory, uint64_t generation, qa_catalog **out,
    qa_product_id *selected, qa_error *);
/* Actual configured Q2 family capability for HTTP game-relative destinations.
 * Only classic/rerelease editions are admitted; retain before catalog release. */
qa_fs_root *qa_catalog_q2_download_root(const qa_catalog *, qa_product_edition);
/* Borrows the real configured Q3 family download capability. Retain the root
 * independently when it must outlive the catalog. NULL means no write owner. */
qa_fs_root *qa_catalog_q3_download_root(const qa_catalog *);
/* Original $modlist pairs from the retained Q3 home/base directories. Raw PK3
 * presence admits undiscovered and invalid packages; descriptions keep the
 * source's first 48 bytes. The caller owns the returned alternating strings. */
bool qa_catalog_q3_mod_list(const qa_catalog *, qa_vfs_listing *, qa_error *);
void qa_catalog_retain(qa_catalog *);
void qa_catalog_release(qa_catalog *);
uint64_t qa_catalog_generation(const qa_catalog *);
size_t qa_catalog_count(const qa_catalog *);
const qa_product *qa_catalog_at(const qa_catalog *, size_t);
const qa_product *qa_catalog_product(const qa_catalog *, qa_product_id);
/* Original discovered configuration ancestry and writable authority survive
 * Q3 media restriction. These borrows never reopen or derive a native root. */
qa_product_id qa_catalog_configuration_base(const qa_catalog *, qa_product_id);
const qa_catalog_mount *qa_catalog_product_write_mount(const qa_catalog *, qa_product_id);
qa_fs_root *qa_catalog_product_write_root(const qa_catalog *, qa_product_id);
/* Original corpus directory authority, including a shared writable corpus.
 * NULL means discovery admitted no loose directory for this product. */
const qa_catalog_mount *qa_catalog_product_loose_mount(const qa_catalog *, qa_product_id);
qa_fs_root *qa_catalog_product_loose_root(const qa_catalog *, qa_product_id);
/* Actual discovery-owned install family of this Q1 product (or its reached
 * installed base). The family never enters the ordinary product search path. */
const qa_catalog_mount *qa_catalog_product_family_mount(const qa_catalog *,qa_product_id);
qa_fs_root *qa_catalog_product_family_root(const qa_catalog *,qa_product_id);
qa_fs_root *qa_catalog_corpus_root(const qa_catalog *);
/* Accepts the product key or its persistent family:edition:package identity. */
const qa_product *qa_catalog_find(const qa_catalog *, const char *);
const qa_catalog_mount *qa_catalog_mount_at(const qa_catalog *, size_t);
/* Acquires the actual package snapshot; discovery metadata alone has no digest. */
bool qa_catalog_mount_digest_read(const qa_catalog *, qa_mount_id,
    const qa_sha256_digest **, qa_error *);
size_t qa_catalog_mount_count(const qa_catalog *);
bool qa_catalog_product_mounts(const qa_catalog *, qa_product_id,
                               const qa_mount_id **, size_t *);
/* Actual discovered physical mounts owned by this product, excluding inherited
 * base search paths. Borrows have the same catalog lifetime as product_mounts. */
bool qa_catalog_product_own_mounts(const qa_catalog *, qa_product_id,
                                   const qa_mount_id **, size_t *);
const qa_catalog_map *qa_catalog_maps(const qa_catalog *, qa_product_id, size_t *);
const qa_catalog_start *qa_catalog_starts(const qa_catalog *, qa_product_id,
                                         const qa_catalog_episode **, size_t *);
size_t qa_catalog_mod_count(const qa_catalog *);
const qa_catalog_mod *qa_catalog_mod_at(const qa_catalog *, size_t);
const qa_catalog_mod *qa_catalog_mod_find(const qa_catalog *, const char *key);
size_t qa_catalog_weapon_behavior_count(const qa_catalog *);
const qa_catalog_weapon_behavior *qa_catalog_weapon_behavior_at(const qa_catalog *, size_t);
const qa_catalog_weapon_behavior *qa_catalog_weapon_behavior_find(const qa_catalog *,
                                                                  qa_product_id, const char *id);
bool qa_catalog_mod_key(const char *key);
/* A scoped view includes only this product and its base, with native search
 * precedence. It retains pool resources independently of the catalog. */
bool qa_catalog_open(const qa_catalog *, qa_product_id, qa_vfs **, qa_error *);
/* Pure qualification against the retained product recipe. Does not recreate
 * a view, reopen native paths or change resource/read ownership. */
bool qa_catalog_product_view_current(const qa_catalog *, qa_product_id, const qa_vfs *);
/* Resolve an actual scoped-view mount through its retained catalog recipe.
 * The content owner follows the selected product's original ancestry, so an
 * inherited base mount and a selected variant retain their proper identity. */
bool qa_catalog_product_mount_origin(const qa_catalog *, qa_product_id,
    const qa_vfs *, qa_mount_id, qa_product_id *content, qa_mount_id *catalog_mount);
/* Qualify an issued acquisition against the unchanged original scoped view. */
bool qa_catalog_product_acquisition_origin(const qa_catalog *, qa_product_id,
    const qa_vfs *, const qa_vfs_acquisition *, qa_product_id *, qa_mount_id *, qa_error *);
/* Initial files.c identity lookup uses actual discovered Q3 mounts even when
 * the selected retail package lacks other required files. */
bool qa_catalog_q3_identification_open(const qa_catalog *, qa_product_id, qa_vfs **, qa_error *);
/* Before drafts retain a fresh catalog, replace every Q3 product's media with
 * genuine demota while preserving its selected gameplay/product identity. */
bool qa_catalog_q3_restrict(qa_catalog *, qa_error *);
bool qa_catalog_q3_restricted(const qa_catalog *);
typedef struct qa_catalog_mount_selection {
    qa_product_id assets, geometry, combat;
    bool explicit_presentation;
    const qa_product_id *additional;
    size_t additional_count;
} qa_catalog_mount_selection;
bool qa_catalog_mount_plan(const qa_catalog *, const qa_catalog_mount_selection *,
                            qa_vfs **, qa_error *);
/* Network game directories are a single safe name. This maps known products;
 * callers rediscover a newly downloaded package before selecting it. */
bool qa_catalog_remote(const qa_catalog *, qa_product_id base, const char *directory,
                        qa_product_id *out, qa_error *);

#endif
