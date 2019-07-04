#FFMPEG=../ffmpeg-4.1.3
OPENSSLLIBS=/usr/local/Cellar/openssl/1.0.2r/lib/libssl.a /usr/local/Cellar/openssl/1.0.2r/lib/libcrypto.a
FFMPEGLIBS=/usr/local/Cellar/ffmpeg/4.1.3/lib/libavcodec.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libavformat.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libavutil.a /usr/local/Cellar/ffmpeg/4.1.3/lib/libswresample.a -lsoxr -ltheora -lvorbisenc -lvorbisfile -lvorbis -lvpx -lx264 -lx265 -lxml2  /usr/local/Cellar/xvid/1.3.5/lib/libxvidcore.a
AVFRAMEWORKS=-framework AudioToolbox -lbz2 -framework CoreMedia -framework VideoToolbox
FRAMEWORKS=-framework IOKit -framework OpenGL -framework CoreFoundation -framework CoreVideo -m64  -L/usr/local/lib -framework OpenAL -framework AppKit -framework CoreGraphics $(AVFRAMEWORKS)
OPENCORELIBS=/usr/local/Cellar/opencore-amr/0.1.5/lib/libopencore-amrnb.a /usr/local/Cellar/opencore-amr/0.1.5/lib/libopencore-amrwb.a
ETCLIBS=/usr/local/Cellar/aom/1.0.0/lib/libaom.a /usr/local/Cellar/libbluray/1.1.0/lib/libbluray.a /usr/local/Cellar/freetype/2.10.0/lib/libfreetype.a /usr/local/Cellar/fontconfig/2.13.1/lib/libfontconfig.a /usr/local/Cellar/expat/2.2.7/lib/libexpat.a /usr/local/Cellar/gnutls/3.6.7.1/lib/libgnutls.dylib -liconv /usr/local/Cellar/lame/3.100/lib/libmp3lame.a -llzma /usr/local/Cellar/openjpeg/2.3.1/lib/libopenjp2.a /usr/local/Cellar/opus/1.3/lib/libopus.a -lpng -lsnappy -lspeex 



player : main.o
	g++ -g -o player main.o librtmp/librtmp.a $(OPENSSLLIBS) -lz $(FFMPEGLIBS) $(FRAMEWORKS) $(OPENCORELIBS) $(ETCLIBS)



main.o : main.cpp
	g++ -g -c main.cpp 

