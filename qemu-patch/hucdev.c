#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#include <sys/inotify.h>
#include <errno.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "hw/misc/huc_msg.h"

#include "hw/core/cpu.h"

#include <sys/sysinfo.h>
#include <sched.h>

#define MAX_CPU 64
#define SET_SIZE CPU_ALLOC_SIZE(64)

#define HUCDEV_REG_MASK 0xFF
#define HUCDEV_BUF_MASK 0xFFFF

#define HUCDEV_REG_STATUS_IRQ 0
#define HUCDEV_REG_RAISE_IRQ 8
#define HUCDEV_REG_LOW_IRQ 12

#define HUCDEV_REG_PCI_BAR 0
#define HUCDEV_BUF_PCI_BAR 1

#define TYPE_HUC_DEVICE "hucdev"

#define HUCDEV_BUF_SIZE 65536

/* 根据父类找到子类 */
#define HUC_DEV(obj) OBJECT_CHECK(HucState, obj, TYPE_HUC_DEVICE)

#define HUCDEV_REG_END 92 /* in byte */

#define HUC_DEVICE_ID 0x11eb

#include "hw/misc/bpf_injection_msg.h"

/*
 * Device buffer mmio struct
 * offset in bytes/sizeof(uint32_t)
 * 因为buffer是uint32_t的数组
 * 所以数据存储在buffer中的偏移量需要按4字节对齐
 *
 * +---+--------------------------------+<-----buf
 * | 0 | irq_status [R] / raise_irq [W] |
 * +---+--------------------------------+
 * | 1 |          lower_irq [W]         |
 * +---+--------------------------------+
 * | 2 |      unspecified/reserved      |
 * +---+--------------------------------+
 * | 3 |      unspecified/reserved      |
 * +---+--------------------------------+<-----buf + 4(myheader)
 * | 4 |  payload_len | type | version  | guest应用程序读取这段数据需要跳过前4*4 字节
 * +---+                                | lseek(fd, 16, SEEK_SET)
 * | 5 |            buffer              |
 * +---+                                |
 * |   |                                |
 * |   |                                |
 *                  ......
 * |   |                                |
 * |   |                                |
 * +---+--------------------------------+
 */

static const char *regnames[] = {
	"STATUS",
	"CTRL",
	"RAISE_IRQ",
	"LOWER_IRQ"
};

//int map_hyperthread(cpu_set_t* set);

typedef struct {
	PCIDevice pdev; /* 父类是pci设备 */

	MemoryRegion regs; /* legacy寄存器io操作 */
	MemoryRegion mmio; /* mmio类型的操作 */

	/*
	 * 用于存储寄存器的空间
	 * 每个寄存器是4byte 所以右移动2位
	 * 所以一共是92 >> 2 或(92 / 4) = 23个寄存器
	 */
	uint32_t ioregs[HUCDEV_REG_END >> 2];

	/* 用于存放buffer的存储空间 */
	uint32_t *buf;

	uint32_t irq_status;

	QemuThread thread;
	QemuMutex mutex;
	QemuCond cond;

    bool hyperthreading_remapping;

	int listen_fd;
	int connect_fd;
} HucState;

static void huc_instance_init(Object *obj)
{
	return;
}

static uint64_t hucdev_io_read(void *opaque, hwaddr addr, unsigned size)
{
	HucState *hucdev = opaque;
	uint64_t val;
	unsigned int index;

	addr = addr & HUCDEV_REG_MASK;
	index = addr >> 2;

	if (addr >= HUCDEV_REG_END)
	{
		//printf("Unknow I/O read, addr = 0x%08x", addr);
		return 0;
	}

	switch (addr)
	{
		case HUCDEV_REG_STATUS_IRQ:
			val = hucdev->irq_status;
			break;
		default:
			val = hucdev->ioregs[index];
			break;
	}

	return val;
}

/* 触发中断并将val保存到irq status中 */
static void hucdev_raise_irq(HucState *hucdev, uint32_t irq_status)
{
	hucdev->irq_status |= irq_status;
	if (hucdev->irq_status)
		pci_set_irq(&hucdev->pdev, 1);
}

static void hucdev_lower_irq(HucState *hucdev, uint32_t val)
{
	hucdev->irq_status &= ~val;
	if (!hucdev->irq_status)
		pci_set_irq(&hucdev->pdev, 0);
}

