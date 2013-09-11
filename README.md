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
//example.c

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
        LASSERT(ret == VX_ERR_SUCCESS, "error: '%s' reported for '%s'", vx_get_error_str(ret), argv[1]); 

        int w = vx_get_width(video), h = vx_get_height(video);
        char* frame_data = calloc(1, w * h);
        LASSERT(frame_data, "could not allocate frame data buffer");

        while( vx_get_frame(video, w, h, VX_PIX_FMT_GRAY8, frame_data) == VX_ERR_SUCCESS ){
                // do something with grayscale frame_data 
                // use VX_PIX_FMT_RGB24 for RGB data but remember to * 3 in image_data allocation 
        }

        vx_close(video);
        free(frame_data);

        return 0;
}
```

compile with

    gcc -std=c99 -Wall example.c `pkg-config --libs --cflags libvx` -o example
