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

//int g_fec_data_num=20;
//int g_fec_redundant_num=10;
//int g_fec_mtu=1250;
//int g_fec_queue_len=200;
//int g_fec_timeout=8*1000; //8ms
//int g_fec_mode=0;

fec_parameter_t g_fec_par;

int debug_fec_enc=0;
int debug_fec_dec=0;
//int dynamic_update_fec=1;

const int encode_fast_send=1;
const int decode_fast_send=1;

int short_packet_optimize=1;
int header_overhead=40;

u32_t fec_buff_num=2000;// how many packet can fec_decode_manager hold. shouldnt be very large,or it will cost huge memory


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
	assert(current_len+len+sizeof(u16_t) +100<sizeof(input_buf));
	assert(len<=65535&&len>=0);
	counter++;
	assert(counter<=max_blob_packet_num);
	write_u16(input_buf+current_len,len);
	current_len+=sizeof(u16_t);
	memcpy(input_buf+current_len,s,len);
	current_len+=len;
	return 0;
}

int blob_encode_t::output(int n,char ** &s_arr,int & len)
{
	len=round_up_div(current_len,n);
	write_u32(input_buf,counter);
	for(int i=0;i<n;i++)
	{
		output_buf[i]=input_buf+len*i;
	}
	s_arr=output_buf;
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
	assert(current_len+len+100<(int)sizeof(input_buf));//avoid overflow
	memcpy(input_buf+current_len,s,len);
	current_len+=len;
	return 0;
}
int blob_decode_t::output(int &n,char ** &s_arr,int *&len_arr)
{

	int parser_pos=0;

	if(parser_pos+(int)sizeof(u32_t)>current_len) {mylog(log_info,"failed 0\n");return -1;}

	n=(int)read_u32(input_buf+parser_pos);
	if(n>max_blob_packet_num) {mylog(log_info,"failed 1\n");return -1;}
	s_arr=output_buf;
	len_arr=output_len;

	parser_pos+=sizeof(u32_t);
	for(int i=0;i<n;i++)
	{
		if(parser_pos+(int)sizeof(u16_t)>current_len) {mylog(log_info,"failed2 \n");return -1;}
		len_arr[i]=(int)read_u16(input_buf+parser_pos);
		parser_pos+=(int)sizeof(u16_t);
		if(parser_pos+len_arr[i]>current_len) {mylog(log_info,"failed 3 %d  %d %d\n",parser_pos,len_arr[i],current_len);return -1;}
		s_arr[i]=input_buf+parser_pos;
		parser_pos+=len_arr[i];
	}
	return 0;
}


fec_encode_manager_t::~fec_encode_manager_t()
{
	clear_all();
	//fd_manager.fd64_close(timer_fd64);
}
/*
u64_t fec_encode_manager_t::get_timer_fd64()
{
	return timer_fd64;
}*/

