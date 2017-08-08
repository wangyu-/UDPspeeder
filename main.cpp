#include "common.h"
#include "log.h"

using  namespace std;

typedef unsigned long long u64_t;   //this works on most platform,avoid using the PRId64
typedef long long i64_t;

typedef unsigned int u32_t;
typedef int i32_t;

//const u32_t anti_replay_window_size=1000;
typedef u64_t anti_replay_seq_t;
int disable_anti_replay=0;
int dup_num=3;
int dup_delay=900;   //ms
int iv_min=2;
int iv_max=30;//< 256;
int random_number_fd=-1;

int remote_fd=-1;
int local_fd=-1;
int is_client = 0, is_server = 0;
int local_listen_fd=-1;

int disable_conv_clear=0;

u32_t remote_address_uint32=0;

char local_address[100], remote_address[100];
int local_port = -1, remote_port = -1;
int multi_process_mode=0;

int VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV;

struct anti_replay_t
{
	u64_t max_packet_received;
	char window[anti_replay_window_size];
	anti_replay_seq_t anti_replay_seq;
	anti_replay_seq_t get_new_seq_for_send()
	{
		return anti_replay_seq++;
	}
	anti_replay_t()
	{
		max_packet_received=0;
		anti_replay_seq=0;//random first seq
		//memset(window,0,sizeof(window)); //not necessary
	}
	void re_init()
	{
		max_packet_received=0;
		//memset(window,0,sizeof(window));
	}

	int is_vaild(u64_t seq)
	{
		if(disable_anti_replay) return 1;
		//if(disabled) return 0;

		if(seq==max_packet_received) return 0;
		else if(seq>max_packet_received)
		{
			if(seq-max_packet_received>=anti_replay_window_size)
			{
				memset(window,0,sizeof(window));
				window[seq%anti_replay_window_size]=1;
			}
			else
			{
				for (u64_t i=max_packet_received+1;i<seq;i++)
					window[i%anti_replay_window_size]=0;
				window[seq%anti_replay_window_size]=1;
			}
			max_packet_received=seq;
			return 1;
		}
		else if(seq<max_packet_received)
		{
			if(max_packet_received-seq>=anti_replay_window_size) return 0;
			else
			{
				if (window[seq%anti_replay_window_size]==1) return 0;
				else
				{
					window[seq%anti_replay_window_size]=1;
					return 1;
				}
			}
		}


		return 0; //for complier check
	}
}anti_replay;
struct conn_manager_t  //TODO change map to unordered map
{
	//typedef hash_map map;
	unordered_map<u64_t,u32_t> u64_to_fd;  //conv and u64 are both supposed to be uniq
	unordered_map<u32_t,u64_t> fd_to_u64;

	unordered_map<u32_t,u64_t> fd_last_active_time;

	unordered_map<u32_t,u64_t>::iterator clear_it;

	unordered_map<u32_t,u64_t>::iterator it;
	unordered_map<u32_t,u64_t>::iterator old_it;

	//void (*clear_function)(uint64_t u64) ;

	long long last_clear_time;

