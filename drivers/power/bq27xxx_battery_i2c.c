/*
 * SCI Reset driver for Keystone based devices
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <asm/unaligned.h>

#include <linux/power/bq27xxx_battery.h>

static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

#define BQ27XXX_TERM_V_MIN	2800
#define BQ27XXX_TERM_V_MAX	3700

#define BQ27XXX_REG_CTRL		0

#define BQ27XXX_BLOCK_DATA_CLASS	0x3E
#define BQ27XXX_DATA_BLOCK		0x3F
#define BQ27XXX_BLOCK_DATA		0x40
#define BQ27XXX_BLOCK_DATA_CHECKSUM	0x60
#define BQ27XXX_BLOCK_DATA_CONTROL	0x61
#define BQ27XXX_SET_CFGUPDATE		0x13
#define BQ27XXX_SOFT_RESET		0x42

enum bq27xxx_dm_subclass_index {
	BQ27XXX_DM_DESIGN_CAP = 0,
	BQ27XXX_DM_DESIGN_ENERGY,
	BQ27XXX_DM_TERMINATE_VOLTAGE,
	BQ27XXX_NUM_IDX,
};

struct bq27xxx_dm_regs {
	unsigned int subclass_id;
	unsigned int offset;
	char *name;
};

#define BQ27XXX_GAS_GAUGING_STATE_SUBCLASS	82

static struct bq27xxx_dm_regs bq27425_dm_subclass_regs[] = {
	{ BQ27XXX_GAS_GAUGING_STATE_SUBCLASS, 12, "design-capacity" },
	{ BQ27XXX_GAS_GAUGING_STATE_SUBCLASS, 14, "design-energy" },
	{ BQ27XXX_GAS_GAUGING_STATE_SUBCLASS, 18, "terminate-voltage" },
};

static struct bq27xxx_dm_regs *bq27xxx_dm_subclass_regs[] = {
	[BQ27425] = bq27425_dm_subclass_regs,
};

static unsigned int bq27xxx_unseal_keys[] = {
	[BQ27425] = 0x04143672,
};

static irqreturn_t bq27xxx_battery_irq_handler_thread(int irq, void *data)
{
	struct bq27xxx_device_info *di = data;

	bq27xxx_battery_update(di);

	return IRQ_HANDLED;
}

static int bq27xxx_battery_i2c_read(struct bq27xxx_device_info *di, u8 reg,
				    bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	unsigned char data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	if (!single)
		ret = get_unaligned_le16(data);
	else
		ret = data[0];

	return ret;
}

static int bq27xxx_battery_i2c_write(struct bq27xxx_device_info *di, u8 reg,
				     int value, bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;

	data[0] = reg;
	if (single) {
		data[1] = (unsigned char) value;
		msg.len = 2;
	} else {
		put_unaligned_le16(value, &data[1]);
		msg.len = 3;
	}

	msg.buf = data;
	msg.addr = client->addr;
	msg.flags = 0;

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EINVAL;
}

static int bq27xxx_battery_i2c_bulk_read(struct bq27xxx_device_info *di, u8 reg,
					 u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);

	if (!client->adapter)
		return -ENODEV;

	return i2c_smbus_read_i2c_block_data(client, reg, len, data);
}

static int bq27xxx_battery_i2c_bulk_write(struct bq27xxx_device_info *di,
					  u8 reg, u8 *data, int len)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 buf[33];

	if (!client->adapter)
		return -ENODEV;

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	msg.buf = buf;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len + 1;

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EINVAL;
}

static int bq27xxx_battery_i2c_set_seal_state(struct bq27xxx_device_info *di,
					      bool state)
{
	unsigned int key = bq27xxx_unseal_keys[di->chip];
	int ret;

	if (state)
		return di->bus.write(di, BQ27XXX_REG_CTRL, 0x20, false);

	ret = di->bus.write(di, BQ27XXX_REG_CTRL, (key >> 16) & 0xffff, false);
	if (ret < 0)
		return ret;

	return di->bus.write(di, BQ27XXX_REG_CTRL, key & 0xffff, false);
}

static int bq27xxx_battery_i2c_read_dm_block(struct bq27xxx_device_info *di,
					     int subclass)
{
	int ret = di->bus.write(di, BQ27XXX_REG_CTRL, 0, false);

	if (ret < 0)
		return ret;

	ret = di->bus.write(di, BQ27XXX_BLOCK_DATA_CONTROL, 0, true);
	if (ret < 0)
		return ret;

	ret = di->bus.write(di, BQ27XXX_BLOCK_DATA_CLASS, subclass, true);
	if (ret < 0)
		return ret;

	ret = di->bus.write(di, BQ27XXX_DATA_BLOCK, 0, true);
	if (ret < 0)
		return ret;

	usleep_range(1000, 1500);

	return di->bus.read_bulk(di, BQ27XXX_BLOCK_DATA,
				(u8 *) &di->buffer, sizeof(di->buffer));
}

static int bq27xxx_battery_i2c_print_config(struct bq27xxx_device_info *di)
{
	struct bq27xxx_dm_regs *reg = bq27xxx_dm_subclass_regs[di->chip];
	int ret, i;

	ret = bq27xxx_battery_i2c_read_dm_block(di,
			BQ27XXX_GAS_GAUGING_STATE_SUBCLASS);
	if (ret < 0)
		return ret;

	for (i = 0; i < BQ27XXX_NUM_IDX; i++) {
		int val;

		if (reg->subclass_id != BQ27XXX_GAS_GAUGING_STATE_SUBCLASS)
			continue;

		val = be16_to_cpup((u16 *) &di->buffer[reg->offset]);

		dev_info(di->dev, "settings for %s set at %d\n", reg->name, val);

		reg++;
	}

	return 0;
}

static bool bq27xxx_battery_update_dm_setting(struct bq27xxx_device_info *di,
			      unsigned int reg, unsigned int val)
{
	struct bq27xxx_dm_regs *dm_reg = &bq27xxx_dm_subclass_regs[di->chip][reg];
	u16 *prev = (u16 *) &di->buffer[dm_reg->offset];

	if (be16_to_cpup(prev) == val)
		return false;

	*prev = cpu_to_be16(val);

	return true;
}

static u8 bq27xxx_battery_checksum(struct bq27xxx_device_info *di)
{
	u8 *data = (u8 *) &di->buffer;
	u16 sum = 0;
	int i;

	for (i = 0; i < sizeof(di->buffer); i++) {
		sum += data[i];
		sum &= 0xff;
	}

	return 0xff - sum;
}

static int bq27xxx_battery_i2c_write_nvram(struct bq27xxx_device_info *di,
					   unsigned int subclass)
{
	int ret;

	ret = di->bus.write(di, BQ27XXX_REG_CTRL, BQ27XXX_SET_CFGUPDATE, false);
	if (ret)
		return ret;

	ret = di->bus.write(di, BQ27XXX_BLOCK_DATA_CONTROL, 0, true);
	if (ret)
		return ret;

	ret = di->bus.write(di, BQ27XXX_BLOCK_DATA_CLASS, subclass, true);
	if (ret)
		return ret;

	ret = di->bus.write(di, BQ27XXX_DATA_BLOCK, 0, true);
	if (ret)
		return ret;

	ret = di->bus.write_bulk(di, BQ27XXX_BLOCK_DATA,
				(u8 *) &di->buffer, sizeof(di->buffer));
	if (ret < 0)
		return ret;

	usleep_range(1000, 1500);

	di->bus.write(di, BQ27XXX_BLOCK_DATA_CHECKSUM,
				bq27xxx_battery_checksum(di), true);

	usleep_range(1000, 1500);

	di->bus.write(di, BQ27XXX_REG_CTRL, BQ27XXX_SOFT_RESET, false);

	return 0;
}

static int bq27xxx_battery_i2c_set_config(struct bq27xxx_device_info *di,
					  unsigned int cap, unsigned int energy,
					  unsigned int voltage)
{
	int ret = bq27xxx_battery_i2c_read_dm_block(di,
				BQ27XXX_GAS_GAUGING_STATE_SUBCLASS);

	if (ret < 0)
		return ret;

	ret  = bq27xxx_battery_update_dm_setting(di, BQ27XXX_DM_DESIGN_CAP, cap);
	ret |= bq27xxx_battery_update_dm_setting(di, BQ27XXX_DM_DESIGN_ENERGY,
						 energy);
	ret |= bq27xxx_battery_update_dm_setting(di, BQ27XXX_DM_TERMINATE_VOLTAGE,
						 voltage);

	if (ret) {
		dev_info(di->dev, "updating NVM settings\n");
		return bq27xxx_battery_i2c_write_nvram(di,
				BQ27XXX_GAS_GAUGING_STATE_SUBCLASS);
	}

	return 0;
}

static int bq27xxx_battery_i2c_parse_dt(struct bq27xxx_device_info *di)
{
	struct device_node *np = di->dev->of_node;
	int cap, energy, voltage = -EINVAL;
	int ret = 0;

	/* no settings to be set for this chipset so abort */
	if (!bq27xxx_dm_subclass_regs[di->chip])
		return 0;

	bq27xxx_battery_i2c_set_seal_state(di, false);

	if (np) {
		ret = of_property_read_u32(np, "ti,design-capacity", &cap);
		if (ret < 0 || cap > 0x7fff) {
			if (!ret)
				dev_err(di->dev,
					"invalid ti,design-capacity %d\n",
					cap);
			cap = -EINVAL;
		}

		ret = of_property_read_u32(np, "ti,design-energy", &energy);
		if (ret < 0 || energy > 0x7fff) {
			if (!ret)
				dev_err(di->dev,
					"invalid ti,design-energy %d\n",
					energy);
			energy = -EINVAL;
		}

		ret = of_property_read_u32(np, "ti,terminate-voltage", &voltage);
		if (ret < 0 || voltage < BQ27XXX_TERM_V_MIN
			    || voltage > BQ27XXX_TERM_V_MAX) {
			if (!ret)
				dev_err(di->dev,
					"invalid ti,terminate-voltage %d\n",
					voltage);
			voltage = -EINVAL;
		}

		/* assume that we want the defaults */
		if (cap < 0 && energy < 0 && voltage < 0) {
			ret = 0;
			goto out;
		}

		/* we need all three settings for safety reasons */
		if (cap < 0 || energy < 0 || voltage < 0) {
			dev_err(di->dev, "missing or invalid devicetree values;"
					 "NVM not updated\n");
			ret = -EINVAL;
			goto out;
		}

		ret = bq27xxx_battery_i2c_set_config(di, cap, energy, voltage);
	}

