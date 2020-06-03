#include "mydatalink.h"

/* ȫ�ֱ��������̫�鷳��,ȫдһ����� ����Ҳ�����ع���~ */
int isACKNAKdelayed = 0; //��ע�Ƿ��Ѿ����ڱ��ӳٵ�ack��nak
int ACK_or_NAK = 0; //��ע���ӳٵ���ack����nak ��Ϊ1��ack,0��nak
int isACKNAKranged = 0; //��ע���ӳٵ��ǲ��Ƿ�Χack
unsigned char ACKNAKseq1 = 0;//���ӳٵ����
unsigned char ACKNAKseq2 = 0; //�ڷ�Χack���õ�

unsigned char* recv_buffer[RECVWINDOW]; //���ջ�����buffer ֻ�����ָ��,�ǵ�ȫ����ʼ��ΪNULL
int recv_buffer_lengthes[RECVWINDOW]; //��¼���� �ǵñ����˳�ʼ��
unsigned char* send_buffer[SENDWINDOW]; //���ͻ�����buffer
int send_buffer_lengthes[SENDWINDOW];//���ͻ�����ÿ֡�ĳ���
int is_sent[SENDWINDOW];//����Ƿ��Ѿ����� Ϊ1���ѷ��� 0��δ���� Ӧ��ȫ����ʼ��Ϊ1

unsigned char recv_lowerbound = 0;//���մ�������½�
unsigned char recv_upperbound = RECVWINDOW;//���մ����Ͻ� ǰ�պ�

unsigned char send_lowerbound = 0;//���ʹ�������½�
unsigned char send_upperbound = 0;//���ʹ�������Ͻ�

int SPLIT_LEVEL = 3;//�ɱ�֡����package�ķָ�ȼ�,Ĭ��Ϊ3,Ҳ���ǲ��ָ�
static const int fragment_numbers[4] = { 8,4,2,1 }; //ÿ���ָ�ȼ�Ӧ�ý�һ��package�ֳɶ��ٷ�

int number_of_received_frames = 0;
int number_of_broken_recived_frames = 0;
int enable_ranged_ack = 1; //�Ƿ����÷�Χack
/* ȫ�ֱ�������~ */

//���ص�ǰ���õ������
static unsigned char number_of_available_send_seq() {
	return (SENDWINDOW - (send_upperbound - send_lowerbound));
}

//�ж�����Ƿ�������ֵ֮��,ǰ�պ� �����ڷ���0 ���򷵻�1
static int between(unsigned char L, unsigned char H, unsigned char mid) {
	if (L < H) { //��һ��������
		return (L <= mid && mid < H);
	}
	else if (L > H) {
		return (L <= mid || mid < H);
	}
	else return 0; //���ʱ��LH���,û������ֵ
}

 //Ϊ����֡���CRCУ��ͣ�Ȼ�󽻸��������
static void put_frame(unsigned char* frame, int len)
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
}

//��������ack����nak��֡,���acknak�ӳ�
static void flush_ACKNAK_delay(void) {
	if (!isACKNAKdelayed) { //����ĵ����� ֱ�ӷ���
		//dbg_warning("����ص�����acknakˢ�º���,û����Ҫ�������͵�acknak��֡.\n");
		stop_ack_timer();
		isACKNAKdelayed = 0;
		isACKNAKranged = 0;
		return;
	}
	unsigned char* frame_ptr = NULL;
	if (ACK_or_NAK == 0) {
		//����nak��֡
		frame_ptr = (unsigned char*)malloc(6);
		if (frame_ptr == NULL) {
			dbg_warning("�ڷ���NAK��֡ʱ�����ڴ�ʧ��.\n");
			exit(-1);
		}
		*(frame_ptr) = NAK;
		*(frame_ptr + 1) = ACKNAKseq1;
		put_frame(frame_ptr, 2);
		dbg_frame("����nak ���Ϊ%d.\n",ACKNAKseq1);
	}
	else {
		//����ack
		frame_ptr = (unsigned char*)malloc(7);
		if (frame_ptr == NULL) {
			dbg_warning("�ڷ���ACK��֡ʱ�����ڴ�ʧ��.\n");
			exit(-1);
		}
		if (isACKNAKranged) {
			//���ͷ�Χack
			*(frame_ptr) = ACK | ACK_RANGED;
			*(frame_ptr + 1) = ACKNAKseq1;
			*(frame_ptr + 2) = ACKNAKseq2;
			put_frame(frame_ptr, 3);
			dbg_frame("���ͷ�Χack ���Ϊ%d - %d.\n", ACKNAKseq1,ACKNAKseq2);
		}
		else {
			//������ͨack��֡
			*(frame_ptr) = ACK;
			*(frame_ptr + 1) = ACKNAKseq1;
			put_frame(frame_ptr, 2);
			dbg_event("�ظ�ack��֡ id:%d.\n", ACKNAKseq1);
		}
	}
	stop_ack_timer();
	isACKNAKdelayed = 0;
	isACKNAKranged = 0; //���acknak�ӳ�
	return;
}

