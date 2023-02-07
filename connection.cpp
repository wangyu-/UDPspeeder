/*
 * connection.cpp
 *
 *  Created on: Sep 23, 2017
 *      Author: root
 */

#include "connection.h"

// const int disable_conv_clear=0;//a udp connection in the multiplexer is called conversation in this program,conv for short.

const int disable_conn_clear = 0;  // a raw connection is called conn.

int report_interval = 0;

void server_clear_function(u64_t u64)  // used in conv_manager in server mode.for server we have to use one udp fd for one conv(udp connection),
// so we have to close the fd when conv expires
{
    fd64_t fd64 = u64;
    assert(fd_manager.exist(fd64));
    ev_io &watcher = fd_manager.get_info(fd64).io_watcher;

    address_t &addr = fd_manager.get_info(fd64).addr;            //
    assert(conn_manager.exist(addr));                            //
    struct ev_loop *loop = conn_manager.find_insert(addr).loop;  // overkill ? should we just use ev_default_loop(0)?

    ev_io_stop(loop, &watcher);

    fd_manager.fd64_close(fd64);
}

////////////////////////////////////////////////////////////////////

conn_manager_t::conn_manager_t() {
    mp.reserve(10007);
    last_clear_time = 0;
}
int conn_manager_t::exist(address_t addr) {
    if (mp.find(addr) != mp.end()) {
        return 1;
    }
    return 0;
}

conn_info_t *&conn_manager_t::find_insert_p(address_t addr)  // be aware,the adress may change after rehash
{
    // u64_t u64=0;
    // u64=ip;
    // u64<<=32u;
    // u64|=port;
    unordered_map<address_t, conn_info_t *>::iterator it = mp.find(addr);
    if (it == mp.end()) {
        mp[addr] = new conn_info_t;
        // lru.new_key(addr);
    } else {
        // lru.update(addr);
    }
    return mp[addr];
}
conn_info_t &conn_manager_t::find_insert(address_t addr)  // be aware,the adress may change after rehash
{
    // u64_t u64=0;
    // u64=ip;
    // u64<<=32u;
    // u64|=port;
    unordered_map<address_t, conn_info_t *>::iterator it = mp.find(addr);
    if (it == mp.end()) {
        mp[addr] = new conn_info_t;
        // lru.new_key(addr);
    } else {
        // lru.update(addr);
    }
    return *mp[addr];
}
int conn_manager_t::erase(unordered_map<address_t, conn_info_t *>::iterator erase_it) {
    delete (erase_it->second);
    mp.erase(erase_it->first);
    return 0;
}
int conn_manager_t::clear_inactive() {
    if (get_current_time() - last_clear_time > conn_clear_interval) {
        last_clear_time = get_current_time();
        return clear_inactive0();
    }
    return 0;
}

int conn_manager_t::clear_inactive0() {
    // mylog(log_info,"called\n");
    unordered_map<address_t, conn_info_t *>::iterator it;
    unordered_map<address_t, conn_info_t *>::iterator old_it;

    if (disable_conn_clear) return 0;

    // map<uint32_t,uint64_t>::iterator it;
    int cnt = 0;
    it = clear_it;  // TODO,write it back
    int size = mp.size();
    int num_to_clean = size / conn_clear_ratio + conn_clear_min;  // clear 1/10 each time,to avoid latency glitch

    // mylog(log_trace,"mp.size() %d\n", size);

    num_to_clean = min(num_to_clean, (int)mp.size());
    u64_t current_time = get_current_time();

    // mylog(log_info,"here size=%d\n",(int)mp.size());
    for (;;) {
        if (cnt >= num_to_clean) break;
        if (mp.begin() == mp.end()) break;
        if (it == mp.end()) {
            it = mp.begin();
        }

        if (it->second->conv_manager.s.get_size() > 0) {
            // mylog(log_info,"[%s:%d]size %d \n",my_ntoa(get_u64_h(it->first)),get_u64_l(it->first),(int)it->second->conv_manager.get_size());
            it++;
        } else if (current_time < it->second->last_active_time + server_conn_timeout) {
            it++;
        } else {
            address_t tmp_addr = it->first;  // avoid making get_str() const;
            mylog(log_info, "{%s} inactive conn cleared \n", tmp_addr.get_str());
            old_it = it;
            it++;
            erase(old_it);
        }
        cnt++;
    }
    clear_it = it;
    return 0;
}
