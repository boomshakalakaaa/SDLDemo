#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H


#ifdef __cplusplus
extern "C"{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL.h>

class PacketQueue
{
public:
    PacketQueue();
    ~PacketQueue();

public:
    AVPacketList    *first_pkt;     // 队头的一个packet, 注意类型不是AVPacket
    AVPacketList    *last_pkt;      // 队尾packet
    int             nb_packets;     // packet包个数
    int             size;           // 当前包的总大小（单位为字节）
    SDL_mutex       *mutex;         // 互斥锁
    SDL_cond        *cond;          // 条件变量
public:
    void    packet_queue_init();
    int     packet_queue_put(AVPacket *packet);
    int     packet_queue_get(AVPacket *pakcet, int block);
    void    packet_queue_flush();
};


#ifdef __cplusplus
}
#endif

#endif // PACKETQUEUE_H
