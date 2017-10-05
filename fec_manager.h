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

struct fec_encode_manager_t
{
	int fec_data_num,fec_redundant_num;
	int fec_mtu;
	char buf[256+5][buf_len+100];
	char *output_buf[256+5];
	int output_len;
	int ready_for_output;

	int counter;

	blob_encode_t blob_encode;
	fec_encode_manager_t();
	int re_init();
	int input(char *s,int len,int &is_first_packet);
	int output(int &n,char ** &s_arr,int &len);
};
struct fec_data_t
{
	int used;
	u32_t seq;
	int data_num;
	int redundant_num;
	int idx;
	int type;
	char buf[buf_len];
	int len;
};
struct fec_decode_manager_t
{
	anti_replay_t anti_replay;
	fec_data_t fec_data[fec_buff_size];
	int index;
	unordered_map<u32_t, map<int,int> > mp;
	blob_decode_t blob_decode;

	fec_decode_manager_t()
	{
		for(int i=0;i<(int)fec_buff_size;i++)
			fec_data[i].used=0;
		ready_for_output=0;
	}

	int output_n;
	char ** output_s_arr;
	int * output_len_arr;

	int ready_for_output;
	int input(char *s,int len)
	{
		char *ori_s=s;
		u32_t seq=read_u32(s);
		s+=sizeof(u32_t);
		int data_num=(unsigned char)*(s++);
		int redundant_num=(unsigned char)*(s++);
		int innder_index=(unsigned char)*(s++);
		int type=(unsigned char)*(s++);
		len=len-int(s-ori_s);
		if(len<0)
		{
			return -1;
		}

		if(!anti_replay.is_vaild(seq))
		{
			return 0;
		}
		if(!mp[seq].empty())
		{
			int tmp_idx=mp[seq].begin()->second;
			int ok=1;
			if(data_num+redundant_num>255)
				ok=0;
			if(fec_data[tmp_idx].data_num!=data_num||fec_data[tmp_idx].redundant_num!=redundant_num||fec_data[tmp_idx].len!=len)
			{
				ok=0;
			}
			if(ok==0)
			{
				return 0;
			}
		}
		if(fec_data[index].used!=0)
		{
			int tmp_seq=fec_data[index].seq;
			anti_replay.set_invaild(tmp_seq);
			if(mp.find(tmp_seq)!=mp.end())
			{
				mp.erase(tmp_seq);
			}
		}

		fec_data[index].used=1;
		fec_data[index].seq=seq;
		fec_data[index].data_num=data_num;
		fec_data[index].redundant_num=redundant_num;
		fec_data[index].idx=innder_index;
		fec_data[index].type=type;
		fec_data[index].len=len;
		mp[seq][innder_index]=index;


		map<int,int> &inner_mp=mp[seq];
		if((int)inner_mp.size()>=data_num)
		{
			anti_replay.set_invaild(seq);
			char *fec_tmp_arr[256+5]={0};
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				fec_tmp_arr[it->first]=fec_data[it->second].buf;
			}
			rs_decode2(data_num,redundant_num,fec_tmp_arr,len);
			blob_decode.clear();
			for(int i=0;i<data_num;i++)
			{
				blob_decode.input(fec_tmp_arr[i],len);
			}
			blob_decode.output(output_n,output_s_arr,output_len_arr);
			ready_for_output=1;
		}

		index++;
		if(index==int(anti_replay_buff_size)) index=0;


		return 0;
	}
	int output(int &n,char ** &s_arr,int* &len_arr)
	{
		if(!ready_for_output)
		{
			n=-1;
			s_arr=0;
			len_arr=0;
		}
		else
		{
			ready_for_output=0;
			n=output_n;
			s_arr=output_s_arr;
			len_arr=output_len_arr;
		}
		return 0;
	}
};

#endif /* FEC_MANAGER_H_ */
