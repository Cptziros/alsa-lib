/*
 *  PCM Stream Plug-In Interface
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
#include "../pcm_local.h"

/*
 *  Basic stream plugin
 */
 
typedef struct stream_private_data {
	snd_pcm_t *slave;
} stream_t;

static ssize_t stream_transfer(snd_pcm_plugin_t *plugin,
			       const snd_pcm_plugin_voice_t *src_voices,
			       snd_pcm_plugin_voice_t *dst_voices,
			       size_t samples)
{
	stream_t *data;
	ssize_t result;
	struct iovec *vec;
	int count, voice;

	if (plugin == NULL)
		return -EINVAL;
	data = (stream_t *)plugin->extra_data;
	if (data == NULL)
		return -EINVAL;
	vec = (struct iovec *)((char *)data + sizeof(*data));
	if (plugin->channel == SND_PCM_CHANNEL_PLAYBACK) {
		if (src_voices == NULL)
			return -EINVAL;
		if ((result = snd_pcm_plugin_src_samples_to_size(plugin, samples)) < 0)
			return result;
		count = plugin->src_format.voices;
		if (plugin->src_format.interleave) {
			result = snd_pcm_write(data->slave, src_voices->area.addr, result);
		} else {
			result /= count;
			for (voice = 0; voice < count; voice++) {
				if (src_voices[voice].enabled)
					vec[voice].iov_base = src_voices[voice].area.addr;
				else
					vec[voice].iov_base = 0;
				vec[voice].iov_len = result;
			}
			result = snd_pcm_writev(data->slave, vec, count);
		}
		if (result < 0)
			return result;
		return snd_pcm_plugin_src_size_to_samples(plugin, result);
	} else if (plugin->channel == SND_PCM_CHANNEL_CAPTURE) {
		if (dst_voices == NULL)
			return -EINVAL;
		if ((result = snd_pcm_plugin_dst_samples_to_size(plugin, samples)) < 0)
			return result;
		count = plugin->dst_format.voices;
		if (plugin->dst_format.interleave) {
			result = snd_pcm_read(data->slave, dst_voices->area.addr, result);
			for (voice = 0; voice < count; voice++)
				dst_voices[voice].enabled = src_voices[voice].enabled;
		} else {
			result /= count;
			for (voice = 0; voice < count; voice++) {
				dst_voices[voice].enabled = src_voices[voice].enabled;
				if (dst_voices[voice].enabled)
					vec[voice].iov_base = dst_voices[voice].area.addr;
				else
					vec[voice].iov_base = 0;
				vec[voice].iov_len = result;
			}
			result = snd_pcm_readv(data->slave, vec, count);
		}
		if (result < 0)
			return result;
		return snd_pcm_plugin_dst_size_to_samples(plugin, result);
	} else {
		return -EINVAL;
	}
}
 
static int stream_src_voices(snd_pcm_plugin_t *plugin,
			     size_t samples,
			     snd_pcm_plugin_voice_t **voices)
{
	int err;
	unsigned int voice;
	snd_pcm_plugin_voice_t *v;
	err = snd_pcm_plugin_client_voices(plugin, samples, &v);
	if (err < 0)
		return err;
	*voices = v;
	for (voice = 0; voice < plugin->src_format.voices; ++voice, ++v)
		v->wanted = 1;
	return 0;
}

int snd_pcm_plugin_build_stream(snd_pcm_plugin_handle_t *pcm,
				int channel,
				snd_pcm_t *slave,
				snd_pcm_format_t *format,
				snd_pcm_plugin_t **r_plugin)
{
	int err;
	stream_t *data;
	snd_pcm_plugin_t *plugin;

	if (!r_plugin)
		return -EINVAL;
	*r_plugin = NULL;
	if (!pcm || channel < 0 || channel > 1)
		return -EINVAL;
	err = snd_pcm_plugin_build(pcm, channel,
				   "I/O stream",
				   format, format,
				   sizeof(stream_t) + sizeof(struct iovec) * format->voices,
				   &plugin);
	if (err < 0)
		return err;
	data = (stream_t *)plugin->extra_data;
	data->slave = slave;
	plugin->transfer = stream_transfer;
	if (format->interleave && channel == SND_PCM_CHANNEL_PLAYBACK)
		plugin->client_voices = stream_src_voices;
	*r_plugin = plugin;
	return 0;
}
