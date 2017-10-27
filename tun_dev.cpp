/*
 * tun.cpp
 *
 *  Created on: Oct 26, 2017
 *      Author: root
 */


#include "common.h"
#include "log.h"
#include "misc.h"


int get_tun_fd(char * dev_name)
{
	int tun_fd=open("/dev/net/tun",O_RDWR);

	if(tun_fd <0)
	{
		mylog(log_fatal,"open /dev/net/tun failed");
		myexit(-1);
	}
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN|IFF_NO_PI;

	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);

	if(ioctl(tun_fd, TUNSETIFF, (void *)&ifr) != 0)
	{
		mylog(log_fatal,"open /dev/net/tun failed");
		myexit(-1);
	}
	return tun_fd;
}

int set_if(char *if_name,char * local_ip,char * remote_ip,int mtu)
{
	//printf("i m here1\n");
	struct ifreq ifr;
	struct sockaddr_in sai;
	memset(&ifr,0,sizeof(ifr));
	memset(&sai, 0, sizeof(struct sockaddr));

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

    sai.sin_family = AF_INET;
    sai.sin_port = 0;
    sai.sin_addr.s_addr = inet_addr(local_ip);
    memcpy(&ifr.ifr_addr,&sai, sizeof(struct sockaddr));
    assert(ioctl(sockfd, SIOCSIFADDR, &ifr)==0);



    sai.sin_addr.s_addr = inet_addr(local_ip);
    memcpy(&ifr.ifr_addr,&sai, sizeof(struct sockaddr));
    assert(ioctl(sockfd, SIOCSIFADDR, &ifr)==0);



    sai.sin_addr.s_addr = inet_addr(remote_ip);
    memcpy(&ifr.ifr_addr,&sai, sizeof(struct sockaddr));
    assert(ioctl(sockfd, SIOCSIFDSTADDR, &ifr)==0);

    ifr.ifr_mtu=mtu;
    assert(ioctl(sockfd, SIOCSIFMTU, &ifr)==0);


    assert(ioctl(sockfd, SIOCGIFFLAGS, &ifr)==0);
   // ifr.ifr_flags |= ( IFF_UP|IFF_POINTOPOINT|IFF_RUNNING|IFF_NOARP|IFF_MULTICAST );
    ifr.ifr_flags |= ( IFF_UP|IFF_POINTOPOINT|IFF_RUNNING|IFF_NOARP|IFF_MULTICAST );
    assert(ioctl(sockfd, SIOCSIFFLAGS, &ifr)==0);

    //printf("i m here2\n");
	return 0;
}

//enum tun_header_t {header_reserved=0,header_normal=1,header_new=2,header_reject=3};
const char header_normal=1;
const char header_new_connect=2;
const char header_reject=3;

int put_header(char header,char *& data,int &len)
{
	assert(len>=0);
	data=data-1;
	data[0]=header;
	len+=1;
	return 0;
}
int get_header(char &header,char *& data,int &len)
{
	assert(len>=0);
	if(len<1) return -1;
	header=data[0];
	data=data+1;
	len-=1;
	return 0;
}


