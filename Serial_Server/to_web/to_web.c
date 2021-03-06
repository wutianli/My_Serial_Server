/*******************************************************************
和WEB后台通讯模块：该模块负责和WEB后台通讯，主要完成串口重启，服务器重启，
服务器静态IP设置，服务器恢复出厂设置四个功能。
*******************************************************************/

#include "com_ser.h"

//获取MAC地址
int get_mac(char* mac)
{
    int sockfd;
    struct ifreq tmp;
    char mac_addr[30];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0)
    {
        perror("create socket fail\n");
        return -1;
    }

    memset(&tmp,0,sizeof(struct ifreq));
    strncpy(tmp.ifr_name,"eth0",sizeof(tmp.ifr_name)-1);
    if( (ioctl(sockfd,SIOCGIFHWADDR,&tmp)) < 0 )
    {
        printf("mac ioctl error\n");
        return -1;
    }

    sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x",
            (unsigned char)tmp.ifr_hwaddr.sa_data[0],
            (unsigned char)tmp.ifr_hwaddr.sa_data[1],
            (unsigned char)tmp.ifr_hwaddr.sa_data[2],
            (unsigned char)tmp.ifr_hwaddr.sa_data[3],
            (unsigned char)tmp.ifr_hwaddr.sa_data[4],
            (unsigned char)tmp.ifr_hwaddr.sa_data[5]
            );

    close(sockfd);
    memcpy(mac,mac_addr,strlen(mac_addr));

    return 0;
}

/*服务器静态IP 设置函数*/
int ip_set()
{
	User  user;
	FILE  *fp 	   = NULL;
	int   i = 0, j = 0;
	int   file_size;
	char  *pfile   = NULL;
	char  *p       = NULL;
	char  find_str[50];

	char  mac[30];
	char  mac_t[30];
	char  *ver_buffer[6] = {"ip=\"","mask=\"","gateway=\"","mac=\"","dns_a=\"","dns_b=\""};

	get_mac(mac);

	//将无” ：“的Mac地址转成有” ：“的Mac地址
	for(i,j; i < 18,j < 12; i++,j++)
	{
		if(i == 2 || i == 5 || i == 8 || i == 11 || i == 14)
		{
			mac_t[i] = ':';
			i++;
		}
		mac_t[i] = mac[j];
	}
	mac_t[17] = '\0';

	//从文件读取IP,网络掩码，网关，Mac地址，DNS服务器地址
	if((fp=fopen(IP_SET_DIR,"r"))==NULL)
	{
		printf("open ip.conf fail!\n");
		return(-1);
	}
	fseek(fp,0,SEEK_END);
	file_size=ftell(fp);
	rewind(fp);

	if((pfile = malloc(file_size)) == NULL)
	{
		printf("malloc fail!\n");
		return(-1);
	}
	fread(pfile,file_size,1,fp);

	for(i = 0;i < 6;i++)
	{
		if((p = strstr(pfile,ver_buffer[i]))!=NULL)
		{
			memset(find_str, 0, sizeof(find_str));
			sprintf(find_str ,"%s%s", ver_buffer[i],"%[^\"]");
			sscanf(p, find_str, (char *)(&user)+i*IP_SET_NUM);
		}
	}
	fclose(fp);

	user.mac[17] = '\0';

	//调用系统命令配置IP相关参数
	if(0!= strcmp(user.mac,mac_t))
	{
		char mac_set[60] = "ifconfig eth0 hw ether ";
		strcat(mac_set, user.mac);

		system("ifconfig eth0 down");
		system(mac_set);
		usleep(1000);
		system("ifconfig eth0 up");
	}

	printf("%s\n",user.ip);
	printf("%s\n",user.mask);
	printf("%s\n",user.gateway);
	printf("%s\n",user.mac);

	//调用ifconfig eth0 xxx.xxx.xxx.xxx netmask xxx.xxx.xxx.xxx up
	char ip[60] = "ifconfig eth0 ";
	strcat(ip, user.ip);
	strcat(ip, " netmask ");
	strcat(ip, user.mask);
	strcat(ip, " up");
	system(ip);

	//调用route add default gw xxx.xxx.xxx.xxx
	char gw[40] = "route add default gw ";
	strcat(gw, user.gateway);
	system(gw);

	system("echo \"; generated by /sbin/dhclient-script\"> /etc/resolv.conf");
	system("echo \"search localhost\">> /etc/resolv.conf");

	char dns_a[60] = "echo \"nameserver ";
	strcat(dns_a,user.dns_a);
	strcat(dns_a,"\">> /etc/resolv.conf");
	system(dns_a);

	char dns_b[60] = "echo \"nameserver ";
	strcat(dns_b,user.dns_b);
	strcat(dns_b,"\">> /etc/resolv.conf");
	system(dns_b);

	return 0;
}

/*创建端口主线程等待线程函数*/
void create_wait_port(int port)
{
    int temp;

    memset(&thread_w, 0, sizeof(thread_w));

    if((temp = pthread_create(&thread_w, NULL, thread_wait_port,
    		                 (void *)port)) != 0)
	{
        printf("create_wait_port_error!\n");
		return;
	}
}

/*端口主线程等待线程*/
void *thread_wait_port(void *port)
{
	int port_n = (int)port;
	wait_port(port_n);
	pthread_exit(NULL);
}


