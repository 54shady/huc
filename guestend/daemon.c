#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sched.h>
#include <signal.h>

#include "bpf_injection_msg.h"

#include "bpf_load.h"

#define HUC_DEV_NODE "/dev/hucdev"
#define MAX_CPU 64
#define SET_SIZE CPU_ALLOC_SIZE(64)
#define IOCTL_SCHED_SETAFFINITY 13
#define IOCTL_PROGRAM_INJECTION_RESULT_READY 14

int isPowerOfTwo(uint64_t n);
int findPosition(uint64_t n);
void print_cpu_set_mask(cpu_set_t* set);
void print_binary_mask(uint64_t value, int len);

/* 从fd中读出bytecode保存到返回值 */
static struct bpf_injection_msg_t recv_bytecode(int fd)
{
	struct bpf_injection_msg_t mymsg;
	int len;
	int offset = 0;
	int payload_left;

	/* skip the first 4 * 4 bytes */
	if (lseek(fd, 16, SEEK_SET) < 0)
	{
		perror("seek\n");
		return mymsg;
	}
	printf("\nDaemon process %d waiting for a bytecode...\n", getpid());

	/* read header first */
	len = read(fd, &(mymsg.header), sizeof(struct bpf_injection_msg_header));
	if (len < 0)
	{
		perror("read error\n");
		return mymsg;
	}
	print_bpf_injection_message(mymsg.header);

	/* alloc buf for payload */
	mymsg.payload = malloc(mymsg.header.payload_len);
	if (!mymsg.payload)
	{
		perror("malloc error\n");
		return mymsg;
	}

	/* ready to read payload */
	if (lseek(fd, 20, SEEK_SET) < 0)
	{
		perror("lseek\n");
		return mymsg;
	}
	payload_left = mymsg.header.payload_len;

	while (payload_left > 0)
	{
		len = read(fd, mymsg.payload + offset, 4);
		if (len < 0)
		{
			perror("read payload error");
			return mymsg;
		}
		offset += len;
		payload_left -= len;
	}

	printf("Received payload of %d bytes.\n", offset);
	return mymsg;
}

/* save bytecode from buf to local path with len
 * @path : point to local path
 * @buf : bytecode(the payload in bpf_injection_msg_t)
 * @len : bytecode len(the payload_len in header)
 * */
#define SAVED_BYTECODE_FILE "/tmp/bytecode"
static int save_bytecode(const char *path, void *buf, unsigned len)
{
	/* save bytecode to local file */
	FILE *fp;
	fp = fopen(path, "w");
	if (fp == NULL)
	{
		perror("fopen");
		return 1;
	}

	if (fwrite(buf, 1, len, fp) != len)
	{
		perror("fwrite error\n");
		return 1;
	}

	if (fclose(fp) != 0)
	{
		perror("fclose");
		return 1;
	}

	return 0;
}

int isPowerOfTwo(uint64_t n)
{
    return n && (!(n & (n - 1)));
}

int findPosition(uint64_t n)
{
	uint64_t i = 1;
	int pos = 0;

    if (!isPowerOfTwo(n)) {
        return -1;
    }
    while (!(i & n)) {
        i = i << 1;
        ++pos;
    }
    return pos;
}

void print_cpu_set_mask(cpu_set_t* set)
{
	int i;

	printf("CPU mask set on cpus:");

	for (i = 0; i < MAX_CPU; i++) {
		if (CPU_ISSET_S(i, SET_SIZE, set)) {
            printf(" %d", i);
        }
    }
    printf("\n");
}

void print_binary_mask(uint64_t value, int len)
{
	uint64_t flag;
	int i;

	if (len > 8) {
		return;
	}
	for (i = 0; i < (len * 8); i++) {
		flag = 1;
		flag = flag << i;
		if (value & flag) {
			printf("1");
		}
		else {
			printf("0");
		}
	}
	printf("\n");
}

