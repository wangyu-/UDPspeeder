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
#include "fec_manager.h"

extern int report_interval;

const int disable_conv_clear = 0;

void server_clear_function(u64_t u64);

template <class T>
struct conv_manager_t  // manage the udp connections
{
    // typedef hash_map map;
    unordered_map<T, u32_t> data_to_conv;  // conv and u64 are both supposed to be uniq
    unordered_map<u32_t, T> conv_to_data;

    lru_collector_t<u32_t> lru;
    // unordered_map<u32_t,u64_t> conv_last_active_time;

    // unordered_map<u32_t,u64_t>::iterator clear_it;

    void (*additional_clear_function)(T data) = 0;

    long long last_clear_time;

    conv_manager_t() {
        // clear_it=conv_last_active_time.begin();
        long long last_clear_time = 0;
        additional_clear_function = 0;
    }
    ~conv_manager_t() {
        clear();
    }
    int get_size() {
        return conv_to_data.size();
    }
    void reserve() {
        data_to_conv.reserve(10007);
        conv_to_data.reserve(10007);
        // conv_last_active_time.reserve(10007);

        lru.mp.reserve(10007);
    }
    void clear() {
        if (disable_conv_clear) return;

        if (additional_clear_function != 0) {
            for (auto it = conv_to_data.begin(); it != conv_to_data.end(); it++) {
                // int fd=int((it->second<<32u)>>32u);
                additional_clear_function(it->second);
            }
        }
        data_to_conv.clear();
        conv_to_data.clear();

        lru.clear();
        // conv_last_active_time.clear();

        // clear_it=conv_last_active_time.begin();
    }
    u32_t get_new_conv() {
        u32_t conv = get_fake_random_number_nz();
        while (conv_to_data.find(conv) != conv_to_data.end()) {
            conv = get_fake_random_number_nz();
        }
        return conv;
    }
    int is_conv_used(u32_t conv) {
        return conv_to_data.find(conv) != conv_to_data.end();
    }
    int is_data_used(T data) {
        return data_to_conv.find(data) != data_to_conv.end();
    }
    u32_t find_conv_by_data(T data) {
        return data_to_conv[data];
    }
    T find_data_by_conv(u32_t conv) {
        return conv_to_data[conv];
    }
    int update_active_time(u32_t conv) {
        // return conv_last_active_time[conv]=get_current_time();
        lru.update(conv);
        return 0;
    }
    int insert_conv(u32_t conv, T data) {
        data_to_conv[data] = conv;
        conv_to_data[conv] = data;
        // conv_last_active_time[conv]=get_current_time();
        lru.new_key(conv);
        return 0;
    }
    int erase_conv(u32_t conv) {
        if (disable_conv_clear) return 0;
        T data = conv_to_data[conv];
        if (additional_clear_function != 0) {
            additional_clear_function(data);
        }
        conv_to_data.erase(conv);
        data_to_conv.erase(data);
        // conv_last_active_time.erase(conv);
        lru.erase(conv);
        return 0;
    }
    int clear_inactive(char *info = 0) {
        if (get_current_time() - last_clear_time > conv_clear_interval) {
            last_clear_time = get_current_time();
            return clear_inactive0(info);
        }
        return 0;
    }
    int clear_inactive0(char *info) {
        if (disable_conv_clear) return 0;

        unordered_map<u32_t, u64_t>::iterator it;
        unordered_map<u32_t, u64_t>::iterator old_it;

        // map<uint32_t,uint64_t>::iterator it;
        int cnt = 0;
        // it=clear_it;
        int size = lru.size();
        int num_to_clean = size / conv_clear_ratio + conv_clear_min;  // clear 1/10 each time,to avoid latency glitch

        num_to_clean = min(num_to_clean, size);

        my_time_t current_time = get_current_time();
        for (;;) {
            if (cnt >= num_to_clean) break;
            if (lru.empty()) break;

            u32_t conv;
            my_time_t ts = lru.peek_back(conv);

            if (current_time - ts < conv_timeout) break;

            erase_conv(conv);
            if (info == 0) {
                mylog(log_info, "conv %x cleared\n", conv);
            } else {
                mylog(log_info, "[%s]conv %x cleared\n", info, conv);
            }
            cnt++;
        }
        return 0;
    }

    /*
conv_manager_t();
~conv_manager_t();
int get_size();
void reserve();
void clear();
u32_t get_new_conv();
int is_conv_used(u32_t conv);
int is_u64_used(T u64);
u32_t find_conv_by_u64(T u64);
T find_u64_by_conv(u32_t conv);
int update_active_time(u32_t conv);
int insert_conv(u32_t conv,T u64);
int erase_conv(u32_t conv);
int clear_inactive(char * ip_port=0);
int clear_inactive0(char * ip_port);*/
};  // g_conv_manager;

