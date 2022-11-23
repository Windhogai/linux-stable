// SPDX-License-Identifier: GPL-2.0-or-later
/* LDD5 Uebung03
 *
 * Bauernfeind, Schmalzer
 * led pwm, device tree and platform deivce 
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
struct ledpwm_t {
	uint32_t *registers;
	struct miscdevice misc;
	struct platform_device *pdev;
};

static ssize_t ledPwm_read(struct file *filep, char __user *buf,
	size_t count, loff_t *offp)
{

	unsigned long ret;
	struct ledpwm_t *ledpwm;
	unsigned int ledIntensity;
	uint8_t ledIntPercent;

	ledpwm = container_of(filep->private_data, struct ledpwm_t, misc);

	dev_info(&ledpwm->pdev->dev, "in read\n");

	if (buf == NULL) {
		dev_err(&ledpwm->pdev->dev, "Invalid buffer\n");
		return -EINVAL;
	}

	// end of file
	if (*offp >= 1)
		return 0;

	// small buffers
	if (count < 1)
		return -ETOOSMALL;

	ledIntensity = ioread32(ledpwm->registers);
	ledIntPercent = (uint8_t)(100 * ledIntensity / maxLedIntensity);
	ret = copy_to_user(buf, &ledIntPercent, 1);
	if (ret) {
		dev_err(&ledpwm->pdev->dev, "Invalid input data\n");
		return ret;
	}
	*offp += 1;
	return 1;
}

static ssize_t ledPwm_write(struct file *filep, const char __user *buf,
	size_t count, loff_t *offp)
{
	unsigned long ret;
	int i;
	uint32_t res;
	uint8_t kBuf[100];
	struct ledpwm_t *ledpwm;
	
	ledpwm = container_of(filep->private_data, struct ledpwm_t, misc);
	
	if (count > 100) {
		dev_err(&ledpwm->pdev->dev, "Input overflow\n");
		return -EINVAL;
	}
	if (buf == NULL) {
		dev_err(&ledpwm->pdev->dev, "Invalid buffer\n");
		return -EINVAL;
	}

	ret = copy_from_user(kBuf, buf, count);
	if (ret) {
		dev_err(&ledpwm->pdev->dev, "Invalid input data\n");
		return ret;
	}
	
	for (i = 0; i < count; i++) {
		// discard input values > 100
		if (kBuf[i] <= 100) {
			res = (uint32_t)((kBuf[i]) * maxLedIntensity / 100);
			if (kBuf[i] == 100)
				res = maxLedIntensity;
			else if (kBuf[i] == 0)
				res = 0;
			else
				res += 1; // compensate rounding error
			iowrite32(res, (void *)ledpwm->registers);
			dev_info(&ledpwm->pdev->dev, "set led to %i\n", res);
			msleep(200);
		} else {
			dev_err(&ledpwm->pdev->dev, "Invalid value\n");
		}
	}
	*offp = count;
	return count;
}

static const struct file_operations fops = {
	.read = ledPwm_read,
	.write = ledPwm_write,
};


static int ledpwm_probe(struct platform_device *pdev)
{
	int status;
	struct resource *io;
	struct ledpwm_t *ledpwm;
	static atomic_t ledpwm_no = ATOMIC_INIT(-1);
	char buf[10];
	int no = atomic_inc_return(&ledpwm_no);

	// alloc ressources
	ledpwm = devm_kzalloc(&pdev->dev, sizeof(*ledpwm),
						GFP_KERNEL);
	if (ledpwm == NULL) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Error in kzalloc");
		goto exit;
	}									
	platform_set_drvdata(pdev, ledpwm);
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io == NULL) {
		status = -EFAULT;
		goto reset_drvdata;
	}

	ledpwm->registers = devm_ioremap_resource(&pdev->dev, io);
	if (ledpwm->registers == NULL) {
		status = -EFAULT;
		dev_err(&pdev->dev, "Error in ioremap");
		goto reset_drvdata;
	}

	ledpwm->pdev = pdev;

	snprintf(buf, 10, "ledpwm%d", no);
	ledpwm->misc.name = buf;
	ledpwm->misc.minor = MISC_DYNAMIC_MINOR;
	ledpwm->misc.fops = &fops;
	ledpwm->misc.parent = &pdev->dev;
	status = misc_register(&ledpwm->misc);

	// turn off all leds
	iowrite32(0x0, ledpwm->registers);

	if (status != 0)
	{
		dev_err(&pdev->dev, "Error in misc_registers");
		goto reset_drvdata;
	}

	// everything okay
	return 0;

reset_drvdata:
	ledpwm = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);
exit:
	return status;
}

static int ledpwm_remove(struct platform_device *pdev)
{
	struct ledpwm_t *ledpwm;

	ledpwm = platform_get_drvdata(pdev);

	// turn off all leds
	iowrite32(0x0, ledpwm->registers);
	
	misc_deregister(&ledpwm->misc);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

//struct  device_id
static const struct of_device_id
	ledpwm_of_match[] = {
	{ .compatible = "ldd,ledpwm", },
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
