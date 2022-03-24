#include "rtmp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

#include "debug.h"

using namespace std;

void func_client_run(void *instance, void *arg);

#define static_dbg_file         "/tmp/rtmp_client.log"
const char *rtmcl_dbg_file = static_dbg_file;

#define ADTS_HEADER_SIZE 7

/**
 * 包含系统调用，慎用，特别是高频处理逻辑（且实时性要求高）
*/
void rtmclDbgSys(const char *str)
{
    char buf[3000] = {0};
    strcat(buf, "echo \"");
    strcat(buf, str);
    strcat(buf, "\" >> ");
    strcat(buf, rtmcl_dbg_file);
    system(buf);
}


// static std::thread rtmp_client::t0;
std::atomic_int threads_inited(0);
static std::thread client_manage;
std::vector<std::thread> client_threads;

class rtmp_client
{
public:
    RtmpClientCallback *client_cb;
    string client_url;
    void *client_user_data;
    std::atomic_int client_runing;
    std::thread t0;
    int open_timeout;
    int recv_timeout;

private:

public:
    rtmp_client(const char *url, void *user_data, int otm, int rtm, RtmpClientCallback *cb);
    ~rtmp_client();

    void rtmpclient_stop() {client_runing--;};

};

rtmp_client::rtmp_client(const char *url, void *user_data, int otm, int rtm, RtmpClientCallback *cb):
client_runing(0), open_timeout(10), recv_timeout(3)
{
    client_url = url;
    client_cb = new RtmpClientCallback;
    if (client_cb)
    {
        client_cb->onFinish = cb ? cb->onFinish : NULL;
        client_cb->onRecv = cb ? cb->onRecv : NULL;
        client_cb->onDebug = cb ? cb->onDebug : NULL;
    }
    client_user_data = user_data;
    if (otm > 0) {
        open_timeout = otm;
    }
    if (rtm > 0) {
        recv_timeout = rtm;
    }

    t0 = std::thread(func_client_run, this, client_user_data);
}

rtmp_client::~rtmp_client()
{
    client_url = "";
    if (client_cb)
    {
        client_cb->onFinish = NULL;
        client_cb->onRecv = NULL;
        client_cb->onDebug = NULL;
        delete client_cb;
    }
    
    client_user_data = NULL;
}

void rtmclDbg(rtmp_client *ins, const char *format, ...)
{
    if (ins && ins->client_cb && ins->client_cb->onDebug)
    {
        int len;
        char str[1024] = {0};
        va_list ap, ap2;

        va_start(ap, format);
        va_copy(ap2, ap);
        /* first try */
        len = vsnprintf(str, 1024, format, ap);
        va_end(ap);
        if (len >= (int) 1024) {
            /* buffer wasn't big enough */
            char *tmpstr = malloc(len + 1);
            vsnprintf(tmpstr, len+1, format, ap2);
            snprintf(str, sizeof(str)-10, "%s...", tmpstr);
            // strncpy(str, sizeof(str) - 10, tmpstr);
            free(tmpstr);
        }
        va_end(ap2);

        ins->client_cb->onDebug(str);
    }
    
}

//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/dict.h>
//#include <libavcodec/bsf.h>
#include <libavcodec/adts_parser.h>
#include <libavutil/log.h>
#include <libavformat/avio.h>
#ifdef __cplusplus
}
#endif

// ^
typedef struct  _ADTSContext_
{//undefined
    int write_adts;
    int objecttype;
    int sample_rate_index;
    int channel_conf;
} ADTSContext;

