#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/compat.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <linux/of_gpio.h>



#include "hall_detect.h"

#include <linux/init.h>
#include <linux/module.h>

#include <linux/input.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#define HALL_DEBUG 1
#define LOG_TAG "[HALL_EFFECT] "

#if HALL_DEBUG
#define LOG_INFO(fmt, args...)    pr_info(LOG_TAG fmt, ##args)
#else
#define LOG_INFO(fmt, args...)
#endif

#define HALL_COMPATIBLE_DEVICE_ID "mediatek,hall_detect"
static bool hall_state;
module_param(hall_state,bool,0644);

struct hall_t hall_data;
const struct of_device_id hall_of_match[] = {
    { .compatible = HALL_COMPATIBLE_DEVICE_ID, },
};

static int hall_probe(struct platform_device * pdev)
{
    int ret = 0;
    struct pinctrl * hall_pinctrl = NULL;
    struct pinctrl_state * state = NULL;

    LOG_INFO("Enter %s\n", __func__);

    hall_pinctrl = devm_pinctrl_get(&pdev->dev);
    state = pinctrl_lookup_state(hall_pinctrl, "hall_active");
    if (IS_ERR(state)) {
        LOG_INFO(" can't find hall active\n");
        return -EINVAL;
    }

    ret = pinctrl_select_state(hall_pinctrl, state);
    if (ret) {
        LOG_INFO(" can't select hall active\n");
        return -EINVAL;
    }
    else
        LOG_INFO(" select hall active success\n");

    return ret;
}

static int hall_remove(struct platform_device * pdev)
{
    LOG_INFO("Enter %s\n", __func__);
    return 0;
}

static struct platform_driver hall_driver = {
    .driver = {
        .name = "hall_detect",
        .owner = THIS_MODULE,
        .of_match_table = hall_of_match,
    },
    .probe  = hall_probe,
    .remove = hall_remove,
};

static irqreturn_t hall_irq_func(int irq, void * data)
{
    int ret = 0;
    struct hall_t * halldsi = NULL;
    unsigned long flags;

    LOG_INFO("Enter %s\n", __func__);

    halldsi = (struct hall_t *)data;

    spin_lock_irqsave(&halldsi->spinlock, flags);

    ret = gpio_get_value_cansleep(halldsi->gpiopin);
    if (1 == ret) {
        irq_set_irq_type(halldsi->irq, IRQ_TYPE_LEVEL_LOW);
        halldsi->curr_mode = HALL_FAR;
    }
    else {
        irq_set_irq_type(halldsi->irq, IRQ_TYPE_LEVEL_HIGH);
        halldsi->curr_mode = HALL_NEAR;
    }

    if (halldsi->curr_mode == HALL_NEAR) {
        input_report_key(halldsi->hall_dev, KEY_HALL_SLOW_IN, 1);
        input_report_key(halldsi->hall_dev, KEY_HALL_SLOW_IN, 0);
    } else {
        input_report_key(halldsi->hall_dev, KEY_HALL_SLOW_OUT, 1);
        input_report_key(halldsi->hall_dev, KEY_HALL_SLOW_OUT, 0);
    }
    hall_state = !!halldsi->curr_mode;
    input_sync(halldsi->hall_dev);

    spin_unlock_irqrestore(&halldsi->spinlock, flags);
    return IRQ_HANDLED;
}

int hall_register_input_device(void)
{
    int ret = 0;
    struct input_dev * input = NULL;

    LOG_INFO("Enter %s\n", __func__);

    input = input_allocate_device();
    if (!input) {
        LOG_INFO("Can't allocate input device! \n");
        return -1;
    }

    /* init input device data*/
    input->name = "hall_irq";
    __set_bit(EV_KEY, input->evbit);
    __set_bit(KEY_HALL_SLOW_IN, input->keybit);
    __set_bit(KEY_HALL_SLOW_OUT, input->keybit);
    ret = input_register_device(input);
    if (ret) {
        LOG_INFO("Failed to register device\n");
        goto err_free_dev;
    }

    hall_data.hall_dev = input;
    return 0;

err_free_dev:
    input_free_device(input);
    return ret;
}

static int __init hall_init(void) {
    struct device_node * node = NULL;
    int ret = 0;
    hall_state = 0;

    LOG_INFO("Enter %s\n", __func__);

    hall_data.curr_mode = HALL_FAR;

    /* pinctrl start */
    ret = platform_driver_register(&hall_driver);
    if (ret) {
        LOG_INFO("register hall_driver failed\n");
        return ret;
    }

    /* input device start */
    ret = hall_register_input_device();
    if (ret) {
        LOG_INFO("register hall input failed\n");
        return ret;
    }
    /* input device end */

    node = of_find_matching_node(NULL, hall_of_match);
    if (NULL == node) {
        LOG_INFO("can't find compatible node\n");
        return -1;
    }
    hall_data.gpiopin = of_get_named_gpio(node, "hall,gpio_irq", 0);
    if (!gpio_is_valid(hall_data.irq)) {
        LOG_INFO("hall int gpio not found\n");
        return -1;
    }

    LOG_INFO("hall_data.gpiopin is %d\n", hall_data.gpiopin);

    gpio_direction_input(hall_data.gpiopin);
    hall_data.irq = gpio_to_irq(hall_data.gpiopin);
    if (hall_data.irq < 0) {
        LOG_INFO("Unable to configure irq\n");
        return -1;
    }

    spin_lock_init(&hall_data.spinlock);
    ret = request_threaded_irq(hall_data.irq, NULL, hall_irq_func, IRQF_TRIGGER_LOW | IRQF_ONESHOT, "hall_irq", &hall_data);
    if (!ret) {
        enable_irq_wake(hall_data.irq);
        LOG_INFO("enable irq wake\n");
    }

    return 0;
}

static void __exit hall_exit(void) {
    LOG_INFO("Enter %s\n", __func__);

    input_unregister_device(hall_data.hall_dev);
    free_irq(hall_data.irq, hall_irq_func);
}

module_init(hall_init);
module_exit(hall_exit);
MODULE_LICENSE("GPL v2");
