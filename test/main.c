#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc == 2, "usage: %s [videofile]", argv[0]);
	
	int num_frames;
	vx_error ret;
	 
	ret = vx_count_frames_in_file(argv[1], &num_frames);
	LASSERT(ret == VX_ERR_SUCCESS, "could not count frames in video file: %s", argv[1]);	
	printf("num_frames: %d\n", num_frames);

	vx_video* video = NULL;

	ret = vx_open(&video, argv[1]);
	LASSERT(ret == VX_ERR_SUCCESS, "could not open video file: %s", argv[1]);	
	
	int w = vx_get_width(video);
	int h = vx_get_height(video);

	printf("video size: %d x %d\n", w, h);
	
	vx_frame* frame = vx_frame_create(w, h, VX_PIX_FMT_GRAY8);
	LASSERT(frame, "could not allocate frame");

	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Surface* screen = SDL_SetVideoMode(w, h, 0, 0);
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(vx_frame_get_buffer(frame), w, h, 8, w, 0xff, 0xff, 0xff, 0);

	num_frames = 0;
	while( vx_get_frame(video, frame) == VX_ERR_SUCCESS ){
		SDL_BlitSurface(surface, NULL, screen, NULL);
		SDL_Flip(screen);
		
		if(vx_frame_get_flags(frame) & VX_FF_KEYFRAME){
			double dts = vx_timestamp_to_seconds(video, vx_frame_get_dts(frame));
			double pts = vx_timestamp_to_seconds(video, vx_frame_get_pts(frame));

			printf("%d is a keyframe, byte pos: %llu%s dts/pts (in secs): %f/%f, has pts: %s\n", num_frames, 
				vx_frame_get_byte_pos(frame), vx_frame_get_flags(frame) & VX_FF_BYTE_POS_GUESSED ? " (guessed)" : "", 
				dts, pts, (vx_frame_get_flags(frame) & VX_FF_HAS_PTS ? "true" : "false"));
		}
		
		num_frames++;
	}

	vx_frame_destroy(frame);

	printf("\n");
	printf("num_frames: %d\n", num_frames);

	vx_close(video);

	return 0;
}