// ^
int aac_decode_extradata(ADTSContext *adts, unsigned char *pbuf, int bufsize)  
{  
      int aot, aotext, samfreindex;  
      int i, channelconfig;  
      unsigned char *p = pbuf;  
      if (!adts || !pbuf || bufsize<2)  
      {  
            return -1;  
      }  
      aot = (p[0]>>3)&0x1f;  
      if (aot == 31)  
      {  
            aotext = (p[0]<<3 | (p[1]>>5)) & 0x3f;  
            aot = 32 + aotext;  
            samfreindex = (p[1]>>1) & 0x0f;   
            if (samfreindex == 0x0f)  
            {  
                channelconfig = ((p[4]<<3) | (p[5]>>5)) & 0x0f;  
            }  
            else  
            {  
                channelconfig = ((p[1]<<3)|(p[2]>>5)) & 0x0f;  
            }  
      }  
      else  
      {  
            samfreindex = ((p[0]<<1)|p[1]>>7) & 0x0f;  
            if (samfreindex == 0x0f)  
            {  
                channelconfig = (p[4]>>3) & 0x0f;  
            }  
            else  
            {  
                channelconfig = (p[1]>>3) & 0x0f;  
            }  
      }  
#ifdef AOT_PROFILE_CTRL  
      if (aot < 2) aot = 2;  
#endif  
      adts->objecttype = aot-1;  
      adts->sample_rate_index = samfreindex;  
      adts->channel_conf = channelconfig;  
      adts->write_adts = 1;  
      return 0;  
}
// ^
int aac_set_adts_head(ADTSContext *acfg, unsigned char *buf, int size)  
{

    unsigned char byte;    
    if (size < ADTS_HEADER_SIZE)  
    {
        return -1;  
    }       
    buf[0] = 0xff;  
    buf[1] = 0xf1;  
    byte = 0;  
    byte |= (acfg->objecttype & 0x03) << 6;  
    byte |= (acfg->sample_rate_index & 0x0f) << 2;  
    byte |= (acfg->channel_conf & 0x07) >> 2;  
    buf[2] = byte;  
    byte = 0;  
    byte |= (acfg->channel_conf & 0x07) << 6;  
    byte |= (ADTS_HEADER_SIZE + size) >> 11;  
    buf[3] = byte;  
    byte = 0;  
    byte |= (ADTS_HEADER_SIZE + size) >> 3;  
    buf[4] = byte;  
    byte = 0;  
    byte |= ((ADTS_HEADER_SIZE + size) & 0x7) << 5;  
    byte |= (0x7ff >> 6) & 0x1f;  
    buf[5] = byte;  
    byte = 0;  
    byte |= (0x7ff & 0x3f) << 2;  
    buf[6] = byte;     
    return 0;  
}

typedef struct _client_contex_ {
    AVFormatContext *ifmt_ctx;
    int64_t last_time;
    int64_t time_out;
} RWDataContext;

static int interruptCallback(void *context)
{
    RWDataContext *ctx = (RWDataContext *)context;
    if (ctx == NULL || (ctx->time_out == 0)) {
        FFRTMP_LOG(LOG_INFO, "[ffclient]Recv data interrupt, but ctx null!\n");
        return 0;
    }
    int64_t end =  time(NULL);

    if (ctx->last_time == 0) {
        FFRTMP_LOG(LOG_INFO, "[ffclient]now %lld, last %lld!\n", end, ctx->last_time);
        ctx->last_time = end;
        return 0;
    }

    if ((ctx->last_time > 0) && end - ctx->last_time >= ctx->time_out) {
        FFRTMP_LOG(LOG_INFO, "[ffclient]Recv data timeout!\n");
        return 1;
    }

    return 0;
}

int adts_header(unsigned char *buf, int size, int type, int rate, int channels)
{         
    unsigned char byte;    
    if (size < ADTS_HEADER_SIZE)  
    {
        return -1;  
    }       
    buf[0] = 0xff;  
    buf[1] = 0xf1;  
    byte = 0;  
    byte |= (type & 0x03) << 6;  
    byte |= (rate & 0x0f) << 2;  
    byte |= (channels & 0x07) >> 2;  
    buf[2] = byte;  
    byte = 0;  
    byte |= (channels & 0x07) << 6;  
    byte |= (ADTS_HEADER_SIZE + size) >> 11;  
    buf[3] = byte;  
    byte = 0;  
    byte |= (ADTS_HEADER_SIZE + size) >> 3;  
    buf[4] = byte;  
    byte = 0;  
    byte |= ((ADTS_HEADER_SIZE + size) & 0x7) << 5;  
    byte |= (0x7ff >> 6) & 0x1f;  
    buf[5] = byte;  
    byte = 0;  
    byte |= (0x7ff & 0x3f) << 2;  
    buf[6] = byte;     
    return 0;  
}