int main(int argc, char *argv[])
{
	struct bpf_injection_msg_t mymsg;
	int pid;
	int child_pid = -1;
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 50000000L;
	uint64_t value = 0;
	uint32_t index = 0;

	int fd = open(HUC_DEV_NODE, O_RDWR);
	if (fd < 0)
	{
		perror("open error\n");
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		mymsg = recv_bytecode(fd);

		/* 根据头部信息中的type类型来处理 */
		switch (mymsg.header.type)
		{
			case PROGRAM_INJECTION:
				/* 1. save to local file */
				printf("Writing bytecode to %s ", SAVED_BYTECODE_FILE);
				save_bytecode(SAVED_BYTECODE_FILE,
						mymsg.payload,
						mymsg.header.payload_len);
				printf("successfully\n");
				free(mymsg.payload);

				/*
				 * 2. 创建子进程来加载bytecode
				 * 每次都加载新接收的bytecode
				 * 所以需要杀掉之前的子进程
				 */
				pid = fork();
				if (pid == -1)
				{
					perror("Fork error\n");
					child_pid = -1;
					break;
				}
				else if (pid == 0)
				{
					/* child process */
					child_pid = 0;
				}
				else
				{
					if (child_pid != -1)
					{
						kill(child_pid, SIGKILL);
						printf("Kill old child, ready to load new bytecode\n");
					}
					child_pid = pid;
					printf("New child pid: %d\n", child_pid);
					/*
					 * 父进程在这里break后就退出case了
					 * 只有子进程会执行下面的load bytecode 代码
					 *
					 * 父进程进入下一次接受新bytecode的入口
					 * 子进程处理bytecode的事务
					 */
					break;
				}

				/* Child process do the work */
				printf("Pid: %d Loading bytecode...", getpid());
				if (load_bpf_file(SAVED_BYTECODE_FILE))
				{
					printf("Load bytecode error\n");
					return 1;
				}
				printf("Load bytecode Done\n");
				printf("map_fd[0] = %d\n", map_fd[0]);

				sleep(1);

				while (1)
				{
					int i;
					cpu_set_t *set;
					int cpu_index;

					nanosleep(&ts, NULL);

					/* 用户态使用libbpf里的bpf call linux/tools/lib/bpf/bpf.c */
					bpf_map_lookup_elem(map_fd[0], &index, &value); /* count for 0 */
					if (value != 0)
					{
						set = CPU_ALLOC(MAX_CPU);
						/* count from 1, cuz 0 is count above */
						for (i = MAX_CPU; i > 0; i--)
						{
							bpf_map_lookup_elem(map_fd[0], &i, &value);
							if (value != 0)
							{
								memcpy(set, &value, SET_SIZE);
								cpu_index = findPosition(value);
								printf("Index_%02u = %ld, cindex = %d\n", i,
										value, cpu_index);

								print_cpu_set_mask(set);
								printf("Binary mask: ");
								print_binary_mask(value, 2);
								if (isPowerOfTwo(value)) {
									printf("CPU mask refers to one cpu only: cpu #%d\n", cpu_index);
								}
								else {
									printf("CPU mask refers to multiple cpus.\n");
								}

								/* 根据从bpf map中读取的值来设置cpu mask */
								printf("Set cpu mask %ld>>>\n", value);
								ioctl(fd, IOCTL_SCHED_SETAFFINITY, &value);
								value = 0;
								index = i;
								bpf_map_update_elem(map_fd[0], &index, &value, BPF_ANY);
							}
						}
						CPU_FREE(set);
						index = 0;
						bpf_map_update_elem(map_fd[0], &index, &value, BPF_ANY);
					}
					/* value += 2; */
					/* bpf_map_update_elem(map_fd[0], &index, &value, BPF_ANY); */
				}
				break;
			default:
				printf("Unrecognized bpf_injection_message type (%d).\n", mymsg.header.type);
				break;
		}
	}

	close(fd);

	return 0;
}
