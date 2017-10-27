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


int tun_dev_client_event_loop()
{
	char buf[buf_len+1];
	//char *data=buf+1;
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

	dest_t dest;
	dest.type=type_fd64;
	dest.inner.fd64=remote_fd64;
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
			if(events[idx].data.u64==(u64_t)tun_fd)
			{
				len=read(tun_fd,buf,max_data_len);
				assert(len>=0);

				mylog(log_trace,"Received packet from tun,len: %d\n",len);

				delay_manager.add(0,dest,buf,len);;
			}
			else if(events[idx].data.u64==(u64_t)remote_fd64)
			{
				fd64_t fd64=events[idx].data.u64;
				int fd=fd_manager.to_fd(fd64);

				len=recv(fd,buf,max_data_len,0);

				mylog(log_trace,"Received packet from udp,len: %d\n",len);
				assert(len>=0);

				assert(write(tun_fd,buf,len)>0);
			}
		}
		delay_manager.check();
	}


	return 0;
}

int tun_dev_server_event_loop()
{
	char buf[buf_len+1];
	char *data=buf+1;
	int len;
	int i,j,k,ret;
	int epoll_fd,tun_fd;

	tun_fd=get_tun_fd("tun11");
	assert(tun_fd>0);

	assert(new_listen_socket(local_listen_fd,local_ip_uint32,local_port)==0);

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

	//ip_port_t dest_ip_port;

	dest_t dest;
	dest.type=type_ip_port;
	dest.inner.ip_port.ip=0;
	dest.inner.ip_port.port=0;
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
				if ((len = recvfrom(local_listen_fd, buf, max_data_len, 0,
						(struct sockaddr *) &udp_new_addr_in, &udp_new_addr_len)) == -1) {
					mylog(log_error,"recv_from error,this shouldnt happen,err=%s,but we can try to continue\n",strerror(errno));
					continue;
					//myexit(1);
				};

				dest.inner.ip_port.ip=udp_new_addr_in.sin_addr.s_addr;
				dest.inner.ip_port.port=ntohs(udp_new_addr_in.sin_port);

				mylog(log_trace,"Received packet from %s:%d,len: %d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port),len);

				assert(write(tun_fd,buf,len)>0);

			}
			else if(events[idx].data.u64==(u64_t)tun_fd)
			{
				len=read(tun_fd,buf,max_data_len);
				assert(len>=0);

				mylog(log_trace,"Received packet from tun,len: %d\n",len);

				if(dest.inner.ip_port.to_u64()==0)
				{
					mylog(log_warn,"there is no client yet\n");
					continue;
				}

				delay_manager.add(0,dest,buf,len);;


			}
		}
		delay_manager.check();
	}


	return 0;
}
