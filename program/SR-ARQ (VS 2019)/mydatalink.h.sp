#pragma once

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "protocol.h"


/* ���flag��λ���� */
#define IS_DATA 0x80 // 1000 0000 �Ƿ�������֡
#define IS_PIGGYBACKING 0x40 // 0100 0000 ������֡���Ƿ��Ӵ���ACK/NAK
#define ACK 0x20 //0010 0000 �Ƿ�ΪACK
#define NAK 0x10 //0001 0000 �Ƿ�ΪNAK ע��Ŷ ack��nak�ز�����ͬʱΪ1
#define ACK_RANGED 0x08 //0000 1000 ����λΪ1 ��Ϊ��ΧACK

#define DATA_SPLIT_MASK 0x07 // 0000 0111 ��÷ָ�ȼ��õ�����
/* ʣ�µĵ���λ����ͨ���frame�ķָ�ȼ�
	0 -> 1/8
	1 -> 1/4
	2 -> 1/2
	3 -> 1
*/

#define TIME_OF_PROP 270 //���εĴ���ʱ�� �������㳬ʱtimer

#define SENDWINDOW _SW_ //���ʹ��ڴ�С,ѡ���ش��� Խ��Խ�� ���ǰ��ڼ�ʱ�����63 �Ǿ���Ϊ63����
#define RECVWINDOW _SW_ //���մ���

#define TRANSMISSION_SPEED 1 //ÿ����1�ֽڵķ����ٶ�


/* һ�������Э����ʱӦ���ɳ����Լ��ó�,���������ۼ����ʵ�����ֵ��һ������ȫ��Ӧ��,�����ڼ�һ��ƫ��ֵ,��������֮ͨ���������� */
#define DATA_TIMER_TIMEOUT _DTT_ //��λ��ms
#define ACK_TIMER_TIMEOUT _ATT_