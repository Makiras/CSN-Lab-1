#include "mydatalink.h"

/* 全局变量解耦合太麻烦了,全写一起好了 反正也不会重构了~ */
int isACKNAKdelayed = 0; //标注是否已经存在被延迟的ack或nak
int ACK_or_NAK = 0; //标注被延迟的是ack还是nak 若为1是ack,0是nak
int isACKNAKranged = 0; //标注被延迟的是不是范围ack
unsigned char ACKNAKseq1 = 0;//被延迟的序号
unsigned char ACKNAKseq2 = 0; //在范围ack中用的

unsigned char* recv_buffer[RECVWINDOW]; //接收缓冲区buffer 只存的是指针,记得全部初始化为NULL
int recv_buffer_lengthes[RECVWINDOW]; //记录长度 记得别忘了初始化
unsigned char* send_buffer[SENDWINDOW]; //发送缓冲区buffer
int send_buffer_lengthes[SENDWINDOW];//发送缓冲区每帧的长度
int is_sent[SENDWINDOW];//标记是否已经发送 为1是已发送 0是未发送 应该全部初始化为1

unsigned char recv_lowerbound = 0;//接收窗口序号下界
unsigned char recv_upperbound = RECVWINDOW;//接收窗口上界 前闭后开

unsigned char send_lowerbound = 0;//发送窗口序号下界
unsigned char send_upperbound = 0;//发送窗口序号上界

int SPLIT_LEVEL = 3;//可变帧长对package的分割等级,默认为3,也就是不分割
static const int fragment_numbers[4] = { 8,4,2,1 }; //每个分割等级应该将一个package分成多少份

int number_of_received_frames = 0;
int number_of_broken_recived_frames = 0;
int enable_ranged_ack = 1; //是否启用范围ack
/* 全局变量结束~ */

//返回当前可用的序号数
static unsigned char number_of_available_send_seq() {
	return (SENDWINDOW - (send_upperbound - send_lowerbound));
}

//判断序号是否在期望值之间,前闭后开 若不在返回0 否则返回1
static int between(unsigned char L, unsigned char H, unsigned char mid) {
	if (L < H) { //最一般的情况下
		return (L <= mid && mid < H);
	}
	else if (L > H) {
		return (L <= mid || mid < H);
	}
	else return 0; //这个时候LH相等,没有期望值
}

 //为数据帧添加CRC校验和，然后交付给物理层
static void put_frame(unsigned char* frame, int len)
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
}

//立即发送ack或者nak短帧,清空acknak延迟
static void flush_ACKNAK_delay(void) {
	if (!isACKNAKdelayed) { //错误的调用了 直接返回
		//dbg_warning("错误地调用了acknak刷新函数,没有需要立即发送的acknak短帧.\n");
		stop_ack_timer();
		isACKNAKdelayed = 0;
		isACKNAKranged = 0;
		return;
	}
	unsigned char* frame_ptr = NULL;
	if (ACK_or_NAK == 0) {
		//发送nak短帧
		frame_ptr = (unsigned char*)malloc(6);
		if (frame_ptr == NULL) {
			dbg_warning("在发送NAK短帧时申请内存失败.\n");
			exit(-1);
		}
		*(frame_ptr) = NAK;
		*(frame_ptr + 1) = ACKNAKseq1;
		put_frame(frame_ptr, 2);
		dbg_frame("发送nak 序号为%d.\n",ACKNAKseq1);
	}
	else {
		//发送ack
		frame_ptr = (unsigned char*)malloc(7);
		if (frame_ptr == NULL) {
			dbg_warning("在发送ACK短帧时申请内存失败.\n");
			exit(-1);
		}
		if (isACKNAKranged) {
			//发送范围ack
			*(frame_ptr) = ACK | ACK_RANGED;
			*(frame_ptr + 1) = ACKNAKseq1;
			*(frame_ptr + 2) = ACKNAKseq2;
			put_frame(frame_ptr, 3);
			dbg_frame("发送范围ack 序号为%d - %d.\n", ACKNAKseq1,ACKNAKseq2);
		}
		else {
			//发送普通ack短帧
			*(frame_ptr) = ACK;
			*(frame_ptr + 1) = ACKNAKseq1;
			put_frame(frame_ptr, 2);
			dbg_event("回复ack短帧 id:%d.\n", ACKNAKseq1);
		}
	}
	stop_ack_timer();
	isACKNAKdelayed = 0;
	isACKNAKranged = 0; //清空acknak延迟
	return;
}

