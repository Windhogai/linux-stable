// SPDX-License-Identifier: GPL-2.0-or-later
/* LDD5 Uebung02
 *
 * Bauernfeind, Schmalzer
 * led running light with character device
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <asm/errno.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>

// leds
static uint32_t intensity;
static const uint32_t startAddress = 0xFF203080;
static const uint32_t ledNumber = 0x20/4;
static const uint32_t led9 = 0xFF2030A4;
static const uint32_t maxLedIntensity = 2047;
static int direction;
static int faster;
static int waitTime = 100;
static uint32_t *runningLightAddress;
static void timer_callback_wait(struct timer_list *timer);
static void timer_callback_running_light(struct timer_list *timer);
static DEFINE_TIMER(waitTimer, timer_callback_wait);
static DEFINE_TIMER(runTimer, timer_callback_running_light);

// char device
struct device_data_t {
	uint32_t *led0to7;
	uint32_t *led9;
	struct cdev cdev;
};
static struct device_data_t device_data;
static dev_t device_number;
static struct class *cdev_class;
static struct device *cdev_device;

// sys fs
static struct device *sysfs_device;

static ssize_t led9_off_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	uint32_t ledIntensity;
	uint32_t value;
	struct device_data_t *data = dev_get_drvdata(dev);

	ledIntensity = ioread32(data->led9);
	if (ledIntensity > 0)
		value = 0;
	else
		value = 1;
	// 48 -> offset int to ASCII
	return scnprintf(buf, PAGE_SIZE, "%c\n", (char)(value+48));
}

static DEVICE_ATTR(led9_off, 0444, led9_off_show, NULL);

static int ledPwm_open(struct inode *inode, struct file *file)
{
	struct device_data_t *data = container_of(inode->i_cdev,
		struct device_data_t, cdev);

	file->private_data = data;
	pr_info("In %s", __func__);
	return 0;
}

static ssize_t ledPwm_read(struct file *filep, char __user *buf,
	size_t count, loff_t *offp)
{
	struct device_data_t *data = filep->private_data;
	unsigned int ledIntensity;
	uint8_t ledIntPercent;
	unsigned long ret;

	if (buf == NULL) {
		pr_err("Invalid buffer");
		return -EINVAL;
	}

	// end of file
	if (*offp >= 1)
		return 0;

	// small buffers
	if (count < 1)
		return -ETOOSMALL;

	ledIntensity = ioread32(data->led9);
	ledIntPercent = (uint8_t)(100 * ledIntensity / maxLedIntensity);
	ret = copy_to_user(buf, &ledIntPercent, 1);
	if (ret) {
		pr_err("Invalid input data");
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
	struct device_data_t *data = filep->private_data;

	if (count > 100) {
		pr_err("Input overflow");
		return -EINVAL;
	}
	if (buf == NULL) {
		pr_err("Invalid buffer");
		return -EINVAL;
	}
	ret = copy_from_user(kBuf, buf, count);
	if (ret) {
		pr_err("Invalid input data");
		return ret;
	}
	// count - 1 -> discard lf character
	for (i = 0; i < count-1; i++) {
		// discard input values > 100
		if (kBuf[i] <= 100) {
			res = (uint32_t)((kBuf[i]) * maxLedIntensity / 100);
			if (kBuf[i] == 100)
				res = maxLedIntensity;
			else if (kBuf[i] == 0)
				res = 0;
			else
				res += 1; // compensate rounding error
			iowrite32(res, (void *)data->led9);
			pr_info("set led 9 to %i", res);
			msleep(200);
		} else {
			pr_err("Invalid value");
		}
	}
	*offp = count;
	return count;
}

static const struct file_operations fops = {
	.open = ledPwm_open,
	.read = ledPwm_read,
	.write = ledPwm_write,
};

// Timer callbacks

static void timer_callback_wait(struct timer_list *timer)
{
	// start running light timer
	mod_timer(&runTimer, jiffies + msecs_to_jiffies(waitTime));
	// turn off leds
	for (runningLightAddress = (uint32_t *)device_data.led0to7;
		runningLightAddress < device_data.led0to7+ledNumber;
		runningLightAddress++)
		iowrite32(0x000, (void *)runningLightAddress);

	runningLightAddress = device_data.led0to7;
}

static void timer_callback_running_light(struct timer_list *timer)
{
	if (waitTime <= 10)
		faster = 1;
	else if (waitTime >= 100)
		faster = 0;
	if (faster == 0) {
		intensity += 22;
		waitTime--;
	} else if (faster == 1) {
		intensity -= 22;
		waitTime++;
	}
	// turn Led off
	iowrite32(0x0, (void *)runningLightAddress);
	// check if runnign light is currently on a boundary address
	if (runningLightAddress >= device_data.led0to7+ledNumber-1)
		direction = 0;
	else if (runningLightAddress <= device_data.led0to7)
		direction = 1;
	// move the running light depending on direction flag
	if (direction == 1) {
		runningLightAddress++;
		iowrite32(intensity, (void *)runningLightAddress);
	} else {
		runningLightAddress--;
		iowrite32(intensity, (void *)runningLightAddress);
	}
	mod_timer(&runTimer, jiffies + msecs_to_jiffies(waitTime));
}


static int __init ledpwm_init(void)
{
	struct resource *retVal;
	uint32_t *addressPtr;
	int status;

	pr_info("In ledPwm_init");

	// initialize leds

	direction = 0;
	mod_timer(&waitTimer, jiffies + msecs_to_jiffies(3000));

	retVal = request_mem_region(startAddress, ledNumber*4, "LED0-7");
	if (retVal == NULL) {
		pr_err("I/O-memory region already occupied");
		status = -EBUSY;
		goto exit;
	}
	// remap to virtual memory
	device_data.led0to7 = (uint32_t *)ioremap(startAddress, ledNumber*4);
	if (device_data.led0to7 == NULL) {
		pr_err("ioremap failed");
		status = -ENOMEM;
		goto release_memory;
	}

	retVal = request_mem_region(led9, 4, "LED9");
	if (retVal == NULL) {
		pr_err("I/O-memory for LED9 already occupied");
		status = -EBUSY;
		goto remove_leds_0_to_7;
	}

	device_data.led9 = (uint32_t *)ioremap(led9, 4);
	if (device_data.led9 == NULL) {
		pr_err("ioremap LED9 failed");
		status = -ENOMEM;
		goto release_led9_memory;
	}

	// turn on all leds
	for (addressPtr = (uint32_t *)device_data.led0to7;
		addressPtr < device_data.led0to7+ledNumber;
		addressPtr++)
		iowrite32(0x7FF, (void *)addressPtr);

	// initialize character device

	// allocate character device
	status = alloc_chrdev_region(&device_number, 0, 1, "ledPwm");
	if (status < 0) {
		pr_info("Unable to allocate chardev region");
		goto release_all_led;
	}

	// init structure
	cdev_init(&device_data.cdev, &fops);
	device_data.cdev.owner = THIS_MODULE;

	// and add device
	status = cdev_add(&device_data.cdev, device_number, 1);
	if (status < 0) {
		pr_info("Unable to add cdev");
		goto release_chardev;
	}

	// create device file
	cdev_class = class_create(THIS_MODULE, "ldd5");
	if (IS_ERR(cdev_class)) {
		pr_info("Unable to create class");
		status = -EEXIST;
		goto remove_device;
	}

	cdev_device = device_create(cdev_class, NULL, device_number,
		&device_data, "ledPwm");
	if (IS_ERR(cdev_device)) {
		pr_info("Unable to create device");
		status = -EEXIST;
		goto remove_device_class;
	}

	// initialize sysfs device

	sysfs_device = root_device_register("ledPwmFs");
	if (IS_ERR(sysfs_device)) {
		pr_err("Unable to create sysfs device");
		status = -EEXIST;
		goto remove_device_class;
	}
	dev_set_drvdata(sysfs_device, &device_data);
	status = sysfs_create_file(&sysfs_device->kobj,
		&dev_attr_led9_off.attr);
	if (status != 0) {
		pr_err("Unable to create sysfs file");
		goto unregister_device;
	}
	// everything okay
	return 0;

unregister_device:
	root_device_unregister(sysfs_device);
remove_device_class:
	class_destroy(cdev_class);
remove_device:
	cdev_del(&device_data.cdev);
release_chardev:
	unregister_chrdev_region(device_number, 1);
release_all_led:
	iounmap(device_data.led9);
release_led9_memory:
	release_mem_region(led9, 4);
remove_leds_0_to_7:
	iounmap(device_data.led0to7);
release_memory:
	release_mem_region(startAddress, ledNumber*4);
exit:
	return status;
}

static void __exit ledpwm_exit(void)
{
	uint32_t *addressPtr;

	pr_info("In ledPwm_exit");
	// remove sysfs file
	sysfs_remove_file(&sysfs_device->kobj, &dev_attr_led9_off.attr);
	// remove sysfs device
	root_device_unregister(sysfs_device);

	// remove device file
	device_destroy(cdev_class, device_number);
	class_destroy(cdev_class);

	// release resources
	cdev_del(&device_data.cdev);
	unregister_chrdev_region(device_number, 1);

	del_timer_sync(&waitTimer);
	del_timer_sync(&runTimer);

	// turn leds off
	for (addressPtr = device_data.led0to7;
		addressPtr < device_data.led0to7+ledNumber;
		addressPtr++)
		iowrite32(0x0, (void *)addressPtr);

	// unmap virtual memory
	iounmap(device_data.led0to7);
	// release I/O-memory region
	release_mem_region(startAddress, ledNumber*4);

	// unmap virtual memory
	iounmap(device_data.led9);
	// release I/O-memory region
	release_mem_region(led9, 4);
}

module_init(ledpwm_init);
module_exit(ledpwm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("displays a running light (leds 0-7) + pwm controls led 9");
MODULE_AUTHOR("Lukas Schmalzer <lukas.schmalzer@gmail.com>");
MODULE_AUTHOR("Maximilian Bauernfeind <s2020306047@fhooe.at>");