static void hucdev_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
	HucState *hucdev = opaque;
	unsigned int index;

	addr = addr & HUCDEV_REG_MASK;
	index = addr >> 2;

	if (addr >= HUCDEV_REG_END)
	{
		//printf("Unknow I/O rite, addr = 0x%08x, 0x%08x", addr, val);
		return;
	}

	assert(index < ARRAY_SIZE(regnames));

	switch (addr)
	{
		case HUCDEV_REG_RAISE_IRQ:
			hucdev_raise_irq(hucdev, val);
			break;
		case HUCDEV_REG_LOW_IRQ:
			hucdev_lower_irq(hucdev, val);
			break;
		default:
			hucdev->ioregs[index] = val;
			break;
	}
}

static const MemoryRegionOps hucdev_io_ops = {
	.read = hucdev_io_read,
	.write = hucdev_io_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.impl = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
};

static uint64_t hucdev_bufmmio_read(void *opaque, hwaddr addr, unsigned size)
{
	HucState *hucdev = opaque;
	unsigned int index;

	addr = addr & HUCDEV_BUF_MASK;
	index = addr >> 2;

	if (addr + size > HUCDEV_BUF_SIZE * sizeof(uint32_t))
	{
		perror("Out of bounds\n");
		return 0;
	}

	switch (index)
	{
		case 0:
			return hucdev->irq_status;
		default:
			break;
	}

	return hucdev->buf[index];
}

#if 0
int map_hyperthread(cpu_set_t* set)
{
    //Modifies cpu_set only if one cpu is set in
    int i=0;
    int setCount=0;
    int settedCpu;
    int remappedCpu = -1;
    for(i=0; i<MAX_CPU; i++){
        if(CPU_ISSET_S(i, SET_SIZE, set)){
            setCount++;
            settedCpu = i;
        }
    }
    if(setCount == 1){
        CPU_ZERO_S(SET_SIZE, set);
        if(settedCpu%2 == 0){
            remappedCpu = settedCpu / 2;
        }
        else{
            remappedCpu = (get_nprocs()/2) + (settedCpu / 2);
        }
        CPU_SET_S(remappedCpu, SET_SIZE, set);

        // printf("map_hyperthread [guest] %d -> %d [host]", settedCpu, remappedCpu);
    }
    return remappedCpu;
}
#endif

static void hucdev_bufmmio_write(void *opaque, hwaddr addr, uint64_t val,
		unsigned size)
{
	HucState *hucdev = opaque;
	unsigned int index;

	addr = addr & HUCDEV_BUF_MASK;
	index = addr >> 2;

	if (addr + size > HUCDEV_BUF_SIZE * sizeof(uint32_t))
	{
		perror("Out of bounds\n");
		return;
	}

	switch (index)
	{
		case 0:
			hucdev_raise_irq(hucdev, val);
			break;
		case 1:
			hucdev_lower_irq(hucdev, val);
			break;
		case 2:
			printf("doorbell in device\n");
			struct huc_msg_header *myheader;
			myheader = (struct huc_msg_header *) hucdev->buf + 4;
			send(hucdev->connect_fd, myheader, sizeof(struct huc_msg_header), 0);
			send(hucdev->connect_fd, hucdev->buf + 4 + sizeof(struct huc_msg_header)/sizeof(uint32_t), myheader->payload_len, 0);
            hucdev->buf[index] = 0;
			break;
		case 3:
			int vCPU_count=0;
			uint64_t value = val;
			cpu_set_t *set;
			CPUState* cpu;

			set = CPU_ALLOC(MAX_CPU);
			memcpy(set, &value, SET_SIZE);

			cpu = qemu_get_cpu(vCPU_count);
			while (cpu != NULL) {
				printf("cpu #%d[%d]\tthread id:%d\n", vCPU_count, cpu->cpu_index, cpu->thread_id);
				if (CPU_ISSET_S(vCPU_count, SET_SIZE, set)) {
					int remap = vCPU_count;
					//if(hucdev->hyperthreading_remapping == true){
					//	remap = map_hyperthread(set);   //if 1 cpu is set then remap, otherwise do nothing
					//}
					if (sched_setaffinity(cpu->thread_id, SET_SIZE, set) == -1) {
						printf("error sched_setaffinity");
					}

					printf("Bind vCPU%d(thread %d) to pCPU%d\n",
						vCPU_count, cpu->thread_id, remap);
				}
				vCPU_count++;
				cpu = qemu_get_cpu(vCPU_count);
			}
			printf("#number of pCPU: %u\n", get_nprocs()); //assuming NON hotpluggable cpus
			printf("#number of vCPU: %u\n", vCPU_count);

			CPU_FREE(set);
			break;
		default:
			hucdev->buf[index] = val;
			break;
	}

	return;
}

static const MemoryRegionOps hucdev_bufmmio_ops = {
	.read = hucdev_bufmmio_read,
	.write = hucdev_bufmmio_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.impl = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
};