int tun_dev_client_event_loop()
{
	char buf0[buf_len+100];
	char *data=buf0+100;
	int len;
	int i,j,k,ret;
	int epoll_fd,tun_fd;

	int remote_fd;
	fd64_t remote_fd64;

	tun_fd=get_tun_fd("tun11");
	assert(tun_fd>0);

	assert(new_connected_socket(remote_fd,remote_ip_uint32,remote_port)==0);
	remote_fd64=fd_manager.create(remote_fd);

	assert(set_if("tun11","10.0.0.2","10.0.0.1",1000)==0);

	epoll_fd = epoll_create1(0);
	assert(epoll_fd>0);

	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epoll_fd < 0) {
		mylog(log_fatal,"epoll return %d\n", epoll_fd);
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = remote_fd64;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, remote_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  remote_fd64 error\n");
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = tun_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tun_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  tun_fd error\n");
		myexit(-1);
	}


	ev.events = EPOLLIN;
	ev.data.u64 = delay_manager.get_timer_fd();

	mylog(log_debug,"delay_manager.get_timer_fd()=%d\n",delay_manager.get_timer_fd());
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, delay_manager.get_timer_fd(), &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add delay_manager.get_timer_fd() error\n");
		myexit(-1);
	}

	int fifo_fd=-1;

	if(fifo_file[0]!=0)
	{
		fifo_fd=create_fifo(fifo_file);
		ev.events = EPOLLIN;
		ev.data.u64 = fifo_fd;

		ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fifo_fd, &ev);
		if (ret!= 0) {
			mylog(log_fatal,"add fifo_fd to epoll error %s\n",strerror(errno));
			myexit(-1);
		}
		mylog(log_info,"fifo_file=%s\n",fifo_file);
	}

	dest_t dest;
	dest.type=type_fd64;
	dest.inner.fd64=remote_fd64;
	//dest.conv=conv;
	//dest.inner.ip_port=dest_ip_port;
	//dest.cook=1;


	int got_feed_back=0;

	while(1)////////////////////////
	{

		if(about_to_exit) myexit(0);

		int nfds = epoll_wait(epoll_fd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			if(errno==EINTR  )
			{
				mylog(log_info,"epoll interrupted by signal\n");
				myexit(0);
			}
			else
			{
				mylog(log_fatal,"epoll_wait return %d\n", nfds);
				myexit(-1);
			}
		}
		int idx;
		for (idx = 0; idx < nfds; ++idx)
		{
			if(events[idx].data.u64==(u64_t)tun_fd)
			{
				len=read(tun_fd,data,max_data_len);
				assert(len>=0);

				mylog(log_trace,"Received packet from tun,len: %d\n",len);

				if(got_feed_back==0)
					put_header(header_new_connect,data,len);
				else
					put_header(header_normal,data,len);

				do_cook(data,len);

				delay_manager.add(0,dest,data,len);
			}
			else if(events[idx].data.u64==(u64_t)remote_fd64)
			{
				fd64_t fd64=events[idx].data.u64;
				int fd=fd_manager.to_fd(fd64);

				len=recv(fd,data,max_data_len,0);

				if(len<0)
				{
					mylog(log_warn,"recv return %d,errno=%s\n",len,strerror(errno));
					continue;
				}

				if(de_cook(data,len)<0)
				{
					mylog(log_warn,"de_cook(data,len)failed \n");
					continue;

				}


				char header=0;
				if(get_header(header,data,len)!=0)
				{
					mylog(log_warn,"get_header failed\n");
					continue;
				}
				if(header==header_reject)
				{
					mylog(log_fatal,"server switched to handle another client,exit\n");
					myexit(-1);
					continue;
				}
				else if(header==header_normal)
				{
					got_feed_back=1;
				}
				else
				{
					mylog(log_warn,"invalid header\n");
					continue;
				}

				mylog(log_trace,"Received packet from udp,len: %d\n",len);
				assert(len>=0);

				assert(write(tun_fd,data,len)>=0);
			}
		    else if (events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd())
		    {
				uint64_t value;
				read(delay_manager.get_timer_fd(), &value, 8);
				mylog(log_trace,"events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()\n");
				//printf("<timerfd_triggered, %d>",delay_mp.size());
				//fflush(stdout);
			}
			else if (events[idx].data.u64 == (u64_t)fifo_fd)
			{
				char buf[buf_len];
				int len=read (fifo_fd, buf, sizeof (buf));
				if(len<0)
				{
					mylog(log_warn,"fifo read failed len=%d,errno=%s\n",len,strerror(errno));
					continue;
				}
				buf[len]=0;
				handle_command(buf);
			}
			else
			{
				assert(0==1);
			}
		}
		delay_manager.check();
	}


	return 0;
}

