#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL.h>
#include <stdbool.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

static double lastAudioTs = .0;

void audio_callback(const void* samples, int num_samples, double ts, void* user_data)
{
	//FILE* f = (FILE*)user_data;
	//fwrite(samples, sizeof(float) * 2 * num_samples, 1, f);
	//printf("audio samples with timestamp: %f, delta: %f\n", ts, ts - lastAudioTs);
	lastAudioTs = ts;
}

#define TGA_HEADER_SIZE 18
#define BPP 4

int save_tga_mem(vx_frame* frame, int w, int h, char** out_buffer)
{
	const char* frame_buffer = vx_frame_get_buffer(frame); 

	char* buffer = calloc(1, w * h * 3 + TGA_HEADER_SIZE);
	if(!buffer)
		return 0;

	// uncompressed RGB
	buffer[2] = 2;

	// width
	buffer[12] = w & 0xff;
	buffer[13] = (w >> 8) & 0xff;

	// height
	buffer[14] = h & 0xff;
	buffer[15] = (h >> 8) & 0xff;

	// bits per pixel
	buffer[16] = 24;

	*out_buffer = buffer;

	buffer += TGA_HEADER_SIZE;

	for(int y = h - 1; y >= 0; y--){
		for(int x = 0; x < w; x++){
			int i = (x + y * w) * BPP;

			*(buffer++) = frame_buffer[i+0];
			*(buffer++) = frame_buffer[i+1];
			*(buffer++) = frame_buffer[i+2];
		}
	}

	return w * h * 3 + TGA_HEADER_SIZE;
}


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

	vx_frame* frame = vx_frame_create(w, h, VX_PIX_FMT_RGB32);
	LASSERT(frame, "could not allocate frame");

	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Surface* screen = SDL_SetVideoMode(w, h, 0, 0);
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(vx_frame_get_buffer(frame), w, h, 32, w * 4, 0xff0000, 0xff00, 0xff, 0);
	
	FILE* faud = NULL;
	int audio_present = vx_get_audio_present(video);
	printf("has audio: %s\n", audio_present ? "yes" : "no");

	if(audio_present){
		int sample_rate = vx_get_audio_sample_rate(video);
		int channels = vx_get_audio_channels(video);
		const char* fmt = vx_get_audio_sample_format_str(video);

		printf("audio format: %d channels @ %d Hz %s\n", channels, sample_rate, fmt);

		faud = fopen("audio-float.raw", "wb");
		ret = vx_set_audio_params(video, 48000, 2, VX_SAMPLE_FMT_FLT, audio_callback, (void*)faud);

		LASSERT(ret == VX_ERR_SUCCESS, "could not set audio parameters");
	}

	num_frames = 0;
	bool done = false;
	double lastTs = .0;

	while( !done && (ret = vx_get_frame(video, frame)) == VX_ERR_SUCCESS ){
		SDL_Event event;
		
		while(SDL_PollEvent(&event)){
			if(event.type == SDL_QUIT)
				done = false;
		}

		SDL_BlitSurface(surface, NULL, screen, NULL);
		SDL_Flip(screen);
			
		double dts = vx_timestamp_to_seconds(video, vx_frame_get_dts(frame));
		double pts = vx_timestamp_to_seconds(video, vx_frame_get_pts(frame));
		long long ts = vx_frame_get_pts(frame);

		printf("frame with ts: %f (%"PRId64"), delta: %f\n", pts, ts, pts - lastTs);
		lastTs = pts;
		
		if(vx_frame_get_flags(frame) & VX_FF_KEYFRAME){
			printf("%d is a keyframe, byte pos: %"PRIu64"%s dts/pts (in secs): %f/%f, has pts: %s\n", num_frames, 
				(uint64_t)vx_frame_get_byte_pos(frame), vx_frame_get_flags(frame) & VX_FF_BYTE_POS_GUESSED ? " (guessed)" : "", 
				dts, pts, (vx_frame_get_flags(frame) & VX_FF_HAS_PTS ? "true" : "false"));
		}

#if 0
		if(num_frames == 100){
			FILE* f = fopen("image.data", "wb");
			fwrite(vx_frame_get_buffer(frame), w * 4 * h, 1, f);
			fclose(f);
		}
#endif
#if 0
		if(num_frames == 100){
			char* buf;
			int size = save_tga_mem(frame, w, h, &buf);
			FILE* f = fopen("image.tga", "wb");
			fwrite(buf, size, 1, f);
			fclose(f);
		}
#endif
		
		num_frames++;
	}

	printf("stopped because: %s\n", vx_get_error_str(ret));

	vx_frame_destroy(frame);

	printf("\n");
	printf("num_frames: %d\n", num_frames);

	vx_close(video);

	if(faud)
		fclose(faud);

	return 0;
}
