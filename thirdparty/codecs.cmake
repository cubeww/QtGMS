enable_language(C)
add_library(qtgmsvorbis STATIC
    ${CMAKE_CURRENT_LIST_DIR}/ogg/src/bitwise.c
    ${CMAKE_CURRENT_LIST_DIR}/ogg/src/framing.c
)
set(VORBIS_COMPILER_SOURCES mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0 mapping0 registry codebook sharedbook lookup bitrate vorbisenc)
foreach(source IN LISTS VORBIS_COMPILER_SOURCES)
    target_sources(qtgmsvorbis PRIVATE ${CMAKE_CURRENT_LIST_DIR}/vorbis/lib/${source}.c)
endforeach()
set_target_properties(qtgmsvorbis PROPERTIES AUTOMOC OFF C_STANDARD 99)
target_include_directories(qtgmsvorbis PUBLIC ${CMAKE_CURRENT_LIST_DIR}/ogg/include ${CMAKE_CURRENT_LIST_DIR}/vorbis/include PRIVATE ${CMAKE_CURRENT_LIST_DIR}/vorbis/lib)
target_compile_definitions(qtgmsvorbis PRIVATE WINVER=0x0501 _WIN32_WINNT=0x0501)

add_library(qtgmslame STATIC)
set(LAME_COMPILER_SOURCES VbrTag bitstream encoder fft gain_analysis id3tag lame
    newmdct presets psymodel quantize quantize_pvt reservoir set_get tables
    takehiro util vbrquantize version mpglib_interface)
foreach(source IN LISTS LAME_COMPILER_SOURCES)
    target_sources(qtgmslame PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lame/libmp3lame/${source}.c)
endforeach()
set_target_properties(qtgmslame PROPERTIES AUTOMOC OFF C_STANDARD 99)
target_include_directories(qtgmslame PUBLIC ${CMAKE_CURRENT_LIST_DIR}/lame/include
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lame ${CMAKE_CURRENT_LIST_DIR}/lame/libmp3lame)
target_compile_definitions(qtgmslame PRIVATE HAVE_CONFIG_H WINVER=0x0501 _WIN32_WINNT=0x0501)
