/*
 * rs.h
 *
 *  Created on: Sep 14, 2017
 *      Author: root
 */

#ifndef LIB_RS_H_
#define LIB_RS_H_

#include "fec.h"


int rs_encode(void *code,void *data[],int size);

int rs_recover_data(void *code,void *data[],int size);

int rs_recover_all(void *code,void *data[],int size);



#endif /* LIB_RS_H_ */
