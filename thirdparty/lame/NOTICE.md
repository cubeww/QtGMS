# LAME MP3 encoder

Version: 3.100 (upstream source release).
Source: https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download
Archive SHA-256: `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`.

Copyright belongs to the LAME authors; see the individual source headers and
`AUTHORS`. Distributed under the GNU Library General Public License, version 2
or later; see `COPYING`.

QtGMS builds the encoder library directly for legacy Windows streamed sounds.
Decoding and resampling remain in miniaudio. The command-line frontend, MP3
decoder, assembly routines and vector routines are not built. `config.h` is the
QtGMS build configuration; upstream encoder sources are unmodified.
