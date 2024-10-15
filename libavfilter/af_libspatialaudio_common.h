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
#include "avfilter.h"

#ifdef __cplusplus
#define SPATIALEXTERNC extern "C"
#else
#define SPATIALEXTERNC
#endif

#define AMB_BLOCK_TIME_LEN 1024
#define AMB_MAX_ORDER 3

#define FOV_DEGREES_MIN       20.f
#define FOV_DEGREES_MAX       150.f
#define FOV_DEGREES_DEFAULT   80.f

enum SpatialaudioMode
{
    AMBISONICS_DECODER, // Ambisonics decoding
    AMBISONICS_BINAURAL_DECODER, // Ambisonics decoding to binaural
};

struct SpatialaudioContext;

SPATIALEXTERNC int spatialaudio_context_create(struct SpatialaudioContext **spctx, void *logctx);

SPATIALEXTERNC void spatialaudio_context_set_mode(struct SpatialaudioContext *spctx, enum SpatialaudioMode mode);
SPATIALEXTERNC void spatialaudio_context_set_viewpoint(struct SpatialaudioContext *spctx, float yaw, float pitch, float roll, float fov);

SPATIALEXTERNC int spatialaudio_context_configure_input(struct SpatialaudioContext *spctx, const AVChannelLayout *out_layout);
SPATIALEXTERNC int spatialaudio_context_configure_output(struct SpatialaudioContext *spctx, const AVChannelLayout *out_layout);

SPATIALEXTERNC int spatialaudio_context_process(struct SpatialaudioContext *spctx, AVFrame *in, AVFrame *out);

SPATIALEXTERNC void spatialaudio_context_destroy(struct SpatialaudioContext **spctx);
