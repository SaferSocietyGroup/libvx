#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc == 2, "usage: %s [videofile]", argv[0]);

	vx_video_t* video = vx_open(argv[1]);
	LASSERT(video, "could not open video file: %s", argv[1]);	
	
	int w = vx_get_width(video);
	int h = vx_get_height(video);

	char* buffer = calloc(1, w * h);
	LASSERT(buffer, "could not allocate frame buffer");

	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Surface* screen = SDL_SetVideoMode(w, h, 0, 0);
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(buffer, w, h, 8, w, 0xff, 0xff, 0xff, 0);

	while( vx_get_frame(video, w, h, VX_PIX_FMT_GRAY8, buffer) == 0 ){
		SDL_BlitSurface(surface, NULL, screen, NULL);
		SDL_Flip(screen);
	}

	printf("\n");

	vx_close(video);

	return 0;
}
