/*
 * packet.h
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */

#ifndef PACKET_H_
#define PACKET_H_

#include "common.h"

extern int iv_min;
extern int iv_max;//< 256;

extern u64_t packet_send_count;
extern u64_t dup_packet_send_count;
extern u64_t packet_recv_count;
extern u64_t dup_packet_recv_count;
extern char key_string[1000];
extern int disable_replay_filter;

void encrypt_0(char * input,int &len,char *key);
void decrypt_0(char * input,int &len,char *key);
int add_seq(char * data,int &data_len );
int remove_seq(char * data,int &data_len);
int do_obscure(const char * input, int in_len,char *output,int &out_len);
int de_obscure(const char * input, int in_len,char *output,int &out_len);

#endif /* PACKET_H_ */
