#ifndef QTGMS_AUDIOBACKEND_H
#define QTGMS_AUDIOBACKEND_H
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WINMM
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include "miniaudio.h"
#endif
