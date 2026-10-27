# AMD FSR 3.1 for the Vulkan renderer (outside Windows): the FidelityFX SDK 1.1.x upscaler component and its Vulkan
# backend, built from source into od_ffx_vk (tools/fetch_sdks.py puts the SDK in third_party/fidelityfx-vk; SDK 2.x
# ships D3D12 only). Its GLSL pass shaders are compiled at build time by tools/ffx_sc_glsl.py (FidelityFX-SC's GLSL path,
# whose own executable is Windows only) with the system's glslangValidator. Sets OD_FFX_VK_FOUND.
set(OD_FFX_VK_ROOT "${CMAKE_SOURCE_DIR}/third_party/fidelityfx-vk/sdk")
find_program(OD_GLSLANG glslangValidator DOC "glslangValidator (glslang), compiles FSR's GLSL shaders")
set(OD_FFX_VK_FOUND OFF)
if(NOT EXISTS "${OD_FFX_VK_ROOT}/include/FidelityFX/host/ffx_fsr3upscaler.h" OR NOT OD_GLSLANG)
    message(STATUS "FSR (Vulkan): off (needs third_party/fidelityfx-vk from tools/fetch_sdks.py and glslangValidator)")
    return()
endif()
file(STRINGS "${OD_FFX_VK_ROOT}/include/FidelityFX/host/ffx_types.h" od_ffx_guard REGEX "#ifndef FFX_SDK_DEFAULT_CONTEXT_SIZE")
file(STRINGS "${OD_FFX_VK_ROOT}/src/backends/vk/ffx_vk.cpp" od_ffx_align REGEX "ArraySize.*, 64\\);")
file(STRINGS "${OD_FFX_VK_ROOT}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp" od_ffx_lut REGEX "useLut *= true;")
file(STRINGS "${OD_FFX_VK_ROOT}/src/backends/vk/ffx_vk.cpp" od_ffx_mem REGEX "DEVICE_UNCACHED_BIT_AMD\\) && !")
if(NOT od_ffx_guard OR NOT od_ffx_align OR od_ffx_lut OR NOT od_ffx_mem)
    message(FATAL_ERROR "FSR (Vulkan): third_party/fidelityfx-vk is not patched; run tools/fetch_sdks.py --only fidelityfx-vk")
endif()
set(OD_FFX_VK_FOUND ON)

set(od_ffx_gpu "${OD_FFX_VK_ROOT}/include/FidelityFX/gpu")
set(od_ffx_out "${CMAKE_BINARY_DIR}/generated/ffx_vk")
# CMakeCompileFSR3UpscalerShaders.txt + CMakeShadersFSR3Upscaler.txt (the Vulkan args)
set(od_ffx_args --arg=-e --arg=CS --arg=--target-env --arg=vulkan1.2 --arg=-S --arg=comp --arg=-Os
    -DFFX_GLSL=1 -DFFX_GPU=1
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
    --perm=FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=0,1
    --perm=FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=0,1
    --perm=FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=0,1
    --perm=FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0,1
    --perm=FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=0,1
    --perm=FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0,1
    -I${od_ffx_gpu} -I${od_ffx_gpu}/fsr3upscaler)
file(GLOB od_ffx_shaders "${OD_FFX_VK_ROOT}/src/backends/vk/shaders/fsr3upscaler/*.glsl")
set(od_ffx_headers)
foreach(shader ${od_ffx_shaders})
    get_filename_component(n ${shader} NAME_WE)
    # wave32 and wave64 are the same SPIR-V in GLSL; 16bit = FFX_HALF=1
    foreach(half 0 1)
        if(half)
            set(names --name=${n}_16bit --name=${n}_wave64_16bit)
            set(h ${od_ffx_out}/${n}_16bit_permutations.h)
        else()
            set(names --name=${n} --name=${n}_wave64)
            set(h ${od_ffx_out}/${n}_permutations.h)
        endif()
        add_custom_command(OUTPUT ${h}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ffx_sc_glsl.py --glslang=${OD_GLSLANG}
                    --output=${od_ffx_out} ${names} ${od_ffx_args} -DFFX_HALF=${half} ${shader}
            DEPENDS ${shader} ${CMAKE_SOURCE_DIR}/tools/ffx_sc_glsl.py
            DEPFILE ${h}.d
            COMMENT "FSR shader ${n} (half=${half})")
        list(APPEND od_ffx_headers ${h})
    endforeach()
endforeach()
add_custom_target(od_ffx_vk_shaders DEPENDS ${od_ffx_headers})

add_library(od_ffx_vk STATIC
    ${OD_FFX_VK_ROOT}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
    ${OD_FFX_VK_ROOT}/src/backends/vk/ffx_vk.cpp
    ${OD_FFX_VK_ROOT}/src/backends/shared/ffx_shader_blobs.cpp
    ${OD_FFX_VK_ROOT}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
    ${OD_FFX_VK_ROOT}/src/shared/ffx_assert.cpp
    ${OD_FFX_VK_ROOT}/src/shared/ffx_message.cpp
    ${OD_FFX_VK_ROOT}/src/shared/ffx_breadcrumbs_list.cpp
    ${OD_FFX_VK_ROOT}/src/shared/ffx_object_management.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ffx_vk_no_framegen.cpp)
add_dependencies(od_ffx_vk od_ffx_vk_shaders)
target_compile_definitions(od_ffx_vk PRIVATE FFX_FSR3UPSCALER FFX_GCC)
# twice the SDK's words: wchar_t is 4 bytes here (tools/fetch_sdks.py makes the define overridable); PUBLIC, the
# context's size must agree with the app's
target_compile_definitions(od_ffx_vk PUBLIC "FFX_SDK_DEFAULT_CONTEXT_SIZE=(1024 * 256)")
set_target_properties(od_ffx_vk PROPERTIES CXX_STANDARD 17) # std::wstring_convert (the backend's UTF-8 names)
target_compile_options(od_ffx_vk PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/ffx_gcc_compat.h)
target_include_directories(od_ffx_vk PUBLIC ${OD_FFX_VK_ROOT}/include)
target_include_directories(od_ffx_vk PRIVATE ${OD_FFX_VK_ROOT}/src/shared ${OD_FFX_VK_ROOT}/src/components
    ${OD_FFX_VK_ROOT}/src/backends/shared ${OD_FFX_VK_ROOT}/src/backends/shared/blob_accessors ${od_ffx_out})
target_link_libraries(od_ffx_vk PUBLIC Vulkan::Headers)
target_compile_options(od_ffx_vk PRIVATE -w) # third-party code
