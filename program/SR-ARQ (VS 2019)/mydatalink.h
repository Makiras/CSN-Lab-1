#pragma once

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "protocol.h"


/* 针对flag的位定义 */
#define IS_DATA 0x80 // 1000 0000 是否是数据帧
#define IS_PIGGYBACKING 0x40 // 0100 0000 在数据帧中是否捎带了ACK/NAK
#define ACK 0x20 //0010 0000 是否为ACK
#define NAK 0x10 //0001 0000 是否为NAK 注意哦 ack与nak必不可能同时为1
#define ACK_RANGED 0x08 //0000 1000 若此位为1 则为范围ACK

#define DATA_SPLIT_MASK 0x07 // 0000 0111 获得分割等级用的掩码
/* 剩下的低三位用于通告此frame的分割等级
	0 -> 1/8
	1 -> 1/4
	2 -> 1/2
	3 -> 1
*/

#define TIME_OF_PROP 270 //单次的传播时延 用来计算超时timer

#define SENDWINDOW 64 //发送窗口大小,选择重传嘛 越大越好 但是碍于计时器最高63 那就设为63好了
#define RECVWINDOW 64 //接收窗口

#define TRANSMISSION_SPEED 1 //每毫秒1字节的发送速度


/* 一个优秀的协议延时应该由程序自己得出,但是在理论计算和实际最佳值不一定是完全对应的,所以在加一个偏置值,可以在跑通之后慢慢调试 */
#define DATA_TIMER_TIMEOUT 4000 //单位是ms
#define ACK_TIMER_TIMEOUT 200