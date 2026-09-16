# miniaudio and stb_vorbis

- Source: https://github.com/mackron/miniaudio/tree/0.11.25
- Vendored release: miniaudio 0.11.25, including its extras/stb_vorbis.c (v1.22).
- miniaudio: Copyright 2025 David Reid, distributed under the MIT option in LICENSE.
- stb_vorbis: Copyright (c) 2017 Sean Barrett, distributed under the MIT option below.
- stb_vorbis.c uses standard `fopen` for file decoding so the executable does not import `fopen_s`, which is absent from the Windows XP system CRT.
- miniaudio.h has one local WAV reader fix: ignore `cbSize` for `WAVE_FORMAT_PCM`, using the `fmt ` chunk length to locate the next chunk. This follows [Microsoft's WAVEFORMATEX documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex) and permits PCM files with uninitialized extension-size fields. Other WAV formats retain their original extension parsing.
- backend.h/backend.cpp enable only WinMM playback and WAV, MP3 and Vorbis decoding for QtGMS; no build-time download is required.
- Backend documentation: https://miniaud.io/docs/manual/index.html (WinMM supports Windows 95 and later).

## stb_vorbis MIT license

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to
do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
