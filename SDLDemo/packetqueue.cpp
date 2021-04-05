#include "packetqueue.h"

PacketQueue::PacketQueue()
{
    this->packet_queue_init();

}
PacketQueue::~PacketQueue()
{


}

//初始化队列
void PacketQueue::packet_queue_init()
{
     this->first_pkt    = NULL;
     this->last_pkt     = NULL;
     this->nb_packets   = 0;
     this->size         = 0;
     this->mutex        = SDL_CreateMutex();
     this->cond         = SDL_CreateCond();
}

//入队 -- 尾添加
int PacketQueue::packet_queue_put(AVPacket *packet)
{
     // 包校验
     if (av_dup_packet(packet) < 0)
     {
         return -1;
     }
     // 为本次添加到队列的包结点 申请空间
     AVPacketList   *pkt_list;
     pkt_list = (AVPacketList *)av_malloc(sizeof(AVPacketList));

     if (pkt_list == NULL)
     {
          return -1;
     }

     pkt_list->pkt   = *packet;
     pkt_list->next  = NULL;

     //上锁
     SDL_LockMutex(this->mutex);

     if (this->last_pkt == NULL)
     {
         //队列为空
          this->first_pkt = pkt_list;
     }
     else
     {
         //队列非空
         this->last_pkt->next = pkt_list;
     }
     //移动末尾指针，增加包个数、包总字节数
     this->last_pkt = pkt_list;
     this->nb_packets++;
     this->size += packet->size;

     SDL_CondSignal(this->cond);   //添加完发送条件变量的信号--没有信号要阻塞(生产者消费者)
     SDL_UnlockMutex(this->mutex);

     return 0;
}

/// 出队--头删除
/// queue传入队列指针 pkt 输出类型的参数, 返回结果.
/// block 表示是否阻塞 为1时 队列为空阻塞等待
int PacketQueue::packet_queue_get(AVPacket *pkt, int block)
{
     AVPacketList *pkt_list = NULL;
     int ret = 0;

     //加锁
     SDL_LockMutex(this->mutex);

     while(1)
     {
         //要删除的结点为队头指针指向的结点
         pkt_list = this->first_pkt;
         if (pkt_list != NULL)
         {
             //队不空，还有数据
             this->first_pkt = this->first_pkt->next;
             if (this->first_pkt == NULL)
             {
                 this->last_pkt = NULL;
             }

             //包个数减小、包总字节数减少
             this->nb_packets--;
             this->size -= pkt_list->pkt.size;
             // 复制给packet
             *pkt = pkt_list->pkt;
             av_free(pkt_list);
             ret = 1;
             break;
         }
         else if (block == 0)
         {
             ret = 0;
             break;
         }
         else
         {
             SDL_CondWait(this->cond, this->mutex);
         }
     }

     SDL_UnlockMutex(this->mutex);
     return ret;
}
//清空队列
void PacketQueue::packet_queue_flush()
{   
    //加锁
    SDL_LockMutex(this->mutex);

    while(this->nb_packets != 0)
    {
        AVPacketList *del = this->first_pkt;
        first_pkt = first_pkt->next;
        av_free_packet(&del->pkt);
        av_freep(&del);
        nb_packets--;
    }

    //清空队列参数
    this->last_pkt = NULL;
    this->first_pkt = NULL;
    this->size = 0;
    SDL_UnlockMutex(this->mutex);
}
