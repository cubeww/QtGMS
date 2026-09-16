cmake_minimum_required(VERSION 3.21)

project(QtGMS VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Keep the deployment baseline explicit: the editor must run on Windows XP.
find_package(Qt5 5.6.3 EXACT REQUIRED COMPONENTS Widgets Xml PrintSupport)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "QtGMS requires a Windows 32-bit toolchain for Windows XP.")
endif()

enable_language(RC)

add_library(qtgmsaudio STATIC thirdparty/miniaudio/backend.cpp)
set_target_properties(qtgmsaudio PROPERTIES AUTOMOC OFF)
target_include_directories(qtgmsaudio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio")
target_compile_definitions(qtgmsaudio PRIVATE WINVER=0x0501 _WIN32_WINNT=0x0501)
include(thirdparty/codecs.cmake)
include(thirdparty/shaders.cmake)
add_subdirectory(thirdparty/lzma)

add_library(qtgmsxxhash STATIC thirdparty/xxhash/xxhash.c)
set_target_properties(qtgmsxxhash PROPERTIES AUTOMOC OFF C_STANDARD 99)
target_include_directories(qtgmsxxhash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xxhash")

add_library(qtgmscompiler STATIC
    src/scriptsource.cpp
    src/scriptsource.h
    src/compiler/builddirectory.cpp
    src/compiler/datawriter.cpp
    src/compiler/gmlparser.cpp
    src/compiler/gmlcompiler.cpp
    src/compiler/audiocompiler.cpp
    src/compiler/compilerbuild.cpp
    src/compiler/compilerassets.cpp
    src/compiler/compilerobjects.cpp
    src/compiler/compilerextensions.cpp
    src/compiler/texturecompiler.cpp
    src/compiler/projectcompiler.cpp
    src/compiler/compilejob.cpp
    src/compiler/compilercache.cpp
    src/compiler/fasthash.cpp
    src/compiler/shadercompiler.cpp
)
target_link_libraries(qtgmscompiler PUBLIC Qt5::Widgets Qt5::Xml qtgmsaudio qtgmsvorbis qtgmslame qtgmsangle qtgmsxxhash)
target_include_directories(qtgmscompiler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(QtGMS WIN32
    src/main.cpp
    src/editorlanguage.cpp
    src/editorlanguage.h
    src/preferencesdialog.cpp
    src/codeeditorsettings.cpp
    src/codeeditorsettings.h
    src/preferencesdialog.h
    src/objecteventtranslations.cpp
    src/objecteventtranslations.h
    src/mainwindow.cpp
    src/mainwindowrecent.cpp
    src/mainwindowsearch.cpp
    src/scriptsearch.cpp
    src/scriptsearch.h
    src/scriptsearchwindow.cpp
    src/scriptsearchwindow.h
    src/mainwindowcompile.cpp
    src/gamerunner.cpp
    src/gamerunner.h
    src/mainwindowimport.cpp
    src/mainwindow.h
    src/project.cpp
    src/resourceselector.cpp
    src/resourceselector.h
    src/resourcereferences.cpp
    src/resourcereferences.h
    src/projectfiletransaction.cpp
    src/projectfiletransaction.h
    src/mainwindowtree.cpp
    src/resourcetreerequest.h
    src/projecttree.cpp
    src/projectcopy.cpp
    src/extensionpackage.cpp
    src/extensionpackage.h
    src/projectremove.cpp
    src/projectrename.cpp
    src/resourceeditorwindow.cpp
    src/projectconfigurations.cpp
    src/configurationmanager.cpp
    src/configurationmanager.h
    src/projectstorage.cpp
    src/projectexportjob.cpp
    src/projectexportjob.h
    src/sevenzipwriter.cpp
    src/sevenzipwriter.h
    src/mainwindowexport.cpp
    src/mainwindowprojectimport.cpp
    src/projectimportjob.cpp
    src/projectimportjob.h
    src/projectarchivedialog.cpp
    src/projectarchivedialog.h
    src/sevenziparchive.cpp
    src/sevenziparchive.h
    src/project.h
    src/projectloader.cpp
    src/projectloader.h
    src/editorwindow.cpp
    src/editorwindow.h
    src/editordialog.cpp
    src/editordialog.h
    src/dialogframe.cpp
    src/dialogframe.h
    src/editorstandarddialogs.cpp
    src/editorstandarddialogs.h
    src/windowtitlebar.cpp
    src/windowtitlebar.h
    src/resourcebrowser.cpp
    src/resourcebrowser.h
    src/resourceitemdelegate.cpp
    src/resourceitemdelegate.h
    src/compilepanel.cpp
    src/compilepanel.h
    src/editortheme.cpp
    src/editortheme.h
    src/editorstyle.cpp
    src/editorstyle.h
    src/imagecanvas.cpp
    src/imagecanvas.h
    src/imageeditorwindow.cpp
    src/imageeditorwindow.h
    src/imageoperations.cpp
    src/imageoperations.h
    src/imageresizer.cpp
    src/imageresizer.h
    src/imageoperationdialog.cpp
    src/imageoperationdialog.h
    src/imagepalette.cpp
    src/imagepalette.h
    src/spritedocument.cpp
    src/spritedocument.h
    src/spritepropertieswindow.cpp
    src/spritepropertieswindow.h
    src/resourceeditorwindow.h
    src/backgrounddocument.cpp
    src/backgrounddocument.h
    src/backgroundpropertieswindow.cpp
    src/backgroundpropertieswindow.h
    src/sounddocument.cpp
    src/sounddocument.h
    src/soundpropertieswindow.cpp
    src/soundpropertieswindow.h
    src/audiopreview.cpp
    src/audiopreview.h
    src/textfiledocument.cpp
    src/textfiledocument.h
    src/codeeditor.cpp
    src/codeeditor.h
    src/codeblockdata.h
    src/codecompletionitem.h
    src/gmlsymbols.h
    src/gmlsymbols.cpp
    src/codecompletion.h
    src/codecompletion.cpp
    src/codesignaturehelp.h
    src/codesignaturehelp.cpp
    src/codeeditorcolors.cpp
    src/codeeditorcolors.h
    src/codeeditorpanel.cpp
    src/codeeditorpanel.h
    src/codefindpanel.cpp
    src/codefindpanel.h
    src/gmlhighlighter.cpp
    src/gmlhighlighter.h
    src/scripteditorwindow.cpp
    src/scripteditorwindow.h
    src/shaderdocument.cpp
    src/shaderdocument.h
    src/shadereditorwindow.cpp
    src/shadereditorwindow.h
    src/shaderhighlighter.cpp
    src/shaderhighlighter.h
    src/fontdocument.cpp
    src/actionlibrary.cpp
    src/actionlibrary.h
    src/actionxml.cpp
    src/actionxml.h
    src/actioneditorpanel.cpp
    src/actioneditorpanel.h
    src/timelinedocument.cpp
    src/timelinedocument.h
    src/richtextdocument.cpp
    src/richtextdocument.h
    src/richtexteditor.cpp
    src/richtexteditor.h
    src/gameinformationwindow.cpp
    src/gameinformationwindow.h
    src/pathdocument.cpp
    src/pathdocument.h
    src/pathcanvas.cpp
    src/pathcanvas.h
    src/pathpropertieswindow.cpp
    src/pathpropertieswindow.h
    src/projectgamesettings.cpp
    src/gamesettingsdocument.cpp
    src/gamesettingsdocument.h
    src/gamesettingswindow.cpp
    src/gamesettingswindow.h
    src/projectmacros.cpp
    src/macrodocument.cpp
    src/macrodocument.h
    src/macroeditorwindow.cpp
    src/macroeditorwindow.h
    src/extensiondocument.cpp
    src/extensiondocument.h
    src/extensioneditordialogs.cpp
    src/extensioneditordialogs.h
    src/extensionpropertieswindow.cpp
    src/extensionpropertieswindow.h
    src/roomdocument.cpp
    src/roomdocument.h
    src/roomassets.cpp
    src/roomassets.h
    src/roomcanvas.cpp
    src/roomcanvas.h
    src/roomoverview.cpp
    src/roomoverview.h
    src/roompropertieswindow.cpp
    src/roompropertieswindow.h
    src/timelinepropertieswindow.cpp
    src/timelinepropertieswindow.h
    src/objectdocument.cpp
    src/objectdocument.h
    src/actioneditordialog.cpp
    src/actioneditordialog.h
    src/codesnippeteditorwindow.cpp
    src/codesnippeteditorwindow.h
    src/codeactioneditorwindow.cpp
    src/codeactioneditorwindow.h
    src/objecteventdialog.cpp
    src/objecteventdialog.h
    src/actionpalette.cpp
    src/actionpalette.h
    src/objectpropertieswindow.cpp
    src/objectcodeeditors.cpp
    src/objectpropertieswindow.h
    resources/objectresources.qrc
    src/fontdocument.h
    src/fontatlas.cpp
    src/fontatlas.h
    src/fontpropertieswindow.cpp
    src/fontpropertieswindow.h
    src/fontrangedialog.cpp
    src/fontrangedialog.h
    src/spriteframeswindow.cpp
    src/spriteframescommands.cpp
    src/spriteanimation.cpp
    src/spriteanimation.h
    src/spriteframeswindow.h
    src/spritemaskwindow.cpp
    src/spritemaskwindow.h
    resources/resources.qrc
    resources/projecttemplate.qrc
    resources/application.rc
)

target_link_libraries(QtGMS PRIVATE Qt5::Widgets Qt5::Xml Qt5::PrintSupport qtgmscompiler qtgmsaudio qtgmslzma user32 gdi32 winmm comdlg32)
# Fractional Windows DPI scaling uses the matching, pinned Qt 5.6.3 internals.
set_source_files_properties(src/main.cpp PROPERTIES INCLUDE_DIRECTORIES "${Qt5Gui_PRIVATE_INCLUDE_DIRS}")
target_include_directories(QtGMS PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_compile_definitions(QtGMS PRIVATE
    WINVER=0x0501
    _WIN32_WINNT=0x0501
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MINGW)
    target_compile_options(qtgmscompiler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(QtGMS PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(QtGMS PRIVATE
        -Wl,--major-subsystem-version,5,--minor-subsystem-version,1
    )
elseif(MSVC)
    target_compile_options(QtGMS PRIVATE /W4)
    target_link_options(QtGMS PRIVATE /SUBSYSTEM:WINDOWS,5.01)
endif()

get_target_property(QtQmakeExecutable Qt5::qmake IMPORTED_LOCATION)
get_filename_component(QtBinDirectory "${QtQmakeExecutable}" DIRECTORY)
find_program(QtLreleaseExecutable lrelease HINTS "${QtBinDirectory}" NO_DEFAULT_PATH REQUIRED)
find_program(QtLupdateExecutable lupdate HINTS "${QtBinDirectory}" NO_DEFAULT_PATH REQUIRED)
set(TranslationSources)
set(TranslationResources "<RCC><qresource prefix=\"/translations\">")
foreach(Language zh_CN ja_JP ko_KR)
    set(TranslationSource "${CMAKE_CURRENT_SOURCE_DIR}/translations/qtgms_${Language}.ts")
    set(TranslationBinary "${CMAKE_CURRENT_BINARY_DIR}/translations/qtgms_${Language}.qm")
    list(APPEND TranslationSources "${TranslationSource}")
    add_custom_command(OUTPUT "${TranslationBinary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND "${QtLreleaseExecutable}" "${TranslationSource}" -qm "${TranslationBinary}"
        DEPENDS "${TranslationSource}" VERBATIM)
    list(APPEND TranslationBinaries "${TranslationBinary}")
    string(APPEND TranslationResources "<file alias=\"qtgms_${Language}.qm\">${TranslationBinary}</file>")
    string(REGEX REPLACE "_.*" "" QtLanguage "${Language}")
    if(Language STREQUAL "zh_CN")
        set(QtLanguage zh_CN)
        set(QtTranslationName qt)
    else()
        set(QtTranslationName qtbase)
    endif()
    set(QtTranslation "${QtBinDirectory}/../translations/${QtTranslationName}_${QtLanguage}.qm")
    list(APPEND QtTranslations "${QtTranslation}")
    string(APPEND TranslationResources "<file alias=\"qt_${Language}.qm\">${QtTranslation}</file>")
endforeach()
string(APPEND TranslationResources "</qresource></RCC>")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc" CONTENT "${TranslationResources}")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qrc_translations.cpp"
    COMMAND Qt5::rcc -name translations -o "${CMAKE_CURRENT_BINARY_DIR}/qrc_translations.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
    DEPENDS ${TranslationBinaries} ${QtTranslations} "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
    VERBATIM)
target_sources(QtGMS PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/qrc_translations.cpp")
add_custom_target(update-translations
    COMMAND "${QtLupdateExecutable}" "${CMAKE_CURRENT_SOURCE_DIR}/src" -no-obsolete -locations none -ts ${TranslationSources}
    VERBATIM)
find_program(QtDeployExecutable windeployqt HINTS "${QtBinDirectory}" NO_DEFAULT_PATH REQUIRED)

set(ActionLibraries
    resources/actionlibraries/01_move.lib
    resources/actionlibraries/02_main1.lib
    resources/actionlibraries/03_main2.lib
    resources/actionlibraries/04_control.lib
    resources/actionlibraries/05_score.lib
    resources/actionlibraries/06_extra.lib
    resources/actionlibraries/07_draw.lib
)
add_custom_target(action-libraries ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${ActionLibraries} "$<TARGET_FILE_DIR:QtGMS>/lib"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
add_dependencies(QtGMS action-libraries)

add_custom_target(game-runtime ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/runtime/windows"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/windows/Runner.exe"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/windows/d3dx9_43.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/windows/d3dcompiler_43.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/windows/NOTICE.md"
        "$<TARGET_FILE_DIR:QtGMS>/runtime/windows"
    VERBATIM
)
add_dependencies(QtGMS game-runtime)

add_custom_target(deploy
    COMMAND "${QtDeployExecutable}" --release --no-translations
        --no-system-d3d-compiler --no-angle --no-opengl-sw
        "$<TARGET_FILE:QtGMS>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/licenses/miniaudio"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio/NOTICE.md"
        "$<TARGET_FILE_DIR:QtGMS>/licenses/miniaudio"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/licenses/vorbis" "$<TARGET_FILE_DIR:QtGMS>/licenses/ogg"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vorbis/COPYING" "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vorbis/NOTICE.md" "$<TARGET_FILE_DIR:QtGMS>/licenses/vorbis"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ogg/COPYING" "$<TARGET_FILE_DIR:QtGMS>/licenses/ogg"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/licenses/lame"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lame/COPYING" "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lame/NOTICE.md" "$<TARGET_FILE_DIR:QtGMS>/licenses/lame"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/licenses/angle"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/angle/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/angle/NOTICE.md" "$<TARGET_FILE_DIR:QtGMS>/licenses/angle"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/angle/src/third_party/compiler/LICENSE" "$<TARGET_FILE_DIR:QtGMS>/licenses/angle/ArrayBoundsClamper-LICENSE"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtGMS>/licenses/xxhash"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xxhash/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xxhash/NOTICE.md" "$<TARGET_FILE_DIR:QtGMS>/licenses/xxhash"
    DEPENDS QtGMS
    COMMENT "Deploying the Qt runtime beside QtGMS"
    VERBATIM
)

include(cmake/packaging.cmake)
