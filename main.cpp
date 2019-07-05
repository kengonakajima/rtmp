#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h" // moyai

#include <pthread.h>

// need brew ffmpeg
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
};

GLFWwindow *g_win;
MoyaiClient *g_moyai_client;
Viewport *g_vp;
const int RETINA=1;
Layer *g_layer;
Image *g_img;
Texture *g_tex;
Prop2D *g_prop;
const int g_scrw=640, g_scrh=480;
bool g_game_done=false;

unsigned char g_picture_data[g_scrw*g_scrh*4];
int g_frame_num;


struct SwsContext *g_swsctx;


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
#if 0            
            print("Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d] Format:%d",
                  pCodecContext->frame_number,
                  av_get_picture_type_char(pFrame->pict_type),
                  pFrame->pkt_size,
                  (int)pFrame->pts,
                  pFrame->key_frame,
                  pFrame->coded_picture_number,
                  pFrame->format // AV_PIX_FMT_YUV420P = 0
                  );
#endif

            unsigned char *ydata=pFrame->data[0];
            unsigned char *udata=pFrame->data[1];
            unsigned char *vdata=pFrame->data[2];
            assert(ydata);
            assert(udata);
            assert(vdata);            
            int ylinesize=pFrame->linesize[0]; // 768
            int ulinesize=pFrame->linesize[1]; // 384
            int vlinesize=pFrame->linesize[2]; // 384
            int w=pFrame->width;
            int h=pFrame->height;
            int g_frame_num=pCodecContext->frame_number;

            //            static unsigned char outbuf[g_scrw*g_scrh*3];
            unsigned char *outptrs[1]= { g_picture_data };
            int outlinesizes[1] = { g_scrw*4 };

            int swsret=sws_scale(g_swsctx, pFrame->data, pFrame->linesize, 0, pFrame->height, outptrs, outlinesizes );
            if(swsret<=0) print("swsret:%d",swsret);
            
            //                        print("LSZ:%d %d %d w:%d h:%d", ylinesize, ulinesize, vlinesize, w, h);
#if 0            
            if(h>g_scrh)h=g_scrh;
            if(w>g_scrw)w=g_scrw;

            size_t copysz = w*h*1;
            for(int y=0;y<h;y++) {
                for(int x=0;x<w;x++) {
                    int ind = (x+y*w)*4;
                    //                    g_picture_data[ind]=ydata[x+y*ylinesize];
                    //                    g_picture_data[ind+1]=ydata[x+y*ylinesize];
                    //                    g_picture_data[ind+2]=ydata[x+y*ylinesize];
                    g_picture_data[ind+0]=outbuf[(x+y*g_scrw)*3];
                    g_picture_data[ind+1]=outbuf[(x+y*g_scrw)*3+1];
                    g_picture_data[ind+2]=outbuf[(x+y*g_scrw)*3+2];
                    g_picture_data[ind+3]=0xff;
                }
            }
#endif            
        }
    }
    return 0;
}


void glfw_error_cb( int code, const char *desc ) {
    print("glfw_error_cb. code:%d desc:'%s'", code, desc );
}
void fbsizeCallback( GLFWwindow *window, int w, int h ) {
    print("fbsizeCallback: %d,%d",w,h);
}

void keyboardCallback( GLFWwindow *window, int key, int scancode, int action, int mods ) {
    if(action && key == 'Q') {
        g_game_done=true;
    }
}

