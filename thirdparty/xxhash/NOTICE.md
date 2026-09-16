# xxHash

Vendored unmodified `xxhash.c`, `xxhash.h`, and `LICENSE` from xxHash v0.8.3:
https://github.com/Cyan4973/xxHash/tree/v0.8.3

Copyright Yann Collet. BSD 2-Clause license; see LICENSE.

QtGMS uses XXH3-128 for local compiler cache keys, cached-content checksums, and
texture deduplication. It is statically linked using the existing 32-bit target
compiler settings, without raising the required CPU instruction set.
No xxhsum utility code is included. Builds require neither network access nor
the references directory.
