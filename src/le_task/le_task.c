#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_task, LOG_LEVEL_DBG);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL), BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct bt_conn *pending_unpaired_conn = NULL;

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
	} else {
		LOG_INF("Connected");
	}

	if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
		LOG_ERR("Failed to set security");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected, reason 0x%02x %s", reason, bt_hci_err_to_str(reason));

	if (pending_unpaired_conn == conn) {
		int err = bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
		if (err) {
			LOG_ERR("Failed to unpair, err %d", err);
		} else {
			LOG_INF("Bond removed");
		}

		bt_conn_unref(pending_unpaired_conn);

		pending_unpaired_conn = NULL;
	}
}

static void recycled(void)
{
	int err;

	LOG_INF("recycled connection object resource, restart adv!");
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	/*
	printk("Connection parameter update request:\n"
			"  interval: %u-%u (%.2f-%.2f ms)\n"
			"  latency:  %u\n"
			"  timeout:  %u (%.0f ms)\n",
			param->interval_min, param->interval_max,
			(double)param->interval_min * 1.25, (double)param->interval_max * 1.25,
			param->latency,
			param->timeout, (double)param->timeout * 10);
	*/
	/*
	if (param->interval_min < 16) {
		LOG_WRN("Rejected: interval too short");
		return false;
	}

	if (param->latency > 10) {
		LOG_WRN("Rejected: latency too high");
		return false;
	}
	 */

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	LOG_DBG("Connection parameter updated: \r\n"
		"  interval: %u (%.2f ms)\r\n"
		"  latency:  %u\r\n"
		"  timeout:  %u (%.0f ms)",
		interval, (double)interval * 1.25, latency, timeout, (double)timeout * 10);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));

	if (err) {
		LOG_ERR("Security change failed, err 0x%02x %s, peer: %s", err,
			bt_security_err_to_str(err), addr);

		pending_unpaired_conn = bt_conn_ref(conn);
		bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	} else {
		LOG_INF("Security level %d, peer: %s", level, addr);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated,
	.security_changed = security_changed,
};

enum bt_security_err pairing_accept(struct bt_conn *conn,
				    const struct bt_conn_pairing_feat *const feat)
{
	LOG_DBG("  IO caps:     %u\r\n"
		"  OOB data:    %u\r\n"
		"  Auth req:    0x%02x\r\n"
		"  Max key sz:  %u\r\n"
		"  Init key:    0x%02x\r\n"
		"  Resp key:    0x%02x",
		feat->io_capability, feat->oob_data_flag, feat->auth_req, feat->max_enc_key_size,
		feat->init_key_dist, feat->resp_key_dist);

	if (feat->max_enc_key_size < 16) {
		LOG_ERR("Rejected: key size too small");
		return BT_SECURITY_ERR_AUTH_REQUIREMENT;
	}

	return BT_SECURITY_ERR_SUCCESS;
}

static void auth_cancel(struct bt_conn *conn)
{
	LOG_WRN("Pairing cancelled!");
}

static struct bt_conn_auth_cb auth_cb_display = {
	.pairing_accept = pairing_accept,
	.passkey_display = NULL,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	LOG_INF("Pairing complete, bonded: %d, peer: %s", bonded, addr);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	LOG_WRN("Pairing failed with %s, reason 0x%02x %s", addr, reason,
		bt_security_err_to_str(reason));

	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
	.bond_deleted = NULL,
};

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	err = settings_load();
	if (err) {
		LOG_ERR("Failed to load settings (err %d)", err);
	} else {
		LOG_INF("Settings loaded");
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static int le_task_init(void)
{
	int err;

	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	bt_conn_auth_cb_register(&auth_cb_display);
	bt_conn_auth_info_cb_register(&auth_info_cb);

	return 0;
}
SYS_INIT(le_task_init, APPLICATION, 90);
