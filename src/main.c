/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
    int ret;
    bool led_state = true;
    
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led0)) {
		return 0;
	}

	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0) {
		return 0;
	}

    while(1)
    {
		ret = gpio_pin_toggle_dt(&led0);
		if (ret < 0) {
			return 0;
		}

        led_state = !led_state;
		printf("LED state: %s\n", led_state ? "ON" : "OFF");

        k_sleep(K_MSEC(1000));
    }

	return 0;
}
