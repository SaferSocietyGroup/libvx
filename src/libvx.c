#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>

#include "libvx.h"

static bool initialized = false; 

struct vx_frame_info {
	vx_frame_flag flags;
	long long pos;
	long long dts;
	long long pts;
};

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
		avcodec_register_all();
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
	
	// Find the best video stream
	AVCodec* codec;
	me->stream = av_find_best_stream(me->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);

	if(me->stream < 0){
		if(me->stream == AVERROR_STREAM_NOT_FOUND)
			error = VX_ERR_VIDEO_STREAM;

		if(me->stream == AVERROR_DECODER_NOT_FOUND)
			error = VX_ERR_FIND_CODEC;

		goto cleanup;
	}

	// Get a pointer to the codec context for the video stream
	me->codec_ctx = me->fmt_ctx->streams[me->stream]->codec;
	
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

	// already freed by avformat_free_context?
	//if(me->codec_ctx && avcodec_is_open(me->codec_ctx))
	//	avcodec_close(me->codec_ctx);
	
	free(me);
}

static vx_error count_frames(vx_video* me, int* out_num_frames)
{
	assert(me);
	int num_frames = 0;

	AVPacket packet;

	while(true){
		memset(&packet, 0, sizeof(packet));

		if(av_read_frame(me->fmt_ctx, &packet) < 0){
			break;
		}

		if(packet.stream_index == me->stream){
			num_frames++;
		}

		av_free_packet(&packet);
	}

	if(packet.data)
		av_free_packet(&packet);

	*out_num_frames = num_frames;

	return VX_ERR_SUCCESS;
}


vx_error vx_count_frames_in_file(const char* filename, int* out_num_frames)
{
	vx_video* video = NULL;
	vx_error ret;

	ret = vx_open(&video, filename);
	if(ret != VX_ERR_SUCCESS)
		return ret;

	ret = count_frames(video, out_num_frames);

	vx_close(video);

	return ret;
}

int vx_get_width(vx_video* me)
{
	return me->codec_ctx->width;
}

int vx_get_height(vx_video* me)
{
	return me->codec_ctx->height;
}

long long vx_get_file_position(vx_video* video)
{
	return video->fmt_ctx->pb->pos; 
}

long long vx_get_file_size(vx_video* video)
{
	return avio_size(video->fmt_ctx->pb);
}

vx_error vx_get_frame(vx_video* me, int width, int height, vx_pix_fmt pix_fmt, void* out_buffer, vx_frame_info* fi)
{
	AVPacket packet;
	memset(&packet, 0, sizeof(packet));

	int frame_finished = 0;
	AVFrame* frame = avcodec_alloc_frame();

	vx_error ret = VX_ERR_UNKNOWN;
	int64_t frame_pos = -1;
	int64_t file_pos = avio_tell(me->fmt_ctx->pb);

	for(int i = 0; i < 1024; i++){
		if(av_read_frame(me->fmt_ctx, &packet) < 0){
			ret = VX_ERR_EOF;
			goto cleanup;
		}
		
		if(packet.stream_index == me->stream){
			int bytes_remaining = packet.size;
			int bytes_decoded = 0;

			if(frame_pos < 0 && packet.pos != -1)
				frame_pos = packet.pos;

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

	if(fi){
		fi->flags = frame->pict_type == AV_PICTURE_TYPE_I ? VX_FF_KEYFRAME : 0;
		fi->flags |= frame_pos < 0 ? VX_FF_BYTE_POS_GUESSED : 0;
		fi->flags |= frame->pts > 0 ? VX_FF_HAS_PTS : 0; 

		fi->pos = frame_pos >= 0 ? frame_pos : file_pos;	
		fi->pts = frame->pts;
		fi->dts = frame->pkt_dts;
	}
	
	AVPicture pict;
	int av_pixfmt = pix_fmt == VX_PIX_FMT_GRAY8 ? PIX_FMT_GRAY8 : PIX_FMT_RGB24;
	if(avpicture_fill(&pict, out_buffer, av_pixfmt, width, height) < 0){
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

double vx_timestamp_to_seconds(vx_video* me, long long ts)
{
	return (double)ts * av_q2d(me->fmt_ctx->streams[me->stream]->time_base);
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
	av_free(buffer);
}

vx_frame_info* vx_fi_create()
{
	return calloc(1, sizeof(vx_frame_info));
}

void vx_fi_destroy(vx_frame_info* fi)
{
	free(fi);
}

unsigned int vx_fi_get_flags(vx_frame_info* fi)
{
	return fi->flags;
}

long long vx_fi_get_byte_pos(vx_frame_info* fi)
{
	return fi->pos;
}

long long vx_fi_get_dts(vx_frame_info* fi)
{
	return fi->dts;
}

long long vx_fi_get_pts(vx_frame_info* fi)
{
	return fi->pts;
}
