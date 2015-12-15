#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include "libvx.h"

#ifdef DEBUG
#	define dprintf(...) { printf("%s:%d %-30s ", __FILE__, __LINE__, __func__); printf(__VA_ARGS__); }
#else
# define dprintf(...)
#endif

static bool initialized = false; 

typedef struct
{
	vx_frame_flag flags;
	long long pos;
	long long dts;
	long long pts;
} vx_frame_info;

struct vx_frame
{
	vx_frame_info info;
	int width, height;
	vx_pix_fmt pix_fmt;

	void* buffer;
};

typedef struct vx_frame_queue_item
{
	vx_frame_info info;
	AVFrame* frame;
} vx_frame_queue_item;

#define FRAME_QUEUE_SIZE 16 

struct vx_video
{
	AVFormatContext* fmt_ctx;
	AVCodecContext* video_codec_ctx;
	AVCodecContext* audio_codec_ctx;
	SwrContext* swr_ctx;

	int video_stream;
	int audio_stream;

	int out_sample_rate;
	int out_channels;
	vx_sample_fmt out_sample_format;

	vx_audio_callback audio_cb;
	void* audio_user_data;

	int swr_channels;
	int swr_sample_rate;
	int64_t swr_channel_layout;
	int swr_sample_format;

	uint8_t** audio_buffer;
	int audio_line_size;

	int num_queue;
	vx_frame_queue_item frame_queue[FRAME_QUEUE_SIZE + 1];

	vx_error decoding_error;
};

static enum AVPixelFormat vx_to_av_pix_fmt(vx_pix_fmt fmt)
{
	enum AVPixelFormat formats[] = {AV_PIX_FMT_GRAY8, AV_PIX_FMT_RGB24, AV_PIX_FMT_BGRA};
	return formats[fmt];
}

static int vx_enqueue_qsort_fn(const void* a, const void* b)
{
	return ((vx_frame_queue_item*)b)->info.pts - ((vx_frame_queue_item*)a)->info.pts;
}

static void vx_enqueue(vx_video* me, vx_frame_queue_item item)
{
	me->frame_queue[me->num_queue++] = item;
	qsort(me->frame_queue, me->num_queue, sizeof(vx_frame_queue_item), vx_enqueue_qsort_fn);
}

static vx_frame_queue_item vx_dequeue(vx_video* me)
{
	return me->frame_queue[--me->num_queue];
}

static bool find_stream_and_open_codec(vx_video* me, enum AVMediaType type, int* out_stream, AVCodecContext** out_codec_ctx, vx_error* out_error)
{
	AVCodec* codec;
	*out_stream = av_find_best_stream(me->fmt_ctx, type, -1, -1, &codec, 0);

	if(*out_stream < 0){
		if(*out_stream == AVERROR_STREAM_NOT_FOUND)
			*out_error = VX_ERR_VIDEO_STREAM;

		if(*out_stream == AVERROR_DECODER_NOT_FOUND)
			*out_error = VX_ERR_FIND_CODEC;

		return false;
	}

	// Get a pointer to the codec context for the video stream
	*out_codec_ctx = me->fmt_ctx->streams[*out_stream]->codec;
	
	// Open codec
	if(avcodec_open2(*out_codec_ctx, codec, NULL) < 0){
		*out_error = VX_ERR_OPEN_CODEC;

		return false;
	}

	return true;
}

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
	
	// find video and audio streams and open respective codecs
	if(!find_stream_and_open_codec(me, AVMEDIA_TYPE_VIDEO, &me->video_stream, &me->video_codec_ctx, &error)){
		goto cleanup;
	}
		
	if(!find_stream_and_open_codec(me, AVMEDIA_TYPE_AUDIO, &me->audio_stream, &me->audio_codec_ctx, &error)){
		dprintf("no audio stream\n");
	}
	
	// reference counted frames for video codec so they can be queued without cloning
	me->video_codec_ctx->refcounted_frames = 1;
	
	*video = me;
	return VX_ERR_SUCCESS;

