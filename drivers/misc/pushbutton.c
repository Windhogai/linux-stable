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


static const uint32_t maxLedIntensity = 2047;

// miscdevice
struct pushbutton_t {
	uint32_t *registers;
	struct miscdevice misc;
	struct platform_device *pdev;
};

static ssize_t pushbutton_read(struct file *filep, char __user *buf,
	size_t count, loff_t *offp)
{

	unsigned long ret;
	struct pushbutton_t *pushbutton;
	
	pushbutton = container_of(filep->private_data, struct pushbutton_t, misc);

	dev_info(&pushbutton->pdev->dev, "in read\n");

	if (buf == NULL) {
		dev_err(&pushbutton->pdev->dev, "Invalid buffer\n");
		return -EINVAL;
	}

	// end of file
	if (*offp >= 1)
		return 0;

	// small buffers
	if (count < 1)
		return -ETOOSMALL;

	ledIntensity = ioread32(pushbutton->registers);
	ledIntPercent = (uint8_t)(100 * ledIntensity / maxLedIntensity);
	ret = copy_to_user(buf, &ledIntPercent, 1);
	if (ret) {
		dev_err(&pushbutton->pdev->dev, "Invalid input data\n");
		return ret;
	}
	*offp += 1;
	return 1;
}

static const struct file_operations fops = {
	.read = ledPwm_read,
};

static int ledpwm_probe(struct platform_device *pdev)
{
	int status;
	struct resource *io;
	struct pushbutton_t *pushbutton;
	static atomic_t ledpwm_no = ATOMIC_INIT(-1);
	char buf[10];
	int no = atomic_inc_return(&ledpwm_no);

	// alloc ressources
	pushbutton = devm_kzalloc(&pdev->dev, sizeof(*pushbutton),
						GFP_KERNEL);
	if (pushbutton == NULL) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Error in kzalloc");
		goto exit;
	}									
	platform_set_drvdata(pdev, pushbutton);
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io == NULL) {
		status = -EFAULT;
		goto reset_drvdata;
	}

	pushbutton->registers = devm_ioremap_resource(&pdev->dev, io);
	if (pushbutton->registers == NULL) {
		status = -EFAULT;
		dev_err(&pdev->dev, "Error in ioremap");
		goto reset_drvdata;
	}

	pushbutton->pdev = pdev;

	snprintf(buf, 10, "pushbutton%d", no);
	pushbutton->misc.name = buf;
	pushbutton->misc.minor = MISC_DYNAMIC_MINOR;
	pushbutton->misc.fops = &fops;
	pushbutton->misc.parent = &pdev->dev;
	status = misc_register(&pushbutton->misc);

	// turn off all leds
	iowrite32(0x0, pushbutton->registers);

	if (status != 0)
	{
		dev_err(&pdev->dev, "Error in misc_registers");
		goto reset_drvdata;
	}

	// everything okay
	return 0;

reset_drvdata:
	pushbutton = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);
exit:
	return status;
}

static int ledpwm_remove(struct platform_device *pdev)
{
	struct pushbutton_t *pushbutton;

	pushbutton = platform_get_drvdata(pdev);

	// turn off all leds
	iowrite32(0x0, pushbutton->registers);
	
	misc_deregister(&pushbutton->misc);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

//struct  device_id
static const struct of_device_id
	ledpwm_of_match[] = {
	{ .compatible = "ldd,pushbutton", },
	{ },
};
MODULE_DEVICE_TABLE(of, ledpwm_of_match);

//struct led driver
static struct platform_driver ledpwm_driver = {
	.driver = {
		.name = "LEDPWM",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ledpwm_of_match)
	},
	.probe = ledpwm_probe,
	.remove = ledpwm_remove
};
module_platform_driver(ledpwm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("device tree and platform devices");
MODULE_AUTHOR("Lukas Schmalzer <lukas.schmalzer@gmail.com>");
MODULE_AUTHOR("Maximilian Bauernfeind <s2020306047@fhooe.at>");