//从发送队列里面取一帧发送到物理层,在得到pysicallayerready信号时调用
static void send_frame_to_physical() {
	unsigned char index = send_lowerbound;
	while (index != send_upperbound) {
		if (is_sent[index % SENDWINDOW] == 0)break; //得到队列里最前面的未发送帧
		index++;
	}

	if (index == send_upperbound) {
		//dbg_event("在没有可发送的帧的时候调用了send_frame_to_physical.\n");
		return;
	}

	is_sent[index % SENDWINDOW] = 1;
	unsigned char* frame_ptr = send_buffer[index % SENDWINDOW];
	int length_of_frame = send_buffer_lengthes[index % SENDWINDOW];
	if (frame_ptr == NULL) {
		dbg_warning("在发送交付物理层时遇到了空指针(你不该看到这条消息的).\n");
		return;
	}
	if (length_of_frame < 3) {
		dbg_warning("在交付物理层是遇到了错误的长度(小于3).\n");
		return;
	}
	unsigned char* frame_to_physical_ptr = (unsigned char*)malloc(length_of_frame + 6+16);
	if (frame_to_physical_ptr == NULL) {
		dbg_warning("在将发送缓冲区的帧发送到物理层时申请内存失败.\n");
		exit(-1);
	}
	memcpy(frame_to_physical_ptr, frame_ptr, length_of_frame+2);
	if (!isACKNAKdelayed) {
		//测试用
		unsigned char frame_to_physical[1024];
		memcpy(frame_to_physical, frame_to_physical_ptr, length_of_frame + 2);


		put_frame(frame_to_physical_ptr, length_of_frame+2);
		dbg_frame("已发送序号为 %d 的一帧至物理层. 没有携带ack,nak.\n", *(frame_to_physical_ptr + 1));
	}
	else {
		*frame_to_physical_ptr |= IS_PIGGYBACKING;
		if (ACK_or_NAK == 0) {
			//捎带一个nak信息
			*frame_to_physical_ptr |= NAK;
			*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
			put_frame(frame_to_physical_ptr, length_of_frame + 1+2);
			dbg_frame("已发送序号为 %d 的一帧至物理层.\n 携带了序号为 %d 的nak.\n", *(frame_to_physical_ptr + 1),ACKNAKseq1);
		}
		else {
			if (isACKNAKranged) {
				*frame_to_physical_ptr |= ACK | ACK_RANGED;
				*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
				*(frame_to_physical_ptr + length_of_frame +2+ 1) = ACKNAKseq2;
				put_frame(frame_to_physical_ptr, length_of_frame + 2 +2);
				dbg_frame("已发送序号为 %d 的一帧至物理层.\n 携带了序号为 %d - %d 的范围ack.\n", *(frame_to_physical_ptr + 1), ACKNAKseq1, ACKNAKseq2);
			}
			else {
				*frame_to_physical_ptr |= ACK;
				*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
				put_frame(frame_to_physical_ptr, length_of_frame + 1+2);
				dbg_frame("已发送序号为 %d 的一帧至物理层.\n 携带了序号为 %d 的ack.\n", *(frame_to_physical_ptr + 1 ), ACKNAKseq1);
			}
		}
		isACKNAKdelayed = 0;
		stop_ack_timer();
		
	}
	start_timer(index % SENDWINDOW,DATA_TIMER_TIMEOUT);
	free(frame_to_physical_ptr);
}

//如果datatimer超时,调用此函数
static void data_timer_timeout(unsigned char timer_id) {
	stop_timer(timer_id);
	unsigned char* frame_ptr = send_buffer[timer_id];
	if (frame_ptr == NULL) {
		dbg_warning("错误的启动了id为 %d 的timer超时函数,请检查程序运行逻辑.\n", timer_id);
		return;
	}
	unsigned char seq = *(frame_ptr + 1);
	dbg_frame("编号为 %d 的datatimer超时, 对应的帧序号为 %d",timer_id,seq);
	is_sent[timer_id] = 0; //使其重新发送
}

