/*
 * drivers/power/pad_battery.c
 *
 * Gas Gauge driver for TI's BQ20Z45
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/asusec.h>

#include <mach/board-transformer-misc.h>

#define BATTERY_POLLING_RATE                60
#define TEMP_KELVIN_TO_CELCIUS              2731

#define USB_NO_Cable                        0
#define USB_DETECT_CABLE                    1
#define USB_SHIFT                           0
#define AC_SHIFT                            1

#define USB_Cable                           ((1 << (USB_SHIFT)) | (USB_DETECT_CABLE))
#define USB_AC_Adapter                      ((1 << (AC_SHIFT)) | (USB_DETECT_CABLE))
#define USB_CALBE_DETECT_MASK               (USB_Cable  | USB_DETECT_CABLE)

/* battery status value bits */
#define BATTERY_CHARGING                    0x40
#define BATTERY_FULL_CHARGED                0x20
#define BATTERY_FULL_DISCHARGED             0x10

static unsigned int battery_cable_status = 0;
static unsigned int battery_docking_status = 0;
static unsigned int battery_driver_ready = 0;

static int ac_on;
static int usb_on;
static int docking_status = 0;
struct workqueue_struct *battery_work_queue = NULL;

/* Functions declaration */
static int pad_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val);
static int dock_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val);

typedef enum {
	Charger_Type_Battery = 0,
	Charger_Type_AC,
	Charger_Type_USB,
	Charger_Type_Dock_Battery,
	Charger_Type_Docking_AC,
	Charger_Type_Num,
	Charger_Type_Force32 = 0x7FFFFFFF
} Charger_Type;

static enum power_supply_property pad_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
};

static const unsigned pad_battery_prop_offs[] = {
	[POWER_SUPPLY_PROP_STATUS] = 1,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = 3,
	[POWER_SUPPLY_PROP_TEMP] = 7,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = 9,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = 11,
	[POWER_SUPPLY_PROP_CAPACITY] = 13,
	[POWER_SUPPLY_PROP_CHARGE_NOW] = 15,
	[POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG] = 17,
	[POWER_SUPPLY_PROP_TIME_TO_FULL_AVG] = 19,
};

void check_cabe_type(void)
{
	if (battery_cable_status == USB_AC_Adapter) {
		ac_on = 1;
		usb_on = 0;
	} else if (battery_cable_status == USB_Cable) {
		ac_on = 0;
		usb_on = 1;
	} else {
		ac_on = 0;
		usb_on = 0;
	}
}

static enum power_supply_property power_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int power_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			if(psy->type == POWER_SUPPLY_TYPE_MAINS && ac_on)
				val->intval =  1;
			else if (psy->type == POWER_SUPPLY_TYPE_USB && usb_on)
				val->intval =  1;
			else if (psy->type == POWER_SUPPLY_TYPE_DOCK_AC && battery_docking_status)
				val->intval =  1;
			else
				val->intval = 0;
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

static char *supply_list[] = {
	"battery",
	"ac",
	"usb",
};

static struct power_supply pad_supply[] = {
	{
		.name            = "battery",
		.type            = POWER_SUPPLY_TYPE_BATTERY,
		.properties      = pad_battery_properties,
		.num_properties  = ARRAY_SIZE(pad_battery_properties),
		.get_property    = pad_battery_get_property,
	},
	{
		.name            = "ac",
		.type            = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to     = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties      = power_properties,
		.num_properties  = ARRAY_SIZE(power_properties),
		.get_property    = power_get_property,
	},
	{
		.name            = "usb",
		.type            = POWER_SUPPLY_TYPE_USB,
		.supplied_to     = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties      = power_properties,
		.num_properties  = ARRAY_SIZE(power_properties),
		.get_property    = power_get_property,
	},
	{
		.name            = "dock_battery",
		.type            = POWER_SUPPLY_TYPE_DOCK_BATTERY,
		.properties      = pad_battery_properties,
		.num_properties  = ARRAY_SIZE(pad_battery_properties),
		.get_property    = dock_battery_get_property,
	},
	{
		.name            = "docking_ac",
		.type            = POWER_SUPPLY_TYPE_DOCK_AC,
		.properties      = power_properties,
		.num_properties  = ARRAY_SIZE(power_properties),
		.get_property    = power_get_property,
	},
};

