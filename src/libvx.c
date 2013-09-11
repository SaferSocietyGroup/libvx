#include <stdlib.h>
#include <stdbool.h>
#include "libvx.h"

#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>

static bool initialized = false; 

struct vx_video {
	AVFormatContext* fmt_ctx;
	int stream;
	AVCodecContext* codec_ctx;
	AVCodec* codec;
	
	AVPicture pict;
	AVPacket packet;
};

vx_error_t vx_open(vx_video_t** video, const char* filename)
{
	if(!initialized){
		initialized = true;
		av_register_all();
	}

	vx_video_t* me = calloc(1, sizeof(vx_video_t));
	if(!me)
		return VX_ERR_ALLOCATE;
	
	vx_error_t error = VX_ERR_UNKNOWN;

	// open stream
	if(avformat_open_input(&me->fmt_ctx, filename, NULL, NULL) != 0){
		error = VX_ERR_OPEN_FILE;
		goto cleanup;
	}

	// Get stream information
	if(avformat_find_stream_info(me->fmt_ctx, NULL) < 0){
		error = VX_ERR_STREAM_INFO;
		goto cleanup;
	}
	
	// Print video format information
	// av_dump_format(me->fmt_ctx, 0, filename, 0);

	// Find the first video stream
	me->stream = -1;

	for(unsigned int i = 0; i < me->fmt_ctx->nb_streams; i++){
		if(me->fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			me->stream = i;
			break;
		}
	}

	if(me->stream == -1){
		error = VX_ERR_VIDEO_STREAM;
		goto cleanup;
	}

	// Get a pointer to the codec context for the video stream
	me->codec_ctx = me->fmt_ctx->streams[me->stream]->codec;

	// Find the decoder for the video stream
	me->codec = avcodec_find_decoder(me->codec_ctx->codec_id);
		
	if(!me->codec){
		error = VX_ERR_FIND_CODEC;
		goto cleanup;
	}

	// Open codec
	if(avcodec_open2(me->codec_ctx, me->codec, NULL) < 0){
		error = VX_ERR_OPEN_CODEC;
		goto cleanup;
	}

	*video = me;
	return VX_ERR_SUCCESS;

cleanup:
	return error;
}

void vx_close(vx_video_t* me)
{
}

int vx_get_width(vx_video_t* me)
{
	return me->codec_ctx->width;
}

int vx_get_height(vx_video_t* me)
{
	return me->codec_ctx->height;
}

vx_error_t vx_get_frame(vx_video_t* me, int width, int height, vx_pix_fmt_t pix_fmt, void* buffer)
{
	AVPacket packet;
	memset(&packet, 0, sizeof(AVPacket));
	int frame_finished = 0;
	AVFrame* frame = avcodec_alloc_frame();

	vx_error_t ret = VX_ERR_UNKNOWN;

	for(int i = 0; i < 1024; i++){
		// Read frames until we get a video frame
		do {
			av_free_packet(&packet);
			memset(&packet, 0, sizeof(AVPacket));

			if(av_read_frame(me->fmt_ctx, &packet) < 0){
				av_free_packet(&packet);
				memset(&packet, 0, sizeof(AVPacket));
				return VX_ERR_EOF;
			}
		} while (packet.stream_index != me->stream);

		int bytes_remaining = packet.size;
		int bytes_decoded = 0;

		// Decode until all bytes in the packet are decoded
		while(bytes_remaining > 0 && !frame_finished){
			if( (bytes_decoded = avcodec_decode_video2(me->codec_ctx, frame, &frame_finished, &packet)) < 0 ){
				ret = VX_ERR_DECODE_VIDEO;
				goto cleanup;
			}

			bytes_remaining -= bytes_decoded;
		}

		if(frame_finished)
			break;
	}

	av_free_packet(&packet);
	
	AVPicture pict;
	int av_pixfmt = pix_fmt == VX_PIX_FMT_GRAY8 ? PIX_FMT_GRAY8 : PIX_FMT_RGB24;
	if(avpicture_fill(&pict, buffer, av_pixfmt, width, height) < 0){
		ret = VX_ERR_SCALING;
		goto cleanup;
	}

	struct SwsContext* sws_ctx = sws_getContext(me->codec_ctx->width, me->codec_ctx->height, 
		me->codec_ctx->pix_fmt, width, height, av_pixfmt, SWS_BILINEAR, NULL, NULL, NULL);

	if(!sws_ctx){
		ret = VX_ERR_SCALING;
		goto cleanup;
	}

	sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, me->codec_ctx->height, pict.data, pict.linesize); 
	sws_freeContext(sws_ctx);
	
	//av_free(frame->data[0]);
	av_free(frame);

	return VX_ERR_SUCCESS;

cleanup:
	return ret;
}

vx_error_t vx_get_frame_rate(vx_video_t* me, float* out_fps)
{
	AVRational rate = me->fmt_ctx->streams[me->stream]->avg_frame_rate;

	if(rate.num == 0 && rate.den == 0)
		return VX_ERR_FRAME_RATE;  

	*out_fps = (float)av_q2d(rate);
	return VX_ERR_SUCCESS;
}

vx_error_t vx_get_duration(vx_video_t* me, float* out_duration)
{
	*out_duration = (float)me->fmt_ctx->duration / (float)AV_TIME_BASE;
	return VX_ERR_SUCCESS;
}

//if(pCodecCtx->sample_aspect_ratio.den != 0 && pCodecCtx->sample_aspect_ratio.num != 0){
//		pixelAspect = (float)pCodecCtx->sample_aspect_ratio.num / pCodecCtx->sample_aspect_ratio.den; 
///	}else{
//		pixelAspect = 1;
//	}

//	aspectRatio = ((float)w * pixelAspect) / h;
