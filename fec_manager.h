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

const int max_packet_num=1000;

const u32_t anti_replay_buff_size=30000;
const u32_t fec_buff_size=3000;


struct anti_replay_t
{

	u64_t replay_buffer[anti_replay_buff_size];
	unordered_set<u32_t> st;
	int index;
	anti_replay_t()
	{
		memset(replay_buffer,-1,sizeof(replay_buffer));
		st.rehash(anti_replay_buff_size*10);
		index=0;
	}
	void set_invaild(u32_t seq)
	{

		if(st.find(seq)!=st.end() )
		{
			mylog(log_trace,"seq %llx exist\n",seq);
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
	char buf[(256+5)*buf_len];
	int current_len;
	int counter;

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
	char buf[(256+5)*buf_len];
	int current_len;
	int last_len;
	int counter;

	blob_decode_t();
	int clear();
	int input(char *input,int len);
	int output(int &n,char ** &output,int *&len_arr);
};

class fec_encode_manager_t
{
	int fec_data_num,fec_redundant_num;
	int fec_mtu;
	char buf[256+5][buf_len+100];
	char *output_buf[256+5];
	int output_len;
	int ready_for_output;
	u32_t seq;
	int counter;

	blob_encode_t blob_encode;
public:
	fec_encode_manager_t();
	int re_init(int data_num,int redundant_num,int mtu);
	int input(char *s,int len/*,int &is_first_packet*/);
	int output(int &n,char ** &s_arr,int &len);
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
class fec_decode_manager_t
{
	anti_replay_t anti_replay;
	fec_data_t fec_data[fec_buff_size];
	int index;
	unordered_map<u32_t, map<int,int> > mp;
	blob_decode_t blob_decode;

	int output_n;
	char ** output_s_arr;
	int * output_len_arr;
	int ready_for_output;
public:
	fec_decode_manager_t();
	int re_init();
	int input(char *s,int len);
	int output(int &n,char ** &s_arr,int* &len_arr);
};

#endif /* FEC_MANAGER_H_ */