cleanup:
	vx_close(me);
	return error;
}

void vx_close(vx_video* me)
{
	assert(me);

	if(me->swr_ctx)
		swr_free(&me->swr_ctx);

	if(me->fmt_ctx)
		avformat_free_context(me->fmt_ctx);

	// already freed by avformat_free_context?
	//if(me->video_codec_ctx && avcodec_is_open(me->video_codec_ctx))
	//	avcodec_close(me->video_codec_ctx);

	for(int i = 0; i < me->num_queue; i++){
		av_frame_unref(me->frame_queue[i].frame);
		av_frame_free(&me->frame_queue[i].frame);
	}
	
	free(me);
}

static bool vx_read_frame(AVFormatContext* fmt_ctx, AVPacket* packet, int stream)
{
	// try to read a frame, if it can't be read, skip ahead a bit and try again
	int64_t last_fp = avio_tell(fmt_ctx->pb);

	for(int i = 0; i < 1024; i++){
		int ret = av_read_frame(fmt_ctx, packet);

		// success
		if(ret == 0)
			return true;

		// eof, no need to retry
		if(ret == AVERROR_EOF || avio_feof(fmt_ctx->pb))
			return false;

		// other error, might be a damaged stream, seek forward a couple bytes and try again
		if((i % 10) == 0){
			int64_t fp = avio_tell(fmt_ctx->pb);

			if(fp <= last_fp)
				fp = last_fp + 100 * i;

			dprintf("retry: @%" PRId64 "\n", fp);
			avformat_seek_file(fmt_ctx, stream, fp + 100, fp + 512, fp + 1024 * 1024, AVSEEK_FLAG_BYTE | AVSEEK_FLAG_ANY);

			last_fp = fp;
		}
	}

	return false;
}