static struct pad_device_info {
	struct i2c_client     *client;
	struct delayed_work   status_poll_work;
	struct delayed_work   low_bat_work;
	struct wake_lock      low_battery_wake_lock;
	struct wake_lock      cable_event_wake_lock;
	struct timer_list     charger_pad_dock_detect_timer;

	unsigned int old_capacity;
	bool dock_charger_pad_interrupt_enabled;

	spinlock_t lock;
} *pad_device;

static void battery_status_poll(struct work_struct *work)
{
	struct pad_device_info *battery_device = container_of(work, struct pad_device_info, status_poll_work.work);

	if(!battery_driver_ready)
		pr_warning("pad_battery: %s: driver not ready\n", __func__);

	power_supply_changed(&pad_supply[Charger_Type_Battery]);

	/* Schedule next poll */
	queue_delayed_work(battery_work_queue, &battery_device->status_poll_work, BATTERY_POLLING_RATE*HZ);
}

static void low_battery_check(struct work_struct *work)
{
	cancel_delayed_work(&pad_device->status_poll_work);
	queue_delayed_work(battery_work_queue, &pad_device->status_poll_work, 0.1*HZ);
	msleep(2000);
	enable_irq(gpio_to_irq(LOW_BATTERY_GPIO));
}

void battery_callback(unsigned usb_cable_state)
{
	int old_cable_status;

	if(!battery_driver_ready)
		return;

	old_cable_status = battery_cable_status;
	battery_cable_status = usb_cable_state;

	pr_info("pad_battery: %s: usb_cable_state = %x\n", __func__, usb_cable_state);

	if (old_cable_status != battery_cable_status) {
		pr_info("pad_battery: %s: cable event wake lock 5 sec...\n", __func__);
		wake_lock_timeout(&pad_device->cable_event_wake_lock, 5*HZ);
	}
	check_cabe_type();

	if(!battery_cable_status){
		if (old_cable_status == USB_AC_Adapter){
			power_supply_changed(&pad_supply[Charger_Type_AC]);
		} else if (old_cable_status == USB_Cable){
			power_supply_changed(&pad_supply[Charger_Type_USB]);
		}
	} else if (battery_cable_status == USB_Cable && old_cable_status != USB_Cable){
		power_supply_changed(&pad_supply[Charger_Type_USB]);
	} else if (battery_cable_status == USB_AC_Adapter && old_cable_status != USB_AC_Adapter){
		power_supply_changed(&pad_supply[Charger_Type_AC]);
	}
	cancel_delayed_work(&pad_device->status_poll_work);
	queue_delayed_work(battery_work_queue, &pad_device->status_poll_work, 2*HZ);
}

static irqreturn_t battery_interrupt_handler(int irq, void *dev_id)
{
	if (irq == gpio_to_irq(DOCK_CHARGING_GPIO))
		mod_timer(&pad_device->charger_pad_dock_detect_timer, jiffies + (5*HZ));

	if (irq == gpio_to_irq(LOW_BATTERY_GPIO)){
		disable_irq_nosync(gpio_to_irq(LOW_BATTERY_GPIO));
		wake_lock_timeout(&pad_device->low_battery_wake_lock, 10*HZ);
		queue_delayed_work(battery_work_queue, &pad_device->low_bat_work, 0.1*HZ);
	}

	return IRQ_HANDLED;
}

static void charger_pad_dock_detection(unsigned long unused)
{
	int dock_in = !gpio_get_value(TEGRA_GPIO_PU4);
	int charger_pad_dock = !gpio_get_value(DOCK_CHARGING_GPIO);

	if (docking_status && dock_in && charger_pad_dock)
		battery_docking_status = true;
	else
		battery_docking_status = false;

	check_cabe_type();
	power_supply_changed(&pad_supply[Charger_Type_Docking_AC]);

	if(battery_driver_ready){
		cancel_delayed_work(&pad_device->status_poll_work);
		queue_delayed_work(battery_work_queue, &pad_device->status_poll_work, 0.2*HZ);
	}
}

