#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/mmc/mmc.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#define CPUID "cpuid"
#define CHIPID "chipid"
#define PROC_SERIAL_NUM_FILE "serial_num"
#define PROC_CHIPID_FILE "chip_id"

static struct proc_dir_entry *entry;
const char *cpuid_buf = NULL;
const char *chipid_buf = NULL;
bool cpuid_flag = true;
bool chipid_flag = true;

static int serial_num_proc_show(struct seq_file *file, void *data)
{
	if (cpuid_flag)
		seq_printf(file, "0x%s\n", cpuid_buf);
	else
		seq_printf(file, "%s\n", "123456ABCDEF");

	return 0;
}

static int serial_num_proc_open (struct inode *inode, struct file *file)
{
	return single_open(file, serial_num_proc_show, inode->i_private);
}

static const struct proc_ops serial_num_proc_fops = {
	.proc_open = serial_num_proc_open,
	.proc_read = seq_read,
};

static int chip_id_proc_show(struct seq_file *file, void *data)
{
	if (chipid_flag)
		seq_printf(file, "0x%s\n", chipid_buf);
	else
		seq_printf(file, "%s\n", "123456ABCDEF");

	return 0;
}

static int chip_id_proc_open (struct inode *inode, struct file *file)
{
	return single_open(file, chip_id_proc_show, inode->i_private);
}

static const struct proc_ops chip_id_proc_fops = {
	.proc_open = chip_id_proc_open,
	.proc_read = seq_read,
};

static int sn_fuse_probe(struct platform_device *pdev)
{
	struct device_node *sn_fuse_node = pdev->dev.of_node;
	int ret;

	pr_err("sn_fuse_probe...\n");

	ret = of_property_read_string(sn_fuse_node, CPUID, &cpuid_buf);
	if (ret) {
		pr_err("%s: get cpuid fail\n", __func__);
		cpuid_flag = false;
		return -1;
	}
	pr_err("cpuid = %s\n", cpuid_buf);

	ret = of_property_read_string(sn_fuse_node, CHIPID, &chipid_buf);
	if (ret) {
		pr_err("%s: get chipid fail\n", __func__);
		chipid_flag = false;
		return -1;
	}
	pr_err("chipid = %s\n", chipid_buf);

	return 0;
}

static int sn_fuse_remove(struct platform_device *pdev)
{
	pr_err("enter [%s] \n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sn_fuse_of_match[] = {
	{.compatible = "mediatek,sn_fuse",},
	{},
};
#endif

static struct platform_driver sn_fuse_driver = {
	.probe = sn_fuse_probe,
	.remove = sn_fuse_remove,
	.driver = {
		.name = "sn_fuse",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sn_fuse_of_match,
#endif
	},
};

static int __init sn_fuse_init(void)
{
	int ret;

	ret = platform_driver_register(&sn_fuse_driver);
	if (ret) {
		pr_err("[%s]Failed to register sn_fuse driver\n", __func__);
		return ret;
	}

	entry = proc_create(PROC_SERIAL_NUM_FILE, 0644, NULL, &serial_num_proc_fops);
	if (entry == NULL) {
		pr_err("[%s]: create serial_num_proc_entry failed\n", __func__);
	}

	entry = proc_create(PROC_CHIPID_FILE, 0644, NULL, &chip_id_proc_fops);
	if (entry == NULL)	{
	pr_err("[%s]: create chip_id_proc_entry failed\n", __func__);
	}

	return 0;
}
module_init(sn_fuse_init);

static void __exit sn_fuse_exit(void)
{
	printk("sn_fuse_exit\n");
	platform_driver_unregister(&sn_fuse_driver);
}
module_exit(sn_fuse_exit);
MODULE_DESCRIPTION("serial_num driver");
MODULE_LICENSE("GPL");

