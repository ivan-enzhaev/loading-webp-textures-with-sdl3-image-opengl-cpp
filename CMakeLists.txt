cmake_minimum_required(VERSION 3.21)

# =============================================================================
# Build Configuration Toggle: Switch between "Debug" and "Release"
# =============================================================================
# set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose build type" FORCE)

project(sdl3_image_webp_opengl_cross_platform CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Android Target
# =============================================================================
if(ANDROID)
    # -------------------------------------------------------------------------
    # Paths
    # -------------------------------------------------------------------------
    set(SDL3_DIR       "C:/libs/sdl3-3.4.18-android")
    set(SDL3_IMAGE_DIR "C:/libs/sdl3_image-3.4.8-webp-android")

    # -------------------------------------------------------------------------
    # Native Shared Library Target
    # -------------------------------------------------------------------------
    add_library(main SHARED
        src/main.cpp
    )

    # -------------------------------------------------------------------------
    # Include Directories
    # -------------------------------------------------------------------------
    target_include_directories(main PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL3_DIR}/${ANDROID_ABI}/include
        ${SDL3_IMAGE_DIR}/${ANDROID_ABI}/include
    )

    # -------------------------------------------------------------------------
    # Link SDL3, SDL3_image & Android System Libraries
    # -------------------------------------------------------------------------
    target_link_libraries(main PRIVATE
        ${SDL3_IMAGE_DIR}/${ANDROID_ABI}/lib/libSDL3_image.so
        ${SDL3_DIR}/${ANDROID_ABI}/lib/libSDL3.so
        GLESv3
        EGL
        android
        log
    )

# =============================================================================
# WebAssembly Target (Emscripten)
# =============================================================================
elseif(EMSCRIPTEN)
    # -------------------------------------------------------------------------
    # Paths
    # -------------------------------------------------------------------------
    set(SDL3_DIR       "C:/libs/sdl3-3.4.18-wasm")
    set(SDL3_image_DIR "C:/libs/sdl3_image-3.4.8-webp-wasm")
    set(WEBP_DIR       "C:/libs/webp-1.6.0-wasm")

    # -------------------------------------------------------------------------
    # Executable Target (Emscripten generates .js and .wasm)
    # -------------------------------------------------------------------------
    add_executable(app
        src/main.cpp
    )

    # Set output name to app.js
    set_target_properties(app PROPERTIES
        OUTPUT_NAME "app"
        SUFFIX ".js"
    )

    # -------------------------------------------------------------------------
    # Include Directories
    # -------------------------------------------------------------------------
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL3_DIR}/include
        ${SDL3_image_DIR}/include
        ${WEBP_DIR}/include
    )

    # -------------------------------------------------------------------------
    # Link Static Libraries
    # -------------------------------------------------------------------------
    target_link_libraries(app PRIVATE
        ${SDL3_image_DIR}/lib/libSDL3_image.a
        ${SDL3_DIR}/lib/libSDL3.a
        ${WEBP_DIR}/lib/libwebpdemux.a
        ${WEBP_DIR}/lib/libwebpmux.a
        ${WEBP_DIR}/lib/libwebp.a
        ${WEBP_DIR}/lib/libsharpyuv.a
    )

    # -------------------------------------------------------------------------
    # Emscripten Link Options (WebGL 2.0, Memory Growth, Preloaded Assets)
    # -------------------------------------------------------------------------
    target_link_options(app PRIVATE
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@assets"
    )

# =============================================================================
# Desktop Target (Windows MinGW & Ubuntu Linux)
# =============================================================================
else()
    # -------------------------------------------------------------------------
    # Paths
    # -------------------------------------------------------------------------
    if(WIN32)
        set(GLAD_DIR         "C:/libs/glad-0.1.36/opengl-3.3")
        set(SDL3_DIR         "C:/libs/sdl3-3.4.18-mingw/lib/cmake/SDL3")
        set(SDL3_image_DIR   "C:/libs/sdl3_image-3.4.8-webp-mingw/lib/cmake/SDL3_image")
        set(webp_ROOT        "C:/libs/webp-1.6.0-mingw")
        set(webp_INCLUDE_DIR "C:/libs/webp-1.6.0-mingw/include")
        set(webp_LIBRARY     "C:/libs/webp-1.6.0-mingw/lib/libwebp.a")
    else()
        set(GLAD_DIR         "$ENV{HOME}/libs/glad-0.1.36/opengl-3.3")
        set(SDL3_DIR         "$ENV{HOME}/libs/sdl3-3.4.18-ubuntu/lib/cmake/SDL3")
        set(SDL3_image_DIR   "$ENV{HOME}/libs/sdl3_image-3.4.8-webp-ubuntu/lib/cmake/SDL3_image")
        set(webp_ROOT        "$ENV{HOME}/libs/webp-1.6.0-ubuntu")
        set(webp_INCLUDE_DIR "$ENV{HOME}/libs/webp-1.6.0-ubuntu/include")
        set(webp_LIBRARY     "$ENV{HOME}/libs/webp-1.6.0-ubuntu/lib/libwebp.a")
    endif()

    # -------------------------------------------------------------------------
    # Locate Official SDL3 and SDL3_image Packages
    # -------------------------------------------------------------------------
    find_package(SDL3 REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)

    # -------------------------------------------------------------------------
    # Executable Target
    # -------------------------------------------------------------------------
    add_executable(app
        src/main.cpp
        ${GLAD_DIR}/src/glad.c
    )

    # -------------------------------------------------------------------------
    # Include Directories
    # -------------------------------------------------------------------------
    target_include_directories(app PRIVATE
        ${GLAD_DIR}/include
    )

    # -------------------------------------------------------------------------
    # Copy Assets to Build Directory
    # -------------------------------------------------------------------------
    add_custom_command(TARGET app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:app>/assets
    )

    # -------------------------------------------------------------------------
    # Static C++ Runtime (Applied to BOTH Windows & Ubuntu)
    # -------------------------------------------------------------------------
    target_link_options(app PRIVATE -static-libgcc -static-libstdc++)

    # -------------------------------------------------------------------------
    # Optimization & Symbol Stripping (Release Builds Only)
    # -------------------------------------------------------------------------
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        target_compile_options(app PRIVATE -Os)
        target_link_options(app PRIVATE -s)
    endif()

    # -------------------------------------------------------------------------
    # Platform Linking & Dependencies
    # -------------------------------------------------------------------------
    if(WIN32)
        target_link_libraries(app PRIVATE
            SDL3_image::SDL3_image-static
            SDL3::SDL3-static
            opengl32
            C:/libs/webp-1.6.0-mingw/lib/libwebpdemux.a
            C:/libs/webp-1.6.0-mingw/lib/libwebpmux.a
            C:/libs/webp-1.6.0-mingw/lib/libwebp.a
            C:/libs/webp-1.6.0-mingw/lib/libsharpyuv.a
        )

        # Windows static linking & subsystem selection
        target_link_options(app PRIVATE -static)

        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_link_options(app PRIVATE -mconsole)
        else()
            target_link_options(app PRIVATE -mwindows)
        endif()
    else()
        target_link_libraries(app PRIVATE
            SDL3_image::SDL3_image-static
            SDL3::SDL3-static
            GL
            $ENV{HOME}/libs/webp-1.6.0-ubuntu/lib/libwebpdemux.a
            $ENV{HOME}/libs/webp-1.6.0-ubuntu/lib/libwebpmux.a
            $ENV{HOME}/libs/webp-1.6.0-ubuntu/lib/libwebp.a
            $ENV{HOME}/libs/webp-1.6.0-ubuntu/lib/libsharpyuv.a
        )
    endif()
endif()