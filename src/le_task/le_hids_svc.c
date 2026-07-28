#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/led.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(le_hids_svc, LOG_LEVEL_DBG);

#include "le_hids_svc.h"

static struct hids_info info = {
	.version = 0x0000,
	.code = 0x00,
	.flags = HIDS_NORMALLY_CONNECTABLE,
};

static struct hids_report input = {
	.id = 0x01,
	.type = HIDS_INPUT,
};

static uint8_t simulate_input;
static uint8_t ctrl_point;
static uint8_t report_map[] = {
	0x05, 0x0C, /* Usage Page (Consumer Devices) */
	0x09, 0x01, /* Usage (Consumer Control) */
	0xA1, 0x01, /* Collection (Application) */
	0x85, 0x01, /*   Report Id (1) */
	0x15, 0x00, /*   Logical Minimum (0) */
	0x25, 0x01, /*   Logical Maximum (1) */
	0x75, 0x01, /*   Report Size (1) */
	0x95, 0x02, /*   Report Count (2) */
	0x09, 0xE9, /*   Usage (Volume Increment) */
	0x09, 0xEA, /*   Usage (Volume Decrement) */
	0x81, 0x02, /*   Input (Data,Var,Abs,No Wrap,Linear,...) */
	0x75, 0x06, /*   Report Size (6) */
	0x95, 0x01, /*   Report Count (1) */
	0x81, 0x03, /*   Input (Const,Var,Abs,No Wrap,Linear,...) */
	0xC0,       /* End Collection */
};

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	simulate_input = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(ctrl_point)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
	le_hids_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info,
			       NULL, &info),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
	BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report, NULL, &input),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point), );

#define LED0_NODE    DT_ALIAS(led0)
#define BUTTONS_NODE DT_PATH(buttons)

static const struct led_dt_spec led0 = LED_DT_SPEC_GET(LED0_NODE);

/* Current pressed-button bitmask (bit 0 = Volume+, bit 1 = Volume-) */
static uint8_t buttons_state;

static void button_input_cb(struct input_event *evt, void *user_data)
{
	uint8_t btn_bit;

	if (evt->sync == 0) {
		return;
	}

	LOG_DBG("Button %d %s at %" PRIu32, evt->code, evt->value ? "pressed" : "released",
		k_cycle_get_32());

	if (led0.dev != NULL) {
		led_set_brightness_dt(&led0, evt->value ? 100 : 0);
	}

	/* Map input key code to HID consumer control bit (Volume+/Volume-) */
	switch (evt->code) {
	case INPUT_KEY_0:
		btn_bit = BIT(0); /* Volume Increment */
		break;
	case INPUT_KEY_1:
		btn_bit = BIT(1); /* Volume Decrement */
		break;
	default:
		/* Key not mapped to a HID button */
		return;
	}

	if (evt->value) {
		buttons_state |= btn_bit;
	} else {
		buttons_state &= ~btn_bit;
	}

	/* Only notify when host has enabled CCC */
	if (!simulate_input) {
		LOG_INF("simulate_input is disabled");
		return;
	}

	/* HID Report (Report ID 1, Consumer Control):
	 * Byte 0: bit 0 = Volume+, bit 1 = Volume-, bits 2..7 = padding
	 * Note: Report ID is not included in BLE HOGP notifications, it is
	 *       bound via the Report Reference Descriptor (0x2908).
	 */
	uint8_t report[1] = {buttons_state};

	bt_gatt_notify_uuid(NULL, BT_UUID_HIDS_REPORT, le_hids_svc.attrs, report, sizeof(report));
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(BUTTONS_NODE), button_input_cb, NULL);
