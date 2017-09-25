/*
 * connection.h
 *
 *  Created on: Sep 23, 2017
 *      Author: root
 */

#ifndef CONNECTION_H_
#define CONNECTION_H_

extern int disable_anti_replay;

#include "connection.h"
#include "common.h"
#include "log.h"
#include "delay_manager.h"
#include "fd_manager.h"


/*
struct anti_replay_t  //its for anti replay attack,similar to openvpn/ipsec 's anti replay window
{
	u64_t max_packet_received;
	char window[anti_replay_window_size];
	anti_replay_seq_t anti_replay_seq;
	anti_replay_seq_t get_new_seq_for_send();
	anti_replay_t();
	void re_init();

	int is_vaild(u64_t seq);
};//anti_replay;
*/

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

	conv_manager_t();
	~conv_manager_t();
	int get_size();
	void reserve();
	void clear();
	u32_t get_new_conv();
	int is_conv_used(u32_t conv);
	int is_u64_used(u64_t u64);
	u32_t find_conv_by_u64(u64_t u64);
	u64_t find_u64_by_conv(u32_t conv);
	int update_active_time(u32_t conv);
	int insert_conv(u32_t conv,u64_t u64);
	int erase_conv(u32_t conv);
	int clear_inactive(char * ip_port=0);
	int clear_inactive0(char * ip_port);
};//g_conv_manager;

struct conn_info_t     //stores info for a raw connection.for client ,there is only one connection,for server there can be thousand of connection since server can
//handle multiple clients
{
	conv_manager_t conv_manager;
	//anti_replay_t anti_replay;
	fd64_t timer_fd;
	ip_port_t ip_port;
};//g_conn_info;

struct conn_manager_t  //manager for connections. for client,we dont need conn_manager since there is only one connection.for server we use one conn_manager for all connections
{

 u32_t ready_num;

 unordered_map<fd64_t,u64_t> fd64_mp;
 unordered_map<u64_t,conn_info_t*> mp;//<ip,port> to conn_info_t;
 	 	 	 	 	 	 	 	 	  //put it at end so that it de-consturcts first

 unordered_map<u64_t,conn_info_t*>::iterator clear_it;

 long long last_clear_time;

 conn_manager_t();
 int exist_ip_port(ip_port_t);
 conn_info_t *& find_insert_p(ip_port_t);  //be aware,the adress may change after rehash
 conn_info_t & find_insert(ip_port_t) ; //be aware,the adress may change after rehash
 int exist_fd64(fd64_t fd64);
 void insert_fd64(fd64_t fd64,ip_port_t);
 ip_port_t find_by_fd64(fd64_t fd64);


 int erase(unordered_map<u64_t,conn_info_t*>::iterator erase_it);
int clear_inactive();
int clear_inactive0();

};

extern conn_manager_t conn_manager;


#endif /* CONNECTION_H_ */
