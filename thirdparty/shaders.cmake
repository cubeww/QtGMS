file(GLOB QTGMS_ANGLE_TRANSLATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/compiler/translator/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/compiler/translator/depgraph/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/compiler/translator/timing/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/compiler/preprocessor/*.cpp")
add_library(qtgmsangle STATIC ${QTGMS_ANGLE_TRANSLATOR_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/third_party/compiler/ArrayBoundsClamper.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/common/angleutils.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/common/debug.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/common/mathutil.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/common/tls.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/angle/src/common/utilities.cpp")
set_target_properties(qtgmsangle PROPERTIES AUTOMOC OFF)
target_include_directories(qtgmsangle PUBLIC "${CMAKE_CURRENT_LIST_DIR}/angle/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/angle/src")
target_compile_definitions(qtgmsangle PRIVATE ANGLE_ENABLE_HLSL ANGLE_TRANSLATOR_STATIC
    _WIN32_WINNT=0x0501 WINVER=0x0501 NOMINMAX)
