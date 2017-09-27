/*
 * delay_manager.h
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */

#ifndef DELAY_MANAGER_H_
#define DELAY_MANAGER_H_

#include "common.h"
#include "packet.h"

//enum delay_type_t {none=0,enum_sendto_u64,enum_send_fd,client_to_local,client_to_remote,server_to_local,server_to_remote};

/*
struct fd_ip_port_t
{
	int fd;
	u32_t ip;
	u32_t port;
};
union dest_t
{
	fd_ip_port_t fd_ip_port;
	int fd;
	u64_t u64;
};
*/
struct delay_data_t
{
	dest_t dest;
	//int left_time;//
	char * data;
	int len;
	int handle();
};

struct delay_manager_t
{
	int timer_fd;
	int capacity;
	multimap<my_time_t,delay_data_t> delay_mp;  //unit us,1 us=0.001ms
	delay_manager_t();
	~delay_manager_t();
	int get_timer_fd();
	int check();
	int add(my_time_t delay,const dest_t &dest,char *data,int len);
};

#endif /* DELAY_MANAGER_H_ */
