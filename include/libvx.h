#ifndef LIBVX_H
#define LIBVX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_video vx_video;

typedef enum {
	VX_PIX_FMT_RGB24 = 0,
	VX_PIX_FMT_GRAY8 = 1,
} vx_pix_fmt;

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
} vx_error;

typedef enum {
	VX_FF_KEYFRAME = 1,
} vx_frame_flag;

vx_error vx_open(vx_video** video, const char* filename);
void vx_close(vx_video* video);

int vx_get_width(vx_video* video);
int vx_get_height(vx_video* video);

long long vx_get_file_position(vx_video* video);
long long vx_get_file_size(vx_video* video);

vx_error vx_count_frames_in_file(const char* filename, int* out_num_frames);

vx_error vx_get_pixel_aspect_ratio(vx_video* video, float* out_par);

vx_error vx_get_frame_rate(vx_video* video, float* out_fps);
vx_error vx_get_duration(vx_video* video, float* out_duration);

vx_error vx_get_frame(vx_video* video, int width, int height, vx_pix_fmt pix_fmt, unsigned int* out_frame_flags, void* out_buffer);
const char* vx_get_error_str(vx_error error);

void* vx_alloc_frame_buffer(int width, int height, vx_pix_fmt pix_fmt);
void vx_free_frame_buffer(void* buffer);

#ifdef __cplusplus
}
#endif

#endif
