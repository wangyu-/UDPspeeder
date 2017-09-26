#include "common.h"
#include "log.h"
#include "git_version.h"
#include "lib/rs.h"
#include "packet.h"
#include "connection.h"
#include "fd_manager.h"
#include "delay_manager.h"

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
u32_t local_ip_uint32,remote_ip_uint32=0;
char local_ip[100], remote_ip[100];
int local_port = -1, remote_port = -1;

u64_t last_report_time=0;
int report_interval=0;

//conn_manager_t conn_manager;
delay_manager_t delay_manager;
fd_manager_t fd_manager;

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
int client_event_loop()
{
	//char buf[buf_len];
	int i, j, k;int ret;
	int yes = 1;
	int epoll_fd;
	int remote_fd;
	fd64_t remote_fd64;

    conn_info_t conn_info;

	init_listen_socket();

	epoll_fd = epoll_create1(0);

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

	ev.events = EPOLLIN;
	ev.data.u64 = remote_fd64;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, remote_fd, &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add raw_fd error\n");
		myexit(-1);
	}

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
		    if (events[idx].data.u64 == (u64_t)local_listen_fd)
			{
				char data[buf_len];
				int data_len;
				struct sockaddr_in udp_new_addr_in={0};
				socklen_t udp_new_addr_len = sizeof(sockaddr_in);
				if ((data_len = recvfrom(local_listen_fd, data, max_data_len, 0,
						(struct sockaddr *) &udp_new_addr_in, &udp_new_addr_len)) == -1) {
					mylog(log_error,"recv_from error,this shouldnt happen at client\n");
					myexit(1);
				};

				if(data_len>=mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}
				mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port),data_len);

				ip_port_t ip_port;
				ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
				ip_port.port=ntohs(udp_new_addr_in.sin_port);
				u64_t u64=ip_port.to_u64();
				u32_t conv;

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
				}
				conn_info.conv_manager.update_active_time(conv);


				dest_t dest;
				dest.type=type_fd64_conv;
				dest.inner.fd64=remote_fd64;
				dest.conv=conv;
				my_send(dest,data,data_len);
			}
			else if (events[idx].data.u64 == remote_fd64)
			{
				char data[buf_len];
				if(!fd_manager.exist(remote_fd64))   //fd64 has been closed
				{
					continue;
				}
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
				if(data_len>mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}
				u32_t conv;
				char *new_data;
				int new_len;
				if(get_conv(conv,data,data_len,new_data,new_len)!=0)
					continue;
				if(!conn_info.conv_manager.is_conv_used(conv))continue;
				u64_t u64=conn_info.conv_manager.find_u64_by_conv(conv);
				dest_t dest;
				dest.inner.ip_port.from_u64(u64);
				dest.type=type_ip_port;
				my_send(dest,new_data,new_len);
				mylog(log_trace,"[%s] send packet\n",dest.inner.ip_port.to_s());
			}
			/*
			else if(events[idx].data.u64 ==(u64_t)timer_fd)
			{
				u64_t value;
				read(timer_fd, &value, 8);
				client_on_timer(conn_info);

				mylog(log_trace,"epoll_trigger_counter:  %d \n",epoll_trigger_counter);
				epoll_trigger_counter=0;
			}*/

			else if(events[idx].data.u64>u32_t(-1) )
			{
				assert(!fd_manager.exist(events[idx].data.u64));//this fd64 has been closed
			}
			else
			{
				mylog(log_fatal,"unknown fd,this should never happen\n");
				myexit(-1);
			}
		}
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

    conn_info_t conn_info;

	init_listen_socket();

	epoll_fd = epoll_create1(0);

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


	mylog(log_info,"now listening at %s:%d\n",my_ntoa(local_ip_uint32),local_port);
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
			else */if (events[idx].data.u64 == (u64_t)local_listen_fd)
			{
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

				if(data_len>=mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}
				mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port),data_len);

				ip_port_t ip_port;
				ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
				ip_port.port=ntohs(udp_new_addr_in.sin_port);
				if(!conn_manager.exist_ip_port(ip_port))
				{
					conn_info_t &conn_info=conn_manager.find_insert(ip_port);
					conn_info.conv_manager.reserve();
				}
				conn_info_t &conn_info=conn_manager.find_insert(ip_port);

				u32_t conv;
				char *new_data;
				int new_len;
				if(get_conv(conv,data,data_len,new_data,new_len)!=0)
					continue;

				/*
				id_t tmp_conv_id;
				memcpy(&tmp_conv_id,&data_[0],sizeof(tmp_conv_id));
				tmp_conv_id=ntohl(tmp_conv_id);*/

				if (!conn_info.conv_manager.is_conv_used(conv))
				{
					int new_udp_fd;
					ret=new_connected_socket(new_udp_fd,remote_ip_uint32,remote_port);

					if (ret != 0) {
						mylog(log_warn, "[%s:%d]new_connected_socket failed\n",my_ntoa(ip_port.ip),ip_port.port);
						continue;
					}

					fd64_t fd64 = fd_manager.create(new_udp_fd);
					ev.events = EPOLLIN;
					ev.data.u64 = fd64;
					ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_udp_fd, &ev);

					conn_info.conv_manager.insert_conv(conv, fd64);
					fd_manager.get_info(fd64).ip_port=ip_port;
					//assert(!conn_manager.exist_fd64(fd64));

					//conn_manager.insert_fd64(fd64,ip_port);
				}
				fd64_t fd64= conn_info.conv_manager.find_u64_by_conv(conv);
				//int fd=fd_manager.fd64_to_fd(fd64);
				dest_t dest;
				dest.type=type_fd64;
				dest.inner.fd64=fd64;
				my_send(dest,new_data,new_len);


				//int fd = int((u64 << 32u) >> 32u);
				//////////////////////////////todo

				//u64_t u64=((u64_t(udp_new_addr_in.sin_addr.s_addr))<<32u)+ntohs(udp_new_addr_in.sin_port);
			}
			/*
			else if ((events[idx].data.u64 >>32u) == 2u)
			{
				if(debug_flag)begin_time=get_current_time();
				int fd=get_u64_l(events[idx].data.u64);
				u64_t dummy;
				read(fd, &dummy, 8);

				if(conn_manager.timer_fd_mp.find(fd)==conn_manager.timer_fd_mp.end()) //this can happen,when fd is a just closed fd
				{
					mylog(log_info,"timer_fd no longer exits\n");
					continue;
				}
				conn_info_t* p_conn_info=conn_manager.timer_fd_mp[fd];
				u32_t ip=p_conn_info->raw_info.recv_info.src_ip;
				u32_t port=p_conn_info->raw_info.recv_info.src_port;
				assert(conn_manager.exist(ip,port));//TODO remove this for peformance

				assert(p_conn_info->state.server_current_state == server_ready); //TODO remove this for peformance

				//conn_info_t &conn_info=conn_manager.find(ip,port);
				char ip_port[40];

				sprintf(ip_port,"%s:%d",my_ntoa(ip),port);

				server_on_timer_multi(*p_conn_info,ip_port);

				if(debug_flag)
				{
					end_time=get_current_time();
					mylog(log_debug,"(events[idx].data.u64 >>32u) == 2u ,%llu,%llu,%llu  \n",begin_time,end_time,end_time-begin_time);
				}
			}*/
			else if (events[idx].data.u64 >u32_t(-1))
			{
				char data[buf_len];
				int data_len;
				fd64_t fd64=events[idx].data.u64;
				if(!fd_manager.exist(fd64))   //fd64 has been closed
				{
					continue;
				}

				//assert(conn_manager.exist_fd64(fd64));

				assert(fd_manager.exist_info(fd64));
				ip_port_t ip_port=fd_manager.get_info(fd64).ip_port;

				assert(conn_manager.exist_ip_port(ip_port));

				conn_info_t* p_conn_info=conn_manager.find_insert_p(ip_port);

				conn_info_t &conn_info=*p_conn_info;

				assert(conn_info.conv_manager.is_u64_used(fd64));

				u32_t conv=conn_info.conv_manager.find_conv_by_u64(fd64);

				int fd=fd_manager.to_fd(fd64);
				data_len=recv(fd,data,max_data_len,0);

				mylog(log_trace,"received a packet from udp_fd,len:%d\n",data_len);

				if(data_len<0)
				{
					mylog(log_debug,"udp fd,recv_len<0 continue,%s\n",strerror(errno));

					continue;
				}


				if(data_len>=mtu_warn)
				{
					mylog(log_warn,"huge packet,data len=%d (>=%d).strongly suggested to set a smaller mtu at upper level,to get rid of this warn\n ",data_len,mtu_warn);
				}

				dest_t dest;
				dest.type=type_ip_port_conv;
				dest.conv=conv;
				dest.inner.ip_port=ip_port;
				my_send(dest,data,data_len);
				mylog(log_trace,"[%s] send packet\n",ip_port.to_s());

			}
			else
			{
				mylog(log_fatal,"unknown fd,this should never happen\n");
				myexit(-1);
			}

		}
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
	int is_client=0,is_server=0;
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
	if(is_client==1)
	{
		program_mode=client_mode;
	}
	else
	{
		program_mode=server_mode;
	}
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
	dup2(1, 2);		//redirect stderr to stdout
	int i, j, k;
	process_arg(argc,argv);
	delay_manager.capacity=max_pending_packet;
	init_random_number_fd();

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

