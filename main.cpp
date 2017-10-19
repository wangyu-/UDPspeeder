#include "common.h"
#include "log.h"
#include "git_version.h"
#include "lib/rs.h"
#include "packet.h"
#include "connection.h"
#include "fd_manager.h"
#include "delay_manager.h"
#include "fec_manager.h"

using  namespace std;

typedef unsigned long long u64_t;   //this works on most platform,avoid using the PRId64
typedef long long i64_t;

typedef unsigned int u32_t;
typedef int i32_t;

//int dup_num=1;
//int dup_delay_min=20;   //0.1ms
//int dup_delay_max=20;



//int random_number_fd=-1;

int mtu_warn=1350;

int disable_mtu_warn=1;
int disable_fec=0;

int debug_force_flush_fec=0;

int fec_data_num=20;
int fec_redundant_num=10;
int fec_mtu=1250;
int fec_pending_num=200;
int fec_pending_time=8*1000; //8ms
int fec_type=1;

int jitter_min=0*1000;
int jitter_max=0*1000;

int output_interval_min=0*1000;
int output_interval_max=0*1000;

int fix_latency=0;

u32_t local_ip_uint32,remote_ip_uint32=0;
char local_ip[100], remote_ip[100];
int local_port = -1, remote_port = -1;

//u64_t last_report_time=0;


conn_manager_t conn_manager;
delay_manager_t delay_manager;
fd_manager_t fd_manager;

int time_mono_test=1;


const int disable_conv_clear=0;

int socket_buf_size=1024*1024;


int VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV;

