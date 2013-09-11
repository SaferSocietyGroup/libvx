#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc == 2, "usage: %s [videofile]", argv[0]);

	vx_error_t ret;
	vx_video_t* video;

	ret = vx_open(&video, argv[1]);
	LASSERT(ret == VX_ERR_SUCCESS, "could not open video file: %s", argv[1]);	

	int w = vx_get_width(video), h = vx_get_height(video);
	char* buffer = calloc(1, w * h);
	LASSERT(buffer, "could not allocate frame buffer");

	float fps = 30.0f;
	if(vx_get_frame_rate(video, &fps) != VX_ERR_SUCCESS)
		printf("could not determine fps, guessing 30.0\n");

	printf("video file: %s\n", argv[0]);
	printf("%d x %d @ %.2f fps\n", w, h, fps);

	int num = 0;
	while( vx_get_frame(video, w, h, VX_PIX_FMT_GRAY8, buffer) == VX_ERR_SUCCESS ){
		printf("\rframe %d        ", ++num);
	}
	printf("\n");

	vx_close(video);

	return 0;
}