int docking_callback(int docking_in)
{
	if(!battery_driver_ready)
		return -1;

	pr_info("pad_battery: %s: docked in = %u, TEGRA_GPIO_PU4 is %x\n", __func__, docking_in, gpio_get_value(TEGRA_GPIO_PU4));
	docking_status = docking_in;

	if(docking_status){
		if(!pad_device->dock_charger_pad_interrupt_enabled){
			enable_irq(gpio_to_irq(DOCK_CHARGING_GPIO));
			pad_device->dock_charger_pad_interrupt_enabled = true;
			pr_info("pad_battery: %s: enable_irq for TEGRA_GPIO_PS5\n", __func__) ;
		}
	} else if(pad_device->dock_charger_pad_interrupt_enabled){
		disable_irq(gpio_to_irq(DOCK_CHARGING_GPIO));
		pad_device->dock_charger_pad_interrupt_enabled = false;
		pr_info("pad_battery: %s: disable_irq for TEGRA_GPIO_PS5\n", __func__) ;
	}

	if(battery_driver_ready){
		cancel_delayed_work_sync(&pad_device->status_poll_work);
		power_supply_changed(&pad_supply[Charger_Type_Battery]);
		mod_timer(&pad_device->charger_pad_dock_detect_timer, jiffies + (5*HZ));
	}

	return 0;
}

static int battery_get_value(enum power_supply_property psp, bool pad)
{
	int rc, offs;

	if (psp >= ARRAY_SIZE(pad_battery_prop_offs))
		return -EINVAL;

	if (!pad_battery_prop_offs[psp])
		return -EINVAL;

	offs = pad_battery_prop_offs[psp];

	if (pad)
		rc = pad_battery_monitor(offs);
	else
		rc = dock_battery_monitor(offs);

	return rc;
}

static int battery_get_psp(enum power_supply_property psp,
	union power_supply_propval *val, bool pad)
{
	int ret;
	s32 temp_capacity;

	ret = battery_get_value(psp, pad);
	if (ret < 0)
		return ret;

	val->intval = (s16)ret;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			if (pad) {
				/* mask the upper byte and then find the actual status */
				if (!(ret & BATTERY_CHARGING) && (ac_on || battery_docking_status)) {
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
					if (pad_device->old_capacity == 100)
						val->intval = POWER_SUPPLY_STATUS_FULL;
				} else if (ret & BATTERY_FULL_CHARGED)
					val->intval = POWER_SUPPLY_STATUS_FULL;
				else if (ret & BATTERY_FULL_DISCHARGED)
					val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
				else
					val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			} else {
				if (ret & 0x4)
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
				else
					val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
			break;

		case POWER_SUPPLY_PROP_TEMP:
			val->intval -= TEMP_KELVIN_TO_CELCIUS;
			break;

		case POWER_SUPPLY_PROP_CAPACITY:
			/*
			 * Spec says that this can be >100 %
			 * even if max value is 100 %
			 */
			temp_capacity = ((ret >= 100) ? 100 : ret);

			/* start: for mapping %99 to 100%. Lose 84%*/
			if(temp_capacity == 99)
				temp_capacity = 100;
			if(temp_capacity >=84 && temp_capacity <=98)
				temp_capacity++;
			/* for mapping %99 to 100% */

			 /* lose 26% 47% 58%,69%,79% */
			if(temp_capacity >70 && temp_capacity <80)
				temp_capacity -= 1;
			else if(temp_capacity >60 && temp_capacity <=70)
				temp_capacity -= 2;
			else if(temp_capacity >50 && temp_capacity <=60)
				temp_capacity -= 3;
			else if(temp_capacity >30 && temp_capacity <=50)
				temp_capacity -= 4;
			else if(temp_capacity >=0 && temp_capacity <=30)
				temp_capacity -= 5;

			/*Re-check capacity to avoid  that temp_capacity <0*/
			temp_capacity = ((temp_capacity <0) ? 0 : temp_capacity);

			val->intval = temp_capacity;
			pad_device->old_capacity = val->intval;

		default:
			break;	
	}

	return 0;
}

static int battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val, bool pad)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
			if (pad)
				val->intval = 1;
			else
				val->intval = dock_battery_monitor(0);
			break;

		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;

		default:
			if (battery_get_psp(psp, val, pad))
				return -EINVAL;
			break;
	}

	return 0;
}

static int pad_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = battery_get_property(psy, psp, val, true);
	return ret;
}

static int dock_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = battery_get_property(psy, psp, val, false);
	return ret;
}

