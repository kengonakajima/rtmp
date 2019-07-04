#include "librtmp/rtmp.h"
#include "librtmp/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// need brew ffmpeg
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h" 
};


static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(pCodecContext, pPacket);

    if (response < 0) {
        fprintf(stderr,"Error while sending a packet to the decoder: %s\n", av_err2str(response));
        return response;
    }

    while (response >= 0) {
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
        response = avcodec_receive_frame(pCodecContext, pFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            fprintf(stderr,"Error while receiving a frame from the decoder: %s\n", av_err2str(response));
            return response;
        }

        if (response >= 0) {
            fprintf(stderr,
                    "Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d] Format:%d\n",
                    pCodecContext->frame_number,
                    av_get_picture_type_char(pFrame->pict_type),
                    pFrame->pkt_size,
                    pFrame->pts,
                    pFrame->key_frame,
                    pFrame->coded_picture_number,
                    pFrame->format // AV_PIX_FMT_YUV420P = 0
                    
                    );

            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
            // save a grayscale frame into a .pgm file
            save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);
            fprintf(stderr,"pointers: %p %p %p %p\n", // Y U V
                    pFrame->data[0], pFrame->data[1], pFrame->data[2], pFrame->data[3]);
        }
    }
    return 0;
}



// ./player 127.0.0.1 appname streamname
int main( int argc, char **argv ) {
    if(argc<3){
        RTMP_LogPrintf("need IP APP PATH args. Ex: 127.0.0.1 app live");
        return 1;
    }
    char *iparg=argv[1];
    char *apparg=argv[2];
    char *playpatharg=argv[3];

    
    // decoder

    av_register_all();
    avformat_network_init();
    
    AVFormatContext *pFormatContext = avformat_alloc_context();
    fprintf(stderr,"avformat_alloc_context: ret: %p\n",pFormatContext);
    if(!pFormatContext) {
        fprintf(stderr,"fatal\n");
        return 1;
    }
    char input[]="hoge.mov";
    char inputrtmp[]="rtmp://139.162.118.45/app/live";
    if(avformat_open_input(&pFormatContext, inputrtmp, NULL, NULL) != 0) { // blocks if no stream live
        fprintf(stderr,"avformat_open_input failed\n");
        return 1;
    }
    fprintf(stderr,"avformat_open_input ok\n");

    if(avformat_find_stream_info(pFormatContext,  NULL) < 0) {
        fprintf(stderr,"avformat_find_stream_info failed\n");
        return 1;
    }
    fprintf(stderr,"avformat_find_stream_info ok\n"); 

    av_dump_format(pFormatContext,0,input,0);

    AVCodec *pCodec = NULL;
    AVCodecParameters *pCodecParameters =  NULL;
    
    int videoindex=-1;
    for(int i=0;i<pFormatContext->nb_streams;i++) {
        AVCodecParameters *pLocalCodecParameters =  NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        fprintf(stderr,"AVStream->time_base before open coded %d/%d\n", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        fprintf(stderr,"AVStream->r_frame_rate before open coded %d/%d\n", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        fprintf(stderr,"AVStream->start_time %" PRId64 "\n", pFormatContext->streams[i]->start_time);
        fprintf(stderr,"AVStream->duration %" PRId64 "\n", pFormatContext->streams[i]->duration);
        fprintf(stderr,"finding the proper decoder (CODEC)\n");
        AVCodec *pLocalCodec = NULL;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        if(!pLocalCodec) {
            fprintf(stderr,"pLocalCodec null\n");
            return 1;
        }
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if(videoindex==-1) {
                videoindex = i;
                fprintf(stderr,"videoindex:%d\n",videoindex);
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
                
            }
            fprintf(stderr,"Video Codec: resolution %d x %d\n", pLocalCodecParameters->width, pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            fprintf(stderr,"Audio Codec: %d channels, sample rate %d\n", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
            
        }
        fprintf(stderr,"\tCodec %s ID %d bit_rate %lld\n", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext) {
        fprintf(stderr,"failed to allocated memory for AVCodecContext\n");
        return 1;
    }


    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
        fprintf(stderr,"failed to copy codec params to codec context\n");
        return 1;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        fprintf(stderr,"failed to open codec through avcodec_open2\n");
        return 1;
    }

    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame) {
        fprintf(stderr,"failed to allocated memory for AVFrame\n");
        return 1;
    }
    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket) {
        fprintf(stderr,"failed to allocated memory for AVPacket\n");
        return 1;
    }

    int how_many_packets_to_process=8;
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        // if it's the video stream
        if (pPacket->stream_index == videoindex) {
            fprintf(stderr,"AVPacket->pts %" PRId64 "\n", pPacket->pts);
            int response = decode_packet(pPacket, pCodecContext, pFrame);
            if (response < 0) break;
            // stop it, otherwise we'll be saving hundreds of frames
            if (--how_many_packets_to_process <= 0) break;
        }
        av_packet_unref(pPacket);
    }

    fprintf(stderr,"releasing all the resources\n");
    avformat_close_input(&pFormatContext);
    avformat_free_context(pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
    
    fprintf(stderr,"end\n");
    
    exit(0);

    // network
    RTMP_LogPrintf( "start. host:%s\n", argv[1]);
    RTMP rtmp={0};
    RTMP_Init(&rtmp);
    RTMP_debuglevel = RTMP_LOGDEBUG;
    AVal hostname={ argv[1], strlen(argv[1]) };
    AVal sockshost={ 0, 0 };
    AVal playpath={playpatharg,strlen(playpatharg)};
    char url[1024];
    snprintf(url,sizeof(url), "rtmp://%s/%s", iparg, apparg);
    AVal tcUrl={ url, strlen(url) };
    AVal swfUrl={0,0};
    AVal pageUrl={0,0};
    AVal app={apparg,strlen(apparg)};
    AVal auth={0,0};
    AVal swfHash={0,0};
    AVal flashVer={0,0};
    AVal subscribepath={0,0};
    AVal usherToken={0,0};
    RTMP_SetupStream(&rtmp, RTMP_PROTOCOL_RTMP, &hostname, 1935, &sockshost, &playpath, &tcUrl,
                     &swfUrl, &pageUrl, &app, &auth, &swfHash, 0,
                     &flashVer, &subscribepath, &usherToken, 0, 0, false, 30
                     );


    int r=RTMP_Connect(&rtmp,NULL);
    fprintf(stderr, "RTMP_Connect return:%d\n", r);
    if(!r) {
        fprintf(stderr, "fatal, quit\n");
        exit(1);
    }
    r = RTMP_ConnectStream(&rtmp,0);
    fprintf(stderr, "RTMP_ConnectStream return:%d\n", r);

    FILE *fp=fopen("hoge","wb");
    while(1) {
        usleep(50*1000);
        fprintf(stderr,".");
        unsigned int nt=RTMP_GetTime();
        size_t bufsize = 64*1024;
        char *buffer = (char*)malloc(bufsize);
        int nr=RTMP_Read(&rtmp,buffer,bufsize);
        fprintf(stderr, "time:%u read:%d\n",nt,nr);
        if(nr>0) {
            fwrite(buffer,1,nr, fp);
            fflush(fp);
        }
    }
    
}
