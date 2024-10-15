/*
 * Copyright (C) 2024 Marvin Scholz
 *
 * Authors: Marvin Scholz <epirat07@gmail.com>
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

#include "libavutil/mem.h"
#include "libavutil/tx.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "audio.h"

#include "libavfilter/af_libspatialaudio_common.h"

struct SpatialaudioContextC {
    struct SpatialaudioContext *spctx;
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    return FFERROR_NOT_READY;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, 512, 512, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
}

static av_cold int init(AVFilterContext *ctx)
{
    struct SpatialaudioContextC *s = ctx->priv;

    AVChannelLayout *channel_layout = &ctx->inputs[0]->ch_layout;
    int order = av_channel_layout_ambisonic_order(channel_layout);
    if (order < 0 || order > 3) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported ambisonic order %i\n", order);
        return AVERROR(ENOTSUP);
    }

    s->spctx = init_spatialaudio_context(ctx);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    return 0;
}

#define OFFSET(x) offsetof(SOFAlizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption libspatialaudio_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(libspatialaudio);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_libspatialaudio = {
    .name          = "libspatialaudio",
    .description   = NULL_IF_CONFIG_SMALL("libspatialaudio spatial audio rendering."),
    .priv_size     = sizeof(struct SpatialaudioContextC),
    .priv_class    = &libspatialaudio_class,
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
};
