/*
 * packet.cpp
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */


#include "common.h"
#include "log.h"

int iv_min=2;
int iv_max=16;//< 256;
u64_t packet_send_count=0;
u64_t dup_packet_send_count=0;
u64_t packet_recv_count=0;
u64_t dup_packet_recv_count=0;
typedef u64_t anti_replay_seq_t;
const u32_t anti_replay_buff_size=10000;
int disable_replay_filter=0;

int random_drop=0;

char key_string[1000]= "secret key";

struct anti_replay_t
{
	u64_t max_packet_received;

	u64_t replay_buffer[anti_replay_buff_size];
	unordered_set<u64_t> st;
	u32_t const_id;
	u32_t anti_replay_seq;
	int index;
	anti_replay_seq_t get_new_seq_for_send()
	{
		if(const_id==0) prepare();
		anti_replay_seq_t res=const_id;
		res<<=32u;
		anti_replay_seq++;
		res|=anti_replay_seq;
		const_id=0;
		return res;
	}
	void prepare()
	{
		anti_replay_seq=get_true_random_number();//random first seq
		const_id=get_true_random_number_nz();
	}
	anti_replay_t()
	{
		memset(replay_buffer,0,sizeof(replay_buffer));
		st.rehash(anti_replay_buff_size*10);
		max_packet_received=0;
		index=0;
	}

	int is_vaild(u64_t seq)
	{
		if(const_id==0) prepare();
		//if(disable_replay_filter) return 1;
		if(seq==0)
		{
			mylog(log_debug,"seq=0\n");
			return 0;
		}
		if(st.find(seq)!=st.end() )
		{
			mylog(log_trace,"seq %llx exist\n",seq);
			return 0;
		}

		if(replay_buffer[index]!=0)
		{
			assert(st.find(replay_buffer[index])!=st.end());
			st.erase(replay_buffer[index]);
		}
		replay_buffer[index]=seq;
		st.insert(seq);
		index++;
		if(index==int(anti_replay_buff_size)) index=0;

		return 1; //for complier check
	}
}anti_replay;

void encrypt_0(char * input,int &len,char *key)
{
	int i,j;
	if(key[0]==0) return;
	for(i=0,j=0;i<len;i++,j++)
	{
		if(key[j]==0)j=0;
		input[i]^=key[j];
	}
}
void decrypt_0(char * input,int &len,char *key)
{

	int i,j;
	if(key[0]==0) return;
	for(i=0,j=0;i<len;i++,j++)
	{
		if(key[j]==0)j=0;
		input[i]^=key[j];
	}
}
int add_seq(char * data,int &data_len )
{
	if(data_len<0) return -1;
	anti_replay_seq_t seq=anti_replay.get_new_seq_for_send();
	seq=hton64(seq);
	memcpy(data+data_len,&seq,sizeof(seq));
	data_len+=sizeof(seq);
	return 0;
}
int remove_seq(char * data,int &data_len)
{
	anti_replay_seq_t seq;
	if(data_len<int(sizeof(seq))) return -1;
	data_len-=sizeof(seq);
	memcpy(&seq,data+data_len,sizeof(seq));
	seq=ntoh64(seq);
	if(anti_replay.is_vaild(seq)==0)
	{
		if(disable_replay_filter==1)  //todo inefficient code,why did i put it here???
			return 0;
		mylog(log_trace,"seq %llx dropped bc of replay-filter\n ",seq);
		return -1;
	}
	packet_recv_count++;
	return 0;
}
int do_obscure(const char * input, int in_len,char *output,int &out_len)
{
	//memcpy(output,input,in_len);
//	out_len=in_len;
	//return 0;

	int i, j, k;
	if (in_len > 65535||in_len<0)
		return -1;
	int iv_len=iv_min+rand()%(iv_max-iv_min);
	get_true_random_chars(output,iv_len);
	memcpy(output+iv_len,input,in_len);

	output[iv_len+in_len]=(uint8_t)iv_len;

	output[iv_len+in_len]^=output[0];
	output[iv_len+in_len]^=key_string[0];

	for(i=0,j=0,k=1;i<in_len;i++,j++,k++)
	{
		if(j==iv_len) j=0;
		if(key_string[k]==0)k=0;
		output[iv_len+i]^=output[j];
		output[iv_len+i]^=key_string[k];
	}


	out_len=iv_len+in_len+1;
	return 0;
}
int de_obscure(const char * input, int in_len,char *output,int &out_len)
{
	//memcpy(output,input,in_len);
	//out_len=in_len;
	//return 0;

	int i, j, k;
	if (in_len > 65535||in_len<0)
	{
		mylog(log_debug,"in_len > 65535||in_len<0 ,  %d",in_len);
		return -1;
	}
	int iv_len= int ((uint8_t)(input[in_len-1]^input[0]^key_string[0]) );
	out_len=in_len-1-iv_len;
	if(out_len<0)
	{
		mylog(log_debug,"%d %d\n",in_len,out_len);
		return -1;
	}
	for(i=0,j=0,k=1;i<in_len;i++,j++,k++)
	{
		if(j==iv_len) j=0;
		if(key_string[k]==0)k=0;
		output[i]=input[iv_len+i]^input[j]^key_string[k];

	}
	dup_packet_recv_count++;
	return 0;
}


int sendto_u64 (int fd,char * buf, int len,int flags, u64_t u64)
{

	if(is_server)
	{
		dup_packet_send_count++;
	}
	if(is_server&&random_drop!=0)
	{
		if(get_true_random_number()%10000<(u32_t)random_drop)
		{
			return 0;
		}
	}

	sockaddr_in tmp_sockaddr;

	memset(&tmp_sockaddr,0,sizeof(tmp_sockaddr));
	tmp_sockaddr.sin_family = AF_INET;
	tmp_sockaddr.sin_addr.s_addr = (u64 >> 32u);

	tmp_sockaddr.sin_port = htons(uint16_t((u64 << 32u) >> 32u));

	return sendto(fd, buf,
			len , 0,
			(struct sockaddr *) &tmp_sockaddr,
			sizeof(tmp_sockaddr));
}

int send_fd (int fd,char * buf, int len,int flags)
{
	if(is_client)
	{
		dup_packet_send_count++;
	}
	if(is_client&&random_drop!=0)
	{
		if(get_true_random_number()%10000<(u32_t)random_drop)
		{
			return 0;
		}
	}
	return send(fd,buf,len,flags);
}

