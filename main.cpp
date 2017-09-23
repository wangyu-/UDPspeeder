#include "common.h"
#include "log.h"
#include "git_version.h"
#include "lib/rs.h"
#include "packet.h"
//#include "conn_manager.h"
#include "delay_manager.h"
#include "classic.h"

using  namespace std;

typedef unsigned long long u64_t;   //this works on most platform,avoid using the PRId64
typedef long long i64_t;

typedef unsigned int u32_t;
typedef int i32_t;



int dup_num=1;
int dup_delay_min=20;   //0.1ms
int dup_delay_max=20;

int jitter_min=0;
int jitter_max=0;

//int random_number_fd=-1;

int mtu_warn=1350;
u32_t remote_address_uint32=0;
char local_address[100], remote_address[100];
int local_port = -1, remote_port = -1;

u64_t last_report_time=0;
int report_interval=0;

//conn_manager_t conn_manager;
delay_manager_t delay_manager;

const int disable_conv_clear=0;
struct conv_manager_t  // manage the udp connections
{
	//typedef hash_map map;
	unordered_map<u64_t,u32_t> u64_to_conv;  //conv and u64 are both supposed to be uniq
	unordered_map<u32_t,u64_t> conv_to_u64;

	unordered_map<u32_t,u64_t> conv_last_active_time;

	unordered_map<u32_t,u64_t>::iterator clear_it;

	unordered_map<u32_t,u64_t>::iterator it;
	unordered_map<u32_t,u64_t>::iterator old_it;

	//void (*clear_function)(uint64_t u64) ;

	long long last_clear_time;

	conv_manager_t()
	{
		clear_it=conv_last_active_time.begin();
		long long last_clear_time=0;
		//clear_function=0;
	}
	~conv_manager_t()
	{
		clear();
	}
	int get_size()
	{
		return conv_to_u64.size();
	}
	void reserve()
	{
		u64_to_conv.reserve(10007);
		conv_to_u64.reserve(10007);
		conv_last_active_time.reserve(10007);
	}
	void clear()
	{
	/////	if(disable_conv_clear) return ;

		if(program_mode==server_mode)
		{
			for(it=conv_to_u64.begin();it!=conv_to_u64.end();it++)
			{
				//int fd=int((it->second<<32u)>>32u);
	//////			server_clear_function(  it->second);//////////////todo
			}
		}
		u64_to_conv.clear();
		conv_to_u64.clear();
		conv_last_active_time.clear();

		clear_it=conv_last_active_time.begin();

	}
	u32_t get_new_conv()
	{
		u32_t conv=get_true_random_number_nz();
		while(conv_to_u64.find(conv)!=conv_to_u64.end())
		{
			conv=get_true_random_number_nz();
		}
		return conv;
	}
	int is_conv_used(u32_t conv)
	{
		return conv_to_u64.find(conv)!=conv_to_u64.end();
	}
	int is_u64_used(u64_t u64)
	{
		return u64_to_conv.find(u64)!=u64_to_conv.end();
	}
	u32_t find_conv_by_u64(u64_t u64)
	{
		return u64_to_conv[u64];
	}
	u64_t find_u64_by_conv(u32_t conv)
	{
		return conv_to_u64[conv];
	}
	int update_active_time(u32_t conv)
	{
		return conv_last_active_time[conv]=get_current_time();
	}
	int insert_conv(u32_t conv,u64_t u64)
	{
		u64_to_conv[u64]=conv;
		conv_to_u64[conv]=u64;
		conv_last_active_time[conv]=get_current_time();
		return 0;
	}
	int erase_conv(u32_t conv)
	{
		if(disable_conv_clear) return 0;
		u64_t u64=conv_to_u64[conv];
		if(program_mode==server_mode)
		{
			//server_clear_function(u64);
		}
		conv_to_u64.erase(conv);
		u64_to_conv.erase(u64);
		conv_last_active_time.erase(conv);
		return 0;
	}
	int clear_inactive(char * ip_port=0)
	{
		if(get_current_time()-last_clear_time>conv_clear_interval)
		{
			last_clear_time=get_current_time();
			return clear_inactive0(ip_port);
		}
		return 0;
	}
	int clear_inactive0(char * ip_port)
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
};//g_conv_manager;