int start_pull(rtmp_client *this0, void *user_data, const char *url)
{
    RWDataContext *contex;
    AVFormatContext *ifmt_ctx = NULL;
    AVPacket *pkt;
    const char *in_filename = NULL;
    int ret, i;
    int videoindex=-1, audioindex=-1;
    int need_to_annexb = 0;
    int status = 0;
    RtmpClientCallback *cb = this0 ? this0->client_cb : NULL;
    // 定义音频基本参数 profile 选择和通道数 采样率
    int aac_type = 0;//fmt_ctx->streams[1]->codecpar->profile;
    int channels = 0;//fmt_ctx->streams[1]->codecpar->channels;
    int sample_rate= 0;//fmt_ctx->streams[1]->codecpar->sample_rate;
    AVDictionary* opts = NULL;

    in_filename  = url;
    
    // av_register_all();
    //Network
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]avformat_network_init ...\n", this0);
    if (avformat_network_init() < 0 ) {
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to init ffmpeg network!\n");
        return stacode_network_failed;
    }
    ifmt_ctx = avformat_alloc_context();
    if (ifmt_ctx == NULL) {
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to init ffmpeg alloc!\n");
        return stacode_ffmpeg_alloc;
    }
    // 读数据中断回调
    contex = new RWDataContext;
    if (contex == NULL) {
        status = stacode_syscall_failed;
        FFRTMP_LOG(LOG_ERR, "[ffclient]memery less!\n");
        goto end;
    }
    if (contex) {
        contex->ifmt_ctx = ifmt_ctx;
        contex->time_out = this0->open_timeout;
        ifmt_ctx->interrupt_callback.opaque = (void*)contex;
        ifmt_ctx->interrupt_callback.callback = interruptCallback;//设置回调
    }

    //Input
    // av_dict_set(&opts, "stimeout", "3000000", 0);//单位 如果是http:ms  如果是udp:s // 测试过的是 3s
    // av_dict_set(&opts, "rw_timeout", "10000", 0);//单位:ms
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]avformat_open_input:ctx %p, timeout %d!\n", this0, ifmt_ctx, contex->time_out);
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &opts)) < 0) {
        FFRTMP_LOG(LOG_ERR, "[ffclient] Could't connect: %s!\n", url);
        return stacode_ffmpeg_connect;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        status = stacode_ffmpeg_failed;
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to retrieve input stream information\n");
        goto end;
    }

    // 
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]Which audio and video index by %p...\n", this0, ifmt_ctx);
    for(i=0; i<ifmt_ctx->nb_streams; i++) {
        if(ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            videoindex=i;
        }
        else if (ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioindex=i;
            aac_type = ifmt_ctx->streams[i]->codecpar->profile;
            channels = ifmt_ctx->streams[i]->codecpar->channels;
            sample_rate= ifmt_ctx->streams[i]->codecpar->sample_rate;
        }
    }

    FFRTMP_LOG(LOG_DBG, "[ffclient]Got vindex %d, aindex %d", videoindex, audioindex);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got aac_type %d, channels %d, rate %d", aac_type, channels, sample_rate);

    av_dump_format(ifmt_ctx, 0, in_filename, 0);
    
    //分配packet
    pkt = av_packet_alloc();
    av_init_packet(pkt);
    need_to_annexb = 1;

    contex->time_out = this0->recv_timeout;
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]Streaming start recv: time out %d...\n", this0, contex->time_out);

    //拉流
    while (this0->client_runing)
    {
        AVStream *in_stream, *out_stream;
        //Get an AVPacket
        contex->last_time = time(NULL);
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0)
            break;
        if (ret > 0) {
            FFRTMP_LOG(LOG_DBG, "[ffclient]Maybe Stream tiemout!\n");
            break;
        }
        if (ifmt_ctx->pb && ifmt_ctx->pb->eof_reached) {
            FFRTMP_LOG(LOG_DBG, "[ffclient]Stream disconnected!\n");
            break;
        }

        //读出的帧判断是否是视频帧
        if(ret == 0 && pkt->stream_index == videoindex) {
            //是否需要使用h264_mp4toannexb转换
            if(need_to_annexb) {
                //获取比特流过滤器(h264_mp4toannexb)
                const AVBitStreamFilter* bsfilter = av_bsf_get_by_name("h264_mp4toannexb");
                AVBSFContext *bsf_ctx = NULL;
                //申请过滤器上下文
                av_bsf_alloc(bsfilter, &bsf_ctx);
                //从视频流中拷贝编解决码器参数
                avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[videoindex]->codecpar);
                //初始化过滤器上下文
                av_bsf_init(bsf_ctx);

                int input_size = pkt->size;

                //记录下是否send 一个packet，receive 一个packet。基本都是这个情况的
                //有比较少情况出现会send 一个packet，receive 几个packet
                //（SPS、PPS、I帧在一个packet send,receive 多个packet）。
                int out_pkt_count = 0;

                if(av_bsf_send_packet(bsf_ctx, pkt) != 0) {
                    //不管是否成功，都要释放packet，因为bitstreamfilter内部还有引用这个内存空间的
                    av_packet_unref(pkt);
                    continue;
                }
                //不管是否成功，都要释放packet，因为bitstreamfilter内部还有引用这个内存空间的
                av_packet_unref(pkt);

                while (av_bsf_receive_packet(bsf_ctx , pkt) == 0) {
                    out_pkt_count++;
                    if (cb && cb->onRecv) {
                        cb->onRecv((void *)this0, user_data, pkt->data, pkt->size, 1);
                    }
                    av_packet_unref(pkt);
                }

                //send 一个packet ,receive pakcet 超过2个就输出提示信息
                if(out_pkt_count >= 2)
                {
                    FFRTMP_LOG(LOG_DBG, "one send packet size = %d, receive %d packet.\n", input_size,
                        out_pkt_count);
                }
                if(bsf_ctx) {
                    av_bsf_free(&bsf_ctx);
                }
            }
            else
            {
                if (cb && cb->onRecv) {
                    cb->onRecv((void *)this0, user_data, pkt->data, pkt->size, 1);
                }
                
                av_packet_unref(pkt);
            }
        }
        else if(ret == 0 && pkt->stream_index == audioindex)
        {
            unsigned char adts_header_buf[7];
            adts_header(adts_header_buf, pkt->size, aac_type, sample_rate, channels);

            if (cb && cb->onRecv) {
                cb->onRecv((void *)this0, user_data, adts_header_buf, 7, 0);
                cb->onRecv((void *)this0, user_data, pkt->data, pkt->size, 0);
            }
           
            av_packet_unref(pkt);
        }
    }

    if (ret < 0) {
        FFRTMP_LOG(LOG_DBG, "[ffclient]Finishing error %lld\n", ret);
        status = stacode_ffmpeg_failed;
    }
    if (ret > 0) {
        FFRTMP_LOG(LOG_DBG, "[ffclient]Finishing timeout %lld\n", ret);
        status = stacode_stream_timeout;
    }
    
    end:
    avformat_close_input(&ifmt_ctx);
    
    return status;
}

