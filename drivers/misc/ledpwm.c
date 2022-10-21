// SPDX-License-Identifier: GPL-2.0-or-later
/* LDD5 Uebung01
 *
 * Bauernfeind, Schmalzer
 * led running light
 * This is a test program to activate and deactivate
 * the Leds on the De1-SoC by pwm hardware access.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <asm/errno.h>

static uint32_t ledHighIntensity = 0x5FF;
static uint32_t ledLowIntensity = 0x300;
static uint32_t intensity = 0x0;

static const uint32_t startAddress = 0xFF203080;
static const uint32_t lenAddress = 0x28/4;
static uint32_t *ledMem;
static int direction;
static int waitTime = 100;
static int faster = 1;
static uint32_t *runningLightAddress;
static void timer_callback_wait(struct timer_list *timer);
static void timer_callback_running_light(struct timer_list *timer);

static DEFINE_TIMER(waitTimer, timer_callback_wait);
static DEFINE_TIMER(runTimer, timer_callback_running_light);

static void timer_callback_wait(struct timer_list *timer)
{
	// start running light timer
	mod_timer(&runTimer, jiffies + msecs_to_jiffies(waitTime));
	// turn off leds
	for (runningLightAddress = (uint32_t *)ledMem;
		runningLightAddress < ledMem+lenAddress;
		runningLightAddress++)
		iowrite32(0x000, (void *)runningLightAddress);

	runningLightAddress = ledMem;
}

static void timer_callback_running_light(struct timer_list *timer)
{	
	if(waitTime <= 10) {
		faster = 1;
	} else if (waitTime >= 100) {
		faster = 0;
	}
	if(faster == 0) {
		intensity += 22;
		waitTime--;
	}
	else if (faster == 1) {
		intensity -= 22;
		waitTime++;
	}
	// turn of led
	iowrite32(0x0, (void *)runningLightAddress);
    // check if runnign light is currently on a boundary address
	if (runningLightAddress >= ledMem+lenAddress-1)
		direction = 0;
	else if (runningLightAddress <= ledMem)
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

	direction = 0;
	mod_timer(&waitTimer, jiffies + msecs_to_jiffies(3000));

	retVal = request_mem_region(startAddress, lenAddress*4, "DE1-SoC");
	if (retVal == NULL) {
		pr_err("I/O-memory region already occupied");
		return -EBUSY;
	}
	// remap to virtual memory
	ledMem = (uint32_t *)ioremap(startAddress, lenAddress*4);
	if (ledMem == NULL) {
		pr_err("ioremap failed");
		return -ENOMEM;
	}
	// clear leds
	for (addressPtr = (uint32_t *)ledMem;
		addressPtr < ledMem+lenAddress;
		addressPtr++)
		iowrite32(0x7FF, (void *)addressPtr);
	return 0;
}

static void __exit ledpwm_exit(void)
{
	uint32_t *addressPtr;

	del_timer_sync(&waitTimer);
	del_timer_sync(&runTimer);

	// turn leds off
	for (addressPtr = ledMem;
		addressPtr < ledMem+lenAddress;
		addressPtr++)
		iowrite32(0x0, (void *)addressPtr);

	// unmap virtual memory
	iounmap(ledMem);
	// release I/O-memory region
	release_mem_region(startAddress, lenAddress*4);
}

module_init(ledpwm_init);
module_exit(ledpwm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("displays a running light on leds 0 to 9");
MODULE_AUTHOR("Lukas Schmalzer <lukas.schmalzer@gmail.com>");
MODULE_AUTHOR("Maximilian Bauernfeind <s2020306047@fhooe.at>");
