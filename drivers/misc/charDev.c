#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>

struct demo {
	u32 *registers; // dummy
	struct cdev cdev;
};

static struct demo device_data;
static dev_t device_number;
static struct class *cdev_class;
static struct device *cdev_device;

static int demo_open(struct inode *inode, struct file *file)
{
	struct demo *data = container_of(inode->i_cdev, struct demo, cdev);
	file->private_data = data;

	printk(KERN_INFO "In demo_open\n");

	return 0;
}

static ssize_t demo_read(struct file *filep, char __user *buf, size_t count, loff_t *offp)
{
	struct demo *data = filep->private_data;
	printk(KERN_INFO "In demo_read. count: %d, off: %lld\n", count, *offp);

	// end of file
	if (*offp >= 5)
		return 0;

	// small buffers
	if (count < 5)
		return -ETOOSMALL;

	// TODO: data->registers ...
	
	copy_to_user(buf, "hello", 5);
	*offp += 5;
	return 5;
}

static ssize_t demo_write(struct file *filep, const char __user *buf, size_t count, loff_t *offp)
{
	struct demo *data = filep->private_data;
	printk(KERN_INFO "In demo_write. count: %d, off: %lld\n", count, *offp);

	// TODO: data->registers ...
	
	msleep(2000);

	return count;
}

static struct file_operations fops = {
	.open = demo_open,
	.read = demo_read,
	.write = demo_write,
};

static int __init demo_init(void)
{
	int status;

	printk(KERN_INFO "In demo_init\n");

	// TODO: request_mem_region, ioremap, ...
	// device_data.registers = ioremap()
	
	// allocate character device
	status = alloc_chrdev_region(&device_number, 0, 1, "demo");
	if (status < 0) {
		printk(KERN_INFO "Unable to allocate chardev region\n");
		goto exit;
	}

	// init structure
	cdev_init(&device_data.cdev, &fops);
	device_data.cdev.owner = THIS_MODULE;

	// and add device
	status = cdev_add(&device_data.cdev, device_number, 1);
	if (status < 0) {
		printk(KERN_INFO "Unable to add cdev\n");
		goto release_chardev;
	}

	// create device file
	cdev_class = class_create(THIS_MODULE, "ldd5");
	if (IS_ERR(cdev_class)) {
		printk(KERN_INFO "Unable to create class\n");
		status = -EEXIST;
		goto remove_device;
	}

	cdev_device = device_create(cdev_class, NULL, device_number, &device_data, "demo");
	if (IS_ERR(cdev_device)) {
		printk(KERN_INFO "Unable to create device\n");
		status = -EEXIST;
		goto remove_device_class;
	}

	// everything okay
	return 0;

remove_device_class:
	class_destroy(cdev_class);
remove_device:
	cdev_del(&device_data.cdev);
release_chardev:
	unregister_chrdev_region(device_number, 1);
exit:
	return status;
}

static void __exit demo_exit(void)
{
	printk(KERN_INFO "In demo_exit\n");

	// remove device file
	device_destroy(cdev_class, device_number);
	class_destroy(cdev_class);

	// release resources
	cdev_del(&device_data.cdev);
	unregister_chrdev_region(device_number, 1);
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device demo");
MODULE_AUTHOR("Jakob Winkler <jakob.winkler@fh-hagenberg.at>");