static vx_error count_frames(vx_video* me, int* out_num_frames)
{
	assert(me);
	int num_frames = 0;

	AVPacket packet;

	while(true){
		memset(&packet, 0, sizeof(packet));

		if(!vx_read_frame(me->fmt_ctx, &packet, me->video_stream)){
			break;
		}

		if(packet.stream_index == me->video_stream){
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
	return me->video_codec_ctx->width;
}

int vx_get_height(vx_video* me)
{
	return me->video_codec_ctx->height;
}

long long vx_get_file_position(vx_video* video)
{
	return video->fmt_ctx->pb->pos; 
}

long long vx_get_file_size(vx_video* video)
{
	return avio_size(video->fmt_ctx->pb);
}

int vx_get_audio_sample_rate(vx_video* me)
{
	if(!me->audio_codec_ctx)
		return 0;
	
	return me->audio_codec_ctx->sample_rate;
}

int vx_get_audio_present(vx_video* me)
{
	return me->audio_codec_ctx ? 1 : 0;
}

int vx_get_audio_channels(vx_video* me)
{
	if(!me->audio_codec_ctx)
		return 0;

	return me->audio_codec_ctx->channels;
}

const char* vx_get_audio_sample_format_str(vx_video* me)
{
	if(!me->audio_codec_ctx)
		return NULL;

	return av_get_sample_fmt_name(me->audio_codec_ctx->sample_fmt);
}

static vx_error vx_decode_frame(vx_video* me, vx_frame_info* fi, AVFrame** out_frame, int* out_stream_idx)
{
	AVPacket packet;
	memset(&packet, 0, sizeof(packet));

	int frame_finished = 0;
	AVFrame* frame = av_frame_alloc();

	vx_error ret = VX_ERR_UNKNOWN;
	int64_t frame_pos = -1;
	int64_t file_pos = avio_tell(me->fmt_ctx->pb);
	int retries = 0;

	for(int i = 0; i < 1024; i++){
		if(!vx_read_frame(me->fmt_ctx, &packet, me->video_stream)){
			ret = VX_ERR_EOF;
			goto cleanup;
		}

		*out_stream_idx = packet.stream_index;

		if(packet.stream_index == me->video_stream || packet.stream_index == me->audio_stream){
			int bytes_remaining = packet.size;
			int bytes_decoded = 0;

			if(frame_pos < 0 && packet.pos != -1)
				frame_pos = packet.pos;

			// Decode until all bytes in the packet are decoded
			while(bytes_remaining > 0 && !frame_finished){
				if(packet.stream_index == me->video_stream){
					bytes_decoded = avcodec_decode_video2(me->video_codec_ctx, frame, &frame_finished, &packet);
				}else{
					bytes_decoded = avcodec_decode_audio4(me->audio_codec_ctx, frame, &frame_finished, &packet);
				}

				char eb[2048];

				if(bytes_decoded < 0){
					av_strerror(bytes_decoded, eb, sizeof(eb));

					if(retries++ > 1000){
						ret = VX_ERR_DECODE_VIDEO;
						goto cleanup;
					}

					// every 10 retries, skip ahead a few bytes
					if((retries % 10) == 0){
						int64_t fp = avio_tell(me->fmt_ctx->pb);
						avformat_seek_file(me->fmt_ctx, me->video_stream, fp + 100, fp + 512, fp + 1024 * 1024, AVSEEK_FLAG_BYTE | AVSEEK_FLAG_ANY);
					}

					break;
				}

				bytes_remaining -= bytes_decoded;
			}
		}

		av_free_packet(&packet);

		if(frame_finished)
			break;
	}
		
	fi->flags = frame->pict_type == AV_PICTURE_TYPE_I ? VX_FF_KEYFRAME : 0;
	fi->flags |= frame_pos < 0 ? VX_FF_BYTE_POS_GUESSED : 0;
	fi->flags |= frame->pts > 0 ? VX_FF_HAS_PTS : 0; 

	fi->pos = frame_pos >= 0 ? frame_pos : file_pos;	
	fi->pts = av_frame_get_best_effort_timestamp(frame);
	fi->dts = frame->pkt_dts;

	*out_frame = frame;
	
	return VX_ERR_SUCCESS;

cleanup:
	if(frame){
		av_frame_unref(frame);
		av_frame_free(&frame);
	}

	if(packet.data)
		av_free_packet(&packet);

	return ret;
}

static vx_error vx_scale_frame(vx_video* me, AVFrame* frame, vx_frame* vxframe)
{
	vx_error ret = VX_ERR_UNKNOWN;

	AVPicture pict;
	int av_pixfmt = vx_to_av_pix_fmt(vxframe->pix_fmt);
	if(avpicture_fill(&pict, vxframe->buffer, av_pixfmt, vxframe->width, vxframe->height) < 0){
		ret = VX_ERR_SCALING;
		goto cleanup;
	}

	struct SwsContext* sws_ctx = sws_getContext(me->video_codec_ctx->width, me->video_codec_ctx->height, 
		me->video_codec_ctx->pix_fmt, vxframe->width, vxframe->height, av_pixfmt, SWS_BILINEAR, NULL, NULL, NULL);

	if(!sws_ctx){
		ret = VX_ERR_SCALING;
		goto cleanup;
	}

	assert(frame->data);

	sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, me->video_codec_ctx->height, pict.data, pict.linesize); 
	sws_freeContext(sws_ctx);
	
	return VX_ERR_SUCCESS;

cleanup:

	return ret;
}

vx_error vx_get_frame(vx_video* me, vx_frame* vxframe)
{
	vx_error ret = VX_ERR_UNKNOWN;
	AVFrame* frame = NULL;

	while(me->num_queue < FRAME_QUEUE_SIZE && me->decoding_error == VX_ERR_SUCCESS){
		vx_frame_info fi;
		int stream_idx = -1;
		ret = vx_decode_frame(me, &fi, &frame, &stream_idx);

		if(ret == VX_ERR_EOF || ret == VX_ERR_DECODE_VIDEO){
			if(me->decoding_error == VX_ERR_SUCCESS)
				me->decoding_error = ret;
			break;
		}

		if(ret != VX_ERR_SUCCESS)
			goto cleanup;
		
		if(stream_idx == me->video_stream){
			// video frame

			vx_frame_queue_item item;

			item.info = fi;
			item.frame = frame;

			vx_enqueue(me, item);
		}

		else if(stream_idx == me->audio_stream && me->audio_cb){
			// audio frame (and audio is enabled)

			int64_t pts = av_frame_get_best_effort_timestamp(frame);
			double ts = pts * av_q2d(me->fmt_ctx->streams[me->audio_stream]->time_base);

			AVCodecContext* actx = me->audio_codec_ctx;
			int dst_sample_count = av_rescale_rnd(frame->nb_samples, me->out_sample_rate, actx->sample_rate, AV_ROUND_UP);

			if(me->swr_channels != me->audio_codec_ctx->channels || me->swr_channel_layout != me->audio_codec_ctx->channel_layout
				|| me->swr_sample_rate != me->audio_codec_ctx->sample_rate || me->swr_sample_format != me->audio_codec_ctx->sample_fmt)
			{
				dprintf("audio format changed\n");
				dprintf("channels:       %d -> %d\n", me->swr_channels, me->audio_codec_ctx->channels);
				dprintf("channel layout: %08"PRIx64" -> %08"PRIx64"\n", me->swr_channel_layout, me->audio_codec_ctx->channel_layout);
				dprintf("sample rate:    %d -> %d\n", me->swr_sample_rate, me->audio_codec_ctx->sample_rate);
				dprintf("sample format:  %d -> %d\n", me->swr_sample_format, me->audio_codec_ctx->sample_fmt);

				// reinitialize swr_ctx if the audio codec magically changed parameters
				vx_set_audio_params(me, me->out_sample_rate, me->out_channels, me->out_sample_format, me->audio_cb, me->audio_user_data);
			}

			int swrret = swr_convert(me->swr_ctx, me->audio_buffer, dst_sample_count, (const uint8_t**)frame->data, frame->nb_samples);

			if(swrret < 0){
				ret = VX_ERR_RESAMPLE_AUDIO;
				goto cleanup;
			}

			me->audio_cb(me->audio_buffer[0], swrret, ts, me->audio_user_data);
		}
	}

	if(me->num_queue > 0){
		vx_frame_queue_item item = vx_dequeue(me);

		frame = item.frame;
		vxframe->info = item.info;

		ret = vx_scale_frame(me, frame, vxframe);
		if(ret != VX_ERR_SUCCESS)
			goto cleanup;

		av_frame_unref(frame);
		av_frame_free(&frame);
	}

	else{
		return me->decoding_error;
	}

	return VX_ERR_SUCCESS;

cleanup:
	if(frame){
		av_frame_unref(frame);
		av_frame_free(&frame);
	}

	return ret;
}

vx_error vx_get_frame_rate(vx_video* me, float* out_fps)
{
	AVRational rate = me->fmt_ctx->streams[me->video_stream]->avg_frame_rate;

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
		"error while decoding video",            //VX_ERR_DECODE_VIDEO    = 10,
		"error while scaling",                   //VX_ERR_SCALING         = 11,
		"could not get pixel aspect ratio",      //VX_ERR_PIXEL_ASPECT    = 12,
		"error while decoding audio",            //VX_ERR_DECODE_AUDIO    = 13,
		"no audio available",                    //VX_ERR_NO_AUDIO        = 14,
	  "error while resampling audio",          //VX_ERR_RESAMPLE_AUDIO  = 15,
	};

	return err_str[error];
}

