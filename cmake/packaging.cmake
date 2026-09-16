# Install into CPack's fresh staging directory; never archive the build tree.
install(TARGETS QtGMS RUNTIME DESTINATION .)
install(FILES ${ActionLibraries} DESTINATION lib)
install(FILES
    runtime/windows/Runner.exe
    runtime/windows/d3dx9_43.dll
    runtime/windows/d3dcompiler_43.dll
    runtime/windows/NOTICE.md
    DESTINATION runtime/windows)
install(FILES packaging/qt.conf DESTINATION .)
install(FILES packaging/README.txt DESTINATION .)
install(FILES LICENSE DESTINATION .)
install(DIRECTORY packaging/licenses/ DESTINATION licenses)

foreach(Library miniaudio vorbis ogg lame angle xxhash stb lzma)
    foreach(License LICENSE COPYING NOTICE.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${Library}/${License}")
            install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${Library}/${License}"
                DESTINATION "licenses/${Library}")
        endif()
    endforeach()
endforeach()
install(FILES thirdparty/angle/src/third_party/compiler/LICENSE
    DESTINATION licenses/angle RENAME ArrayBoundsClamper-LICENSE)
install(FILES thirdparty/angle/src/third_party/murmurhash/LICENSE
    DESTINATION licenses/angle RENAME MurmurHash-LICENSE)

get_filename_component(PackageCompilerBin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(PackageCompilerRoot "${PackageCompilerBin}" DIRECTORY)
foreach(Library gcc mingw-w64 winpthreads)
    install(DIRECTORY "${PackageCompilerRoot}/licenses/${Library}/" DESTINATION "licenses/mingw/${Library}")
endforeach()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploypackage.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/deploypackage.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploypackage.cmake")

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME QtGMS)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "QtGMS-${PROJECT_VERSION}-windows-xp-x86")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
