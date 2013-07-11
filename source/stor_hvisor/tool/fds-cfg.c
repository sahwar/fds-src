#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include "fbd.h"


static int  add_device()
{


	return 0;
}
static int  set_blocksize( int bsize, int  cmd, int dev_id)
{

	int ret = 0;

	if ((ret = ioctl(dev_id, cmd, bsize)) < 0)
	{
		printf("Error %d: setting the block size \n",ret);
		return ret;
	}

	return ret;
}

static int set_tgtsize(int tgt_size, int cmd, int dev_id)
{
	int ret = 0;
	if((ret = ioctl(dev_id, cmd, tgt_size)) < 0)
	{
		printf("Error %d: setting the  target disk  size \n",ret);
		return ret;
	}

	return ret;
}


static int connect_cluster_tcp_udp(int ipaddr, int cmd, int dev_id)
{
	int ret = 0;
	if((ret = ioctl(dev_id, cmd, ipaddr)) < 0)
	{
		printf("Error %d: Connecting to the fds cluster \n",ret);
		return ret;
	}

	return ret;
}

static int disconnect_cluster(int ipaddr, int cmd, int dev_id)
{
	int ret = 0;
	if((ret = ioctl(dev_id, cmd, ipaddr)) < 0)
	{
		printf("Error %d:  Dis connecting  the fds cluster  \n",ret);
		return ret;
	}

	return ret;
}


static int read_vvc_catalog(int count, int cmd, int dev_id)
{
	int ret = 0;
	if((ret = ioctl(dev_id, cmd, count)) < 0)
	{
		printf("Error %d:  Reading the VVC catalog  \n",ret);
		return ret;
	}

	return ret;
}

void usage(char* errmsg, ...) 
{
	fprintf(stderr, "\n" 
			"Usage:fds-cfg\n"
			"\n"
			"-d <size>             disk size in bytes \n"
			"-b <block-size>       block size ( 1024 2048 ..) \n"
			"a                     Add new block device \n"
			"-t <tgt-ipaddr>       TCP-Open Cluster Connection \n"
			"-u <tgt-ipaddr>       UDP-Open Cluster Connection \n"
			"-r <vvc-count>        Read  VVC  catalog  \n"
			"-c                    Close Cluster connection\n"
			"h 		       help \n\n");
	
}


int    main(int argc, char *argv[])
{
	int  blk_size;
	int  tgt_size;
	int  blk_cnt;
	int  fbd= 0;
	long int  ipaddr=0;
	int  opt;

	struct option long_options[] = {
		{ "size", required_argument, NULL, 'd' },
		{ "block-size", required_argument, NULL, 'b' },
		{ "add-dev", no_argument, NULL, 'a' },
		{ "tgt-ipaddr", required_argument, NULL, 't' },
		{ "tgt-ipaddr", required_argument, NULL, 'u' },
		{ "vvc-count", required_argument, NULL, 'r' },
		{ "tgt-disconnect", no_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ 0, 0, 0, 0 }, 
	};

	fbd = open("/dev/fbd0", O_RDWR);
//	fbd = open("/dev/fbd0", O_WRONLY);
	if (fbd < 0)
	  	printf( "Cannot open  the device: Please emake sure the  device is  created \n");

	while((opt=getopt_long_only(argc, argv, "-d:-b:a:-t:-u:-r:c:h:", long_options, NULL))>=0) 
	{
		switch(opt) {
		case 'b':
			blk_size=(int)strtol(optarg, NULL, 0);
			set_blocksize( blk_size,FBD_SET_TGT_BLK_SIZE, fbd);
			break;
		case 'd':
			tgt_size=(int)strtol(optarg, NULL, 0);
			set_tgtsize( tgt_size,FBD_SET_TGT_SIZE, fbd);
			break;
		case 'a':
			add_device(optarg);
			exit(EXIT_SUCCESS);
		case 't':
			ipaddr = inet_addr(optarg);
			connect_cluster_tcp_udp(ntohl(ipaddr),FBD_OPEN_TARGET_CON_TCP,fbd);
			exit(EXIT_SUCCESS);
		case 'u':
			ipaddr = inet_addr(optarg);
			connect_cluster_tcp_udp(ntohl(ipaddr),FBD_OPEN_TARGET_CON_UDP,fbd);
			printf(" Received the response from  driver \n");
			break;
		case 'r':
			blk_cnt=(int)strtol(optarg, NULL, 0);
			read_vvc_catalog(blk_cnt,FBD_READ_VVC_CATALOG,fbd);
			break;
		case 'c':
			disconnect_cluster(ipaddr,FBD_CLOSE_TARGET_CON,fbd);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(NULL);
			exit(EXIT_SUCCESS);
		default:
			usage(NULL);
			exit(EXIT_FAILURE);
		}
	}
	
printf(" closing the device \n");
  close(fbd);
  return 0;
}

