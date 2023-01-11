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
    std::atomic_int client_stoped;
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
client_runing(0), open_timeout(10), recv_timeout(3), client_stoped(0)
{
    client_url = url;
    client_cb = new RtmpClientCallback;
    if (client_cb)
    {
        client_cb->onFinish = cb ? cb->onFinish : NULL;
        client_cb->onRecv = cb ? cb->onRecv : NULL;
        client_cb->onAudioReport = cb ? cb->onAudioReport : NULL;
        client_cb->onVideoReport = cb ? cb->onVideoReport : NULL;
        client_cb->onStreamReport = cb ? cb->onStreamReport : NULL;
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
    t0.join();
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

const char *rtmp_codec_name(int codec_id)
{
    switch (codec_id)
    {
    // audio
    case AV_CODEC_ID_AAC: { return "AAC"; break; }
    case AV_CODEC_ID_OPUS: { return "OPUS"; break; }

    // video
    case AV_CODEC_ID_H264: { return "H264"; break; }

    default: { return ""; break; }
    }
}

int walker_running(rtmp_client *this0, void *user_data, const char *url)
{
    RWDataContext *contex;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket *pkt;
    const char *in_filename = NULL;
    int64_t ret, i;
    int videoindex=-1, audioindex=-1;
    int need_to_annexb = 0, status = 0;
    RtmpClientCallback *cb = this0 ? this0->client_cb : NULL;
    // 定义音频基本参数 profile 选择和通道数 采样率
    int audio_changed = 0, video_changed = 0, stream_changed = 0;
    int aac_type = 0, channels = 0, sample_rate= 0; // audio only
    int video_type = 0, width = 0, height = 0;   // video only
    // video: the pixel format, enum AVPixelFormat. AV_PIX_FMT_YUV420P=0
    // audio: the sample format, enum AVSampleFormat. AV_SAMPLE_FMT_S32P=8
    int vcodec_format = 0, acodec_format = 0;
    int vbit_rate = 0, abit_rate = 0;
    int vcodec_id = 0, acodec_id = 0;
    int v_frame_rate = 0, a_frame_rate = 0;
    int v_frate_avg = 0;
    AVDictionary* opts = NULL;
    int need_flv = 0;
    const char *out_file = "/data/rtmptest.flv";

    in_filename  = url;
    
    // av_register_all();
    //Network
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]avformat_network_init ...\n", this0);
    if (avformat_network_init() < 0 ) {
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to init ffmpeg network!\n");
        return errcode_network_failed;
    }
    ifmt_ctx = avformat_alloc_context();
    if (ifmt_ctx == NULL) {
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to init ffmpeg alloc!\n");
        return errcode_ffmpeg_alloc;
    }
    // 读数据中断回调
    contex = new RWDataContext;
    if (contex == NULL) {
        status = errcode_syscall_failed;
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
        return errcode_ffmpeg_connect;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        status = errcode_ffmpeg_failed;
        FFRTMP_LOG(LOG_ERR, "[ffclient]Failed to retrieve input stream information\n");
        goto end;
    }

    // Read Rtream Info
    for(i=0; i<ifmt_ctx->nb_streams; i++) {
        if(ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            videoindex=i;
        }
        else if (ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioindex=i;
        }
    }
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]Which audio and video index by %p...\n", this0, ifmt_ctx);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got vindex %d, aindex %d\n", videoindex, audioindex);

    av_dump_format(ifmt_ctx, 0, in_filename, 0);
    
    //分配packet
    pkt = av_packet_alloc();
    av_init_packet(pkt);
    need_to_annexb = 1;

    contex->time_out = this0->recv_timeout;
    FFRTMP_LOG(LOG_DBG, "[ffclient][%p]Streaming start recv: time out %d...\n", this0, contex->time_out);

    for(i=0; i<ifmt_ctx->nb_streams; i++) {
        if(ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            video_type = ifmt_ctx->streams[i]->codecpar->profile;
            width = ifmt_ctx->streams[i]->codecpar->width;
            height = ifmt_ctx->streams[i]->codecpar->height;
            vbit_rate = ifmt_ctx->streams[i]->codecpar->bit_rate;
            vcodec_id = ifmt_ctx->streams[i]->codecpar->codec_id;
            if (ifmt_ctx->streams[i]->r_frame_rate.den)
                v_frame_rate = ifmt_ctx->streams[i]->r_frame_rate.num/ifmt_ctx->streams[i]->r_frame_rate.den;
            if (ifmt_ctx->streams[i]->avg_frame_rate.den)
                v_frate_avg = ifmt_ctx->streams[i]->avg_frame_rate.num/ifmt_ctx->streams[i]->avg_frame_rate.den;
            
            vcodec_format = ifmt_ctx->streams[i]->codecpar->format;
        }
        else if (ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
            aac_type = ifmt_ctx->streams[i]->codecpar->profile;
            channels = ifmt_ctx->streams[i]->codecpar->channels;
            sample_rate = ifmt_ctx->streams[i]->codecpar->sample_rate;
            abit_rate = ifmt_ctx->streams[i]->codecpar->bit_rate;
            acodec_id = ifmt_ctx->streams[i]->codecpar->codec_id;
            if (ifmt_ctx->streams[i]->r_frame_rate.den)
                a_frame_rate = ifmt_ctx->streams[i]->r_frame_rate.num/ifmt_ctx->streams[i]->r_frame_rate.den;
            acodec_format = ifmt_ctx->streams[i]->codecpar->format;
        }
    }
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got aac_type %d, channels %d, rate %d\n", aac_type, channels, sample_rate);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got acodec_id %x(%s:%s), acodec_format %d, frate %d\n",
        acodec_id, avcodec_get_name(acodec_id), rtmp_codec_name(acodec_id), acodec_format, a_frame_rate);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got type %d, width %d, height %d\n", video_type, width, height);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got vcodec_id %d(%s:%s), vcodec_format %d, frate %d\n",
        vcodec_id, avcodec_get_name(vcodec_id), rtmp_codec_name(vcodec_id), vcodec_format, v_frame_rate);
    FFRTMP_LOG(LOG_DBG, "[ffclient]Got abit_rate %d, vbit_rate %d\n", abit_rate, vbit_rate);
    if (cb && cb->onVideoReport) {
        cb->onVideoReport(user_data, video_type, width, height, vcodec_format,
            v_frame_rate, avcodec_get_name(vcodec_id));
    }
    if (cb && cb->onAudioReport) {
        cb->onAudioReport(user_data, aac_type, channels, sample_rate, acodec_format,
            a_frame_rate, avcodec_get_name(acodec_id));
    }
    if (cb && cb->onStreamReport) {
        cb->onStreamReport(user_data, vbit_rate, abit_rate);
    }
    need_flv = 1;
    if (need_flv)
    {
        avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_file); //RTMP
        if (!ofmt_ctx) {
            FFRTMP_LOG(LOG_ERR, "Could not create output context\n");
            ret = AVERROR_UNKNOWN;
            status = errcode_ffmpeg_failed;
            goto end;
        }
        const AVOutputFormat *ofmt = ofmt_ctx->oformat;
        for (i = 0; i < ifmt_ctx->nb_streams; i++) {
            //Create output AVStream according to input AVStream
            AVStream *in_stream = ifmt_ctx->streams[i];
            //AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
            const AVCodec *codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
            AVStream *out_stream = avformat_new_stream(ofmt_ctx , codec);

            if (!out_stream) {
                FFRTMP_LOG(LOG_ERR, "Failed allocating output stream\n");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            AVCodecContext *p_codec_ctx = (AVCodecContext *)avcodec_alloc_context3(codec);
            ret = avcodec_parameters_to_context(p_codec_ctx , (const AVCodecParameters *)in_stream->codecpar);

            //Copy the settings of AVCodecContext
            if (ret < 0) {
                FFRTMP_LOG(LOG_ERR, "Failed to copy context from input to output stream codec context\n");
                goto end;
            }
            p_codec_ctx->codec_tag = 0;
            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                p_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            ret = avcodec_parameters_from_context(out_stream->codecpar, p_codec_ctx);
            if(ret < 0){
                av_log(NULL , AV_LOG_ERROR , "eno:[%d] error to paramters codec paramter \n" , ret);
            }
        }

        //Dump Format------------------
        av_dump_format(ofmt_ctx, 0, out_file, 1);
        //Open output URL
        if (!(ofmt->flags & AVFMT_NOFILE)) {
            ret = avio_open(&ofmt_ctx->pb, out_file, AVIO_FLAG_WRITE);
            if (ret < 0) {
                FFRTMP_LOG(LOG_DBG, "Could not open output URL '%s'", out_file);
                goto end;
            }
        }
        //Write file header
        ret = avformat_write_header(ofmt_ctx, NULL);
        if (ret < 0) {
            FFRTMP_LOG(LOG_DBG, "Error occurred when opening output URL\n");
            goto end;
        }
    }

    //拉流
    while (this0->client_runing)
    {
        AVStream *in_stream = NULL, *out_stream = NULL;
        int frame_index = 0;
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

        if (stream_changed == 0) {
            for(i=0; i<ifmt_ctx->nb_streams; i++) {
                if(ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
                    stream_changed += (vbit_rate == ifmt_ctx->streams[i]->codecpar->bit_rate) ? 0 : 1;
                    vbit_rate = ifmt_ctx->streams[i]->codecpar->bit_rate;
                }
                else if (ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
                    stream_changed += (abit_rate == ifmt_ctx->streams[i]->codecpar->bit_rate) ? 0 : 1;
                    abit_rate = ifmt_ctx->streams[i]->codecpar->bit_rate;
                }
            }
        }
        if (stream_changed) {
            FFRTMP_LOG(LOG_DBG, "[ffclient]Stream vbit_rate %d, vcodec_id %d changed!\n", vbit_rate, vcodec_id);
            if (stream_changed && ret == 0) {
                cb->onStreamReport(user_data, vbit_rate, abit_rate);
            }
            stream_changed = 0;
        }

        if (need_flv) {
            in_stream  = ifmt_ctx->streams[pkt->stream_index];
            out_stream = ofmt_ctx->streams[pkt->stream_index];
            /* copy packet */
            //Convert PTS/DTS
            pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            //Print to Screen
            if(pkt->stream_index==videoindex) {
                FFRTMP_LOG(LOG_DBG, "Receive %8d video frames from input URL\n",frame_index);
                frame_index++;
            }
            ret = av_interleaved_write_frame(ofmt_ctx, pkt);
            if (ret < 0) {
                FFRTMP_LOG(LOG_DBG, "Error muxing packet\n");
                break;
            }

            av_packet_unref(pkt);

            continue;
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
        FFRTMP_LOG(LOG_DBG, "[ffclient]Finishing error %ld\n", ret);
        status = errcode_ffmpeg_failed;
    }
    if (ret > 0) {
        FFRTMP_LOG(LOG_DBG, "[ffclient]Finishing timeout %lld\n", ret);
        status = errcode_stream_timeout;
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
            cb->onFinish((void *)this0, (void *)this0->client_user_data, errcode_url_empty);
        }
        return ;
    }
    this0->client_runing++;

    int status = walker_running(this0, (void *)this0->client_user_data, this0->client_url.c_str());

    if (cb->onFinish) {
        cb->onFinish((void *)this0, (void *)this0->client_user_data, 0);
    }

    this0->client_stoped++;
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

        FFRTMP_LOG(LOG_DBG, "[ffclient]Initialize ...\n");
        // client_manage = thread(func_client_manage);
    }
    else
    {
        threads_inited--;
    }

    FFRTMP_LOG(LOG_DBG, "[ffclient]new rtmp client: %s %s!\n", url, ins ? "success" : "failed");

    return (void *)ins;
}

void rtmp_client_stop(void *contex)
{
    rtmp_client *client = (rtmp_client *)contex;

    FFRTMP_LOG(LOG_DBG, "[ffclient]stop rtmp client: %s!\n", client ? client->client_url.c_str() : "-?-");
    if (client)
    {
        if (client->client_cb && client->client_cb->onDebug) {
            client->client_cb->onDebug("client %p, ctx %p, stoped!\n", client->client_user_data, client);
        }
        client->rtmpclient_stop();
        while (client->client_stoped == 0) usleep(10000);
        delete client;
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