out:
	bq27xxx_battery_i2c_print_config(di);
	bq27xxx_battery_i2c_set_seal_state(di, true);

	return ret;
}

static int bq27xxx_battery_i2c_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	struct bq27xxx_device_info *di;
	int ret;
	char *name;
	int num;

	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (num < 0)
		return num;

	name = devm_kasprintf(&client->dev, GFP_KERNEL, "%s-%d", id->name, num);
	if (!name)
		goto err_mem;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		goto err_mem;

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->name = name;

	di->bus.read = bq27xxx_battery_i2c_read;
	di->bus.write = bq27xxx_battery_i2c_write;
	di->bus.read_bulk = bq27xxx_battery_i2c_bulk_read;
	di->bus.write_bulk = bq27xxx_battery_i2c_bulk_write;

	ret = bq27xxx_battery_i2c_parse_dt(di);
	if (ret)
		goto err_failed;

	ret = bq27xxx_battery_setup(di);
	if (ret)
		goto err_failed;

	/* Schedule a polling after about 1 min */
	schedule_delayed_work(&di->work, 60 * HZ);

	i2c_set_clientdata(client, di);

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, bq27xxx_battery_irq_handler_thread,
				IRQF_ONESHOT,
				di->name, di);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register IRQ %d error %d\n",
				client->irq, ret);
			return ret;
		}
	}

	return 0;

