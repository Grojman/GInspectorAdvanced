cmake_minimum_required(VERSION 3.19)
project(GInspectorAdvanced VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg)

option(GIA_BUILD_TESTS "Build the unit tests" ON)

# Everything except main.cpp lives in a static library so the tests can link it.
file(GLOB_RECURSE GIA_LIB_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/core/*.h
    src/model/*.cpp src/model/*.h
    src/ui/*.cpp src/ui/*.h)

add_library(gia_lib STATIC ${GIA_LIB_SOURCES} resources/resources.qrc)
target_include_directories(gia_lib PUBLIC src)
target_link_libraries(gia_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
target_compile_definitions(gia_lib PUBLIC GIA_VERSION="${PROJECT_VERSION}")

set(GIA_APP_SOURCES src/main.cpp)
if(WIN32)
    list(APPEND GIA_APP_SOURCES resources/app.rc)
endif()

qt_add_executable(GInspectorAdvanced WIN32 MACOSX_BUNDLE ${GIA_APP_SOURCES})
target_link_libraries(GInspectorAdvanced PRIVATE gia_lib)

if(MSVC)
    target_compile_options(gia_lib PRIVATE /W4 /utf-8)
else()
    target_compile_options(gia_lib PRIVATE -Wall -Wextra)
endif()

install(TARGETS GInspectorAdvanced
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES resources/ginspectoradvanced.desktop DESTINATION share/applications)
    install(FILES resources/icons/app.svg DESTINATION share/icons/hicolor/scalable/apps
            RENAME ginspectoradvanced.svg)
endif()

if(GIA_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(gia_tests tests/tst_core.cpp)
    target_link_libraries(gia_tests PRIVATE gia_lib Qt6::Test)
    target_compile_definitions(gia_tests PRIVATE GIA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_project")
    add_test(NAME gia_tests COMMAND gia_tests)
endif()
