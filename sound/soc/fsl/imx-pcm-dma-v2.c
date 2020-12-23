/*
 * imx-pcm-dma-v2.c -- ALSA Soc Audio Layer
 *
 * Copyright 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is based on code copyrighted by Freescale,
 * Liam Girdwood, Javier Martin and probably others.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>

#include "imx-pcm.h"

static const struct snd_pcm_hardware imx_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.buffer_bytes_max = IMX_SSI_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 65532, /* Limited by SDMA engine */
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static bool imx_dma_filter_fn(struct dma_chan *chan, void *param)
{
	if (!imx_dma_is_general_purpose(chan))
		return false;

	chan->private = param;

	return true;
}

/* this may get called several times by oss emulation */
static int imx_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_data *dma_data;
	struct dma_slave_config config;
	struct dma_chan *chan;
	int err = 0;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME
	 */
	if (!dma_data)
		return 0;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	chan = snd_dmaengine_pcm_get_chan(substream);
	if (!chan)
		return -EINVAL;

	/* fills in addr_width and direction */
	err = snd_hwparams_to_dma_slave_config(substream, params, &config);
	if (err)
		return err;

	snd_dmaengine_pcm_set_config_from_dai_data(substream,
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream),
			&config);

	return dmaengine_slave_config(chan, &config);
}

static int imx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static snd_pcm_uframes_t imx_pcm_pointer(struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_pointer(substream);
}

static int imx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &imx_pcm_hardware);

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* DT boot: filter_data is the DMA name */
	if (rtd->cpu_dai->dev->of_node) {
		struct dma_chan *chan;

		chan = dma_request_slave_channel(rtd->cpu_dai->dev,
						 dma_data->filter_data);
		ret = snd_dmaengine_pcm_open(substream, chan);
	} else {
		ret = snd_dmaengine_pcm_open_request_chan(substream,
							  imx_dma_filter_fn,
							  dma_data->filter_data);
	}
	return ret;
}

static int imx_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops imx_pcm_ops = {
	.open		= imx_pcm_open,
	.close		= snd_dmaengine_pcm_close_release_chan,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= imx_pcm_hw_params,
	.hw_free	= imx_pcm_hw_free,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= imx_pcm_pointer,
	.mmap		= imx_pcm_mmap,
};

static int imx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = imx_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static void imx_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = SNDRV_PCM_STREAM_PLAYBACK; stream < SNDRV_PCM_STREAM_LAST; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
	}
}

static int imx_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = imx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = imx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	/* free preallocated buffers in case of error */
	if (ret)
		imx_pcm_free_dma_buffers(pcm);

	return ret;
}

static struct snd_soc_platform_driver imx_soc_platform = {
	.ops		= &imx_pcm_ops,
	.pcm_new	= imx_pcm_new,
	.pcm_free	= imx_pcm_free_dma_buffers,
};

int imx_pcm_platform_register(struct device *dev)
{
	return devm_snd_soc_register_platform(dev, &imx_soc_platform);
}
EXPORT_SYMBOL_GPL(imx_pcm_platform_register);

MODULE_LICENSE("GPL");
