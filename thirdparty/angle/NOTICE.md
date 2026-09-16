# ANGLE shader translator

Source: the ANGLE snapshot distributed in Qt qtbase v5.6.3:
https://github.com/qt/qtbase/tree/v5.6.3/src/3rdparty/angle

Archive: https://codeload.github.com/qt/qtbase/tar.gz/refs/tags/v5.6.3
SHA-256: aa2222da44599d05d9e250f30c6e517d7517af3a28528298fac5e1ad4d33a7c5

Only the translator, its headers and common support files are built. No ANGLE
renderer or graphics DLL is required. This snapshot supports the project's
MinGW 4.9.2 and Windows XP deployment baseline.

The generated GLSL and preprocessor lexer/parser sources are checked in, so
building the application requires neither flex nor bison. They were generated
with winflexbison 2.5.25 (flex 2.6.4, bison 3.8.2), using `--no-lines` for bison
and `--noline --nounistd` for flex. Original grammar files are retained.

ANGLE's BSD license is in LICENSE. Third-party files retain their own notices;
generated bison parsers include the Bison skeleton exception.