//当得到networklayerready事件时调用
static void get_package_from_network(int split_level) {
	int nums = fragment_numbers[split_level];
	unsigned char available_nums = number_of_available_send_seq();
	if (nums > available_nums) {
		//dbg_warning("当前发送序号下界为 %d 上界为 %d 可用数为 %d 当前分割登记下需要 %d 个可用序号 不能获取packet.\n", send_lowerbound, send_upperbound, available_nums, nums);
		return;
	}
	unsigned char* package_ptr = (unsigned char*)malloc(2048);
	if (package_ptr == NULL) {
		dbg_warning("在获取网络层packet时申请内存失败.\n");
		exit(-1);
	}

	

	int length_of_package = get_packet(package_ptr);

	
	int length_of_fragment = length_of_package / nums;
	unsigned char* temp_ptr = package_ptr;
	for (int i = 0; i < nums; i++) {
		unsigned seq = send_upperbound++;
		
		unsigned char* fragment_ptr;
		if (i != nums - 1) {
			fragment_ptr = (unsigned char*)malloc(2 + length_of_fragment+16);
			if (fragment_ptr == NULL) {
				dbg_warning("在将packet分割为片段时申请内存失败.\n");
				exit(-1);
			}
			*fragment_ptr = IS_DATA | (DATA_SPLIT_MASK & split_level);
			*(fragment_ptr + 1) = seq;
			is_sent[seq%SENDWINDOW] = 0;
			memcpy(fragment_ptr + 2, temp_ptr, length_of_fragment);
			temp_ptr += length_of_fragment;
		}
		else {
			length_of_fragment = length_of_package - (temp_ptr - package_ptr); //整数除法不准 兜个底
			fragment_ptr = (unsigned char*)malloc(2 + length_of_fragment+16);
			if (fragment_ptr == NULL) {
				dbg_warning("在将packet分割为片段时申请内存失败.\n");
				exit(-1);
			}
			*fragment_ptr = IS_DATA | (DATA_SPLIT_MASK & split_level);
			*(fragment_ptr + 1) = seq;
			is_sent[seq%SENDWINDOW] = 0;
			memcpy(fragment_ptr + 2, temp_ptr, length_of_fragment);
		}
		
		dbg_frame("放入缓冲区一帧 序号为%d 长度为%d.\n",seq,length_of_fragment);
		send_buffer_lengthes[seq % SENDWINDOW] = length_of_fragment;
		send_buffer[seq % SENDWINDOW] = fragment_ptr;
	}
}

//发送ack(lazy) 如果是nak那么isack为0 是ack就为1
static void send_ACKNAK(unsigned char seq, int is_ACK) {

	if (isACKNAKdelayed&&enable_ranged_ack) {
		if (ACK_or_NAK == 1 && is_ACK == 1) {
			if (!isACKNAKranged) {
				if (seq == ACKNAKseq1 - 1) {
					ACKNAKseq2 = ACKNAKseq1;
					ACKNAKseq1 = seq;
					isACKNAKranged = 1;
					return;
				}
				if (seq == ACKNAKseq1 + 1) {
					ACKNAKseq2 = seq;
					isACKNAKranged = 1;
					return;
				}
			}
			else {
				if (seq == ACKNAKseq1 - 1) {
					ACKNAKseq1 = seq;
					return;
				}
				if (seq == ACKNAKseq2 + 1) {
					ACKNAKseq2 = seq;
					return;
				}
			}
		}
	}
	flush_ACKNAK_delay();
	isACKNAKdelayed = 1;
	isACKNAKranged = 0;
	ACK_or_NAK = is_ACK;
	ACKNAKseq1 = seq;
	start_ack_timer(ACK_TIMER_TIMEOUT);
}

/*
static void send_ACKNAK(unsigned char seq, int is_ACK) {
	unsigned char* frame_ptr = NULL;
	if (is_ACK == 0) {
		//发送nak短帧
		frame_ptr = (unsigned char*)malloc(6);
		if (frame_ptr == NULL) {
			dbg_warning("在发送NAK短帧时申请内存失败.\n");
			exit(-1);
		}
		*(frame_ptr) = NAK;
		*(frame_ptr + 1) = seq;
		dbg_event("回复nak短帧 id:%d.\n", seq);
		put_frame(frame_ptr, 2);
	}
	else {
		//发送ack
		frame_ptr = (unsigned char*)malloc(7);
		if (frame_ptr == NULL) {
			dbg_warning("在发送ACK短帧时申请内存失败.\n");
			exit(-1);
		}
		//发送普通ack短帧
		*(frame_ptr) = ACK;
		*(frame_ptr + 1) = seq;
		dbg_event("回复ack短帧 id:%d.\n", seq);
		put_frame(frame_ptr, 2);

	}
}
*/

