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
    const AVClass *class;
    struct SpatialaudioContext *spctx;

    // The output channel layout to mix to
    AVChannelLayout output_layout;

    float fov;
    float roll;
    float pitch;
    float yaw;
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    struct SpatialaudioContextC *s = ctx->priv;
    AVFrame *out;
    int ret;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    ret = spatialaudio_context_process(s->spctx, in, out);

    av_frame_free(&in);
    return (ret < 0) ? ret : ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    struct SpatialaudioContextC *s = ctx->priv;
    spatialaudio_context_destroy(&s->spctx);
}

static av_cold int init(AVFilterContext *ctx)
{
    struct SpatialaudioContextC *s = ctx->priv;
    int ret = spatialaudio_context_create(&s->spctx, ctx);
    if (ret < 0)
        return ret;

    spatialaudio_context_set_viewpoint(s->spctx, s->yaw, s->pitch, s->roll, s->fov);
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    int ret;
    AVFilterContext *ctx = inlink->dst;
    struct SpatialaudioContextC *s = ctx->priv;
    FilterLink *ff_inlink = ff_filter_link(inlink);

    ret = spatialaudio_context_configure_input(s->spctx, &inlink->ch_layout);
    if (ret < 0)
        return ret;

    ff_inlink->min_samples = ff_inlink->max_samples = AMB_BLOCK_TIME_LEN;
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    struct SpatialaudioContextC *s = ctx->priv;

    ret = spatialaudio_context_configure_output(s->spctx, &outlink->ch_layout);
    if (ret < 0)
        return ret;

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    int ret;
    struct SpatialaudioContextC *s = ctx->priv;
    AVFilterChannelLayouts *channel_layouts = NULL;

    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };

    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, sample_fmts);
    if (ret < 0)
        return ret;

    ret = ff_add_channel_layout(&channel_layouts, &s->output_layout);
    if (ret < 0)
        return ret;

    ret = ff_channel_layouts_ref(channel_layouts, &cfg_out[0]->channel_layouts);
    if (ret < 0)
        return ret;

    return 0;
}

#define OFFSET(x) offsetof(struct SpatialaudioContextC, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption libspatialaudio_options[] = {
    { "channel_layout", "Output channel layout", OFFSET(output_layout),
        AV_OPT_TYPE_CHLAYOUT, { .str = "stereo" }, .flags = FLAGS },

    { "fov", "FoV in the soundsphere", OFFSET(fov),
        AV_OPT_TYPE_FLOAT, { .dbl = FOV_DEGREES_DEFAULT }, FOV_DEGREES_MIN, FOV_DEGREES_MAX, .flags = FLAGS },

    { "roll", "Roll to apply to the position in the soundsphere",OFFSET(roll),
        AV_OPT_TYPE_FLOAT, { .dbl = 0.f }, -180.f, 180.f, .flags = FLAGS },

    { "pitch","Pitch to apply to the position in the soundsphere",OFFSET(pitch),
        AV_OPT_TYPE_FLOAT, { .dbl = 0.f }, -180.f, 180.f, .flags = FLAGS },

    { "yaw",  "Yaw to apply to the position in the soundsphere",  OFFSET(yaw),
        AV_OPT_TYPE_FLOAT, { .dbl = 0.f }, -180.f, 180.f, .flags = FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(libspatialaudio);

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_libspatialaudio = {
    .name          = "libspatialaudio",
    .description   = NULL_IF_CONFIG_SMALL("Spatial audio rendering using libspatialaudio"),
    .priv_size     = sizeof(struct SpatialaudioContextC),
    .priv_class    = &libspatialaudio_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
