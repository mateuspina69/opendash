# HLSL -> DXIL at build time (DXC from vcpkg's directx-dxc), or SPIR-V for the Vulkan renderer outside Windows (the
# system's dxc). Each shader becomes a header with a byte array.
if(WIN32)
    find_package(directx-dxc CONFIG REQUIRED)
    set(OD_SHADER_TARGET_FLAGS "")
else()
    find_program(DIRECTX_DXC_TOOL dxc REQUIRED DOC "DXC (the directx-shader-compiler package)")
    # the registers move to NVRHI's default Vulkan binding offsets (nvrhi::VulkanBindingOffsets: t 0, s 128, b 256,
    # u 384); the constant buffers keep the D3D layout the C++ structs follow
    set(OD_SHADER_TARGET_FLAGS -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -fvk-t-shift 0 0
        -fvk-s-shift 128 0 -fvk-b-shift 256 0 -fvk-u-shift 384 0)
endif()
set(OD_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${OD_SHADER_OUT}")

# od_add_shader(TARGET <tgt> FILE <hlsl> ENTRY <fn> PROFILE <vs_6_0|ps_6_5|cs_6_0 ...: the lowest the shader needs> NAME <symbol> [HLSL 2018]
#               [DEFINES A=1 ...] [INCLUDES dir ...])
#   -> ${OD_SHADER_OUT}/<symbol>.h (const unsigned char <symbol>[]), on <tgt>'s include path
function(od_add_shader)
    cmake_parse_arguments(S "" "TARGET;FILE;ENTRY;PROFILE;NAME;HLSL" "DEFINES;INCLUDES" ${ARGN})
    if(NOT S_HLSL)
        set(S_HLSL 2021)
    endif()
    set(out "${OD_SHADER_OUT}/${S_NAME}.h")
    set(defs "")
    foreach(d ${S_DEFINES})
        list(APPEND defs -D ${d})
    endforeach()
    set(incs "")
    foreach(i ${S_INCLUDES})
        list(APPEND incs -I "${i}")
    endforeach()
    get_filename_component(src "${S_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${DIRECTX_DXC_TOOL}" -nologo -T ${S_PROFILE} -E ${S_ENTRY} ${defs} ${incs} -HV ${S_HLSL} -Zpr -O3
                ${OD_SHADER_TARGET_FLAGS} -Fh "${out}" -Vn ${S_NAME} "${src}"
        DEPENDS "${src}"
        COMMENT "dxc ${S_FILE}:${S_ENTRY} -> ${S_NAME}.h" VERBATIM)
    target_sources(${S_TARGET} PRIVATE "${out}")
    target_include_directories(${S_TARGET} PRIVATE "${OD_SHADER_OUT}")
endfunction()
