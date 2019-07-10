#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h" // moyai

#include "OpenAL/al.h"
#include "OpenAL/alc.h"


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

SoundSystem *g_soundsystem;
const int ABUFNUM=4, ABUFLEN=1024; // bufnum増やすと遅れが増えるが、ずれてるだけかなあ
ALuint g_alsource;
ALuint g_albuffer[ABUFNUM];
int16_t g_pcmdata[ABUFNUM][ABUFLEN];

struct SwsContext *g_swsctx;

struct DecodedData {
    int pts; // 0 for unused
    void *data;
    size_t szb;
};

struct DecodedData g_picture_ring[100];
struct DecodedData g_sample_ring[100];
int g_picture_head=0;
int g_sample_head=0;

double g_first_picture_received_at=0;
int g_latest_picture_pts=0;
double g_first_sample_received_at=0;
int g_latest_sample_pts=0;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void setupRingbuffer() {
    for(int i=0;i<elementof(g_picture_ring);i++) {
        g_picture_ring[i].szb=g_scrw*g_scrh*4;
        g_picture_ring[i].data=malloc( g_picture_ring[i].szb );
    }
    for(int i=0;i<elementof(g_sample_ring);i++) {
        g_sample_ring[i].szb = sizeof(int16_t)*ABUFLEN;
        g_sample_ring[i].data=malloc( g_sample_ring[i].szb);
    }
}

void pushSampleToRing( int16_t *buf, size_t szb, int pts ) {
    int r=pthread_mutex_lock(&g_mutex);
    if(r!=0) {
        print("pushSampleToRing: pth lock fail");
        return;
    }
    int di=g_sample_head % elementof(g_sample_ring);
    g_sample_ring[di].pts = pts;
    g_sample_ring[di].szb = szb;
    memcpy( g_sample_ring[di].data, buf, szb );
    g_sample_head++;
    if(g_first_sample_received_at==0) g_first_sample_received_at=now();
    g_latest_sample_pts = pts;
    r=pthread_mutex_unlock(&g_mutex);
    if(r!=0) {
        print("pushSampleToRing: pth unlock fail");
    }
}
void pushPictureToRing( uint8_t *buf, size_t sz, int pts ) {
    int r=pthread_mutex_lock(&g_mutex);
    if(r!=0) {
        print("pushPictureToRing: pth lock fail");
        return;
    }
    int di=g_picture_head % elementof(g_picture_ring);            
    g_picture_ring[di].pts = pts;
    g_picture_ring[di].szb = sz;
    memcpy( g_picture_ring[di].data, buf, sz);
    g_picture_head++;

    if(g_first_picture_received_at==0) g_first_picture_received_at=now();
    g_latest_picture_pts = pts;
            
    r=pthread_mutex_unlock(&g_mutex);
    if(r!=0) {
        print("pushPictureToRing: pth unlock fail");
    }
}


