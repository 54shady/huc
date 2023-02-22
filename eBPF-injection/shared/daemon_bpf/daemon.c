#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "bpf_injection_msg.h"

#define HUC_DEV_NODE "/dev/hucdev"

int main(int argc, char *argv[])
{
	int offset;
	int payload_left;
	int len;
	struct bpf_injection_msg_t mymsg;

	int fd = open(HUC_DEV_NODE, O_RDWR);
	if (fd < 0)
	{
		perror("open error\n");
		exit(EXIT_FAILURE);
	}

	/* recv bytecode */

	/* skip the first 4 * 4 bytes */
	if (lseek(fd, 16, SEEK_SET) < 0)
	{
		perror("seek\n");
		return 1;
	}
	printf("Waiting for a bytecode...\n");

	/* read header first */
	len = read(fd, &(mymsg.header), sizeof(struct bpf_injection_msg_header));
	if (len < 0)
	{
		perror("read error\n");
		return 1;
	}
	print_bpf_injection_message(mymsg.header);

	/* alloc buf for payload */
	mymsg.payload = malloc(mymsg.header.payload_len);
	if (!mymsg.payload)
	{
		perror("malloc error\n");
		return 1;
	}

	/* ready to read payload */
	if (lseek(fd, 20, SEEK_SET) < 0)
	{
		perror("lseek\n");
		return 1;
	}
	offset = 0;
	payload_left = mymsg.header.payload_len;

	while (payload_left > 0)
	{
		len = read(fd, mymsg.payload + offset, 4);
		if (len < 0)
		{
			perror("read payload error");
			return 1;
		}
		offset += len;
		payload_left -= len;
	}

	/* save bytecode to local file */
	FILE *fp;
	fp = fopen("/tmp/bytecode", "w");
	if (fp == NULL)
	{
		perror("fopen");
		return 1;
	}

	if (fwrite(mymsg.payload, 1, mymsg.header.payload_len, fp) != mymsg.header.payload_len)
	{
		perror("fwrite error\n");
		return 1;
	}

	if (fclose(fp) != 0)
	{
		perror("fclose");
		return 1;
	}

	free(mymsg.payload);
	close(fd);

	return 0;
}
