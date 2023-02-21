#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

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

static loff_t huc_llseek(struct file *filp, loff_t off, int whence)
{
	pr_info("llseek\n");

	return 0;
}

static ssize_t huc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	pr_info("read\n");
	return 0;
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
	.llseek  = huc_llseek,
	.read    = huc_read,
	.write   = huc_write,
	.unlocked_ioctl = huc_ioctl,
};

static void __iomem *bufmmio;

static irqreturn_t irq_handler(int irq, void *dev)
{
	int n;
	irqreturn_t ret;
	u32 irq_status;

	/* 比对下是否为该设备的中断 */
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
	major = register_chrdev(0, HUCDEV_NAME, &fops);
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

	printk("aaa:pdev-irq = %d\n", pdev->irq);
	pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, (u8 *)&(pdev->irq));
	printk("bbb:pdev-irq = %d\n", pdev->irq);
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
	pr_info("remove\n");

	pci_release_selected_regions(pdev, HUCDEV_BUF_PCI_BAR);

	iounmap(bufmmio);
	//free_irq(pdev->irq, NULL);
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
