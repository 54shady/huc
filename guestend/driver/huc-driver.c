#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>

#include "bpf_injection_msg.h"

#define HUCDEV_NAME "hucdev"
#define HUC_DEVICE_ID 0x11eb
#define QEMU_VENDOR_ID 0x1234

#define HUCDEV_REG_PCI_BAR 0
#define HUCDEV_BUF_PCI_BAR 1

#define HUCDEV_REG_STATUS_IRQ   0
#define HUCDEV_REG_LOWER_IRQ    4
#define HUCDEV_REG_RAISE_IRQ    8
#define HUCDEV_REG_DOORBELL		8
#define HUCDEV_REG_SETAFFINITY	12

static int major;
static struct class *cls;
static struct cdev huc_cdev;

#if 0
static loff_t huc_llseek(struct file *filp, loff_t off, int whence)
{
	pr_info("llseek\n");

	return 0;
}
#endif
/*
 * flag = 1: read payload
 * flag = 2: read header
 */
static int flag = 0;
static DECLARE_WAIT_QUEUE_HEAD(wq);
static int payload_left;
static void __iomem *bufmmio;

static ssize_t huc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	ssize_t ret;
	/* 因为pci的buf是按u32来存储的 */
	u32 kbuf;
	struct bpf_injection_msg_header myheader;

	/* 没有可读取的数据时睡眠在这 */
	wait_event_interruptible(wq, flag >=1);

	printk("%s, %d\n", __FUNCTION__, __LINE__);
	/*
	 * 1. 读取的偏移量要按4字节对齐,否则返回
	 * 2. 读取的长度是0也返回
	 */
	if (*off % 4 || len == 0)
	{
		ret = 0;
	}
	else
	{
		/* 从偏离地址读取4个字节 */
		kbuf = ioread32(bufmmio + *off);

		/* 读取头部信息 */
		if (flag == 2)
		{
			memcpy(&myheader, &kbuf, sizeof(kbuf));
			pr_info("  Version:%u\n  Type:%u\n  Payload_len:%u\n", myheader.version, myheader.type, myheader.payload_len);

			/* 保存下payload的长度并跳转到读取payload的阶段 */
			payload_left = myheader.payload_len;
			flag = 1;
		}
		else if (flag == 1)
		{
			/*
			 * 每次更新下剩余的payload的长度
			 * 因为guest应用每次读取len长度payload
			 */
			payload_left -= len;

			if (payload_left <= 0)
				flag = 0; /* 读取结束 */
		}

		/*
		 * 将每次读取的数据返回到用户空间
		 * 所以这里建议用户空间每次按4字节读取
		 */
		if (copy_to_user(buf, (void *)&kbuf, 4))
		{
			ret = -EFAULT;
		}
		else
		{
			ret = len;
			*off += 4;
		}
	}

	return ret;
}

static ssize_t huc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("write\n");
	return 0;
}

static long huc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_info("ioctl\n");
	return 0;
}

/* These fops are a bit daft since read and write interfaces don't map well to IO registers.
 * One ioctl per register would likely be the saner option. But we are lazy.
 * We use the fact that every IO is aligned to 4 bytes. Misaligned reads means EOF. */
static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.llseek  = generic_file_llseek,
	.read    = huc_read,
	.write   = huc_write,
	.unlocked_ioctl = huc_ioctl,
};

static irqreturn_t irq_handler(int irq, void *dev)
{
	int n;
	irqreturn_t ret;
	u32 irq_status;

	/*
	 * 比对下是否为该设备的中断
	 * 因为中断配置为共享中断
	 * grep huc_irq_handler /proc/interrupts
	 */
	n = *(int *)dev;
	if (n == major)
	{
		irq_status = ioread32(bufmmio + HUCDEV_REG_STATUS_IRQ);
		pr_debug("interrupt irq = %d dev = %d irq_status = 0x%llx\n",
				irq, n, (unsigned long long)irq_status);

		switch (irq_status)
		{
			case PROGRAM_INJECTION:
				pr_info("PROGRAM_INJECTION irq handler\n");
				flag = 2;

				/*
				 * 应用程序read的时候会休眠在等待队列上等待数据到来
				 * 这里唤醒调用read的应用程序
				 * 通知其数据到来接收数据
				 */
				wake_up_interruptible(&wq);
				break;
		}
		/* Must do this ACK, or else the interrupts just keeps firing. */
		iowrite32(irq_status, bufmmio + HUCDEV_REG_LOWER_IRQ);
		ret = IRQ_HANDLED;
	}
	else
	{
		ret = IRQ_NONE;
	}

	return ret;
}

static int huc_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	dev_t dev_id;

	/* mknode automatically */
	alloc_chrdev_region(&dev_id, 0, 1, "hucdev");
	major = MAJOR(dev_id);
	cdev_init(&huc_cdev, &fops);
	cdev_add(&huc_cdev, dev_id, 1);
	cls = class_create(THIS_MODULE, "hucdev");
	if (IS_ERR(cls))
		return -EINVAL;

	device_create(cls, NULL, MKDEV(major, 0), NULL, "hucdev"); /* /dev/testdemo */

	if (pci_enable_device(pdev) < 0)
	{
		dev_err(&(pdev->dev), "enable pci device error\n");
		goto error;
	}

	if (pci_request_region(pdev, HUCDEV_BUF_PCI_BAR, "bufregion"))
	{
		dev_err(&(pdev->dev), "request buffer region error\n");
		goto error;
	}
	bufmmio = pci_iomap(pdev,
			HUCDEV_BUF_PCI_BAR,
			pci_resource_len(pdev, HUCDEV_BUF_PCI_BAR));
	if (!bufmmio)
	{
		printk("%s, %d\n", "error", __LINE__);
		goto error;
	}

	/*
	 * no need to get irq number manually
	 * pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, (u8 *)&(pdev->irq));
	 */
	if (devm_request_irq(&pdev->dev, pdev->irq, irq_handler, IRQF_SHARED, "huc_irq_handler", &major) < 0)
	{
		dev_err(&(pdev->dev), "request_irq\n");
		goto error;
	}

	pr_info("pci_probe COMPLETED SUCCESSFULLY\n");

	return 0;
error:
	return 1;
}

static void huc_pci_remove(struct pci_dev *pdev)
{
	dev_t dev_id;

	pr_info("remove\n");
	dev_id = MKDEV(major, 0);
	device_destroy(cls, dev_id);
	class_destroy(cls);
	cdev_del(&huc_cdev);
	unregister_chrdev_region(dev_id, 1);

	pci_release_region(pdev, HUCDEV_BUF_PCI_BAR);
	iounmap(bufmmio);
	unregister_chrdev(major, HUCDEV_NAME);

	return;
}

static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(QEMU_VENDOR_ID, HUC_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_ids);
static struct pci_driver huc_pci_driver = {
	.name     = HUCDEV_NAME,
	.id_table = pci_ids,
	.probe    = huc_probe,
	.remove   = huc_pci_remove,
};

static int huc_init(void)
{
	if (pci_register_driver(&huc_pci_driver) < 0) {
		return 1;
	}

	return 0;
}

static void huc_exit(void)
{
	pci_unregister_driver(&huc_pci_driver);
}

module_init(huc_init);
module_exit(huc_exit);

MODULE_LICENSE("GPL");