fec_encode_manager_t::fec_encode_manager_t()
{
	//int timer_fd;

	/*
	if ((timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
	{
		mylog(log_fatal,"timer_fd create error");
		myexit(1);
	}
	timer_fd64=fd_manager.create(timer_fd);*/


	/////reset_fec_parameter(g_fec_data_num,g_fec_redundant_num,g_fec_mtu,g_fec_queue_len,g_fec_timeout,g_fec_mode);

	fec_par.clone(g_fec_par);
	clear_data();

}
/*
int fec_encode_manager_t::reset_fec_parameter(int data_num,int redundant_num,int mtu,int queue_len,int timeout,int mode)
{
	fec_data_num=data_num;
	fec_redundant_num=redundant_num;
	fec_mtu=mtu;
	fec_queue_len=queue_len;
	fec_timeout=timeout;
	fec_mode=mode;

	assert(data_num+redundant_num<max_fec_packet_num);

	//clear();

	clear_data();
	return 0;
}*/
int fec_encode_manager_t::append(char *s,int len/*,int &is_first_packet*/)
{
	if(counter==0)
	{
		first_packet_time=get_current_time_us();

		const double m=1000*1000;

		ev_timer_stop(loop, &timer);
		ev_timer_set(&timer, fec_par.timeout/m,0 );
		ev_timer_start(loop, &timer);
	}
	if(fec_par.mode==0)//for type 0 use blob
	{
		assert(blob_encode.input(s,len)==0);
	}
	else if(fec_par.mode==1)//for tpe 1 use  input_buf and counter
	{
		mylog(log_trace,"counter=%d\n",counter);
		assert(len<=65535&&len>=0);
		//assert(len<=fec_mtu);//relax this limitation
		char * p=input_buf[counter]+sizeof(u32_t)+4*sizeof(char);//copy directly to final position,avoid unnecessary copy.
		//remember to change this,if protocol is modified

		write_u16(p,(u16_t)((u32_t)len));  //TODO  omit this u16 for data packet while sending
		p+=sizeof(u16_t);
		memcpy(p,s,len);
		input_len[counter]=len+sizeof(u16_t);
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
	if(counter==0&&fec_par.version!=g_fec_par.version)
	{
		fec_par.clone(g_fec_par);
	}

	int about_to_fec=0;
	int delayed_append=0;
	//int counter_back=counter;
	assert(fec_par.mode==0||fec_par.mode==1);

	if(fec_par.mode==0&& s!=0 &&counter==0)
	{
		int out_len=blob_encode.get_shard_len(fec_par.get_tail().x,len);
		if(out_len>fec_par.mtu)
		{
			mylog(log_warn,"message too long ori_len=%d out_len=%d fec_mtu=%d,ignored\n",len,out_len,fec_par.mtu);
			return -1;
		}
	}
	if(fec_par.mode==1&&s!=0&&len>fec_par.mtu)
	{
		mylog(log_warn,"mode==1,message len=%d,len>fec_mtu,fec_mtu=%d,packet may not be delivered\n",len,fec_par.mtu);
		//return -1;
	}
	if(s==0&&counter==0)
	{
		mylog(log_warn,"unexpected s==0&&counter==0\n");
		return -1;
	}
	if(s==0) about_to_fec=1;//now

	if(fec_par.mode==0&& blob_encode.get_shard_len(fec_par.get_tail().x,len)>fec_par.mtu) {about_to_fec=1; delayed_append=1;}//fec then add packet

	if(fec_par.mode==0) assert(counter<fec_par.queue_len);//counter will never equal fec_pending_num,if that happens fec should already been done.
	if(fec_par.mode==1) assert(counter<fec_par.get_tail().x);


	if(s!=0&&!delayed_append)
	{
		append(s,len);
	}

	if(fec_par.mode==0&& counter==fec_par.queue_len) about_to_fec=1;

	if(fec_par.mode==1&& counter==fec_par.get_tail().x) about_to_fec=1;


    if(about_to_fec)
	{
    	char ** blob_output=0;
    	int fec_len=-1;
    	mylog(log_trace,"counter=%d\n",counter);

    	if(counter==0)
    	{
    		mylog(log_warn,"unexpected counter==0 here\n");
    		return -1;
    	}

    	int actual_data_num;
    	int actual_redundant_num;

    	if(fec_par.mode==0)
    	{


    		int tail_x=fec_par.get_tail().x;
    		int tail_y=fec_par.get_tail().y;
    		actual_data_num=tail_x;
    		actual_redundant_num=tail_y;

    		if(short_packet_optimize)
    		{
    			u32_t best_len=(blob_encode.get_shard_len(tail_x,0)+header_overhead)*(tail_x+tail_y);
    			int best_data_num=tail_x;
    			assert(tail_x<=fec_par.rs_cnt);
    			for(int i=1;i<tail_x;i++)
    			{
    				assert(fec_par.rs_par[i-1].x==i);
    				int tmp_x=fec_par.rs_par[i-1].x;
    				int tmp_y=fec_par.rs_par[i-1].y;
    				assert(tmp_x==i);
    				u32_t shard_len=blob_encode.get_shard_len(tmp_x,0);
    				if(shard_len>(u32_t)fec_par.mtu) continue;

    				u32_t new_len=(shard_len+header_overhead)*(tmp_x+tmp_y);
    				if(new_len<best_len)
    				{
    					best_len=new_len;
    					best_data_num=tmp_x;
    				}
    			}
    			actual_data_num=best_data_num;
    			assert(best_data_num>=1&&best_data_num<=fec_par.rs_cnt);
    			actual_redundant_num=fec_par.rs_par[best_data_num-1].y;
    		}

        	assert(blob_encode.output(actual_data_num,blob_output,fec_len)==0);

    		if(debug_fec_enc)
    			mylog(log_debug,"[enc]seq=%08x x=%d y=%d len=%d cnt=%d\n",seq,actual_data_num,actual_redundant_num,fec_len,counter);
    		else
    			mylog(log_trace,"[enc]seq=%08x x=%d y=%d len=%d cnt=%d\n",seq,actual_data_num,actual_redundant_num,fec_len,counter);
    	}
    	else
    	{
    		assert(counter<=fec_par.rs_cnt);
    		actual_data_num=counter;
    		actual_redundant_num=fec_par.rs_par[counter-1].y;

    		int sum_ori=0;
    		for(int i=0;i<counter;i++)
    		{
    			sum_ori+=input_len[i];
    			assert(input_len[i]>=0);
    			if(input_len[i]>fec_len) fec_len=input_len[i];
    		}

    		int sum=fec_len*counter;

    		if(debug_fec_enc)
    			mylog(log_debug,"[enc]seq=%08x x=%d y=%d len=%d sum_ori=%d sum=%d\n",seq,actual_data_num,actual_redundant_num,fec_len,sum_ori,sum);
    		else
    			mylog(log_trace,"[enc]seq=%08x x=%d y=%d len=%d sum_ori=%d sum=%d\n",seq,actual_data_num,actual_redundant_num,fec_len,sum_ori,sum);
    	}

    	//mylog(log_trace,"%d %d %d\n",actual_data_num,actual_redundant_num,fec_len);

    	char *tmp_output_buf[max_fec_packet_num+5]={0};
    	for(int i=0;i<actual_data_num+actual_redundant_num;i++)
    	{
    		int tmp_idx=0;

        	write_u32(input_buf[i] + tmp_idx, seq);
			tmp_idx += sizeof(u32_t);
			input_buf[i][tmp_idx++] = (unsigned char) fec_par.mode;
			if (fec_par.mode == 1 && i < actual_data_num)
			{
				input_buf[i][tmp_idx++] = (unsigned char) 0;
				input_buf[i][tmp_idx++] = (unsigned char) 0;
			} else
			{
				input_buf[i][tmp_idx++] = (unsigned char) actual_data_num;
				input_buf[i][tmp_idx++] = (unsigned char) actual_redundant_num;
			}
			input_buf[i][tmp_idx++] = (unsigned char) i;

    		tmp_output_buf[i]=input_buf[i]+tmp_idx; //////caution ,trick here.

    		if(fec_par.mode==0)
    		{
        		output_len[i]=tmp_idx+fec_len;
        		if(i<actual_data_num)
        		{
        			memcpy(input_buf[i]+tmp_idx,blob_output[i],fec_len);
        		}
    		}
    		else
    		{
    			if(i<actual_data_num)
    			{
    				output_len[i]=tmp_idx+input_len[i];
    				memset(tmp_output_buf[i]+input_len[i],0,fec_len-input_len[i]);
    			}
    			else
    				output_len[i]=tmp_idx+fec_len;

    		}
    		output_buf[i]=input_buf[i];//output_buf points to same block of memory with different offset

    	}

    	if(0)
    	{
			printf("seq=%u,fec_len=%d,%d %d,before fec\n",seq,fec_len,actual_data_num,actual_redundant_num);

			for(int i=0;i<actual_data_num;i++)
			{
				printf("{");
				for(int j=0;j<8+fec_len;j++)
				{
					log_bare(log_warn,"0x%02x,",(u32_t)(unsigned char)input_buf[i][j]);
				}
				printf("},\n");
				//log_bare(log_warn,"")
			}
    	}
    	//output_len=blob_len+sizeof(u32_t)+4*sizeof(char);/////remember to change this 4,if modified the protocol
		rs_encode2(actual_data_num,actual_data_num+actual_redundant_num,tmp_output_buf,fec_len);

		if(0)
		{
			printf("seq=%u,fec_len=%d,%d %d,after fec\n",seq,fec_len,actual_data_num,actual_redundant_num);
			for(int i=0;i<actual_data_num+actual_redundant_num;i++)
			{
				printf("{");
				for(int j=0;j<8+fec_len;j++)
				{
					log_bare(log_warn,"0x%02x,",(u32_t)(unsigned char)output_buf[i][j]);
				}
				printf("},\n");
				//log_bare(log_warn,"")
			}
		}

		//mylog(log_trace,"!!! s= %d\n");
		assert(ready_for_output==0);
    	ready_for_output=1;
    	first_packet_time_for_output=first_packet_time;
    	first_packet_time=0;
    	seq++;
    	counter=0;
    	output_n=actual_data_num+actual_redundant_num;
    	blob_encode.clear();

		my_itimerspec its;
		memset(&its,0,sizeof(its));
		ev_timer_stop(loop, &timer);
		//timerfd_settime(timer_fd,TFD_TIMER_ABSTIME,&its,0);

    	if(encode_fast_send&&fec_par.mode==1)
    	{
			int packet_to_send[max_fec_packet_num+5]={0};
			int packet_to_send_counter=0;

			//assert(counter!=0);
			if(s!=0)
				packet_to_send[packet_to_send_counter++]=actual_data_num-1;
			for(int i=actual_data_num;i<actual_data_num+actual_redundant_num;i++)
			{

					packet_to_send[packet_to_send_counter++]=i;
			}
			output_n=packet_to_send_counter;//re write
			for(int i=0;i<packet_to_send_counter;i++)
			{
				output_buf[i]=output_buf[packet_to_send[i]];
				output_len[i]=output_len[packet_to_send[i]];
			}

    	}
	}
    else
    {
    	if(encode_fast_send&&s!=0&&fec_par.mode==1)
    	{
    		assert(counter>=1);
    		assert(counter<=255);
    		int input_buf_idx=counter-1;
    		assert(ready_for_output==0);
    		ready_for_output=1;
    		first_packet_time_for_output=0;
    		output_n=1;


    		int tmp_idx=0;
    		write_u32(input_buf[input_buf_idx]+tmp_idx,seq);
    		tmp_idx+=sizeof(u32_t);

    		input_buf[input_buf_idx][tmp_idx++]=(unsigned char)fec_par.mode;
    		input_buf[input_buf_idx][tmp_idx++]=(unsigned char)0;
    		input_buf[input_buf_idx][tmp_idx++]=(unsigned char)0;
    		input_buf[input_buf_idx][tmp_idx++]=(unsigned char)((u32_t)input_buf_idx);

    		output_len[0]=input_len[input_buf_idx]+tmp_idx;
    		output_buf[0]=input_buf[input_buf_idx];

    		if(0)
    		{
				printf("seq=%u,buf_idx=%d\n",seq,input_buf_idx);
				for(int j=0;j<output_len[0];j++)
				{
					log_bare(log_warn,"0x%02x,",(u32_t)(unsigned char)output_buf[0][j]);
				}
				printf("\n");
    		}
    	}
    }

	if(s!=0&&delayed_append)
	{
		assert(fec_par.mode!=1);
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
/*
int fec_decode_manager_t::re_init()
{
	clear();
	return 0;
}*/

int fec_decode_manager_t::input(char *s,int len)
{
	assert(s!=0);
	assert(len+100<buf_len);//guarenteed by upper level

	int tmp_idx=0;
	int tmp_header_len=sizeof(u32_t)+sizeof(char)*4;
	if(len<tmp_header_len)
	{
		mylog(log_warn,"len =%d\n",len);
		return -1;
	}
	u32_t seq=read_u32(s+tmp_idx);
	tmp_idx+=sizeof(u32_t);
	int type=(unsigned char)s[tmp_idx++];
	int data_num=(unsigned char)s[tmp_idx++];
	int redundant_num=(unsigned char)s[tmp_idx++];
	int inner_index=(unsigned char)s[tmp_idx++];
	len=len-tmp_idx;

	//mylog(log_trace,"input\n");

	if(len<0)
	{
		mylog(log_warn,"len<0\n");
		return -1;
	}

	if(type==1)
	{
		if(len<(int)sizeof(u16_t))
		{
			mylog(log_warn,"type==1&&len<2\n");
			return -1;
		}
		if(data_num==0&&(int)( read_u16(s+tmp_idx)+sizeof(u16_t))!=len)
		{
			mylog(log_warn,"inner_index<data_num&&read_u16(s+tmp_idx)+sizeof(u16_t)!=len    %d %d\n",(int)( read_u16(s+tmp_idx)+sizeof(u16_t)),len);
			return -1;
		}
	}

	if(type==0&&data_num==0)
	{
		mylog(log_warn,"unexpected type==0&&data_num==0\n");
		return -1;
	}
	if(data_num+redundant_num>=max_fec_packet_num)
	{
		mylog(log_warn,"data_num+redundant_num>=max_fec_packet_num\n");
		return -1;
	}
	if(!anti_replay.is_vaild(seq))
	{
		mylog(log_trace,"!anti_replay.is_vaild(seq) ,seq =%u\n",seq);
		return 0;
	}

	if(mp[seq].fec_done!=0)
	{
		mylog(log_debug,"fec already done, ignore, seq=%u\n",seq);
		return -1;
	}

	if(mp[seq].group_mp.find(inner_index)!=mp[seq].group_mp.end() )
	{
		mylog(log_debug,"dup fec index\n");//duplicate can happen on  a normal network, so its just log_debug
		return -1;
	}


	if(mp[seq].type==-1)
		mp[seq].type=type;
	else
	{
		if(mp[seq].type!=type)
		{
			mylog(log_warn,"type mismatch\n");
			return -1;
		}
	}

	if(data_num!=0)
	{
		//mp[seq].data_counter++;

		if(mp[seq].data_num==-1)
		{
			mp[seq].data_num=data_num;
			mp[seq].redundant_num=redundant_num;
			mp[seq].len=len;
		}
		else
		{
			if(mp[seq].data_num!=data_num||mp[seq].redundant_num!=redundant_num||mp[seq].len!=len)
			{
				mylog(log_warn,"unexpected mp[seq].data_num!=data_num||mp[seq].redundant_num!=redundant_num||mp[seq].len!=len\n");
				return -1;
			}
		}
	}

	//mylog(log_info,"mp.size()=%d index=%d\n",mp.size(),index);

	if(fec_data[index].used!=0)
	{
		u32_t tmp_seq=fec_data[index].seq;
		anti_replay.set_invaild(tmp_seq);

		auto tmp_it=mp.find(tmp_seq);
		if(tmp_it!=mp.end())
		{
			int x=tmp_it->second.data_num;
			int y=tmp_it->second.redundant_num;
			int cnt=tmp_it->second.group_mp.size();

			if(cnt<x)
			{
				if(debug_fec_dec)
					mylog(log_debug,"[dec][failed]seq=%08x x=%d y=%d cnt=%d\n",tmp_seq,x,y,cnt);
				else
					mylog(log_trace,"[dec][failed]seq=%08x x=%d y=%d cnt=%d\n",tmp_seq,x,y,cnt);
			}
			mp.erase(tmp_it);
		}
		if(tmp_seq==seq)
		{
			mylog(log_warn,"unexpected tmp_seq==seq ,seq=%d\n",seq);
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
	assert(0<=index&&index<(int)fec_buff_num);
	assert(len+100<buf_len);
	memcpy(fec_data[index].buf,s+tmp_idx,len);
	mp[seq].group_mp[inner_index]=index;
	//index++ at end of function

	map<int,int> &inner_mp=mp[seq].group_mp;


	int about_to_fec=0;
	if(type==0)
	{
		//assert((int)inner_mp.size()<=data_num);
		if((int)inner_mp.size()>data_num)
		{
			mylog(log_warn,"inner_mp.size()>data_num\n");
			anti_replay.set_invaild(seq);
			goto end;
		}
		if((int)inner_mp.size()==data_num)
			about_to_fec=1;
	}
	else
	{
		if(mp[seq].data_num!=-1)
		{
			if((int)inner_mp.size()>mp[seq].data_num+1)
			{
				mylog(log_warn,"inner_mp.size()>data_num+1\n");
				anti_replay.set_invaild(seq);
				goto end;
			}
			if((int)inner_mp.size()>=mp[seq].data_num)
			{
				about_to_fec=1;
			}
		}
	}


	if(about_to_fec)
	{
		int group_data_num=mp[seq].data_num;
		int group_redundant_num=mp[seq].redundant_num;

		int x_got=0;
		int y_got=0;
		//mylog(log_error,"fec here!\n");
		if(type==0)
		{
			char *fec_tmp_arr[max_fec_packet_num+5]={0};
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				if(it->first <group_data_num)
					x_got++;
				else
					y_got++;
				fec_tmp_arr[it->first]=fec_data[it->second].buf;
			}
			assert(rs_decode2(group_data_num,group_data_num+group_redundant_num,fec_tmp_arr,len)==0); //the input data has been modified in-place
			//this line should always succeed
    		mp[seq].fec_done=1;

    		if(debug_fec_dec)
    			mylog(log_debug,"[dec]seq=%08x x=%d y=%d len=%d cnt=%d X=%d Y=%d\n",seq,group_data_num,group_redundant_num,len,int(inner_mp.size()),x_got,y_got);
    		else
    			mylog(log_trace,"[dec]seq=%08x x=%d y=%d len=%d cnt=%d X=%d Y=%d\n",seq,group_data_num,group_redundant_num,len,int(inner_mp.size()),x_got,y_got);

			blob_decode.clear();
			for(int i=0;i<group_data_num;i++)
			{
				blob_decode.input(fec_tmp_arr[i],len);
			}
			if(blob_decode.output(output_n,output_s_arr,output_len_arr)!=0)
			{
				mylog(log_warn,"blob_decode failed\n");
				//ready_for_output=0;
				anti_replay.set_invaild(seq);
				goto end;
			}
			assert(ready_for_output==0);
			ready_for_output=1;
			anti_replay.set_invaild(seq);
		}
		else//type==1
		{


			int max_len=-1;
			int fec_result_ok=1;
			int data_check_ok=1;
			int debug_num=inner_mp.size();

			int missed_packet[max_fec_packet_num+5];
			int missed_packet_counter=0;

			//outupt_s_arr_buf[max_fec_packet_num+5]={0};

			//memset(output_s_arr_buf,0,sizeof(output_s_arr_buf));//in efficient

			for(int i=0;i<group_data_num+group_redundant_num;i++)
			{
				output_s_arr_buf[i]=0;
			}
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				if(it->first <group_data_num)
					x_got++;
				else
					y_got++;

				output_s_arr_buf[it->first]=fec_data[it->second].buf;
				if(fec_data[it->second].len<(int)sizeof(u16_t))
				{
					mylog(log_warn,"fec_data[it->second].len<(int)sizeof(u16_t)");
					data_check_ok=0;
				}

				if(fec_data[it->second].len > max_len)
					max_len=fec_data[it->second].len;
			}
			if(max_len!=mp[seq].len)
			{
				data_check_ok=0;
				mylog(log_warn,"max_len!=mp[seq].len");
			}
			if(data_check_ok==0)
			{
				//ready_for_output=0;
				mylog(log_warn,"data_check_ok==0\n");
				anti_replay.set_invaild(seq);
				goto end;
			}
			for(auto it=inner_mp.begin();it!=inner_mp.end();it++)
			{
				int tmp_idx=it->second;
				assert(max_len>=fec_data[tmp_idx].len);//guarenteed by data_check_ok
				memset(fec_data[tmp_idx].buf+fec_data[tmp_idx].len,0,max_len-fec_data[tmp_idx].len);
			}

			for(int i=0;i<group_data_num;i++)
			{
				if(output_s_arr_buf[i]==0 ||i==inner_index) //only missed packet +current packet
				{
					missed_packet[missed_packet_counter++]=i;
				}
			}
			mylog(log_trace,"fec done,%d %d,missed_packet_counter=%d\n",group_data_num,group_redundant_num,missed_packet_counter);

			assert(rs_decode2(group_data_num,group_data_num+group_redundant_num,output_s_arr_buf,max_len)==0);//this should always succeed
			mp[seq].fec_done=1;

			int sum_ori=0;

			for(int i=0;i<group_data_num;i++)
			{
				output_len_arr_buf[i]=read_u16(output_s_arr_buf[i]);
				sum_ori+=output_len_arr_buf[i];
				output_s_arr_buf[i]+=sizeof(u16_t);
				if(output_len_arr_buf[i]>max_data_len)
				{
					mylog(log_warn,"invaild len %d,seq= %u,data_num= %d r_num= %d,i= %d\n",output_len_arr_buf[i],seq,group_data_num,group_redundant_num,i);
					fec_result_ok=0;
					for(int i=0;i<missed_packet_counter;i++)
					{
						log_bare(log_warn,"%d ",missed_packet[i]);
					}
					log_bare(log_warn,"\n");
					//break;
				}
			}

			int sum=max_len*group_data_num;

    		if(debug_fec_dec)
    			mylog(log_debug,"[dec]seq=%08x x=%d y=%d len=%d sum_ori=%d sum=%d X=%d Y=%d\n",seq,group_data_num,group_redundant_num,max_len,sum_ori,sum,x_got,y_got);
    		else
    			mylog(log_trace,"[dec]seq=%08x x=%d y=%d len=%d sum_ori=%d sum=%d X=%d Y=%d\n",seq,group_data_num,group_redundant_num,max_len,sum_ori,sum,x_got,y_got);

			if(fec_result_ok)
			{

				output_n=group_data_num;

				if(decode_fast_send)
				{
					output_n=missed_packet_counter;
					for(int i=0;i<missed_packet_counter;i++)
					{
						output_s_arr_buf[i]=output_s_arr_buf[missed_packet[i]];
						output_len_arr_buf[i]=output_len_arr_buf[missed_packet[i]];
					}
				}


				output_s_arr=output_s_arr_buf;
				output_len_arr=output_len_arr_buf;
				assert(ready_for_output==0);
				ready_for_output=1;
			}
			else
			{
				//fec_not_ok:
				ready_for_output=0;
			}
			anti_replay.set_invaild(seq);
		}// end of type==1
	}
	else //not about_to_fec
	{

		if(decode_fast_send)
		{
			if(type==1&&data_num==0)
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
	}

	end:
	index++;
	if(index==int(fec_buff_num)) index=0;

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
