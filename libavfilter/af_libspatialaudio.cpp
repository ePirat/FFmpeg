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
    #include "libavutil/avassert.h"
    #include "libavutil/mem.h"
    #include "avfilter.h"
    #include "filters.h"
    #include "libavfilter/af_libspatialaudio_common.h"
}

#include <algorithm>
#include <iterator>
#include <vector>
#include <map>

#include <spatialaudio/Ambisonics.h>
#include <spatialaudio/SpeakersBinauralizer.h>

struct SpatialaudioContext
{
    SpatialaudioContext() {}

    ~SpatialaudioContext() {}

    void *logctx;

    SpatialaudioMode mode;

    CAmbisonicBinauralizer binauralDecoder;
    SpeakersBinauralizer binauralizer;
    CAmbisonicDecoder speakerDecoder;
    CAmbisonicProcessor processor;
    CAmbisonicZoomer zoomer;

    // Ambisonic order
    unsigned ambisonicOrder;
    // Non-diegetic channels
    unsigned nondiegeticChCount;

    unsigned inChCount;
    unsigned outChCount;

    /* View point */
    struct {
        float theta;
        float phi;
        float roll;
        float zoom;
    } viewpoint;

};

// Speaker positions according to Rec. ITU-R BS.2051-3
static const std::map<enum AVChannel, PolarPoint> speakerPositions = {
    { AV_CHAN_FRONT_LEFT,    { DegreesToRadians(+30),  0.f, 1.0f } },
    { AV_CHAN_FRONT_RIGHT,   { DegreesToRadians(-30),  0.f, 1.0f } },
    { AV_CHAN_SIDE_LEFT,     { DegreesToRadians(+110), 0.f, 1.0f } },
    { AV_CHAN_SIDE_RIGHT,    { DegreesToRadians(-110),  0.f, 1.0f } },
    { AV_CHAN_BACK_LEFT,     { DegreesToRadians(+145),  0.f, 1.0f } },
    { AV_CHAN_BACK_RIGHT,    { DegreesToRadians(-145),  0.f, 1.0f } },
    { AV_CHAN_BACK_CENTER,   { DegreesToRadians(+0),    0.f, 1.0f } },
    { AV_CHAN_FRONT_CENTER,  { DegreesToRadians(+0),    0.f, 1.0f } },
    { AV_CHAN_LOW_FREQUENCY, { DegreesToRadians(+0),    0.f, 0.5f } },
};

// L/R channels
// This list must be ordered so that the left channels is always followed by the right channel!
static const std::vector<enum AVChannel> stereoChannels = {
    AV_CHAN_FRONT_LEFT,
    AV_CHAN_FRONT_RIGHT,

    AV_CHAN_SIDE_LEFT,
    AV_CHAN_SIDE_RIGHT,

    AV_CHAN_BACK_LEFT,
    AV_CHAN_BACK_RIGHT,
};

int spatialaudio_context_create(struct SpatialaudioContext **spctx, void *logctx)
{
    void *storage = av_malloc(sizeof(SpatialaudioContext));
    if (!storage)
        return AVERROR(ENOMEM);

    *spctx = new(storage) SpatialaudioContext();
    (*spctx)->logctx = logctx;
    return 0;
}

void spatialaudio_context_set_mode(struct SpatialaudioContext *spctx, enum SpatialaudioMode mode)
{
    spctx->mode = mode;
}

void spatialaudio_context_set_viewpoint(struct SpatialaudioContext *spctx, float yaw, float pitch, float roll, float fov)
{
    spctx->viewpoint.theta  = -DegreesToRadians(yaw);
    spctx->viewpoint.phi    =  DegreesToRadians(pitch);
    spctx->viewpoint.roll   =  DegreesToRadians(roll);

    if (fov >= FOV_DEGREES_DEFAULT)
        spctx->viewpoint.zoom = 0.f; // no unzoom as it does not really make sense.
    else
        spctx->viewpoint.zoom = (FOV_DEGREES_DEFAULT - fov) / (FOV_DEGREES_DEFAULT - FOV_DEGREES_MIN);
}

int spatialaudio_context_configure_input(struct SpatialaudioContext *spctx, const AVChannelLayout *in_layout)
{
    spctx->inChCount = in_layout->nb_channels;

    if (in_layout->order == AV_CHANNEL_ORDER_AMBISONIC) {
        int order = av_channel_layout_ambisonic_order(in_layout);
        if (order < 0 || order > AMB_MAX_ORDER) {
            av_log(spctx->logctx, AV_LOG_ERROR, "unsupported/invalid ambisonic order\n");
            return AVERROR(EINVAL);
        }
        spctx->ambisonicOrder = order;
        spctx->nondiegeticChCount = spctx->inChCount - (spctx->ambisonicOrder + 1) * (spctx->ambisonicOrder + 1);

        av_log(spctx->logctx, AV_LOG_VERBOSE, "channels: %d, ambisonic order: %d, non-diegetic channels: %d\n",
            spctx->inChCount, spctx->ambisonicOrder, spctx->nondiegeticChCount);

        if (spctx->nondiegeticChCount > 0 &&
            (spctx->nondiegeticChCount != 2 ||
                av_channel_layout_subset(in_layout, AV_CH_LAYOUT_STEREO) != AV_CH_LAYOUT_STEREO))
        {
            av_log(spctx->logctx, AV_LOG_ERROR, "Invalid amount of non-diegetic channels: %d\n", spctx->nondiegeticChCount);
            return AVERROR(EINVAL);
        }
    } else {
        return AVERROR(EINVAL);
    }

    return 0;
}

