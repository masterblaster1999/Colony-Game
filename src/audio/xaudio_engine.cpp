// xaudio_engine.cpp
//
// Patch notes:
// - Ensure math constants are available by defining _USE_MATH_DEFINES before <cmath>
//   (adds M_PI, etc.). Also include a fallback #ifndef M_PI guard.
// - Ensure XAudio2 helper inline functions (XAudio2CutoffFrequencyToOnePoleCoefficient,
//   XAudio2CutoffFrequencyToRadians, etc.) are declared by defining XAUDIO2_HELPER_FUNCTIONS
//   before including <xaudio2.h>. This is required on modern Windows SDKs.

// Guard _USE_MATH_DEFINES so we don't trigger C4005 if it's also defined
// on the MSVC command line (/D_USE_MATH_DEFINES).
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#ifndef XAUDIO2_HELPER_FUNCTIONS
#define XAUDIO2_HELPER_FUNCTIONS 1
#endif

#include <xaudio2.h>  // must come after XAUDIO2_HELPER_FUNCTIONS

#include "xaudio_engine.h"

#include <fstream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <limits>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Microsoft::WRL::ComPtr;

namespace colony::audio {


// NOTE: This file has been split into smaller *.inl parts under src/audio/detail/
//       to improve readability while still compiling as a single translation unit.

#include "detail/xaudio_engine_part01_local_helpers.inl"
#include "detail/xaudio_engine_part02_engine_core.inl"
#include "detail/xaudio_engine_part03_update_transport.inl"
#include "detail/xaudio_engine_part04_registry.inl"
#include "detail/xaudio_engine_part05_playback.inl"
#include "detail/xaudio_engine_part06_bus_volumes.inl"
#include "detail/xaudio_engine_part07_bus_fx.inl"
#include "detail/xaudio_engine_part08_ambience.inl"
#include "detail/xaudio_engine_part09_snapshots_rtpcs.inl"
#include "detail/xaudio_engine_part10_queries_callbacks.inl"
#include "detail/xaudio_engine_part11_internals.inl"
#include "detail/xaudio_engine_part12_3d_helpers.inl"
#include "detail/xaudio_engine_part13_wav_loader.inl"
#include "detail/xaudio_engine_part14_polyphony_ducking.inl"
#include "detail/xaudio_engine_part15_ducking_api.inl"
