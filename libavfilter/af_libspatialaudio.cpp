/*
 * Copyright (C) 2024 Marvin Scholz
 * Copyright (C) 2017 VLC authors and VideoLAN
 *
 * Authors: Marvin Scholz <epirat07@gmail.com>
 *
 * Heavily inspired from VLC media players' spatialaudio.cpp
 *   Authors: Adrien Maglo <magsoft@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

extern "C" {
    #include "libavutil/mem.h"
    #include "avfilter.h"
    #include "filters.h"
    #include "libavfilter/af_libspatialaudio_common.h"
}

#include <spatialaudio/Ambisonics.h>
#include <spatialaudio/SpeakersBinauralizer.h>

#define AMB_BLOCK_TIME_LEN 1024

#define AMB_MAX_ORDER 3

struct SpatialaudioContext
{
    SpatialaudioContext()
        : speakers(NULL)
        , inBuf(NULL)
        , outBuf(NULL)
    {}
    ~SpatialaudioContext()
    {
        delete[] speakers;
        if (inBuf != NULL)
            for (unsigned i = 0; i < inputNb; ++i)
                av_free(inBuf[i]);
        av_free(inBuf);

        if (outBuf != NULL)
            for (unsigned i = 0; i < outputNb; ++i)
                av_free(outBuf[i]);
        av_free(outBuf);
    }

    enum
    {
        AMBISONICS_DECODER, // Ambisonics decoding module
        AMBISONICS_BINAURAL_DECODER, // Ambisonics decoding module using binaural
        BINAURALIZER // Binauralizer module
    } mode;

    CAmbisonicBinauralizer binauralDecoder;
    SpeakersBinauralizer binauralizer;
    CAmbisonicDecoder speakerDecoder;
    CAmbisonicProcessor processor;
    CAmbisonicZoomer zoomer;

    CAmbisonicSpeaker *speakers;

    std::vector<float> inputSamples;
    //vlc_tick_t inputPTS;

    // Ambisonic order
    unsigned order;

    unsigned nondiegetic;

    // number of physical left/right channel pairs
    unsigned lr_channels;

    float** inBuf;
    float** outBuf;
    unsigned inputNb;
    unsigned outputNb;

    /* View point. */
    struct {
        float teta;
        float phi;
        float roll;
        float zoom;
    } viewpoint;
};

struct SpatialaudioContext *init_spatialaudio_context(AVFilterContext *ctx) {
  struct SpatialaudioContext *sp;
  sp = new SpatialaudioContext();

  return sp;
}