int spatialaudio_context_configure_output(struct SpatialaudioContext *spctx, const AVChannelLayout *out_layout)
{
    spctx->outChCount = out_layout->nb_channels;
    if (spctx->outChCount == 1 ||
        !spctx->speakerDecoder.Configure(spctx->ambisonicOrder, true, AMB_BLOCK_TIME_LEN, kAmblib_CustomSpeakerSetUp, spctx->outChCount))
    {
        av_log(spctx->logctx, AV_LOG_ERROR, "Failure creating the ambisonics decoder\n");
        return AVERROR(EINVAL);
    }

    for (size_t idx = 0; idx < spctx->outChCount; idx++) {
        char name[32];
        enum AVChannel channel = av_channel_layout_channel_from_index(out_layout, idx);
        av_channel_name(name, sizeof(name), channel);

        PolarPoint point = {};
        auto it = speakerPositions.find(channel);
        if (it != speakerPositions.end()) {
            point = it->second;
            av_log(spctx->logctx, AV_LOG_VERBOSE, "Setting point for channel '%s':\t Azi.: %.2f, Elev.: %.2f, Dist.: %.2f\n",
                name, point.fAzimuth, point.fElevation, point.fDistance);
        } else {
            av_log(spctx->logctx, AV_LOG_WARNING, "No position information for channel '%s', assuming Azi.: %.2f, Elev.: %.2f, Dist.: %.2f\n",
                name, point.fAzimuth, point.fElevation, point.fDistance);
        }

        spctx->speakerDecoder.SetPosition(idx, point);
    }

    if (!spctx->processor.Configure(spctx->ambisonicOrder, true, AMB_BLOCK_TIME_LEN, 0))
    {
        av_log(spctx->logctx, AV_LOG_ERROR, "Failure creating the ambisonics processor\n");
        return AVERROR_EXTERNAL;
    }

    if (!spctx->zoomer.Configure(spctx->ambisonicOrder, true, AMB_BLOCK_TIME_LEN, 0))
    {
        av_log(spctx->logctx, AV_LOG_ERROR, "Failure creating the ambisonics zoomer\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

// Mix in the non-diegetic (headlocked) channels into the output
static void add_nondiegetic_channels(struct SpatialaudioContext *spctx, AVFrame *in, AVFrame *out)
{
    const AVChannelLayout *out_layout = &out->ch_layout;

    // The non-diegetic channels are at the end
    unsigned nondiegeticChStart = spctx->inChCount - spctx->nondiegeticChCount;

    for (size_t idx = 0; idx < spctx->outChCount; idx++) {
        enum AVChannel channel = av_channel_layout_channel_from_index(out_layout, idx);
        
        auto it = std::find(stereoChannels.cbegin(), stereoChannels.cend(), channel);
        if (it == stereoChannels.cend())
            continue;

        ptrdiff_t pos = std::distance(stereoChannels.cbegin(), it);

        // This is either 0 for left or 1 for right
        unsigned lr = !((pos + 1) % 2);

        float *in_plane = ((float **)in->extended_data)[nondiegeticChStart + lr];
        float *out_plane = ((float **)out->extended_data)[idx];

        for (size_t i = 0; i < out->nb_samples; i++)
            out_plane[i] = out_plane[i] / 2.f + in_plane[i] / 2.f;
    }
}

int spatialaudio_context_process(struct SpatialaudioContext *spctx, AVFrame *in, AVFrame *out)
{
    CBFormat inData;
    inData.Configure(spctx->ambisonicOrder, true, in->nb_samples);

    av_assert2(in->ch_layout.nb_channels >= spctx->inChCount - spctx->nondiegeticChCount);
    for (unsigned i = 0; i < spctx->inChCount - spctx->nondiegeticChCount; ++i)
        inData.InsertStream((float *)in->extended_data[i], i, in->nb_samples);

    Orientation ori(spctx->viewpoint.theta, spctx->viewpoint.phi, spctx->viewpoint.roll);
    spctx->processor.SetOrientation(ori);
    spctx->processor.Refresh();
    spctx->processor.Process(&inData, inData.GetSampleCount());

    spctx->zoomer.SetZoom(spctx->viewpoint.zoom);
    spctx->zoomer.Refresh();
    spctx->zoomer.Process(&inData, inData.GetSampleCount());

    spctx->speakerDecoder.Process(&inData, out->nb_samples, (float **)out->extended_data);

    if (spctx->nondiegeticChCount > 0)
        add_nondiegetic_channels(spctx, in, out);

    return 0;
}

void spatialaudio_context_destroy(struct SpatialaudioContext **spctx)
{
    if (*spctx)
        (*spctx)->~SpatialaudioContext();
    av_freep(spctx);
}
