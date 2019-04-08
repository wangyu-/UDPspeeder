/*
 * common.h
 *
 *  Created on: Jul 29, 2017
 *      Author: wangyu
 */

#ifndef COMMON_H_
#define COMMON_H_
//#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<getopt.h>

#include<unistd.h>
#include<errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h> //for exit(0);
#include <errno.h> //For errno - the error number
//#include <netinet/tcp.h>   //Provides declarations for tcp header
//#include <netinet/udp.h>
//#include <netinet/ip.h>    //Provides declarations for ip header
//#include <netinet/if_ether.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
//#include <netinet/in.h>
//#include <net/if.h>
#include <stdarg.h>
#include <assert.h>
#include <my_ev.h>

#if defined(__MINGW32__)
#include <winsock2.h>
#include <Ws2tcpip.h >
typedef int socklen_t;
#else
#include <sys/socket.h> 
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif


#include<unordered_map>
#include<unordered_set>
#include<map>
#include<list>
#include<string>
#include<vector>
using  namespace std;


typedef unsigned long long u64_t;   //this works on most platform,avoid using the PRId64
typedef long long i64_t;

typedef unsigned int u32_t;
typedef int i32_t;

typedef unsigned short u16_t;
typedef short i16_t;


#if defined(__MINGW32__)
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
#define setsockopt(a,b,c,d,e) setsockopt(a,b,c,(const char *)(d),e)
#endif

char *get_sock_error();
int get_sock_errno();
int init_ws();

#if defined(__MINGW32__)
typedef SOCKET my_fd_t;
inline int sock_close(my_fd_t fd)
{
	return closesocket(fd);
}
#else
typedef int my_fd_t;
inline int sock_close(my_fd_t fd)
{
	return close(fd);
}

#endif


struct my_itimerspec {
	struct timespec it_interval;  /* Timer interval */
	struct timespec it_value;     /* Initial expiration */
};

typedef u64_t my_time_t;

const int max_addr_len=100;
const int max_data_len=3600;
const int buf_len=max_data_len+200;

const int default_mtu=1250;

//const u32_t timer_interval=400;
////const u32_t conv_timeout=180000;
//const u32_t conv_timeout=40000;//for test
const u32_t conv_timeout=180000;
const int max_conv_num=10000;
const int max_conn_num=200;

/*
const u32_t max_handshake_conn_num=10000;
const u32_t max_ready_conn_num=1000;
//const u32_t anti_replay_window_size=1000;


const u32_t client_handshake_timeout=5000;
const u32_t client_retry_interval=1000;

const u32_t server_handshake_timeout=10000;// this should be much longer than clients. client retry initially ,server retry passtively*/

const int conv_clear_ratio=30;  //conv grabage collecter check 1/30 of all conv one time
const int conn_clear_ratio=50;
const int conv_clear_min=1;
const int conn_clear_min=1;

const u32_t conv_clear_interval=1000;
const u32_t conn_clear_interval=1000;


const i32_t max_fail_time=0;//disable

const u32_t heartbeat_interval=1000;

const u32_t timer_interval=400;//this should be smaller than heartbeat_interval and retry interval;

//const uint32_t conv_timeout=120000; //120 second
//const u32_t conv_timeout=120000; //for test

const u32_t client_conn_timeout=10000;
const u32_t client_conn_uplink_timeout=client_conn_timeout+2000;

//const uint32_t server_conn_timeout=conv_timeout+60000;//this should be 60s+ longer than conv_timeout,so that conv_manager can destruct convs gradually,to avoid latency glicth
const u32_t server_conn_timeout=conv_timeout+20000;//for test


extern int about_to_exit;

enum raw_mode_t{mode_faketcp=0,mode_udp,mode_icmp,mode_end};
extern raw_mode_t raw_mode;
enum program_mode_t {unset_mode=0,client_mode,server_mode};
extern program_mode_t program_mode;
extern unordered_map<int, const char*> raw_mode_tostring ;

enum working_mode_t {unset_working_mode=0,tunnel_mode,tun_dev_mode};
extern working_mode_t working_mode;

extern int socket_buf_size;


//typedef u32_t id_t;

typedef u64_t iv_t;

typedef u64_t padding_t;

typedef u64_t anti_replay_seq_t;

typedef u64_t fd64_t;

//enum dest_type{none=0,type_fd64_ip_port,type_fd64,type_fd64_ip_port_conv,type_fd64_conv/*,type_fd*/};
enum dest_type{none=0,type_fd64_addr,type_fd64,type_fd,type_write_fd,type_fd_addr/*,type_fd*/};