void func_client_run(void *instance, void *arg)
{
    rtmp_client *this0 = (rtmp_client *)instance;
    void *user_data = arg;
    RtmpClientCallback *cb = this0 ? this0->client_cb : NULL;

    FFRTMP_LOG(LOG_DBG, "[ffclient] %s start!\n", this0->client_url.c_str());

    if (this0 == NULL || cb == NULL)
    {
        FFRTMP_LOG(LOG_ERR, "[ffclient]Cient run with null arg!\n");
        return;
    }

    if (this0->client_url.empty()) {
        if (cb->onFinish) {
            cb->onFinish((void *)this0, (void *)this0->client_user_data, stacode_url_empty);
        }
        return ;
    }
    this0->client_runing++;

    int status = start_pull(this0, (void *)this0->client_user_data, this0->client_url.c_str());

    if (cb->onFinish) {
        cb->onFinish((void *)this0, (void *)this0->client_user_data, 0);
    }

    FFRTMP_LOG(LOG_DBG, "[ffclient] %s stop!\n", this0->client_url.c_str());

    // client_threads.emplace_back(std::move(this0->t0));
}

void func_client_manage()
{
    while(threads_inited)
    {
        sleep(3);
        for(auto & th:client_threads)
            th.join();
        client_threads.clear();
    }
}

/**
 * **********************************************************
 *                     rtmp client api
 * **********************************************************
*/
extern "C" {

void *rtmp_client_start(void *user_data, const char *url, int open_timeout, int recv_timeout,
    RtmpClientCallback *cb)
{
    rtmp_client *ins = NULL;

    ins = new rtmp_client(url, user_data, open_timeout, recv_timeout, cb);

    if (!ins)
    {
        FFRTMP_LOG(LOG_ERR, "[ffclient]start rtmp failed!\n");
    }
    else if (0 == threads_inited++)
    {
        //
        ffrtmp_log_init(0, 0, static_dbg_file);

        rtmclDbg(ins, "[ffclient]Initialize ...\n");
        FFRTMP_LOG(LOG_DBG, "[ffclient]Initialize ...\n");
        // client_manage = thread(func_client_manage);
    }
    else
    {
        threads_inited--;
    }
    rtmclDbg(ins, "[ffclient]new rtmp client: %s %s!\n", url, ins ? "success" : "failed");
    FFRTMP_LOG(LOG_DBG, "[ffclient]new rtmp client: %s %s!\n", url, ins ? "success" : "failed");

    return (void *)ins;
}

void rtmp_client_stop(void *contex)
{
    rtmp_client *client = (rtmp_client *)contex;

    rtmclDbg(client, "[ffclient]stop rtmp client: %s!\n", client ? client->client_url.c_str() : "-?-");
    FFRTMP_LOG(LOG_DBG, "[ffclient]stop rtmp client: %s!\n", client ? client->client_url.c_str() : "-?-");
    if (client)
    {
        client->rtmpclient_stop();
    }
    
    return;
}

void TestDbg(int level, const char *format, ...)
{
    do {
        int len;
        char str[3196] = {0};
        va_list ap, ap2;

        va_start(ap, format);
        va_copy(ap2, ap);
        /* first try */
        len = vsnprintf(str, 3196, format, ap);
        va_end(ap);
        if (len >= (int) 3196) {
            /* buffer wasn't big enough */
            char *tmpstr = malloc(len + 1);
            vsnprintf(tmpstr, len+1, format, ap2);
            snprintf(str, sizeof(str)-10, "%s...\n", tmpstr);
            // strncpy(str, sizeof(str) - 10, tmpstr);
            free(tmpstr);
        }
        va_end(ap2);

       FFRTMP_LOG(LOG_DBG, "%s", str);
       if (level < 3) printf("%s", str);
    } while(0);

    return 0;
}

} // extern "C" {} Eend!
