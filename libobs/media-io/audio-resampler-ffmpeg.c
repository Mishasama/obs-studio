/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "../util/bmem.h"
#include "audio-resampler.h"
#include "audio-io.h"
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include "lag_lead_filter.h"

// #define DEBUG_COMPENSATION

struct audio_resampler {
	struct SwrContext *context;
	bool opened;

	uint32_t input_freq;
	uint64_t input_layout;
	enum AVSampleFormat input_format;

	uint8_t *output_buffer[MAX_AV_PLANES];
	uint64_t output_layout;
	enum AVSampleFormat output_format;
	int output_size;
	uint32_t output_ch;
	uint32_t output_freq;
	uint32_t output_planes;

	struct lag_lead_filter compensation_filter;
	bool compensation_filter_configured;

	uint64_t total_input_samples;
	uint64_t total_output_samples;
};

static inline enum AVSampleFormat convert_audio_format(enum audio_format format)
{
	switch (format) {
	case AUDIO_FORMAT_UNKNOWN:
		return AV_SAMPLE_FMT_S16;
	case AUDIO_FORMAT_U8BIT:
		return AV_SAMPLE_FMT_U8;
	case AUDIO_FORMAT_16BIT:
		return AV_SAMPLE_FMT_S16;
	case AUDIO_FORMAT_32BIT:
		return AV_SAMPLE_FMT_S32;
	case AUDIO_FORMAT_FLOAT:
		return AV_SAMPLE_FMT_FLT;
	case AUDIO_FORMAT_U8BIT_PLANAR:
		return AV_SAMPLE_FMT_U8P;
	case AUDIO_FORMAT_16BIT_PLANAR:
		return AV_SAMPLE_FMT_S16P;
	case AUDIO_FORMAT_32BIT_PLANAR:
		return AV_SAMPLE_FMT_S32P;
	case AUDIO_FORMAT_FLOAT_PLANAR:
		return AV_SAMPLE_FMT_FLTP;
	}

	/* shouldn't get here */
	return AV_SAMPLE_FMT_S16;
}

static inline uint64_t convert_speaker_layout(enum speaker_layout layout)
{
	switch (layout) {
	case SPEAKERS_UNKNOWN:
		return 0;
	case SPEAKERS_MONO:
		return AV_CH_LAYOUT_MONO;
	case SPEAKERS_STEREO:
		return AV_CH_LAYOUT_STEREO;
	case SPEAKERS_2POINT1:
		return AV_CH_LAYOUT_SURROUND;
	case SPEAKERS_3POINT0:
		return AV_CH_LAYOUT_SURROUND;
	case SPEAKERS_4POINT0:
		return AV_CH_LAYOUT_4POINT0;
	case SPEAKERS_QUAD:
		return AV_CH_LAYOUT_QUAD;
	case SPEAKERS_3POINT1:
		return AV_CH_LAYOUT_3POINT1;
	case SPEAKERS_5POINT0:
		return AV_CH_LAYOUT_5POINT0_BACK;
	case SPEAKERS_4POINT1:
		return AV_CH_LAYOUT_4POINT1;
	case SPEAKERS_5POINT1:
		return AV_CH_LAYOUT_5POINT1_BACK;
	case SPEAKERS_6POINT0:
		return AV_CH_LAYOUT_6POINT0;
	case SPEAKERS_6POINT1:
		return AV_CH_LAYOUT_6POINT1;
	case SPEAKERS_7POINT0:
		return AV_CH_LAYOUT_7POINT0;
	case SPEAKERS_7POINT1:
		return AV_CH_LAYOUT_7POINT1;
	case SPEAKERS_OCTAGONAL:
		return AV_CH_LAYOUT_OCTAGONAL;
	case SPEAKERS_9POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER);
	case SPEAKERS_10POINT0:
		return (AV_CH_LAYOUT_6POINT0_FRONT | AV_CH_BACK_CENTER |
			AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT | AV_CH_TOP_CENTER);
	case SPEAKERS_11POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER |
			AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT);
	case SPEAKERS_12POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER |
			AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
			AV_CH_TOP_FRONT_CENTER);
	case SPEAKERS_13POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER |
			AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
			AV_CH_TOP_FRONT_CENTER | AV_CH_TOP_BACK_CENTER);
	case SPEAKERS_14POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER |
			AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
			AV_CH_TOP_FRONT_CENTER | AV_CH_TOP_BACK_LEFT |
			AV_CH_TOP_BACK_RIGHT);
	case SPEAKERS_15POINT0:
		return (AV_CH_LAYOUT_OCTAGONAL | AV_CH_TOP_CENTER |
			AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
			AV_CH_TOP_FRONT_CENTER | AV_CH_TOP_BACK_CENTER |
			AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT);
	case SPEAKERS_HEXADECAGONAL:
		return AV_CH_LAYOUT_HEXADECAGONAL;
	case SPEAKERS_NHK:
		return AV_CH_LAYOUT_22POINT2;
	}

	/* shouldn't get here */
	return 0;
}

