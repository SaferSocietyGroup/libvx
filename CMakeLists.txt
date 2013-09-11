PROJECT(libvx C)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -ggdb -std=c99")

cmake_minimum_required(VERSION 2.4)

SET(TARGET_NAME "vx")

SET(SOURCE_FILES
	src/libvx.c
)

SET(CPACK_PACKAGE_NAME libvx)
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "1")
SET(CPACK_PACKAGE_VERSION_PATCH "0")
SET(CPACK_PACKAGE_VENDOR "NetClean AB")
SET(CPACK_PACKAGE_CONTACT "Fredrik Hultin <noname@nurd.se>")
SET(CPACK_PACKAGE_DESCRIPTION "libvx is a video file frame extraction library using ffmpeg")

find_package(PkgConfig)

pkg_check_modules(LIBAVDEVICE libavdevice)
pkg_check_modules(LIBAVFILTER libavfilter)
pkg_check_modules(LIBAVFORMAT libavformat)
pkg_check_modules(LIBSWSCALE libswscale)
pkg_check_modules(LIBAVUTIL libavutil)

INCLUDE_DIRECTORIES(
        include
	${LIBAVDEVICE_INCLUDE_DIRS}
	${LIBAVFILTER_INCLUDE_DIRS}
	${LIBAVFORMAT_INCLUDE_DIRS}
	${LIBSWSCALE_INCLUDE_DIRS}
	${LIBAVUTIL_INCLUDE_DIRS}
)

ADD_LIBRARY(vx STATIC ${SOURCE_FILES})

# Check if cmake has the deb-file generator
IF(EXISTS "${CMAKE_ROOT}/Modules/CPackDeb.cmake")
	SET(CPACK_GENERATOR DEB)

	FIND_PROGRAM(DPKG "dpkg")
	IF(NOT DPKG STREQUAL "DPKG-NOTFOUND")
		EXEC_PROGRAM(dpkg ARGS --print-architecture OUTPUT_VARIABLE CPACK_SYSTEM_NAME)
	ELSE(NOT DPKG STREQUAL "DPKG-NOTFOUND")
		SET(CPACK_SYSTEM_NAME "i386")
	ENDIF(NOT DPKG STREQUAL "DPKG-NOTFOUND")
	INCLUDE(CPack)
ENDIF(EXISTS "${CMAKE_ROOT}/Modules/CPackDeb.cmake")

INSTALL(TARGETS vx ARCHIVE DESTINATION lib)
INSTALL(FILES include/libvx.h DESTINATION include/libvx)
INSTALL(FILES pkg-config/libvx.pc DESTINATION lib/pkgconfig)
