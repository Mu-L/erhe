set(_target "imgui")
add_library(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    imgui/imconfig.h
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/imgui_internal.h
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/misc/cpp/imgui_stdlib.h
)
if (${ERHE_FONT_RASTERIZATION_LIBRARY} STREQUAL "freetype")
    add_definitions(
        -DIMGUI_ENABLE_FREETYPE        
        -DIMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION
    )
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        imgui/misc/freetype/imgui_freetype.cpp
        imgui/misc/freetype/imgui_freetype.h
    )
    if (${ERHE_SVG_LIBRARY} STREQUAL "plutosvg")
        add_definitions(-DIMGUI_ENABLE_FREETYPE_PLUTOSVG)
        target_link_libraries(${_target} PRIVATE plutosvg)
    endif ()

    target_link_libraries(${_target} PUBLIC freetype)
endif()
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${_target} PUBLIC glm::glm-header-only)