struct conn_info_t     //stores info for a raw connection.for client ,there is only one connection,for server there can be thousand of connection since server can
//handle multiple clients
{
	conv_manager_t conv_manager;
};
struct conn_manager_t  //manager for connections. for client,we dont need conn_manager since there is only one connection.for server we use one conn_manager for all connections
{

 u32_t ready_num;

 unordered_map<int,conn_info_t *> udp_fd_mp;  //a bit dirty to used pointer,but can void unordered_map search
 unordered_map<int,conn_info_t *> timer_fd_mp;//we can use pointer here since unordered_map.rehash() uses shallow copy

 unordered_map<id_t,conn_info_t *> const_id_mp;

 unordered_map<u64_t,conn_info_t*> mp; //put it at end so that it de-consturcts first

 unordered_map<u64_t,conn_info_t*>::iterator clear_it;

 long long last_clear_time;

 conn_manager_t()
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
 int exist(u32_t ip,uint16_t port)
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
 conn_info_t *& find_insert_p(u32_t ip,uint16_t port)  //be aware,the adress may change after rehash
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
 conn_info_t & find_insert(u32_t ip,uint16_t port)  //be aware,the adress may change after rehash
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
 int erase(unordered_map<u64_t,conn_info_t*>::iterator erase_it)
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
int clear_inactive()
{
	if(get_current_time()-last_clear_time>conn_clear_interval)
	{
		last_clear_time=get_current_time();
		return clear_inactive0();
	}
	return 0;
}
int clear_inactive0()
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

}conn_manager;

int VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV;



