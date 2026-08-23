#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(le_hts_svc, LOG_LEVEL_DBG);

/* Temperature Measurement: flags + IEEE-11073 32-bit float (3B mantissa + 1B exponent) */
struct temp_measurement {
	/* bit0: 0=Celsius 1=Fahrenheit, 
	* bit1: timestamp present, bit2: type present
	*/
	uint8_t flags; 
	uint8_t temp[4];
} __packed;

static volatile bool ht_notify_enabled = false;
static struct temp_measurement measurement = {
	.flags = 0x00, /* Celsius, no timestamp, no type */
};
static const uint8_t temp_type = 2; /* 1=Armpit 2=Body(general) 3=Ear ... 9=Tympanum */

/* value = val1 + val2 * 1e-6 (degC), encode as mantissa * 10^-2 */
static void encode_temp(const struct sensor_value *val)
{
	int32_t mantissa = val->val1 * 100 + val->val2 / 10000;

	measurement.temp[0] = mantissa & 0xFF;
	measurement.temp[1] = (mantissa >> 8) & 0xFF;
	measurement.temp[2] = (mantissa >> 16) & 0xFF;
	measurement.temp[3] = 0xFE; /* exponent = -2 */
}

static ssize_t read_measurement(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &measurement, sizeof(measurement));
}

static ssize_t read_temp_type(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &temp_type, sizeof(temp_type));
}

static void measurement_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ht_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* Health Thermometer Service */
BT_GATT_SERVICE_DEFINE(
	le_hts_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HTS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HTS_MEASUREMENT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, read_measurement, NULL, NULL),
	BT_GATT_CCC(measurement_ccc_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_HTS_TEMP_TYP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_temp_type, NULL, NULL), );

/* SHT3X sensor thread */
#define SHT3X_THREAD_SIZE 1024
#define SHT3X_THREAD_PRIO 7

static const struct device *sht3x = DEVICE_DT_GET(DT_NODELABEL(sht3xd));
static struct sensor_value temp, hum;

static void sht3x_thread(void *p1, void *p2, void *p3)
{
	if (!device_is_ready(sht3x)) {
		LOG_ERR("SHT3X device not ready");
		return;
	}

	while (1) {
		if (sensor_sample_fetch(sht3x) == 0) {
			sensor_channel_get(sht3x, SENSOR_CHAN_AMBIENT_TEMP, &temp);
			sensor_channel_get(sht3x, SENSOR_CHAN_HUMIDITY, &hum);
			encode_temp(&temp);

			// LOG_INF("Temp: %d.%06d degC, Humidity: %d.%06d %%", temp.val1, temp.val2,
			// 	hum.val1, hum.val2);

			if (ht_notify_enabled) {
				bt_gatt_notify_uuid(NULL, BT_UUID_HTS_MEASUREMENT, le_hts_svc.attrs, &measurement, sizeof(measurement));
			}
		} else {
			LOG_ERR("SHT3X sample fetch failed");
		}

		k_sleep(K_MSEC(1000));
	}
}

K_THREAD_DEFINE(sht3x_tid, SHT3X_THREAD_SIZE, sht3x_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(SHT3X_THREAD_PRIO), 0, 0);