int tun_dev_server_event_loop()
{
	char buf0[buf_len+100];
	char *data=buf0+100;
	int len;
	int i,j,k,ret;
	int epoll_fd,tun_fd;

	int local_listen_fd;
	//fd64_t local_listen_fd64;

	tun_fd=get_tun_fd("tun11");
	assert(tun_fd>0);

	assert(new_listen_socket(local_listen_fd,local_ip_uint32,local_port)==0);
  //  local_listen_fd64=fd_manager.create(local_listen_fd);

	assert(set_if("tun11","10.0.0.1","10.0.0.2",1000)==0);

	epoll_fd = epoll_create1(0);
	assert(epoll_fd>0);

	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epoll_fd < 0) {
		mylog(log_fatal,"epoll return %d\n", epoll_fd);
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = local_listen_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_listen_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  udp_listen_fd error\n");
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = tun_fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tun_fd, &ev);
	if (ret!=0) {
		mylog(log_fatal,"add  tun_fd error\n");
		myexit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.u64 = delay_manager.get_timer_fd();

	mylog(log_debug,"delay_manager.get_timer_fd()=%d\n",delay_manager.get_timer_fd());
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, delay_manager.get_timer_fd(), &ev);
	if (ret!= 0) {
		mylog(log_fatal,"add delay_manager.get_timer_fd() error\n");
		myexit(-1);
	}

	int fifo_fd=-1;

	if(fifo_file[0]!=0)
	{
		fifo_fd=create_fifo(fifo_file);
		ev.events = EPOLLIN;
		ev.data.u64 = fifo_fd;

		ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fifo_fd, &ev);
		if (ret!= 0) {
			mylog(log_fatal,"add fifo_fd to epoll error %s\n",strerror(errno));
			myexit(-1);
		}
		mylog(log_info,"fifo_file=%s\n",fifo_file);
	}

	//ip_port_t dest_ip_port;

	dest_t dest;
	dest.type=type_fd_ip_port;

	dest.inner.fd_ip_port.fd=local_listen_fd;
	dest.inner.fd_ip_port.ip_port.ip=0;
	dest.inner.fd_ip_port.ip_port.port=0;
	//dest.conv=conv;
	//dest.inner.ip_port=dest_ip_port;
	//dest.cook=1;

	while(1)////////////////////////
	{

		if(about_to_exit) myexit(0);

		int nfds = epoll_wait(epoll_fd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			if(errno==EINTR  )
			{
				mylog(log_info,"epoll interrupted by signal\n");
				myexit(0);
			}
			else
			{
				mylog(log_fatal,"epoll_wait return %d\n", nfds);
				myexit(-1);
			}
		}
		int idx;
		for (idx = 0; idx < nfds; ++idx)
		{

			if(events[idx].data.u64==(u64_t)local_listen_fd)
			{
				struct sockaddr_in udp_new_addr_in={0};
				socklen_t udp_new_addr_len = sizeof(sockaddr_in);
				if ((len = recvfrom(local_listen_fd, data, max_data_len, 0,
						(struct sockaddr *) &udp_new_addr_in, &udp_new_addr_len)) < 0) {
					mylog(log_error,"recv_from error,this shouldnt happen,err=%s,but we can try to continue\n",strerror(errno));
					continue;
					//myexit(1);
				};


				if(de_cook(data,len)<0)
				{
					mylog(log_warn,"de_cook(data,len)failed \n");
					continue;

				}

				char header=0;
				if(get_header(header,data,len)!=0)
				{
					mylog(log_warn,"get_header failed\n");
					continue;
				}

				if((dest.inner.fd_ip_port.ip_port.ip==udp_new_addr_in.sin_addr.s_addr) && (dest.inner.fd_ip_port.ip_port.port=ntohs(udp_new_addr_in.sin_port)))
				{
					if(header!=header_new_connect&& header!=header_normal)
					{
						mylog(log_warn,"invalid header\n");
						continue;
					}
				}
				else
				{
					if(header==header_new_connect)
					{
						mylog(log_info,"new connection from %s:%d \n", inet_ntoa(udp_new_addr_in.sin_addr),
												ntohs(udp_new_addr_in.sin_port));
						dest.inner.fd_ip_port.ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
						dest.inner.fd_ip_port.ip_port.port=ntohs(udp_new_addr_in.sin_port);
					}
					else
					{
						mylog(log_info,"rejected connection from %s:%d\n", inet_ntoa(udp_new_addr_in.sin_addr),ntohs(udp_new_addr_in.sin_port));


						len=1;
						data[0]=header_reject;

						do_cook(data,len);

						dest_t tmp_dest;
						tmp_dest.type=type_fd_ip_port;

						tmp_dest.inner.fd_ip_port.fd=local_listen_fd;
						tmp_dest.inner.fd_ip_port.ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
						tmp_dest.inner.fd_ip_port.ip_port.port=ntohs(udp_new_addr_in.sin_port);

						delay_manager.add(0,tmp_dest,data,len);;
						continue;
					}
				}



				mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port),len);

				ret=write(tun_fd,data,len);
				if( ret<0 )
				{
					mylog(log_warn,"write to tun failed len=%d ret=%d\n errno=%s\n",len,ret,strerror(errno));
				}

			}
			else if(events[idx].data.u64==(u64_t)tun_fd)
			{
				len=read(tun_fd,data,max_data_len);
				assert(len>=0);

				mylog(log_trace,"Received packet from tun,len: %d\n",len);

				if(dest.inner.fd64_ip_port.ip_port.to_u64()==0)
				{
					mylog(log_warn,"there is no client yet\n");
					continue;
				}

				put_header(header_normal,data,len);

				do_cook(data,len);

				delay_manager.add(0,dest,data,len);;

			}
		    else if (events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd())
		    {
				uint64_t value;
				read(delay_manager.get_timer_fd(), &value, 8);
				mylog(log_trace,"events[idx].data.u64 == (u64_t)delay_manager.get_timer_fd()\n");
				//printf("<timerfd_triggered, %d>",delay_mp.size());
				//fflush(stdout);
			}
			else if (events[idx].data.u64 == (u64_t)fifo_fd)
			{
				char buf[buf_len];
				int len=read (fifo_fd, buf, sizeof (buf));
				if(len<0)
				{
					mylog(log_warn,"fifo read failed len=%d,errno=%s\n",len,strerror(errno));
					continue;
				}
				buf[len]=0;
				handle_command(buf);
			}
			else
			{
				assert(0==1);
			}
		}
		delay_manager.check();
	}


	return 0;
}
