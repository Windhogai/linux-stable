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
#include <linux/wait.h>

#define CHARDEV_NAME_LEN 20
#define FIFO_SIZE 8
#define IRQ_NUMBER 40

static const int NO_OF_REGS = 3;
static const char CHARDEV_NAME[] = "pushbuttonTest";

struct pushbuttonRegisters_t {
	uint32_t *dataRegister;
	uint32_t *unused;
	uint32_t *intrptMaskRegister;
	uint32_t *edgeCaptureRegister;
};

// miscdevice
struct pushbutton_t {
	struct pushbuttonRegisters_t pushbutton;
	struct miscdevice misc;
	struct platform_device *pdev;
	struct kfifo fifo;
	struct mutex mutex;
	spinlock_t lock;
	wait_queue_head_t waitQueue;
};

static irqreturn_t pushbutton_irq_handler(int irq, void *data)
{
	struct pushbutton_t *privateData = (struct pushbutton_t *)data;
	uint32_t edgeRegisterContent;

	// checking interrupt number
	if (irq != IRQ_NUMBER) {
		// not adressed to this handler
		return IRQ_NONE;
	}
	edgeRegisterContent
		= ioread32(privateData->pushbutton.edgeCaptureRegister);
	if (!kfifo_is_full(&privateData->fifo)) {
		kfifo_in_spinlocked(&privateData->fifo, &edgeRegisterContent,
		1, &privateData->lock);
	} else
		dev_warn(&privateData->pdev->dev, "kfifo is full\n");
	// inform wait queue in read operation
	wake_up_interruptible(&privateData->waitQueue);
	// reset interrupt
	iowrite32(0xf, privateData->pushbutton.edgeCaptureRegister);
	return IRQ_HANDLED;
}

static ssize_t pushbutton_open(struct inode *inode, struct file *filep)
{
	struct pushbutton_t *privateData;

	privateData = container_of(filep->private_data, struct pushbutton_t,
		misc);
	if (mutex_lock_interruptible(&privateData->mutex))
		dev_warn(&privateData->pdev->dev, "pushbutton open() interrupted\n");
	return 0;
}

static ssize_t pushbutton_release(struct inode *inode, struct file *filep)
{
	struct pushbutton_t *privateData;

	privateData = container_of(filep->private_data,
		struct pushbutton_t, misc);

	mutex_unlock(&privateData->mutex);
	return 0;
}

