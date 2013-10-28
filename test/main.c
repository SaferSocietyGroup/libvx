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

	char* buffer = vx_alloc_frame_buffer(w, h, VX_PIX_FMT_GRAY8);
	LASSERT(buffer, "could not allocate frame buffer");

	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Surface* screen = SDL_SetVideoMode(w, h, 0, 0);
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(buffer, w, h, 8, w, 0xff, 0xff, 0xff, 0);

	num_frames = 0;

	while( vx_get_frame(video, w, h, VX_PIX_FMT_GRAY8, buffer) == VX_ERR_SUCCESS ){
		SDL_BlitSurface(surface, NULL, screen, NULL);
		SDL_Flip(screen);
		num_frames++;
	}

	printf("\n");
	printf("num_frames: %d\n", num_frames);

	vx_close(video);
	vx_free_frame_buffer(buffer);

	return 0;
}