double vx_timestamp_to_seconds(vx_video* me, long long ts)
{
	return (double)ts * av_q2d(me->fmt_ctx->streams[me->video_stream]->time_base);
}

vx_error vx_get_pixel_aspect_ratio(vx_video* me, float* out_par)
{
	AVRational par = me->video_codec_ctx->sample_aspect_ratio;
	if(par.num == 0 && par.den == 1)
		return VX_ERR_PIXEL_ASPECT;

	*out_par = (float)av_q2d(par);
	return VX_ERR_SUCCESS;
}

vx_frame* vx_frame_create(int width, int height, vx_pix_fmt pix_fmt)
{
	vx_frame* me = calloc(1, sizeof(vx_frame));

	if(!me)
		goto error;
	
	me->width = width;
	me->height = height;
	me->pix_fmt = pix_fmt;

	int av_pixfmt = vx_to_av_pix_fmt(pix_fmt);
	int size = avpicture_get_size(av_pixfmt, width, height);

	if(size <= 0)
		goto error;

	me->buffer = av_malloc(size);

	if(!me->buffer)
		goto error;

	return me;

error:
	if(me)
		free(me);
	
	return NULL;
}

void vx_frame_destroy(vx_frame* me)
{
	av_free(me->buffer);
	free(me);
}

