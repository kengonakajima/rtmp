#include "librtmp/rtmp.h"
#include "librtmp/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main( int argc, char **argv ) {
    if(argc<3){
        RTMP_LogPrintf("need IP APP PATH args. Ex: 127.0.0.1 app live");
        return 1;
    }
    char *iparg=argv[1];
    char *apparg=argv[2];
    char *playpatharg=argv[3];
    
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

    while(1) {
        usleep(50*1000);
        fprintf(stderr,".");
        unsigned int nt=RTMP_GetTime();
        size_t bufsize = 64*1024;
        char *buffer = (char*)malloc(bufsize);
        int nr=RTMP_Read(&rtmp,buffer,bufsize);
        fprintf(stderr, "time:%u read:%d\n",nt,nr);
    }
    
}