//当接受到ack或者nak的处理函数
static void recv_ACKNAK(unsigned char flag, unsigned char seq1, unsigned char seq2) {
	if (between(send_lowerbound,send_upperbound,seq1)) {
		if (flag & NAK) {
			//接收到的是nak
			unsigned char* frame_ptr = send_buffer[seq1 % SENDWINDOW];
			if (frame_ptr == NULL) {
				dbg_event("接收到了一个序号为%d 的nak,可是对应帧已被确认.\n", seq1);
				return;
			}
			is_sent[seq1%SENDWINDOW] = 0; //由以后的send_frame_to_physical函数重新发送
			dbg_event("接收到了一个序号为%d 的nak,等待下次物理层ready时发送.\n", seq1);
			return;
		}
		else {
			if (flag & ACK) {
				if (!(flag & ACK_RANGED)) {
					//如果不是范围ack
					seq2 = seq1;
				}
				for (unsigned char i = seq1; (i != (unsigned char)(seq2+1)) && (i != send_upperbound); i++) {
					dbg_event("i: %d seq1: %d, seq2:%d  sendupper: %d\n", i, seq1, seq2, send_upperbound);
					if (send_buffer[i % SENDWINDOW] == NULL) {
						dbg_event("接收到了一个序号为%d 的ack,可是对应帧已被确认.\n", i);
					}
					else {
						free(send_buffer[i % SENDWINDOW]);
						send_buffer[i % SENDWINDOW] = NULL;
						send_buffer_lengthes[i % SENDWINDOW] = 0;
						is_sent[i % SENDWINDOW] = 1; //防止send_frame_to_physical函数错误地读取空指针
						dbg_event("接收到了一个序号为%d 的ack,对应帧已被确认.\n", i);
						stop_timer(i % SENDWINDOW);
					}
				}
				unsigned char i;
				for (i = send_lowerbound; i != send_upperbound; i++) {
					if (send_buffer[i % SENDWINDOW] != NULL)break;
				}
				unsigned char step = i - send_lowerbound;
				dbg_event("原 sendlowerbound %d 向前滑动 %d 现sendlowerbound: %d\n", send_lowerbound,step,send_lowerbound+step);
				send_lowerbound += step;
			}
		}
	}
	
}

//当得到framereceived事件时调用
static void got_frame(void) {
	number_of_received_frames += 1;
	unsigned char* frame_ptr = (unsigned char*)malloc(2048);
	//unsigned char watch[2048];
	//unsigned char* frame_ptr = watch;
	if (frame_ptr == NULL) {
		dbg_warning("在试图获得物理层接收帧时申请内存失败.\n");
		exit(-1);
	}
	int length_of_frame = recv_frame(frame_ptr, 2048);
	//lprintf("length_of_frame %d \n", length_of_frame);
	if (crc32(frame_ptr, length_of_frame) != 0) {
		//校验失败
		number_of_broken_recived_frames += 1;
		if (((*frame_ptr) | IS_DATA)&&length_of_frame>6) {
			unsigned char seq = *(frame_ptr + 1);
			send_ACKNAK(seq, 0);//搏一搏,单车变摩托~
			dbg_frame("收到了一个自称序号为 %d 的损坏数据帧. 发送nak.\n",seq);
		}
		free(frame_ptr);
		return;
	}
	//接下来是正常的处理流程
	if (!((*frame_ptr) & IS_DATA)) { //不是data的acknak短帧
		
		if ((*frame_ptr) & NAK) {
			recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 1));
			free(frame_ptr);
			return;
		}
		if ((*frame_ptr) & ACK) {
			if ((*frame_ptr) & ACK_RANGED) {
				//范围ack
				recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 2));
			}
			else {
				//普通ack短帧
				recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 1));
			}
		}
		free(frame_ptr);
		return;
	}

	//接下来的是处理数据帧的部分
	//处理存在捎带的
	if ((*frame_ptr) & IS_PIGGYBACKING) {
		if (((*frame_ptr) & ACK) && (*frame_ptr) & ACK_RANGED) {
			//是范围的
			recv_ACKNAK(*frame_ptr, *(frame_ptr + length_of_frame - 6), *(frame_ptr + length_of_frame - 5));
			length_of_frame -= 8;
		}
		else
		{
			//不是范围的
			recv_ACKNAK(*frame_ptr, *(frame_ptr + length_of_frame - 5), *(frame_ptr + length_of_frame - 5));
			length_of_frame -= 7;
		}
	}
	else {
		length_of_frame -= 6;
	}
	unsigned char seq = *(frame_ptr + 1);
	send_ACKNAK(seq, 1);
	if (!between(recv_lowerbound, recv_upperbound, seq)) {
		free(frame_ptr);
		dbg_frame("收到了一个序号为 %u 的帧,但这不在期望范围之内 接收期望范围:[%u %u).\n", seq,recv_lowerbound, recv_upperbound);
		return;
	}
	if (recv_buffer[seq % RECVWINDOW] != NULL) {
		free(frame_ptr);
		dbg_frame("收到了一个序号为 %d 的帧,但这个序号的帧已经接收(重发的帧).\n", recv_lowerbound, recv_upperbound);
		return;
	}
	recv_buffer[seq % RECVWINDOW] = frame_ptr;
	recv_buffer_lengthes[seq % RECVWINDOW] = length_of_frame;
	dbg_frame("成功收到了一个序号为 %d 的帧, 长度为%d. \n",seq,length_of_frame);
	int i = recv_lowerbound;
	unsigned char old_upperbound = recv_upperbound;
	while (i != old_upperbound) {
		frame_ptr = recv_buffer[i % RECVWINDOW];
		if (frame_ptr == NULL)break;
		unsigned char splitLevel = (*frame_ptr) & DATA_SPLIT_MASK;
		int f;
		if (splitLevel < 4)f = fragment_numbers[(unsigned char)splitLevel];
		else break;
		if (!between(recv_lowerbound, recv_upperbound, i+f-1))break;
		int length = 0;
		int j = i;
		for (; j != i + f; j++) {
			length += recv_buffer_lengthes[j % RECVWINDOW];
			if (recv_buffer[j % RECVWINDOW] == NULL) {
				break;
			}
			if (((*recv_buffer[j % RECVWINDOW]) & DATA_SPLIT_MASK )!= splitLevel) {
				dbg_warning("分割数不同步,检查程序运行逻辑!.\n");
				exit(-1);
			}
		}
		if (j != i + f)break;
		
		//接下来,将分割后的数据合并为一个帧
		unsigned char* packet_ptr = (unsigned char*)malloc(length+16);
		unsigned char* temp_ptr = packet_ptr;
		for (j = i; j != i + f; j++) {
			int length_of_fragment = recv_buffer_lengthes[j % RECVWINDOW];
			memcpy(temp_ptr, recv_buffer[j % RECVWINDOW]+2, length_of_fragment);
			temp_ptr += length_of_fragment;
			free(recv_buffer[j % RECVWINDOW]);
			recv_buffer[j % RECVWINDOW] = NULL;
		}
		recv_lowerbound += f;
		recv_upperbound += f;
		put_packet(packet_ptr, length);

		short inner_id = *(short*)packet_ptr;
		dbg_event("已交付网络层一个package 内置id为 %d 它的头部帧id为 %d .\n", inner_id,i);
		free(packet_ptr);

		i += f;
	}

}
void init(void) {
	for (int i = 0; i < RECVWINDOW; i++) {
		recv_buffer[i] = NULL;
		recv_buffer_lengthes[i] = 0;
	}
	for (int i = 0; i < SENDWINDOW; i++) {
		send_buffer[i] = NULL;
		send_buffer_lengthes[i] = 0;
		is_sent[i] = 1;
	}
}