audio_resampler_t *audio_resampler_create(const struct resample_info *dst,
					  const struct resample_info *src)
{
	struct audio_resampler *rs = bzalloc(sizeof(struct audio_resampler));
	int errcode;

	rs->opened = false;
	rs->input_freq = src->samples_per_sec;
	rs->input_layout = convert_speaker_layout(src->speakers);
	rs->input_format = convert_audio_format(src->format);
	rs->output_size = 0;
	rs->output_ch = get_audio_channels(dst->speakers);
	rs->output_freq = dst->samples_per_sec;
	rs->output_layout = convert_speaker_layout(dst->speakers);
	rs->output_format = convert_audio_format(dst->format);
	rs->output_planes = is_audio_planar(dst->format) ? rs->output_ch : 1;

	rs->context = swr_alloc_set_opts(NULL, rs->output_layout,
					 rs->output_format,
					 dst->samples_per_sec, rs->input_layout,
					 rs->input_format, src->samples_per_sec,
					 0, NULL);

	if (!rs->context) {
		blog(LOG_ERROR, "swr_alloc_set_opts failed");
		audio_resampler_destroy(rs);
		return NULL;
	}

	if (rs->input_layout == AV_CH_LAYOUT_MONO && rs->output_ch > 1) {
		const double matrix[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS] = {
			{1},
			{1, 1},
			{1, 1, 0},
			{1, 1, 1, 1},
			{1, 1, 1, 0, 1},
			{1, 1, 1, 1, 1, 1},
			{1, 1, 1, 0, 1, 1, 1},
			{1, 1, 1, 0, 1, 1, 1, 1},
		};
		if (swr_set_matrix(rs->context, matrix[rs->output_ch - 1], 1) <
		    0)
			blog(LOG_DEBUG,
			     "swr_set_matrix failed for mono upmix\n");
	}

	errcode = swr_init(rs->context);
	if (errcode != 0) {
		blog(LOG_ERROR, "avresample_open failed: error code %d",
		     errcode);
		audio_resampler_destroy(rs);
		return NULL;
	}

	return rs;
}

void audio_resampler_destroy(audio_resampler_t *rs)
{
	if (rs) {
		uint64_t total_output_samples =
			rs->total_output_samples +
			swr_get_delay(rs->context, rs->output_freq);
		blog(LOG_INFO,
		     "audio_resampler: input %" PRIu64 " samples, "
		     " output %" PRIu64 " samples, "
		     "estimated input frequency %f Hz",
		     rs->total_input_samples, rs->total_output_samples,
		     (double)rs->total_input_samples / total_output_samples *
			     rs->output_freq);

		if (rs->context)
			swr_free(&rs->context);
		if (rs->output_buffer[0])
			av_freep(&rs->output_buffer[0]);

		bfree(rs);
	}
}

void audio_resampler_set_compensation_error(audio_resampler_t *rs, int error_ns)
{
	if (!rs->compensation_filter_configured) {
		lag_lead_filter_reset(&rs->compensation_filter);
		lag_lead_filter_update(&rs->compensation_filter);
		rs->compensation_filter_configured = true;
	}

	lag_lead_filter_set_error_ns(&rs->compensation_filter, error_ns);
}

void audio_resampler_disable_compensation(audio_resampler_t *rs)
{
	rs->compensation_filter_configured = false;
	swr_set_compensation(rs->context, 0, 0);
}

bool audio_resampler_resample(audio_resampler_t *rs, uint8_t *output[],
			      uint32_t *out_frames, uint64_t *ts_offset,
			      const uint8_t *const input[], uint32_t in_frames)
{
	if (!rs)
		return false;

	struct SwrContext *context = rs->context;
	int ret;

	if (rs->compensation_filter_configured) {
		lag_lead_filter_tick(&rs->compensation_filter, rs->input_freq,
				     in_frames);
		double drift =
			lag_lead_filter_get_drift(&rs->compensation_filter);

		ret = swr_set_compensation(
			rs->context, -(int)(drift * 65536 / rs->input_freq),
			65536);
		if (ret < 0) {
			blog(LOG_ERROR, "swr_set_compensation failed: %d", ret);
			return false;
		}
	}

	int64_t delay = swr_get_delay(context, rs->input_freq);
	int estimated = (int)av_rescale_rnd(delay + (int64_t)in_frames,
					    (int64_t)rs->output_freq,
					    (int64_t)rs->input_freq,
					    AV_ROUND_UP);

	*ts_offset = (uint64_t)swr_get_delay(context, 1000000000);

	/* resize the buffer if bigger */
	if (estimated > rs->output_size) {
		if (rs->output_buffer[0])
			av_freep(&rs->output_buffer[0]);

		av_samples_alloc(rs->output_buffer, NULL, rs->output_ch,
				 estimated, rs->output_format, 0);

		rs->output_size = estimated;
	}

	ret = swr_convert(context, rs->output_buffer, rs->output_size,
			  (const uint8_t **)input, in_frames);

	if (ret < 0) {
		blog(LOG_ERROR, "swr_convert failed: %d", ret);
		return false;
	}

#ifdef DEBUG_COMPENSATION
	if (rs->compensation_filter_configured)
		blog(LOG_INFO,
		     "async-compensation in_frames=%d out_frames=%d error_ns=%" PRIi64
		     " internal-condition=(%f %f)",
		     in_frames, ret, rs->compensation_filter.error_ns,
		     rs->compensation_filter.vc1, rs->compensation_filter.vc2);
#endif

	for (uint32_t i = 0; i < rs->output_planes; i++)
		output[i] = rs->output_buffer[i];

	rs->total_input_samples += in_frames;
	rs->total_output_samples += ret;

	*out_frames = (uint32_t)ret;
	return true;
}
