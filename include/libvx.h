#ifndef LIBVX_H
#define LIBVX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_video vx_video;
typedef struct vx_frame vx_frame;

typedef enum {
	VX_PIX_FMT_RGB24 = 0,
	VX_PIX_FMT_GRAY8 = 1,
	VX_PIX_FMT_RGB32 = 2,
} vx_pix_fmt;

typedef enum {
	VX_SAMPLE_FMT_S16 = 0,
	VX_SAMPLE_FMT_FLT = 1
} vx_sample_fmt;

typedef enum {
	VX_ERR_SUCCESS         = 0,
	VX_ERR_UNKNOWN         = 1,
	VX_ERR_ALLOCATE        = 2,
	VX_ERR_FRAME_RATE      = 3,
	VX_ERR_OPEN_FILE       = 4,
	VX_ERR_STREAM_INFO     = 5,
	VX_ERR_VIDEO_STREAM    = 6,
	VX_ERR_FIND_CODEC      = 7,
	VX_ERR_OPEN_CODEC      = 8,
	VX_ERR_EOF             = 9,
	VX_ERR_DECODE_VIDEO    = 10,
	VX_ERR_SCALING         = 11,
	VX_ERR_PIXEL_ASPECT    = 12,
	VX_ERR_DECODE_AUDIO    = 13,
	VX_ERR_NO_AUDIO        = 14,
	VX_ERR_RESAMPLE_AUDIO  = 15,
} vx_error;

typedef enum {
	VX_FF_KEYFRAME = 1,
	VX_FF_BYTE_POS_GUESSED = 2,
	VX_FF_HAS_PTS = 4
} vx_frame_flag;

typedef void (*vx_audio_callback)(const void* samples, int num_samples, double ts, void* user_data);

vx_error vx_open(vx_video** video, const char* filename);
void vx_close(vx_video* video);

int vx_get_width(vx_video* video);
int vx_get_height(vx_video* video);

int vx_get_audio_present(vx_video* video);
int vx_get_audio_sample_rate(vx_video* video);
int vx_get_audio_channels(vx_video* video);
const char* vx_get_audio_sample_format_str(vx_video* video);

vx_error vx_set_audio_params(vx_video* me, int sample_rate, int channels, vx_sample_fmt format, vx_audio_callback cb, void* user_data);

long long vx_get_file_position(vx_video* video);
long long vx_get_file_size(vx_video* video);
double vx_timestamp_to_seconds(vx_video* video, long long ts);

vx_error vx_count_frames_in_file(const char* filename, int* out_num_frames);

vx_error vx_get_pixel_aspect_ratio(vx_video* video, float* out_par);

vx_error vx_get_frame_rate(vx_video* video, float* out_fps);
vx_error vx_get_duration(vx_video* video, float* out_duration);

vx_error vx_get_frame(vx_video* video, vx_frame* frame);
const char* vx_get_error_str(vx_error error);

vx_frame* vx_frame_create(int width, int height, vx_pix_fmt pix_fmt);
void vx_frame_destroy(vx_frame* frame);

unsigned int vx_frame_get_flags(vx_frame* frame);
long long vx_frame_get_byte_pos(vx_frame* frame);
long long vx_frame_get_dts(vx_frame* frame);
long long vx_frame_get_pts(vx_frame* frame);
void* vx_frame_get_buffer(vx_frame* frame);

#ifdef __cplusplus
}
#endif

#endif
