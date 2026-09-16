add_library(qtgmslzma STATIC
    7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zStream.c
    Bcj2.c Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c
    Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c LzmaEnc.c LzFind.c
)
set_target_properties(qtgmslzma PROPERTIES AUTOMOC OFF C_STANDARD 99)
target_include_directories(qtgmslzma PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(qtgmslzma PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY Z7_ST WINVER=0x0501 _WIN32_WINNT=0x0501)
