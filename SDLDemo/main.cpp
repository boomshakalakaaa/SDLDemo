#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "packetqueue.h"

using namespace std;
#ifdef __cplusplus
extern "C"
{
    #endif
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
    #include <SDL.h>
    #ifdef __cplusplus
}
#endif

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#define SDL_AUDIO_BUFFER_SIZE 1024
#define FILE_NAME "D:/Kugou/int.mkv"
#define ERR_STREAM stderr
#define OUT_SAMPLE_RATE 44100

AVFrame wanted_frame;
PacketQueue audio_queue;


//回调函数
void audio_callback(void *userdata, Uint8 *stream, int len);
//解码函数
int audio_decode_frame(AVCodecContext *pcodec_ctx, uint8_t *audio_buf, int buf_size);
//查找 auto_stream索引
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int *audio_stream);

#undef main
int main(int argc, char *argv[])
{
    //0.申请变量
    //文件指针
    AVFormatContext *pFormatCtx = NULL;
    //解码器
    AVCodecContext *pCodecCtx = NULL;
    AVCodec *pCodec = NULL;
    //解码前的AAC数据
    AVPacket packet;
    //解码之后的PCM数据
    AVFrame *pframe = NULL;
    char filename[256] = FILE_NAME;
    //SDL参数设置变量
    //wanted_spec:想要打开的
    //spec:实际打开的,可以不用这个，函数中直接用 NULL,下面用到 spec 用 wanted_spec 代替。
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;
    //解码器需要的流的索引
    int audioStream = -1;

    //1.ffmpeg 初始化
    av_register_all();
    //2.SDL 初始化
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(ERR_STREAM, "Couldn't init SDL:%s\n", SDL_GetError());
        exit(-1);
    }
    //3.打开文件
    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) != 0)
    {
        fprintf(ERR_STREAM, "Couldn't open input file\n");
        exit(-1);
    }
    //3.1 获取文件流信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        fprintf(ERR_STREAM, "Not Found Stream Info\n");
        exit(-1);
    }
    //显示文件信息
    av_dump_format(pFormatCtx, 0, filename, false);
    //4.读取音频流
    if (find_stream_index(pFormatCtx, NULL, &audioStream) == -1)
    {
        fprintf(ERR_STREAM, "Couldn't find stream index\n");
        exit(-1);
    }
    printf("audio_stream = %d\n", audioStream);
    //5.找到对应的解码器
    pCodecCtx = pFormatCtx->streams[audioStream]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id); if (!pCodec)
    {
        fprintf(ERR_STREAM, "Couldn't find decoder\n");
        exit(-1);
    }
    //6.设置音频信息, 用来打开音频设备
    wanted_spec.freq = pCodecCtx->sample_rate;      //采样频率
    wanted_spec.format = AUDIO_S16SYS;              //格式（位数）
    wanted_spec.channels = pCodecCtx->channels;     //通道数
    wanted_spec.silence = 0;                        //设置静音值
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;    //读取第一帧后调整
    wanted_spec.callback = audio_callback;          //回调函数
    wanted_spec.userdata = pCodecCtx;               //传入回调函数的参数

    //7.打开音频设备
    //这里会开一个线程，调用 callback。
    //SDL_OpenAudioDevice->open_audio_device(开线程)->SDL_RunAudio->fill(指向 callback 函数)
    SDL_AudioDeviceID id = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0,0),0,&wanted_spec, &spec,0);
    if( id < 0 ) //第二次打开 audio 会返回-1
    {
        fprintf(ERR_STREAM, "Couldn't open Audio: %s\n", SDL_GetError());
        exit(-1);
    }

    //8.设置参数，供解码时候用, swr_alloc_set_opts 的 in 部分参数
    wanted_frame.format = AV_SAMPLE_FMT_S16;
    wanted_frame.sample_rate = spec.freq;
    wanted_frame.channel_layout = av_get_default_channel_layout(spec.channels);
    wanted_frame.channels = spec.channels;

    //9.打开解码器, 初始化 AVCondecContext，以及进行一些处理工作。
    avcodec_open2(pCodecCtx, pCodec, NULL);

    //10.SDL 播放音频 0 播放
    SDL_PauseAudioDevice(id,0);

    //11.循环读取音频帧(读一帧数据)放入音频同步队列
    while(av_read_frame(pFormatCtx, &packet) >= 0) //读一个 packet，数据放在 packet.data
    {
        //判断是否是音频帧
        if (packet.stream_index == audioStream)
        {
            audio_queue.packet_queue_put(&packet);
        }
        else
        {
            av_free_packet(&packet);
        }
    }

    //如果队列中还有包，线程不退出
    while( audio_queue.nb_packets != 0)
    {
        SDL_Delay(100);
    }
    //回收空间
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
    printf("play finished\n");
    return 0;
}