//�ӷ��Ͷ�������ȡһ֡���͵������,�ڵõ�pysicallayerready�ź�ʱ����
static void send_frame_to_physical() {
	unsigned char index = send_lowerbound;
	while (index != send_upperbound) {
		if (is_sent[index % SENDWINDOW] == 0)break; //�õ���������ǰ���δ����֡
		index++;
	}

	if (index == send_upperbound) {
		//dbg_event("��û�пɷ��͵�֡��ʱ�������send_frame_to_physical.\n");
		return;
	}

	is_sent[index % SENDWINDOW] = 1;
	unsigned char* frame_ptr = send_buffer[index % SENDWINDOW];
	int length_of_frame = send_buffer_lengthes[index % SENDWINDOW];
	if (frame_ptr == NULL) {
		dbg_warning("�ڷ��ͽ��������ʱ�����˿�ָ��(�㲻�ÿ���������Ϣ��).\n");
		return;
	}
	if (length_of_frame < 3) {
		dbg_warning("�ڽ���������������˴���ĳ���(С��3).\n");
		return;
	}
	unsigned char* frame_to_physical_ptr = (unsigned char*)malloc(length_of_frame + 6+16);
	if (frame_to_physical_ptr == NULL) {
		dbg_warning("�ڽ����ͻ�������֡���͵������ʱ�����ڴ�ʧ��.\n");
		exit(-1);
	}
	memcpy(frame_to_physical_ptr, frame_ptr, length_of_frame+2);
	if (!isACKNAKdelayed) {
		//������
		unsigned char frame_to_physical[1024];
		memcpy(frame_to_physical, frame_to_physical_ptr, length_of_frame + 2);


		put_frame(frame_to_physical_ptr, length_of_frame+2);
		dbg_frame("�ѷ������Ϊ %d ��һ֡�������. û��Я��ack,nak.\n", *(frame_to_physical_ptr + 1));
	}
	else {
		*frame_to_physical_ptr |= IS_PIGGYBACKING;
		if (ACK_or_NAK == 0) {
			//�Ӵ�һ��nak��Ϣ
			*frame_to_physical_ptr |= NAK;
			*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
			put_frame(frame_to_physical_ptr, length_of_frame + 1+2);
			dbg_frame("�ѷ������Ϊ %d ��һ֡�������.\n Я�������Ϊ %d ��nak.\n", *(frame_to_physical_ptr + 1),ACKNAKseq1);
		}
		else {
			if (isACKNAKranged) {
				*frame_to_physical_ptr |= ACK | ACK_RANGED;
				*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
				*(frame_to_physical_ptr + length_of_frame +2+ 1) = ACKNAKseq2;
				put_frame(frame_to_physical_ptr, length_of_frame + 2 +2);
				dbg_frame("�ѷ������Ϊ %d ��һ֡�������.\n Я�������Ϊ %d - %d �ķ�Χack.\n", *(frame_to_physical_ptr + 1), ACKNAKseq1, ACKNAKseq2);
			}
			else {
				*frame_to_physical_ptr |= ACK;
				*(frame_to_physical_ptr + length_of_frame+2) = ACKNAKseq1;
				put_frame(frame_to_physical_ptr, length_of_frame + 1+2);
				dbg_frame("�ѷ������Ϊ %d ��һ֡�������.\n Я�������Ϊ %d ��ack.\n", *(frame_to_physical_ptr + 1 ), ACKNAKseq1);
			}
		}
		isACKNAKdelayed = 0;
		stop_ack_timer();
		
	}
	start_timer(index % SENDWINDOW,DATA_TIMER_TIMEOUT);
	free(frame_to_physical_ptr);
}

//���datatimer��ʱ,���ô˺���
static void data_timer_timeout(unsigned char timer_id) {
	stop_timer(timer_id);
	unsigned char* frame_ptr = send_buffer[timer_id];
	if (frame_ptr == NULL) {
		dbg_warning("�����������idΪ %d ��timer��ʱ����,������������߼�.\n", timer_id);
		return;
	}
	unsigned char seq = *(frame_ptr + 1);
	dbg_frame("���Ϊ %d ��datatimer��ʱ, ��Ӧ��֡���Ϊ %d",timer_id,seq);
	is_sent[timer_id] = 0; //ʹ�����·���
}

