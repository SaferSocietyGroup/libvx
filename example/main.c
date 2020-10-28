#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc >= 2, "usage: %s [videofile] ([videofile], ...)", argv[0]);

	for(int i = 1; i < argc; i++){
		vx_error ret;
		vx_video* video;
		int frame_count = 0;

		printf("=== %s ===\n", argv[i]);
		ret = vx_open(&video, argv[i]);
		LASSERT(ret == VX_ERR_SUCCESS, "error: '%s' reported for '%s'", vx_get_error_str(ret), argv[1]);	

		ret = vx_count_frames(video, &frame_count);
		printf("frame count: %d\n", frame_count);

		vx_close(video);

		ret = vx_open(&video, argv[1]);
		LASSERT(ret == VX_ERR_SUCCESS, "error: '%s' reported for '%s'", vx_get_error_str(ret), argv[1]);	

		int w = vx_get_width(video), h = vx_get_height(video);

		vx_frame* frame = vx_frame_create(w, h, VX_PIX_FMT_GRAY8);
		LASSERT(frame, "could not allocate frame buffer");

		float fps = 30.0f;
		if(vx_get_frame_rate(video, &fps) != VX_ERR_SUCCESS)
			printf("could not determine fps, guessing %f\n", fps);

		float par = 1.0f;
		if(vx_get_pixel_aspect_ratio(video, &par) != VX_ERR_SUCCESS)
			printf("could not determine pixel aspect ratio, guessing %f\n", par);

		printf("%d x %d @ %.2f fps, PAR: %.2f\n", w, h, fps, par);

		int num = 0;
		vx_error e;

		do{
			e = vx_get_frame(video, frame);
			printf("\rframe %d / %d       ", ++num, frame_count);
		} while(e <= VX_ERR_SUCCESS);

		printf("\n%s\n", vx_get_error_str(e));

		vx_frame_destroy(frame);
		vx_close(video);
	}

	return 0;
}
