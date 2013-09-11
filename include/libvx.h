#ifndef LIBVX_H
#define LIBVX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_video vx_video_t;

typedef enum {
	VX_PIX_FMT_RGB24 = 0,
	VX_PIX_FMT_GRAY8 = 1,
} vx_pix_fmt_t;

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
} vx_error_t;

vx_error_t vx_open(vx_video_t** video, const char* filename);
void vx_close(vx_video_t* video);

int vx_get_width(vx_video_t* video);
int vx_get_height(vx_video_t* video);

vx_error_t vx_get_frame_rate(vx_video_t* video, float* out_fps);
vx_error_t vx_get_duration(vx_video_t* video, float* out_duration);

vx_error_t vx_get_frame(vx_video_t* video, int width, int height, vx_pix_fmt_t pix_fmt, void* out_buffer);

#ifdef __cplusplus
}
#endif

#endif
