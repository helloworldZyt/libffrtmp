/**
 * rtmp_client.h
 * */
#ifndef __RTMP_CLIENT_H_
#define __RTMP_CLIENT_H_

#include <stdarg.h>

#define TEST_DBG   1

#define RTMPCAPI    extern

typedef enum _rtmp_client_statu_code_ {
    stacode_ffmpeg_alloc=-5,
    stacode_network_failed=-4,
    stacode_ffmpeg_connect=-3,
    stacode_ffmpeg_failed=-2,
    stacode_syscall_failed=-1,  // malloc、new、socket etc.
    stacode_success=0,
    stacode_param_wrong=1,
    stacode_url_empty=2,        // url为空
    stacode_stream_timeout=3,   // 连接或者接收data超时
} RtmpClientCode;

typedef int (*RtmpClientOnRecv)(void *contex, void *user_data, unsigned char *buffer, int length, int video);
typedef int (*RtmpClientOnFinish)(void *contex, void *user_data, int status);
typedef int (*RtmpClientOnDebug)(const char *buffer);

typedef struct rtmp_client_callback_st
{
    RtmpClientOnRecv onRecv;            // 接收数据回调
    RtmpClientOnFinish onFinish;        // 异常或正常结束，回调此函数
    RtmpClientOnDebug onDebug;            // 输出日志
} RtmpClientCallback;

extern "C" {

/**
 * @描述： 
 * @user_data    : 用户私有数据，回调中返给用户
 * @url          : rtmp://host:port/app/streamname
 * @open_timeout : 连接url超时时间
 * @recv_timeout : 接收数据超时时间
 * @cb           : 用户回调函数
 * @retrun       : 'contex' need by rtmp_client_stop
*/
RTMPCAPI void *rtmp_client_start(void *user_data, const char *url, int open_timeout, int recv_timeout,
    RtmpClientCallback *cb);

/**
 * @contex      : return by 'rtmp_client_start'
 * @retrun      : void
*/
RTMPCAPI void rtmp_client_stop(void *contex);



RTMPCAPI void TestDbg(int level, const char *format, ...);

}

#endif // __RTMP_CLIENT_H_