static int pad_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc, i = 0;

	pad_device = kzalloc(sizeof(*pad_device), GFP_KERNEL);
	if (!pad_device)
		return -ENOMEM;

	memset(pad_device, 0, sizeof(*pad_device));

	pad_device->client = client;
	i2c_set_clientdata(client, pad_device);

	pad_device->old_capacity = 0xFF;

	for (i = 0; i < ARRAY_SIZE(pad_supply); i++) {
		rc = power_supply_register(&client->dev, &pad_supply[i]);
		if (rc) {
			pr_err("pad_battery: failed to register power supply\n");
			while (i--)
				power_supply_unregister(&pad_supply[i]);
			kfree(pad_device);
			return rc;
		}
	}

	battery_work_queue = create_singlethread_workqueue("battery_workqueue");
	INIT_DELAYED_WORK(&pad_device->status_poll_work, battery_status_poll);
	INIT_DELAYED_WORK(&pad_device->low_bat_work, low_battery_check);
	cancel_delayed_work(&pad_device->status_poll_work);

	spin_lock_init(&pad_device->lock);
	wake_lock_init(&pad_device->low_battery_wake_lock, WAKE_LOCK_SUSPEND, "low_battery_detection");
	wake_lock_init(&pad_device->cable_event_wake_lock, WAKE_LOCK_SUSPEND, "battery_cable_event");

	asus_ec_irq_request(NULL, DOCK_CHARGING_GPIO, battery_interrupt_handler,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, DOCK_CHARGING);
	asus_ec_irq_request(NULL, LOW_BATTERY_GPIO, battery_interrupt_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, LOW_BATTERY);

	pad_device->dock_charger_pad_interrupt_enabled = true;

	enable_irq_wake(gpio_to_irq(LOW_BATTERY_GPIO));

	battery_driver_ready = 1;

	setup_timer(&pad_device->charger_pad_dock_detect_timer, charger_pad_dock_detection, 0);

	queue_delayed_work(battery_work_queue, &pad_device->status_poll_work, 15*HZ);

	if (tegra3_get_project_id() == TEGRA3_PROJECT_TF201)
		gpio_request_one(THERMAL_POWER_GPIO, GPIOF_INIT_HIGH, THERMAL_POWER);

	pr_info("pad_battery: probed\n");

	return 0;
}

static int __devexit pad_remove(struct i2c_client *client)
{
	struct pad_device_info *pad_device;
	int i = 0;

	pad_device = i2c_get_clientdata(client);
	del_timer_sync(&pad_device->charger_pad_dock_detect_timer);

	for (i = 0; i < ARRAY_SIZE(pad_supply); i++)
		power_supply_unregister(&pad_supply[i]);

	if (pad_device) {
		wake_lock_destroy(&pad_device->low_battery_wake_lock);
		kfree(pad_device);
		pad_device = NULL;
	}

	return 0;
}

static int pad_suspend(struct device *dev)
{
	cancel_delayed_work_sync(&pad_device->status_poll_work);
	del_timer_sync(&pad_device->charger_pad_dock_detect_timer);
	flush_workqueue(battery_work_queue);

	if (tegra3_get_project_id() == TEGRA3_PROJECT_TF201)
		gpio_direction_output(THERMAL_POWER_GPIO, 0);

	return 0;
}

/* Any smbus transaction will wake up pad */
static int pad_resume(struct device *dev)
{
	cancel_delayed_work(&pad_device->status_poll_work);
	queue_delayed_work(battery_work_queue, &pad_device->status_poll_work, 5*HZ);

	if (tegra3_get_project_id() == TEGRA3_PROJECT_TF201)
		gpio_direction_output(THERMAL_POWER_GPIO, 1);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pad_pm_ops, pad_suspend, pad_resume);

static const struct i2c_device_id pad_id[] = {
	{ "pad-battery", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, pad_id);

static struct i2c_driver pad_battery_driver = {
	.probe		= pad_probe,
	.remove 	= __devexit_p(pad_remove),
	.id_table	= pad_id,
	.driver = {
		.name   = "pad-battery",
		.owner  = THIS_MODULE,
		.pm     = &pad_pm_ops,
	},
};

module_i2c_driver(pad_battery_driver);

MODULE_AUTHOR("NVIDIA Corporation");
MODULE_DESCRIPTION("PAD battery monitor driver");
MODULE_LICENSE("GPL");