int event_loop()
{
	struct sockaddr_in local_me, local_other;
	local_listen_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int yes = 1;
	//setsockopt(local_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	set_buf_size(local_listen_fd,4*1024*1024);
	setnonblocking(local_listen_fd);

	//char data[buf_len];
	//char *data=data0;
	socklen_t slen = sizeof(sockaddr_in);
	memset(&local_me, 0, sizeof(local_me));
	local_me.sin_family = AF_INET;
	local_me.sin_port = htons(local_port);
	local_me.sin_addr.s_addr = inet_addr(local_address);
	if (bind(local_listen_fd, (struct sockaddr*) &local_me, slen) == -1)
	{
		mylog(log_fatal,"socket bind error");
		myexit(1);
	}

	int epollfd = epoll_create1(0);
	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epollfd < 0)
	{
		mylog(log_fatal,"epoll created return %d\n", epollfd);
		myexit(-1);
	}
	ev.events = EPOLLIN;
	ev.data.fd = local_listen_fd;
	int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, local_listen_fd, &ev);

	if(ret!=0)
	{
		mylog(log_fatal,"epoll created return %d\n", epollfd);
		myexit(-1);
	}
	int clear_timer_fd=-1;
	set_timer_ms(epollfd,clear_timer_fd,timer_interval);



	ev.events = EPOLLIN;
	ev.data.fd = delay_manager.timer_fd();

	epoll_ctl(epollfd, EPOLL_CTL_ADD, delay_manager.timer_fd, &ev);


	if (ret < 0)
	{
		mylog(log_fatal,"epoll_ctl return %d\n", ret);
		myexit(-1);
	}

	for (;;)
	{
		int nfds = epoll_wait(epollfd, events, max_events, 180 * 1000); //3mins
		if (nfds < 0)
		{
			mylog(log_fatal,"epoll_wait return %d\n", nfds);
			myexit(-1);
		}
		int n;
		int clear_triggered=0;
		for (n = 0; n < nfds; ++n)
		{
			if (events[n].data.fd == local_listen_fd) //data income from local end
			{

				char data[buf_len];
				int data_len;

				slen = sizeof(sockaddr_in);
				if ((data_len = recvfrom(local_listen_fd, data, max_data_len, 0,
						(struct sockaddr *) &local_other, &slen)) == -1) //<--first packet from a new ip:port turple
				{

					mylog(log_error,"recv_from error,errno %s,this shouldnt happen,but lets try to pretend it didnt happen",strerror(errno));
					//myexit(1);
					continue;
				}
				mylog(log_trace, "received data from listen fd,%s:%d, len=%d\n", my_ntoa(local_other.sin_addr.s_addr),ntohs(local_other.sin_port),data_len);
				if(data_len>mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}
				data[data_len] = 0; //for easier debug
				u64_t u64=pack_u64(local_other.sin_addr.s_addr,ntohs(local_other.sin_port));

				if(!conn_manager.exist_u64(u64))
				{

					if(int(conn_manager.fd_to_u64.size())>=max_conv_num)
					{
						mylog(log_info,"new connection from %s:%d ,but ignored,bc of max_conv_num reached\n",my_ntoa(local_other.sin_addr.s_addr),ntohs(local_other.sin_port));
						continue;
					}
					int new_udp_fd;
					if(create_new_udp(new_udp_fd,remote_address_uint32,remote_port)!=0)
					{
						mylog(log_info,"new connection from %s:%d ,but create udp fd failed\n",my_ntoa(local_other.sin_addr.s_addr),ntohs(local_other.sin_port));
						continue;
					}
					struct epoll_event ev;

					mylog(log_trace, "u64: %lld\n", u64);
					ev.events = EPOLLIN;

					ev.data.fd = new_udp_fd;

					ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, new_udp_fd, &ev);
					if (ret != 0) {
						mylog(log_info,"new connection from %s:%d ,but add to epoll failed\n",my_ntoa(local_other.sin_addr.s_addr),ntohs(local_other.sin_port));
						close(new_udp_fd);
						continue;
					}
					mylog(log_info,"new connection from %s:%d ,created new udp fd %d\n",my_ntoa(local_other.sin_addr.s_addr),ntohs(local_other.sin_port),new_udp_fd);
					conn_manager.insert_fd(new_udp_fd,u64);
				}

				int new_udp_fd=conn_manager.find_fd_by_u64(u64);
				conn_manager.update_active_time(new_udp_fd);
				int ret;
				if(is_client)
				{
					add_seq(data,data_len);
					my_time_t sum=0;

					for(int i=0;i<dup_num;i++)
					{
						printf("<%d>\n",i);
						char new_data[buf_len];
						int new_len=0;
						do_obscure(data, data_len, new_data, new_len);
						delay_data_t tmp;
						tmp.type=enum_send_fd;
						tmp.data=new_data;
						tmp.len=new_len;
						tmp.dest.fd=new_udp_fd;
						if(i==0)
							sum+=random_between(jitter_min,jitter_max)*100;
						else
							sum+=random_between(dup_delay_min,dup_delay_max)*100;
						delay_manager.add(sum,tmp);
					}
					/*
					if(jitter_max==0)
					{
						char new_data[buf_len];
						int new_len=0;
						do_obscure(data, data_len, new_data, new_len);
						ret = send_fd(new_udp_fd, new_data,new_len, 0);
						if (ret < 0) {
							mylog(log_warn, "send returned %d ,errno:%s\n", ret,strerror(errno));
						}
						add_and_new(new_udp_fd, dup_num - 1,random_between(dup_delay_min,dup_delay_max), data, data_len,u64);
					}
					else
					{
						add_and_new(new_udp_fd, dup_num,random_between(jitter_min,jitter_max), data, data_len,u64);
					}*/
					packet_send_count++;
				}
				else
				{
					printf("i got a packet\n");
					char new_data[buf_len];
					int new_len;

					if (de_obscure(data, data_len, new_data, new_len) != 0) {
						printf("failed 1\n");
						mylog(log_trace,"de_obscure failed \n");
						continue;
					}
					//dup_packet_recv_count++;
					if (remove_seq(new_data, new_len) != 0) {
						printf("failed 2\n");
						mylog(log_trace,"remove_seq failed \n");
						continue;
					}
					//packet_recv_count++;
					ret = send_fd(new_udp_fd, new_data,new_len, 0);
					if (ret < 0) {
						mylog(log_warn, "send returned %d,%s\n", ret,strerror(errno));
						//perror("what happened????");
					}
				}


			}
			else if(events[n].data.fd == clear_timer_fd)
			{
				clear_triggered=1;
				if(report_interval!=0 &&get_current_time()-last_report_time>u64_t(report_interval)*1000)
				{
					last_report_time=get_current_time();
					if(is_client)
						mylog(log_info,"client-->server: %llu,%llu(include dup); server-->client %llu,%lld(include dup)\n",packet_send_count,
							dup_packet_send_count,packet_recv_count,dup_packet_recv_count);
					else
						mylog(log_info,"client-->server: %llu,%llu(include dup); server-->client %llu,%lld(include dup)\n",packet_recv_count,dup_packet_recv_count,packet_send_count,
								dup_packet_send_count);
				}
			}
			else if (events[n].data.fd == delay_manager.timer_fd)
			{
				uint64_t value;
				read(delay_manager.timer_fd, &value, 8);
				//printf("<timerfd_triggered, %d>",delay_mp.size());
				//fflush(stdout);
			}
			else
			{
				int udp_fd=events[n].data.fd;
				if(!conn_manager.exist_fd(udp_fd)) continue;

				char data[buf_len];
				int data_len =recv(udp_fd,data,max_data_len,0);
				mylog(log_trace, "received data from udp fd %d, len=%d\n", udp_fd,data_len);
				if(data_len<0)
				{
					if(errno==ECONNREFUSED)
					{
						//conn_manager.clear_list.push_back(udp_fd);
						mylog(log_debug, "recv failed %d ,udp_fd%d,errno:%s\n", data_len,udp_fd,strerror(errno));
					}

					mylog(log_warn, "recv failed %d ,udp_fd%d,errno:%s\n", data_len,udp_fd,strerror(errno));
					continue;
				}
				if(data_len>mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}

				assert(conn_manager.exist_fd(udp_fd));

				conn_manager.update_active_time(udp_fd);

				u64_t u64=conn_manager.find_u64_by_fd(udp_fd);

				if(is_client)
				{
					char new_data[buf_len];
					int new_len;
					if (de_obscure(data, data_len, new_data, new_len) != 0) {
						mylog(log_debug,"data_len=%d \n",data_len);
						continue;
					}

					//dup_packet_recv_count++;
					if (remove_seq(new_data, new_len) != 0) {
						mylog(log_debug,"remove_seq error \n");
						continue;
					}
					//packet_recv_count++;
					ret = sendto_u64(u64, new_data,
							new_len , 0);
					if (ret < 0) {
						mylog(log_warn, "sento returned %d,%s\n", ret,strerror(errno));
						//perror("ret<0");
					}
				}
				else
				{
					add_seq(data,data_len);

					my_time_t sum=0;
					for(int i=0;i<dup_num;i++)
					{
						printf("<%d>\n",i);
						char new_data[buf_len];
						int new_len=0;
						do_obscure(data, data_len, new_data, new_len);
						delay_data_t tmp;
						tmp.type=enum_sendto_u64;
						tmp.data=new_data;
						tmp.len=new_len;
						tmp.dest.u64=u64;
						if(i==0)
							sum+=random_between(jitter_min,jitter_max)*100;
						else
							sum+=random_between(dup_delay_min,dup_delay_max)*100;
						delay_manager.add(sum,tmp);
					}
					packet_send_count++;


				}

				//mylog(log_trace, "%s :%d\n", inet_ntoa(tmp_sockaddr.sin_addr),
					//	ntohs(tmp_sockaddr.sin_port));
				//mylog(log_trace, "%d byte sent\n", ret);

			}
		}
		delay_manager.check();
		//conn_manager.check_clear_list();
		if(clear_triggered)   // 删除操作在epoll event的最后进行，防止event cache中的fd失效。
		{
			u64_t value;
			read(clear_timer_fd, &value, 8);
			mylog(log_trace, "timer!\n");
			conn_manager.clear_inactive();
		}
	}
	myexit(0);
	return 0;
}
int unit_test()
{
	int i,j,k;
	void *code=fec_new(3,6);
	char arr[6][100]=
	{
		"aaa","bbb","ccc"
		,"ddd","eee","fff"
	};
	char *data[6];
	for(i=0;i<6;i++)
	{
		data[i]=arr[i];
	}
	rs_encode(code,data,3);
	//printf("%d %d",(int)(unsigned char)arr[5][0],(int)('a'^'b'^'c'^'d'^'e'));

	for(i=0;i<6;i++)
	{
		printf("<%s>",data[i]);
	}

	data[0]=0;
	//data[1]=0;
	//data[5]=0;

	int ret=rs_decode(code,data,3);
	printf("ret:%d\n",ret);

	for(i=0;i<6;i++)
	{
		printf("<%s>",data[i]);
	}
	fec_free(code);
	return 0;
}
void print_help()
{
	char git_version_buf[100]={0};
	strncpy(git_version_buf,gitversion,10);

	printf("UDPspeeder\n");
	printf("git version:%s    ",git_version_buf);
	printf("build date:%s %s\n",__DATE__,__TIME__);
	printf("repository: https://github.com/wangyu-/UDPspeeder\n");
	printf("\n");
	printf("usage:\n");
	printf("    run as client : ./this_program -c -l local_listen_ip:local_port -r server_ip:server_port  [options]\n");
	printf("    run as server : ./this_program -s -l server_listen_ip:server_port -r remote_ip:remote_port  [options]\n");
	printf("\n");
	printf("common option,must be same on both sides:\n");
	printf("    -k,--key              <string>        key for simple xor encryption,default:\"secret key\"\n");

	printf("main options:\n");
	printf("    -d                    <number>        duplicated packet number, -d 0 means no duplicate. default value:0\n");
	printf("    -t                    <number>        duplicated packet delay time, unit: 0.1ms,default value:20(2ms)\n");
	printf("    -j                    <number>        simulated jitter.randomly delay first packet for 0~jitter_value*0.1 ms,to\n");
	printf("                                          create simulated jitter.default value:0.do not use if you dont\n");
	printf("                                          know what it means\n");
	printf("    --report              <number>        turn on udp send/recv report,and set a time interval for reporting,unit:s\n");
	printf("advanced options:\n");
	printf("    -t                    tmin:tmax       simliar to -t above,but delay randomly between tmin and tmax\n");
	printf("    -j                    jmin:jmax       simliar to -j above,but create jitter randomly between jmin and jmax\n");
	printf("    --random-drop         <number>        simulate packet loss ,unit:0.01%%\n");
	printf("    --disable-filter                      disable duplicate packet filter.\n");
	printf("    -m                    <number>        max pending packets,to prevent the program from eating up all your memory,\n");
	printf("                                          default value:0(disabled).\n");
	printf("other options:\n");
	printf("    --log-level           <number>        0:never    1:fatal   2:error   3:warn \n");
	printf("                                          4:info (default)     5:debug   6:trace\n");
	printf("    --log-position                        enable file name,function name,line number in log\n");
	printf("    --disable-color                       disable log color\n");
	printf("    --sock-buf            <number>        buf size for socket,>=10 and <=10240,unit:kbyte,default:1024\n");
	//printf("    -p                                    use multi-process mode instead of epoll.very costly,only for test,do dont use\n");
	printf("    -h,--help                             print this help message\n");

	//printf("common options,these options must be same on both side\n");
}
void process_arg(int argc, char *argv[])
{
	int i, j, k;
	int opt;
    static struct option long_options[] =
      {
		{"log-level", required_argument,    0, 1},
		{"log-position", no_argument,    0, 1},
		{"disable-color", no_argument,    0, 1},
		{"disable-filter", no_argument,    0, 1},
		{"sock-buf", required_argument,    0, 1},
		{"random-drop", required_argument,    0, 1},
		{"report", required_argument,    0, 1},
		{NULL, 0, 0, 0}
      };
    int option_index = 0;
	if (argc == 1)
	{
		print_help();
		myexit( -1);
	}
	for (i = 0; i < argc; i++)
	{
		if(strcmp(argv[i],"--unit-test")==0)
		{
			unit_test();
			myexit(0);
		}
	}
	for (i = 0; i < argc; i++)
	{
		if(strcmp(argv[i],"-h")==0||strcmp(argv[i],"--help")==0)
		{
			print_help();
			myexit(0);
		}
	}
	for (i = 0; i < argc; i++)
	{
		if(strcmp(argv[i],"--log-level")==0)
		{
			if(i<argc -1)
			{
				sscanf(argv[i+1],"%d",&log_level);
				if(0<=log_level&&log_level<log_end)
				{
				}
				else
				{
					log_bare(log_fatal,"invalid log_level\n");
					myexit(-1);
				}
			}
		}
		if(strcmp(argv[i],"--disable-color")==0)
		{
			enable_log_color=0;
		}
	}

    mylog(log_info,"argc=%d ", argc);

	for (i = 0; i < argc; i++) {
		log_bare(log_info, "%s ", argv[i]);
	}
	log_bare(log_info, "\n");

	if (argc == 1)
	{
		print_help();
		myexit(-1);
	}

	int no_l = 1, no_r = 1;
	while ((opt = getopt_long(argc, argv, "l:r:d:t:hcspk:j:m:",long_options,&option_index)) != -1)
	{
		//string opt_key;
		//opt_key+=opt;
		switch (opt)
		{
		case 'p':
			//multi_process_mode=1;
			break;
		case 'k':
			sscanf(optarg,"%s\n",key_string);
			mylog(log_debug,"key=%s\n",key_string);
			if(strlen(key_string)==0)
			{
				mylog(log_fatal,"key len=0??\n");
				myexit(-1);
			}
			break;

		case 'm':
			sscanf(optarg,"%d\n",&max_pending_packet);
			if(max_pending_packet<1000)
			{
				mylog(log_fatal,"max_pending_packet must be >1000\n");
				myexit(-1);
			}
			break;

		case 'j':
			if (strchr(optarg, ':') == 0)
			{
				int jitter;
				sscanf(optarg,"%d\n",&jitter);
				if(jitter<0 ||jitter>1000*100)
				{
					mylog(log_fatal,"jitter must be between 0 and 100,000(10 second)\n");
					myexit(-1);
				}
				jitter_min=0;
				jitter_max=jitter;
			}
			else
			{
				sscanf(optarg,"%d:%d\n",&jitter_min,&jitter_max);
				if(jitter_min<0 ||jitter_max<0||jitter_min>jitter_max)
				{
					mylog(log_fatal," must satisfy  0<=jmin<=jmax\n");
					myexit(-1);
				}
			}
			break;
		case 't':
			if (strchr(optarg, ':') == 0)
			{
				int dup_delay=-1;
				sscanf(optarg,"%d\n",&dup_delay);
				if(dup_delay<1||dup_delay>1000*100)
				{
					mylog(log_fatal,"dup_delay must be between 1 and 100,000(10 second)\n");
					myexit(-1);
				}
				dup_delay_min=dup_delay_max=dup_delay;
			}
			else
			{
				sscanf(optarg,"%d:%d\n",&dup_delay_min,&dup_delay_max);
				if(dup_delay_min<1 ||dup_delay_max<1||dup_delay_min>dup_delay_max)
				{
					mylog(log_fatal," must satisfy  1<=dmin<=dmax\n");
					myexit(-1);
				}
			}
			break;
		case 'd':
			dup_num=-1;
			sscanf(optarg,"%d\n",&dup_num);
			if(dup_num<0 ||dup_num>5)
			{
				mylog(log_fatal,"dup_num must be between 0 and 5\n");
				myexit(-1);
			}
			dup_num+=1;
			break;
		case 'c':
			is_client = 1;
			break;
		case 's':
			is_server = 1;
			break;
		case 'l':
			no_l = 0;
			if (strchr(optarg, ':') != 0)
			{
				sscanf(optarg, "%[^:]:%d", local_address, &local_port);
			}
			else
			{
				mylog(log_fatal," -r ip:port\n");
				myexit(1);
				strcpy(local_address, "127.0.0.1");
				sscanf(optarg, "%d", &local_port);
			}
			break;
		case 'r':
			no_r = 0;
			if (strchr(optarg, ':') != 0)
			{
				//printf("in :\n");
				//printf("%s\n",optarg);
				sscanf(optarg, "%[^:]:%d", remote_address, &remote_port);
				//printf("%d\n",remote_port);
			}
			else
			{
				mylog(log_fatal," -r ip:port\n");
				myexit(1);
				strcpy(remote_address, "127.0.0.1");
				sscanf(optarg, "%d", &remote_port);
			}
			break;
		case 'h':
			break;
		case 1:
			if(strcmp(long_options[option_index].name,"log-level")==0)
			{
			}
			else if(strcmp(long_options[option_index].name,"disable-filter")==0)
			{
				disable_replay_filter=1;
				//enable_log_color=0;
			}
			else if(strcmp(long_options[option_index].name,"disable-color")==0)
			{
				//enable_log_color=0;
			}
			else if(strcmp(long_options[option_index].name,"log-position")==0)
			{
				enable_log_position=1;
			}
			else if(strcmp(long_options[option_index].name,"random-drop")==0)
			{
				sscanf(optarg,"%d",&random_drop);
				if(random_drop<0||random_drop>10000)
				{
					mylog(log_fatal,"random_drop must be between 0 10000 \n");
					myexit(-1);
				}
			}
			else if(strcmp(long_options[option_index].name,"report")==0)
			{
				sscanf(optarg,"%d",&report_interval);

				if(report_interval<=0)
				{
					mylog(log_fatal,"report_interval must be >0 \n");
					myexit(-1);
				}
			}
			else if(strcmp(long_options[option_index].name,"sock-buf")==0)
			{
				int tmp=-1;
				sscanf(optarg,"%d",&tmp);
				if(10<=tmp&&tmp<=10*1024)
				{
					socket_buf_size=tmp*1024;
				}
				else
				{
					mylog(log_fatal,"sock-buf value must be between 1 and 10240 (kbyte) \n");
					myexit(-1);
				}
			}
			else
			{
				mylog(log_fatal,"unknown option\n");
				myexit(-1);
			}
			break;
		default:
			mylog(log_fatal,"unknown option <%x>", opt);
			myexit(-1);
		}
	}

	if (no_l)
		mylog(log_fatal,"error: -i not found\n");
	if (no_r)
		mylog(log_fatal,"error: -o not found\n");
	if (no_l || no_r)
		myexit(-1);
	if (is_client == 0 && is_server == 0)
	{
		mylog(log_fatal,"-s -c hasnt been set\n");
		myexit(-1);
	}
	if (is_client == 1 && is_server == 1)
	{
		mylog(log_fatal,"-s -c cant be both set\n");
		myexit(-1);
	}
}

int main(int argc, char *argv[])
{
	if(argc==1||argc==0)
	{
		printf("this_program classic\n");
		printf("this_program fec\n");
		return 0;
	}
	/*
	if(argc>=2&&strcmp(argv[1],"fec")!=0)
	{
		printf("running into classic mode!\n");
		return classic::main(argc,argv);
	}*/

	assert(sizeof(u64_t)==8);
	assert(sizeof(i64_t)==8);
	assert(sizeof(u32_t)==4);
	assert(sizeof(i32_t)==4);
	dup2(1, 2);		//redirect stderr to stdout
	int i, j, k;
	process_arg(argc,argv);
	delay_manager.capacity=max_pending_packet;
	init_random_number_fd();

	remote_address_uint32=inet_addr(remote_address);


	event_loop();


	return 0;
}

