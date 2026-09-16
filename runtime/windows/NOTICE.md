# Windows game runtime

These original binaries were copied from the supplied GameMaker: Studio 1.4
installation for use with its Windows VM output:

- Runner.exe — YoYo Games game runtime.
- d3dx9_43.dll and d3dcompiler_43.dll — Microsoft DirectX runtime components.

They remain proprietary components subject to their original license terms;
they are not covered by licenses for QtGMS or its open-source dependencies.
The build deploys this directory alongside QtGMS, and Run copies these binaries
to the project's build directory. Neither deployment nor running uses references/.
