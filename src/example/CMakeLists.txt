message(STATUS "Configuring executable 'example'.")
set(_target "example")
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    config/erhe_graphics.json
    config/mesh_memory.json
    config/window.json

    example.cpp
    example.hpp
    example_log.cpp
    example_log.hpp
    frame_controller.cpp
    frame_controller.hpp
    main.cpp
    programs.cpp
    programs.hpp
    res/models/Box.gltf
    res/shaders/erhe_bxdf.glsl
    res/shaders/erhe_ggx.glsl
    res/shaders/erhe_light.glsl
    res/shaders/erhe_math.glsl
    res/shaders/erhe_srgb.glsl
    res/shaders/erhe_texture.glsl
    res/shaders/erhe_tonemap.glsl
    res/shaders/depth.vert
    res/shaders/depth.frag
    res/shaders/standard.vert
    res/shaders/standard.frag
    
)

# Required for log init
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
        Taskflow
        simdjson
        erhe::codegen
        erhe::utility
        erhe::dataformat
        erhe::file
        erhe::gltf
        erhe::graphics
        erhe::graphics_buffer_sink
        erhe::log
        erhe::math
        erhe::primitive
        erhe::profile
        erhe::raytrace # TODO Make it optional - glTF loader currently uses it and log needs to be initialized..
        erhe::renderer
        erhe::scene
        erhe::scene_renderer
        erhe::ui
        erhe::verify
        erhe::window
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

set(_gfx_gen "${CMAKE_BINARY_DIR}/src/erhe/graphics/erhe_graphics/generated")
set(_example_codegen_sources
    ${_gfx_gen}/graphics_config_serialization.hpp
    ${_gfx_gen}/graphics_config.cpp
    ${_gfx_gen}/opengl_config_serialization.hpp
    ${_gfx_gen}/opengl_config.cpp
    ${_gfx_gen}/vulkan_config_serialization.hpp
    ${_gfx_gen}/vulkan_config.cpp
)
set_source_files_properties(${_example_codegen_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_example_codegen_sources})
source_group("codegen generated" FILES ${_example_codegen_sources})

set(_scene_renderer_gen "${CMAKE_BINARY_DIR}/src/erhe/scene_renderer/erhe_scene_renderer/generated")
set(_scene_renderer_ser_sources
    ${_scene_renderer_gen}/mesh_memory_config_serialization.hpp
    ${_scene_renderer_gen}/mesh_memory_config.cpp
)
set_source_files_properties(${_scene_renderer_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_scene_renderer_ser_sources})
source_group("codegen generated/scene_renderer" FILES ${_scene_renderer_ser_sources})

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEBUGGER_WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target} "erhe-executables")