void wait_wait_port(int port)
{
    if(thread_w !=0)
	{
        pthread_join(thread_w,NULL);
        printf("thread_wait_port\n");
    }
}

/*创建和WEB 后台通讯线程函数*/
void creat_to_web()
{
    int temp;

    memset(&thread_sk, 0, sizeof(thread_sk));

    if((temp = pthread_create(&thread_sk, NULL, thread_to_web, NULL)) != 0)
	{
        printf("thread_to_web_error!\n");
		return;
	}
}

/*和WEB 后台通讯线程*/
void *thread_to_web()
{
	int    ret, i = 0;
	int    fd, a_fd, opt;
	char   buf[SOCKET_BUF_NUM];
	struct sockaddr_in ser;

	if ((fd =socket(AF_INET,SOCK_STREAM,0))<0)
	{
		perror("socket");
		return;
	}

	opt = 1;
	setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

	memset(&ser,0,sizeof(ser));
	ser.sin_family=AF_INET;
	ser.sin_port=htons(SOCKET_PORT);
	ser.sin_addr.s_addr=htonl(INADDR_ANY);

	if((ret=bind(fd,(struct sockaddr *)&ser,sizeof(ser)))!=0)
	{
		perror("bind");
		return;
	}

	while(1)
	{
		memset(buf, '\0', sizeof(buf));
		listen(fd,1);

		a_fd = accept(fd,NULL,NULL);
		if(a_fd < 0 )
		{
			perror("accept");
			break;
		}

		recv(a_fd,buf,SOCKET_BUF_NUM,0);

		if(buf[0] == IP_SET_FLAG)//服务器静态IP 设置后立即生效
		{
			ip_set();
			//system("service network restart");
			//system("/etc/rc.d/init.d/networking restart");
		}
		if(buf[0] == SER_RESET_FLAG)//服务器重启
		{
			system("reboot");
		}
		if(buf[0] == RECOVER_FLAG)//服务器恢复出厂设置
		{
			system("cp -f /home/com/cfg/* /mnt/config/cfg/");
			usleep(1000);
			system("reboot");
		}
		if(buf[0] == P_RESET_FLAG)//端口重启
		{
			printf("%s\n",buf);
			for(i = 0; i < PORT_NUM + 1; i++)
			{
				if(buf[i + 1] == PORT_RESET_FLAG)
				{
					port_reset(i);
				}
			}
		}
		if(0 == strcmp(buf, UP_SYSTEM_FLAG))//文件系统升级
		{
			system("/bin/cp /tmp/rootfs.uboot /dev/mtdblock2");
			sleep(1);
			system("reboot");
		}
		if(0 == strcmp(buf, UP_KERNEL_FLAG))//内核升级
		{
			system("/bin/cp /tmp/uImage /dev/mtdblock1");
			sleep(1);
			system("reboot");
		}
		close(a_fd);
	}
	close(fd);
}

/*和WEB 通讯线程等待函数*/
void wait_to_web()
{
    if(thread_sk !=0)
	{
        pthread_join(thread_sk,NULL);
        printf("thread_to_web_over\n");
    }
}

int port_reset(int port)
{
	if(thread_m[port] !=0)
	{
		if(pthread_kill(thread_m[port], 0)!= ESRCH)
		{
			printf("pthread_kill(thread_m[port], 0)!= ESRCH!\n");

			if(thread_r[port] !=0)
			{
				//判断线程是否存在，如果存在，kill掉线程
				if(pthread_kill(thread_r[port], 0)!= ESRCH)
				{
					pthread_kill(thread_r[port], SIGQUIT);
				}
			}

			if(thread_s[port] !=0)
			{
				if(pthread_kill(thread_s[port], 0)!= ESRCH)
				{
					pthread_kill(thread_s[port], SIGQUIT);
				}
			}

			if(udp_thread_r[port] !=0)
			{
				if(pthread_kill(udp_thread_r[port], 0)!= ESRCH)
				{
					pthread_kill(udp_thread_r[port], SIGQUIT);
				}
			}

			if(udp_thread_s[port] !=0)
			{
				if(pthread_kill(udp_thread_s[port], 0)!= ESRCH)
				{
					pthread_kill(udp_thread_s[port], SIGQUIT);
				}
			}

			if(thread_recv_m[port] !=0)
			{
				if(pthread_kill(thread_recv_m[port], 0)!= ESRCH)
				{
					pthread_kill(thread_recv_m[port], SIGQUIT);
				}
			}

			if(thread_send_m[port] !=0)
			{
				if(pthread_kill(thread_send_m[port], 0)!= ESRCH)
				{
					pthread_kill(thread_send_m[port], SIGQUIT);
				}
			}
			if(pthread_kill(thread_m[port], 0)!= ESRCH)
			{
				printf("pthread_kill(thread_m2[port], 0)!= ESRCH!\n");
				pthread_kill(thread_m[port], SIGQUIT);
				port_create(port);
				printf("create_port\n");
			}
		}
		else
		{
			printf("pthread_kill(thread_m[port], 0)== ESRCH\n");
			port_create(port);
			printf("create_port\n");
		}
	}
	else
	{
		printf("thread_m[port] == 0\n");
		port_create(port);
		printf("create_port\n");
	}
	return 0;
}

/*重启串口函数*/
void port_create(int port)
{
	close(s_sockfd[port]);
	close(sockfd[port]);
	
	usleep(1000);
	
	create_port(port);
	
	create_wait_port(port);
	
	led_on();
}

