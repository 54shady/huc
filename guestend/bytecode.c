/*
 * BPF program to monitor CPU affinity tuning
 * 2020 Giacomo Pellicci
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <uapi/linux/bpf.h>
#include <linux/version.h>
#include <linux/ptrace.h>
//#include <bpf/bpf_helpers.h>
#include "bpf_helpers.h"
//#include <bpf/bpf_tracing.h>
//#include "bpf_tracing.h"

#define _(P) ({typeof(P) val = 0; bpf_probe_read(&val, sizeof(val), &P); val;})
#define MAX_ENTRIES 64

/*
 * Using BPF_MAP_TYPE_ARRAY map type all array elements pre-allocated
 * and zero initialized at init time
 *
 * bpfmap name : cpuindex_mask_maps
 * @key: cpu index
 * @value: cpu mask
 */
struct bpf_map_def SEC("maps") cpuindex_mask_maps = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
	.max_entries = MAX_ENTRIES,
};

/*
 * kprobe is NOT a stable ABI
 * kernel functions can be removed, renamed or completely change semantics.
 * Number of arguments and their positions can change, etc.
 * In such case this bpf+kprobe example will no longer be meaningful
 * 这里使用的是bpf+kprobe
 * 由于kprobe(即使用内核中函数作为卯点,可能会随内核版本变动)
 * 所以其实这种方式不是非常具有可移植性
 */
SEC("kprobe/sched_setaffinity")
int bpf_prog1(struct pt_regs *ctx){
	int ret;
	u64 cpu_set;
	u64 *top;
	u32 index = 0;
	int pid;
	char msg[] = "Set vcpu%d mask to %lu\n";

	/*
	 * no need to get pid info, bpf already done this yet
	 *
	 * char msg[] = "Pid %d Enter\n";
	 * pid = bpf_get_current_pid_tgid();
	 * bpf_trace_printk(msg, sizeof(msg), pid);
	 */

	/*
	 * System call prototype:
	 * 用户态系统调用接口如下
	 * int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
	 *
	 * 其对应的内核态接口如下
	 * asmlinkage long sys_sched_setaffinity(pid_t pid, unsigned int len,
	 *				unsigned long __user *user_mask_ptr);
	 *
	 * 内核中的函数原型如下(bpf中钩的是内核中的函数,而不是系统调用)
	 * 	In-kernel function sched_setaffinity has the following prototype:
	 *
	 * long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
	 * This is what we kprobe, not the system call.
	 * 这里注意区别在于参数位置
	 *
	 * 所以在下面代码中获取内核函数sched_setaffinity的参数cpusmask时
	 * 获取的是第二个参数,而不是第三个
	 * long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
	 * pid = (int)PT_REGS_PARM1(ctx);
	 * mask = PT_REGS_PARM2(ctx);
	 *
	 * 利用宏PT_REGS_PARMN来获取内核函数的第N个参数
	 * 参看tools/testing/selftests/bpf/bpf_helpers.h
	 *
	 * 内核中有程序通过调用了CPU_SET或sched_setaffinity设置了cpu mask
	 * 在bytecode中读取guest应用层设置的值(在cpu_set)
	 */
	ret = bpf_probe_read(&cpu_set, 8, (void*)PT_REGS_PARM2(ctx));

	top = bpf_map_lookup_elem(&cpuindex_mask_maps, &index);
	if (!top) {
		return 0;
	}
	if (*top == MAX_ENTRIES - 1) {
		return 0;
	}

	/* 多线程变量原子操作 */
	__sync_fetch_and_add(top, 1);

	index = *top;
	bpf_trace_printk(msg, sizeof(msg), index, cpu_set);

	/*
	 * 内核中使用bpf call: kernel/bpf/helpers.c
	 *
	 * 将cpu_set更新到bpfmap中去,这样daemon程序就能根据该值的变化
	 * 将数据传递到虚拟设备,让虚拟设备来实现设置cpu亲和性
	 */
	bpf_map_update_elem(&cpuindex_mask_maps, &index, &cpu_set, 0);

    return 0;
}

char _license[] SEC("license") = "GPL";

/* Useful because kprobe is NOT a stable ABI. (wrong version fails to be loaded) */
u32 _version SEC("version") = LINUX_VERSION_CODE;
