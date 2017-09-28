/*
 * fec_manager.h
 *
 *  Created on: Sep 27, 2017
 *      Author: root
 */

#ifndef FEC_MANAGER_H_
#define FEC_MANAGER_H_



struct fec_encode_manager_t
{
	int input(char *,int l);
	int ready_for_output();
	int output(char ** &,int l,int &n);
};

struct fec_decode_manager_t
{
	int input(char *,int l);
	int ready_for_output();
	int output(char ** &,int l,int &n);
};


struct blob_encode_t
{
	int input(char *,int l);
	int ready_for_output();
	int output(char ** &,int l,int &n);
};

struct blob_decode_t
{
	int input(char *,int l);
	int ready_for_output();
	int output(char ** &,int l,int &n);
};
#endif /* FEC_MANAGER_H_ */