int init_listen_socket()
{
	local_listen_fd =socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	int yes = 1;
	//setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in local_me={0};

	socklen_t slen = sizeof(sockaddr_in);
	//memset(&local_me, 0, sizeof(local_me));
	local_me.sin_family = AF_INET;
	local_me.sin_port = htons(local_port);
	local_me.sin_addr.s_addr = local_ip_uint32;

	if (bind(local_listen_fd, (struct sockaddr*) &local_me, slen) == -1) {
		mylog(log_fatal,"socket bind error\n");
		//perror("socket bind error");
		myexit(1);
	}
	setnonblocking(local_listen_fd);
    set_buf_size(local_listen_fd,socket_buf_size);

    mylog(log_debug,"local_listen_fd=%d\n,",local_listen_fd);

	return 0;
}
int new_connected_socket(int &fd,u32_t ip,int port)
{
	char ip_port[40];
	sprintf(ip_port,"%s:%d",my_ntoa(ip),port);

	struct sockaddr_in remote_addr_in = { 0 };

	socklen_t slen = sizeof(sockaddr_in);
	//memset(&remote_addr_in, 0, sizeof(remote_addr_in));
	remote_addr_in.sin_family = AF_INET;
	remote_addr_in.sin_port = htons(port);
	remote_addr_in.sin_addr.s_addr = ip;

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		mylog(log_warn, "[%s]create udp_fd error\n", ip_port);
		return -1;
	}
	setnonblocking(fd);
	set_buf_size(fd, socket_buf_size);

	mylog(log_debug, "[%s]created new udp_fd %d\n", ip_port, fd);
	int ret = connect(fd, (struct sockaddr *) &remote_addr_in, slen);
	if (ret != 0) {
		mylog(log_warn, "[%s]fd connect fail\n",ip_port);
		close(fd);
		return -1;
	}
	return 0;
}
int delay_send(my_time_t delay,const dest_t &dest,char *data,int len)
{
	//int rand=random()%100;
	//mylog(log_info,"rand = %d\n",rand);

	if (dest.cook&&random_drop != 0) {
		if (get_true_random_number() % 10000 < (u32_t) random_drop) {
			return 0;
		}
	}
	return delay_manager.add(delay,dest,data,len);;
}
int from_normal_to_fec(conn_info_t & conn_info,char *data,int len,int & out_n,char **&out_arr,int *&out_len,my_time_t *&out_delay)
{

	static my_time_t out_delay_buf[max_fec_packet_num+100]={0};
	//static int out_len_buf[max_fec_packet_num+100]={0};
	//static int counter=0;
	out_delay=out_delay_buf;
	//out_len=out_len_buf;
	inner_stat_t &inner_stat=conn_info.stat.normal_to_fec;
	if(disable_fec)
	{
		assert(data!=0);
		inner_stat.input_packet_num++;
		inner_stat.input_packet_size+=len;
		inner_stat.output_packet_num++;
		inner_stat.output_packet_size+=len;

		if(data==0) return 0;
		out_n=1;
		static char *data_static;
		data_static=data;
		static int len_static;
		len_static=len;
		out_arr=&data_static;
		out_len=&len_static;
		out_delay[0]=0;

	}
	else
	{
		if(data!=0)
		{
			inner_stat.input_packet_num++;
			inner_stat.input_packet_size+=len;
		}
		//counter++;

		conn_info.fec_encode_manager.input(data,len);

		//if(counter%5==0)
			//conn_info.fec_encode_manager.input(0,0);

		//int n;
		//char **s_arr;
		//int s_len;


		conn_info.fec_encode_manager.output(out_n,out_arr,out_len);

		if(out_n>0)
		{
			my_time_t common_latency=0;
			my_time_t first_packet_time=conn_info.fec_encode_manager.get_first_packet_time();

			if(fix_latency==1&&conn_info.fec_encode_manager.get_type()==0)
			{
				my_time_t current_time=get_current_time_us();
				my_time_t tmp;
				assert(first_packet_time!=0);
				//mylog(log_info,"current_time=%llu first_packlet_time=%llu   fec_pending_time=%llu\n",current_time,first_packet_time,(my_time_t)fec_pending_time);
				if((my_time_t)fec_pending_time >=(current_time - first_packet_time))
				{
					tmp=(my_time_t)fec_pending_time-(current_time - first_packet_time);
					//mylog(log_info,"tmp=%llu\n",tmp);
				}
				else
				{
					tmp=0;
					//mylog(log_info,"0\n");
				}
				common_latency+=tmp;
			}

			common_latency+=random_between(jitter_min,jitter_max);

			out_delay_buf[0]=common_latency;

			for(int i=1;i<out_n;i++)
			{
				out_delay_buf[i]=out_delay_buf[i-1]+ (my_time_t)( random_between(output_interval_min,output_interval_max)/(out_n-1)  );
			}
		}

	}

	mylog(log_trace,"from_normal_to_fec input_len=%d,output_n=%d\n",len,out_n);

	if(out_n>0)
	{
		log_bare(log_trace,"seq= %u ",read_u32(out_arr[0]));
	}
	for(int i=0;i<out_n;i++)
	{
		inner_stat.output_packet_num++;
		inner_stat.output_packet_size+=out_len[i];

		log_bare(log_trace,"%d ",out_len[i]);

	}

	log_bare(log_trace,"\n");
	//for(int i=0;i<n;i++)
	//{
		//delay_send(0,dest,s_arr[i],s_len);
	//}
	//delay_send(0,dest,data,len);
	//delay_send(1000*1000,dest,data,len);
	return 0;
}
int from_fec_to_normal(conn_info_t & conn_info,char *data,int len,int & out_n,char **&out_arr,int *&out_len,my_time_t *&out_delay)
{
	static my_time_t out_delay_buf[max_blob_packet_num+100]={0};
	out_delay=out_delay_buf;
	inner_stat_t &inner_stat=conn_info.stat.fec_to_normal;
	if(disable_fec)
	{
		assert(data!=0);
		inner_stat.input_packet_num++;
		inner_stat.input_packet_size+=len;
		inner_stat.output_packet_num++;
		inner_stat.output_packet_size+=len;

		if(data==0) return 0;
		out_n=1;
		static char *data_static;
		data_static=data;
		static int len_static;
		len_static=len;
		out_arr=&data_static;
		out_len=&len_static;
		out_delay[0]=0;
	}
	else
	{

		if(data!=0)
		{
			inner_stat.input_packet_num++;
			inner_stat.input_packet_size+=len;
		}

		conn_info.fec_decode_manager.input(data,len);

		//int n;char ** s_arr;int* len_arr;
		conn_info.fec_decode_manager.output(out_n,out_arr,out_len);
		for(int i=0;i<out_n;i++)
		{
			out_delay_buf[i]=0;

			inner_stat.output_packet_num++;
			inner_stat.output_packet_size+=out_len[i];
		}


	}

	mylog(log_trace,"from_fec_to_normal input_len=%d,output_n=%d,input_seq=%u\n",len,out_n,read_u32(data));


//	printf("<n:%d>",n);
	/*
	for(int i=0;i<n;i++)
	{
		delay_send(0,dest,s_arr[i],len_arr[i]);
		//s_arr[i][len_arr[i]]=0;
		//printf("<%s>\n",s_arr[i]);
	}*/
	//my_send(dest,data,len);
	return 0;
}
int client_event_loop()
{
	//char buf[buf_len];
	int i, j, k;int ret;
	int yes = 1;
	int epoll_fd;
	int remote_fd;
	fd64_t remote_fd64;

    conn_info_t *conn_info_p=new conn_info_t;
    conn_info_t &conn_info=*conn_info_p;  //huge size of conn_info,do not allocate on stack
    //conn_info.conv_manager.reserve();
	conn_info.fec_encode_manager.re_init(fec_data_num,fec_redundant_num,fec_mtu,fec_pending_num,fec_pending_time,fec_type);

	init_listen_socket();

	epoll_fd = epoll_create1(0);
	assert(epoll_fd>0);

	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epoll_fd < 0) {
		mylog(log_fatal,"epoll return %d\n", epoll_fd);
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = local_listen_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_listen_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  udp_listen_fd error\n");
		myexit(-1);
	}

	assert(new_connected_socket(remote_fd,remote_ip_uint32,remote_port)==0);
	remote_fd64=fd_manager.create(remote_fd);

	mylog(log_debug,"remote_fd64=%llu\n",remote_fd64);

	ev.events = EPOLLIN;
	ev.data.u64 = remote_fd64;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, remote_fd, &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add raw_fd error\n");
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = delay_manager.get_timer_fd();

	mylog(log_debug,"delay_manager.get_timer_fd()=%d\n",delay_manager.get_timer_fd());
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, delay_manager.get_timer_fd(), &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add delay_manager.get_timer_fd() error\n");
		myexit(-1);
	}

	u64_t fd64=conn_info.fec_encode_manager.get_timer_fd64();
	ev.events = EPOLLIN;
	ev.data.u64 = fd64;

	mylog(log_debug,"conn_info.fec_encode_manager.get_timer_fd64()=%llu\n",conn_info.fec_encode_manager.get_timer_fd64());
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_manager.to_fd(fd64), &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add fec_encode_manager.get_timer_fd64() error\n");
		myexit(-1);
	}

	//my_timer_t timer;
	conn_info.timer.add_fd_to_epoll(epoll_fd);
	conn_info.timer.set_timer_repeat_us(timer_interval*1000);

	mylog(log_debug,"conn_info.timer.get_timer_fd()=%d\n",conn_info.timer.get_timer_fd());



	while(1)////////////////////////
	{
		if(about_to_exit) myexit(0);

		int nfds = epoll_wait(epoll_fd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			if(errno==EINTR  )
			{
				mylog(log_info,"epoll interrupted by signal\n");
				myexit(0);
			}
			else
			{
				mylog(log_fatal,"epoll_wait return %d\n", nfds);
				myexit(-1);
			}
		}
		int idx;
		for (idx = 0; idx < nfds; ++idx) {
			if(events[idx].data.u64==(u64_t)conn_info.timer.get_timer_fd())
			{
				uint64_t value;
				read(conn_info.timer.get_timer_fd(), &value, 8);
				conn_info.conv_manager.clear_inactive();
				mylog(log_trace,"events[idx].data.u64==(u64_t)conn_info.timer.get_timer_fd()\n");

				conn_info.stat.report_as_client();

				if(debug_force_flush_fec)
				{
				int  out_n;char **out_arr;int *out_len;my_time_t *out_delay;
				dest_t dest;
				dest.type=type_fd64;
				dest.inner.fd64=remote_fd64;
				dest.cook=1;
				from_normal_to_fec(conn_info,0,0,out_n,out_arr,out_len,out_delay);
				for(int i=0;i<out_n;i++)
				{
					delay_send(out_delay[i],dest,out_arr[i],out_len[i]);
				}
				}
			}
			else if (events[idx].data.u64 == (u64_t)local_listen_fd||events[idx].data.u64 == conn_info.fec_encode_manager.get_timer_fd64())
			{
				char data[buf_len];
				int data_len;
				ip_port_t ip_port;
				u32_t conv;
				int  out_n;char **out_arr;int *out_len;my_time_t *out_delay;
				dest_t dest;
				dest.type=type_fd64;
				dest.inner.fd64=remote_fd64;
				dest.cook=1;

				if(events[idx].data.u64 == conn_info.fec_encode_manager.get_timer_fd64())
				{
					mylog(log_trace,"events[idx].data.u64 == conn_info.fec_encode_manager.get_timer_fd64()\n");

					//mylog(log_info,"timer!!!\n");
					uint64_t value;
					if(!fd_manager.exist(fd64))   //fd64 has been closed
					{
						mylog(log_trace,"!fd_manager.exist(fd64)");
						continue;
					}
					if((ret=read(fd_manager.to_fd(fd64), &value, 8))!=8)
					{
						mylog(log_trace,"(ret=read(fd_manager.to_fd(fd64), &value, 8))!=8,ret=%d\n",ret);
						continue;
					}
					if(value==0)
					{
						mylog(log_debug,"value==0\n");
						continue;
					}
					assert(value==1);
					from_normal_to_fec(conn_info,0,0,out_n,out_arr,out_len,out_delay);
					//from_normal_to_fec(conn_info,0,0,out_n,out_arr,out_len,out_delay);
				}
				else//events[idx].data.u64 == (u64_t)local_listen_fd
				{
					mylog(log_trace,"events[idx].data.u64 == (u64_t)local_listen_fd\n");
					struct sockaddr_in udp_new_addr_in={0};
					socklen_t udp_new_addr_len = sizeof(sockaddr_in);
					if ((data_len = recvfrom(local_listen_fd, data, max_data_len, 0,
							(struct sockaddr *) &udp_new_addr_in, &udp_new_addr_len)) == -1) {
						mylog(log_error,"recv_from error,this shouldnt happen at client\n");
						myexit(1);
					};

					if(!disable_mtu_warn&&data_len>=mtu_warn)
					{
						mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
					}
					mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
							ntohs(udp_new_addr_in.sin_port),data_len);

					ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
					ip_port.port=ntohs(udp_new_addr_in.sin_port);

					u64_t u64=ip_port.to_u64();

					if(!conn_info.conv_manager.is_u64_used(u64))
					{
						if(conn_info.conv_manager.get_size() >=max_conv_num)
						{
							mylog(log_warn,"ignored new udp connect bc max_conv_num exceed\n");
							continue;
						}
						conv=conn_info.conv_manager.get_new_conv();
						conn_info.conv_manager.insert_conv(conv,u64);
						mylog(log_info,"new packet from %s:%d,conv_id=%x\n",inet_ntoa(udp_new_addr_in.sin_addr),ntohs(udp_new_addr_in.sin_port),conv);
					}
					else
					{
						conv=conn_info.conv_manager.find_conv_by_u64(u64);
						mylog(log_trace,"conv=%d\n",conv);
					}
					conn_info.conv_manager.update_active_time(conv);
					char * new_data;
					int new_len;
					put_conv(conv,data,data_len,new_data,new_len);


					mylog(log_trace,"data_len=%d new_len=%d\n",data_len,new_len);
					//dest.conv=conv;
					from_normal_to_fec(conn_info,new_data,new_len,out_n,out_arr,out_len,out_delay);

				}
				mylog(log_trace,"out_n=%d\n",out_n);
				for(int i=0;i<out_n;i++)
				{
					delay_send(out_delay[i],dest,out_arr[i],out_len[i]);
				}
				//my_send(dest,data,data_len);
			}
		    else if (events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()) {
				uint64_t value;
				read(delay_manager.get_timer_fd(), &value, 8);
				mylog(log_trace,"events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()\n");
				//printf("<timerfd_triggered, %d>",delay_mp.size());
				//fflush(stdout);
			}
			else if(events[idx].data.u64>u32_t(-1) )
			{
				char data[buf_len];
				if(!fd_manager.exist(events[idx].data.u64))   //fd64 has been closed
				{
					mylog(log_trace,"!fd_manager.exist(events[idx].data.u64)");
					continue;
				}
				assert(events[idx].data.u64==remote_fd64);
				int fd=fd_manager.to_fd(remote_fd64);
				int data_len =recv(fd,data,max_data_len,0);
				mylog(log_trace, "received data from udp fd %d, len=%d\n", remote_fd,data_len);
				if(data_len<0)
				{
					if(errno==ECONNREFUSED)
					{
						//conn_manager.clear_list.push_back(udp_fd);
						mylog(log_debug, "recv failed %d ,udp_fd%d,errno:%s\n", data_len,remote_fd,strerror(errno));
					}

					mylog(log_warn, "recv failed %d ,udp_fd%d,errno:%s\n", data_len,remote_fd,strerror(errno));
					continue;
				}
				if(!disable_mtu_warn&&data_len>mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}

				if(de_cook(data,data_len)!=0)
				{
					mylog(log_debug,"de_cook error");
					continue;
				}

				int  out_n;char **out_arr;int *out_len;my_time_t *out_delay;
				from_fec_to_normal(conn_info,data,data_len,out_n,out_arr,out_len,out_delay);

				mylog(log_trace,"out_n=%d\n",out_n);

				for(int i=0;i<out_n;i++)
				{
					u32_t conv;
					char *new_data;
					int new_len;
					if(get_conv(conv,out_arr[i],out_len[i],new_data,new_len)!=0)
					{
						mylog(log_debug,"get_conv(conv,out_arr[i],out_len[i],new_data,new_len)!=0");
						continue;
					}
					if(!conn_info.conv_manager.is_conv_used(conv))
					{
						mylog(log_trace,"!conn_info.conv_manager.is_conv_used(conv)");
					}

					conn_info.conv_manager.update_active_time(conv);

					u64_t u64=conn_info.conv_manager.find_u64_by_conv(conv);
					dest_t dest;
					dest.inner.ip_port.from_u64(u64);
					dest.type=type_ip_port;
					//dest.conv=conv;

					delay_send(out_delay[i],dest,new_data,new_len);
				}
				//mylog(log_trace,"[%s] send packet\n",dest.inner.ip_port.to_s());
			}
			else
			{
				mylog(log_fatal,"unknown fd,this should never happen\n");
				myexit(-1);
			}
		}
		delay_manager.check();
	}
	return 0;
}