err_mem:
	ret = -ENOMEM;

err_failed:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return ret;
}

static int bq27xxx_battery_i2c_remove(struct i2c_client *client)
{
	struct bq27xxx_device_info *di = i2c_get_clientdata(client);

	bq27xxx_battery_teardown(di);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);

	return 0;
}

static const struct i2c_device_id bq27xxx_i2c_id_table[] = {
	{ "bq27200", BQ27000 },
	{ "bq27210", BQ27010 },
	{ "bq27500", BQ27500 },
	{ "bq27510", BQ27500 },
	{ "bq27520", BQ27500 },
	{ "bq27530", BQ27530 },
	{ "bq27531", BQ27530 },
	{ "bq27541", BQ27541 },
	{ "bq27542", BQ27541 },
	{ "bq27546", BQ27541 },
	{ "bq27742", BQ27541 },
	{ "bq27545", BQ27545 },
	{ "bq27421", BQ27421 },
	{ "bq27441", BQ27421 },
	{ "bq27621", BQ27421 },
	{ "bq27425", BQ27425 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27xxx_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id bq27xxx_battery_i2c_of_match_table[] = {
	{ .compatible = "ti,bq27200" },
	{ .compatible = "ti,bq27210" },
	{ .compatible = "ti,bq27500" },
	{ .compatible = "ti,bq27510" },
	{ .compatible = "ti,bq27520" },
	{ .compatible = "ti,bq27530" },
	{ .compatible = "ti,bq27531" },
	{ .compatible = "ti,bq27541" },
	{ .compatible = "ti,bq27542" },
	{ .compatible = "ti,bq27546" },
	{ .compatible = "ti,bq27742" },
	{ .compatible = "ti,bq27545" },
	{ .compatible = "ti,bq27421" },
	{ .compatible = "ti,bq27425" },
	{ .compatible = "ti,bq27441" },
	{ .compatible = "ti,bq27621" },
	{},
};
MODULE_DEVICE_TABLE(of, bq27xxx_battery_i2c_of_match_table);
#endif

static struct i2c_driver bq27xxx_battery_i2c_driver = {
	.driver = {
		.name = "bq27xxx-battery",
		.of_match_table = of_match_ptr(bq27xxx_battery_i2c_of_match_table),
	},
	.probe = bq27xxx_battery_i2c_probe,
	.remove = bq27xxx_battery_i2c_remove,
	.id_table = bq27xxx_i2c_id_table,
};
module_i2c_driver(bq27xxx_battery_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("BQ27xxx battery monitor i2c driver");
MODULE_LICENSE("GPL");
