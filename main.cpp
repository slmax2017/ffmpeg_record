#include <iostream>
#include <vector>
#include <algorithm>
#include <stdint.h>
using namespace std;

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/mem.h"    
#include "libavutil/imgutils.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libavutil/time.h"
#include "libavutil/audio_fifo.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}


typedef int stream_index;
int e = -1;
char buf[1024] = { 0 };
//#define in_file_video "desktop"             
#define in_file_video "video=screen-capture-recorder"             
#define in_file_audio "audio=virtual-audio-capturer"
#define out_file "output.mp4"
#define FRAME 14
//#define in_file_audio "cuc_ieschool.mp3"
//#define out_file "rtmp://127.0.0.1:1935/live"
#define FIFO_NOT_ENOUGH 500 << 5
#define FIFO_AGEIN 501 << 5 

#define  ptr_check(x) \
            do {\
                if (!x){\
                    printf("operator fail"); \
                    return -1; \
                }\
            }while(0) 

#define void_handle(x) \
            do { \
                if ((e = (x)) < 0) {\
                    memset(buf, 0, 1024); \
                    av_strerror(e, buf, 1024); \
                    printf("err msg = %s", buf); \
                    return -1; \
                } \
            }while(0) 

#define ret_handle(x, r) \
            do { \
                if ((r = (x)) < 0) {\
                    memset(buf, 0, 1024); \
                    av_strerror(r, buf, 1024); \
                    printf("err msg = %s", buf); \
                    return -1; \
                } \
            }while(0) 


int handle_audio_frame(AVAudioFifo *audio_fifo, AVFrame *audio_frame, AVCodecContext *audio_codec_ctx)
{
    if (av_audio_fifo_size(audio_fifo) < audio_codec_ctx->frame_size) {

        void_handle(av_audio_fifo_realloc(audio_fifo, av_audio_fifo_size(audio_fifo) + audio_frame->nb_samples));
        void_handle(av_audio_fifo_write(audio_fifo, (void**)audio_frame->extended_data, audio_frame->nb_samples));

        //往队列添加数据后,再次检查队列数据,如何还是小于frame_size,则返回
        if (av_audio_fifo_size(audio_fifo) < audio_codec_ctx->frame_size) return FIFO_NOT_ENOUGH;
    }

    //读取编码器能够编码的长度
    void_handle(av_audio_fifo_read(audio_fifo, (void**)audio_frame->data, audio_codec_ctx->frame_size));
    audio_frame->nb_samples = audio_codec_ctx->frame_size;

    //检查读取后的数据长度
    if (av_audio_fifo_size(audio_fifo) >= audio_codec_ctx->frame_size) return FIFO_AGEIN;

    return EXIT_SUCCESS;
}


