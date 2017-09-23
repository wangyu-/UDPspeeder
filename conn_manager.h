/*
 * conn_manager.h
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */

#ifndef CONN_MANAGER_H_
#define CONN_MANAGER_H_

#include "common.h"
#include "log.h"

extern int disable_conv_clear;

struct conn_manager_t_not_used  //TODO change map to unordered map
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
	//list<int> clear_list;
	conn_manager_t_not_used();
	~conn_manager_t_not_used();
	int get_size();
	void rehash();
	void clear();
	int exist_fd(u32_t fd);
	int exist_u64(u64_t u64);
	u32_t find_fd_by_u64(u64_t u64);
	u64_t find_u64_by_fd(u32_t fd);
	int update_active_time(u32_t fd);
	int insert_fd(u32_t fd,u64_t u64);
	int erase_fd(u32_t fd);
	//void check_clear_list();
	int clear_inactive();
	int clear_inactive0();

};


#endif /* CONN_MANAGER_H_ */
