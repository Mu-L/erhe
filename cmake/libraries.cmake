include(FetchContent)

FetchContent_Declare(
    bvh
    GIT_REPOSITORY https://github.com/madmann91/bvh.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    cpp-terminal
    GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    embree
    GIT_REPOSITORY https://github.com/embree/embree.git
    GIT_TAG        v3.13.3 # TODO v4.1.0 is out
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG        20.38.17
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        main # TODO v0.8.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    #GIT_TAG        origin/master
    #GIT_TAG        171a020c828669490b98af0c2640e50f1511d2cc
    GIT_TAG        11.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    freetype
    #GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-2
)

# Not in use yet
#FetchContent_Declare(
#    GeometricTools
#    GIT_REPOSITORY https://github.com/tksuoran/GeometricTools.git
#    GIT_TAG        cmake # GTE-version-5.14
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        origin/master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

#FetchContent_Declare(
#    glTF-SDK
#    GIT_REPOSITORY https://github.com/microsoft/glTF-SDK.git
#    GIT_TAG        master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

# Not yet in use
#FetchContent_Declare(
#    googletest
#    GIT_REPOSITORY https://github.com/google/googletest.git
#    GIT_TAG        master
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)


FetchContent_Declare(
    harfbuzz
    #GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    #GIT_TAG        8.4.0
    GIT_REPOSITORY https://github.com/tksuoran/harfbuzz.git
    GIT_TAG        fix-bigobj
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

#FetchContent_Declare(
#    ImGui
#    GIT_REPOSITORY https://github.com/ocornut/imgui.git
#    GIT_TAG        master # v1.81
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

# Not yet in use
#FetchContent_Declare(
#    libigl
#    GIT_REPOSITORY https://github.com/libigl/libigl.git
#    GIT_TAG        v2.3.0
#    GIT_SHALLOW    TRUE
#    GIT_PROGRESS   TRUE
#)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG         master # v5.0.0 has issue
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
)

FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY  https://github.com/sammycage/lunasvg.git
    GIT_TAG         v2.4.0
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
)

FetchContent_Declare(
    MathGeoLib
    #GIT_REPOSITORY https://github.com/juj/MathGeoLib.git
    GIT_REPOSITORY  https://github.com/tksuoran/MathGeoLib.git
    GIT_TAG        origin/winver
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.21
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    nvtx
    GIT_REPOSITORY https://github.com/NVIDIA/NVTX.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    OpenXR-SDK
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    #GIT_TAG        release-1.0.34
    GIT_TAG        release-1.1.38
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.9.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    taskflow
    GIT_REPOSITORY https://github.com/taskflow/taskflow.git
    GIT_TAG        v3.7.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.11.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(
    wuffs
    GIT_REPOSITORY https://github.com/google/wuffs.git
    GIT_TAG        v0.4.0-alpha.5
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
