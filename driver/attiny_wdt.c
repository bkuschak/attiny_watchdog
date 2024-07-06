// SPDX-License-Identifier: GPL-2.0
/* 
 * I2C watchdog driver for the ATtiny-based watchdog timer.
 * Tested on 5.10.168-ti-r72 Debian 12 on AM335x.
 *
 * bkuschak@gmail.com 12/21/2023
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define MODULE_NAME		"attiny_wdt"

/* The first timeout triggers an alert. If no refresh occurs before a second
 * timeout, a reboot or powercycle occurs.  
 */
#define WATCHDOG_TIMEOUT	64		/* seconds */
#define WATCHDOG_REBOOT_TIMEOUT	(2*WATCHDOG_TIMEOUT)

/* Chip registers */
#define REG_VERSION		0x00
#define REG_CONTROL		0x01
#define REG_TIMER		0x02
#define REG_STATUS		0x03

#define CONTROL_ENABLE_RESET		(1<<0)
#define CONTROL_ENABLE_POWERCYCLE	(1<<1)
#define CONTROL_ENABLE_ALERT		(1<<2)

/* TODO Status register not implemented yet */

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned timeout = WATCHDOG_TIMEOUT;
#if 0
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
	__MODULE_STRING(WATCHDOG_TIMEOUT) ")");
#endif

struct attiny_wdt_private
{
	struct device *dev;		/* i2c device */
	struct regmap *regmap;		/* i2c registers */
	struct watchdog_device wdev;
};

static int attiny_wdt_start(struct watchdog_device *wdev)
{
	struct attiny_wdt_private *priv = watchdog_get_drvdata(wdev);
	struct regmap *regmap = priv->regmap;
	int ret;

	ret = regmap_write(regmap, REG_CONTROL, CONTROL_ENABLE_POWERCYCLE |
			CONTROL_ENABLE_ALERT);
	ret = regmap_write(regmap, REG_TIMER, 255);
	if (ret)
		dev_err(priv->dev, "failed to write register: ret = %d", ret);

	dev_info(priv->dev, "watchdog%d: starting timer", wdev->id);
	return 0;
}

static int attiny_wdt_stop(struct watchdog_device *wdev)
{
	struct attiny_wdt_private *priv = watchdog_get_drvdata(wdev);
	struct regmap *regmap = priv->regmap;
	int ret;

	ret = regmap_write(regmap, REG_CONTROL, 0);
	if (ret)
		dev_err(priv->dev, "failed to write register: ret = %d", ret);

	dev_info(priv->dev, "watchdog%d: stopping timer", wdev->id);
	return 0;
}

static const struct watchdog_ops attiny_wdt_ops = {
	.start = attiny_wdt_start,
	.stop = attiny_wdt_stop,
};

static const struct watchdog_info attiny_wdt_info = {
	//.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.options = WDIOF_KEEPALIVEPING,
	.identity = MODULE_NAME
};

static ssize_t attiny_wdt_value_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct attiny_wdt_private *priv = dev_get_drvdata(dev);
	struct regmap *regmap = priv->regmap;
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, attr->index, &val);
	if (ret < 0)
		return ret;
	return snprintf(buf, PAGE_SIZE, "0x%02hx\n", val);
}

static SENSOR_DEVICE_ATTR_RO(version, attiny_wdt_value, REG_VERSION);
static SENSOR_DEVICE_ATTR_RO(control, attiny_wdt_value, REG_CONTROL);
static SENSOR_DEVICE_ATTR_RO(timer, attiny_wdt_value, REG_TIMER);
static SENSOR_DEVICE_ATTR_RO(status, attiny_wdt_value, REG_STATUS);

static struct attribute *attiny_wdt_attrs[] = {
	&sensor_dev_attr_version.dev_attr.attr,
	&sensor_dev_attr_control.dev_attr.attr,
	&sensor_dev_attr_status.dev_attr.attr,
	&sensor_dev_attr_timer.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(attiny_wdt);

static const struct regmap_config attiny_wdt_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_STATUS,
};

static int attiny_wdt_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct attiny_wdt_private *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regmap = devm_regmap_init_i2c(client, &attiny_wdt_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(priv->regmap);
	}

	priv->wdev.info = &attiny_wdt_info;
	priv->wdev.ops = &attiny_wdt_ops;
	priv->wdev.timeout = WATCHDOG_TIMEOUT;
	priv->wdev.min_timeout = WATCHDOG_TIMEOUT;
	priv->wdev.max_timeout = WATCHDOG_TIMEOUT;

	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_set_nowayout(&priv->wdev, nowayout);
	watchdog_init_timeout(&priv->wdev, timeout, dev);

	ret = devm_watchdog_register_device(dev, &priv->wdev);
	if (ret < 0) {
		dev_err(dev, "failed to register watchdog device\n");
		return ret;
	}

	/* TODO Clear faults */

	/* TODO - handle the case where the WDT is enabled by hardware prior to
	 * this driver being loaded.
	 */

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   priv, attiny_wdt_groups);
	dev_info(dev, "registered device watchdog%d. Timeout %d sec.", 
		priv->wdev.id, WATCHDOG_TIMEOUT);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id attiny_wdt_id[] = {
	{"attiny_wdt", 0},
	{"attiny_watchdog", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, attiny_wdt_id);

static struct i2c_driver attiny_wdt_driver = {
	.driver = {
		   .name = "attiny_wdt",
		   },
	.probe_new = attiny_wdt_probe,
	.id_table = attiny_wdt_id,
};

module_i2c_driver(attiny_wdt_driver);

MODULE_AUTHOR("Brian Kuschak <bkuschak@gmail.com>");
MODULE_DESCRIPTION("Custom ATtiny watchdog timer driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("isa:" MODULE_NAME);
