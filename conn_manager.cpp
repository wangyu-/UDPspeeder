/*
 * conn_manager.cpp
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */

#include "conn_manager.h"

int disable_conv_clear=0;

conn_manager_t_not_used::conn_manager_t_not_used() {
	clear_it = fd_last_active_time.begin();
	long long last_clear_time = 0;
	rehash();
	//clear_function=0;
}
conn_manager_t_not_used::~conn_manager_t_not_used() {
	clear();
}
int conn_manager_t_not_used::get_size() {
	return fd_to_u64.size();
}
void conn_manager_t_not_used::rehash() {
	u64_to_fd.rehash(10007);
	fd_to_u64.rehash(10007);
	fd_last_active_time.rehash(10007);
}
void conn_manager_t_not_used::clear() {
	if (disable_conv_clear)
		return;

	for (it = fd_to_u64.begin(); it != fd_to_u64.end(); it++) {
		//int fd=int((it->second<<32u)>>32u);
		close(it->first);
	}
	u64_to_fd.clear();
	fd_to_u64.clear();
	fd_last_active_time.clear();

	clear_it = fd_last_active_time.begin();

}
int conn_manager_t_not_used::exist_fd(u32_t fd) {
	return fd_to_u64.find(fd) != fd_to_u64.end();
}
int conn_manager_t_not_used::exist_u64(u64_t u64) {
	return u64_to_fd.find(u64) != u64_to_fd.end();
}
u32_t conn_manager_t_not_used::find_fd_by_u64(u64_t u64) {
	return u64_to_fd[u64];
}
u64_t conn_manager_t_not_used::find_u64_by_fd(u32_t fd) {
	return fd_to_u64[fd];
}
int conn_manager_t_not_used::update_active_time(u32_t fd) {
	return fd_last_active_time[fd] = get_current_time();
}
int conn_manager_t_not_used::insert_fd(u32_t fd, u64_t u64) {
	u64_to_fd[u64] = fd;
	fd_to_u64[fd] = u64;
	fd_last_active_time[fd] = get_current_time();
	return 0;
}
int conn_manager_t_not_used::erase_fd(u32_t fd) {
	if (disable_conv_clear)
		return 0;
	u64_t u64 = fd_to_u64[fd];

	u32_t ip = (u64 >> 32u);

	int port = uint16_t((u64 << 32u) >> 32u);

	mylog(log_info, "fd %d cleared,assocated adress %s,%d\n", fd, my_ntoa(ip),
			port);

	close(fd);

	fd_to_u64.erase(fd);
	u64_to_fd.erase(u64);
	fd_last_active_time.erase(fd);
	return 0;
}
/*
void conn_manager_t::check_clear_list() {
	while (!clear_list.empty()) {
		int fd = *clear_list.begin();
		clear_list.pop_front();
		erase_fd(fd);
	}
}*/
int conn_manager_t_not_used::clear_inactive() {
	if (get_current_time() - last_clear_time > conv_clear_interval) {
		last_clear_time = get_current_time();
		return clear_inactive0();
	}
	return 0;
}
int conn_manager_t_not_used::clear_inactive0() {
	if (disable_conv_clear)
		return 0;

	//map<uint32_t,uint64_t>::iterator it;
	int cnt = 0;
	it = clear_it;
	int size = fd_last_active_time.size();
	int num_to_clean = size / conv_clear_ratio + conv_clear_min; //clear 1/10 each time,to avoid latency glitch

	u64_t current_time = get_current_time();
	for (;;) {
		if (cnt >= num_to_clean)
			break;
		if (fd_last_active_time.begin() == fd_last_active_time.end())
			break;

		if (it == fd_last_active_time.end()) {
			it = fd_last_active_time.begin();
		}

		if (current_time - it->second > conv_timeout) {
			//mylog(log_info,"inactive conv %u cleared \n",it->first);
			old_it = it;
			it++;
			u32_t fd = old_it->first;
			erase_fd(old_it->first);

		} else {
			it++;
		}
		cnt++;
	}
	return 0;
}


