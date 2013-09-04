#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc == 2, "usage: %s [videofile]", argv[0]);

	vx_video_t* video = vx_open(argv[1]);
	LASSERT(video, "could not open video file: %s", argv[1]);	

	char* buffer = calloc(1, vx_get_width(video) * vx_get_height(video));
	LASSERT(buffer, "could not allocate frame buffer");

	int num = 0;
	while( vx_get_frame(video, vx_get_width(video), vx_get_height(video), VX_PIX_FMT_GRAY8, buffer) == 0 ){
		printf("\rframe %d        ", ++num);
	}
	printf("\n");

	vx_close(video);

	return 0;
}
