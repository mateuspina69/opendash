# The app's dependencies outside Windows, where vcpkg is not assumed: NVRHI (its Vulkan backend; it fetches the
# Vulkan-Headers itself), Dear ImGui with its SDL3 backend and DirectXMath are fetched at pinned versions; DXC
# (directx-shader-compiler) comes from the system's packages.
include(FetchContent)

set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
    GIT_SHALLOW FALSE
    EXCLUDE_FROM_ALL)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_Declare(directxmath
    GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
    GIT_TAG may2026
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none) # header only: its own CMake project is not needed
FetchContent_MakeAvailable(nvrhi imgui directxmath)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
add_library(imgui::imgui ALIAS imgui)

# DirectXMath outside Windows needs sal.h: the annotations it uses, empty (cmake/sal/sal.h)
add_library(od_directxmath INTERFACE)
target_include_directories(od_directxmath INTERFACE ${directxmath_SOURCE_DIR}/Inc ${CMAKE_CURRENT_LIST_DIR}/sal)

find_library(OD_VULKAN_LOADER NAMES vulkan libvulkan.so.1 REQUIRED DOC "the Vulkan loader (vulkan-icd-loader)")