//函数参数解析：
//userdata是前面设置的 wanted_spec.userdata = pCodecCtx; 传入回调函数的参数
//stream是要把声音数据写入的SDL缓冲区的指针
//len代表SDL缓冲区的最大大小，代表调用一次callback最多发送len个字节

//这个函数的工作模式是:
//1. 一共有两个缓冲区
//2. 用户缓冲区audio_buf 用audio_buf_size记录缓冲区数据量
//3. SDL播放缓冲区stream 调用一次 callback  最多一次能装len个字节
//4. 实际每次解码得到的数据可能比 len 大，一次发不完 就存入用户缓冲区中
//3. 发不完的时候，会 len == 0，不继续循环，退出函数，下一次继续调用 callback，进行拷贝
//4. 由于上次没发完，这次不解码数据，发上次的剩余的，audio_buf_size 就表示了 用户缓冲区剩余的数据量
//5. 注意，callback 每次一定要发且仅发 len 个数据，否则不会退出
//   如果没发够，缓冲区中又没有了，就再解码获取存入缓冲区
//   发够了，就退出，下一次调用callback再发
//   三个变量设置为 static 就是为了保存上次数据
//6. 解码函数中将从队列中取数据, 解码后填充到播放缓冲区

void audio_callback(void *userdata, Uint8 *stream, int len)
{
    //注意：userdata 是传入的解码器的参数
    AVCodecContext *pcodec_ctx = (AVCodecContext *) userdata;

    //一次实际解码数据的字节数
    int audio_data_size = 0;

    //1.5倍最大帧大小 便于缓冲 之所以缓冲区类型是uint8_t是因为AVFrame结构体中存放数据的数组就是uint8_t类型
    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];

    //缓冲区数量、发送到的位置标志
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    /* len 是由 SDL 传入的 SDL 缓冲区的大小，如果这个缓冲未满，我们就一直往里填充数据
    audio_buf_index 和 audio_buf_size 标示我们自己用来放置解码出来的数据的缓冲区
    这些数据待 copy 到 SDL 缓冲区，当 audio_buf_index >= audio_buf_size 的时候意味着我
    们的缓冲为空，没有数据可供 copy，这时候需要调用 audio_decode_frame 来解码出更多帧数据*/

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            //将解码的数据存入audio_buf缓冲区中，返回实际解码音频大小
            audio_data_size = audio_decode_frame(pcodec_ctx,audio_buf,sizeof(audio_buf));

            if (audio_data_size < 0)
            {
                // audio_data_size < 0 表示没能解码出数据，我们默认播放这一帧为静音
                audio_buf_size = 1024;       
                memset(audio_buf, 0, audio_buf_size);
            } else
            {
                //如果解码出数据，缓冲区大小 变为 实际解码得到的数据长度
                audio_buf_size = audio_data_size;
            }
            audio_buf_index = 0;
        }
        //计算实际每次向SDL缓冲区拷贝的大小 = 总数据量 - 上一次拷贝到的位置的下标
        int real_len = audio_buf_size - audio_buf_index;
        if (real_len > len)
        {
            //如果这个值大于 SDL缓冲区最大长度len 把它强制赋值为len
            real_len = len;
        }
        //向SDL缓冲区拷贝解码后的音频数据
        memcpy(stream, (uint8_t *) audio_buf + audio_buf_index, real_len);
        //下次SDL缓冲区最大长度 = 本次SDL缓冲区最大长度 - 单次实际拷贝长度
        len -= real_len;
        //下次stream开始的地址向后偏移 real_len 即实际拷贝的长度
        stream += real_len;
        //下次标志缓冲区中拷贝到的位置的下标 = 这次下标 + 实际拷贝长度
        audio_buf_index += real_len;
    }
}
//解码封装函数
int audio_decode_frame(AVCodecContext *pcodec_ctx, uint8_t *audio_buf, int buf_size)
{
    //从 队列里取的AAC数据包
    static AVPacket pkt;
    //指向音频数据首地址的指针
    static uint8_t *audio_pkt_data = NULL;
    //记录音频数据包大小
    static int audio_pkt_size = 0;

    //解码器
    AVCodecContext *aCodecCtx = pcodec_ctx;
    //AVFrame存放解码后的PCM数据
    AVFrame *audioFrame = NULL;

    //转换格式用的结构体
    static struct SwrContext *swr_ctx = NULL;

    for(;;)
    {
        //从缓冲队列取packet
        if(audio_queue.packet_queue_get(&pkt, 0) <= 0)
        {
            return -1;
        }
        //为AVFrame申请空间，清零
        audioFrame = av_frame_alloc();
        memset(audioFrame, 0, sizeof(AVFrame));

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;

        while(audio_pkt_size > 0)
        {
            //当包里的音频数据数量>0
            int got_audio = 0;
            //解码音频帧得到PCM数据 存到AVFrame 返回实际解码得到的值ret
            //avcodec_decode_audio4解码后得到的数据格式为
            //AV_SAMPLE_FMT_FLTP（float, planar）而不再是AV_SAMPLE_FMT_S16（signed 16 bits）
            //无法使用SDL直接播放，需要转换成 AV_SAMPLE_FMT_S16
            int ret = avcodec_decode_audio4(aCodecCtx, audioFrame, &got_audio, &pkt);
            if( ret < 0 )
            {
                printf("Error in decoding audio frame.\n");
                exit(0);
            }
            //一帧一个声道读取数据是 nb_samples , channels 为声道数 2 表示 16 位 2 个字节
            int data_size = audioFrame->nb_samples * wanted_frame.channels * 2;
            if( got_audio )
            {
                if (swr_ctx != NULL)
                {
                    swr_free(&swr_ctx);
                    swr_ctx = NULL;
                }
                //设置转换参数
                swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout,
                                             (AVSampleFormat)wanted_frame.format,wanted_frame.sample_rate,
                                             audioFrame->channel_layout,(AVSampleFormat)audioFrame->format,
                                             audioFrame->sample_rate, 0, NULL);
                //初始化
                if (swr_ctx == NULL || swr_init(swr_ctx) < 0)
                {
                    printf("swr_init error\n");
                    break;
                }
                //转换 输入缓冲区为audioFrame->data 输出到 audio_buf
                swr_convert(swr_ctx, &audio_buf,
                            AVCODEC_MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **)audioFrame->data,
                            audioFrame->nb_samples);
            }
            //剩余未解码数据 = 上一次未解码数据 - 本次实际解码数据
            audio_pkt_size -= ret;
            if (audioFrame->nb_samples <= 0)
            {
                //如果解码数据不够 继续从队列取数据解码
                continue;
            }
            //解码封装函数出口，返回实际解码帧长度
            av_free_packet(&pkt);
            return data_size;
        }
        av_free_packet(&pkt);
    }
    return -1;
}
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int* audio_stream)
{
    assert(video_stream != NULL || audio_stream != NULL);
    int i = 0;
    int audio_index = -1;
    int video_index = -1;
    for (i = 0; i < pformat_ctx->nb_streams; i++)
    {
        if (pformat_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
        }
        if (pformat_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_index = i;
        }
    }
    //注意以下两个判断有可能返回-1. if (video_stream == NULL)
    {
        *audio_stream = audio_index;
        return *audio_stream;
    }
    if (audio_stream == NULL)
    {
        *video_stream = video_index;
        return *video_stream;
    }
    *video_stream = video_index;
    *audio_stream = audio_index;
    return 0;
}
