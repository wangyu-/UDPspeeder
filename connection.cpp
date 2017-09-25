/*
 * connection.cpp
 *
 *  Created on: Sep 23, 2017
 *      Author: root
 */

#include "connection.h"

int disable_anti_replay=0;//if anti_replay windows is diabled

const int disable_conv_clear=0;//a udp connection in the multiplexer is called conversation in this program,conv for short.

const int disable_conn_clear=0;//a raw connection is called conn.

conn_manager_t conn_manager;

void server_clear_function(u64_t u64);

conv_manager_t::conv_manager_t()
	{
		clear_it=conv_last_active_time.begin();
		long long last_clear_time=0;
		//clear_function=0;
	}
conv_manager_t::~conv_manager_t()
	{
		clear();
	}
	int conv_manager_t::get_size()
	{
		return conv_to_u64.size();
	}
	void conv_manager_t::reserve()
	{
		u64_to_conv.reserve(10007);
		conv_to_u64.reserve(10007);
		conv_last_active_time.reserve(10007);
	}
	void conv_manager_t::clear()
	{
		if(disable_conv_clear) return ;

		if(program_mode==server_mode)
		{
			for(it=conv_to_u64.begin();it!=conv_to_u64.end();it++)
			{
				//int fd=int((it->second<<32u)>>32u);
				server_clear_function(  it->second);
			}
		}
		u64_to_conv.clear();
		conv_to_u64.clear();
		conv_last_active_time.clear();

		clear_it=conv_last_active_time.begin();

	}
	u32_t conv_manager_t::get_new_conv()
	{
		u32_t conv=get_true_random_number_nz();
		while(conv_to_u64.find(conv)!=conv_to_u64.end())
		{
			conv=get_true_random_number_nz();
		}
		return conv;
	}
	int conv_manager_t::is_conv_used(u32_t conv)
	{
		return conv_to_u64.find(conv)!=conv_to_u64.end();
	}
	int conv_manager_t::is_u64_used(u64_t u64)
	{
		return u64_to_conv.find(u64)!=u64_to_conv.end();
	}
	u32_t conv_manager_t::find_conv_by_u64(u64_t u64)
	{
		return u64_to_conv[u64];
	}
	u64_t conv_manager_t::find_u64_by_conv(u32_t conv)
	{
		return conv_to_u64[conv];
	}
	int conv_manager_t::update_active_time(u32_t conv)
	{
		return conv_last_active_time[conv]=get_current_time();
	}
	int conv_manager_t::insert_conv(u32_t conv,u64_t u64)
	{
		u64_to_conv[u64]=conv;
		conv_to_u64[conv]=u64;
		conv_last_active_time[conv]=get_current_time();
		return 0;
	}
	int conv_manager_t::erase_conv(u32_t conv)
	{
		if(disable_conv_clear) return 0;
		u64_t u64=conv_to_u64[conv];
		if(program_mode==server_mode)
		{
			server_clear_function(u64);
		}
		conv_to_u64.erase(conv);
		u64_to_conv.erase(u64);
		conv_last_active_time.erase(conv);
		return 0;
	}
	int conv_manager_t::clear_inactive(char * ip_port)
	{
		if(get_current_time()-last_clear_time>conv_clear_interval)
		{
			last_clear_time=get_current_time();
			return clear_inactive0(ip_port);
		}
		return 0;
	}
	int conv_manager_t::clear_inactive0(char * ip_port)
	{
		if(disable_conv_clear) return 0;


		//map<uint32_t,uint64_t>::iterator it;
		int cnt=0;
		it=clear_it;
		int size=conv_last_active_time.size();
		int num_to_clean=size/conv_clear_ratio+conv_clear_min;   //clear 1/10 each time,to avoid latency glitch

		num_to_clean=min(num_to_clean,size);

		u64_t current_time=get_current_time();
		for(;;)
		{
			if(cnt>=num_to_clean) break;
			if(conv_last_active_time.begin()==conv_last_active_time.end()) break;

			if(it==conv_last_active_time.end())
			{
				it=conv_last_active_time.begin();
			}

			if( current_time -it->second  >conv_timeout )
			{
				//mylog(log_info,"inactive conv %u cleared \n",it->first);
				old_it=it;
				it++;
				u32_t conv= old_it->first;
				erase_conv(old_it->first);
				if(ip_port==0)
				{
					mylog(log_info,"conv %x cleared\n",conv);
				}
				else
				{
					mylog(log_info,"[%s]conv %x cleared\n",ip_port,conv);
				}
			}
			else
			{
				it++;
			}
			cnt++;
		}
		return 0;
	}

	conn_manager_t::conn_manager_t()
 {
	 ready_num=0;
	 mp.reserve(10007);
	 clear_it=mp.begin();
	 timer_fd_mp.reserve(10007);
	 const_id_mp.reserve(10007);
	 udp_fd_mp.reserve(100007);
	 last_clear_time=0;
	 //current_ready_ip=0;
	// current_ready_port=0;
 }
 int conn_manager_t::exist(u32_t ip,uint16_t port)
 {
	 u64_t u64=0;
	 u64=ip;
	 u64<<=32u;
	 u64|=port;
	 if(mp.find(u64)!=mp.end())
	 {
		 return 1;
	 }
	 return 0;
 }
 /*
 int insert(uint32_t ip,uint16_t port)
 {
	 uint64_t u64=0;
	 u64=ip;
	 u64<<=32u;
	 u64|=port;
	 mp[u64];
	 return 0;
 }*/
 conn_info_t *& conn_manager_t::find_insert_p(u32_t ip,uint16_t port)  //be aware,the adress may change after rehash
 {
	 u64_t u64=0;
	 u64=ip;
	 u64<<=32u;
	 u64|=port;
	 unordered_map<u64_t,conn_info_t*>::iterator it=mp.find(u64);
	 if(it==mp.end())
	 {
		 mp[u64]=new conn_info_t;
	 }
	 return mp[u64];
 }
 conn_info_t & conn_manager_t::find_insert(u32_t ip,uint16_t port)  //be aware,the adress may change after rehash
 {
	 u64_t u64=0;
	 u64=ip;
	 u64<<=32u;
	 u64|=port;
	 unordered_map<u64_t,conn_info_t*>::iterator it=mp.find(u64);
	 if(it==mp.end())
	 {
		 mp[u64]=new conn_info_t;
	 }
	 return *mp[u64];
 }
 int conn_manager_t::erase(unordered_map<u64_t,conn_info_t*>::iterator erase_it)
 {
		if(erase_it->second->state.server_current_state==server_ready)
		{
			ready_num--;
			assert(i32_t(ready_num)!=-1);
			assert(erase_it->second!=0);
			assert(erase_it->second->timer_fd !=0);
			assert(erase_it->second->oppsite_const_id!=0);
			assert(const_id_mp.find(erase_it->second->oppsite_const_id)!=const_id_mp.end());
			assert(timer_fd_mp.find(erase_it->second->timer_fd)!=timer_fd_mp.end());

			const_id_mp.erase(erase_it->second->oppsite_const_id);
			timer_fd_mp.erase(erase_it->second->timer_fd);
			close(erase_it->second->timer_fd);// close will auto delte it from epoll
			delete(erase_it->second);
			mp.erase(erase_it->first);
		}
		else
		{
			assert(erase_it->second->blob==0);
			assert(erase_it->second->timer_fd ==0);
			assert(erase_it->second->oppsite_const_id==0);
			delete(erase_it->second);
			mp.erase(erase_it->first);
		}
		return 0;
 }