/*
struct ip_port_t
{
	u32_t ip;
	int port;
	void from_u64(u64_t u64);
	u64_t to_u64();
	char * to_s();
};

struct fd64_ip_port_t
{
	fd64_t fd64;
	ip_port_t ip_port;
};
struct fd_ip_port_t
{
	int fd;
	ip_port_t ip_port;
};*/


struct pseudo_header {
    u32_t source_address;
    u32_t dest_address;
    unsigned char placeholder;
    unsigned char protocol;
    unsigned short tcp_length;
};

u32_t djb2(unsigned char *str,int len);
u32_t sdbm(unsigned char *str,int len);

struct address_t  //TODO scope id
{
	struct hash_function
	{
	    u32_t operator()(const address_t &key) const
		{
	    	return sdbm((unsigned char*)&key.inner,sizeof(key.inner));
		}
	};

	union storage_t //sockaddr_storage is too huge, we dont use it.
	{
		sockaddr_in ipv4;
		sockaddr_in6 ipv6;
	};
	storage_t inner;

	/*address_t()
	{
		clear();
	}*/
	void clear()
	{
		memset(&inner,0,sizeof(inner));
	}
	/*
	int from_ip_port(u32_t  ip, int port)
	{
		clear();
		inner.ipv4.sin_family=AF_INET;
		inner.ipv4.sin_port=htons(port);
		inner.ipv4.sin_addr.s_addr=ip;
		return 0;
	}*/

	int from_ip_port_new(int type, void *  ip, int port)
	{
		clear();
		if(type==AF_INET)
		{
			inner.ipv4.sin_family=AF_INET;
			inner.ipv4.sin_port=htons(port);
			inner.ipv4.sin_addr.s_addr=*((u32_t *)ip);
		}
		else if(type==AF_INET6)
		{
			inner.ipv6.sin6_family=AF_INET6;
			inner.ipv6.sin6_port=htons(port);
			inner.ipv6.sin6_addr=*((in6_addr*)ip);
		}
		return 0;
	}

	int from_str(char * str);

	int from_str_ip_only(char * str);

	int from_sockaddr(sockaddr *,socklen_t);

	char* get_str();
	void to_str(char *);

	inline int is_vaild()
	{
		u32_t ret=((sockaddr*)&inner)->sa_family;
		return (ret==AF_INET||ret==AF_INET6);
	}

	inline u32_t get_type()
	{
		assert(is_vaild());
		u32_t ret=((sockaddr*)&inner)->sa_family;
		return ret;
	}

	inline u32_t get_len()
	{
		u32_t type=get_type();
		switch(type)
		{
			case AF_INET:
				return sizeof(sockaddr_in);
			case AF_INET6:
				return sizeof(sockaddr_in6);
			default:
				assert(0==1);
		}
		return -1;
	}

	inline u32_t get_port()
	{
		u32_t type=get_type();
		switch(type)
		{
			case AF_INET:
				return ntohs(inner.ipv4.sin_port);
			case AF_INET6:
				return ntohs(inner.ipv6.sin6_port);
			default:
				assert(0==1);
		}
		return -1;
	}

	inline void set_port(int port)
	{
		u32_t type=get_type();
		switch(type)
		{
			case AF_INET:
				inner.ipv4.sin_port=htons(port);
				break;
			case AF_INET6:
				inner.ipv6.sin6_port=htons(port);
				break;
			default:
				assert(0==1);
		}
		return ;
	}

    bool operator == (const address_t &b) const
    {
    	//return this->data==b.data;
        return memcmp(&this->inner,&b.inner,sizeof(this->inner))==0;
    }

    int new_connected_udp_fd();

    char* get_ip();
};

namespace std {
template <>
 struct hash<address_t>
 {
   std::size_t operator()(const address_t& key) const
   {

	 //return address_t::hash_function(k);
	   return sdbm((unsigned char*)&key.inner,sizeof(key.inner));
   }
 };
}

struct fd64_addr_t
{
	fd64_t fd64;
	address_t addr;
};
struct fd_addr_t
{
	int fd;
	address_t addr;
};
union inner_t
{
	fd64_t fd64;
	int fd;
	fd64_addr_t fd64_addr;
	fd_addr_t fd_addr;
};
struct dest_t
{
	dest_type type;
	inner_t inner;
	u32_t conv;
	int cook=0;
};

