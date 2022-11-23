// SPDX-License-Identifier: GPL-2.0-or-later
/* LDD5 Uebung04
 *
 * Bauernfeind, Schmalzer
 * pushbutton.c 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/errno.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>

static int FIFO_SIZE = 8;

struct pushbuttonRegister_t {
	uint32_t *data;
	uint32_t *unused;
	uint32_t *interruptMask;
	uint32_t *edgeCaputre;
};

// miscdevice
struct pushbutton_t {
	struct pushbuttonRegister_t reg;
	struct miscdevice misc;
	struct platform_device *pdev;
	struct kfifo fifo;
	struct mutex mutex;
	spinlock_t lock;
};

static irqreturn_t pushbutton_irq_handler(int irq, void *data) {
	//TODO check interrupt number
	struct pushbutton_t *privateData = (struct pushbutton_t *)data;
	uint32_t dataRegisterContent;
	
	dev_info(&privateData->pdev->dev, "in interrupt\n");

	dataRegisterContent = ioread32(privateData->reg.data);
	if(!kfifo_is_full(&privateData->fifo))
		kfifo_in_spinlocked(&privateData->fifo, &dataRegisterContent, 
		1, &privateData->lock);
	else
		dev_warn(&privateData->pdev->dev, "kfifo is full\n");
	return IRQ_HANDLED;
}

static ssize_t pushbutton_open(struct inode *inode, struct file *filep) {
	struct pushbutton_t *privateData;
	privateData = container_of(filep->private_data, struct pushbutton_t, misc);
	if (mutex_lock_interruptible(&privateData->mutex)) {
		dev_warn(&privateData->pdev->dev, "pushbutton open() interrupted\n");
	}
	return 0;
}

static ssize_t pushbutton_release(struct inode *inode, struct file *filep) {
	struct pushbutton_t *privateData;
	privateData = container_of(filep->private_data, struct pushbutton_t, misc);
	mutex_unlock(&privateData->mutex);
	return 0;
}

static ssize_t pushbutton_read(struct file *filep, char __user *buf,
	size_t count, loff_t *offp)
{
	unsigned long flags;
	unsigned int copiedBytes;
	struct pushbutton_t *pushbutton;
	pushbutton = container_of(filep->private_data, struct pushbutton_t, misc);

	dev_info(&pushbutton->pdev->dev, "in read\n");

	// end of file
	if (*offp >= 1)
		return 0;

	// small buffers
	if (count < FIFO_SIZE)
		return -ETOOSMALL;

	// wait for at least one button press
	// while(kfifo_is_empty(&pushbutton->fifo));

	spin_lock_irqsave(&pushbutton->lock, flags);
	// if(!kfifo_out_spinlocked(&pushbutton->fifo, buf, FIFO_SIZE, &pushbutton->fifo))
	if(kfifo_to_user(&pushbutton->fifo, buf, FIFO_SIZE, &copiedBytes)) {
		dev_warn(&pushbutton->pdev->dev, "Error in kfifo_to_user\n");
	}
	else {
		*offp += (loff_t) copiedBytes;
		dev_info(&pushbutton->pdev->dev, "copied %d bytes\n", copiedBytes);
	}
	// delete content of reset
	kfifo_reset(&pushbutton->fifo);
	spin_unlock_irqrestore(&pushbutton->lock, flags);
	*offp = 1;
	return 1;
}

static const struct file_operations fops = {
	.open = pushbutton_open,
	.read = pushbutton_read,
	.release = pushbutton_release,
};

static int pushbutton_probe(struct platform_device *pdev)
{
	int status;
	struct resource *io = 0x1;
	struct resource * ressourceArray[5];
	struct pushbutton_t *pushbutton;
	static atomic_t pushbutton_no = ATOMIC_INIT(-1);
	char buf[15];
	int irq;
	int i;
	int no = atomic_inc_return(&pushbutton_no);

	// alloc ressources
	pushbutton = devm_kzalloc(&pdev->dev, sizeof(*pushbutton),
						GFP_KERNEL);
	if (pushbutton == NULL) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Error in kzalloc");
		goto exit;
	}		
	// initialize char device access mutex und spin lock
	mutex_init(&pushbutton->mutex);
	spin_lock_init(&pushbutton->lock);

	// allocate fifo
	if (kfifo_alloc(&pushbutton->fifo, FIFO_SIZE, GFP_KERNEL)) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Error in kfifo_alloc");
		goto exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Error in platform_get_irq");
		status =  -EFAULT;
		goto free_kfifo;
	}
	dev_info(&pdev->dev, "interrupt irq %i", irq);

	status = devm_request_irq(&pdev->dev,irq, pushbutton_irq_handler,
	0, dev_name(&pdev->dev), pushbutton);
	if(status < 0) {
		dev_err(&pdev->dev, "Error in devm_request_irq");
		goto free_kfifo;
	}

	platform_set_drvdata(pdev, pushbutton);

	dev_info(&pdev->dev, "num resources: %i", pdev->num_resources);
	
	for(i = 0; i < pdev->num_resources; i++) {
		dev_info(&pdev->dev, "resource %i: %u", i, pdev->resource[i]);
	}

	i = 0;
	while (io != NULL)
	{
		ressourceArray[i] = platform_get_resource(pdev, IORESOURCE_MEM, i);
		i++;
	}

	/*
	if (io == NULL) {
		status = -EFAULT;
		goto reset_drvdata;
	}
	*/
	pushbutton->reg.data = 
		devm_ioremap_resource(&pdev->dev, ressourceArray[0]);
	pushbutton->reg.unused = 
		devm_ioremap_resource(&pdev->dev, ressourceArray[1]);
	pushbutton->reg.interruptMask = 
		devm_ioremap_resource(&pdev->dev, ressourceArray[2]);
	pushbutton->reg.edgeCaputre = 
		devm_ioremap_resource(&pdev->dev, ressourceArray[3]);

	if (pushbutton->reg.data == NULL) {
		status = -EFAULT;
		dev_err(&pdev->dev, "Error in ioremap");
		goto reset_drvdata;
	}
	
	iowrite32(0xffffffff, pushbutton->reg.interruptMask);

	pushbutton->pdev = pdev;

	snprintf(buf, 15, "pushbutton%d", no);
	pushbutton->misc.name = buf;
	pushbutton->misc.minor = MISC_DYNAMIC_MINOR;
	pushbutton->misc.fops = &fops;
	pushbutton->misc.parent = &pdev->dev;
	status = misc_register(&pushbutton->misc);
	if (status != 0)
	{
		dev_err(&pdev->dev, "Error in misc_registers");
		goto reset_drvdata;
	}

	// everything okay
	return 0;

reset_drvdata:
	platform_set_drvdata(pdev, NULL);
free_kfifo:
	kfifo_free(&pushbutton->fifo);
exit:
	return status;
}

static int pushbutton_remove(struct platform_device *pdev)
{
	struct pushbutton_t *pushbutton;
	pushbutton = platform_get_drvdata(pdev);
	misc_deregister(&pushbutton->misc);
	platform_set_drvdata(pdev, NULL);
	kfifo_free(&pushbutton->fifo);
	return 0;
}

//struct  device_id
static const struct of_device_id
	pushbutton_of_match[] = {
	{ .compatible = "ldd,pushbutton", },
	{ },
};
MODULE_DEVICE_TABLE(of, pushbutton_of_match);

//struct led driver
static struct platform_driver pushbutton_driver = {
	.driver = {
		.name = "PUSHBUTTON",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pushbutton_of_match)
	},
	.probe = pushbutton_probe,
	.remove = pushbutton_remove
};
module_platform_driver(pushbutton_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("pushbutton driver using device tree and platform devices");
MODULE_AUTHOR("Lukas Schmalzer <lukas.schmalzer@gmail.com>");
MODULE_AUTHOR("Maximilian Bauernfeind <s2020306047@fhooe.at>");
