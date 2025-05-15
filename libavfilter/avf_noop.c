/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * No-op video filter
 */

#include <stdbool.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"

typedef struct NoopContext {
    const AVClass *class;
    unsigned nb_video; // Number of video inputs
    unsigned nb_audio; // Number of audio inputs
    unsigned *in_status; // Array of inputs status
} NoopContext;

static char get_media_type_char(enum AVMediaType media_type)
{
    switch (media_type) {
    case AVMEDIA_TYPE_VIDEO:      return 'v';
    case AVMEDIA_TYPE_AUDIO:      return 'a';
    default:                      return 'u';
    }
}

static int create_pads(AVFilterContext *ctx, int count, AVFilterPad pad, bool inpad)
{
    av_assert0(pad.name == NULL);

    for (int i = 0; i < count; i++) {
        pad.name = av_asprintf("%s:%c%d",
            (inpad) ? "in" : "out",
            get_media_type_char(pad.type), i);
        if (pad.name == NULL) {
            return AVERROR(ENOMEM);
        }

        int ret = ((inpad) ? ff_append_inpad_free_name
                           : ff_append_outpad_free_name)(ctx, &pad);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int copy_link_props(AVFilterLink *dst, AVFilterLink *src)
{
    FilterLink *src_fl = ff_filter_link(src);
    FilterLink *dst_fl = ff_filter_link(dst);

    av_assert1(src->type == AVMEDIA_TYPE_VIDEO || src->type == AVMEDIA_TYPE_AUDIO);
    av_assert1(src->type == dst->type);

    if (src->type != dst->type)
        return AVERROR_BUG;

    switch (src->type) {
    case AVMEDIA_TYPE_VIDEO:
        dst->h                      = src->h;
        dst->w                      = src->w;
        dst->sample_aspect_ratio    = src->sample_aspect_ratio;
        dst->colorspace             = src->colorspace;
        dst->color_range            = src->color_range;

        dst_fl->frame_rate          = src_fl->frame_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        dst->sample_rate = src->sample_rate;

        int ret = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
        if (ret)
            return ret;
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    dst->time_base = src->time_base;
    return 0;
}

static int config_output_props(AVFilterLink *outlink)
{
    unsigned out_idx = FF_OUTLINK_IDX(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[out_idx];

    return copy_link_props(outlink, inlink);
}

static av_cold int init(AVFilterContext *ctx)
{
    int ret;
    NoopContext *noop_ctx = ctx->priv;

    // Create input pads

    ret = create_pads(ctx, noop_ctx->nb_video, (AVFilterPad){
        .type = AVMEDIA_TYPE_VIDEO,
    }, true);
    if (ret < 0)
        return ret;

    ret = create_pads(ctx, noop_ctx->nb_audio, (AVFilterPad){
        .type = AVMEDIA_TYPE_AUDIO,
    }, true);
    if (ret < 0)
        return ret;

    // Create output pads

    ret = create_pads(ctx, noop_ctx->nb_video, (AVFilterPad){
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_output_props,
    }, false);
    if (ret < 0)
        return ret;

    ret = create_pads(ctx, noop_ctx->nb_audio, (AVFilterPad){
        .type           = AVMEDIA_TYPE_AUDIO,
        .config_props   = config_output_props,
    }, false);
    if (ret < 0)
        return ret;

    noop_ctx->in_status = av_calloc(noop_ctx->nb_video + noop_ctx->nb_audio, sizeof(unsigned));
    if (noop_ctx->in_status == NULL)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NoopContext *noop_ctx = ctx->priv;

    av_freep(&noop_ctx->in_status);
}

static int activate(AVFilterContext *ctx)
{
    NoopContext *noop_ctx = ctx->priv;

    // Forward outlinks status back to inlinks
    for (int i = 0; i < ctx->nb_outputs; i++) {
        int ret = ff_outlink_get_status(ctx->outputs[i]);
        if (ret && noop_ctx->in_status[i] != ret) {
            noop_ctx->in_status[i] = ret;
            ff_inlink_set_status(ctx->inputs[i], ret);
            return 0;
        }
    }

    // Process available frames
    for (int i = 0; i < ctx->nb_outputs; i++) {
        if (!ff_outlink_frame_wanted(ctx->outputs[i]))
            continue;

        // The output wants a frame, check if we have one
        if (!ff_inlink_check_available_frame(ctx->inputs[i]))
            continue;

        // We should have a frame
        AVFrame *frame = NULL;
        int ret = ff_inlink_consume_frame(ctx->inputs[i], &frame);
        av_assert1(ret);

        if (ret < 0)
            return ret;

        // We are a noop filter, so just forward the frame
        if (ret)
            return ff_filter_frame(ctx->outputs[i], frame);
    }

    // Forward inlinks status to outlinks
    for (int i = 0; i < ctx->nb_outputs; i++) {
        // Skip if we already got EOF or error for that input
        if (noop_ctx->in_status[i] != 0)
            continue;

        FF_FILTER_FORWARD_STATUS(ctx->inputs[i], ctx->outputs[i]);
    }

    // Forward outlinks wanted frames back to inlinks
    for (int i = 0; i < ctx->nb_outputs; i++) {
        // We can just do this unconditionally here as we
        // already handled all possible other cases before.
        FF_FILTER_FORWARD_WANTED(ctx->outputs[i], ctx->inputs[i]);
    }

    return FFERROR_NOT_READY;
}

#define OFFSET(x) offsetof(NoopContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM

static const AVOption noop_options[] = {
    { "v", "Number of video streams", OFFSET(nb_video), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, V|F },
    { "a", "Number of audio streams", OFFSET(nb_audio), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(noop);

const FFFilter ff_avf_noop = {
    .p.name        = "noop",
    .p.description = NULL_IF_CONFIG_SMALL("Pass the inputs unchanged to the outputs."),
    .p.priv_class  = &noop_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY |
                     AVFILTER_FLAG_DYNAMIC_INPUTS |
                     AVFILTER_FLAG_DYNAMIC_OUTPUTS,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(NoopContext),
};
