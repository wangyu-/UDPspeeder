/*
 * rs.c
 *
 *  Created on: Sep 14, 2017
 *      Author: root
 */
#include "rs.h"

void rs_encode(void *code,void *data[],int size)
{
	int k=get_k(code);
	int n=get_n(code);
	for(int i=k;i<n;i++)
	{
		fec_encode(code, data, data[i],i, size);
	}

	return ;
}

int rs_decode(void *code,void *data[],int size)
{
	int k=get_k(code);
	int n=get_n(code);
	int index[n];
	int count=0;
	for(int i=0;i<n;i++)
	{
		if(data[i]!=0)
		{
			index[count++]=i;
		}
	}
	if(count<k)
		return -1;
	for(int i=0;i<n;i++)
	{
		if(k<count)
			data[i]=data[index[i]];
		else
			data[i]=0;
	}
	return fec_decode(code,data,index,size);
}