int main(int argc, char** argv) {
	init(); //初始化各全局变量
	enable_network_layer();
	int event, arg;
	protocol_init(argc, argv);
	lprintf("Designed by Rivers Jin, build: " __DATE__"  "__TIME__"\n");
	int is_phsical_ready = 1;
	while (1) {
		//lprintf("sendlower:%ud sendupper :%ud recvlower:%ud recvupper:%ud \n",send_lowerbound,send_upperbound,recv_lowerbound,recv_upperbound);
		event = wait_for_event(&arg);
		if (number_of_received_frames > 100) {
			if (enable_ranged_ack) {
				if ((float)number_of_broken_recived_frames / number_of_received_frames > 0.1) {
					enable_ranged_ack = 0;
					SPLIT_LEVEL = 1;
					dbg_event("*****误码率较高,关闭范围ack功能,降低帧长度*****\n");
				}
			}
		}
		switch (event)
		{
		case NETWORK_LAYER_READY:
			get_package_from_network(SPLIT_LEVEL);
			break;
		case PHYSICAL_LAYER_READY:
			is_phsical_ready = 1;
			break;
		case FRAME_RECEIVED:
			got_frame();
			break;
		case DATA_TIMEOUT:
			data_timer_timeout(arg);
			break;
		case ACK_TIMEOUT:
			flush_ACKNAK_delay();
			break;
		default:
			break;
		}
		if (phl_sq_len() < 64)is_phsical_ready = 1;
		else is_phsical_ready = 0;
		if (is_phsical_ready) {
			send_frame_to_physical();
			enable_network_layer();
		}
		int nums = fragment_numbers[SPLIT_LEVEL];
		int available_nums = number_of_available_send_seq();
		if (nums > available_nums && is_phsical_ready) {
			disable_network_layer();
		}
		else enable_network_layer();
	}
}