/* 创建tcp服务端并监听端口port */
static int make_socket(uint16_t port)
{
	int sock;
	struct sockaddr_in name;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("Create Socker");
		return -1;
	}

	/* name a socket */
	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *)&name, sizeof(name)) < 0)
	{
		perror("Bind");
		return -1;
	}

	return sock;
}

static void handle_acceptfd(void *opaque);
static void handle_listenfd(void *opaque)
{
	HucState *hucdev = opaque;

	hucdev->connect_fd = accept(hucdev->listen_fd, NULL, NULL);

	/* add connect_fd from watched fd */
	qemu_set_fd_handler(hucdev->connect_fd, handle_acceptfd, NULL, hucdev);

	/* remove listen_fd from watched fd */
	qemu_set_fd_handler(hucdev->listen_fd, NULL, NULL, NULL);

	return;
}

static void handle_acceptfd(void *opaque)
{
	HucState *hucdev = opaque;
	int len = 0;
	struct huc_msg_header *myheader;

	/*
	 * receive message header
	 * place it in hucdev->buf at offset 4 * sizeof(uint32_t)
	 */
	len = recv(hucdev->connect_fd,
			hucdev->buf + 4,
			sizeof(struct huc_msg_header),
			0);
	if (len <= 0)
	{
		/*
		 * len = 0: connection closed
		 * len < 0: error
		 */

		/* remove connect_fd from watched fd */
		qemu_set_fd_handler(hucdev->connect_fd, NULL, NULL, NULL);
		hucdev->connect_fd = -1;

		/* add listen_fd from watched fd */
		qemu_set_fd_handler(hucdev->listen_fd, handle_listenfd, NULL, hucdev);

		return;
	}

	/* 从第buf[4]开始保存header */
	myheader = (struct huc_msg_header *)hucdev->buf + 4;

	/*
	 * receive message payload
	 * place it in hucdev->buf + 4 + sizeof(struct huc_msg_header) / sizeof(uint32_t)
	 * 因为buffer是uint32_t的指针,所以要按照uint32_t对齐排放数据
	 */
	len = recv(hucdev->connect_fd,
			hucdev->buf + 4 + sizeof(struct huc_msg_header) /
			sizeof(uint32_t),
			myheader->payload_len, 0);

	/* 根据传递的消息的头部信息中的类型判断 */
	switch (myheader->type)
	{
		case PROGRAM_INJECTION:
			/*
			 * 数据内容保存在buffer中
			 * 产生中断给guest端的驱动程序
			 */
			hucdev_raise_irq(hucdev, PROGRAM_INJECTION);
			int i=0;
			CPUState* cpu = qemu_get_cpu(i);
			while(cpu != NULL){
				printf("cpu #%d[%d]\tthread id:%d\n", i, cpu->cpu_index, cpu->thread_id);
				i++;
				cpu = qemu_get_cpu(i);
			}
			printf("Guest has %d vCPUS\n", i);
			break;
#if 1
        case PROGRAM_INJECTION_RESULT:
            break;
        case PROGRAM_INJECTION_AFFINITY:
            // Injection affinity infos are stored in buf.
            {
                struct cpu_affinity_infos_t* myaffinityinfo;
                int vCPU_count=0;
                CPUState* cpu = qemu_get_cpu(vCPU_count);
                while(cpu != NULL){
                    printf("cpu #%d[%d]\tthread id:%d\n", vCPU_count, cpu->cpu_index, cpu->thread_id);
                    vCPU_count++;
                    cpu = qemu_get_cpu(vCPU_count);
                }
                printf("Guest has %d vCPUS\n", vCPU_count);
                myaffinityinfo = (struct cpu_affinity_infos_t*)(hucdev->buf + 4 + sizeof(struct huc_msg_header)/sizeof(uint32_t));
                myaffinityinfo->n_vCPU = vCPU_count;
                printf("#pCPU: %u", myaffinityinfo->n_pCPU);
                printf("#vCPU: %u", myaffinityinfo->n_vCPU);
                hucdev_raise_irq(hucdev, PROGRAM_INJECTION_AFFINITY);
            }


            break;
        case RESET:
            {
                uint64_t value = 0xFFFFFFFF;
                cpu_set_t *set;
                CPUState* cpu;
                int vCPU_count=0;

                set = CPU_ALLOC(MAX_CPU);
                memcpy(set, &value, SET_SIZE);

                cpu = qemu_get_cpu(vCPU_count);
                while(cpu != NULL){
                    printf("cpu #%d[%d]\tthread id:%d\t RESET affinity", vCPU_count, cpu->cpu_index, cpu->thread_id);
                    if (sched_setaffinity(cpu->thread_id, SET_SIZE, set) == -1){
                        printf("error sched_setaffinity");
                    }
                    vCPU_count += 1;
                    cpu = qemu_get_cpu(vCPU_count);
                }
                CPU_FREE(set);
                break;
            }
        case PIN_ON_SAME:
            {
                cpu_set_t *set;
                CPUState* cpu;
                int vCPU_count=0;
                set = CPU_ALLOC(MAX_CPU);
                CPU_SET_S(0, SET_SIZE, set);    //static pin on pCPU0

                cpu = qemu_get_cpu(vCPU_count);
                while(cpu != NULL){
                    printf("cpu #%d[%d]\tthread id:%d\t PIN_ON_SAME [pcpu#%d]", vCPU_count, cpu->cpu_index, cpu->thread_id, 0);
                    if (sched_setaffinity(cpu->thread_id, SET_SIZE, set) == -1){
                        printf("error sched_setaffinity");
                    }
                    vCPU_count += 1;
                    cpu = qemu_get_cpu(vCPU_count);
                }
                CPU_FREE(set);
                break;
            }
        case HT_REMAPPING:
            {
                hucdev->hyperthreading_remapping = !hucdev->hyperthreading_remapping;
                printf("HT_REMAPPING: %d", hucdev->hyperthreading_remapping);
            }
#endif
		default:
			break;
	}

	return;
}