int conn_manager_t::clear_inactive()
{
	if(get_current_time()-last_clear_time>conn_clear_interval)
	{
		last_clear_time=get_current_time();
		return clear_inactive0();
	}
	return 0;
}
int conn_manager_t::clear_inactive0()
{
	 unordered_map<u64_t,conn_info_t*>::iterator it;
	 unordered_map<u64_t,conn_info_t*>::iterator old_it;

	if(disable_conn_clear) return 0;

	//map<uint32_t,uint64_t>::iterator it;
	int cnt=0;
	it=clear_it;
	int size=mp.size();
	int num_to_clean=size/conn_clear_ratio+conn_clear_min;   //clear 1/10 each time,to avoid latency glitch

	mylog(log_trace,"mp.size() %d\n", size);

	num_to_clean=min(num_to_clean,(int)mp.size());
	u64_t current_time=get_current_time();

	for(;;)
	{
		if(cnt>=num_to_clean) break;
		if(mp.begin()==mp.end()) break;

		if(it==mp.end())
		{
			it=mp.begin();
		}

		if(it->second->state.server_current_state==server_ready &&current_time - it->second->last_hb_recv_time  <=server_conn_timeout)
		{
				it++;
		}
		else if(it->second->state.server_current_state!=server_ready&& current_time - it->second->last_state_time  <=server_handshake_timeout )
		{
			it++;
		}
		else if(it->second->blob!=0&&it->second->blob->conv_manager.get_size() >0)
		{
			assert(it->second->state.server_current_state==server_ready);
			it++;
		}
		else
		{
			mylog(log_info,"[%s:%d]inactive conn cleared \n",my_ntoa(it->second->raw_info.recv_info.src_ip),it->second->raw_info.recv_info.src_port);
			old_it=it;
			it++;
			erase(old_it);
		}
		cnt++;
	}
	return 0;
}


void server_clear_function(u64_t u64)//used in conv_manager in server mode.for server we have to use one udp fd for one conv(udp connection),
//so we have to close the fd when conv expires
{
	int fd=int(u64);
	int ret;
	assert(fd!=0);
	/*
	epoll_event ev;

	ev.events = EPOLLIN;
	ev.data.u64 = u64;

	ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
	if (ret!=0)
	{
		mylog(log_fatal,"fd:%d epoll delete failed!!!!\n",fd);
		myexit(-1);   //this shouldnt happen
	}*/                //no need
	ret= close(fd);  //closed fd should be auto removed from epoll

	if (ret!=0)
	{
		mylog(log_fatal,"close fd %d failed !!!!\n",fd);
		myexit(-1);  //this shouldnt happen
	}
	//mylog(log_fatal,"size:%d !!!!\n",conn_manager.udp_fd_mp.size());
	assert(conn_manager.udp_fd_mp.find(fd)!=conn_manager.udp_fd_mp.end());
	conn_manager.udp_fd_mp.erase(fd);
}