struct inner_stat_t {
    u64_t input_packet_num;
    u64_t input_packet_size;
    u64_t output_packet_num;
    u64_t output_packet_size;
};
struct stat_t {
    u64_t last_report_time;
    inner_stat_t normal_to_fec;
    inner_stat_t fec_to_normal;
    stat_t() {
        clear();
    }
    void clear(){
        memset(this, 0, sizeof(stat_t));
    }
    void report_as_client() {
        if (report_interval != 0 && get_current_time() - last_report_time > u64_t(report_interval) * 1000) {
            last_report_time = get_current_time();
            inner_stat_t &a = normal_to_fec;
            inner_stat_t &b = fec_to_normal;
            mylog(log_info, "[report]client-->server:(original:%llu pkt;%llu byte) (fec:%llu pkt,%llu byte)  server-->client:(original:%llu pkt;%llu byte) (fec:%llu pkt;%llu byte)\n",
                  a.input_packet_num, a.input_packet_size, a.output_packet_num, a.output_packet_size,
                  b.output_packet_num, b.output_packet_size, b.input_packet_num, b.input_packet_size);
        }
    }
    void report_as_server(address_t &addr) {
        if (report_interval != 0 && get_current_time() - last_report_time > u64_t(report_interval) * 1000) {
            last_report_time = get_current_time();
            inner_stat_t &a = fec_to_normal;
            inner_stat_t &b = normal_to_fec;
            mylog(log_info, "[report][%s]client-->server:(original:%llu pkt;%llu byte) (fec:%llu pkt;%llu byte)  server-->client:(original:%llu pkt;%llu byte) (fec:%llu pkt;%llu byte)\n",
                  addr.get_str(),
                  a.output_packet_num, a.output_packet_size, a.input_packet_num, a.input_packet_size,
                  b.input_packet_num, b.input_packet_size, b.output_packet_num, b.output_packet_size);
        }
    }
};

struct conn_info_t : not_copy_able_t  // stores info for a raw connection.for client ,there is only one connection,for server there can be thousand of connection since server can
// handle multiple clients
{
    union tmp_union_t {
        conv_manager_t<address_t> c;
        conv_manager_t<u64_t> s;
        // avoid templates here and there, avoid pointer and type cast
        tmp_union_t() {
            if (program_mode == client_mode) {
                new (&c) conv_manager_t<address_t>();
            } else {
                assert(program_mode == server_mode);
                new (&s) conv_manager_t<u64_t>();
            }
        }
        ~tmp_union_t() {
            if (program_mode == client_mode) {
                c.~conv_manager_t<address_t>();
            } else {
                assert(program_mode == server_mode);
                s.~conv_manager_t<u64_t>();
            }
        }
    } conv_manager;

    fec_encode_manager_t fec_encode_manager;
    fec_decode_manager_t fec_decode_manager;
    ev_timer timer;
    // my_timer_t timer;

    u64_t last_active_time;
    stat_t stat;

    struct ev_loop *loop = 0;
    int local_listen_fd;

    int remote_fd;       // only used for client
    fd64_t remote_fd64;  // only used for client

    // ip_port_t ip_port;
    address_t addr;  // only used for server

    conn_info_t() {
        if (program_mode == server_mode) {
            conv_manager.s.additional_clear_function = server_clear_function;
        } else {
            assert(program_mode == client_mode);
        }
    }

    ~conn_info_t() {
        if (loop)
            ev_timer_stop(loop, &timer);
    }
    void update_active_time() {
        last_active_time = get_current_time();
    }
    /*
    conn_info_t(const conn_info_t &b)
    {
            assert(0==1);
    }*/
};
/*
struct conn_manager_t  //manager for connections. for client,we dont need conn_manager since there is only one connection.for server we use one conn_manager for all connections
{

        unordered_map<u64_t,conn_info_t*> mp;//<ip,port> to conn_info_t;
        unordered_map<u64_t,conn_info_t*>::iterator clear_it;
        long long last_clear_time;

        conn_manager_t();
        conn_manager_t(const conn_info_t &b)
        {
                assert(0==1);
        }
        int exist(ip_port_t);
        conn_info_t *& find_p(ip_port_t);  //be aware,the adress may change after rehash
        conn_info_t & find(ip_port_t) ; //be aware,the adress may change after rehash
        int insert(ip_port_t);

        int erase(unordered_map<u64_t,conn_info_t*>::iterator erase_it);
        int clear_inactive();
        int clear_inactive0();

};*/

struct conn_manager_t  // manager for connections. for client,we dont need conn_manager since there is only one connection.for server we use one conn_manager for all connections
{
    unordered_map<address_t, conn_info_t *> mp;  // put it at end so that it de-consturcts first
    unordered_map<address_t, conn_info_t *>::iterator clear_it;

    long long last_clear_time;

    conn_manager_t();
    int exist(address_t addr);
    conn_info_t *&find_insert_p(address_t addr);  // be aware,the adress may change after rehash //not true?
    conn_info_t &find_insert(address_t addr);     // be aware,the adress may change after rehash

    int erase(unordered_map<address_t, conn_info_t *>::iterator erase_it);
    int clear_inactive();
    int clear_inactive0();
};

extern conn_manager_t conn_manager;

#endif /* CONNECTION_H_ */