//���õ�networklayerready�¼�ʱ����
static void get_package_from_network(int split_level) {
	int nums = fragment_numbers[split_level];
	unsigned char available_nums = number_of_available_send_seq();
	if (nums > available_nums) {
		//dbg_warning("��ǰ��������½�Ϊ %d �Ͻ�Ϊ %d ������Ϊ %d ��ǰ�ָ�Ǽ�����Ҫ %d ��������� ���ܻ�ȡpacket.\n", send_lowerbound, send_upperbound, available_nums, nums);
		return;
	}
	unsigned char* package_ptr = (unsigned char*)malloc(2048);
	if (package_ptr == NULL) {
		dbg_warning("�ڻ�ȡ�����packetʱ�����ڴ�ʧ��.\n");
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
				dbg_warning("�ڽ�packet�ָ�ΪƬ��ʱ�����ڴ�ʧ��.\n");
				exit(-1);
			}
			*fragment_ptr = IS_DATA | (DATA_SPLIT_MASK & split_level);
			*(fragment_ptr + 1) = seq;
			is_sent[seq%SENDWINDOW] = 0;
			memcpy(fragment_ptr + 2, temp_ptr, length_of_fragment);
			temp_ptr += length_of_fragment;
		}
		else {
			length_of_fragment = length_of_package - (temp_ptr - package_ptr); //����������׼ ������
			fragment_ptr = (unsigned char*)malloc(2 + length_of_fragment+16);
			if (fragment_ptr == NULL) {
				dbg_warning("�ڽ�packet�ָ�ΪƬ��ʱ�����ڴ�ʧ��.\n");
				exit(-1);
			}
			*fragment_ptr = IS_DATA | (DATA_SPLIT_MASK & split_level);
			*(fragment_ptr + 1) = seq;
			is_sent[seq%SENDWINDOW] = 0;
			memcpy(fragment_ptr + 2, temp_ptr, length_of_fragment);
		}
		
		dbg_frame("���뻺����һ֡ ���Ϊ%d ����Ϊ%d.\n",seq,length_of_fragment);
		send_buffer_lengthes[seq % SENDWINDOW] = length_of_fragment;
		send_buffer[seq % SENDWINDOW] = fragment_ptr;
	}
}

//����ack(lazy) �����nak��ôisackΪ0 ��ack��Ϊ1
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
		//����nak��֡
		frame_ptr = (unsigned char*)malloc(6);
		if (frame_ptr == NULL) {
			dbg_warning("�ڷ���NAK��֡ʱ�����ڴ�ʧ��.\n");
			exit(-1);
		}
		*(frame_ptr) = NAK;
		*(frame_ptr + 1) = seq;
		dbg_event("�ظ�nak��֡ id:%d.\n", seq);
		put_frame(frame_ptr, 2);
	}
	else {
		//����ack
		frame_ptr = (unsigned char*)malloc(7);
		if (frame_ptr == NULL) {
			dbg_warning("�ڷ���ACK��֡ʱ�����ڴ�ʧ��.\n");
			exit(-1);
		}
		//������ͨack��֡
		*(frame_ptr) = ACK;
		*(frame_ptr + 1) = seq;
		dbg_event("�ظ�ack��֡ id:%d.\n", seq);
		put_frame(frame_ptr, 2);

	}
}
*/

//�����ܵ�ack����nak�Ĵ�����
static void recv_ACKNAK(unsigned char flag, unsigned char seq1, unsigned char seq2) {
	if (between(send_lowerbound,send_upperbound,seq1)) {
		if (flag & NAK) {
			//���յ�����nak
			unsigned char* frame_ptr = send_buffer[seq1 % SENDWINDOW];
			if (frame_ptr == NULL) {
				dbg_event("���յ���һ�����Ϊ%d ��nak,���Ƕ�Ӧ֡�ѱ�ȷ��.\n", seq1);
				return;
			}
			is_sent[seq1%SENDWINDOW] = 0; //���Ժ��send_frame_to_physical�������·���
			dbg_event("���յ���һ�����Ϊ%d ��nak,�ȴ��´������readyʱ����.\n", seq1);
			return;
		}
		else {
			if (flag & ACK) {
				if (!(flag & ACK_RANGED)) {
					//������Ƿ�Χack
					seq2 = seq1;
				}
				for (unsigned char i = seq1; (i != (unsigned char)(seq2+1)) && (i != send_upperbound); i++) {
					dbg_event("i: %d seq1: %d, seq2:%d  sendupper: %d\n", i, seq1, seq2, send_upperbound);
					if (send_buffer[i % SENDWINDOW] == NULL) {
						dbg_event("���յ���һ�����Ϊ%d ��ack,���Ƕ�Ӧ֡�ѱ�ȷ��.\n", i);
					}
					else {
						free(send_buffer[i % SENDWINDOW]);
						send_buffer[i % SENDWINDOW] = NULL;
						send_buffer_lengthes[i % SENDWINDOW] = 0;
						is_sent[i % SENDWINDOW] = 1; //��ֹsend_frame_to_physical��������ض�ȡ��ָ��
						dbg_event("���յ���һ�����Ϊ%d ��ack,��Ӧ֡�ѱ�ȷ��.\n", i);
						stop_timer(i % SENDWINDOW);
					}
				}
				unsigned char i;
				for (i = send_lowerbound; i != send_upperbound; i++) {
					if (send_buffer[i % SENDWINDOW] != NULL)break;
				}
				unsigned char step = i - send_lowerbound;
				dbg_event("ԭ sendlowerbound %d ��ǰ���� %d ��sendlowerbound: %d\n", send_lowerbound,step,send_lowerbound+step);
				send_lowerbound += step;
			}
		}
	}
	
}

