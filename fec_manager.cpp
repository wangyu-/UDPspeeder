/*
 * fec_manager.cpp
 *
 *  Created on: Sep 27, 2017
 *      Author: root
 */

#include "fec_manager.h"
#include "log.h"
#include "common.h"
#include "lib/rs.h"
u32_t seq=0;



blob_encode_t::blob_encode_t()
{
	clear();
}
int blob_encode_t::clear()
{
	counter=0;
	current_len=(int)sizeof(u32_t);
	return 0;
}

int blob_encode_t::get_num()
{
	return counter;
}
int blob_encode_t::get_shard_len(int n)
{
	return round_up_div(current_len,n);
}

int blob_encode_t::get_shard_len(int n,int next_packet_len)
{
	return round_up_div(current_len+(int)sizeof(u16_t)+next_packet_len,n);
}

int blob_encode_t::input(char *s,int len)
{
	assert(current_len+len+sizeof(u16_t) <=256*buf_len);
	assert(len<=65535&&len>=0);
	counter++;
	assert(counter<=max_packet_num);
	write_u16(buf+current_len,len);
	current_len+=sizeof(u16_t);
	memcpy(buf+current_len,s,len);
	current_len+=len;
	return 0;
}

int blob_encode_t::output(int n,char ** &s_arr,int & len)
{
	static char *output_arr[256+100];
	len=round_up_div(current_len,n);
	write_u32(buf,counter);
	for(int i=0;i<n;i++)
	{
		output_arr[i]=buf+len*i;
	}
	s_arr=output_arr;
	return 0;
}
blob_decode_t::blob_decode_t()
{
	clear();

}
int blob_decode_t::clear()
{
	current_len=0;
	last_len=-1;
	counter=0;
	return 0;
}
int blob_decode_t::input(char *s,int len)
{
	if(last_len!=-1)
	{
		assert(last_len==len);
	}
	counter++;
	assert(counter<=256);
	last_len=len;
	assert(current_len+len+100<(int)sizeof(buf));
	memcpy(buf+current_len,s,len);
	current_len+=len;
	return 0;
}
int blob_decode_t::output(int &n,char ** &s_arr,int *&len_arr)
{
	static char *s_buf[max_packet_num+100];
	static int len_buf[max_packet_num+100];

	int parser_pos=0;

	if(parser_pos+(int)sizeof(u32_t)>current_len) return -1;

	n=(int)read_u32(buf+parser_pos);
	if(n>max_packet_num) {mylog(log_info,"failed 1\n");return -1;}
	s_arr=s_buf;
	len_arr=len_buf;

	parser_pos+=sizeof(u32_t);
	for(int i=0;i<n;i++)
	{
		if(parser_pos+(int)sizeof(u16_t)>current_len) {mylog(log_info,"failed2 \n");return -1;}
		len_arr[i]=(int)read_u16(buf+parser_pos);
		parser_pos+=(int)sizeof(u16_t);
		if(parser_pos+len_arr[i]>current_len) {mylog(log_info,"failed 3 %d  %d %d\n",parser_pos,len_arr[i],current_len);return -1;}
		s_arr[i]=buf+parser_pos;
		parser_pos+=len_arr[i];
	}
	return 0;
}

fec_encode_manager_t::fec_encode_manager_t()
{
	re_init();
}
int fec_encode_manager_t::re_init()
{
	fec_data_num=4;
	fec_redundant_num=2;
	fec_mtu=1200;

	counter=0;
	blob_encode.clear();
	ready_for_output=0;
	return 0;
}
int fec_encode_manager_t::input(char *s,int len,int &is_first_packet)
{
	is_first_packet=0;
    if(s==0 ||blob_encode.get_shard_len(fec_data_num,len)>=fec_mtu)
	{
    	char ** blob_output;
    	int blob_len;
    	assert(counter!=0);
    	blob_encode.output(fec_data_num,blob_output,blob_len);
    	for(int i=0;i<fec_data_num+fec_redundant_num;i++)
    	{
    		int tmp_idx=0;
    		write_u32(buf[i]+tmp_idx,seq);
    		tmp_idx+=sizeof(u32_t);
    		buf[i][tmp_idx++]=(unsigned char)fec_data_num;
    		buf[i][tmp_idx++]=(unsigned char)fec_redundant_num;
    		buf[i][tmp_idx++]=(unsigned char)i;
    		buf[i][tmp_idx++]=(unsigned char)0;
    		if(i<fec_data_num)
    		{
    			memcpy(buf[i]+tmp_idx,blob_output[i],blob_len);
    			tmp_idx+=blob_len;
    		}
    		output_buf[i]=buf[i]+sizeof(u32_t)+3*sizeof(char);

    	}
    	output_len=blob_len+sizeof(u32_t)+3*sizeof(char);
		rs_encode2(fec_data_num,fec_data_num+fec_redundant_num,output_buf,blob_len);
		for(int i=0;i<fec_data_num+fec_redundant_num;i++)
		{
			output_buf[i]=buf[i];
		}

    	ready_for_output=1;
    	seq++;
    	counter=0;
    	blob_encode.clear();
	}

    if(s!=0)
    {
    	if(counter==0) is_first_packet=1;
    	blob_encode.input(s,len);
    	counter++;
    }

	return 0;
}

int fec_encode_manager_t::output(int &n,char ** &s_arr,int &len)
{
	if(!ready_for_output)
	{
		n=-1;
		len=-1;
		s_arr=0;
	}
	else
	{
		n=fec_data_num+fec_redundant_num;
		len=output_len;
		s_arr=output_buf;
		ready_for_output=0;
	}
	return 0;
}

/*
int fec_decode_manager_t::input(char *s,int l)
{
	return 0;
}

int fec_decode_manager_t::output(int &n,char ** &s_arr,int* &l_arr)
{
	return 0;
}*/