int server_event_loop()
{
	//char buf[buf_len];
	int i, j, k;int ret;
	int yes = 1;
	int epoll_fd;
	int remote_fd;

//    conn_info_t conn_info;

	init_listen_socket();

	epoll_fd = epoll_create1(0);
	assert(epoll_fd>0);

	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epoll_fd < 0) {
		mylog(log_fatal,"epoll return %d\n", epoll_fd);
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = local_listen_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_listen_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  udp_listen_fd error\n");
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = delay_manager.get_timer_fd();
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, delay_manager.get_timer_fd(), &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add delay_manager.get_timer_fd() error\n");
		myexit(-1);
	}

	mylog(log_debug," delay_manager.get_timer_fd() =%d\n", delay_manager.get_timer_fd());

	mylog(log_info,"now listening at %s:%d\n",my_ntoa(local_ip_uint32),local_port);

	my_timer_t timer;
	timer.add_fd_to_epoll(epoll_fd);
	timer.set_timer_repeat_us(timer_interval*1000);



	mylog(log_debug," timer.get_timer_fd() =%d\n",timer.get_timer_fd());

	while(1)////////////////////////
	{

		if(about_to_exit) myexit(0);

		int nfds = epoll_wait(epoll_fd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			if(errno==EINTR  )
			{
				mylog(log_info,"epoll interrupted by signal\n");
				myexit(0);
			}
			else
			{
				mylog(log_fatal,"epoll_wait return %d\n", nfds);
				myexit(-1);
			}
		}
		int idx;
		for (idx = 0; idx < nfds; ++idx)
		{
			/*
			if ((events[idx].data.u64 ) == (u64_t)timer_fd)
			{
				conn_manager.clear_inactive();
				u64_t dummy;
				read(timer_fd, &dummy, 8);
				//current_time_rough=get_current_time();
			}
			else */
			if(events[idx].data.u64==(u64_t)timer.get_timer_fd())
			{
				uint64_t value;
				read(timer.get_timer_fd(), &value, 8);
				conn_manager.clear_inactive();
				mylog(log_trace,"events[idx].data.u64==(u64_t)timer.get_timer_fd()\n");
				//conn_info.conv_manager.clear_inactive();
			}
			else if (events[idx].data.u64 == (u64_t)local_listen_fd)
			{

				mylog(log_trace,"events[idx].data.u64 == (u64_t)local_listen_fd\n");
				//int recv_len;
				char data[buf_len];
				int data_len;
				struct sockaddr_in udp_new_addr_in={0};
				socklen_t udp_new_addr_len = sizeof(sockaddr_in);
				if ((data_len = recvfrom(local_listen_fd, data, max_data_len, 0,
						(struct sockaddr *) &udp_new_addr_in, &udp_new_addr_len)) == -1) {
					mylog(log_error,"recv_from error,this shouldnt happen at client\n");
					myexit(1);
				};
				mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port),data_len);

				if(!disable_mtu_warn&&data_len>=mtu_warn)///////////////////////delete this for type 0 in furture
				{
					mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}


				if(de_cook(data,data_len)!=0)
				{
					mylog(log_debug,"de_cook error");
					continue;
				}


				ip_port_t ip_port;
				ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
				ip_port.port=ntohs(udp_new_addr_in.sin_port);
				mylog(log_trace,"ip_port= %s\n",ip_port.to_s());
				if(!conn_manager.exist(ip_port))
				{
					if(conn_manager.mp.size() >=max_conn_num)
					{
						mylog(log_warn,"new connection %s ignored bc max_conn_num exceed\n",ip_port.to_s());
						continue;
					}

					conn_manager.insert(ip_port);
					conn_info_t &conn_info=conn_manager.find(ip_port);
					conn_info.fec_encode_manager.re_init(fec_data_num,fec_redundant_num,fec_mtu,fec_pending_num,fec_pending_time,fec_type);
					//conn_info.conv_manager.reserve();  //already reserved in constructor

					u64_t fec_fd64=conn_info.fec_encode_manager.get_timer_fd64();
					mylog(log_debug,"fec_fd64=%llu\n",fec_fd64);
					ev.events = EPOLLIN;
					ev.data.u64 = fec_fd64;
					ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_manager.to_fd(fec_fd64), &ev);

					fd_manager.get_info(fec_fd64).ip_port=ip_port;

					conn_info.timer.add_fd64_to_epoll(epoll_fd);
					conn_info.timer.set_timer_repeat_us(timer_interval*1000);

					mylog(log_debug,"conn_info.timer.get_timer_fd64()=%llu\n",conn_info.timer.get_timer_fd64());

					u64_t timer_fd64=conn_info.timer.get_timer_fd64();
					fd_manager.get_info(timer_fd64).ip_port=ip_port;

					mylog(log_info,"new connection from %s\n",ip_port.to_s());

				}
				conn_info_t &conn_info=conn_manager.find(ip_port);

				conn_info.update_active_time();
				int  out_n;char **out_arr;int *out_len;my_time_t *out_delay;
				from_fec_to_normal(conn_info,data,data_len,out_n,out_arr,out_len,out_delay);

				mylog(log_trace,"out_n= %d\n",out_n);
				for(int i=0;i<out_n;i++)
				{
					u32_t conv;
					char *new_data;
					int new_len;
					if(get_conv(conv,out_arr[i],out_len[i],new_data,new_len)!=0)
					{
						mylog(log_debug,"get_conv failed");
						continue;
					}

					/*
					id_t tmp_conv_id;
					memcpy(&tmp_conv_id,&data_[0],sizeof(tmp_conv_id));
					tmp_conv_id=ntohl(tmp_conv_id);*/

					if (!conn_info.conv_manager.is_conv_used(conv))
					{
						if(conn_info.conv_manager.get_size() >=max_conv_num)
						{
							mylog(log_warn,"ignored new udp connect bc max_conv_num exceed\n");
							continue;
						}

						int new_udp_fd;
						ret=new_connected_socket(new_udp_fd,remote_ip_uint32,remote_port);

						if (ret != 0) {
							mylog(log_warn, "[%s]new_connected_socket failed\n",ip_port.to_s());
							continue;
						}

						fd64_t fd64 = fd_manager.create(new_udp_fd);
						ev.events = EPOLLIN;
						ev.data.u64 = fd64;
						ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_udp_fd, &ev);

						conn_info.conv_manager.insert_conv(conv, fd64);
						fd_manager.get_info(fd64).ip_port=ip_port;


						mylog(log_info,"[%s]new conv %x,fd %d created,fd64=%llu\n",ip_port.to_s(),conv,new_udp_fd,fd64);
						//assert(!conn_manager.exist_fd64(fd64));

						//conn_manager.insert_fd64(fd64,ip_port);
					}
					conn_info.conv_manager.update_active_time(conv);
					fd64_t fd64= conn_info.conv_manager.find_u64_by_conv(conv);
					//int fd=fd_manager.fd64_to_fd(fd64);
					dest_t dest;
					dest.type=type_fd64;
					dest.inner.fd64=fd64;
					//dest.conv=conv;
					delay_send(out_delay[i],dest,new_data,new_len);
				}
			}
		    else if (events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()) {
				uint64_t value;
				read(delay_manager.get_timer_fd(), &value, 8);
				mylog(log_trace,"events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()\n");
			}
			else if (events[idx].data.u64 >u32_t(-1))
			{
				char data[buf_len];
				int data_len;
				u32_t conv;
				fd64_t fd64=events[idx].data.u64;
				mylog(log_trace,"events[idx].data.u64 >u32_t(-1),%llu\n",(u64_t)events[idx].data.u64);
				if(!fd_manager.exist(fd64))   //fd64 has been closed
				{
					mylog(log_trace,"!fd_manager.exist(fd64)\n");
					continue;
				}

				assert(fd_manager.exist_info(fd64));
				ip_port_t ip_port=fd_manager.get_info(fd64).ip_port;
				assert(conn_manager.exist(ip_port));

				conn_info_t &conn_info=conn_manager.find(ip_port);
				//conn_info.update_active_time(); //cant put it here

				int  out_n=-2;char **out_arr;int *out_len;my_time_t *out_delay;
				dest_t dest;
				dest.type=type_ip_port;
				//dest.conv=conv;
				dest.inner.ip_port=ip_port;
				dest.cook=1;

				if(fd64==conn_info.fec_encode_manager.get_timer_fd64())
				{
					//mylog(log_infol,"timer!!!\n");
					uint64_t value;
					if((ret=read(fd_manager.to_fd(fd64), &value, 8))!=8)
					{
						mylog(log_trace,"fd_manager.to_fd(fd64), &value, 8)!=8 ,%d\n",ret);
						continue;
					}
					if(value==0)
					{
						mylog(log_trace,"value==0\n");
						continue;
					}
					assert(value==1);
					from_normal_to_fec(conn_info,0,0,out_n,out_arr,out_len,out_delay);
				}
				else if(fd64==conn_info.timer.get_timer_fd64())
				{
					uint64_t value;
					read(conn_info.timer.get_timer_fd(), &value, 8);
					conn_info.conv_manager.clear_inactive();
					if(debug_force_flush_fec)
					{
					from_normal_to_fec(conn_info,0,0,out_n,out_arr,out_len,out_delay);
					}

					conn_info.stat.report_as_server(ip_port);
					continue;
				}
				else
				{
					assert(conn_info.conv_manager.is_u64_used(fd64));

					conv=conn_info.conv_manager.find_conv_by_u64(fd64);
					conn_info.conv_manager.update_active_time(conv);
					conn_info.update_active_time();

					int fd=fd_manager.to_fd(fd64);
					data_len=recv(fd,data,max_data_len,0);

					mylog(log_trace,"received a packet from udp_fd,len:%d,conv=%d\n",data_len,conv);

					if(data_len<0)
					{
						mylog(log_debug,"udp fd,recv_len<0 continue,%s\n",strerror(errno));

						continue;
					}

					if(!disable_mtu_warn&&data_len>=mtu_warn)
					{
						mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
					}

					char * new_data;
					int new_len;
					put_conv(conv,data,data_len,new_data,new_len);

					from_normal_to_fec(conn_info,new_data,new_len,out_n,out_arr,out_len,out_delay);
				}

				mylog(log_trace,"out_n=%d\n",out_n);
				for(int i=0;i<out_n;i++)
				{
					delay_send(out_delay[i],dest,out_arr[i],out_len[i]);
				}
				//mylog(log_trace,"[%s] send packet\n",ip_port.to_s());

			}
			else
			{
				mylog(log_fatal,"unknown fd,this should never happen\n");
				myexit(-1);
			}
		}
		delay_manager.check();
	}
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
	rs_encode2(3,6,data,3);
	//printf("%d %d",(int)(unsigned char)arr[5][0],(int)('a'^'b'^'c'^'d'^'e'));

	for(i=0;i<6;i++)
	{
		printf("<%s>",data[i]);
	}

	data[0]=0;
	//data[1]=0;
	//data[5]=0;

	int ret=rs_decode2(3,6,data,3);
	printf("ret:%d\n",ret);

	for(i=0;i<6;i++)
	{
		printf("<%s>",data[i]);
	}
	fec_free(code);


	char arr2[6][100]=
	{
		"aaa11111","","ccc333333333"
		,"ddd444","eee5555","ff6666"
	};
	blob_encode_t blob_encode;
	for(int i=0;i<6;i++)
		blob_encode.input(arr2[i],strlen(arr2[i]));

	char **output;
	int shard_len;
	blob_encode.output(7,output,shard_len);


	printf("<shard_len:%d>",shard_len);
	blob_decode_t blob_decode;
	for(int i=0;i<7;i++)
	{
		blob_decode.input(output[i],shard_len);
	}

	char **decode_output;
	int * len_arr;
	int num;


	ret=blob_decode.output(num,decode_output,len_arr);

	printf("<num:%d,ret:%d>\n",num,ret);
	for(int i=0;i<num;i++)
	{
		char buf[1000]={0};
		memcpy(buf,decode_output[i],len_arr[i]);
		printf("<%d:%s>",len_arr[i],buf);
	}
	printf("\n");
	static fec_encode_manager_t fec_encode_manager;
	static fec_decode_manager_t fec_decode_manager;

	{

		string a = "11111";
		string b = "22";
		string c = "33333333";

		fec_encode_manager.input((char *) a.c_str(), a.length());
		fec_encode_manager.input((char *) b.c_str(), b.length());
		fec_encode_manager.input((char *) c.c_str(), c.length());
		fec_encode_manager.input(0, 0);

		int n;
		char **s_arr;
		int *len;


		fec_encode_manager.output(n,s_arr,len);
		printf("<n:%d,len:%d>",n,len[0]);

		for(int i=0;i<n;i++)
		{
			fec_decode_manager.input(s_arr[i],len[i]);
		}

		{
			int n;char ** s_arr;int* len_arr;
			fec_decode_manager.output(n,s_arr,len_arr);
			printf("<n:%d>",n);
			for(int i=0;i<n;i++)
			{
				s_arr[i][len_arr[i]]=0;
				printf("<%s>\n",s_arr[i]);
			}
		}


	}

	{
		string a = "aaaaaaa";
		string b = "bbbbbbbbbbbbb";
		string c = "ccc";


		fec_encode_manager.input((char *) a.c_str(), a.length());
		fec_encode_manager.input((char *) b.c_str(), b.length());
		fec_encode_manager.input((char *) c.c_str(), c.length());
		fec_encode_manager.input(0, 0);

		int n;
		char **s_arr;
		int * len;


		fec_encode_manager.output(n,s_arr,len);
		printf("<n:%d,len:%d>",n,len[0]);

		for(int i=0;i<n;i++)
		{
			if(i==1||i==3||i==5||i==0)
			fec_decode_manager.input(s_arr[i],len[i]);
		}

		{
			int n;char ** s_arr;int* len_arr;
			fec_decode_manager.output(n,s_arr,len_arr);
			printf("<n:%d>",n);
			for(int i=0;i<n;i++)
			{
				s_arr[i][len_arr[i]]=0;
				printf("<%s>\n",s_arr[i]);
			}
		}
	}

	for(int i=0;i<10;i++)
	{
		string a = "aaaaaaaaaaaaaaaaaaaaaaa";
		string b = "bbbbbbbbbbbbb";
		string c = "cccccccccccccccccc";



		printf("======\n");
		int n;
		char **s_arr;
		int * len;
		fec_decode_manager.output(n,s_arr,len);

		fec_encode_manager.re_init(3,2,fec_mtu,fec_pending_num,fec_pending_time,1);

		fec_encode_manager.input((char *) a.c_str(), a.length());
		fec_encode_manager.output(n,s_arr,len);
		assert(n==1);

		fec_decode_manager.input(s_arr[0],len[0]);

		fec_decode_manager.output(n,s_arr,len);
		assert(n==1);
		printf("%s\n",s_arr[0]);

		fec_encode_manager.input((char *) b.c_str(), b.length());
		fec_encode_manager.output(n,s_arr,len);
		assert(n==1);
		//fec_decode_manager.input(s_arr[0],len[0]);


		fec_encode_manager.input((char *) c.c_str(), c.length());
		fec_encode_manager.output(n,s_arr,len);

		assert(n==3);

		fec_decode_manager.input(s_arr[0],len[0]);
		//printf("n=%d\n",n);


		{
			int n;
			char **s_arr;
			int * len;
			fec_decode_manager.output(n,s_arr,len);
			assert(n==1);
			printf("%s\n",s_arr[0]);
		}

		fec_decode_manager.input(s_arr[1],len[1]);

		{
			int n;
			char **s_arr;
			int * len;
			fec_decode_manager.output(n,s_arr,len);
			assert(n==1);
			printf("n=%d\n",n);
			s_arr[0][len[0]]=0;
			printf("%s\n",s_arr[0]);
		}

	}

	return 0;
}
void print_help()
{
	char git_version_buf[100]={0};
	strncpy(git_version_buf,gitversion,10);

	printf("UDPspeeder V2\n");
	printf("git version:%s    ",git_version_buf);
	printf("build date:%s %s\n",__DATE__,__TIME__);
	printf("repository: https://github.com/wangyu-/UDPspeeder\n");
	printf("\n");
	printf("usage:\n");
	printf("    run as client : ./this_program -c -l local_listen_ip:local_port -r server_ip:server_port  [options]\n");
	printf("    run as server : ./this_program -s -l server_listen_ip:server_port -r remote_ip:remote_port  [options]\n");
	printf("\n");
	printf("common option,must be same on both sides:\n");
	printf("    -k,--key              <string>        key for simple xor encryption. if not set,xor is disabled\n");

	printf("main options:\n");
	printf("    -f,--fec              x:y             forward error correction,send y redundant packets for every x packets\n");
	printf("    --timeout             <number>        how long could a packet be held in queue before doing fec,unit: ms,default :8ms\n");
	printf("    --mode                <number>        fec-mode,available values: 0,1 ; 0 cost less bandwidth,1 cost less latency(default)\n");
	printf("    --report              <number>        turn on send/recv report,and set a period for reporting,unit:s\n");

	printf("advanced options:\n");
	printf("    --mtu                 <number>        mtu. for mode 0,the program will split packet to segment smaller than mtu_value.\n");
	printf("                                          for mode 1,no packet will be split,the program just check if the mtu is exceed.\n");
	printf("                                          default value:1250 \n");
	printf("    -j,--jitter           <number>        simulated jitter.randomly delay first packet for 0~<number> ms,default value:0.\n");
	printf("                                          do not use if you dont know what it means.\n");
	printf("    -i,--interval         <number>        scatter each fec group to a interval of <number> ms,to protect burst packet loss.\n");
	printf("                                          default value:0.do not use if you dont know what it means.\n");
	printf("    --random-drop         <number>        simulate packet loss ,unit:0.01%%. default value: 0\n");
	printf("    --disable-obscure     <number>        disable obscure,to save a bit bandwidth and cpu\n");
//	printf("    --disable-xor         <number>        disable xor\n");

	printf("developer options:\n");
	printf("    -j ,--jitter          jmin:jmax       similiar to -j above,but create jitter randomly between jmin and jmax\n");
	printf("    -i,--interval         imin:imax       similiar to -i above,but scatter randomly between imin and imax\n");
    printf("    -q,--queue-len        <number>        max fec queue len,only for mode 0\n");
    printf("    --decode-buf          <number>        size of buffer of fec decoder,unit:packet,default:2000\n");
    printf("    --fix-latency         <number>        try to stabilize latency,only for mode 0\n");
    printf("    --delay-capacity      <number>        max number of delayed packets\n");
	printf("    --disable-fec         <number>        completely disable fec,turn the program into a normal udp tunnel\n");
	printf("    --sock-buf            <number>        buf size for socket,>=10 and <=10240,unit:kbyte,default:1024\n");
	printf("log and help options:\n");
	printf("    --log-level           <number>        0:never    1:fatal   2:error   3:warn \n");
	printf("                                          4:info (default)     5:debug   6:trace\n");
	printf("    --log-position                        enable file name,function name,line number in log\n");
	printf("    --disable-color                       disable log color\n");
	printf("    -h,--help                             print this help message\n");

	//printf("common options,these options must be same on both side\n");
}
void process_arg(int argc, char *argv[])
{
	int is_client=0,is_server=0;
	int i, j, k;
	int opt;
    static struct option long_options[] =
      {
		{"log-level", required_argument,    0, 1},
		{"log-position", no_argument,    0, 1},
		{"disable-color", no_argument,    0, 1},
		{"disable-filter", no_argument,    0, 1},
		{"disable-fec", no_argument,    0, 1},
		{"disable-obscure", no_argument,    0, 1},
		{"disable-xor", no_argument,    0, 1},
		{"fix-latency", no_argument,    0, 1},
		{"sock-buf", required_argument,    0, 1},
		{"random-drop", required_argument,    0, 1},
		{"report", required_argument,    0, 1},
		{"delay-capacity", required_argument,    0, 1},
		{"mtu", required_argument,    0, 1},
		{"mode", required_argument,   0,1},
		{"timeout", required_argument,   0,1},
		{"decode-buf", required_argument,   0,1},
		{"queue-len", required_argument,   0,'q'},
		{"fec", required_argument,   0,'f'},
		{"jitter", required_argument,   0,'j'},

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
	while ((opt = getopt_long(argc, argv, "l:r:hcsk:j:f:p:n:i:",long_options,&option_index)) != -1)
	{
		//string opt_key;
		//opt_key+=opt;
		switch (opt)
		{
		case 'k':
			sscanf(optarg,"%s\n",key_string);
			mylog(log_debug,"key=%s\n",key_string);
			if(strlen(key_string)==0)
			{
				mylog(log_fatal,"key len=0??\n");
				myexit(-1);
			}
			break;
		case 'j':
			if (strchr(optarg, ':') == 0)
			{
				int jitter;
				sscanf(optarg,"%d\n",&jitter);
				if(jitter<0 ||jitter>1000*10)
				{
					mylog(log_fatal,"jitter must be between 0 and 10,000(10 second)\n");
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
			jitter_min*=1000;
			jitter_max*=1000;
			break;
		case 'i':
			if (strchr(optarg, ':') == 0)
			{
				int output_interval=-1;
				sscanf(optarg,"%d\n",&output_interval);
				if(output_interval<0||output_interval>1000*10)
				{
					mylog(log_fatal,"output_interval must be between 0 and 10,000(10 second)\n");
					myexit(-1);
				}
				output_interval_min=output_interval_max=output_interval;
			}
			else
			{
				sscanf(optarg,"%d:%d\n",&output_interval_min,&output_interval_max);
				if(output_interval_min<0 ||output_interval_max<0||output_interval_min>output_interval_max)
				{
					mylog(log_fatal," must satisfy  0<=output_interval_min<=output_interval_max\n");
					myexit(-1);
				}
			}
			output_interval_min*=1000;
			output_interval_max*=1000;
			break;
		case 'f':
			if (strchr(optarg, ':') == 0)
			{
				mylog(log_fatal,"invalid format for f");
				myexit(-1);
			}
			else
			{
				sscanf(optarg,"%d:%d\n",&fec_data_num,&fec_redundant_num);
				if(fec_data_num<1 ||fec_redundant_num<0||fec_data_num+fec_redundant_num>254)
				{
					mylog(log_fatal,"fec_data_num<1 ||fec_redundant_num<0||fec_data_num+fec_redundant_num>254\n");
					myexit(-1);
				}
			}
			break;
		case 'q':
			sscanf(optarg,"%d",&fec_pending_num);
			if(fec_pending_num<1||fec_pending_num>10000)
			{

					mylog(log_fatal,"fec_pending_num should be between 1 and 10000\n");
					myexit(-1);
			}
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
				sscanf(optarg, "%[^:]:%d", local_ip, &local_port);
			}
			else
			{
				mylog(log_fatal," -r ip:port\n");
				myexit(1);
				strcpy(local_ip, "127.0.0.1");
				sscanf(optarg, "%d", &local_port);
			}
			break;
		case 'r':
			no_r = 0;
			if (strchr(optarg, ':') != 0)
			{
				//printf("in :\n");
				//printf("%s\n",optarg);
				sscanf(optarg, "%[^:]:%d", remote_ip, &remote_port);
				//printf("%d\n",remote_port);
			}
			else
			{
				mylog(log_fatal," -r ip:port\n");
				myexit(1);
				strcpy(remote_ip, "127.0.0.1");
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
			else if(strcmp(long_options[option_index].name,"disable-fec")==0)
			{
				disable_fec=1;
			}
			else if(strcmp(long_options[option_index].name,"disable-obscure")==0)
			{
				mylog(log_info,"obscure disabled\n");
				disable_obscure=1;
			}
			else if(strcmp(long_options[option_index].name,"disable-xor")==0)
			{
				mylog(log_info,"xor disabled\n");
				disable_xor=1;
			}
			else if(strcmp(long_options[option_index].name,"fix-latency")==0)
			{
				mylog(log_info,"fix-latency enabled\n");
				fix_latency=1;
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
				mylog(log_info,"random_drop=%d\n",random_drop);
			}
			else if(strcmp(long_options[option_index].name,"delay-capacity")==0)
			{
				sscanf(optarg,"%d",&delay_capacity);

				if(delay_capacity<0)
				{
					mylog(log_fatal,"delay_capacity must be >=0 \n");
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
			else if(strcmp(long_options[option_index].name,"decode-buf")==0)
			{
				sscanf(optarg,"%d",&fec_buff_num);
				if(fec_buff_num<300 || fec_buff_num>20000)
				{
					mylog(log_fatal,"decode-buf value must be between 300 and 20000 (kbyte) \n");
					myexit(-1);
				}
				mylog(log_info,"decode-buf=%d\n",fec_buff_num);
			}
			else if(strcmp(long_options[option_index].name,"mode")==0)
			{
				sscanf(optarg,"%d",&fec_type);
				if(fec_type!=0&&fec_type!=1)
				{
					mylog(log_fatal,"mode should be 0 or 1\n");
					myexit(-1);
				}
			}
			else if(strcmp(long_options[option_index].name,"mtu")==0)
			{
				sscanf(optarg,"%d",&fec_mtu);
				if(fec_mtu<100||fec_mtu>2000)
				{
					mylog(log_fatal,"fec_mtu should be between 100 and 2000\n");
					myexit(-1);
				}
			}
			else if(strcmp(long_options[option_index].name,"timeout")==0)
			{
				sscanf(optarg,"%d",&fec_pending_time);
				if(fec_pending_time<0||fec_pending_time>1000)
				{

						mylog(log_fatal,"fec_pending_time should be between 0 and 1000(1s)\n");
						myexit(-1);
				}
				fec_pending_time*=1000;
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
	if(is_client==1)
	{
		program_mode=client_mode;
	}
	else
	{
		program_mode=server_mode;
	}

	mylog(log_info,"jitter_min=%d jitter_max=%d output_interval_min=%d output_interval_max=%d fec_pending_num=%d fec_data_num=%d fec_redundant_num=%d fec_mtu=%d fec_pending_time=%d fec_type=%d\n",
			jitter_min/1000,jitter_max/1000,output_interval_min/1000,output_interval_max/1000,fec_pending_time/1000,
			fec_data_num,fec_redundant_num,fec_mtu,fec_pending_num,fec_type);
}

int main(int argc, char *argv[])
{

	/*
	if(argc==1||argc==0)
	{
		printf("this_program classic\n");
		printf("this_program fec\n");
		return 0;
	}*/
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
	assert(sizeof(u16_t)==2);
	assert(sizeof(i16_t)==2);
	dup2(1, 2);		//redirect stderr to stdout
	int i, j, k;
	process_arg(argc,argv);

	delay_manager.set_capacity(delay_capacity);
	local_ip_uint32=inet_addr(local_ip);
	remote_ip_uint32=inet_addr(remote_ip);

	if(program_mode==client_mode)
	{
		client_event_loop();
	}
	else
	{
		server_event_loop();
	}

	return 0;
}