static void huc_realize(PCIDevice *pdev, Error **errp)
{
	HucState *hucdev = HUC_DEV(pdev);
	uint8_t *pci_conf = pdev->config;

	/* 设置中断管脚 */
	pci_config_set_interrupt_pin(pci_conf, 1);

	/* 配置msi方式中断 */
	if (msi_init(pdev, 0, 1, true, false, errp))
		return;

	qemu_mutex_init(&hucdev->mutex);
	qemu_cond_init(&hucdev->cond);

	/* Init I/O mapped memory region, exposing registers */
	memory_region_init_io(&hucdev->regs,
			OBJECT(hucdev),
			&hucdev_io_ops,
			hucdev,
			"hucdev-regs",
			HUCDEV_REG_MASK + 1);
	pci_register_bar(pdev,
			HUCDEV_REG_PCI_BAR,
			PCI_BASE_ADDRESS_SPACE_MEMORY,
			&hucdev->regs);

	/* Init memory mapped memory region, to expose eBPF progrom */
	memory_region_init_io(&hucdev->mmio,
			OBJECT(hucdev),
			&hucdev_bufmmio_ops,
			hucdev,
			"hucdev-buf",
			HUCDEV_BUF_SIZE * sizeof(uint32_t));
	pci_register_bar(pdev,
			HUCDEV_BUF_PCI_BAR,
			PCI_BASE_ADDRESS_SPACE_MEMORY,
			&hucdev->mmio);
	hucdev->buf = malloc(HUCDEV_BUF_SIZE * sizeof(uint32_t));
	if (!hucdev->buf)
	{
		perror("No memory");
		return;
	}

    /* setup ht (default=disabled) */
    hucdev->hyperthreading_remapping = false;

	hucdev->listen_fd = -1;
	hucdev->connect_fd = -1;
	hucdev->listen_fd = make_socket(8888);
	if (hucdev->listen_fd < 0)
		return;

	if (listen(hucdev->listen_fd, 1) < 0)
	{
		perror("Listen error\n");
		return;
	}

	qemu_set_fd_handler(hucdev->listen_fd, handle_listenfd, NULL, hucdev);
}

static void hucdev_uninit(PCIDevice *pdev)
{
	HucState *hucdev = HUC_DEV(pdev);

	msi_uninit(pdev);

	if (hucdev->listen_fd != -1)
	{
		qemu_set_fd_handler(hucdev->listen_fd, NULL, NULL, NULL);
		qemu_close(hucdev->listen_fd);
	}
}

static void huc_class_init(ObjectClass *class, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(class);
	PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

	k->realize = huc_realize;
	k->exit = hucdev_uninit;
	k->vendor_id = PCI_VENDOR_ID_QEMU;
	k->device_id = HUC_DEVICE_ID;
	k->class_id = PCI_CLASS_OTHERS;

	/* 设置设备的分类 */
	set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void huc_register_types(void)
{
	static InterfaceInfo interfaces[] = {
		{ INTERFACE_CONVENTIONAL_PCI_DEVICE },
		{ },
	};

	static const TypeInfo huc_info = {
		.name = TYPE_HUC_DEVICE,
		.parent = TYPE_PCI_DEVICE,
		.instance_size = sizeof(HucState),
		.instance_init = huc_instance_init,
		.class_init = huc_class_init,
		.interfaces = interfaces,
	};

	type_register_static(&huc_info);
}
type_init(huc_register_types)
