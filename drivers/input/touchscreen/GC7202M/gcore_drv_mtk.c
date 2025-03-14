/*
 * GalaxyCore touchscreen driver
 *
 * Copyright (C) 2021 GalaxyCore Incorporated
 *
 * Copyright (C) 2021 Neo Chen <neo_chen@gcoreinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "gcore_drv_common.h"

void gcore_suspend(void)
{
	//struct gcore_dev *gdev = fn_data.gdev;

	GTP_DEBUG("enter gcore suspend");

#ifdef	CONFIG_ENABLE_PROXIMITY_TP_SCREEN_OFF
	if(gcore_tpd_proximity_flag && gcore_tpd_proximity_flag_off){
		fn_data.gdev->ts_stat = TS_SUSPEND;
		GTP_DEBUG("Proximity TP Now.");
		return ;
	}

#endif

#if GCORE_WDT_RECOVERY_ENABLE
	cancel_delayed_work_sync(&fn_data.gdev->wdt_work);
#endif
	
	cancel_delayed_work_sync(&fn_data.gdev->fwu_work);

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	enable_irq_wake(fn_data.gdev->touch_irq);
#endif

#ifdef CONFIG_SAVE_CB_CHECK_VALUE
	fn_data.gdev->CB_ckstat = false;
#endif

	fn_data.gdev->ts_stat = TS_SUSPEND;

	gcore_touch_release_all_point(fn_data.gdev->input_device);
	
	GTP_DEBUG("gcore suspend end");

}

void gcore_resume(struct work_struct *work)
{
	//struct gcore_dev *gdev = fn_data.gdev;

	GTP_DEBUG("enter gcore resume");
	
#ifdef	CONFIG_ENABLE_PROXIMITY_TP_SCREEN_OFF
			if(fn_data.gdev->PS_Enale == true){
				tpd_enable_ps(1);
			}
		
#endif


#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	disable_irq_wake(fn_data.gdev->touch_irq);
#endif
	
#ifdef CONFIG_GCORE_AUTO_UPDATE_FW_HOSTDOWNLOAD
	gcore_request_firmware_update_work(NULL);
#else
#if CONFIG_GCORE_RESUME_EVENT_NOTIFY
	queue_delayed_work(fn_data.gdev->gtp_workqueue, &fn_data.gdev->resume_notify_work, msecs_to_jiffies(200));
#endif
	gcore_touch_release_all_point(fn_data.gdev->input_device);
#endif
	fn_data.gdev->ts_stat = TS_NORMAL;

	GTP_DEBUG("gcore resume end");
}

#ifdef TP_RESUME_BY_FB_NOTIFIER
int gcore_ts_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct gcore_dev *gdev= container_of(self, struct gcore_dev, fb_notifier);
	int *blank=(int *)data;

	GTP_DEBUG("event = %d, blank = %d", event, blank);


	if (!(event == MTK_DISP_EARLY_EVENT_BLANK || event == MTK_DISP_EVENT_BLANK)) {
		GTP_DEBUG("event(%lu) do not need process\n", event);
		return 0;
	}
	if(gdev&&data) {
		switch (*blank) {
			case MTK_DISP_BLANK_POWERDOWN:
				if (event == MTK_DISP_EARLY_EVENT_BLANK) {
					gcore_suspend();
				}
			break;

			case MTK_DISP_BLANK_UNBLANK:
				if (event == MTK_DISP_EVENT_BLANK) {
					//gcore_resume();
					queue_work(fn_data.gdev->fwu_workqueue,&fn_data.gdev->resume_work);
			}
			break;

			default:
			break;
		}
	}
	return 0;
}
#endif

static int __init touch_driver_init(void)
{
	GTP_DEBUG("touch driver init.");

	if (gcore_touch_bus_init()) {
		GTP_ERROR("bus init fail!");
		return -EPERM;
	}

	return 0;
}

/* should never be called */
static void __exit touch_driver_exit(void)
{
	gcore_touch_bus_exit();
}

module_init(touch_driver_init);
module_exit(touch_driver_exit);

MODULE_AUTHOR("GalaxyCore, Inc.");
MODULE_DESCRIPTION("GalaxyCore Touch Main Mudule");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
MODULE_LICENSE("GPL");
