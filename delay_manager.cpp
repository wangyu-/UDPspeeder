/*
 * delay_manager.cpp
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */
#include "delay_manager.h"
#include "log.h"
#include "packet.h"

int delay_data_t::handle()
{
	return my_send(dest,data,len)>=0;
}


delay_manager_t::delay_manager_t()
{
	capacity=0;

	//if ((timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0)
	//{
	//	mylog(log_fatal,"timer_fd create error");
	//	myexit(1);
	//}

	//itimerspec zero_its;
	//memset(&zero_its, 0, sizeof(zero_its));

	//timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &zero_its, 0);

}
delay_manager_t::~delay_manager_t()
{
	//TODO ,we currently dont need to deconstruct it
}

/*
int delay_manager_t::get_timer_fd()
{
	return timer_fd;
}*/

//int add(my_time_t delay,const dest_t &dest,const char *data,int len);
int delay_manager_t::add(my_time_t delay,const dest_t &dest,char *data,int len)
{
	delay_data_t delay_data;
	delay_data.dest=dest;
	//delay_data.data=data;
	delay_data.len=len;

	if(capacity!=0&&int(delay_mp.size()) >=capacity)
	{
		mylog(log_warn,"max pending packet reached,ignored\n");
		return -1;
	}
	if(delay==0)
	{
		static char buf[buf_len];
		delay_data.data=buf;
		memcpy(buf,data,len);
		int ret=delay_data.handle();
		if (ret != 0) {
			mylog(log_trace, "handle() return %d\n", ret);
		}
		return 0;
	}

	delay_data_t tmp=delay_data;
	tmp.data=(char *)malloc(delay_data.len+100);
    if(!tmp.data)
    {
        mylog(log_warn, "malloc() returned null in delay_manager_t::add()");
        return -1;
    }
	memcpy(tmp.data,data,delay_data.len);

	my_time_t tmp_time=get_current_time_us();
	tmp_time+=delay;

	delay_mp.insert(make_pair(tmp_time,tmp));

	////check();  check everytime when add, is it better ??

	return 0;
}

int delay_manager_t::check()
{
	if(!delay_mp.empty())
	{
		my_time_t current_time;

		multimap<my_time_t,delay_data_t>::iterator it;
		while(1)
		{
			int ret=0;
			it=delay_mp.begin();
			if(it==delay_mp.end()) break;

			current_time=get_current_time_us();
			if(it->first <= current_time)
			{
				ret=it->second.handle();
				if (ret != 0) {
					mylog(log_trace, "handle() return %d\n", ret);
				}
				free(it->second.data);
				delay_mp.erase(it);
			}
			else
			{
				break;
			}

		}
		if(!delay_mp.empty())
		{
			const double m=1000*1000;
			double timer_value=delay_mp.begin()->first/m -get_current_time_us()/m; // be aware of negative value, and be aware of uint
			if(timer_value<0) timer_value=0; // set it to 0 if negative, although libev support negative value
			ev_timer_stop(loop, &timer);
			ev_timer_set(&timer, timer_value,0 );
			ev_timer_start(loop, &timer);
		}
		else
		{
			ev_timer_stop(loop, &timer); //not necessary
		}
	}
	return 0;
}
