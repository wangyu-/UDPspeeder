#include<stdio.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<getopt.h>
#include <unistd.h>
#include<errno.h>

#include <fcntl.h>
//#include"aes.h"

#include <sys/epoll.h>
#include <sys/wait.h>

#include<map>
#include<string>
#include<vector>
using namespace std;

#include <sys/time.h>
#include <time.h>

#include <sys/timerfd.h>


typedef unsigned long long u64_t;   //this works on most platform,avoid using the PRId64
typedef long long i64_t;

typedef unsigned int u32_t;
typedef int i32_t;

const u32_t anti_replay_window_size=1000;
typedef u64_t anti_replay_seq_t;
int disable_anti_replay=0;
int dup_num=3;
int dup_delay=5000;   //1000 = 1ms
int iv_min=2;
int iv_max=30;//< 256;
int random_number_fd=-1;

int remote_fd=-1;
int local_fd=-1;
int is_client = 0, is_server = 0;


int VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV;
void setnonblocking(int sock) {
	int opts;
	opts = fcntl(sock, F_GETFL);

	if (opts < 0) {
		perror("fcntl(sock,GETFL)");
		exit(1);
	}

	opts = opts | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(sock,SETFL,opts)");
		exit(1);
	}
}
void init_random_number_fd()
{

	random_number_fd=open("/dev/urandom",O_RDONLY);

	if(random_number_fd==-1)
	{
		printf("error open /dev/urandom\n");
	}
	setnonblocking(random_number_fd);
}
void get_true_random_chars(char * s,int len)
{
	int size=read(random_number_fd,s,len);
	if(size!=len)
	{
		printf("get random number failed\n");
		exit(-1);
	}
}
u32_t get_true_random_number()
{
	u32_t ret;
	int size=read(random_number_fd,&ret,sizeof(ret));
	if(size!=sizeof(ret))
	{
		printf("get random number failed %d\n",size);
		exit(-1);
	}
	return ret;
}
u64_t ntoh64(u64_t a)
{
	if(__BYTE_ORDER == __LITTLE_ENDIAN)
	{
		return __bswap_64( a);
	}
	else return a;

}
u64_t hton64(u64_t a)
{
	if(__BYTE_ORDER == __LITTLE_ENDIAN)
	{
		return __bswap_64( a);
	}
	else return a;
}
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

struct my_time:timespec
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
};
struct delay_data
{
	int fd;
	int times_left;
	char * data;
	int len;
};
int timer_fd;

multimap<my_time,delay_data> delay_mp;

my_time time_after_delay(my_time time)
{
	time.tv_nsec+=dup_delay*1000ll;  //8ms
	if(time.tv_nsec>=1000*1000*1000ll )
	{
		time.tv_nsec-=1000*1000*1000ll;
		time.tv_sec+=1;
	}
	return time;
}
int add_to_delay_mp(int fd,int times_left,char * buf,int len)
{
	delay_data tmp;
	tmp.data = buf;
	tmp.fd = fd;
	tmp.times_left = times_left;
	tmp.len = len;

	my_time tmp_time;
	clock_gettime(CLOCK_MONOTONIC, &tmp_time);
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

char local_address[100], remote_address[100];
int local_port = -1, remote_port = -1;
//char keya[100], keyb[100];
//int dup_a = 1, dup_b = 1;
//char iv[100];

const int buf_len = 20480;

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
			ret=clock_gettime(CLOCK_MONOTONIC, &current_time);
			if(ret!=0)
			{
				printf("unknown error\n");
				exit(1);
			}
			if(it->first < current_time||it->first ==current_time)
			{
				//send packet
				printf("<%d>",it->second.len);
				if(  (is_client  &&it->second.fd==remote_fd )   || (is_server  &&it->second.fd==local_fd ) )
				{
					char new_data[buf_len];int new_len;
					do_obscure(it->second.data,it->second.len,new_data,new_len);
					ret = send(it->second.fd, new_data, new_len, 0);
				}
				else
				{
					ret = send(it->second.fd, it->second.data, it->second.len, 0);
				}

				if (ret < 0) {
					printf("send return %d at @300", ret);
					exit(1);
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
			its.it_value=delay_mp.begin()->first;
			timerfd_settime(timer_fd,TFD_TIMER_ABSTIME,&its,0);
		}
	}
	//printf("end");
}
int set_buf_size(int fd)
{
	int socket_buf_size=1024*1024;
    if(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socket_buf_size, sizeof(socket_buf_size))<0)
    //if(setsockopt(fd, SOL_SOCKET, SO_SNDBUFFORCE, &socket_buf_size, sizeof(socket_buf_size))<0)
    {
    	printf("set SO_SNDBUF fail\n");
    	exit(1);
    }
    //if(setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &socket_buf_size, sizeof(socket_buf_size))<0)
    if(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buf_size, sizeof(socket_buf_size))<0)
    {
    	printf("set SO_RCVBUF fail\n");
    	exit(1);
    }
	return 0;
}
int main(int argc, char *argv[])
{
	dup2(1, 2);		//redirect stderr to stdout


	init_random_number_fd();

	int i, j, k;
	int opt;
	signal(SIGCHLD, handler);

	printf("argc=%d ", argc);
	for (i = 0; i < argc; i++)
		printf("%s ", argv[i]);
	printf("\n");
	if (argc == 1)
	{
		printf(
				"proc -c/-s -l ip:port -r ip:port  [-n dup_times] [-t dup_delay(1000=1ms)] \n");
		return -1;
	}
	int no_l = 1, no_r = 1;
	while ((opt = getopt(argc, argv, "l:r:d:t:hcs")) != -1)
	{
		//string opt_key;
		//opt_key+=opt;
		switch (opt)
		{
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

	struct sockaddr_in local_me, local_other;
	int local_listen_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

			if ((timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
			{
				printf("timer_fd create error");
				exit(1);
			}
			ev.events = EPOLLIN;
			ev.data.fd = timer_fd;

			itimerspec zero_its;
			memset(&zero_its, 0, sizeof(zero_its));

			timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &zero_its, 0);
			epoll_ctl(epollfd, EPOLL_CTL_ADD, timer_fd, &ev);
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
					else if (events[n].data.fd == timer_fd)
					{
						uint64_t value;
						read(timer_fd, &value, 8);
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

	return 0;
}