static ssize_t pushbutton_read(struct file *filep, char __user *buf,
	size_t count, loff_t *offp)
{
	unsigned long flags;
	unsigned int copiedBytesFifo;
	unsigned int copiedBytesBuffer;
	unsigned char kBuffer[FIFO_SIZE];
	struct pushbutton_t *privateData;

	privateData = container_of(filep->private_data,
		struct pushbutton_t, misc);
	// lock fifo for empty check
	spin_lock_irqsave(&privateData->lock, flags);
	if (*offp > 0 && kfifo_is_empty(&privateData->fifo)) {
		// if offp > 0 -> read was called prior
		// && fifo is empty -> all data already read
		// => End of File
		spin_unlock_irqrestore(&privateData->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&privateData->lock, flags);

	// small buffers
	if (count < FIFO_SIZE)
		return -ETOOSMALL;

	// wait if kfifo is empty
	if (wait_event_interruptible(privateData->waitQueue,
		kfifo_is_empty(&privateData->fifo) == 0)) {
	}
	// thread safe copy to kernel buffer
	copiedBytesFifo = kfifo_out_spinlocked(&privateData->fifo, kBuffer,
		FIFO_SIZE, &privateData->lock);
	// copy to user space
	copiedBytesBuffer = copy_to_user(buf, kBuffer, copiedBytesFifo);
	if (copiedBytesBuffer) {
		dev_err(&privateData->pdev->dev, "Error: copy_to_user\n");
		*offp += copiedBytesFifo - copiedBytesBuffer;
		return (copiedBytesFifo - copiedBytesBuffer);
	}
	*offp += copiedBytesFifo;
	return copiedBytesFifo;
}

static const struct file_operations fops = {
	.open = pushbutton_open,
	.read = pushbutton_read,
	.release = pushbutton_release,
};

static int pushbutton_probe(struct platform_device *pdev)
{
	int status;
	struct resource *resources[3];
	struct pushbutton_t *privateData;
	static atomic_t pushbutton_no = ATOMIC_INIT(-1);
	char charDevName[CHARDEV_NAME_LEN];
	int irq;
	int index;
	int no = atomic_inc_return(&pushbutton_no);

	// alloc resources
	privateData = devm_kzalloc(&pdev->dev, sizeof(*privateData),
						GFP_KERNEL);
	if (privateData == NULL) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Error in kzalloc");
		goto exit;
	}
	// initialize char device access mutex und spin lock
	mutex_init(&privateData->mutex);
	spin_lock_init(&privateData->lock);
	init_waitqueue_head(&privateData->waitQueue);

	// allocate fifo
	if (kfifo_alloc(&privateData->fifo, FIFO_SIZE, GFP_KERNEL)) {
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

	status = devm_request_irq(&pdev->dev, irq, pushbutton_irq_handler,
	0, dev_name(&pdev->dev), privateData);
	if (status < 0) {
		dev_err(&pdev->dev, "Error in devm_request_irq");
		goto free_kfifo;
	}

	platform_set_drvdata(pdev, privateData);

	// data register
	resources[0] = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	// interrupt mask register
	resources[1] = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	// edge capture register
	resources[2] = platform_get_resource(pdev, IORESOURCE_MEM, 3);

	for (index = 0; index < NO_OF_REGS; index++) {
		if (resources[index] == NULL) {
			status = -EFAULT;
			dev_err(&pdev->dev, "Error in platform_get_resource");
			goto reset_drvdata;
		}
	}

	privateData->pushbutton.dataRegister =
		devm_ioremap_resource(&pdev->dev, resources[0]);
	privateData->pushbutton.intrptMaskRegister =
		devm_ioremap_resource(&pdev->dev, resources[1]);
	privateData->pushbutton.edgeCaptureRegister =
		devm_ioremap_resource(&pdev->dev, resources[2]);

	if (privateData->pushbutton.dataRegister == NULL ||
		privateData->pushbutton.intrptMaskRegister == NULL ||
		privateData->pushbutton.edgeCaptureRegister == NULL) {
		status = -EFAULT;
		dev_err(&pdev->dev, "Error in ioremap");
		goto reset_drvdata;
	}

	// set device in private data for (used for error msg)
	privateData->pdev = pdev;

	// register character device
	snprintf(charDevName, CHARDEV_NAME_LEN, "pushbutton%d", no);
	privateData->misc.name = charDevName;
	privateData->misc.minor = MISC_DYNAMIC_MINOR;
	privateData->misc.fops = &fops;
	privateData->misc.parent = &pdev->dev;
	status = misc_register(&privateData->misc);
	if (status != 0) {
		dev_err(&pdev->dev, "Error in misc_registers");
		goto reset_drvdata;
	}

	// activate all buttons interrupt mask
	iowrite32(0xf, privateData->pushbutton.intrptMaskRegister);
	// reset edge capture register
	iowrite32(0xf, privateData->pushbutton.edgeCaptureRegister);

	// everything okay
	return 0;

reset_drvdata:
	platform_set_drvdata(pdev, NULL);
free_kfifo:
	kfifo_free(&privateData->fifo);
exit:
	return status;
}

static int pushbutton_remove(struct platform_device *pdev)
{
	struct pushbutton_t *privateData;

	privateData = platform_get_drvdata(pdev);
	// deactivate all buttons interrupt mask
	iowrite32(0x0, privateData->pushbutton.intrptMaskRegister);
	// reset edge capture register
	iowrite32(0xf, privateData->pushbutton.edgeCaptureRegister);

	misc_deregister(&privateData->misc);
	platform_set_drvdata(pdev, NULL);
	kfifo_free(&privateData->fifo);
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
		.name = "PushButtonDrv",
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
