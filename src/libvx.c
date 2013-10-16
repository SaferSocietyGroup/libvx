#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>

#include "libvx.h"

static bool initialized = false; 

struct vx_video {
	AVFormatContext* fmt_ctx;
	AVCodecContext* codec_ctx;
	int stream;
};

vx_error vx_open(vx_video** video, const char* filename)
{
	if(!initialized){
		initialized = true;
		av_register_all();
	}

	vx_video* me = calloc(1, sizeof(vx_video));
	if(!me)
		return VX_ERR_ALLOCATE;
	
	vx_error error = VX_ERR_UNKNOWN;

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
	AVCodec* codec = avcodec_find_decoder(me->codec_ctx->codec_id);
		
	if(!codec){
		error = VX_ERR_FIND_CODEC;
		goto cleanup;
	}

	// Open codec
	if(avcodec_open2(me->codec_ctx, codec, NULL) < 0){
		error = VX_ERR_OPEN_CODEC;
		goto cleanup;
	}

	*video = me;
	return VX_ERR_SUCCESS;

cleanup:
	vx_close(me);
	return error;
}

void vx_close(vx_video* me)
{
	assert(me);
	
	if(me->fmt_ctx)
		avformat_free_context(me->fmt_ctx);

	if(me->codec_ctx)
		avcodec_close(me->codec_ctx);
	
	free(me);
}

int vx_get_width(vx_video* me)
{
	return me->codec_ctx->width;
}

int vx_get_height(vx_video* me)
{
	return me->codec_ctx->height;
}

vx_error vx_get_frame(vx_video* me, int width, int height, vx_pix_fmt pix_fmt, void* buffer)
{
	AVPacket packet;
	memset(&packet, 0, sizeof(packet));

	int frame_finished = 0;
	AVFrame* frame = avcodec_alloc_frame();

	vx_error ret = VX_ERR_UNKNOWN;

	for(int i = 0; i < 1024; i++){
		if(av_read_frame(me->fmt_ctx, &packet) < 0){
			ret = VX_ERR_EOF;
			goto cleanup;
		}
		
		if(packet.stream_index == me->stream){
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
		}

		av_free_packet(&packet);

		if(frame_finished)
			break;
	}
	
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
	if(frame)
		av_free(frame);

	if(packet.data)
		av_free_packet(&packet);

	return ret;
}

vx_error vx_get_frame_rate(vx_video* me, float* out_fps)
{
	AVRational rate = me->fmt_ctx->streams[me->stream]->avg_frame_rate;

	if(rate.num == 0 || rate.den == 0)
		return VX_ERR_FRAME_RATE;  

	*out_fps = (float)av_q2d(rate);
	return VX_ERR_SUCCESS;
}

vx_error vx_get_duration(vx_video* me, float* out_duration)
{
	*out_duration = (float)me->fmt_ctx->duration / (float)AV_TIME_BASE;
	return VX_ERR_SUCCESS;
}

const char* vx_get_error_str(vx_error error)
{
	if(error > VX_ERR_SCALING)
		error = VX_ERR_UNKNOWN;

	const char* err_str[] = {
		"operation successful",                  //VX_ERR_SUCCESS         = 0,
		"unknown error",                         //VX_ERR_UNKNOWN         = 1,
		"memory allocation failed",              //VX_ERR_ALLOCATE        = 2,
		"could not determine frame rate",        //VX_ERR_FRAME_RATE      = 3,
		"could not open file",                   //VX_ERR_OPEN_FILE       = 4,
		"could not retreive stream information", //VX_ERR_STREAM_INFO     = 5,
		"could not open video stream",           //VX_ERR_VIDEO_STREAM    = 6,
		"could not find codec",                  //VX_ERR_FIND_CODEC      = 7,
		"could not open codec",                  //VX_ERR_OPEN_CODEC      = 8,
		"end of file",                           //VX_ERR_EOF             = 9,
		"error while decodin",                   //VX_ERR_DECODE_VIDEO    = 10,
		"error while scaling",                   //VX_ERR_SCALING         = 11,
	};

	return err_str[error];
}

vx_error vx_get_pixel_aspect_ratio(vx_video* me, float* out_par)
{
	AVRational par = me->codec_ctx->sample_aspect_ratio;
	if(par.num == 0 && par.den == 1)
		return VX_ERR_PIXEL_ASPECT;

	*out_par = (float)av_q2d(par);
	return VX_ERR_SUCCESS;
}

void* vx_alloc_frame_buffer(int width, int height, vx_pix_fmt pix_fmt)
{
	int av_pixfmt = pix_fmt == VX_PIX_FMT_GRAY8 ? PIX_FMT_GRAY8 : PIX_FMT_RGB24;
	return av_malloc(avpicture_get_size(av_pixfmt, width, height));
}

void vx_free_frame_buffer(void* buffer)
{
	free(buffer);
}
