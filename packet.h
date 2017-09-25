/*
 * packet.h
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */

#ifndef PACKET_H_
#define PACKET_H_

#include "common.h"
#include "fd_manager.h"

extern int iv_min;
extern int iv_max;//< 256;

extern u64_t packet_send_count;
extern u64_t dup_packet_send_count;
extern u64_t packet_recv_count;
extern u64_t dup_packet_recv_count;
extern char key_string[1000];
extern int disable_replay_filter;
extern int random_drop;
extern int local_listen_fd;


enum dest_type{none=0,type_ip_port,type_fd64,type_fd};


struct ip_port_t
{
	u32_t ip;
	int port;
};
union inner_t
{
	ip_port_t ip_port;
	int fd;
	fd64_t fd64;
};
struct dest_t
{
	dest_type type;
	inner_t inner;
};

int my_send(dest_t &dest,char *data,int len);

void encrypt_0(char * input,int &len,char *key);
void decrypt_0(char * input,int &len,char *key);
int add_seq(char * data,int &data_len );
int remove_seq(char * data,int &data_len);
int do_obscure(const char * input, int in_len,char *output,int &out_len);
int de_obscure(const char * input, int in_len,char *output,int &out_len);

//int sendto_fd_u64 (int fd,u64_t u64,char * buf, int len,int flags);
int sendto_ip_port (u32_t ip,int port,char * buf, int len,int flags);
int send_fd (int fd,char * buf, int len,int flags);

#endif /* PACKET_H_ */
