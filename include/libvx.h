#ifndef LIBVX_H
#define LIBVX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vx_video vx_video_t;

typedef enum {
	VX_PIX_FMT_RGB24,
	VX_PIX_FMT_GRAY8
} vx_pix_fmt_t;

vx_video_t* vx_open(const char* filename);
void vx_close(vx_video_t* video);

int vx_get_width(vx_video_t* video);
int vx_get_height(vx_video_t* video);

int vx_get_frame(vx_video_t* video, int width, int height, vx_pix_fmt_t pix_fmt, void* buffer);

#ifdef __cplusplus
}
#endif

#endif