struct fd_info_t
{
	address_t addr;
	ev_io io_watcher;
};

u64_t get_current_time();
//u64_t get_current_time_rough();
u64_t get_current_time_us();
u64_t pack_u64(u32_t a,u32_t b);

u32_t get_u64_h(u64_t a);

u32_t get_u64_l(u64_t a);

void write_u16(char *,u16_t a);
u16_t read_u16(char *);

void write_u32(char *,u32_t a);
u32_t read_u32(char *);

void write_u64(char *,u64_t a);
u64_t read_uu64(char *);

char * my_ntoa(u32_t ip);

void myexit(int a);
void init_random_number_fd();
u64_t get_fake_random_number_64();
u32_t get_fake_random_number();
u32_t get_fake_random_number_nz();
u64_t ntoh64(u64_t a);
u64_t hton64(u64_t a);
bool larger_than_u16(uint16_t a,uint16_t b);
bool larger_than_u32(u32_t a,u32_t b);
void setnonblocking(int sock);
int set_buf_size(int fd,int socket_buf_size);

unsigned short csum(const unsigned short *ptr,int nbytes);
unsigned short tcp_csum(const pseudo_header & ph,const unsigned short *ptr,int nbytes);

void  signal_handler(int sig);
//int numbers_to_char(id_t id1,id_t id2,id_t id3,char * &data,int &len);
//int char_to_numbers(const char * data,int len,id_t &id1,id_t &id2,id_t &id3);

void myexit(int a);

int add_iptables_rule(char *);

int clear_iptables_rule();
void get_fake_random_chars(char * s,int len);
int random_between(u32_t a,u32_t b);

int set_timer_ms(int epollfd,int &timer_fd,u32_t timer_interval);

int round_up_div(int a,int b);

int create_fifo(char * file);
/*
int create_new_udp(int &new_udp_fd,int remote_address_uint32,int remote_port);
*/

int new_listen_socket(int &fd,u32_t ip,int port);

int new_connected_socket(int &fd,u32_t ip,int port);

int new_listen_socket2(int &fd,address_t &addr);
int new_connected_socket2(int &fd,address_t &addr);

struct not_copy_able_t
{
	not_copy_able_t()
	{

	}
	not_copy_able_t(const not_copy_able_t &other)
	{
		assert(0==1);
	}
	const not_copy_able_t & operator=(const not_copy_able_t &other)
	{
		assert(0==1);
		return other;
	}
};


template <class key_t>
struct lru_collector_t:not_copy_able_t
{
	//typedef void* key_t;
//#define key_t void*
	struct lru_pair_t
	{
		key_t key;
		my_time_t ts;
	};

	unordered_map<key_t,typename list<lru_pair_t>::iterator> mp;

	list<lru_pair_t> q;
	int update(key_t key)
	{
		assert(mp.find(key)!=mp.end());
		auto it=mp[key];
		q.erase(it);

		my_time_t value=get_current_time();
		if(!q.empty())
		{
			assert(value >=q.front().ts);
		}
		lru_pair_t tmp; tmp.key=key; tmp.ts=value;
		q.push_front( tmp);
		mp[key]=q.begin();

		return 0;
	}
	int new_key(key_t key)
	{
		assert(mp.find(key)==mp.end());

		my_time_t value=get_current_time();
		if(!q.empty())
		{
			assert(value >=q.front().ts);
		}
		lru_pair_t tmp; tmp.key=key; tmp.ts=value;
		q.push_front( tmp);
		mp[key]=q.begin();

		return 0;
	}
	int size()
	{
		return q.size();
	}
	int empty()
	{
		return q.empty();
	}
	void clear()
	{
		mp.clear(); q.clear();
	}
	my_time_t ts_of(key_t key)
	{
		assert(mp.find(key)!=mp.end());
		return mp[key]->ts;
	}

	my_time_t peek_back(key_t &key)
	{
		assert(!q.empty());
		auto it=q.end(); it--;
		key=it->key;
		return it->ts;
	}
	void erase(key_t key)
	{
		assert(mp.find(key)!=mp.end());
		q.erase(mp[key]);
		mp.erase(key);
	}
	/*
	void erase_back()
	{
		assert(!q.empty());
		auto it=q.end(); it--;
		key_t key=it->key;
		erase(key);
	}*/
};


vector<string> string_to_vec(const char * s,const char * sp) ;

#endif /* COMMON_H_ */
