#include "librtmp/rtmp.h"
#include "librtmp/log.h"
#include <stdlib.h>
#include <string.h>

int main( int argc, char **argv ) {
    if(argc<3){
        RTMP_LogPrintf("need url and app and playpath: rtmp://127.0.0.1/app/playpath app playpath");
        return 1;
    }
    char *urlarg=argv[1];
    char *apparg=argv[2];
    char *playpatharg=argv[3];
    
    RTMP_LogPrintf( "start. host:%s\n", argv[1]);
    RTMP rtmp={0};
    RTMP_Init(&rtmp);
    RTMP_debuglevel = RTMP_LOGDEBUG;
    AVal hostname={ argv[1], strlen(argv[1]) };
    AVal sockshost={ 0, 0 };
    AVal playpath={playpatharg,strlen(playpatharg)};
    AVal tcUrl={ urlarg, strlen(urlarg) };
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

        //      RTMP_SetupStream(&rtmp, protocol, &hostname, port, &sockshost, &playpath,
        //		       &tcUrl, &swfUrl, &pageUrl, &app, &auth, &swfHash, swfSize,
        //		       &flashVer, &subscribepath, &usherToken, dSeek, dStopOffset, bLiveStream, timeout); 
       
}
