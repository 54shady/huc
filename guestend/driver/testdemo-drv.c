#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#define TESTDEMO_NAME "testdemo"
#define TESTDEMO_DEVICE_ID 0x5679
#define QEMU_VENDOR_ID 0x1234

#define TESTDEMO_BUFRW_PCI_BAR 2

static int major;
static void __iomem *bufmmio;

static ssize_t testdemo_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
#ifndef COPY_BYTE_TO_BYTE
	copy_to_user(buf, bufmmio, len);

	return len;
#else
	ssize_t ret;
	u32 kbuf;
	u8 *p = (u8 *)&kbuf;
	int left = len;

	while (left >=0 )
	{
		kbuf = ioread32(bufmmio + *off);
		left -= 4;
		//printk("len = %ld, offset = %d, \n", len, *off);
		printk("%c%c%c%c\n", p[0], p[1], p[2], p[3]);

		if (copy_to_user(buf + *off, (void *)&kbuf, 4))
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
#endif
}

static ssize_t testdemo_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
#ifndef COPY_BYTE_TO_BYTE
	copy_from_user(bufmmio, buf, len);

	return len;
#else
	int left = len;
	u32 kbuf;
	u8 *p = (u8 *)&kbuf;

	while (left >= 0)
	{
		copy_from_user((void *)&kbuf, buf + *off, 4);
		printk("%c%c%c%c\n", p[0], p[1], p[2], p[3]);
		iowrite32(kbuf, bufmmio + *off);
		left -= 4;
		*off += 4;
	}

	return len;
#endif
}

static long testdemo_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_info("ioctl\n");
	return 0;
}

static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.llseek  = generic_file_llseek,
	.read    = testdemo_read,
	.write   = testdemo_write,
	.unlocked_ioctl = testdemo_ioctl,
};

static int testdemo_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	//unsigned long adr;
	//unsigned int busadr;

	major = register_chrdev(0, TESTDEMO_NAME, &fops);
	if (pci_enable_device(pdev) < 0)
	{
		dev_err(&(pdev->dev), "enable pci device error\n");
		goto error;
	}

	if (pci_request_region(pdev, TESTDEMO_BUFRW_PCI_BAR, "bufregion"))
	{
		dev_err(&(pdev->dev), "request buffer region error\n");
		goto error;
	}

	/*
	 * 这里可以手动进行映射物理地址
	 * 在qemu monitor里查询设备的物理地址,和大小
	 * (qemu) info pci
	 * Bus  0, device   5, function 0:
	 * Class 0255: PCI device 1234:5679
	 *  PCI subsystem 1af4:1100
	 *  BAR2: 32 bit memory at 0xfebb0000 [0xfebbffff].
	 *
	 * 直接使用ioremap映射
	 * bufmmio = ioremap(0xfebb0000, 0xffff);
	 */
	bufmmio = pci_iomap(pdev,
			TESTDEMO_BUFRW_PCI_BAR,
			pci_resource_len(pdev, TESTDEMO_BUFRW_PCI_BAR));
	if (!bufmmio)
	{
		printk("%s, %d\n", "error", __LINE__);
		goto error;
	}

	//adr = virt_to_phys(bufmmio);
	//busadr = virt_to_bus(bufmmio);
	//printk("bufmmio = %x, adr = %lx, bus=%x\n", bufmmio, adr, busadr);

	pr_info("pci_probe COMPLETED SUCCESSFULLY\n");

	return 0;
error:
	return 1;
}

static void testdemo_pci_remove(struct pci_dev *pdev)
{
	pr_info("remove\n");

	pci_release_region(pdev, TESTDEMO_BUFRW_PCI_BAR);
	iounmap(bufmmio);
	unregister_chrdev(major, TESTDEMO_NAME);

	return;
}

static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(QEMU_VENDOR_ID, TESTDEMO_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_ids);
static struct pci_driver testdemo_pci_driver = {
	.name     = TESTDEMO_NAME,
	.id_table = pci_ids,
	.probe    = testdemo_probe,
	.remove   = testdemo_pci_remove,
};

static int testdemo_init(void)
{
	if (pci_register_driver(&testdemo_pci_driver) < 0) {
		return 1;
	}

	return 0;
}

static void testdemo_exit(void)
{
	pci_unregister_driver(&testdemo_pci_driver);
}

module_init(testdemo_init);
module_exit(testdemo_exit);

MODULE_LICENSE("GPL");