int main()
{
    avdevice_register_all();
    AVFormatContext *in_vedio_ctx = NULL, *in_audio_ctx = NULL, *out_ctx = NULL;
    bool isVedio = true;
    AVStream *out_vedio_stream = NULL;
    AVStream *out_audio_stream = NULL;
    AVDictionary *encode_opt = NULL;
    AVRational time_base = { 1, FRAME };

    //创建输入输出上下文
    void_handle(avformat_open_input(&in_vedio_ctx, in_file_video, av_find_input_format("dshow"), NULL));
    void_handle(avformat_find_stream_info(in_vedio_ctx, NULL));
    void_handle(avformat_open_input(&in_audio_ctx, in_file_audio, av_find_input_format("dshow"), NULL));
    void_handle(avformat_find_stream_info(in_audio_ctx, NULL));
    void_handle(avformat_alloc_output_context2(&out_ctx, NULL, NULL, out_file));

    //查找流通道
    int in_vedio_stream_index = -1, in_audio_stream_index = -1;
    ret_handle(av_find_best_stream(in_vedio_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, -1), in_vedio_stream_index);
    ret_handle(av_find_best_stream(in_audio_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, -1), in_audio_stream_index);

    vector<stream_index> vec_streams;
    vec_streams.push_back(in_vedio_stream_index);
    vec_streams.push_back(in_audio_stream_index);

    //为输出上下文创建音视频流
    AVStream *in_vedio_stream = in_vedio_ctx->streams[in_vedio_stream_index];
    AVStream *in_audio_stream = in_audio_ctx->streams[in_audio_stream_index];

    //打开输入流的解码器,后面需要解码
    void_handle(avcodec_open2(in_vedio_stream->codec, avcodec_find_decoder(in_vedio_stream->codec->codec_id), NULL));
    void_handle(avcodec_open2(in_audio_stream->codec, avcodec_find_decoder(in_audio_stream->codec->codec_id), NULL));

    if (!in_audio_stream->codec->channel_layout) {
        in_audio_stream->codec->channel_layout = av_get_default_channel_layout(in_audio_stream->codec->channels);
    }

    for (auto si : vec_streams) {

        AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
        ptr_check(out_stream);

        //拷贝解码器上下文参数
        avcodec_parameters_to_context(out_stream->codec,
            isVedio ? in_vedio_stream->codecpar : in_audio_stream->codecpar);

        if (isVedio) {
            out_stream->codec->codec_id = out_ctx->oformat->video_codec;
            AVCodec *codec = avcodec_find_encoder(out_stream->codec->codec_id);
            out_stream->codec->pix_fmt = codec->pix_fmts[0];
            out_stream->codec->time_base = time_base;
            isVedio = false;
        } else {
            out_stream->codec->codec_id = out_ctx->oformat->audio_codec;
            AVCodec *codec = avcodec_find_encoder(out_stream->codec->codec_id);
            out_stream->codec->sample_fmt = codec->sample_fmts[0];
            out_stream->codec->channel_layout = AV_CH_LAYOUT_STEREO;
            out_stream->codec->channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
        }

        if (out_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
    }

    out_vedio_stream = out_ctx->streams[0];
    out_audio_stream = out_ctx->streams[1];

    if (out_ctx->oformat->video_codec == AV_CODEC_ID_H264) {
        out_ctx->streams[0]->codec->qmin = 25;
        out_ctx->streams[0]->codec->qmax = 50;
        out_ctx->streams[0]->codec->qcompress = 0;
        av_dict_set(&encode_opt, "preset", "superfast", 0);
        av_dict_set(&encode_opt, "tune", "film", 0);
    }

    //打开编码器
    for (int i = 0; i < out_ctx->nb_streams; ++i) {
        AVCodecContext *codeCtx = out_ctx->streams[i]->codec;
        void_handle(avcodec_open2(codeCtx, avcodec_find_encoder(codeCtx->codec_id), i == 0 ? &encode_opt : NULL));
    }

    //创建文件
    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        void_handle(avio_open(&out_ctx->pb, out_file, AVIO_FLAG_READ_WRITE));
    }

    //写文件头
    void_handle(avformat_write_header(out_ctx, NULL));

    int height = in_vedio_stream->codec->height;
    int wigth = in_vedio_stream->codec->width;
    AVFrame *frame = av_frame_alloc();

    //视频转换
    AVFrame *frameYUV = av_frame_alloc();
    uint8_t *data = (uint8_t*)av_malloc(av_image_get_buffer_size(out_vedio_stream->codec->pix_fmt, out_vedio_stream->codec->width, out_vedio_stream->codec->height, 1));
    void_handle(av_image_fill_arrays(frameYUV->data, frameYUV->linesize, data,
        out_vedio_stream->codec->pix_fmt, out_vedio_stream->codec->width, out_vedio_stream->codec->height, 1));
    struct SwsContext *img_convert_ctx = sws_getContext(wigth, height,
                                                        in_vedio_stream->codec->pix_fmt,
                                                        wigth, height,
                                                        out_vedio_stream->codec->pix_fmt,
                                                        SWS_BICUBIC, NULL, NULL, NULL);

    //音频重采样
    AVFrame *frameAUDIO = av_frame_alloc();
    //frameAUDIO->channel_layout = out_audio_stream->codec->channel_layout;
    frameAUDIO->format = out_audio_stream->codec->sample_fmt;
    frameAUDIO->sample_rate = out_audio_stream->codec->sample_rate;
    frameAUDIO->nb_samples = out_audio_stream->codec->frame_size;
    void_handle(av_frame_get_buffer(frameAUDIO, 0));
    SwrContext *ado_convert_ctx = swr_alloc_set_opts(NULL,
                                                    out_audio_stream->codec->channel_layout,
                                                    out_audio_stream->codec->sample_fmt,
                                                    out_audio_stream->codec->sample_rate,
                                                    in_audio_stream->codec->channel_layout,
                                                    in_audio_stream->codec->sample_fmt,
                                                    in_audio_stream->codec->sample_rate,
                                                    0, NULL);

    ptr_check(ado_convert_ctx);
    void_handle(swr_init(ado_convert_ctx));
    AVAudioFifo *audio_fifo = av_audio_fifo_alloc(out_audio_stream->codec->sample_fmt, out_audio_stream->codec->channels, 1);
    ptr_check(audio_fifo);

    //写文件内容
    int64_t nowTime = av_gettime();
    AVPacket *packet_src = av_packet_alloc();
    AVPacket *packet_dst = av_packet_alloc();
    int64_t ts_a = 0, ts_b = 0, *ts_p = NULL;
    AVFormatContext *cur_ctx = NULL;
    AVCodecContext *out_cur_codec_ctx = NULL;
    AVStream *in_cur_stream = NULL;
    AVStream *out_cur_stream = NULL;
    AVFrame *cur_frame = NULL;
    int ret_src = 0;
    int ret_dst = 0;
    int ret = 0;                                         
    int vedio_encode_index = 0, audio_encode_index = 0;; 
    while (1) {                                          
        if (audio_encode_index >= 2000 || vedio_encode_index >= 500) break;
        if (av_compare_ts(ts_a, in_vedio_stream->time_base, ts_b, in_audio_stream->time_base) <= 0) {
            cur_ctx = in_vedio_ctx;
            in_cur_stream = in_vedio_stream;
            out_cur_stream = out_vedio_stream;
            out_cur_codec_ctx = out_vedio_stream->codec;
            cur_frame = frameYUV;
            ts_p = &ts_a;
            isVedio = true;
        } else {
            cur_ctx = in_audio_ctx;
            in_cur_stream = in_audio_stream;
            out_cur_stream = out_audio_stream;
            out_cur_codec_ctx = out_audio_stream->codec;
            cur_frame = frameAUDIO;
            ts_p = &ts_b;
            isVedio = false;
        }

        //读取一帧原始数据(屏幕或者声卡)
        if (av_read_frame(cur_ctx, packet_src) < 0) {
            memset(buf, 0, 1024);
            av_strerror(e, buf, 1024);
            printf("av_read_frame err = %s\n", buf);
            break;
        }

        ret_handle(avcodec_send_packet(in_cur_stream->codec, packet_src), ret_src);
        while (ret_src >= 0) {

            //解码原始帧,并转换
            ret_src = avcodec_receive_frame(in_cur_stream->codec, frame);
            if (ret_src == AVERROR(EAGAIN) || ret_src == AVERROR_EOF)  break;
            if (isVedio) void_handle(sws_scale(img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, height, cur_frame->data, cur_frame->linesize));
            else void_handle(swr_convert_frame(ado_convert_ctx, cur_frame, frame));

            cur_frame->height = frame->height;
            cur_frame->width = frame->width;
            cur_frame->format = isVedio ? out_cur_codec_ctx->pix_fmt : out_cur_codec_ctx->sample_fmt;
            cur_frame->pts = frame->pts;//isVedio ? frame->pts - nowTime : frame->pts - nowTime;

            if (!isVedio) {
                ret = handle_audio_frame(audio_fifo, cur_frame, out_cur_codec_ctx);
                if (ret == FIFO_NOT_ENOUGH) break; 
            }

            ret_dst = avcodec_send_frame(out_cur_codec_ctx, cur_frame);
            while (ret_dst >= 0) {

                ret_dst = avcodec_receive_packet(out_cur_codec_ctx, packet_dst);
                if (ret_dst == AVERROR(EAGAIN) || ret_dst == AVERROR_EOF) break;

                AVRational ffmpeg_time_base = { 1, AV_TIME_BASE };
                int64_t pkt_time = av_rescale_q(packet_dst->pts, in_cur_stream->time_base, ffmpeg_time_base);
                int64_t now_time_1 = av_gettime() - nowTime;
                if (pkt_time - now_time_1 > 0) {
                    // av_usleep(pkt_time - now_time_1);
                }

                *ts_p = packet_dst->pts;
                av_packet_rescale_ts(packet_dst, in_cur_stream->time_base, out_cur_stream->time_base);
                packet_dst->stream_index = out_cur_stream->index;
                printf("v_index = %d, a_index = %d, pts = %lld,  stream_index = %d, size = %d\n",
                                                        isVedio ? vedio_encode_index++ : 0,
                                                        !isVedio ? audio_encode_index++ : 0,
                                                        packet_dst->pts,
                                                        packet_dst->stream_index, packet_dst->size);

                void_handle(av_write_frame(out_ctx, packet_dst));
                av_packet_unref(packet_dst);
            }
            av_packet_unref(packet_src);
        }

    }
    //写文件尾
    void_handle(av_write_trailer(out_ctx));

    if (frame)              av_frame_free(&frame);
    if (frameYUV)           av_frame_free(&frameYUV);
    if (frameAUDIO)         av_frame_free(&frameAUDIO);
    if (packet_src)         av_packet_free(&packet_src);
    if (packet_dst)         av_packet_free(&packet_dst);
    if (in_audio_ctx)       avformat_close_input(&in_audio_ctx);
    if (out_ctx)            avformat_close_input(&out_ctx);
    if (in_vedio_ctx)       avformat_close_input(&in_vedio_ctx);
    if (data)               av_free(data);
    if (img_convert_ctx)    sws_freeContext(img_convert_ctx);
    if (ado_convert_ctx)    swr_free(&ado_convert_ctx);
    if (audio_fifo)         av_audio_fifo_free(audio_fifo);
    cout << "over" << endl;
    return 0;
}