void *receiveRTMPThreadFunc(void *urlarg) {
    char *url=(char*)urlarg;
    print("receiveRTMPThreadFunc: url:'%s'",url);
    av_register_all();
    avformat_network_init();
    
    AVFormatContext *pFormatContext = avformat_alloc_context();
    fprintf(stderr,"avformat_alloc_context: ret: %p\n",pFormatContext);
    if(!pFormatContext) {
        fprintf(stderr,"fatal\n");
        return 0;
    }


    if(avformat_open_input(&pFormatContext, url, NULL, NULL) != 0) { // blocks if no stream live
        fprintf(stderr,"avformat_open_input failed\n");
        return 0;
    }
    fprintf(stderr,"avformat_open_input ok\n");

    if(avformat_find_stream_info(pFormatContext,  NULL) < 0) {
        printf("avformat_find_stream_info failed");
        return 0;
    }
    print("avformat_find_stream_info ok"); 

    av_dump_format(pFormatContext,0,url,0);

    /////
    g_swsctx=sws_getContext( g_scrw, g_scrh, AV_PIX_FMT_YUV420P,
                             g_scrw, g_scrh, AV_PIX_FMT_RGB32,
                             SWS_BICUBIC,
                             NULL,NULL,NULL);
    if(!g_swsctx) {
        print("sws_getContext failed");
        return 0;
    }

    /////
    
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
            return 0;
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
        return 0;
    }


    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
        fprintf(stderr,"failed to copy codec params to codec context\n");
        return 0;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        fprintf(stderr,"failed to open codec through avcodec_open2\n");
        return 0;
    }

    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame) {
        fprintf(stderr,"failed to allocated memory for AVFrame\n");
        return 0;
    }
    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket) {
        fprintf(stderr,"failed to allocated memory for AVPacket\n");
        return 0;
    }

    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        // if it's the video stream
        if (pPacket->stream_index == videoindex) {
            //            fprintf(stderr,"AVPacket->pts %" PRId64 "\n", pPacket->pts);
            int response = decode_packet(pPacket, pCodecContext, pFrame);
            if (response < 0) break;
            // stop it, otherwise we'll be saving hundreds of frames
        }
        av_packet_unref(pPacket);
    }

    fprintf(stderr,"releasing all the resources\n");
    avformat_close_input(&pFormatContext);
    avformat_free_context(pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
    return 0;
}



void updateImage() {
    g_img->copyFromBuffer(g_picture_data,g_scrw,g_scrh, g_scrw*g_scrh*4);
    g_tex->setImage(g_img);
}


// ./player 127.0.0.1 appname streamname
int main( int argc, char **argv ) {
    char *url=argv[1];
    if(!url) {
        fprintf(stderr,"need URL\n");
        return 1;
    }

    /////////////
    if(!glfwInit()) {
        print("can't init glfw");
        return 1;
    }
    glfwSetErrorCallback( glfw_error_cb );

    
    g_win=glfwCreateWindow(g_scrw,g_scrh,"player",NULL,NULL);
    if(!g_win) {
        print("cant open glfw window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);
    glClearColor(0,0,0,1);

    glfwSetFramebufferSizeCallback(g_win, fbsizeCallback);
    glfwSetKeyCallback( g_win, keyboardCallback );

    
    g_moyai_client = new MoyaiClient(g_win,g_scrw,g_scrh);
    g_vp=new Viewport();
    g_vp->setSize(g_scrw*RETINA,g_scrh*RETINA);
    g_vp->setScale2D(g_scrw,g_scrh);

    g_layer=new Layer();
    g_layer->setViewport(g_vp);
    g_moyai_client->insertLayer(g_layer);
    

    g_img=new Image();
    g_img->setSize(g_scrw,g_scrh);
    g_img->drawLine(0,0,g_scrw,g_scrh,Color(1,0,0,1));
    g_tex=new Texture();
    g_tex->setImage(g_img);
    g_prop=new Prop2D();
    g_prop->setTexture(g_tex);
    g_prop->setScl(g_scrw,g_scrh);
    g_prop->setLoc(0,0);
    g_layer->insertProp(g_prop);

    
    /////////////

    pthread_t tid;
    int err=pthread_create(&tid,NULL,receiveRTMPThreadFunc, url);
    if(err) {
        print("pthread_create failed. ret:%d",err);
        return 1;
    }

    int frm=0,totfrm=0;
    static double last_nt=now();
    static double last_print_time;
    while(!g_game_done) {
        double nt=now();
        double dt=nt-last_nt;
        if(last_print_time<nt-1) {
            last_print_time=nt;
            print("FPS:%d(%d) recvfrm:%d diff_frm:%d",frm, totfrm, g_frame_num, g_frame_num - totfrm);
            frm=0;
        }
        frm++;
        totfrm++;
        
        glfwPollEvents();
        updateImage();
        g_moyai_client->render();
        last_nt=nt;
    }
    glfwTerminate();

}
