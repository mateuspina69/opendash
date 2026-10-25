cmake_minimum_required(VERSION 3.28)
project(opendash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# the app's renderer: D3D12 on Windows (vcpkg), Vulkan elsewhere (cmake/linux_deps.cmake)
if(WIN32)
    find_package(imgui CONFIG REQUIRED)
    find_package(nvrhi CONFIG REQUIRED)
    find_package(directx-headers CONFIG REQUIRED)
else()
    include(cmake/linux_deps.cmake)
endif()
include(cmake/shaders.cmake)

# the game's install path and the PS1 disc image's folder: Windows's, or the same paths in the default Wine prefix
if(WIN32)
    set(od_default_game_dir "C:/Program Files (x86)/Capcom/Dash2")
    set(od_default_disc_dir "C:/Dash2/Mega Man Legends 2 (USA)")
else()
    set(od_default_game_dir "$ENV{HOME}/.wine/drive_c/Program Files (x86)/Capcom/Dash2")
    set(od_default_disc_dir "$ENV{HOME}/.wine/drive_c/Dash2/Mega Man Legends 2 (USA)")
endif()
set(OPENDASH_DAT_DIR "${od_default_game_dir}/dat" CACHE PATH
    "Original game .DAT directory (used by tests; never copied into the repo)")
set(OPENDASH_EXE "${od_default_game_dir}/dash2.exe" CACHE FILEPATH
    "The user's dash2.exe (per-stage tables are read from it; never copied into the repo)")
set(OPENDASH_DISC_CUE "${od_default_disc_dir}/Mega Man Legends 2 (USA).cue" CACHE FILEPATH
    "The user's PS1 disc image, a .cue sheet with raw 2352-byte .bin tracks (never copied into the repo)")

# --- the game code, platform independent, in three layers: content -> game -> engine (a layer only includes the ones
# below it; game reaches content through the registry it declares, game/content_registry) ---
# engine: the machine the port runs on (GameMem, x87, DAT / VRAM / bank / exe tables, room data, config, lang)
# game: the game's systems (actors, combat, collision, spawn, transitions, messages, menus, save, story, draw passes)
# content: per-area classes, room scripts and cutscenes; content/registry.cpp registers them (od::content::registerAll)
foreach(layer engine game content)
    file(GLOB_RECURSE od_${layer}_sources CONFIGURE_DEPENDS src/${layer}/*.cpp)
    add_library(od${layer} STATIC ${od_${layer}_sources})
    target_include_directories(od${layer} PUBLIC src)
    if(MSVC)
        target_compile_options(od${layer} PRIVATE /W4 /permissive-)
    endif()
endforeach()
target_link_libraries(odgame PUBLIC odengine)
target_link_libraries(odcontent PUBLIC odgame)

# --- net: the netplay transport, protocol and session (plain Winsock / BSD sockets UDP, docs/adr/0005-netplay-transport.md); no
# game code, the app glues it to the game ---
file(GLOB od_net_sources CONFIGURE_DEPENDS src/net/*.cpp)
add_library(odnet STATIC ${od_net_sources})
target_include_directories(odnet PUBLIC src)
if(WIN32)
    target_compile_definitions(odnet PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(odnet PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(odnet PRIVATE /W4 /permissive-)
endif()

# --- render logic: GPU-free math shared by the renderers (unit tested) ---
add_library(odrender STATIC src/render/render_logic.cpp src/render/ui_canvas.cpp src/render/ui_font.cpp
    src/render/ui_svg.cpp)
target_include_directories(odrender PUBLIC src)
find_package(Stb QUIET) # stb_truetype: the UI font (render/ui_font); stb_image_write: dev dumps
if(NOT Stb_FOUND) # no vcpkg: the system's headers
    find_path(Stb_INCLUDE_DIR stb_truetype.h PATH_SUFFIXES stb REQUIRED)
endif()
target_include_directories(odrender PUBLIC ${Stb_INCLUDE_DIR})
find_package(NanoSVG CONFIG REQUIRED) # the vector UI's pictures (render/ui_svg)
target_link_libraries(odrender PRIVATE NanoSVG::nanosvg NanoSVG::nanosvgrast)
if(MSVC)
    target_compile_options(odrender PRIVATE /W4 /permissive-)
endif()

# the build id (app/build_id.hpp), regenerated on every build (cmake/build_id.cmake)
find_package(Git REQUIRED)
set(OD_BUILD_ID_CPP ${CMAKE_BINARY_DIR}/generated/build_id.cpp)
add_custom_target(od_build_id
    COMMAND ${CMAKE_COMMAND} -DGIT=${GIT_EXECUTABLE} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${OD_BUILD_ID_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/build_id.cmake
    BYPRODUCTS ${OD_BUILD_ID_CPP}
    COMMENT "build id")

# --- app ---
file(GLOB_RECURSE od_app_sources CONFIGURE_DEPENDS src/app/*.cpp)
add_executable(opendash WIN32 ${od_app_sources} src/audio/mixer.cpp src/audio/music_loops.cpp
    src/render/nvrhi_renderer.cpp src/render/imgui_nvrhi.cpp src/render/ui_pass.cpp src/render/upscaler_none.cpp)
target_include_directories(opendash PRIVATE src)
target_sources(opendash PRIVATE ${OD_BUILD_ID_CPP})
add_dependencies(opendash od_build_id)
target_link_libraries(opendash PRIVATE odcontent odrender odnet SDL3::SDL3 imgui::imgui nvrhi)
if(WIN32)
    target_sources(opendash PRIVATE src/render/device_d3d12.cpp)
    target_compile_definitions(opendash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(opendash PRIVATE dxgi d3d12 nvrhi_d3d12 Microsoft::DirectX-Headers)
else()
    target_sources(opendash PRIVATE src/render/device_vulkan.cpp)
    target_link_libraries(opendash PRIVATE nvrhi_vk nvrhi Vulkan::Headers ${OD_VULKAN_LOADER} od_directxmath)
endif()
od_add_shader(TARGET opendash FILE src/render/shaders/output.hlsl ENTRY vs_main PROFILE vs_6_0 NAME g_output_vs)
od_add_shader(TARGET opendash FILE src/render/shaders/output.hlsl ENTRY ps_main PROFILE ps_6_0 NAME g_output_ps)
od_add_shader(TARGET opendash FILE src/render/shaders/debugview.hlsl ENTRY vs_main PROFILE vs_6_0 NAME g_debugview_vs)
od_add_shader(TARGET opendash FILE src/render/shaders/debugview.hlsl ENTRY ps_main PROFILE ps_6_0 NAME g_debugview_ps)
od_add_shader(TARGET opendash FILE src/render/shaders/ui.hlsl ENTRY vs_main PROFILE vs_6_0 NAME g_ui_vs)
od_add_shader(TARGET opendash FILE src/render/shaders/ui.hlsl ENTRY ps_main PROFILE ps_6_0 NAME g_ui_ps)
od_add_shader(TARGET opendash FILE src/render/shaders/preview.hlsl ENTRY vs_main PROFILE vs_6_0 NAME g_preview_vs)
od_add_shader(TARGET opendash FILE src/render/shaders/preview.hlsl ENTRY ps_main PROFILE ps_6_0 NAME g_preview_ps)
od_add_shader(TARGET opendash FILE src/render/shaders/scene.hlsl ENTRY vs_main PROFILE vs_6_0 NAME g_scene_vs)
od_add_shader(TARGET opendash FILE src/render/shaders/scene.hlsl ENTRY ps_main PROFILE ps_6_0 NAME g_scene_ps)
od_add_shader(TARGET opendash FILE src/render/shaders/scene.hlsl ENTRY ps_main PROFILE ps_6_5 NAME g_scene_rt_ps
              DEFINES OD_RT=1)
if(WIN32)
    add_custom_command(TARGET opendash POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:opendash> $<TARGET_FILE_DIR:opendash>
        COMMAND_EXPAND_LISTS)
endif()
add_custom_command(TARGET opendash POST_BUILD # seed saves/ from the Debug dir (other configs)
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_BINARY_DIR}/Debug -DDST=$<TARGET_FILE_DIR:opendash>
            -P ${CMAKE_SOURCE_DIR}/cmake/sync_runtime_data.cmake)

# --- upscaler SDKs (Windows only): not in the repository; whoever builds puts them in third_party/ ---
if(WIN32)
# Every build compiles all four backends, so every build of the same sources is the same exe; whether a backend is
# offered is decided at run time only (its DLL next to the exe, the GPU / driver): DLLs are delay-loaded or loaded by
# the SDK, a missing one turns the backend off.
set(OD_FFX_ROOT "${CMAKE_SOURCE_DIR}/third_party/fidelityfx/Kits/FidelityFX")
set(OD_DLSS_ROOT "${CMAKE_SOURCE_DIR}/third_party/dlss")
set(OD_XESS_ROOT "${CMAKE_SOURCE_DIR}/third_party/xess")
set(OD_NIS_ROOT "${CMAKE_SOURCE_DIR}/third_party/nis/NIS")
set(od_sdk_files "${OD_FFX_ROOT}/api/include/ffx_api.h" "${OD_DLSS_ROOT}/include/nvsdk_ngx.h"
    "${OD_XESS_ROOT}/inc/xess/xess_d3d12.h" "${OD_NIS_ROOT}/NIS_Main.hlsl")
foreach(f IN LISTS od_sdk_files)
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "upscaler SDKs: ${f} missing (FidelityFX, DLSS, XeSS and NIS go in third_party/: "
                            "fidelityfx/Kits/FidelityFX, dlss, xess, nis/NIS)")
    endif()
endforeach()

target_sources(opendash PRIVATE src/render/upscaler_fsr.cpp src/render/upscaler_dlss.cpp src/render/upscaler_xess.cpp
    src/render/upscaler_nis.cpp)
target_include_directories(opendash PRIVATE "${OD_FFX_ROOT}/api/include" "${OD_FFX_ROOT}/upscalers/include"
    "${OD_DLSS_ROOT}/include" "${OD_XESS_ROOT}/inc" "${OD_NIS_ROOT}")
target_link_libraries(opendash PRIVATE delayimp
    "${OD_FFX_ROOT}/signedbin/amd_fidelityfx_loader_dx12.lib"
    "${OD_DLSS_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d$<$<CONFIG:Debug>:_dbg>.lib"
    "${OD_XESS_ROOT}/lib/libxess.lib")
target_link_options(opendash PRIVATE /DELAYLOAD:amd_fidelityfx_loader_dx12.dll /DELAYLOAD:libxess.dll)
add_custom_command(TARGET opendash POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${OD_FFX_ROOT}/signedbin/amd_fidelityfx_loader_dx12.dll"
    "${OD_FFX_ROOT}/signedbin/amd_fidelityfx_upscaler_dx12.dll"
    "${OD_DLSS_ROOT}/lib/Windows_x86_64/rel/nvngx_dlss.dll"
    "${OD_XESS_ROOT}/bin/libxess.dll" $<TARGET_FILE_DIR:opendash>)
set(OD_NIS_COMMON NIS_HDR_MODE=0 NIS_BLOCK_WIDTH=32 NIS_THREAD_GROUP_SIZE=256 NIS_USE_HALF_PRECISION=0)
od_add_shader(TARGET opendash FILE "${OD_NIS_ROOT}/NIS_Main.hlsl" ENTRY main PROFILE cs_6_0 NAME g_nis_scaler_cs
              HLSL 2018 DEFINES NIS_SCALER=1 NIS_BLOCK_HEIGHT=24 ${OD_NIS_COMMON})
od_add_shader(TARGET opendash FILE "${OD_NIS_ROOT}/NIS_Main.hlsl" ENTRY main PROFILE cs_6_0 NAME g_nis_sharpen_cs
              HLSL 2018 DEFINES NIS_SCALER=0 NIS_BLOCK_HEIGHT=32 ${OD_NIS_COMMON})
endif() # WIN32: the upscaler SDKs

# --- tests ---
enable_testing()
# the test-only translator between the native game state and dash2.exe's memory layout (tests/translator: the goldens
# compare the native state in the exe's layout; never linked into the game)
file(GLOB od_translator_sources CONFIGURE_DEPENDS tests/translator/*.cpp)
add_library(odtranslator STATIC ${od_translator_sources})
target_include_directories(odtranslator PUBLIC tests)
target_link_libraries(odtranslator PUBLIC odcontent)
if(MSVC)
    target_compile_options(odtranslator PRIVATE /W4 /permissive-)
endif()

add_executable(test_disc tests/test_disc.cpp)
target_link_libraries(test_disc PRIVATE odengine)
target_include_directories(test_disc PRIVATE tests)
add_test(NAME test_disc COMMAND test_disc ${OPENDASH_DISC_CUE})

add_executable(test_ps1_files tests/test_ps1_files.cpp)
target_link_libraries(test_ps1_files PRIVATE odengine)
target_include_directories(test_ps1_files PRIVATE tests)
add_test(NAME test_ps1_files COMMAND test_ps1_files ${OPENDASH_DISC_CUE} ${OPENDASH_DAT_DIR})

add_executable(test_lang tests/test_lang.cpp)
target_link_libraries(test_lang PRIVATE odengine)
target_include_directories(test_lang PRIVATE tests)
add_test(NAME test_lang COMMAND test_lang)

add_executable(test_walker tests/test_walker.cpp)
target_link_libraries(test_walker PRIVATE odtranslator)
target_include_directories(test_walker PRIVATE tests)
add_test(NAME test_walker COMMAND test_walker ${CMAKE_SOURCE_DIR}/tests/data ${OPENDASH_EXE})

add_executable(test_collision tests/test_collision.cpp)
target_link_libraries(test_collision PRIVATE odtranslator)
target_include_directories(test_collision PRIVATE tests)
add_test(NAME test_collision COMMAND test_collision ${OPENDASH_DAT_DIR} ${CMAKE_SOURCE_DIR}/tests/data ${OPENDASH_EXE})

add_executable(test_player tests/test_player.cpp)
target_link_libraries(test_player PRIVATE odtranslator)
target_include_directories(test_player PRIVATE tests)
add_test(NAME test_player COMMAND test_player ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_player_pose tests/test_player_pose.cpp)
target_link_libraries(test_player_pose PRIVATE odtranslator)
target_include_directories(test_player_pose PRIVATE tests)
add_test(NAME test_player_pose COMMAND test_player_pose ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_player_snapshot tests/test_player_snapshot.cpp)
target_link_libraries(test_player_snapshot PRIVATE odtranslator)
target_include_directories(test_player_snapshot PRIVATE tests)
add_test(NAME test_player_snapshot COMMAND test_player_snapshot ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_ghost tests/test_ghost.cpp)
target_link_libraries(test_ghost PRIVATE odtranslator)
target_include_directories(test_ghost PRIVATE tests)
add_test(NAME test_ghost COMMAND test_ghost ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

# the ghost view and its looks (app/ghost_view, app/ghost_looks, app/character_mesh: the app files under test)
add_executable(test_ghost_view tests/test_ghost_view.cpp src/app/ghost_view.cpp src/app/ghost_looks.cpp
    src/app/character_mesh.cpp)
target_link_libraries(test_ghost_view PRIVATE odtranslator SDL3::SDL3)
target_include_directories(test_ghost_view PRIVATE tests)
if(WIN32) # SDL3.dll next to the test
    add_custom_command(TARGET test_ghost_view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:test_ghost_view>
                $<TARGET_FILE_DIR:test_ghost_view>
        COMMAND_EXPAND_LISTS)
endif()
add_test(NAME test_ghost_view COMMAND test_ghost_view ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_player_camera tests/test_player_camera.cpp)
target_link_libraries(test_player_camera PRIVATE odtranslator)
target_include_directories(test_player_camera PRIVATE tests)
add_test(NAME test_player_camera COMMAND test_player_camera ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_player_input tests/test_player_input.cpp)
target_link_libraries(test_player_input PRIVATE odtranslator)
target_include_directories(test_player_input PRIVATE tests)
add_test(NAME test_player_input COMMAND test_player_input ${CMAKE_SOURCE_DIR}/tests/data ${OPENDASH_EXE})

add_executable(test_message tests/test_message.cpp)
target_link_libraries(test_message PRIVATE odtranslator)
target_include_directories(test_message PRIVATE tests)
add_test(NAME test_message COMMAND test_message ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_transition tests/test_transition.cpp)
target_link_libraries(test_transition PRIVATE odtranslator)
target_include_directories(test_transition PRIVATE tests)
add_test(NAME test_transition COMMAND test_transition ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_stage_files tests/test_stage_files.cpp)
target_link_libraries(test_stage_files PRIVATE odtranslator)
target_include_directories(test_stage_files PRIVATE tests)
add_test(NAME test_stage_files COMMAND test_stage_files ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_story tests/test_story.cpp)
target_link_libraries(test_story PRIVATE odtranslator)
target_include_directories(test_story PRIVATE tests)
# test_story runs one process per golden group so `ctest -j N` spreads the goldens over N cores: the goldens
# (tests/data/story_*.bin, not in git) are grouped by their prefix (story_AT, story_ST1E, ...) in chunks of at most
# STORY_CHUNK files; each test passes its file names as the comma-separated filter of test_story. Without goldens the
# single all-goldens test is registered (it fails on "no golden", as before).
set(STORY_CHUNK 4)
file(GLOB STORY_GOLDENS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/data/story_*.bin)
list(SORT STORY_GOLDENS)
set(story_group "")
set(story_files "")
set(story_part 0)
macro(story_flush)
    if(story_files)
        string(REPLACE ";" "," story_filter "${story_files}")
        add_test(NAME test_${story_group}_${story_part}
                 COMMAND test_story ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data 3 ${story_filter})
        math(EXPR story_part "${story_part} + 1")
        set(story_files "")
    endif()
endmacro()
foreach(f ${STORY_GOLDENS})
    get_filename_component(n ${f} NAME)
    string(REGEX MATCH "^story_[^_]+" g ${n})
    if(NOT g STREQUAL story_group)
        story_flush()
        set(story_group ${g})
        set(story_part 0)
    endif()
    list(APPEND story_files ${n})
    list(LENGTH story_files story_len)
    if(story_len GREATER_EQUAL STORY_CHUNK)
        story_flush()
    endif()
endforeach()
story_flush()
if(NOT STORY_GOLDENS)
    add_test(NAME test_story COMMAND test_story ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)
endif()

add_executable(test_storm_draw tests/test_storm_draw.cpp)
target_link_libraries(test_storm_draw PRIVATE odtranslator)
add_test(NAME test_storm_draw COMMAND test_storm_draw ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_weather_draw tests/test_weather_draw.cpp)
target_link_libraries(test_weather_draw PRIVATE odtranslator)
add_test(NAME test_weather_draw COMMAND test_weather_draw ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_actors tests/test_actors.cpp)
target_link_libraries(test_actors PRIVATE odtranslator)
target_include_directories(test_actors PRIVATE tests)
add_test(NAME test_actors COMMAND test_actors ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_audio tests/test_audio.cpp)
target_link_libraries(test_audio PRIVATE odtranslator)
target_include_directories(test_audio PRIVATE tests)
add_test(NAME test_audio COMMAND test_audio ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_spawn tests/test_spawn.cpp)
target_link_libraries(test_spawn PRIVATE odtranslator)
target_include_directories(test_spawn PRIVATE tests)
add_test(NAME test_spawn COMMAND test_spawn ${OPENDASH_DAT_DIR} ${OPENDASH_EXE} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_save tests/test_save.cpp tests/exetables.cpp)
target_link_libraries(test_save PRIVATE odtranslator)
target_include_directories(test_save PRIVATE tests)
set(OPENDASH_GAME_DIR "${od_default_game_dir}" CACHE PATH
    "The user's game directory (DASH2-0*.DAT saves are only read by the tests)")
add_test(NAME test_save COMMAND test_save ${OPENDASH_EXE} ${OPENDASH_GAME_DIR} ${CMAKE_SOURCE_DIR}/tests/data)

add_executable(test_title tests/test_title.cpp)
target_link_libraries(test_title PRIVATE odtranslator)
target_include_directories(test_title PRIVATE tests)
add_test(NAME test_title COMMAND test_title ${OPENDASH_EXE} ${OPENDASH_GAME_DIR} ${CMAKE_SOURCE_DIR}/tests/data)

# the game files' hash of the netplay handshake (engine/game_files_hash)
add_executable(test_game_files_hash tests/test_game_files_hash.cpp)
target_link_libraries(test_game_files_hash PRIVATE odengine)
target_include_directories(test_game_files_hash PRIVATE tests)
add_test(NAME test_game_files_hash COMMAND test_game_files_hash ${OPENDASH_DAT_DIR} ${OPENDASH_EXE})

# the ghosts' smoothing (game/ghost_buffer)
add_executable(test_ghost_buffer tests/test_ghost_buffer.cpp)
target_link_libraries(test_ghost_buffer PRIVATE odgame)
target_include_directories(test_ghost_buffer PRIVATE tests)
add_test(NAME test_ghost_buffer COMMAND test_ghost_buffer)

# the netplay transport / protocol / session (loopback sockets, in process)
add_executable(test_net_protocol tests/test_net_protocol.cpp)
target_link_libraries(test_net_protocol PRIVATE odnet)
target_include_directories(test_net_protocol PRIVATE tests)
add_test(NAME test_net_protocol COMMAND test_net_protocol)

add_executable(test_render_logic tests/test_render_logic.cpp)
target_link_libraries(test_render_logic PRIVATE odrender)
target_include_directories(test_render_logic PRIVATE tests)
add_test(NAME test_render_logic COMMAND test_render_logic)