//���õ�framereceived�¼�ʱ����
static void got_frame(void) {
	number_of_received_frames += 1;
	unsigned char* frame_ptr = (unsigned char*)malloc(2048);
	//unsigned char watch[2048];
	//unsigned char* frame_ptr = watch;
	if (frame_ptr == NULL) {
		dbg_warning("����ͼ�����������֡ʱ�����ڴ�ʧ��.\n");
		exit(-1);
	}
	int length_of_frame = recv_frame(frame_ptr, 2048);
	//lprintf("length_of_frame %d \n", length_of_frame);
	if (crc32(frame_ptr, length_of_frame) != 0) {
		//У��ʧ��
		number_of_broken_recived_frames += 1;
		if (((*frame_ptr) | IS_DATA)&&length_of_frame>6) {
			unsigned char seq = *(frame_ptr + 1);
			send_ACKNAK(seq, 0);//��һ��,������Ħ��~
			dbg_frame("�յ���һ���Գ����Ϊ %d ��������֡. ����nak.\n",seq);
		}
		free(frame_ptr);
		return;
	}
	//�������������Ĵ�������
	if (!((*frame_ptr) & IS_DATA)) { //����data��acknak��֡
		
		if ((*frame_ptr) & NAK) {
			recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 1));
			free(frame_ptr);
			return;
		}
		if ((*frame_ptr) & ACK) {
			if ((*frame_ptr) & ACK_RANGED) {
				//��Χack
				recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 2));
			}
			else {
				//��ͨack��֡
				recv_ACKNAK(*frame_ptr, *(frame_ptr + 1), *(frame_ptr + 1));
			}
		}
		free(frame_ptr);
		return;
	}

	//���������Ǵ�������֡�Ĳ���
	//��������Ӵ���
	if ((*frame_ptr) & IS_PIGGYBACKING) {
		if (((*frame_ptr) & ACK) && (*frame_ptr) & ACK_RANGED) {
			//�Ƿ�Χ��
			recv_ACKNAK(*frame_ptr, *(frame_ptr + length_of_frame - 6), *(frame_ptr + length_of_frame - 5));
			length_of_frame -= 8;
		}
		else
		{
			//���Ƿ�Χ��
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
		dbg_frame("�յ���һ�����Ϊ %u ��֡,���ⲻ��������Χ֮�� ����������Χ:[%u %u).\n", seq,recv_lowerbound, recv_upperbound);
		return;
	}
	if (recv_buffer[seq % RECVWINDOW] != NULL) {
		free(frame_ptr);
		dbg_frame("�յ���һ�����Ϊ %d ��֡,�������ŵ�֡�Ѿ�����(�ط���֡).\n", recv_lowerbound, recv_upperbound);
		return;
	}
	recv_buffer[seq % RECVWINDOW] = frame_ptr;
	recv_buffer_lengthes[seq % RECVWINDOW] = length_of_frame;
	dbg_frame("�ɹ��յ���һ�����Ϊ %d ��֡, ����Ϊ%d. \n",seq,length_of_frame);
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
				dbg_warning("�ָ�����ͬ��,�����������߼�!.\n");
				exit(-1);
			}
		}
		if (j != i + f)break;
		
		//������,���ָ������ݺϲ�Ϊһ��֡
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
		dbg_event("�ѽ��������һ��package ����idΪ %d ����ͷ��֡idΪ %d .\n", inner_id,i);
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
	init(); //��ʼ����ȫ�ֱ���
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
					dbg_event("*****�����ʽϸ�,�رշ�Χack����,����֡����*****\n");
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