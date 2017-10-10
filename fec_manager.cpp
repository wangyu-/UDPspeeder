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
#include "fd_manager.h"

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
	assert(current_len+len+sizeof(u16_t) <=max_fec_packet_num*buf_len);
	assert(len<=65535&&len>=0);
	counter++;
	assert(counter<=max_normal_packet_num);
	write_u16(buf+current_len,len);
	current_len+=sizeof(u16_t);
	memcpy(buf+current_len,s,len);
	current_len+=len;
	return 0;
}

int blob_encode_t::output(int n,char ** &s_arr,int & len)
{
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
	assert(counter<=max_fec_packet_num);
	last_len=len;
	assert(current_len+len+100<(int)sizeof(buf));
	memcpy(buf+current_len,s,len);
	current_len+=len;
	return 0;
}
int blob_decode_t::output(int &n,char ** &s_arr,int *&len_arr)
{

	int parser_pos=0;

	if(parser_pos+(int)sizeof(u32_t)>current_len) return -1;

	n=(int)read_u32(buf+parser_pos);
	if(n>max_normal_packet_num) {mylog(log_info,"failed 1\n");return -1;}
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
	//int timer_fd;
	if ((timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
	{
		mylog(log_fatal,"timer_fd create error");
		myexit(1);
	}
	timer_fd64=fd_manager.create(timer_fd);

	re_init(4,2,1200,100,10000,0);
}
fec_encode_manager_t::~fec_encode_manager_t()
{
	fd_manager.fd64_close(timer_fd64);
}
u64_t fec_encode_manager_t::get_timer_fd64()
{
	return timer_fd64;
}
int fec_encode_manager_t::re_init(int data_num,int redundant_num,int mtu,int pending_num,int pending_time,int type)
{
	fec_data_num=data_num;
	fec_redundant_num=redundant_num;
	fec_mtu=mtu;
	fec_pending_num=pending_num;
	fec_pending_time=pending_time;
	this->type=type;

	counter=0;
	blob_encode.clear();
	ready_for_output=0;
	seq=0;

	itimerspec zero_its;
	memset(&zero_its, 0, sizeof(zero_its));

	timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &zero_its, 0);

	return 0;
}
int fec_encode_manager_t::append(char *s,int len/*,int &is_first_packet*/)
{
	if(counter==0)
	{
		itimerspec its;
		memset(&its.it_interval,0,sizeof(its.it_interval));
		my_time_t tmp_time=fec_pending_time+get_current_time_us();
		its.it_value.tv_sec=tmp_time/1000000llu;
		its.it_value.tv_nsec=(tmp_time%1000000llu)*1000llu;
		timerfd_settime(timer_fd,TFD_TIMER_ABSTIME,&its,0);
	}
	if(type==0)
	{
		blob_encode.input(s,len);
	}
	else if(type==1)
	{
		mylog(log_info,"counter=%d\n",counter);
		assert(len<=65535&&len>=0);
		char * p=buf[counter]+sizeof(u32_t)+4*sizeof(char);
		write_u16(p,(u16_t)((u32_t)len));
		p+=sizeof(u16_t);
		memcpy(p,s,len);//remember to change this,if protocol is modified
		buf_s_len[counter]=len+sizeof(u16_t);
	}
	else
	{
		assert(0==1);
	}
	counter++;
	return 0;
}
int fec_encode_manager_t::input(char *s,int len/*,int &is_first_packet*/)
{
	int about_to_fec=0;
	int delayed_append=0;
	if(type==0&& s!=0 &&counter==0&&blob_encode.get_shard_len(fec_data_num,len)>=fec_mtu)
	{
		mylog(log_warn,"message too long len=%d,ignored\n",len);
		return -1;
	}
	if(type==1&&s!=0&&len>=fec_mtu)
	{
		mylog(log_warn,"message too long len=%d,ignored\n",len);
		return -1;
	}
	if(s==0) about_to_fec=1;//now

	assert(type==0||type==1);
	if(type==0&& blob_encode.get_shard_len(fec_data_num,len)>=fec_mtu) {about_to_fec=1; delayed_append=1;}//fec then add packet
	if(type==0) assert(counter<fec_pending_num);
	if(type==1) assert(counter<fec_data_num);


	if(s!=0&&!delayed_append)
	{
		append(s,len);
	}

	if(type==0&& counter==fec_pending_num) {about_to_fec=1;} //


	if(type==1&& counter==fec_data_num) about_to_fec=1;


    if(about_to_fec)
	{
    	char ** blob_output;
    	int fec_len=-1;
    	mylog(log_debug,"counter=%d\n",counter);
    	if(counter==0)
    	{
    		mylog(log_warn,"unexpected counter==0\n");
    		return -1;
    	}

    	int actual_data_num;
    	int actual_redundant_num;

    	if(type==0)
    	{
    		actual_data_num=fec_data_num;
    		actual_redundant_num=fec_redundant_num;

        	blob_encode.output(actual_data_num,blob_output,fec_len);
    	}
    	else
    	{
    		actual_data_num=counter;
    		actual_redundant_num=fec_redundant_num;

    		for(int i=0;i<counter;i++)
    		{
    			assert(buf_s_len[i]>=0);
    			if(buf_s_len[i]>fec_len) fec_len=buf_s_len[i];
    		}

    	}
    	mylog(log_info,"%d %d %d\n",actual_data_num,actual_redundant_num,fec_len);

    	char *tmp_output_buf[max_fec_packet_num+5]={0};
    	for(int i=0;i<actual_data_num+actual_redundant_num;i++)
    	{
    		int tmp_idx=0;
    		write_u32(buf[i]+tmp_idx,seq);
    		tmp_idx+=sizeof(u32_t);
    		buf[i][tmp_idx++]=(unsigned char)type;
    		buf[i][tmp_idx++]=(unsigned char)actual_data_num;
    		buf[i][tmp_idx++]=(unsigned char)actual_redundant_num;
    		buf[i][tmp_idx++]=(unsigned char)i;

    		tmp_output_buf[i]=buf[i]+tmp_idx; //////caution ,trick here.

    		if(type==0)
    		{
        		output_len[i]=tmp_idx+fec_len;
        		if(i<actual_data_num)
        		{
        			memcpy(buf[i]+tmp_idx,blob_output[i],fec_len);
        		}
    		}
    		else
    		{
    			if(i<actual_data_num)
    			{
    				output_len[i]=tmp_idx+buf_s_len[i];
    				memset(tmp_output_buf[i]+buf_s_len[i],0,fec_len-buf_s_len[i]);
    			}
    			else
    				output_len[i]=tmp_idx+fec_len;

    		}
    		output_buf[i]=buf[i];

    	}
    	//output_len=blob_len+sizeof(u32_t)+4*sizeof(char);/////remember to change this 4,if modified the protocol
		rs_encode2(actual_data_num,actual_data_num+actual_redundant_num,tmp_output_buf,fec_len);

		mylog(log_info,"!!! s= %d\n");
    	ready_for_output=1;
    	seq++;
    	counter=0;
    	output_n=actual_data_num+actual_redundant_num;
    	blob_encode.clear();


		itimerspec its;
		memset(&its,0,sizeof(its));
		timerfd_settime(timer_fd,TFD_TIMER_ABSTIME,&its,0);
	}

	if(s!=0&&delayed_append)
	{
		assert(type!=1);
		append(s,len);
	}

	return 0;
}

int fec_encode_manager_t::output(int &n,char ** &s_arr,int *&len)
{
	if(!ready_for_output)
	{
		n=-1;
		len=0;
		s_arr=0;
	}
	else
	{
		n=output_n;
		len=output_len;
		s_arr=output_buf;
		ready_for_output=0;
	}
	return 0;
}

fec_decode_manager_t::fec_decode_manager_t()
{
	re_init();
}

int fec_decode_manager_t::re_init()
{
	for(int i=0;i<(int)fec_buff_size;i++)
		fec_data[i].used=0;
	ready_for_output=0;
	return 0;
}

int fec_decode_manager_t::input(char *s,int len)
{
	assert(s!=0);
	int tmp_idx=0;
	u32_t seq=read_u32(s+tmp_idx);
	tmp_idx+=sizeof(u32_t);
	int type=(unsigned char)s[tmp_idx++];
	int data_num=(unsigned char)s[tmp_idx++];
	int redundant_num=(unsigned char)s[tmp_idx++];
	int inner_index=(unsigned char)s[tmp_idx++];
	len=len-tmp_idx;

	if(len<0)
	{
		mylog(log_warn,"len<0\n");
		return -1;
	}
	if(type==1&&len<(int)sizeof(u16_t))
	{
		mylog(log_warn,"type==1&&len<2\n");
		return -1;
	}
	if(type==1)
	{
		if(inner_index<data_num&&(int)( read_u16(s+tmp_idx)+sizeof(u16_t))!=len)
		{
			mylog(log_warn,"inner_index<data_num&&read_u16(s+tmp_idx)+sizeof(u16_t)!=len    %d %d\n",(int)( read_u16(s+tmp_idx)+sizeof(u16_t)),len);
			return -1;
		}
	}

	if(data_num+redundant_num>max_fec_packet_num)
	{
		return -1;
	}
	if(!anti_replay.is_vaild(seq))
	{
		return 0;
	}
	if(!mp[seq].empty())
	{
		int first_idx=mp[seq].begin()->second;
		int ok=1;
		if(fec_data[first_idx].data_num!=data_num)
			ok=0;
		if(fec_data[first_idx].redundant_num!=redundant_num)
			ok=0;
		if(fec_data[first_idx].type!=type)
			ok=0;
		if(type==0&&fec_data[first_idx].len!=len)
			ok=0;
		if(ok==0)
		{
			return -1;
		}
	}

	if(fec_data[index].used!=0)
	{
		u32_t tmp_seq=fec_data[index].seq;
		anti_replay.set_invaild(tmp_seq);
		if(mp.find(tmp_seq)!=mp.end())
		{
			mp.erase(tmp_seq);
		}
		if(tmp_seq==seq)
		{
			return -1;
		}
	}

	fec_data[index].used=1;
	fec_data[index].seq=seq;
	fec_data[index].type=type;
	fec_data[index].data_num=data_num;
	fec_data[index].redundant_num=redundant_num;
	fec_data[index].idx=inner_index;
	fec_data[index].len=len;
	memcpy(fec_data[index].buf,s+tmp_idx,len);
	mp[seq][inner_index]=index;

	map<int,int> &inner_mp=mp[seq];
	assert((int)inner_mp.size()<=data_num);
	if((int)inner_mp.size()==data_num)
	{
		if(type==0)
		{
			char *fec_tmp_arr[max_fec_packet_num+5]={0};
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				fec_tmp_arr[it->first]=fec_data[it->second].buf;
			}
			rs_decode2(data_num,data_num+redundant_num,fec_tmp_arr,len); //the input data has been modified in-place
			blob_decode.clear();
			for(int i=0;i<data_num;i++)
			{
				blob_decode.input(fec_tmp_arr[i],len);
			}
			blob_decode.output(output_n,output_s_arr,output_len_arr);
			assert(ready_for_output==0);
			ready_for_output=1;
			anti_replay.set_invaild(seq);
		}
		else
		{
			int max_len=-1;
			int fec_ok=1;
			int debug_num=inner_mp.size();
			//outupt_s_arr_buf[max_fec_packet_num+5]={0};

			//memset(output_s_arr_buf,0,sizeof(output_s_arr_buf));//in efficient

			for(int i=0;i<data_num+redundant_num;i++)
			{
				output_s_arr_buf[i]=0;
			}
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				output_s_arr_buf[it->first]=fec_data[it->second].buf;
				assert(fec_data[it->second].len>=(int)sizeof(u16_t));

				if(fec_data[it->second].len > max_len)
					max_len=fec_data[it->second].len;
			}
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				memset(fec_data[it->second].buf+fec_data[it->second].len,0,max_len-fec_data[it->second].len);
			}
			int missed_packet[max_fec_packet_num+5];
			int missed_packet_counter=0;


			for(int i=0;i<data_num;i++)
			{
				if(output_s_arr_buf[i]==0 ||i==inner_index) //only missed packet +current packet
				{
					missed_packet[missed_packet_counter++]=i;
				}
			}
			rs_decode2(data_num,data_num+redundant_num,output_s_arr_buf,max_len);
			for(int i=0;i<data_num;i++)
			{
				output_len_arr_buf[i]=read_u16(output_s_arr_buf[i]);
				output_s_arr_buf[i]+=sizeof(u16_t);
				if(output_len_arr_buf[i]>max_data_len)
				{
					mylog(log_warn,"invaild len %d,seq= %u,data_num= %d r_num= %d,i= %d\n",output_len_arr_buf[i],seq,data_num,redundant_num,i);
					fec_ok=0;
					for(int i=0;i<missed_packet_counter;i++)
					{
						log_bare(log_warn,"%d ",missed_packet[i]);
					}
					log_bare(log_warn,"\n");
					//break;
				}
			}
			if(fec_ok)
			{

				//output_n=data_num;

				output_n=missed_packet_counter;
				for(int i=0;i<missed_packet_counter;i++)
				{
					output_s_arr_buf[i]=output_s_arr_buf[missed_packet[i]];
					output_len_arr_buf[i]=output_len_arr_buf[missed_packet[i]];
				}


				output_s_arr=output_s_arr_buf;
				output_len_arr=output_len_arr_buf;
				assert(ready_for_output==0);
				ready_for_output=1;
			}
			else
			{
				ready_for_output=0;
			}
			anti_replay.set_invaild(seq);
		}
	}
	else
	{

		if(type==1&&inner_index<data_num)
		{
			assert(ready_for_output==0);
			output_n=1;
			int check_len=read_u16(fec_data[index].buf);
			output_s_arr_buf[0]=fec_data[index].buf+sizeof(u16_t);
			output_len_arr_buf[0]=fec_data[index].len-sizeof(u16_t);

			if(output_len_arr_buf[0]!=check_len)
			{
				mylog(log_warn,"len mismatch %d %d\n",output_len_arr_buf[0],check_len);
			}
			output_s_arr=output_s_arr_buf;
			output_len_arr=output_len_arr_buf;

			ready_for_output=1;
		}
	}

	index++;
	if(index==int(anti_replay_buff_size)) index=0;

	return 0;
}
int fec_decode_manager_t::output(int &n,char ** &s_arr,int* &len_arr)
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