unsigned int vx_frame_get_flags(vx_frame* me)
{
	return me->info.flags;
}

long long vx_frame_get_byte_pos(vx_frame* me)
{
	return me->info.pos;
}

long long vx_frame_get_dts(vx_frame* me)
{
	return me->info.dts;
}

long long vx_frame_get_pts(vx_frame* me)
{
	return me->info.pts;
}

void* vx_frame_get_buffer(vx_frame* frame)
{
	return frame->buffer;
}

vx_error vx_set_audio_params(vx_video* me, int sample_rate, int channels, vx_sample_fmt format, vx_audio_callback cb, void* user_data)
{
	vx_error err = VX_ERR_UNKNOWN;

	if(me->audio_stream < 0)
		return VX_ERR_NO_AUDIO;

	me->audio_cb = cb;
	me->out_channels = channels;
	me->out_sample_rate = sample_rate;
	me->out_sample_format = format;
	me->audio_user_data = user_data;

	if(me->swr_ctx)
		swr_free(&me->swr_ctx);
	
	if(me->audio_buffer){
		av_freep(&me->audio_buffer[0]);
		av_freep(&me->audio_buffer);
	}
			
	AVCodecContext* ctx = me->audio_codec_ctx;

	enum AVSampleFormat avfmt = format == VX_SAMPLE_FMT_FLT ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_S16;

	int64_t src_channel_layout = ctx->channel_layout != 0 ? ctx->channel_layout :
		av_get_default_channel_layout(ctx->channels);

	me->swr_ctx = swr_alloc_set_opts(NULL, av_get_default_channel_layout(channels),
			avfmt, me->out_sample_rate, src_channel_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);

	me->swr_channels = ctx->channels;
	me->swr_channel_layout = ctx->channel_layout;
	me->swr_sample_rate = ctx->sample_rate;
	me->swr_sample_format = ctx->sample_fmt;
	
	if(!me->swr_ctx){
		err = VX_ERR_ALLOCATE;
		goto cleanup;
	}

	swr_init(me->swr_ctx);

	int ret = av_samples_alloc_array_and_samples(&me->audio_buffer, &me->audio_line_size, channels, sample_rate * 4, avfmt, 0);

	if(ret < 0){
		err = VX_ERR_ALLOCATE;
		goto cleanup;
	}
	
	return VX_ERR_SUCCESS;

cleanup:

	if(me->swr_ctx)
		swr_free(&me->swr_ctx);

	if(me->audio_buffer){
		av_freep(&me->audio_buffer[0]);
		av_freep(&me->audio_buffer);
	}
	
	return err;
}
