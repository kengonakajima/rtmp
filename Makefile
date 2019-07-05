#FFMPEG=../ffmpeg-4.1.3
OPENSSLLIBS=/usr/local/Cellar/openssl/1.0.2r/lib/libssl.a /usr/local/Cellar/openssl/1.0.2r/lib/libcrypto.a
FFMPEGLIBS=/usr/local/Cellar/ffmpeg/4.1.3/lib/libavcodec.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libavformat.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libavutil.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libswresample.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libswscale.a -lsoxr -ltheora -lvorbisenc -lvorbisfile -lvorbis -lvpx -lx264 -lx265 -lxml2  /usr/local/Cellar/xvid/1.3.5/lib/libxvidcore.a
AVFRAMEWORKS=-framework AudioToolbox -lbz2 -framework CoreMedia -framework VideoToolbox
FRAMEWORKS=-framework IOKit -framework OpenGL -framework CoreFoundation -framework CoreVideo -m64  -L/usr/local/lib -framework OpenAL -framework AppKit -framework CoreGraphics $(AVFRAMEWORKS)
OPENCORELIBS=/usr/local/Cellar/opencore-amr/0.1.5/lib/libopencore-amrnb.a /usr/local/Cellar/opencore-amr/0.1.5/lib/libopencore-amrwb.a
ETCLIBS=/usr/local/Cellar/aom/1.0.0/lib/libaom.a /usr/local/Cellar/libbluray/1.1.0/lib/libbluray.a /usr/local/Cellar/freetype/2.10.0/lib/libfreetype.a /usr/local/Cellar/fontconfig/2.13.1/lib/libfontconfig.a /usr/local/Cellar/expat/2.2.7/lib/libexpat.a /usr/local/Cellar/gnutls/3.6.7.1/lib/libgnutls.dylib -liconv /usr/local/Cellar/lame/3.100/lib/libmp3lame.a -llzma /usr/local/Cellar/openjpeg/2.3.1/lib/libopenjp2.a /usr/local/Cellar/opus/1.3/lib/libopus.a -lpng -lsnappy -lspeex 




CFLAGS=-g -Imoyai -Imoyai/libuv-1.20.2/include -Imoyai/glfw-3.2/include -std=c++0x

JPEG=moyai/jpeg-9b
JPEGLIB=$(JPEG)/.libs/libjpeg.a # Don't use -ljpeg, because of macosx older deploy target

LIBUV=moyai/libuv-1.20.2
LIBUVLIB=$(LIBUV)/.libs/libuv.a # Don't use -luv, because of macosx older dep tgt

FREETYPE=moyai/freetype-2.4.10
FREETYPELIB=$(FREETYPE)/objs/.libs/libfreetype.a  # build product of freetype source

BZ2=moyai/bzip2-1.0.6
BZ2LIB=$(BZ2)/libbz2.a # build product of bz2 source

ZLIB=moyai/zlib-1.2.7
ZLIBLIB=$(ZLIB)/libz.a

GLFW=moyai/glfw-3.2
GLFWLIB=$(GLFW)/src/libglfw3.a


FTGLLIB=moyai/libftgl.a
MOYAICLILIB=moyai/libmoyaicl.a
MOYAISVLIB=moyai/libmoyaisv.a
SNAPPYLIB=moyai/libsnappy.a
ALUTLIB=moyai/libalut.a

COMMONLIBS= $(LIBPNGLIB) $(SNAPPYLIB) $(ALUTLIB) $(JPEGLIB) $(LIBUVLIB)
CLILIBS = $(EXTCOMMONLIBS) $(FREETYPELIB) $(FTGLLIB) $(GLFWLIB)
CLILIBFLAGS=-framework IOKit -framework OpenGL -framework CoreFoundation -framework CoreVideo -m64  -L/usr/local/lib -framework OpenAL -framework AppKit -framework CoreGraphics  $(OSX_TARGET_FLAG) -lcurl


all: player from_rtmpdump

player : main.cpp
	g++ $(CFLAGS) -o player main.cpp librtmp/librtmp.a $(OPENSSLLIBS) -lz $(FFMPEGLIBS) $(FRAMEWORKS) $(OPENCORELIBS) $(ETCLIBS) $(GLFWLIB) $(MOYAICLILIB) $(JPEGLIB) $(FTGLLIB) $(LIBUVLIB)

from_rtmpdump : from_rtmpdump.cpp
	g++ -g -o from_rtmpdump from_rtmpdump.cpp librtmp/librtmp.a $(OPENSSLLIBS)  -lz