	conn_manager_t()
	{
		clear_it=fd_last_active_time.begin();
		long long last_clear_time=0;
		//clear_function=0;
	}
	~conn_manager_t()
	{
		clear();
	}
	int get_size()
	{
		return fd_to_u64.size();
	}
	void reserve()
	{
		u64_to_fd.reserve(10007);
		fd_to_u64.reserve(10007);
		fd_last_active_time.reserve(10007);
	}
	void clear()
	{
		if(disable_conv_clear) return ;

		for(it=fd_to_u64.begin();it!=fd_to_u64.end();it++)
		{
			//int fd=int((it->second<<32u)>>32u);
			close(  it->first);
		}
		u64_to_fd.clear();
		fd_to_u64.clear();
		fd_last_active_time.clear();

		clear_it=fd_last_active_time.begin();

	}
	int exist_fd(u32_t fd)
	{
		return fd_to_u64.find(fd)!=fd_to_u64.end();
	}
	int exist_u64(u64_t u64)
	{
		return u64_to_fd.find(u64)!=u64_to_fd.end();
	}
	u32_t find_fd_by_u64(u64_t u64)
	{
		return u64_to_fd[u64];
	}
	u64_t find_u64_by_fd(u32_t fd)
	{
		return fd_to_u64[fd];
	}
	int update_active_time(u32_t fd)
	{
		return fd_last_active_time[fd]=get_current_time();
	}
	int insert_fd(u32_t fd,u64_t u64)
	{
		u64_to_fd[u64]=fd;
		fd_to_u64[fd]=u64;
		fd_last_active_time[fd]=get_current_time();
		return 0;
	}
	int erase_fd(u32_t fd)
	{
		if(disable_conv_clear) return 0;
		u64_t u64=fd_to_u64[fd];

		close(fd);

		fd_to_u64.erase(fd);
		u64_to_fd.erase(u64);
		fd_last_active_time.erase(fd);
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
		int size=fd_last_active_time.size();
		int num_to_clean=size/conv_clear_ratio+conv_clear_min;   //clear 1/10 each time,to avoid latency glitch

		u64_t current_time=get_current_time();
		for(;;)
		{
			if(cnt>=num_to_clean) break;
			if(fd_last_active_time.begin()==fd_last_active_time.end()) break;

			if(it==fd_last_active_time.end())
			{
				it=fd_last_active_time.begin();
			}

			if( current_time -it->second  >conv_timeout )
			{
				//mylog(log_info,"inactive conv %u cleared \n",it->first);
				old_it=it;
				it++;
				u32_t fd= old_it->first;
				erase_fd(old_it->first);
				if(ip_port==0)
				{
					mylog(log_info,"fd %x cleared\n",fd);
				}
				else
				{
					mylog(log_info,"[%s]fd %x cleared\n",ip_port,fd);
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
}conn_manager;

typedef u64_t my_time;
/*struct my_time:timespec
{
    bool operator <(const my_time& other)const
     {
        if(tv_sec<other.tv_sec) return true;
        else if(tv_sec>other.tv_sec) return false;
        else return tv_nsec<other.tv_nsec;
     }
    bool operator ==(const my_time& other)const
     {
        if(tv_sec==other.tv_sec&&tv_nsec==other.tv_nsec) return true;
        return false;
     }
};*/
struct delay_data
{
	int fd;
	int times_left;
	char * data;
	int len;
};
int delay_timer_fd;

multimap<my_time,delay_data> delay_mp;

my_time time_after_delay(my_time time)
{
	time+=dup_delay*1000;
	return time;
}


int add_to_delay_mp(int fd,int times_left,char * buf,int len)
{
	delay_data tmp;
	tmp.data = buf;
	tmp.fd = fd;
	tmp.times_left = times_left;
	tmp.len = len;

	my_time tmp_time=get_current_time_us();
	//clock_gettime(CLOCK_MONOTONIC, &tmp_time);
	tmp_time=time_after_delay(tmp_time);
	delay_mp.insert(make_pair(tmp_time,tmp));
	return 0;
}
int add_and_new(int fd,int times_left,char * buf,int len)
{
	char * str= (char *)malloc(len);
	memcpy(str,buf,len);
	add_to_delay_mp(fd,times_left,str,len);
	return 0;
}

multimap<u64_t,delay_data> new_delay_mp;



void handler(int num) {
	int status;
	int pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status)) {
			//printf("The child exit with code %d",WEXITSTATUS(status));
		}
	}

}
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
	if(data_len<sizeof(seq)) return -1;
	data_len-=sizeof(seq);
	memcpy(&seq,data+data_len,sizeof(seq));
	seq=ntoh64(seq);
	if(anti_replay.is_vaild(seq)==0)
	{
		//return -1;   //TODO for test
	}
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
	for(i=0,j=0;i<in_len;i++,j++)
	{
		if(j==iv_len) j=0;
		output[iv_len+i]^=output[j];
	}
	output[iv_len+in_len]^=output[0];
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
		printf("error1,%d",in_len);
		return -1;
	}
	int iv_len= int ((uint8_t)(input[in_len-1]^input[0]) );
	out_len=in_len-1-iv_len;
	if(out_len<0)
	{
		printf("error2,%d %d",in_len,out_len);
		return -1;
	}
	for(i=0,j=0;i<in_len;i++,j++)
	{
		if(j==iv_len) j=0;
		output[i]=input[iv_len+i]^input[j];
	}
	return 0;
}
void check_delay_map()
{
	//printf("<<<begin");
	if(!delay_mp.empty())
	{
		my_time current_time;

		multimap<my_time,delay_data>::iterator it;
		//printf("<map_size:%d>",delay_mp.size());
		//lfflush(stdout);
		while(1)
		{
			int ret;
			it=delay_mp.begin();
			if(it==delay_mp.end()) break;

			current_time=get_current_time_us();
			if(it->first < current_time||it->first ==current_time)
			{
				//send packet
				printf("<%d>",it->second.len);
				if(multi_process_mode)
				{
					if ((is_client && it->second.fd == remote_fd)
							|| (is_server && it->second.fd == local_fd)) {
						char new_data[buf_len];
						int new_len;
						do_obscure(it->second.data, it->second.len, new_data,
								new_len);
						ret = send(it->second.fd, new_data, new_len, 0);
					} else {
						ret = send(it->second.fd, it->second.data,
								it->second.len, 0);
					}

					if (ret < 0) {
						printf("send return %d at @300", ret);
						exit(1);
					}
				}
				else
				{
					if(is_client)
					{
						char new_data[buf_len];
						int new_len;
						do_obscure(it->second.data, it->second.len, new_data,
								new_len);
						ret = send(it->second.fd, new_data, new_len, 0);
					}
					else
					{

						if(conn_manager.exist_fd(it->second.fd))
						{
							u64_t u64=conn_manager.find_u64_by_fd(it->second.fd);

							sockaddr_in tmp_sockaddr;

							memset(&tmp_sockaddr,0,sizeof(tmp_sockaddr));
							tmp_sockaddr.sin_family = AF_INET;
							tmp_sockaddr.sin_addr.s_addr = (u64 >> 32u);

							tmp_sockaddr.sin_port = htons(uint16_t((u64 << 32u) >> 32u));


							char new_data[buf_len];
							int new_len;
							do_obscure(it->second.data, it->second.len, new_data,
									new_len);

							ret = sendto(local_listen_fd, new_data,
									new_len , 0,
									(struct sockaddr *) &tmp_sockaddr,
									sizeof(tmp_sockaddr));
							//ret = send(it->second.fd, it->second.data,
								//	it->second.len, 0);
						}
						else
						{
							it->second.times_left=0;
						}
					}
					if (ret < 0) {
						printf("send return %d at @300", ret);
					}
				}

				if(it->second.times_left>1)
				{
					//delay_mp.insert(pair<my_time,delay_data>(current_time));
					add_to_delay_mp(it->second.fd,it->second.times_left-1,it->second.data,it->second.len);
				}
				else
				{
					free(it->second.data);
				}
				delay_mp.erase(it);
			}
			else
			{
				break;
			}

		}
		if(!delay_mp.empty())
		{
			itimerspec its;
			memset(&its.it_interval,0,sizeof(its.it_interval));
			its.it_value.tv_sec=delay_mp.begin()->first/1000000llu;
			its.it_value.tv_nsec=(delay_mp.begin()->first%1000000llu)*1000llu;
			timerfd_settime(delay_timer_fd,TFD_TIMER_ABSTIME,&its,0);
		}
	}
	//printf("end");
}
int create_new_udp(int &new_udp_fd)
{
	struct sockaddr_in remote_addr_in;

	socklen_t slen = sizeof(sockaddr_in);
	memset(&remote_addr_in, 0, sizeof(remote_addr_in));
	remote_addr_in.sin_family = AF_INET;
	remote_addr_in.sin_port = htons(remote_port);
	remote_addr_in.sin_addr.s_addr = remote_address_uint32;

	new_udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (new_udp_fd < 0) {
		mylog(log_warn, "create udp_fd error\n");
		return -1;
	}
	setnonblocking(new_udp_fd);
	set_buf_size(new_udp_fd);

	mylog(log_debug, "created new udp_fd %d\n", new_udp_fd);
	int ret = connect(new_udp_fd, (struct sockaddr *) &remote_addr_in, slen);
	if (ret != 0) {
		mylog(log_warn, "udp fd connect fail\n");
		close(new_udp_fd);
		return -1;
	}
	return 0;
}
int set_timer(int epollfd,int &timer_fd)
{
	int ret;
	epoll_event ev;

	itimerspec its;
	memset(&its,0,sizeof(its));

	if((timer_fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK)) < 0)
	{
		mylog(log_fatal,"timer_fd create error\n");
		myexit(1);
	}
	its.it_interval.tv_sec=(timer_interval/1000);
	its.it_interval.tv_nsec=(timer_interval%1000)*1000ll*1000ll;
	its.it_value.tv_nsec=1; //imidiately
	timerfd_settime(timer_fd,0,&its,0);


	ev.events = EPOLLIN;
	ev.data.u64 = timer_fd;

	ret=epoll_ctl(epollfd, EPOLL_CTL_ADD, timer_fd, &ev);
	if (ret < 0) {
		mylog(log_fatal,"epoll_ctl return %d\n", ret);
		myexit(-1);
	}
	return 0;
}
int event_loop()
{
	struct sockaddr_in local_me, local_other;
	local_listen_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int yes = 1;
	//setsockopt(local_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	set_buf_size(local_listen_fd);
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
		exit(1);
	}

	int epollfd = epoll_create1(0);
	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epollfd < 0)
	{
		mylog(log_fatal,"epoll created return %d\n", epollfd);
		exit(-1);
	}
	ev.events = EPOLLIN;
	ev.data.fd = local_listen_fd;
	int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, local_listen_fd, &ev);

	if(ret!=0)
	{
		mylog(log_fatal,"epoll created return %d\n", epollfd);
		exit(-1);
	}
	int clear_timer_fd=-1;
	set_timer(epollfd,clear_timer_fd);



	if ((delay_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
	{
		printf("timer_fd create error");
		exit(1);
	}
	ev.events = EPOLLIN;
	ev.data.fd = delay_timer_fd;

	itimerspec zero_its;
	memset(&zero_its, 0, sizeof(zero_its));

	timerfd_settime(delay_timer_fd, TFD_TIMER_ABSTIME, &zero_its, 0);
	epoll_ctl(epollfd, EPOLL_CTL_ADD, delay_timer_fd, &ev);
	if (ret < 0)
	{
		printf("epoll_ctl return %d\n", ret);
		exit(-1);
	}

	for (;;)
	{
		int nfds = epoll_wait(epollfd, events, max_events, 180 * 1000); //3mins
		if (nfds < 0)
		{
			mylog(log_fatal,"epoll_wait return %d\n", nfds);
			exit(-1);
		}
		int n;
		for (n = 0; n < nfds; ++n)
		{
			if (events[n].data.fd == local_listen_fd) //data income from local end
			{
				char data[buf_len];
				int data_len;
				if ((data_len = recvfrom(local_listen_fd, data, buf_len, 0,
						(struct sockaddr *) &local_other, &slen)) == -1) //<--first packet from a new ip:port turple
				{
					printf("recv_from error");
					exit(1);
				}
				data[data_len] = 0; //for easier debug
				u64_t u64=pack_u64(local_other.sin_addr.s_addr,ntohs(local_other.sin_port));

				if(!conn_manager.exist_u64(u64))
				{
					int new_udp_fd;
					if(create_new_udp(new_udp_fd)!=0)
					{
						continue;
					}
					struct epoll_event ev;

					mylog(log_trace, "u64: %lld\n", u64);
					ev.events = EPOLLIN;

					ev.data.fd = new_udp_fd;

					ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, new_udp_fd, &ev);
					if (ret != 0) {
						mylog(log_warn, "add udp_fd error \n");
						//perror("why?");
						close(new_udp_fd);
						continue;
					}
					mylog(log_info,"created new udp\n");
					conn_manager.insert_fd(new_udp_fd,u64);
				}


				int new_udp_fd=conn_manager.find_fd_by_u64(u64);
				conn_manager.update_active_time(new_udp_fd);
				int ret;
				if(is_client)
				{
					add_seq(data,data_len);
					char new_data[buf_len];
					int new_len;
					do_obscure(data, data_len, new_data, new_len);
					ret = send(new_udp_fd, new_data,new_len, 0);

					if(dup_num>1)
					{
						add_and_new(new_udp_fd, dup_num - 1, data, data_len);
					}
				}
				else
				{
					char new_data[buf_len];
					int new_len;
					if (de_obscure(data, data_len, new_data, new_len) != 0) {
						printf("error at line %d    ,data_len=%d \n", __LINE__,data_len);
						continue;
					}

					if (remove_seq(new_data, new_len) != 0) {
						printf("error at line %d\n", __LINE__);
						continue;
					}

					ret = send(new_udp_fd, new_data,new_len, 0);
				}

				if (ret < 0) {
					mylog(log_warn, "send returned %d\n", ret);
					//perror("what happened????");
				}
			}
			else if(events[n].data.fd == clear_timer_fd)
			{
				u64_t value;
				read(clear_timer_fd, &value, 8);
				mylog(log_debug, "timer!\n");
				conn_manager.clear_inactive();
			}
			else if (events[n].data.fd == delay_timer_fd)
			{
				uint64_t value;
				read(delay_timer_fd, &value, 8);
				//printf("<timerfd_triggered, %d>",delay_mp.size());
				//fflush(stdout);
			}
			else
			{
				int udp_fd=events[n].data.fd;
				if(!conn_manager.exist_fd(udp_fd)) continue;

				char data[buf_len];
				int data_len =recv(udp_fd,data,buf_len,0);
				if(data_len<0)
				{
					mylog(log_warn, "recv failed %d\n", data_len);
					continue;
				}
				assert(conn_manager.exist_fd(udp_fd));

				conn_manager.update_active_time(udp_fd);

				u64_t u64=conn_manager.find_u64_by_fd(udp_fd);

				sockaddr_in tmp_sockaddr;

				memset(&tmp_sockaddr,0,sizeof(tmp_sockaddr));
				tmp_sockaddr.sin_family = AF_INET;
				tmp_sockaddr.sin_addr.s_addr = (u64 >> 32u);

				tmp_sockaddr.sin_port = htons(uint16_t((u64 << 32u) >> 32u));

				if(is_client)
				{
					char new_data[buf_len];
					int new_len;
					if (de_obscure(data, data_len, new_data, new_len) != 0) {
						printf("error at line %d    ,data_len=%d \n", __LINE__,data_len);
						continue;
					}

					if (remove_seq(new_data, new_len) != 0) {
						printf("error at line %d\n", __LINE__);
						continue;
					}

					ret = sendto(local_listen_fd, new_data,
							new_len , 0,
							(struct sockaddr *) &tmp_sockaddr,
							sizeof(tmp_sockaddr));
				}
				else
				{
					add_seq(data,data_len);
					char new_data[buf_len];
					int new_len;
					do_obscure(data, data_len, new_data, new_len);

					if(dup_num>1)
					{
						add_and_new(udp_fd, dup_num - 1, data, data_len);
					}

					ret = sendto(local_listen_fd, new_data,
							new_len , 0,
							(struct sockaddr *) &tmp_sockaddr,
							sizeof(tmp_sockaddr));
				}
				if (ret < 0) {
					mylog(log_warn, "sento returned %d\n", ret);
					//perror("ret<0");
				}
				mylog(log_trace, "%s :%d\n", inet_ntoa(tmp_sockaddr.sin_addr),
						ntohs(tmp_sockaddr.sin_port));
				mylog(log_trace, "%d byte sent\n", ret);

			}
		}
		check_delay_map();
	}
	exit(0);
	return 0;
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
		{NULL, 0, 0, 0}
      };
    int option_index = 0;

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
	printf("argc=%d ", argc);
	for (i = 0; i < argc; i++)
		printf("%s ", argv[i]);
	printf("\n");
	if (argc == 1)
	{
		printf(
				"proc -c/-s -l ip:port -r ip:port  [-n dup_times] [-t dup_delay(1000=1ms)] \n");
		exit( -1);
	}
	int no_l = 1, no_r = 1;
	while ((opt = getopt_long(argc, argv, "l:r:d:t:hcsp",long_options,&option_index)) != -1)
	{
		//string opt_key;
		//opt_key+=opt;
		switch (opt)
		{
		case 'p':
			multi_process_mode=1;
			break;
		case 'd':
			dup_num=-1;
			sscanf(optarg,"%d\n",&dup_num);
			if(dup_num<1 ||dup_num>10)
			{
				printf("dup_num must be between 1 and 10\n");
				exit(-1);
			}
			break;
		case 't':
			dup_delay=-1;
			sscanf(optarg,"%d\n",&dup_delay);
			if(dup_delay<1||dup_delay>1000*1000)
			{
				printf("dup_delay must be between 1 and 10\n");
				exit(-1);
			}
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
				printf(" -r ip:port\n");
				exit(1);
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
				printf(" -r ip:port\n");
				exit(1);
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
			else if(strcmp(long_options[option_index].name,"disable-color")==0)
			{
				//enable_log_color=0;
			}
			else if(strcmp(long_options[option_index].name,"log-position")==0)
			{
				enable_log_position=1;
			}
			break;
		default:
			printf("ignore unknown <%s>", optopt);
		}
	}

	if (no_l)
		printf("error: -i not found\n");
	if (no_r)
		printf("error: -o not found\n");
	if (no_l || no_r)
		exit(-1);
	if (is_client == 0 && is_server == 0)
	{
		printf("-s -c hasnt been set\n");
		exit(-1);
	}
	if (is_client == 1 && is_server == 1)
	{
		printf("-s -c cant be both set\n");
		exit(-1);
	}
}
int multi_process()
{
	struct sockaddr_in local_me, local_other;
	local_listen_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int yes = 1;
	setsockopt(local_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	set_buf_size(local_listen_fd);

	char data[buf_len];
	//char *data=data0;
	socklen_t slen = sizeof(sockaddr_in);
	memset(&local_me, 0, sizeof(local_me));
	local_me.sin_family = AF_INET;
	local_me.sin_port = htons(local_port);
	local_me.sin_addr.s_addr = inet_addr(local_address);
	if (bind(local_listen_fd, (struct sockaddr*) &local_me, slen) == -1)
	{
		printf("socket bind error");
		exit(1);
	}
	while (1)
	{
		int data_len;
		if ((data_len = recvfrom(local_listen_fd, data, buf_len, 0,
				(struct sockaddr *) &local_other, &slen)) == -1) //<--first packet from a new ip:port turple
		{
			printf("recv_from error");
			exit(1);
		}

		printf("received packet from %s:%d\n", inet_ntoa(local_other.sin_addr),
				ntohs(local_other.sin_port));

		data[data_len] = 0;
		printf("recv_len: %d\n", data_len);
		fflush(stdout);

		if (is_server)
		{
			char new_data[buf_len];
			int new_len;
			if(de_obscure(data,data_len,new_data,new_len)!=0)
			{
				printf("remove_padding error!\n");
				continue;
			}
			memcpy(data,new_data,new_len);
			data_len=new_len;
			if (remove_seq(data, data_len) != 0)
			{
				printf("remove_seq error!\n");
				continue;
			}


			//data=new_data;
		}

		local_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		//local_me.sin_addr.s_addr=inet_addr("127.0.0.1");
		setsockopt(local_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		if (bind(local_fd, (struct sockaddr*) &local_me, slen) == -1) //偷懒的方法，有潜在问题
		{
			printf("socket bind error in chilld");
			exit(1);
		}
		int ret = connect(local_fd, (struct sockaddr *) &local_other, slen); //偷懒的方法，有潜在问题
		if (fork() == 0)  //子
		{
			if (ret != 0)
			{
				printf("connect return %d @1\n", ret);
				exit(1);
			}
			close(local_listen_fd);

			struct sockaddr_in remote_me, remote_other;
			memset(&remote_other, 0, sizeof(remote_other));
			remote_other.sin_family = AF_INET;
			//printf("remote_address=%s  remote_port=%d\n",remote_address,remote_port);
			remote_other.sin_port = htons(remote_port);
			remote_other.sin_addr.s_addr = inet_addr(remote_address);
			remote_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			ret = connect(remote_fd, (struct sockaddr *) &remote_other, slen);
			if (ret != 0)
			{
				printf("connect return %d @2\n", ret);
				exit(1);
			}
			if (is_client)
			{
				add_seq(data, data_len);
				char new_data[buf_len];
				int new_len;

				do_obscure(data, data_len, new_data, new_len);
				ret = send(remote_fd, new_data, new_len, 0); //<----send the packet receved by father process  ,only for this packet
				printf("send return %d\n", ret);
				if(dup_num > 1)
				{
					add_and_new(remote_fd, dup_num - 1, data, data_len);
				}
			}
			else
			{
				ret = send(remote_fd, data, data_len, 0);
				printf("send return %d\n", ret);
			}

			if (ret < 0)
				exit(-1);


			setnonblocking(remote_fd);
			set_buf_size(remote_fd);

			setnonblocking(local_fd);
			set_buf_size(local_fd);

			int epollfd = epoll_create1(0);
			const int max_events = 4096;
			struct epoll_event ev, events[max_events];
			if (epollfd < 0)
			{
				printf("epoll return %d\n", epollfd);
				exit(-1);
			}
			ev.events = EPOLLIN;
			ev.data.fd = local_fd;
			ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, local_fd, &ev);
			if (ret < 0)
			{
				printf("epoll_ctl return %d\n", ret);
				exit(-1);
			}
			ev.events = EPOLLIN;
			ev.data.fd = remote_fd;
			ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, remote_fd, &ev);
			if (ret < 0)
			{
				printf("epoll_ctl return %d\n", ret);
				exit(-1);
			}

			if ((delay_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
			{
				printf("timer_fd create error");
				exit(1);
			}
			ev.events = EPOLLIN;
			ev.data.fd = delay_timer_fd;

			itimerspec zero_its;
			memset(&zero_its, 0, sizeof(zero_its));

			timerfd_settime(delay_timer_fd, TFD_TIMER_ABSTIME, &zero_its, 0);
			epoll_ctl(epollfd, EPOLL_CTL_ADD, delay_timer_fd, &ev);
			if (ret < 0)
			{
				printf("epoll_ctl return %d\n", ret);
				exit(-1);
			}

			check_delay_map();

			for (;;)
			{
				int nfds = epoll_wait(epollfd, events, max_events, 180 * 1000); //3mins
				if (nfds <= 0)
				{
					printf("epoll_wait return %d\n", nfds);
					exit(-1);
				}
				int n;
				for (n = 0; n < nfds; ++n)
				{
					if (events[n].data.fd == local_fd) //data income from local end
					{
						data_len = recv(local_fd, data, buf_len, 0);
						if (data_len < 0)
						{
							printf("recv return %d @1", data_len);
							exit(1);
						}

						data[data_len] = 0;
						printf("len %d received from child@1\n", data_len);
						//printf("%s received from child@1\n",buf);

						//printf("before send %s\n",buf);
						if(is_client)
						{
							add_seq(data,data_len);
							char new_data[buf_len];
							int new_len;
							do_obscure(data, data_len, new_data, new_len);
							ret = send(remote_fd, new_data, new_len, 0);
							if(dup_num>1)
							{
								add_and_new(remote_fd, dup_num - 1, data, data_len);
							}
						}
						else
						{
							char new_data[buf_len];
							int new_len;
							if(de_obscure(data,data_len,new_data,new_len)!=0) {printf("error at line %d\n",__LINE__);continue;}

							if(remove_seq(new_data,new_len)!=0) {printf("error at line %d\n",__LINE__);continue;}

							ret = send(remote_fd, new_data, new_len, 0);
						}
						if (ret < 0)
						{
							printf("send return %d at @1", ret);
							exit(1);
						}


					}
					else if (events[n].data.fd == remote_fd)
					{
						data_len = recv(remote_fd, data, buf_len, 0);
						if (data_len < 0)
						{
							printf("recv return -1 @2", data_len);
							exit(1);
						}

						data[data_len] = 0;
						printf("len %d received from child@1\n", data_len);
						//printf("%s received from child@2\n",buf);
						if(is_client)
						{
							char new_data[buf_len];
							int new_len;
							if(de_obscure(data,data_len,new_data,new_len)!=0) {printf("error at line %d\n",__LINE__);continue;}

							if(remove_seq(new_data,new_len)!=0) {printf("error at line %d\n",__LINE__);continue;}


							ret = send(local_fd, new_data, new_len, 0);
						}
						else
						{
							add_seq(data,data_len);
							char new_data[buf_len];
							int new_len;
							do_obscure(data, data_len, new_data, new_len);
							ret = send(local_fd, new_data, new_len, 0);
							if(dup_num>1)
							{
								add_and_new(local_fd, dup_num - 1, data, data_len);
							}
						}

						if (ret < 0)
						{
							printf("send return %d @2", ret);
							exit(1);
						}
					}
					else if (events[n].data.fd == delay_timer_fd)
					{
						uint64_t value;
						read(delay_timer_fd, &value, 8);
						//printf("<timerfd_triggered, %d>",delay_mp.size());
						//fflush(stdout);
					}
				}						//end for n = 0; n < nfds
				check_delay_map();
			}
			exit(0);
		}
		else //if(fork()==0)  ... else
		{ //fork 's father process
			close(local_fd); //father process only listen to local_listen_fd,so,close this fd
		}
	}  //while(1)end

}
int main(int argc, char *argv[])
{
	//printf("%lld\n",get_current_time_us());

	//printf("%lld\n",get_current_time_us());

	//printf("%lld\n",get_current_time_us());

	//printf("%lld\n",get_current_time());
	dup2(1, 2);		//redirect stderr to stdout
	int i, j, k;
	process_arg(argc,argv);


	init_random_number_fd();

	signal(SIGCHLD, handler);

	mylog(log_info,"test\n");

	remote_address_uint32=inet_addr(remote_address);


	if(!multi_process_mode)
	{
		event_loop();
	}
	else
	{
		multi_process();
	}


	return 0;
}

