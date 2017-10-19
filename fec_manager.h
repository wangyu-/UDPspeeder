/*
 * fec_manager.h
 *
 *  Created on: Sep 27, 2017
 *      Author: root
 */

#ifndef FEC_MANAGER_H_
#define FEC_MANAGER_H_

#include "common.h"
#include "log.h"
#include "lib/rs.h"

const int max_blob_packet_num=30000;//how many packet can be contain in a blob_t ,can be set very large
const u32_t anti_replay_buff_size=30000;//can be set very large

const int max_fec_packet_num=255;// this is the limitation of the rs lib
extern u32_t fec_buff_num;


struct anti_replay_t
{

	u64_t replay_buffer[anti_replay_buff_size];
	unordered_set<u32_t> st;
	int index;
	anti_replay_t()
	{
		memset(replay_buffer,-1,sizeof(replay_buffer));
		st.rehash(anti_replay_buff_size*3);
		index=0;
	}
	void set_invaild(u32_t seq)
	{

		if(st.find(seq)!=st.end() )
		{
			mylog(log_trace,"seq %u exist\n",seq);
			return;
			//return 0;
		}
		if(replay_buffer[index]!=u64_t(i64_t(-1)))
		{
			assert(st.find(replay_buffer[index])!=st.end());
			st.erase(replay_buffer[index]);
		}
		replay_buffer[index]=seq;
		st.insert(seq);
		index++;
		if(index==int(anti_replay_buff_size)) index=0;
		//return 1; //for complier check
	}
	int is_vaild(u32_t seq)
	{
		return st.find(seq)==st.end();
	}
};

struct blob_encode_t
{
	char input_buf[(max_fec_packet_num+5)*buf_len];
	int current_len;
	int counter;

	char *output_buf[max_fec_packet_num+100];

	blob_encode_t();

    int clear();

    int get_num();
    int get_shard_len(int n);
    int get_shard_len(int n,int next_packet_len);

	int input(char *s,int len);  //len=use len=0 for second and following packet
	int output(int n,char ** &s_arr,int & len);
};

struct blob_decode_t
{
	char input_buf[(max_fec_packet_num+5)*buf_len];
	int current_len;
	int last_len;
	int counter;

	char *output_buf[max_blob_packet_num+100];
	int output_len[max_blob_packet_num+100];

	blob_decode_t();
	int clear();
	int input(char *input,int len);
	int output(int &n,char ** &output,int *&len_arr);
};

class fec_encode_manager_t
{
private:
	u32_t seq;

	int type;
	int fec_data_num,fec_redundant_num;
	int fec_mtu;
	int fec_pending_num;
	int fec_pending_time;

	my_time_t first_packet_time;
	my_time_t first_packet_time_for_output;


	blob_encode_t blob_encode;
	char input_buf[max_fec_packet_num+5][buf_len];
	int input_len[max_fec_packet_num+100];

	char *output_buf[max_fec_packet_num+100];
	int output_len[max_fec_packet_num+100];

	int counter;
	int timer_fd;
	u64_t timer_fd64;

	int ready_for_output;
	u32_t output_n;


	int append(char *s,int len);

public:
	fec_encode_manager_t();
	~fec_encode_manager_t();

	my_time_t get_first_packet_time()
	{
		return first_packet_time_for_output;
	}

	int get_type()
	{
		return type;
	}
	u64_t get_timer_fd64();
	int re_init(int data_num,int redundant_num,int mtu,int pending_num,int pending_time,int type);
	int input(char *s,int len/*,int &is_first_packet*/);
	int output(int &n,char ** &s_arr,int *&len);
};
struct fec_data_t
{
	int used;
	u32_t seq;
	int type;
	int data_num;
	int redundant_num;
	int idx;
	char buf[buf_len];
	int len;
};
struct fec_group_t
{
	int type=-1;
	int data_num=-1;
	int redundant_num=-1;
	int len=-1;
	//int data_counter=0;
	map<int,int>  group_mp;
};
class fec_decode_manager_t
{
	anti_replay_t anti_replay;
	fec_data_t *fec_data;
	unordered_map<u32_t, fec_group_t> mp;
	blob_decode_t blob_decode;

	int index;

	int output_n;
	char ** output_s_arr;
	int * output_len_arr;
	int ready_for_output;

	char *output_s_arr_buf[max_fec_packet_num+100];//only for type=1,for type=0 the buf inside blot_t is used
	int output_len_arr_buf[max_fec_packet_num+100];//same

public:
	fec_decode_manager_t()
	{
		fec_data=new fec_data_t[fec_buff_num+5];
		re_init();
	}
	fec_decode_manager_t(const fec_decode_manager_t &b)
	{
		assert(0==1);//not allowed to copy
	}
	~fec_decode_manager_t()
	{
		delete fec_data;
	}
	int re_init();
	int input(char *s,int len);
	int output(int &n,char ** &s_arr,int* &len_arr);
};

#endif /* FEC_MANAGER_H_ */
