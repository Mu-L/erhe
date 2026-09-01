CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

# Deviceless: the membership and slot logic of Material_set is pure
# bookkeeping over shared_ptr<Material>, so it is tested with no window, no
# device, no scene and no draw list (doc/draw_list_material_set_plan.md R16).
set(_target "erhe_scene_renderer_tests")
add_executable(${_target}
    main.cpp
    test_material_set.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::scene_renderer
        erhe::primitive
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
