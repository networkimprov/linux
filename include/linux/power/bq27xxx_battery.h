#ifndef __LINUX_BQ27X00_BATTERY_H__
#define __LINUX_BQ27X00_BATTERY_H__

enum bq27xxx_chip {
	/* categories; index for bq27xxx_regs[] */
	BQ27000 = 1, /* bq27000, bq27200 */
	BQ27010 = 2, /* bq27010, bq27210 */
	BQ27500 = 3, /* bq27500 */
	BQ27510 = 4, /* bq27510, bq27520 */
	BQ27530 = 5, /* bq27530, bq27531 */
	BQ27541 = 6, /* bq27541, bq27542, bq27546, bq27742 */
	BQ27545 = 7, /* bq27545 */
	BQ27421 = 8, /* bq27421, bq27425, bq27441, bq27621 */

	/* members of categories; translate these to category in _setup() */
	BQ27520 = 101,
	BQ27531 = 102,
	BQ27542 = 103,
	BQ27546 = 104,
	BQ27742 = 105,
	BQ27425 = 106,
	BQ27441 = 107,
	BQ27621 = 108,
};

/**
 * struct bq27xxx_plaform_data - Platform data for bq27xxx devices
 * @name: Name of the battery.
 * @chip: Chip class number of this device.
 * @read: HDQ read callback.
 *	This function should provide access to the HDQ bus the battery is
 *	connected to.
 *	The first parameter is a pointer to the battery device, the second the
 *	register to be read. The return value should either be the content of
 *	the passed register or an error value.
 */
struct bq27xxx_platform_data {
	const char *name;
	enum bq27xxx_chip chip;
	int (*read)(struct device *dev, unsigned int);
};

struct bq27xxx_device_info;
struct bq27xxx_access_methods {
	int (*read)(struct bq27xxx_device_info *di, u8 reg, bool single);
	int (*write)(struct bq27xxx_device_info *di, u8 reg, int value, bool single);
	int (*read_bulk)(struct bq27xxx_device_info *di, u8 reg, u8 *data, int len);
	int (*write_bulk)(struct bq27xxx_device_info *di, u8 reg, u8 *data, int len);
};

struct bq27xxx_reg_cache {
	int temperature;
	int time_to_empty;
	int time_to_empty_avg;
	int time_to_full;
	int charge_full;
	int cycle_count;
	int capacity;
	int energy;
	int flags;
	int power_avg;
	int health;
};

struct bq27xxx_device_info {
	struct device *dev;
	int id;
	enum bq27xxx_chip chip;
	const char *name;
	struct bq27xxx_dm_reg *dm_regs;
	u32 unseal_key;
	struct bq27xxx_access_methods bus;
	struct bq27xxx_reg_cache cache;
	int charge_design_full;
	unsigned long last_update;
	struct delayed_work work;
	struct power_supply *bat;
	struct list_head list;
	struct mutex lock;
	u8 *regs;
};

void bq27xxx_battery_update(struct bq27xxx_device_info *di);
int bq27xxx_battery_setup(struct bq27xxx_device_info *di);
void bq27xxx_battery_teardown(struct bq27xxx_device_info *di);

#endif
