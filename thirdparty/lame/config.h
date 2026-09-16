#ifndef QTGMS_LAME_CONFIG_H
#define QTGMS_LAME_CONFIG_H

/* Encoder-only configuration for the bundled MinGW Windows build. */
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define LAME_LIBRARY_BUILD 1
#define USE_FAST_LOG 1
typedef float ieee754_float32_t;

#endif
