libvx
=====

A video frame extraction library using ffmpeg.

License
-------

See LICENSE (GPLv3)

Authors
-------
 * Fredrik Hultin <noname@nurd.se>, NetClean AB

Dependencies
------------
   * ffmpeg (libavformat, libavcodec, libavutil, etc.)
   * A C99 capable compiler (gcc, mingw, clang, etc.)
   * pkg-config
   * CMake (or spank, https://github.com/noname22/spank)

Compiling & Installing
----------------------

Using CMake (http://www.cmake.org/)

    mkdir build && cd build
    cmake ..
    make
    sudo make install

Using spank (https://github.com/noname22/spank)

    spank install

Usage
-----

```c
#include <libvx.h>
#include <stdlib.h>
#include <stdio.h>

#define LASSERT(_v, ...) if(!(_v)){ printf(__VA_ARGS__); puts(""); exit(1); };

int main(int argc, char** argv)
{
	LASSERT(argc == 2, "usage: %s [videofile]", argv[0]);

	vx_error ret;
	vx_video* video;

	ret = vx_open(&video, argv[1]);
	LASSERT(ret == VX_ERR_SUCCESS, "error: '%s' reported for '%s'", vx_get_error_str(ret), argv[1]);	

	int w = vx_get_width(video), h = vx_get_height(video);

	char* buffer = vx_alloc_frame_buffer(w, h, VX_PIX_FMT_GRAY8);
	LASSERT(buffer, "could not allocate frame buffer");

	float fps = 30.0f;
	if(vx_get_frame_rate(video, &fps) != VX_ERR_SUCCESS)
		printf("could not determine fps, guessing %f\n", fps);

	float par = 1.0f;
	if(vx_get_pixel_aspect_ratio(video, &par) != VX_ERR_SUCCESS)
		printf("could not determine pixel aspect ratio, guessing %f\n", par);

	printf("video file: %s\n", argv[0]);
	printf("%d x %d @ %.2f fps, PAR: %.2f\n", w, h, fps, par);

	int num = 0;
	vx_frame_info* fi = vx_fi_create();

	while( vx_get_frame(video, w, h, VX_PIX_FMT_GRAY8, buffer, fi) == VX_ERR_SUCCESS ){
		printf("\rframe %d        ", ++num);
	}
	printf("\n");

	vx_fi_destroy(fi);
	vx_free_frame_buffer(buffer);
	vx_close(video);

	return 0;
}
```

compile with

    gcc -std=c99 -Wall example.c `pkg-config --libs --cflags libvx` -o example
