cmake_minimum_required(VERSION 3.20)
project(quake_anthology VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QA_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(QA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(qa_compile_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qa_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes
        -ffp-contract=off)
    if(QA_WARNINGS_AS_ERRORS)
        target_compile_options(qa_compile_options INTERFACE -Werror)
    endif()
    if(QA_SANITIZERS)
        target_compile_options(qa_compile_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(qa_compile_options INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

option(QA_NATIVE_HELPER_ONLY "Configure only the target-ABI native compatibility helper" OFF)
include(cmake/Native.cmake)
if(QA_NATIVE_HELPER_ONLY)
    return()
endif()
include(cmake/NativeBundle.cmake)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(CURL 7.85 REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)
find_package(SDL2 2.0.18 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg)
pkg_check_modules(THEORADEC REQUIRED IMPORTED_TARGET theoradec)
find_library(QA_GIF_LIBRARY NAMES gif REQUIRED)

add_library(qa_core STATIC
    src/core/arena.c
    src/core/common_parse.c
    src/core/json_writer.c
    src/core/strings.c
    src/core/text.c
    src/core/tokenizer.c
    src/core/normals.c)
target_include_directories(qa_core PUBLIC include)
target_link_libraries(qa_core PUBLIC qa_data PRIVATE ICU::uc)
target_compile_definitions(qa_core PRIVATE _POSIX_C_SOURCE=200809L)

add_library(qa_script STATIC
    src/core/script/checkpoint.c
    src/core/script/codec.c
    src/core/script/defines.c
    src/core/script/defines_save.c
    src/core/script/directives.c
    src/core/script/expansion.c
    src/core/script/expression.c
    src/core/script/lexer.c
    src/core/script/number.c
    src/core/script/quoted.c
    src/core/script/read.c
    src/core/script/source.c)
target_link_libraries(qa_script PUBLIC qa_core qa_persistence)

add_library(qa_content STATIC
    src/content/archive.c
    src/content/vfs.c
    src/content/resource_pool_save.c
    src/content/vfs_view_save.c
    src/formats/bsp.c
    src/formats/image/image.c
    src/formats/image/indexed.c
    src/formats/image/raster.c
    src/formats/image/png.c
    src/formats/image/jpeg.c
    src/formats/image/gif.c
    src/formats/image/wad.c
    src/formats/model/model.c
    src/formats/model/alias.c
    src/formats/model/md3.c
    src/formats/model/md4.c
    src/formats/model/md5.c
    src/formats/model/text.c
    src/formats/model/metadata.c
    src/formats/model/scales.c
    src/formats/model/lod.c
    src/formats/model/transform.c
    src/formats/image/q3_color.c
    src/formats/image/q3_upload.c
    src/content/map_sidecars.c
    src/content/map_sidecars_save.c
)
target_link_libraries(qa_content PUBLIC qa_core PRIVATE
    ZLIB::ZLIB PNG::PNG JPEG::JPEG ${QA_GIF_LIBRARY})

add_library(qa_navigation_assets STATIC
    src/navigation/asset.c
    src/navigation/aas_file.c
    src/navigation/aas_validate.c
    src/navigation/aas_sample.c
    src/navigation/nav_file.c)
target_link_libraries(qa_navigation_assets PUBLIC qa_core)

add_library(qa_world STATIC
    src/world/actors.c
    src/world/body.c
    src/world/checkpoint.c
    src/world/collision/contents.c
    src/world/collision/geometry.c
    src/world/collision/q1.c
    src/world/collision/q1/geometry.c
    src/world/collision/q2.c
    src/world/collision/q3.c
    src/world/collision/q3/model.c
    src/world/collision/q3/patch.c
    src/world/collision/world.c
    src/world/spatial.c)
target_link_libraries(qa_world PUBLIC qa_content)

add_library(qa_session STATIC src/session/session.c
    src/session/scheduler.c)
target_link_libraries(qa_session PUBLIC qa_world)

add_library(qa_console STATIC
    src/core/q3_key.c
    src/console/program.c
    src/console/release.c
    src/console/text.c
    src/console/frame_time.c
    src/console/cvars.c
    src/console/commands.c
    src/console/field.c
    src/console/source_field.c
    src/console/buffer.c
    src/console/documentation.c
    src/console/discovery.c
    src/console/seat.c
    src/console/seat_save.c
    src/console/dedicated.c
    src/platform/console.c)
target_link_libraries(qa_console PUBLIC qa_core)

add_library(qa_text STATIC
    src/text/q1_text.c
    src/text/localization.c
    src/text/captions.c
    src/text/media_captions.c
    src/text/font/library.c
    src/text/font/save.c
    src/text/font/kfont.c
    src/text/font/freetype.c
    src/text/font/q3.c
    src/text/font/layout.c
    src/text/font/ui.c
    src/text/font/world.c
    src/text/font/console.c)
target_link_libraries(qa_text PUBLIC qa_content qa_audio qa_scene qa_console
    qa_campaign
    PRIVATE Freetype::Freetype)

add_library(qa_qc STATIC
    src/compat/qc/program.c
    src/compat/qc/memory.c
    src/compat/qc/machine.c
    src/compat/qc/builtins.c
    src/compat/qc/instance.c
    src/compat/qc/visual.c
    src/compat/qc/weapon_visual.c
    src/compat/qc/checkpoint.c
    src/compat/qc/text_save.c
    src/compat/qc/observation.c
)
target_link_libraries(qa_qc PUBLIC qa_session qa_gameplay qa_persistence)

add_library(qa_gameplay STATIC
    src/gameplay/operation.c
    src/gameplay/armor.c
    src/gameplay/combat.c
    src/gameplay/inventory.c
    src/gameplay/pickups.c
    src/gameplay/policies.c
    src/gameplay/provenance.c)
target_link_libraries(qa_gameplay PUBLIC qa_world)

add_library(qa_movement STATIC
    src/movement/common.c
    src/movement/prediction.c
    src/movement/entity.c
    src/movement/entity/monsters.c
    src/movement/entity/pushers.c
    src/movement/entity/q3_mover.c
    src/movement/entity/trajectory.c
    src/movement/q1/common.c
    src/movement/q1/netquake.c
    src/movement/q1/quakeworld.c
    src/movement/q2/classic.c
    src/movement/q2/rerelease.c
    src/movement/q3/move.c
    src/movement/q3/slide.c)
target_link_libraries(qa_movement PUBLIC qa_session)

add_library(qa_navigation STATIC
    src/navigation/checkpoint.c
    src/navigation/construct.c
    src/navigation/eligibility.c
    src/navigation/estimates.c
    src/navigation/graph.c
    src/navigation/graph_asset.c
    src/navigation/prediction.c
    src/navigation/route.c
    src/navigation/runtime.c
    src/navigation/spatial.c
    src/navigation/train.c
    src/navigation/travel.c
    src/navigation/world.c)
target_link_libraries(qa_navigation PUBLIC qa_navigation_assets qa_movement)

add_library(qa_builtin STATIC
    src/gameplay/builtin/services.c
    src/gameplay/builtin/random.c
    src/gameplay/builtin/attacks.c)
target_link_libraries(qa_builtin PUBLIC qa_session qa_gameplay qa_movement)

add_library(qa_game_q1 STATIC
    src/gameplay/q1/bot_source.c
    src/gameplay/q1/source_clients.c
    src/gameplay/q1/source_composition.c
    src/gameplay/q1/source_entities.c
    src/gameplay/q1/source_flags.c
    src/gameplay/q1/source_obituary.c
    src/gameplay/q1/source_rogue.c
    src/gameplay/q1/source_runes.c
    src/gameplay/q1/addon_commands.c
    src/gameplay/q1/addon_monsters.c
    src/gameplay/q1/armagon.c
    src/gameplay/q1/boss_children.c
    src/gameplay/q1/boss_final.c
    src/gameplay/q1/boss_oldnew.c
    src/gameplay/q1/boss_projectiles.c
    src/gameplay/q1/boss_small.c
    src/gameplay/q1/character.c
    src/gameplay/q1/charm.c
    src/gameplay/q1/checkpoint/codec.c
    src/gameplay/q1/checkpoint/entity.c
    src/gameplay/q1/checkpoint/io.c
    src/gameplay/q1/checkpoint/map.c
    src/gameplay/q1/checkpoint/monster.c
    src/gameplay/q1/checkpoint/player.c
    src/gameplay/q1/checkpoint/runtime.c
    src/gameplay/q1/checkpoint/shared.c
    src/gameplay/q1/checkpoint/wire.c
    src/gameplay/q1/clone.c
    src/gameplay/q1/console.c
    src/gameplay/q1/demodog.c
    src/gameplay/q1/dragon.c
    src/gameplay/q1/drops.c
    src/gameplay/q1/electric.c
    src/gameplay/q1/expansion_weapons.c
    src/gameplay/q1/explosions.c
    src/gameplay/q1/frames.c
    src/gameplay/q1/freeze.c
    src/gameplay/q1/grapple.c
    src/gameplay/q1/finale.c
    src/gameplay/q1/grapple_weapon.c
    src/gameplay/q1/gremlin.c
    src/gameplay/q1/gremlin_ai.c
    src/gameplay/q1/gremlin_weapons.c
    src/gameplay/q1/heavy.c
    src/gameplay/q1/hipnotic_weapons.c
    src/gameplay/q1/horde.c
    src/gameplay/q1/infected.c
    src/gameplay/q1/inventory.c
    src/gameplay/q1/lavaman.c
    src/gameplay/q1/maps/addon_brushes.c
    src/gameplay/q1/maps/addon_campaign.c
    src/gameplay/q1/maps/addon_fog.c
    src/gameplay/q1/maps/addon_controls.c
    src/gameplay/q1/maps/addon_effects.c
    src/gameplay/q1/maps/addon_fields.c
    src/gameplay/q1/maps/addon_horde.c
    src/gameplay/q1/maps/addon_misc.c
    src/gameplay/q1/maps/addon_triggers.c
    src/gameplay/q1/maps/addon_visuals.c
    src/gameplay/q1/maps/bosses.c
    src/gameplay/q1/maps/ctf.c
    src/gameplay/q1/maps/hipnotic_brushes.c
    src/gameplay/q1/maps/hipnotic_hazards.c
    src/gameplay/q1/maps/hipnotic_misc.c
    src/gameplay/q1/maps/hipnotic_particles.c
    src/gameplay/q1/maps/hipnotic_rotation.c
    src/gameplay/q1/maps/hipnotic_spawn.c
    src/gameplay/q1/maps/hipnotic_triggers.c
    src/gameplay/q1/maps/movers.c
    src/gameplay/q1/maps/mg3_commands.c
    src/gameplay/q1/maps/navigation.c
    src/gameplay/q1/maps/observer.c
    src/gameplay/q1/maps/paths.c
    src/gameplay/q1/maps/rogue_ending.c
    src/gameplay/q1/maps/rogue_hazards.c
    src/gameplay/q1/maps/rogue_misc.c
    src/gameplay/q1/maps/rogue_pendulum.c
    src/gameplay/q1/maps/rogue_plats.c
    src/gameplay/q1/maps/rogue_time.c
    src/gameplay/q1/maps/runtime.c
    src/gameplay/q1/maps/special.c
    src/gameplay/q1/maps/trains.c
    src/gameplay/q1/maps/triggers.c
    src/gameplay/q1/mg3_commands.c
    src/gameplay/q1/mg3_progress.c
    src/gameplay/q1/mg3_weapons.c
    src/gameplay/q1/mission_monsters.c
    src/gameplay/q1/monster_actions.c
    src/gameplay/q1/monster_lifecycle.c
    src/gameplay/q1/monsters.c
    src/gameplay/q1/morph.c
    src/gameplay/q1/obituary.c
    src/gameplay/q1/overlord.c
    src/gameplay/q1/pickups.c
    src/gameplay/q1/powers.c
    src/gameplay/q1/projectiles.c
    src/gameplay/q1/queries.c
    src/gameplay/q1/rocket_ogre.c
    src/gameplay/q1/rogue_weapons.c
    src/gameplay/q1/runtime.c
    src/gameplay/q1/scourge.c
    src/gameplay/q1/selection.c
    src/gameplay/q1/species.c
    src/gameplay/q1/source_travel.c
    src/gameplay/q1/source_travel_state.c
    src/gameplay/q1/source_birth.c
    src/gameplay/q1/source_powers.c
    src/gameplay/q1/sphere_points.c
    src/gameplay/q1/teleport.c
    src/gameplay/q1/weapons.c
    src/gameplay/q1/wire.c
    src/gameplay/q1/wrath.c
    src/gameplay/q1/source_rogue_tag.c
    src/gameplay/q1/source_rogue_flags.c
    src/gameplay/q1/source_rogue_runes.c
    src/gameplay/q1/player_ui.c
)
target_link_libraries(qa_game_q1 PUBLIC qa_builtin qa_campaign qa_persistence)

add_library(qa_game_q2 STATIC
    src/gameplay/q2/bot_source.c
    src/gameplay/q2/wire.c
    src/gameplay/q2/save/wire.c
    src/gameplay/q2/player/rotation.c
    src/gameplay/q2/console.c
    src/gameplay/q2/ballistics.c
    src/gameplay/q2/checkpoint.c
    src/gameplay/q2/combat_policy.c
    src/gameplay/q2/save/io.c
    src/gameplay/q2/save/common.c
    src/gameplay/q2/save/items.c
    src/gameplay/q2/save/player.c
    src/gameplay/q2/save/entities.c
    src/gameplay/q2/save/monsters.c
    src/gameplay/q2/save/game.c
    src/gameplay/q2/definitions.c
    src/gameplay/q2/entities/brushes.c
    src/gameplay/q2/entities/checkpoint.c
    src/gameplay/q2/entities/doors.c
    src/gameplay/q2/entities/goals.c
    src/gameplay/q2/entities/killbox.c
    src/gameplay/q2/entities/lifecycle.c
    src/gameplay/q2/entities/lights.c
    src/gameplay/q2/entities/motion.c
    src/gameplay/q2/entities/movers.c
    src/gameplay/q2/entities/navigation.c
    src/gameplay/q2/entities/q64.c
    src/gameplay/q2/entities/rerelease.c
    src/gameplay/q2/entities/routes.c
    src/gameplay/q2/entities/scenery.c
    src/gameplay/q2/entities/state.c
    src/gameplay/q2/entities/target_effects.c
    src/gameplay/q2/entities/targets.c
    src/gameplay/q2/entities/teleport.c
    src/gameplay/q2/entities/trains.c
    src/gameplay/q2/entities/triggers.c
    src/gameplay/q2/entities/turrets.c
    src/gameplay/q2/game.c
    src/gameplay/q2/source.c
    src/gameplay/q2/alpha.c
    src/gameplay/q2/gibs.c
    src/gameplay/q2/grapple.c
    src/gameplay/q2/grapple_weapon.c
    src/gameplay/q2/hand_grenades.c
    src/gameplay/q2/items/catalog.c
    src/gameplay/q2/items/checkpoint.c
    src/gameplay/q2/items/companions.c
    src/gameplay/q2/items/drop.c
    src/gameplay/q2/items/grants.c
    src/gameplay/q2/items/lifecycle.c
    src/gameplay/q2/items/observation.c
    src/gameplay/q2/items/powers.c
    src/gameplay/q2/items/quad.c
    src/gameplay/q2/items/random.c
    src/gameplay/q2/items/touch.c
    src/gameplay/q2/lmctf_plasma.c
    src/gameplay/q2/map.c
    src/gameplay/q2/mines.c
    src/gameplay/q2/mission_bolts.c
    src/gameplay/q2/monsters/actions.c
    src/gameplay/q2/monsters/ai.c
    src/gameplay/q2/monsters/beam.c
    src/gameplay/q2/monsters/checkpoint.c
    src/gameplay/q2/monsters/combat.c
    src/gameplay/q2/monsters/core.c
    src/gameplay/q2/monsters/corpse.c
    src/gameplay/q2/monsters/lifecycle.c
    src/gameplay/q2/monsters/medic.c
    src/gameplay/q2/monsters/medic_blocked.c
    src/gameplay/q2/monsters/melee.c
    src/gameplay/q2/monsters/moves.c
    src/gameplay/q2/monsters/proboscis.c
    src/gameplay/q2/monsters/reinforcements.c
    src/gameplay/q2/monsters/roster.c
    src/gameplay/q2/monsters/spawn.c
    src/gameplay/q2/monsters/spawn_growth.c
    src/gameplay/q2/monsters/stalker.c
    src/gameplay/q2/monsters/stalker_moves.c
    src/gameplay/q2/monsters/widow_death.c
    src/gameplay/q2/monsters/widow_power.c
    src/gameplay/q2/nuke.c
    src/gameplay/q2/physics.c
    src/gameplay/q2/player/admission.c
    src/gameplay/q2/player/checkpoint.c
    src/gameplay/q2/player/character.c
    src/gameplay/q2/player/feedback.c
    src/gameplay/q2/player/commands.c
    src/gameplay/q2/player/death.c
    src/gameplay/q2/player/environment.c
    src/gameplay/q2/player/equipment.c
    src/gameplay/q2/player/frame.c
    src/gameplay/q2/player/intermission.c
    src/gameplay/q2/player/map_spawns.c
    src/gameplay/q2/player/obituary.c
    src/gameplay/q2/player/spawn.c
    src/gameplay/q2/player/spawn_select.c
    src/gameplay/q2/player/spectator.c
    src/gameplay/q2/player/state.c
    src/gameplay/q2/player/view.c
    src/gameplay/q2/projectiles.c
    src/gameplay/q2/random.c
    src/gameplay/q2/weapon_fire.c
    src/gameplay/q2/weapon_frame.c
    src/gameplay/q2/weapon_presentation.c
    src/gameplay/q2/weapon_state.c
    src/gameplay/q2/weapon_throw.c
    src/gameplay/q2/player/preferences.c
    src/gameplay/q2/player/animation_read.c
    src/gameplay/q2/campaign.c
)
target_link_libraries(qa_game_q2 PUBLIC qa_builtin qa_campaign qa_persistence)

add_library(qa_game_q3 STATIC
    src/gameplay/q3/alpha.c
    src/gameplay/q3/checkpoint.c
    src/gameplay/q3/client_begin.c
    src/gameplay/q3/client_commands.c
    src/gameplay/q3/client_items.c
    src/gameplay/q3/console.c
    src/gameplay/q3/configstrings.c
    src/gameplay/q3/death.c
    src/gameplay/q3/feedback.c
    src/gameplay/q3/game.c
    src/gameplay/q3/mode_effects.c
    src/gameplay/q3/holdables.c
    src/gameplay/q3/items.c
    src/gameplay/q3/inventory.c
    src/gameplay/q3/map/checkpoint.c
    src/gameplay/q3/map/items.c
    src/gameplay/q3/map/misc.c
    src/gameplay/q3/map/movers.c
    src/gameplay/q3/map/runtime.c
    src/gameplay/q3/map/spawn.c
    src/gameplay/q3/map/targets.c
    src/gameplay/q3/map/triggers.c
    src/gameplay/q3/missiles.c
    src/gameplay/q3/movers.c
    src/gameplay/q3/player.c
    src/gameplay/q3/rankings.c
    src/gameplay/q3/round.c
    src/gameplay/q3/save.c
    src/gameplay/q3/shader_remap.c
    src/gameplay/q3/source_entities.c
    src/gameplay/q3/source_effects.c
    src/gameplay/q3/source_supply.c
    src/gameplay/q3/source_objectives.c
    src/gameplay/q3/source_wire.c
    src/gameplay/q3/source_postgame.c
    src/gameplay/q3/view.c
    src/gameplay/q3/weapons.c)
target_link_libraries(qa_game_q3 PUBLIC qa_builtin qa_persistence)

add_library(qa_player_services STATIC
    src/player_services/arena_progress.c
    src/player_services/arena_catalog.c
    src/player_services/team_arena_progress.c
    src/player_services/team_arena_campaign.c
    src/player_services/lobby.c
    src/player_services/lobby_session.c
    src/player_services/rankings.c
    src/player_services/progress.c
    src/player_services/progress_save.c
    src/player_services/progress_file.c)
target_link_libraries(qa_player_services PUBLIC qa_core qa_console qa_network qa_persistence)

add_library(qa_campaign STATIC
    src/campaign/travel.c
    src/campaign/unit.c
    src/campaign/targets.c
    src/campaign/q1/level.c
    src/campaign/q1/finales.c
    src/campaign/q1/spawn.c
    src/campaign/q1/sources.c)
target_link_libraries(qa_campaign PUBLIC qa_session qa_builtin)

add_library(qa_modes STATIC
    src/gameplay/modes/arena.c
    src/gameplay/modes/checkpoint.c
    src/gameplay/modes/commands.c
    src/gameplay/modes/core.c
    src/gameplay/modes/damage.c
    src/gameplay/modes/deathball.c
    src/gameplay/modes/equipment.c
    src/gameplay/modes/save.c
    src/gameplay/modes/flags.c
    src/gameplay/modes/give.c
    src/gameplay/modes/horde.c
    src/gameplay/modes/items.c
    src/gameplay/modes/map.c
    src/gameplay/modes/match.c
    src/gameplay/modes/q3_settings.c
    src/gameplay/modes/q3_session.c
    src/gameplay/modes/q3_objective_source.c
    src/gameplay/modes/objectives.c
    src/gameplay/modes/objects.c
    src/gameplay/modes/placement.c
    src/gameplay/modes/relics.c
    src/gameplay/modes/rogue_tag.c
    src/gameplay/modes/scoring.c
    src/gameplay/modes/tag.c
    src/gameplay/modes/team_info.c
    src/gameplay/modes/teams.c
    src/gameplay/modes/vote.c)
target_link_libraries(qa_modes PUBLIC qa_game_q1 qa_game_q2 qa_game_q3)

add_library(qa_bots STATIC
    src/bots/actions.c
    src/bots/actions_save.c
    src/bots/ai/checkpoint.c
    src/bots/ai/chat.c
    src/bots/ai/combat.c
    src/bots/ai/console.c
    src/bots/ai/decision.c
    src/bots/ai/frame.c
    src/bots/ai/orders.c
    src/bots/ai/reset.c
    src/bots/ai/roster.c
    src/bots/ai/save.c
    src/bots/ai/session.c
    src/bots/ai/source_orders.c
    src/bots/ai/source_inventory.c
    src/bots/ai/source_command.c
    src/bots/ai/source_player.c
    src/bots/ai/source_setup.c
    src/bots/ai/source_match.c
    src/bots/ai/source_memory.c
    src/bots/ai/source_alias.c
    src/bots/ai/source_library.c
    src/bots/ai/source_storage.c
    src/bots/ai/source_chat.c
    src/bots/ai/source_events.c
    src/bots/ai/source_goal.c
    src/bots/ai/source_team_policy.c
    src/bots/ai/source_report.c
    src/bots/ai/team.c
    src/bots/chat/asset.c
    src/bots/chat/asset_save.c
    src/bots/chat/checkpoint.c
    src/bots/chat/construct.c
    src/bots/chat/integrity.c
    src/bots/chat/log.c
    src/bots/chat/parse.c
    src/bots/chat/reply_parse.c
    src/bots/chat/state.c
    src/bots/chat/system_save.c
    src/bots/chat/strings.c
    src/bots/catalog/commands.c
    src/bots/catalog/save.c
    src/bots/catalog/state.c
    src/bots/catalog/text.c
    src/bots/goals/choose.c
    src/bots/goals/checkpoint.c
    src/bots/goals/core.c
    src/bots/goals/map.c
    src/bots/goals/report.c
    src/bots/goals/save.c
    src/bots/goals/source_weights.c
    src/bots/goals/source_state.c
    src/bots/knowledge.c
    src/bots/library/bsp.c
    src/bots/library/bsp_save.c
    src/bots/library/character.c
    src/bots/library/character_log.c
    src/bots/library/core.c
    src/bots/library/genetic.c
    src/bots/library/items.c
    src/bots/library/assets_save.c
    src/bots/library/equipment_save.c
    src/bots/library/source_weapon_resource.c
    src/bots/library/source_weapon_parse.c
    src/bots/library/source_weapon_resource_save.c
    src/bots/library/source_weapon_resource_history.c
    src/bots/library/source_weapon_view.c
    src/bots/library/source_weapon_standalone.c
    src/bots/library/source_weapon_standalone_save.c
    src/bots/chat/source_initial.c
    src/bots/chat/source_initial_parse.c
    src/bots/chat/source_initial_resource.c
    src/bots/library/source_weapon_config.c
    src/bots/library/source_weapon_library.c
    src/bots/library/source_weapon_library_save.c
    src/bots/library/source_weapon_log.c
    src/bots/library/source_fuzzy.c
    src/bots/library/source_fuzzy_operations.c
    src/bots/library/source_fuzzy_parse.c
    src/bots/library/source_fuzzy_store.c
    src/bots/library/source_fuzzy_save.c
    src/bots/library/source_fuzzy_view.c
    src/bots/library/source_fuzzy_standalone.c
    src/bots/library/source_fuzzy_checkpoint.c
    src/bots/library/source_fuzzy_standalone_save.c
    src/bots/library/structure.c
    src/bots/library/variables.c
    src/bots/library/variables_save.c
    src/bots/library/weapons.c
    src/bots/library/weights.c
    src/bots/library/weights_mutate.c
    src/bots/movement/controller.c
    src/bots/memory/core.c
    src/bots/memory/checkpoint.c
    src/bots/memory/save.c
    src/bots/movement/direction.c
    src/bots/movement/ground.c
    src/bots/movement/ladder.c
    src/bots/movement/queries.c
    src/bots/movement/routing.c
    src/bots/movement/source.c
    src/bots/movement/special.c
    src/bots/movement/state.c
    src/bots/movement/save.c
    src/bots/movement/view.c
    src/bots/navigation/core.c
    src/bots/navigation/info.c
    src/bots/navigation/jump_pad.c
    src/bots/navigation/prediction.c
    src/bots/navigation/routes.c
    src/bots/navigation/source.c
    src/bots/perception.c
    src/bots/random.c
    src/bots/runtime/characteristics.c
    src/bots/runtime/core.c
    src/bots/runtime/source_weapon_state.c
    src/bots/runtime/source_weapon_history.c
    src/bots/runtime/source_weapon_save.c
    src/bots/runtime/source_weapon_setup.c
    src/bots/runtime/log.c
    src/bots/runtime/log_save.c
    src/bots/runtime/assets_save.c
    src/bots/runtime/save.c
    src/bots/runtime/requirements_save.c
    src/bots/runtime/handles.c
    src/bots/runtime/observations.c
    src/bots/runtime/observations_save.c
    src/bots/runtime/setup.c
    src/bots/runtime/variables.c
    src/bots/source.c
    src/bots/chat/source_initial_library.c
    src/bots/chat/source_initial_load.c
    src/bots/chat/source_initial_construct.c
    src/bots/chat/source_initial_save.c
    src/bots/chat/source_state.c
    src/bots/chat/source_history.c
    src/bots/chat/source_packed.c
    src/bots/chat/source_graph.c
    src/bots/library/character_reader.c
    src/bots/library/character_save.c
    src/bots/library/character_source.c
    src/bots/library/items_save.c
    src/bots/library/items_source.c
    src/bots/library/source_assets_history.c
)
target_link_libraries(qa_bots PUBLIC qa_builtin qa_navigation qa_script qa_content qa_persistence)

add_library(qa_catalog STATIC
    src/content/catalog/behaviors.c
    src/content/catalog/catalog.c
    src/content/catalog/discovery.c
    src/content/catalog/metadata.c
    src/content/catalog/mounts.c
    src/persistence/q1_product.c
    src/content/catalog/products.c)
target_sources(qa_catalog PRIVATE src/content/catalog/q3_product.c)
target_sources(qa_catalog PRIVATE src/content/catalog/q3_mods.c)
target_sources(qa_catalog PRIVATE src/content/catalog/q3_write.c)
target_sources(qa_catalog PRIVATE src/content/catalog/save.c)
target_link_libraries(qa_catalog PUBLIC qa_content qa_persistence)

add_library(qa_configuration STATIC
    src/session/configuration/draft.c
    src/session/configuration/draft_save.c
    src/session/configuration/identity.c
    src/session/configuration/native_metadata_save.c
    src/session/configuration/presets.c
    src/session/configuration/transaction.c
    src/session/configuration/validate.c)
target_link_libraries(qa_configuration PUBLIC qa_catalog qa_modes qa_session qa_persistence)

add_library(qa_audio STATIC
    src/audio/bank.c
    src/audio/bank_graph_save.c
    src/audio/bank_save.c
    src/audio/codecs.c
    src/audio/device.c
    src/audio/engine.c
    src/audio/engine_save.c
    src/audio/environments.c
    src/audio/mixer.c
    src/audio/mixer_save.c
    src/audio/music.c
    src/audio/output.c
    src/audio/reverb.c
    src/audio/streams.c
    src/audio/wavelet.c)
target_link_libraries(qa_audio PUBLIC qa_content PRIVATE SDL2::SDL2 PkgConfig::VORBISFILE)

add_library(qa_media STATIC
    src/media/source.c
    src/media/cin.c
    src/media/cin_playback.c
    src/media/roq_codebook.c
    src/media/roq_audio.c
    src/media/roq_stream.c
    src/media/roq_decoder.c
    src/media/roq_playback.c
    src/media/ogg.c
    src/media/ogv.c
    src/media/save.c
    src/media/cinematic.c
    src/media/cinematic_scene.c
    src/media/cinematic_presentation_save.c
    src/media/library.c
    src/media/library_prepare.c
    src/media/library_save.c
    src/media/material_movies.c
    src/media/material_movies_prepare.c
    src/media/material_movies_save.c)
target_link_libraries(qa_media PUBLIC qa_content qa_audio qa_scene qa_persistence
    PRIVATE PkgConfig::OGG PkgConfig::THEORADEC)

add_library(qa_scene STATIC
    src/render/scene/world/q1_sky.c
    src/render/scene/frame.c
    src/render/scene/frame_save.c
    src/render/scene/math.c
    src/render/scene/resources.c
    src/render/scene/geometry_save.c
    src/render/scene/resources_save.c
    src/render/scene/resources_state_save.c
    src/render/scene/sort.c
    src/render/scene/models.c
    src/render/scene/models/draw.c
    src/render/scene/models/images.c
    src/render/scene/models/lighting.c
    src/render/scene/models/owner_save.c
    src/render/scene/models/shadows.c
    src/render/scene/models/sprites.c
    src/render/scene/models/topology.c
    src/render/scene/world.c
    src/render/scene/world/owner_save.c
    src/render/scene/world/owner_core.c
    src/render/scene/world/owner_legacy.c
    src/render/scene/world/save.c
    src/render/scene/world/legacy.c
    src/render/scene/world/legacy/geometry.c
    src/render/scene/world/legacy/lighting.c
    src/render/scene/world/legacy/save.c
    src/render/scene/world/legacy/textures.c
    src/render/scene/world/q3.c
    src/render/scene/world/marks.c
    src/render/scene/world/q3/patch.c
    src/render/scene/effects.c
    src/render/scene/effects/particles.c
    src/render/scene/effects/primitives.c
    src/render/scene/effects/shadows.c
    src/render/scene/effects/sky.c
    src/render/scene/effects/stencil.c
    src/render/material/color.c
    src/render/material/coordinates.c
    src/render/material/deform.c
    src/render/material/library.c
    src/render/material/library_save.c
    src/render/material/record_save.c
    src/render/material/math.c
    src/render/material/order.c
    src/render/material/parser.c
    src/render/material/submit.c
    src/render/output_domain.c
)
target_link_libraries(qa_scene PUBLIC qa_world qa_persistence qa_q3_presentation)

add_library(qa_render_cpu STATIC
    src/render/cpu/renderer.c
    src/render/cpu/raster.c
    src/render/cpu/texture.c
    src/render/cpu/fragment.c
    src/render/cpu/fog.c)
target_link_libraries(qa_render_cpu PUBLIC qa_scene)

add_library(qa_display STATIC src/platform/display.c)
target_link_libraries(qa_display PUBLIC qa_core PRIVATE SDL2::SDL2)

add_library(qa_render_gl STATIC
    src/render/gl/api.c
    src/render/gl/program.c
    src/render/gl/resources.c
    src/render/gl/save.c
    src/render/gl/passes.c
    src/render/gl/renderer.c)
target_link_libraries(qa_render_gl PUBLIC qa_scene qa_display PRIVATE SDL2::SDL2)

add_library(qa_render_controls STATIC
    src/render/controls.c
    src/render/controls_save.c)
target_link_libraries(qa_render_controls PUBLIC qa_render_cpu qa_render_gl qa_persistence qa_q3_presentation)
target_link_libraries(qa_render_cpu PRIVATE qa_render_controls)
target_link_libraries(qa_render_gl PRIVATE qa_render_controls)

add_library(qa_input STATIC
    src/input/bindings.c
    src/input/buttons.c
    src/input/client_commands.c
    src/input/commands.c
    src/input/command_save.c
    src/input/gamepad.c
    src/input/haptics.c
    src/input/haptics_save.c
    src/input/keys.c
    src/input/mouse.c
    src/input/release.c
    src/input/seat.c
    src/input/save.c
    src/input/settings.c
    src/input/source_devices.c
    src/platform/input.c
    src/platform/input_save.c)
target_link_libraries(qa_input PUBLIC qa_movement qa_console qa_content qa_display qa_persistence SDL2::SDL2)

add_library(qa_settings STATIC
    src/settings/ui_language.c
    src/settings/codec.c
    src/settings/store.c
    src/settings/restart.c)
target_link_libraries(qa_settings PUBLIC qa_input)

add_library(qa_network STATIC
    src/network/admin/owner.c
    src/network/browser/owner.c
    src/network/browser/kex.c
    src/network/browser/q3.c
    src/network/browser/q3_save.c
    src/network/browser/persistence.c
    src/network/browser/wire.c
    src/network/connections.c
    src/network/dosbox.c
    src/network/downloads/owner.c
    src/network/downloads/window.c
    src/network/ipx.c
    src/network/interfaces.c
    src/network/interfaces_native.c
    src/network/interfaces_save.c
    src/network/message.c
    src/network/q1/channels.c
    src/network/q1/checksum.c
    src/network/q1/commands.c
    src/network/q1/demos.c
    src/network/q1/discovery.c
    src/network/q1/handshake.c
    src/network/q1/history.c
    src/network/q1/netquake.c
    src/network/q1/peer_save.c
    src/network/q1/quakeworld.c
    src/network/q1/qw_source.c
    src/network/q1/scalar.c
    src/network/q1/session.c
    src/network/q1/download_save.c
    src/network/q1/token.c
    src/network/q2/channel.c
    src/network/q2/bootstrap.c
    src/network/q2/checksum.c
    src/network/q2/codec.c
    src/network/q2/connectionless.c
    src/network/q2/fog.c
    src/network/q2/frames.c
    src/network/q2/gtv.c
    src/network/q2/handshake.c
    src/network/q2/kex_channel.c
    src/network/q2/kex_channel_save.c
    src/network/q2/kex_discovery.c
    src/network/q2/kex_discovery_native.c
    src/network/q2/kex_discovery_owner.c
    src/network/q2/kex_discovery_owner_save.c
    src/network/q2/kex_game.c
    src/network/q2/kex_lan.c
    src/network/q2/kex_lan_save.c
    src/network/q2/kex_packet.c
    src/network/q2/kex_save_address.c
    src/network/q2/kex_status.c
    src/network/q2/kex_transport.c
    src/network/q2/kex_transport_handoff.c
    src/network/q2/kex_transport_save.c
    src/network/q2/messages.c
    src/network/q2/mvd.c
    src/network/q2/q2pro.c
    src/network/q2/q2pro_fields.c
    src/network/q2/r1q2.c
    src/network/q2/rerelease.c
    src/network/q2/save_channel.c
    src/network/q2/save_fields.c
    src/network/q2/save_history.c
    src/network/q2/save_messages.c
    src/network/q2/save_record.c
    src/network/q2/session.c
    src/network/q2/session_client.c
    src/network/q2/session_download.c
    src/network/q2/session_records.c
    src/network/q2/session_save.c
    src/network/q2/session_server.c
    src/network/q2/temp_entities.c
    src/network/q2/vanilla.c
    src/network/q2/zpacket.c
    src/network/q3/admission.c
    src/network/q3/channel.c
    src/network/q3/fields_save.c
    src/network/q3/client.c
    src/network/q3/client_downloads.c
    src/network/q3/client_authorization.c
    src/network/q3/server_authorization.c
    src/network/q3/client_save.c
    src/network/q3/clock.c
    src/network/q3/codec.c
    src/network/q3/delta.c
    src/network/q3/huffman.c
    src/network/q3/messages.c
    src/network/q3/pak_references.c
    src/network/q3/pure.c
    src/network/q3/prediction_scene.c
    src/network/q3/server.c
    src/network/q3/download_window.c
    src/network/q3/server_save.c
    src/network/q3/visibility.c
    src/network/reliability.c
    src/network/runtime/commands.c
    src/network/runtime/q3.c
    src/network/runtime/q1_nq.c
    src/network/runtime/q1_qw.c
    src/network/runtime/session.c
    src/network/runtime/save.c
    src/network/runtime/prediction_save.c
    src/network/runtime/unified.c
    src/network/socks.c
    src/network/transport.c
    src/network/unified/channel.c
    src/network/unified/commands.c
    src/network/unified/composition.c
    src/network/unified/document.c
    src/network/unified/packet.c
    src/network/unified/schema.c
    src/network/q2/bootstrap_save.c
    src/network/q2/session_control.c
    src/network/unified/bootstrap.c
    src/network/unified/bootstrap_save.c
    src/network/unified/channel_state.c
    src/network/unified/session.c
    src/network/unified/session_commands.c
    src/network/unified/session_entropy.c
    src/network/unified/session_handshake.c
    src/network/unified/session_identity.c
    src/network/unified/session_inputs.c
    src/network/unified/session_messages.c
    src/network/unified/session_save.c
    src/network/unified/value.c
    src/network/runtime/q1_client.c
    src/network/unified/channel_save.c
    src/network/runtime/q1_client_save.c
    src/network/unified/session_continuation.c
    src/network/q2/kex_discovery_handoff.c
    src/network/runtime/local.c
    src/network/q2/unicast.c
    src/network/unified/session_close.c
)
target_link_libraries(qa_network PUBLIC qa_world qa_movement qa_content qa_http PRIVATE ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(qa_network PRIVATE iphlpapi ws2_32 bcrypt)
endif()

add_library(qa_qvm STATIC
    src/compat/qvm/compatibility.c
    src/compat/qvm/execute.c
    src/compat/qvm/image.c
    src/compat/qvm/intrinsics.c
    src/compat/qvm/memory.c
    src/compat/qvm/module.c
    src/compat/qvm/source_call.c
    src/compat/qvm/records.c)
target_link_libraries(qa_qvm PUBLIC qa_content)

add_library(qa_native_host STATIC
    src/compat/native_host/artifact.c
    src/compat/native_host/checkpoint.c
    src/compat/native_host/host.c
    src/compat/native_host/memory.c
    src/compat/native_host/message.c
    src/compat/native_host/movement.c
    src/compat/native_host/player.c
    src/compat/native_host/reconstruction.c
    src/compat/native_host/q2.c
    src/compat/native_host/q2_wire.c
    src/compat/native_host/q3.c
    src/compat/native_host/world.c)
target_link_libraries(qa_native_host PUBLIC
    qa_native qa_qvm qa_console qa_gameplay qa_movement qa_campaign
    PRIVATE qa_network)

add_library(qa_qc_host STATIC
    src/compat/qc_host/builtins.c
    src/compat/qc_host/call.c
    src/compat/qc_host/entities.c
    src/compat/qc_host/game.c
    src/compat/qc_host/physics.c)
target_link_libraries(qa_qc_host PUBLIC qa_qc qa_builtin qa_console qa_movement)

add_library(qa_q3_presentation STATIC
    src/presentation/q3/assets.c
    src/presentation/q3/shader_access.c
    src/presentation/q3/assets_save.c
    src/presentation/q3/capture.c
    src/presentation/q3/audio.c
    src/presentation/q3/cinematics.c
    src/presentation/q3/core.c
    src/presentation/q3/models.c
    src/presentation/q3/media_save.c
    src/presentation/q3/scene_save.c
    src/presentation/q3/picture.c
    src/presentation/q3/render.c
    src/presentation/q3/scene.c
    src/presentation/q3/assets_custody.c
    src/presentation/q3/source_scene_bank.c
    src/presentation/q3/source_scene_bank_save.c
)
target_link_libraries(qa_q3_presentation PUBLIC qa_scene qa_audio qa_media qa_text qa_input)

add_library(qa_q3_host STATIC
    src/compat/q3_host/bot_actions.c
    src/compat/q3_host/bot_chat.c
    src/compat/q3_host/bot_genetic.c
    src/compat/q3_host/bot_goals.c
    src/compat/q3_host/bot_library.c
    src/compat/q3_host/bot_movement.c
    src/compat/q3_host/bot_navigation.c
    src/compat/q3_host/bot_records.c
    src/compat/q3_host/bot_weapons.c
    src/compat/q3_host/checkpoint.c
    src/compat/q3_host/client_collision.c
    src/compat/q3_host/client_input.c
    src/compat/q3_host/client_keys.c
    src/compat/q3_host/common.c
    src/compat/q3_host/cvars.c
    src/compat/q3_host/entity_tokens.c
    src/compat/q3_host/files.c
    src/compat/q3_host/game_checkpoint.c
    src/compat/q3_host/game_portals.c
    src/compat/q3_host/game_records.c
    src/compat/q3_host/game_spatial.c
    src/compat/q3_host/host.c
    src/compat/q3_host/browser.c
    src/compat/q3_host/information.c
    src/compat/q3_host/input_lifetime.c
    src/compat/q3_host/memory.c
    src/compat/q3_host/presentation.c
    src/compat/q3_host/scripts.c
    src/compat/q3_host/signatures.c
    src/compat/q3_host/collision_scene.c
)
target_link_libraries(qa_q3_host PUBLIC qa_qvm qa_native_host qa_q3_presentation qa_bots qa_script qa_console qa_input)

add_library(qa_application STATIC
    src/app/application/acoustics.c
    src/app/application/bot_world.c
    src/app/application/bot_admission.c
    src/app/application/bots_catalog.c
    src/app/application/bots_frame.c
    src/app/application/bots_setup.c
    src/app/application/bots_memory_bind.c
    src/app/application/bots_log.c
    src/app/application/bots_guests.c
    src/app/application/bots_knowledge.c
    src/app/application/bots_q1_rules.c
    src/app/application/bot_world_bind.c
    src/app/application/bots_transport.c
    src/app/application/q3_campaign_source.c
    src/app/application/q3_campaign_launch.c
    src/app/application/q3_campaign_result.c
    src/app/application/q3_product.c
    src/app/application/native_q3_presentation.c
    src/app/application/native_q3_visibility.c
    src/app/application/equipment_presentation.c
    src/app/application/character_selection.c
    src/app/application/equipment_runtime.c
    src/app/application/equipment_actions.c
    src/app/application/equipment_content.c
    src/app/application/equipment_gear_presentation.c
    src/app/application/equipment_gear_world.c
    src/app/application/equipment_events.c
    src/app/application/weapon_availability.c
    src/app/application/q3_asset_selection.c
    src/app/application/selected_q3_character.c
    src/app/application/selected_effects.c
    src/app/application/supplies.c
    src/app/application/startup_flow.c
    src/app/application/startup_program.c
    src/app/application/startup_lifecycle.c
    src/app/application/engine_shutdown.c
    src/app/application/save_policy.c
    src/app/application/native_q1_console.c
    src/app/application/native_q1_composition.c
    src/app/application/native_q1_composition_player.c
    src/app/application/native_q1_composition_birth.c
    src/app/application/native_q1_composition_death.c
    src/app/application/native_q1_composition_flags.c
    src/app/application/native_q1_composition_rogue.c
    src/app/application/native_q1_powers.c
    src/app/application/native_q1_respawn.c
    src/app/application/native_q1_wire.c
    src/app/application/native_q2_presentation.c
    src/app/application/native_q2_presentation_local.c
    src/app/application/native_q2_delivery.c
    src/app/application/profile.c
    src/app/application/arsenal.c
    src/app/application/arsenal_guest.c
    src/app/application/bots.c
    src/app/application/bots_arsenal.c
    src/app/application/bots_navigation.c
    src/app/application/bots_save.c
    src/app/application/bots_round.c
    src/app/application/bots_projection.c
    src/app/application/bots_submit.c
    src/app/application/composition.c
    src/app/application/control.c
    src/app/application/control_frame.c
    src/app/application/commands.c
    src/app/application/events.c
    src/app/application/events_save.c
    src/app/application/frame_prepare.c
    src/app/application/guest_admission.c
    src/app/application/guest_bots.c
    src/app/application/guest_checkpoint.c
    src/app/application/guest_input_profile.c
    src/app/application/guest_inventory_profile.c
    src/app/application/guest_native_q2.c
    src/app/application/guest_native_q2_equipment.c
    src/app/application/guest_q2_control.c
    src/app/application/guest_native_q2_attack.c
    src/app/application/guest_native_q2_baseline.c
    src/app/application/guest_native_q2_calls.c
    src/app/application/guest_native_q2_clients.c
    src/app/application/guest_native_q2_continuation.c
    src/app/application/guest_native_q2_combat.c
    src/app/application/guest_native_q2_combat_kex.c
    src/app/application/guest_native_q2_combat_state.c
    src/app/application/guest_native_q2_hud.c
    src/app/application/guest_native_q2_inventory.c
    src/app/application/guest_native_q2_private_state.c
    src/app/application/guest_native_q2_save.c
    src/app/application/guest_native_q2_services.c
    src/app/application/guest_projection.c
    src/app/application/guest_projection_profile.c
    src/app/application/guest_q3.c
    src/app/application/guest_q3_console.c
    src/app/application/guest_q3_client_console.c
    src/app/application/guest_q3_factory.c
    src/app/application/guest_q3_body.c
    src/app/application/guest_q3_body_profile.c
    src/app/application/guest_q3_body_save.c
    src/app/application/guest_q3_catalog.c
    src/app/application/guest_q3_combat_profile.c
    src/app/application/guest_q3_combat_state.c
    src/app/application/guest_q3_combat_source.c
    src/app/application/guest_q3_component_body.c
    src/app/application/guest_q3_component_body_save.c
    src/app/application/guest_q3_damage_scope.c
    src/app/application/guest_q3_weapon_profile.c
    src/app/application/guest_q3_weapons.c
    src/app/application/guest_q3_weapons_save.c
    src/app/application/guest_q3_weapons_services.c
    src/app/application/guest_q3_weapons_services_save.c
    src/app/application/guest_q3_grapple_profile.c
    src/app/application/guest_q3_fire.c
    src/app/application/guest_q3_scene_world.c
    src/app/application/guest_q3_source_ui.c
    src/app/application/guest_q3_equipment.c
    src/app/application/guest_q3_equipment_functions.c
    src/app/application/guest_q3_gear.c
    src/app/application/guest_q3_gear_hooks.c
    src/app/application/guest_q3_gear_memory.c
    src/app/application/guest_q3_gear_mirror.c
    src/app/application/guest_q3_gear_save.c
    src/app/application/guest_q3_gear_services.c
    src/app/application/guest_q3_functions.c
    src/app/application/guest_q3_equipment_profile.c
    src/app/application/guest_q3_control.c
    src/app/application/guest_q3_save.c
    src/app/application/guest_q3_clients.c
    src/app/application/guest_q3_effects.c
    src/app/application/guest_q3_exports.c
    src/app/application/guest_q3_roles.c
    src/app/application/guest_q3_restart.c
    src/app/application/guest_q3_server.c
    src/app/application/guest_qc.c
    src/app/application/guest_qc_factory.c
    src/app/application/guest_qc_spawn.c
    src/app/application/guest_qc_rerelease.c
    src/app/application/guest_qc_bot_orders.c
    src/app/application/guest_qc_original_save.c
    src/app/application/guest_qc_visual.c
    src/app/application/guest_qc_checkpoint.c
    src/app/application/guest_qc_clients.c
    src/app/application/guest_qc_declared.c
    src/app/application/guest_qc_imports.c
    src/app/application/guest_qc_outputs.c
    src/app/application/guest_qc_input.c
    src/app/application/guest_qc_messages.c
    src/app/application/guest_qc_projection.c
    src/app/application/guest_qc_qualification.c
    src/app/application/guest_services.c
    src/app/application/guest_source_actor.c
    src/app/application/guest_weapon.c
    src/app/application/lifetime.c
    src/app/application/map.c
    src/app/application/map_players.c
    src/app/application/map_travel.c
    src/app/application/match.c
    src/app/application/match_intents.c
    src/app/application/native_commands.c
    src/app/application/native_q3_console.c
    src/app/application/native_q2_console.c
    src/app/application/native_q2_checkpoint.c
    src/app/application/native_q2_combat_policy.c
    src/app/application/native_q2_feedback_wire.c
    src/app/application/native_q3_chat.c
    src/app/application/native_q3_clients.c
    src/app/application/native_q3_client.c
    src/app/application/native_q3_remote_role.c
    src/app/application/native_q3_remote_lifecycle.c
    src/app/application/native_q3_remote_role_save.c
    src/app/application/native_q3_remote_character.c
    src/app/application/native_q3_remote_content.c
    src/app/application/native_q3_remote_client.c
    src/app/application/native_q3_remote_client_cvars.c
    src/app/application/native_q3_remote_client_save.c
    src/app/application/native_q3_remote_client_settings.c
    src/app/application/native_q3_client_modules.c
    src/app/application/native_q3_client_modules_save.c
    src/app/application/native_q3_client_cvars.c
    src/app/application/native_q3_client_settings.c
    src/app/application/native_q3_client_time.c
    src/app/application/native_q3_client_save.c
    src/app/application/native_q3_control.c
    src/app/application/native_q3_end_frame.c
    src/app/application/native_q3_ipfilters.c
    src/app/application/native_q3_log.c
    src/app/application/native_q3_match.c
    src/app/application/native_q3_objectives.c
    src/app/application/native_q3_postgame.c
    src/app/application/native_q3_rank.c
    src/app/application/native_q3_session.c
    src/app/application/native_q3_settings.c
    src/app/application/native_q3_team_status.c
    src/app/application/native_q3_team_combat.c
    src/app/application/native_q3_votes.c
    src/app/application/native_q3_wire_state.c
    src/app/application/native_q3_equipment.c
    src/app/application/native_modes.c
    src/app/application/native_maps.c
    src/app/application/q3_restart.c
    src/app/application/q3_round.c
    src/app/application/q3_world_restart.c
    src/app/application/rankings.c
    src/app/application/network.c
    src/app/application/network_q2.c
    src/app/application/network_q2_frame.c
    src/app/application/network_q2_save.c
    src/app/application/network_q3_status.c
    src/app/application/network_q1_source.c
    src/app/application/network_q1_signon.c
    src/app/application/network_qw.c
    src/app/application/owner.c
    src/app/application/portals.c
    src/app/application/providers.c
    src/app/application/q1_weapon_rules.c
    src/app/application/presentation.c
    src/app/application/native_q3_wire.c
    src/app/application/publication.c
    src/app/application/services.c
    src/app/application/save.c
    src/app/application/save_content.c
    src/app/application/save_physics.c
    src/app/application/save_progression.c
    src/app/application/save_native_q2.c
    src/app/application/save_native_q2_record.c
    src/app/application/save_resolvers.c
    src/app/application/visuals.c
    src/app/application/network_q2_hooks.c
    src/app/application/native_q2_rotation.c
    src/app/application/view_preferences.c
    src/app/application/guest_q3_combat.c
    src/app/application/guest_q3_scene_profile.c
    src/app/application/guest_q3_scene.c
    src/app/application/guest_q3_scene_save.c
    src/app/application/guest_q3_mod_input.c
    src/app/application/guest_q3_mod_protection.c
    src/app/application/guest_q3_mod_callbacks.c
    src/app/application/guest_q3_mod_profile.c
    src/app/application/guest_q3_mod_save.c
    src/app/application/guest_q3_component_source.c
    src/app/application/unified_output.c
    src/app/application/unified_output_json.c
    src/app/application/unified_presentations.c
    src/app/application/unified_events.c
    src/app/application/network_unified.c
    src/app/application/network_unified_server.c
    src/app/application/native_process_platform.c
    src/app/application/native_q3_client_body.c
    src/app/application/native_q3_client_functions.c
    src/app/application/guest_q3_component.c
    src/app/application/guest_q3_component_clients.c
    src/app/application/guest_q3_component_frame.c
    src/app/application/guest_q3_component_lifecycle.c
    src/app/application/guest_q3_component_records.c
    src/app/application/guest_q3_component_records_calls.c
    src/app/application/guest_q3_component_services.c
    src/app/application/guest_q3_mod.c
    src/app/application/guest_q3_mod_operations.c
    src/app/application/guest_q3_pickups.c
    src/app/application/guest_q3_pickups_profile.c
    src/app/application/guest_qc_presentation.c
    src/app/application/native_client_roles.c
    src/app/application/native_process_resources.c
    src/app/application/native_q2_appearance.c
    src/app/application/unified_native_q2_models.c
    src/app/application/unified_player.c
    src/app/application/unified_prediction.c
    src/app/application/unified_q1_events.c
    src/app/application/unified_q2_components.c
    src/app/application/unified_q3_events.c
    src/app/application/guest_q3_component_input.c
    src/app/application/guest_q3_component_records_save.c
    src/app/application/guest_q3_component_save.c
    src/app/application/guest_q3_component_source_save.c
    src/app/application/guest_q3_components.c
    src/app/application/guest_q3_components_game.c
    src/app/application/guest_q3_components_identity.c
    src/app/application/guest_q3_components_publication.c
    src/app/application/guest_q3_components_scenes.c
    src/app/application/native_process_owner.c
    src/app/application/native_q2_prediction.c
    src/app/application/unified_components.c
    src/app/application/unified_output_capture.c
    src/app/application/unified_q2_events.c
    src/app/application/guest_q3_component_client_adapter.c
    src/app/application/guest_q3_components_command.c
    src/app/application/guest_q3_component_actors.c
    src/app/application/guest_q3_mod_actors.c
    src/app/application/guest_q3_mod_actors_callbacks.c
    src/app/application/guest_q3_mod_actors_damage.c
    src/app/application/guest_q3_mod_actors_profile.c
    src/app/application/guest_q3_mod_actors_state.c
    src/app/application/guest_q3_weapon_models_profile.c
    src/app/application/map_sidecars.c
    src/app/application/native_q2_publication.c
    src/app/application/client_events.c
    src/app/application/guest_q3_components_save.c
    src/app/application/guest_q3_mod_actors_save.c
    src/app/application/guest_q3_weapon_models.c
    src/app/application/guest_q3_weapon_models_save.c
    src/app/application/native_q2_callbacks.c
    src/app/application/native_q2_wire_engine.c
    src/app/application/network_q2_resources.c
    src/app/application/unified_q2_native_events.c
    src/app/application/bots_npc.c
    src/app/application/guest_q3_collision.c
    src/app/application/guest_q3_collision_profile.c
    src/app/application/guest_q3_video.c
    src/app/application/native_q2_inventory_scanner.c
    src/app/application/native_q2_records.c
    src/app/application/native_q2_records_calls.c
    src/app/application/native_q2_records_profile.c
    src/app/application/native_q3_client_modules_video.c
    src/app/application/network_unified_inputs_save.c
    src/app/application/network_unified_save.c
    src/app/application/unified_components_save.c
    src/app/application/unified_output_capture_save.c
    src/app/application/unified_q3_sources.c
    src/app/application/unified_save_internal.c
    src/app/application/bots_npc_save.c
    src/app/application/client_prepare.c
    src/app/application/native_q2_inventory_rows.c
    src/app/application/native_q2_protocol_resources.c
    src/app/application/native_q2_records_save.c
    src/app/application/network_q2_materials.c
    src/app/application/guest_qc_weapon.c
    src/app/application/guest_q3_mod_items_profile.c
)
target_link_libraries(qa_application PUBLIC qa_persistence qa_configuration qa_game_q1 qa_game_q2 qa_game_q3 qa_campaign qa_modes qa_player_services qa_qc_host qa_q3_host qa_text qa_settings)
if(WIN32)
    target_link_libraries(qa_application PRIVATE bcrypt)
endif()

add_library(qa_q3_native_presentation STATIC
    src/presentation/q3_native/pose.c
    src/presentation/q3_native/attachments.c
    src/presentation/q3_native/body.c
    src/presentation/q3_native/packet.c
    src/presentation/q3_native/trajectory.c
    src/presentation/q3_native/media.c
    src/presentation/q3_native/loading.c
    src/presentation/q3_native/loading_save.c
    src/presentation/q3_native/selected_media.c
    src/presentation/q3_native/selected_media_save.c
    src/presentation/q3_native/selected_authored_media.c
    src/presentation/q3_native/selected_authored_media_save.c
    src/presentation/q3_native/remote_frame.c
    src/presentation/q3_native/weapon.c
    src/presentation/q3_native/client_info.c
    src/presentation/q3_native/client_info_save.c
    src/presentation/q3_native/events.c
    src/presentation/q3_native/events_save.c
    src/presentation/q3_native/effects.c
    src/presentation/q3_native/local_entities.c
    src/presentation/q3_native/marks.c
    src/presentation/q3_native/particles.c
    src/presentation/q3_native/particles_save.c
    src/presentation/q3_native/player_fx.c
    src/presentation/q3_native/player_fx_save.c
    src/presentation/q3_native/player_state.c
    src/presentation/q3_native/player_state_save.c
    src/presentation/q3_native/view.c
    src/presentation/q3_native/view_save.c
    src/presentation/q3_native/hud.c
    src/presentation/q3_native/hud_draw.c
    src/presentation/q3_native/hud_preferences.c
    src/presentation/q3_native/hud_corners.c
    src/presentation/q3_native/hud_scoreboard.c
    src/presentation/q3_native/hud_save.c
    src/presentation/q3_native/server_commands.c
    src/presentation/q3_native/server_commands_save.c
    src/presentation/q3_native/voice.c
    src/presentation/q3_native/voice_save.c
    src/presentation/q3_native/mission_hud.c
    src/presentation/q3_native/mission_hud_owner.c
    src/presentation/q3_native/mission_hud_menu.c
    src/presentation/q3_native/mission_hud_text.c
    src/presentation/q3_native/mission_hud_save.c
    src/presentation/q3_native/authored_menu_context.c
    src/presentation/q3_native/authored_menu_shared.c
    src/presentation/q3_native/authored_menu_save.c
    src/presentation/q3_native/native.c
    src/presentation/q3_native/entity_save.c
    src/presentation/q3_native/native_save.c
    src/presentation/q3_native/compiled_frame.c
    src/presentation/q3_native/compiled_source.c
)
target_link_libraries(qa_q3_native_presentation PUBLIC qa_application qa_q3_presentation qa_ui)

add_library(qa_ui STATIC
    src/ui/controller.c
    src/ui/preferences.c
    src/ui/save.c
    src/ui/draw.c
    src/ui/hud.c
    src/ui/input.c
    src/ui/library.c
    src/ui/library_save.c
    src/ui/llm.c
    src/ui/mods.c
    src/ui/mods_save.c
    src/ui/q2_layout.c
    src/ui/rankings.c
    src/ui/search.c
    src/ui/wheel.c)
target_link_libraries(qa_ui PUBLIC qa_application qa_text qa_input qa_settings qa_scene)

add_library(qa_http STATIC
    src/tools/http.c)
target_link_libraries(qa_http PUBLIC qa_core PRIVATE CURL::libcurl)

add_library(qa_tools STATIC
    src/tools/camera.c
    src/tools/camera_author.c
    src/tools/camera_io.c
    src/tools/capture.c
    src/tools/controller.c
    src/tools/debug.c
    src/tools/diagnostics.c
    src/tools/llm.c
    src/tools/llm_auth.c
    src/tools/llm_batch.c
    src/tools/llm_catalog.c
    src/tools/llm_console.c
    src/tools/llm_models.c
    src/tools/llm_request.c
    src/tools/llm_settings.c
    src/tools/llm_stream.c
    src/tools/native_debug.c
    src/tools/profiler.c)
target_link_libraries(qa_tools PUBLIC qa_builtin qa_scene qa_content qa_console qa_text qa_media qa_q3_presentation qa_http)

add_library(qa_persistence STATIC
    src/compat/native/module_save.c
    src/console/cvars_save.c
    src/console/commands_save.c
    src/persistence/actors.c
    src/persistence/combat.c
    src/persistence/coordinator.c
    src/persistence/demo.c
    src/persistence/image.c
    src/persistence/inventory.c
    src/persistence/movement_fields.c
    src/persistence/navigation.c
    src/navigation/graph_save.c
    src/persistence/pickups.c
    src/persistence/q1_source.c
    src/persistence/recovery.c
    src/persistence/session.c
    src/persistence/slots.c
    src/persistence/source_io.c
    src/persistence/strings.c
    src/persistence/targets.c
    src/persistence/world.c)
target_link_libraries(qa_persistence PUBLIC qa_session qa_content qa_gameplay qa_movement qa_network qa_campaign qa_console qa_navigation qa_native)

add_library(qa_frontend STATIC
    src/platform/native_runtime.c
    src/app/frontend/accessibility.c
    src/app/frontend/campaign.c
    src/app/frontend/campaign_menu.c
    src/app/frontend/campaign_cinematic.c
    src/app/frontend/cinematic_captions.c
    src/app/frontend/chat.c
    src/app/frontend/config_scripts.c
    src/app/frontend/config_store.c
    src/app/frontend/config_bindings.c
    src/app/frontend/authored_bindings.c
    src/app/frontend/client_registry.c
    src/app/frontend/config_userinfo.c
    src/app/frontend/config_weapon_defaults.c
    src/app/frontend/startup_config.c
    src/app/frontend/keys.c
    src/app/frontend/restart.c
    src/app/frontend/equipment_held.c
    src/app/frontend/equipment_held_stock.c
    src/app/frontend/equipment_media.c
    src/app/frontend/equipment_media_save.c
    src/app/frontend/equipment_source.c
    src/app/frontend/equipment_q3.c
    src/app/frontend/equipment_q3_save.c
    src/app/frontend/equipment_native.c
    src/app/frontend/equipment_events.c
    src/app/frontend/equipment_gear.c
    src/app/frontend/equipment_gear_output.c
    src/app/frontend/equipment_gear_world_output.c
    src/app/frontend/equipment_gear_save.c
    src/app/frontend/equipment_held_output.c
    src/app/frontend/network_content_q3.c
    src/app/frontend/q1_text.c
    src/app/frontend/input_profile.c
    src/app/frontend/input_settings.c
    src/app/frontend/input_shutdown.c
    src/app/frontend/shared_register.c
    src/app/frontend/shared_ui.c
    src/app/frontend/shared_values.c
    src/app/frontend/shared_video.c
    src/app/frontend/shared_gamma.c
    src/app/frontend/shared_audio.c
    src/app/frontend/shared_music_policy.c
    src/app/frontend/shared_publication.c
    src/app/frontend/shared_render_controls.c
    src/app/frontend/shared_resource_policy.c
    src/app/frontend/shared_storage.c
    src/app/frontend/shared_acoustics.c
    src/app/frontend/shared_settings.c
    src/app/frontend/menu_fonts.c
    src/app/frontend/original_frontend.c
    src/app/frontend/qc_rerelease_events.c
    src/app/frontend/save_menu.c
    src/app/frontend/ui_features.c
    src/app/frontend/ui_features_save.c
    src/app/frontend/audio_identity.c
    src/app/frontend/audio_identity_save.c
    src/app/frontend/audio_restore.c
    src/app/frontend/bindings_ui.c
    src/app/frontend/capture.c
    src/app/frontend/commands.c
    src/app/frontend/content_inventory.c
    src/app/frontend/content_refs.c
    src/app/frontend/frame.c
    src/app/frontend/font_inventory.c
    src/app/frontend/image_inventory.c
    src/app/frontend/travel.c
    src/app/frontend/launch.c
    src/app/frontend/lifetime.c
    src/app/frontend/legacy_render_policy.c
    src/app/frontend/map_events.c
    src/app/frontend/model_inventory.c
    src/app/frontend/material_inventory.c
    src/app/frontend/material_movies.c
    src/app/frontend/material_movies_prepare.c
    src/app/frontend/material_movies_save.c
    src/app/frontend/music_sources.c
    src/app/frontend/music_sources_save.c
    src/app/frontend/player_inventory.c
    src/app/frontend/q3_inventory.c
    src/app/frontend/q3_render_policy.c
    src/app/frontend/menu_save.c
    src/app/frontend/seat_inventory.c
    src/app/frontend/native_q2.c
    src/app/frontend/native_q2_baseline.c
    src/app/frontend/native_q2_messages.c
    src/app/frontend/native_character.c
    src/app/frontend/native_composition.c
    src/app/frontend/network.c
    src/app/frontend/network_kex.c
    src/app/frontend/network_kex_save.c
    src/app/frontend/network_browser.c
    src/app/frontend/network_predictor.c
    src/app/frontend/remote_input.c
    src/app/frontend/remote_config.c
    src/app/frontend/remote_prediction.c
    src/app/frontend/remote_snapshots.c
    src/app/frontend/remote_q3_client.c
    src/app/frontend/remote_q3_commands.c
    src/app/frontend/remote_q3_graph.c
    src/app/frontend/remote_q3_graph_children.c
    src/app/frontend/remote_q3_graph_save.c
    src/app/frontend/remote_q3_frame.c
    src/app/frontend/remote_q3_runtime.c
    src/app/frontend/remote_q3_session.c
    src/app/frontend/remote_q3_transport.c
    src/app/frontend/remote_q3_services.c
    src/app/frontend/remote_q3_initial.c
    src/app/frontend/remote_q3_modules.c
    src/app/frontend/native_q3_client.c
    src/app/frontend/native_q3_client_save.c
    src/app/frontend/native_q3_topology.c
    src/app/frontend/native_q3_commands.c
    src/app/frontend/network_nq.c
    src/app/frontend/network_nq_save.c
    src/app/frontend/network_q3_packages.c
    src/app/frontend/network_qw.c
    src/app/frontend/network_qw_save.c
    src/app/frontend/options.c
    src/app/frontend/particle_events.c
    src/app/frontend/persistence.c
    src/app/frontend/root_restore.c
    src/app/frontend/save_commands.c
    src/app/frontend/save_fields.c
    src/app/frontend/player_events.c
    src/app/frontend/presentation.c
    src/app/frontend/rankings.c
    src/app/frontend/round.c
    src/app/frontend/resources.c
    src/app/frontend/remaps.c
    src/app/frontend/restore_topology.c
    src/app/frontend/seats.c
    src/app/frontend/scene_identity.c
    src/app/frontend/scene_inventory.c
    src/app/frontend/scene_refs.c
    src/app/frontend/selected_character.c
    src/app/frontend/selected_character_save.c
    src/app/frontend/selected_character_lifetime.c
    src/app/frontend/selected_effects.c
    src/app/frontend/source_effects.c
    src/app/frontend/selected_effects_save.c
    src/app/frontend/selected_effects_particles_save.c
    src/app/frontend/selected_effects_q1_particles.c
    src/app/frontend/selected_effects_q2_particles.c
    src/app/frontend/source.c
    src/app/frontend/startup_server_browser.c
    src/app/frontend/system_cinematic.c
    src/app/frontend/system_cinematic_save.c
    src/app/frontend/tools.c
    src/app/frontend/ui_restore.c
    src/app/frontend/visuals.c
    src/app/frontend/view_settings.c
    src/app/frontend/wheels.c
    src/app/frontend/world_inventory.c
    src/app/frontend/global_settings_storage.c
    src/app/frontend/view_bindings.c
    src/app/frontend/startup_rotation.c
    src/app/frontend/remote_q3_modules_save.c
    src/app/frontend/remote_q3_graph_roster.c
    src/app/frontend/remote_q2_client.c
    src/app/frontend/remote_q2_media.c
    src/app/frontend/remote_q2_download.c
    src/app/frontend/remote_q2_presentation.c
    src/app/frontend/remote_q2_restore.c
    src/app/frontend/remote_q1_client.c
    src/app/frontend/remote_q1_entities.c
    src/app/frontend/remote_q1_qw.c
    src/app/frontend/q1_sky.c
    src/app/frontend/q1_sky_prepare.c
    src/app/frontend/q1_sky_save.c
    src/app/frontend/material_movie_inventory.c
    src/app/frontend/q3_color_policy.c
    src/app/frontend/qc_messages.c
    src/app/frontend/remote_q1_effects.c
    src/app/frontend/remote_q1_hud.c
    src/app/frontend/remote_q1_media.c
    src/app/frontend/remote_q1_presentation.c
    src/app/frontend/remote_q1_restore.c
    src/app/frontend/remote_q2_source.c
    src/app/composition/recipe_choices.c
    src/app/composition/recipe_import.c
    src/app/composition/recipe_publish.c
    src/app/frontend/client_source.c
    src/app/frontend/component_scene.c
    src/app/frontend/network_q2_client.c
    src/app/frontend/network_q2_host.c
    src/app/frontend/remote_q1_prediction.c
    src/app/frontend/remote_q1_sky_policy.c
    src/app/frontend/remote_q1_source.c
    src/app/frontend/remote_q1_transport.c
    src/app/frontend/remote_q2_policy.c
    src/app/frontend/remote_q2_prediction.c
    src/app/frontend/remote_unified.c
    src/app/frontend/remote_unified_media.c
    src/app/frontend/remote_unified_prediction.c
    src/app/frontend/remote_unified_render.c
    src/app/frontend/root_resources.c
    src/app/frontend/source_prompt.c
    src/app/frontend/remote_q1_camera.c
    src/app/frontend/native_resource_inventory.c
    src/app/frontend/remote_q1_skins.c
    src/app/frontend/remote_q2_effects.c
    src/app/frontend/remote_q2_effects_bridge.c
    src/app/frontend/remote_q2_effects_policy.c
    src/app/frontend/remote_unified_events.c
    src/app/frontend/network_q1_client.c
    src/app/frontend/network_q1_skin_commands.c
    src/app/frontend/network_unified.c
    src/app/frontend/remote_q2_effects_entities.c
    src/app/frontend/remote_q2_effects_save.c
    src/app/frontend/remote_unified_presentation.c
    src/app/frontend/remote_unified_q1.c
    src/app/frontend/remote_unified_q2.c
    src/app/frontend/remote_unified_q3.c
    src/app/frontend/renderer_materials.c
    src/app/frontend/source_acoustics.c
    src/app/frontend/network_unified_save.c
    src/app/frontend/neutral_config.c
    src/app/frontend/remote_q2_clientinfo.c
    src/app/frontend/remote_q2_footsteps.c
    src/app/frontend/remote_q3_modules_video.c
    src/app/frontend/remote_unified_components.c
    src/app/frontend/remote_unified_components_records.c
    src/app/frontend/remote_unified_components_scene.c
    src/app/frontend/remote_unified_input.c
    src/app/frontend/remote_unified_input_save.c
    src/app/frontend/remote_unified_media_save.c
    src/app/frontend/remote_unified_prediction_save.c
    src/app/frontend/renderer_worlds.c
    src/app/frontend/restart_binding.c
    src/app/frontend/unified_input_command.c
    src/app/frontend/unified_q3_sources.c
    src/app/composition/recipe_save.c
    src/app/frontend/native_q3_video.c
    src/app/frontend/network_q2_events.c
    src/app/frontend/network_unified_client.c
    src/app/frontend/received_music.c
    src/app/frontend/remote_q3_compiled_video.c
    src/app/frontend/remote_unified_components_save.c
    src/app/frontend/remote_unified_save.c
    src/app/frontend/source_renderer_runtime.c
    src/app/frontend/unified_media_inventory.c
    src/app/frontend/unified_q3_client.c
    src/app/frontend/unified_q3_snapshots.c
    src/app/frontend/video_guests.c
    src/app/frontend/remote_q2_material_movies_bridge.c
)
target_link_libraries(qa_frontend PUBLIC qa_application qa_ui qa_tools qa_display qa_render_cpu qa_render_gl qa_render_controls qa_input qa_settings qa_audio qa_q3_presentation qa_q3_native_presentation qa_network PRIVATE SDL2::SDL2 ICU::i18n)
get_filename_component(QA_NATIVE_RUNTIME_FULL_ROOT "${QA_NATIVE_RUNTIME_INSTALL_ROOT}"
    ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
file(RELATIVE_PATH QA_NATIVE_RUNTIME_RELATIVE_ROOT
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${QA_NATIVE_RUNTIME_FULL_ROOT}")
string(REPLACE "\\" "\\\\" QA_NATIVE_RUNTIME_RELATIVE_ROOT "${QA_NATIVE_RUNTIME_RELATIVE_ROOT}")
string(REPLACE "\"" "\\\"" QA_NATIVE_RUNTIME_RELATIVE_ROOT "${QA_NATIVE_RUNTIME_RELATIVE_ROOT}")
target_compile_definitions(qa_frontend PRIVATE
    QA_NATIVE_RUNTIME_RELATIVE_ROOT="${QA_NATIVE_RUNTIME_RELATIVE_ROOT}")

if(WIN32)
    target_link_libraries(qa_tools PRIVATE bcrypt ws2_32 Psapi)
endif()

add_executable(quake-anthology src/main.c)
set_target_properties(quake-anthology PROPERTIES ENABLE_EXPORTS ON)
if(TARGET qa-native-profile-build)
    add_dependencies(quake-anthology qa-native-profile-build)
endif()
target_link_libraries(quake-anthology PRIVATE qa_content qa_frontend qa_native)
target_compile_definitions(quake-anthology PRIVATE QA_VERSION="${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    add_executable(core_test tests/core_test.c)
    target_link_libraries(core_test PRIVATE qa_core)
    add_test(NAME core COMMAND core_test)

    add_executable(archive_test tests/archive_test.c)
    target_link_libraries(archive_test PRIVATE qa_content ZLIB::ZLIB)
    add_test(NAME archive COMMAND archive_test)

    add_executable(vfs_test tests/vfs_test.c)
    target_link_libraries(vfs_test PRIVATE qa_content ZLIB::ZLIB)
    add_test(NAME vfs COMMAND vfs_test)

    add_executable(bsp_test tests/bsp_test.c)
    target_link_libraries(bsp_test PRIVATE qa_content)
    add_test(NAME bsp COMMAND bsp_test)

    add_executable(image_test tests/image_test.c)
    target_link_libraries(image_test PRIVATE qa_content)
    add_test(NAME image COMMAND image_test)

    add_executable(model_test tests/model_test.c)
    target_link_libraries(model_test PRIVATE qa_content)
    add_test(NAME model COMMAND model_test)
endif()

install(TARGETS quake-anthology RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
