#ifndef _FXI_SYSFS_H
#define _FXI_SYSFS_H

#define LED_RED		0
#define LED_GREEN	1
#define LED_BLUE	2
#define LED_NETRED	3
#define LED_NETGREEN	4

#define LED_OFF		0
#define LED_ON		1

void fxi_led_control(int led, int val);


#endif
