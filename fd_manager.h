/*
 * fd_manager.h
 *
 *  Created on: Sep 25, 2017
 *      Author: root
 */

#ifndef FD_MANAGER_H_
#define FD_MANAGER_H_

#include "common.h"

typedef u64_t fd64_t;

struct fd_manager_t   //conver fd to a uniq 64bit number,avoid fd value conflict caused by close and re-create
//not used currently
{
	u64_t counter;
	unordered_map<int,u64_t> fd_to_fd64_mp;
	unordered_map<u64_t,int> fd64_to_fd_mp;
	int fd_exist(int fd);
	int fd64_exist(fd64_t fd64);
	fd64_t fd_to_fd64(int fd);
	int fd64_to_fd(fd64_t);
	void remove_fd(int fd);
	void remove_fd64(fd64_t fd64);
	void reserve();
	u64_t insert_fd(int fd);
	fd_manager_t();
};

extern fd_manager_t fd_manager;
#endif /* FD_MANAGER_H_ */
