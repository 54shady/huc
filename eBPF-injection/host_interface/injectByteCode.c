#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/sysinfo.h>

#include <sched.h>

#include "huc_msg.h"

#define PORT            8888
#define SERVERHOST      "localhost"

void init_sockaddr(struct sockaddr_in *name, const char *hostname, uint16_t port)
{
	struct hostent *hostinfo;

	name->sin_family = AF_INET;
	name->sin_port = htons(port);
	hostinfo = gethostbyname(hostname);
	if (hostinfo == NULL)
	{
		printf("unknow host %s\n", hostname);
		exit(EXIT_FAILURE);
	}
	name->sin_addr = *(struct in_addr *)hostinfo->h_addr;
}

int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_in servername;
	struct huc_msg_t mymsg;

	mymsg = prepare_huc_message("/root/hyperupcall/eBPF-injection/bpfProg/myprog.o");

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("create sock error on client");
		exit(EXIT_FAILURE);
	}

	init_sockaddr(&servername, SERVERHOST, PORT);
	if (connect(sock, (struct sockaddr *)&servername, sizeof(servername)) < 0)
	{
		perror("Connect error");
		exit(EXIT_FAILURE);
	}

	send(sock, &(mymsg.header), sizeof(struct huc_msg_header), 0);
	send(sock, mymsg.payload, mymsg.header.payload_len, 0);

	free(mymsg.payload);
	close(sock);

	return 0;
}
