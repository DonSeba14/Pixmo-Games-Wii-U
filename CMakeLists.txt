cmake_minimum_required(VERSION 3.13)
project(pixmogames C CXX)

# Wii U build (devkitPro powerpc-eabi-cmake). The PC test build uses build-pc.sh.
if(NOT COMMAND wut_create_rpx OR NOT COMMAND wut_create_wuhb)
    message(FATAL_ERROR "Use devkitPro's powerpc-eabi-cmake (wut) to build for Wii U.")
endif()

find_package(SDL2 REQUIRED)

file(GLOB LUDO_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(pixmogames ${LUDO_SOURCES})
target_include_directories(pixmogames PRIVATE src third_party)
target_compile_options(pixmogames PRIVATE -O2 -Wall -Wno-unused-function -Wno-missing-field-initializers
                       -ffunction-sections -fdata-sections)
target_link_libraries(pixmogames PRIVATE SDL2::SDL2-static m)
set_target_properties(pixmogames PROPERTIES LINKER_LANGUAGE CXX)
target_link_options(pixmogames PRIVATE -Wl,--gc-sections)

wut_create_rpx(pixmogames)
wut_create_wuhb(pixmogames
    NAME "Pixmo Games"
    SHORTNAME "Pixmo Games"
    AUTHOR "Pixmo Games"
    ICON "${CMAKE_SOURCE_DIR}/assets/icon.png"
    TVSPLASH "${CMAKE_SOURCE_DIR}/assets/splash_tv.png"
    DRCSPLASH "${CMAKE_SOURCE_DIR}/assets/splash_drc.png")