static int decode_video_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(pCodecContext, pPacket);

    if (response < 0) {
        print("Error while sending a packet to the decoder: %s", av_err2str(response));
        return response;
    }

    while (response >= 0) {
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
        response = avcodec_receive_frame(pCodecContext, pFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            printf("Error while receiving a frame from the decoder: %s", av_err2str(response));
            return response;
        }

        if (response >= 0) {
#if 1            
            print("VFrame %d t:%c sz:%d pts:%d dts:%d dur:%d key_frame:%d [DTS %d] fmt:%d head:%d",
                  pCodecContext->frame_number,
                  av_get_picture_type_char(pFrame->pict_type),
                  pFrame->pkt_size,
                  (int)pFrame->pts, pPacket->dts, pPacket->duration,
                  pFrame->key_frame,
                  pFrame->coded_picture_number,
                  pFrame->format, // AV_PIX_FMT_YUV420P = 0
                  g_picture_head
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


            static uint8_t buf[g_scrw*g_scrh*4];            
            unsigned char *outptrs[1]= { buf };
            int outlinesizes[1] = { g_scrw*4 };

            int swsret=sws_scale(g_swsctx, pFrame->data, pFrame->linesize, 0, pFrame->height, outptrs, outlinesizes );
            if(swsret<=0) print("swsret:%d",swsret);

            pushPictureToRing(buf, sizeof(buf), pFrame->pts);
        }
    }
    return 0;
}
static int decode_audio_packet(AVPacket *pPacket, AVCodecContext *aCodecContext, AVFrame *pFrame ) {
    int response = avcodec_send_packet(aCodecContext,pPacket)    ;
    if(response<0) {
        print("avcodec_send_packet failed audio");
        return response;
    }
    while(response>=0) {
        response=avcodec_receive_frame(aCodecContext,pFrame);
        if( response == AVERROR(EAGAIN) || response == AVERROR_EOF ) {
            break;
        } else if( response < 0) {
            print("error receiving audio decoded data: %s", av_err2str(response));
            return response;
        }
        if(response>=0) {
            int chn= av_get_channel_layout_nb_channels(pFrame->channel_layout);
            float lmax=0, rmax=0;
            for(int i=0;i<pFrame->nb_samples;i++) {
                float lv=0,rv=0;
                if( pFrame->format==AV_SAMPLE_FMT_FLTP ) {
                    lv=((float*)pFrame->data[0])[i];
                    rv=((float*)pFrame->data[1])[i];
                } else {
                    print("invalid sample format:%s",av_get_sample_fmt_name((AVSampleFormat)pFrame->format));
                }
                if(lv>lmax)lmax=lv;
                if(rv>rmax)rmax=rv;
            }
            
#if 1
            char llvch,rlvch;
            if(lmax<0.02) llvch='.'; else if(lmax<0.04)llvch=':'; else llvch='*';
            if(rmax<0.02) rlvch='.'; else if(rmax<0.04)rlvch=':'; else rlvch='*';

            print("AFrame %d sz:%d ch:%d pts:%d dts:%d dur:%d fmt:%s nsmpl:%d llv:%c rlv:%c head:%d",
                  aCodecContext->frame_number, pFrame->pkt_size, chn,
                  pFrame->pts, pPacket->dts, pPacket->duration,
                  av_get_sample_fmt_name((AVSampleFormat)pFrame->format), // AV_SAMPLE_FMT_FLTP : OBS uses this
                  pFrame->nb_samples,
                  llvch,rlvch,
                  g_sample_head
                  );
#endif            
            int samplenum=pFrame->nb_samples;
            if(samplenum>ABUFLEN) samplenum=ABUFLEN;
            int16_t outbuf[ABUFLEN];
            
            for(int i=0;i<samplenum;i++) {
                float left_level=((float*)pFrame->data[0])[i]; // TODO: stereo
                int amp=3;
                outbuf[i]=left_level * 30000 * amp;
            }
            pushSampleToRing(outbuf,samplenum*sizeof(int16_t),pFrame->pts);
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

    /// listing codecs
    {
        AVCodec * codec=0;
        while( (codec = av_codec_next(codec)) ){
            // try to get an encoder from the system
            AVCodec *encoder = avcodec_find_encoder(codec->id);
            if (encoder){
                print("[%d] name:%s ln:%s type:%d ", encoder->id, encoder->name, encoder->long_name, encoder->type);
            }
        }
    }

    
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
                             g_scrw, g_scrh, AV_PIX_FMT_BGR32,
                             SWS_BICUBIC,
                             NULL,NULL,NULL);
    if(!g_swsctx) {
        print("sws_getContext failed");
        return 0;
    }


    // audio streaming
    g_soundsystem = new SoundSystem();


    alGenBuffers(ABUFNUM,g_albuffer);
    alGenSources(1,&g_alsource);

    for(int j=0;j<ABUFNUM;j++) {
        for(int i=0;i<ABUFLEN;i++) g_pcmdata[j][i] = (i%(50+j*2))*200;
    }
    for(int i=0;i<ABUFNUM;i++) {
        alBufferData(g_albuffer[i], AL_FORMAT_MONO16, g_pcmdata[i], ABUFLEN*sizeof(int16_t), 44100);
        alSourceQueueBuffers(g_alsource,1,&g_albuffer[i]);
    }
    alSourcePlay(g_alsource);
    
    /////
    AVCodec *aCodec = NULL;
    AVCodec *pCodec = NULL;
    AVCodecParameters *pCodecParameters =  NULL;
    
    int videoindex=-1, audioindex=-1;    
    for(int i=0;i<pFormatContext->nb_streams;i++) {
        AVCodecParameters *pLocalCodecParameters =  NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        fprintf(stderr,"AVStream->time_base before open coded %d/%d\n", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        fprintf(stderr,"AVStream->r_frame_rate before open coded %d/%d\n", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        fprintf(stderr,"AVStream->start_time %" PRId64 "\n", pFormatContext->streams[i]->start_time);
        fprintf(stderr,"AVStream->duration %" PRId64 "\n", pFormatContext->streams[i]->duration);
        fprintf(stderr,"finding the proper decoder (CODEC) codec_id:%d\n", pLocalCodecParameters->codec_id);
        AVCodec *pLocalCodec = NULL;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        if(!pLocalCodec) {
            fprintf(stderr,"pLocalCodec null\n");
            return 0;
        }
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if(videoindex==-1) {
                videoindex = i;
                print("videoindex:%d",videoindex);
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
                
            }
            fprintf(stderr,"Video Codec: resolution %d x %d\n", pLocalCodecParameters->width, pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            fprintf(stderr,"Audio Codec: %d channels, sample rate %d\n", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
            if(audioindex==-1) {
                audioindex = i;
                aCodec = avcodec_find_decoder(pFormatContext->streams[audioindex]->codec->codec_id);
                print("audioindex:%d, aCodec:%p",audioindex, aCodec);
            }
            
        }
        fprintf(stderr,"\tCodec %s ID %d bit_rate %lld\n", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext) {
        print("failed to allocated memory for AVCodecContext video");
        return 0;
    }
    AVCodecContext *aCodecContext = avcodec_alloc_context3(aCodec);
    if(!aCodecContext) {
        print("failed to allocated memory for AVCodecContext audio");
        return 0;
    }
    if(avcodec_copy_context(aCodecContext, pFormatContext->streams[audioindex]->codec) != 0 ) {
        print("cant copy audio codec context");
        return 0;
    }
    print("audio stream info: freq:%d channels:%d", aCodecContext->sample_rate, aCodecContext->channels);


    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
        fprintf(stderr,"failed to copy codec params to codec context\n");
        return 0;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        print("failed to open video codec through avcodec_open2");
        return 0;
    }
    if( avcodec_open2(aCodecContext, aCodec, NULL) < 0) {
        print("failed to open audio codec ")   ;
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
            int response = decode_video_packet(pPacket, pCodecContext, pFrame);
            if (response < 0) break;
            // stop it, otherwise we'll be saving hundreds of frames
        } else if( pPacket->stream_index == audioindex) {
            int response = decode_audio_packet(pPacket, aCodecContext, pFrame);
            if( response<0) break;
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


// play buffered picture and video synchronized
void updateVideo() {
    int r=pthread_mutex_lock(&g_mutex);
    if(r<0) {
        print("updateVideo: lock fail");
        return;
    }

    int mgn_time=700;
    double nt=now();
    double elt=nt-g_first_picture_received_at;
    int pts = (int)(elt * mgn_time);
    
    if(g_first_picture_received_at>0) {
        int play_pts = pts;
        if( play_pts < g_latest_picture_pts - mgn_time) play_pts = g_latest_picture_pts-mgn_time; //
        //        print("elt:%f timer-pts:%d stream-pts:%d play_pts:%d",elt,pts, g_latest_picture_pts, play_pts);        
        // headから+にスキャンして、play_ptsより新しいのがあったら、そのデータを描画して消す
        for(int di=0;di<elementof(g_picture_ring);di++) {
            int logical_ind = g_picture_head + di;
            int ind = logical_ind % elementof(g_picture_ring);
            DecodedData *ddp=&g_picture_ring[ind];
            //                print("logical_ind:%d ind:%d pts:%d",logical_ind,ind,ddp->pts);
            if(ddp->pts==0)continue;
            if( ddp->pts >= play_pts ) {
                //                print("vid ind:%d ddp->pts:%d play_pts:%d",ind, ddp->pts, play_pts);
                ddp->pts=0;
                g_img->copyFromBuffer((unsigned char*)ddp->data,g_scrw,g_scrh, g_scrw*g_scrh*4);
                g_tex->setImage(g_img);
                break;
            }
        }

        if(g_first_sample_received_at>0) {
            int play_pts = pts;
            if( play_pts < g_latest_sample_pts - mgn_time) play_pts = g_latest_sample_pts-mgn_time; //
            //            print("elt:%f timer-pts:%d stream-pts:%d play_pts:%d",elt,pts, g_latest_picture_pts, play_pts);
            
            // unqueue/queue AL data
            static int play_head=0;
            ALint proced;
            alGetSourcei(g_alsource, AL_BUFFERS_PROCESSED, &proced)        ;
            if(proced>0) {
                prt("al source queue proced:%d ",proced);
                for(int j=0;j<proced;j++) {
                    int bufind = play_head % ABUFNUM;
                    alSourceUnqueueBuffers(g_alsource,1,&g_albuffer[bufind]);

                    // retrieve new data from audio queue
                    for(int di=0;di<elementof(g_sample_ring);di++) {
                        int logical_ind = g_sample_head + di;
                        int ind = logical_ind % elementof(g_sample_ring);
                        DecodedData *ddp=&g_sample_ring[ind];
                        //                print("logical_ind:%d ind:%d pts:%d",logical_ind,ind,ddp->pts);
                        if(ddp->pts==0)continue;
                        if( ddp->pts >= play_pts ) {
                            print("smpl ind:%d ddp->pts:%d play_pts:%d diff:%d nt:%f",
                                  ind, ddp->pts, play_pts, ddp->pts - play_pts, now);
                            ddp->pts=0;
                            alBufferData(g_albuffer[bufind], AL_FORMAT_MONO16, ddp->data, ddp->szb,44100);
                            alSourceQueueBuffers(g_alsource, 1, &g_albuffer[bufind]);
                            play_head++;
                            break;
                        }
                    }
                }
            }
            ALint sst;
            alGetSourcei(g_alsource, AL_SOURCE_STATE, &sst);
            if(sst==AL_STOPPED) {
                alSourcePlay(g_alsource);
                print("play");
            }
                    

        }
    }
    r=pthread_mutex_unlock(&g_mutex);
    if(r<0) print("updateVideo: pthread_mutex_unlock fail");
}


// ./player 127.0.0.1 appname streamname
int main( int argc, char **argv ) {
    char *url=argv[1];
    if(!url) {
        fprintf(stderr,"need URL\n");
        return 1;
    }
    setupRingbuffer();

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
    
    
    /////////////

    pthread_t rtmp_tid;
    int err=pthread_create(&rtmp_tid,NULL,receiveRTMPThreadFunc, url);
    if(err) {
        print("pthread_create failed. ret:%d",err);
        return 1;
    }

    //////////////

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
        updateVideo();
        g_moyai_client->render();
        last_nt=nt;

    }
    glfwTerminate();

}
