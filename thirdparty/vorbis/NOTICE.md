# Xiph audio encoder

libvorbis 1.3.7 and libogg 1.3.6 are statically built into QtGMS for in-process
audio conversion. Original licenses are in each library's COPYING file.

Sources: https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz
and https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.gz

SHA256:
- libvorbis: 0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab
- libogg: 83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638

The upstream lib/include and src/include source trees are retained without
changes. QtGMS builds the encoder sources through thirdparty/codecs.cmake.
