#ifndef __HALL_DETECT_H__
#define __HALL_DETECT_H__

#define HALL_NEAR  (1)
#define HALL_FAR   (0)

#define KEY_HALL_SLOW_IN 0x2ee
#define KEY_HALL_SLOW_OUT 0x2ef

struct hall_t {
	unsigned int irq;
	unsigned int gpiopin;
	unsigned int curr_mode;
	unsigned int retry_cnt;
	struct input_dev * hall_dev;
	spinlock_t   spinlock;
};

#endif /* __HALL_DETECT_H__*/
