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
#include <signal.h>
#include <time.h>

#define SIZE_64BYTE 64
#define DEV_NODE "/dev/testdemo"

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char buf[SIZE_64BYTE] = {0};
	char tmp[SIZE_64BYTE] = {0};
	FILE *fp;

	if (argc < 2)
	{
		printf("Usage ./rw-test r\n");
		printf("Usage ./rw-test w <file>\n");
		return 0;
	}

	fd = open(DEV_NODE, O_RDWR);
	if (fd < 0)
	{
		perror("open error\n");
		exit(EXIT_FAILURE);
	}

	if (!strcmp(argv[1], "r"))
	{
		ret = lseek(fd, 0, SEEK_SET);
		ret = read(fd, buf, SIZE_64BYTE);
		printf("read %d\n", ret);
		printf("buf = %s\n", buf);
	}
	else if (!strcmp(argv[1], "w"))
	{
		fp = fopen(argv[2], "r");
		if (!fp)
		{
			printf("fopen error\n");
			return -1;
		}

		ret = fread(tmp, 1, SIZE_64BYTE, fp);
		printf("read %d bytes data\n", ret);
		fclose(fp);

		ret = write(fd, tmp, ret < SIZE_64BYTE ? ret : SIZE_64BYTE);
		printf("write %d\n", ret);
	}
	else
	{
		printf("Unsupport command\n");
	}

	close(fd);

	return 0;
}
