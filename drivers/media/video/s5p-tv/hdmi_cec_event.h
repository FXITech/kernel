/* linux/drivers/media/video/s5p-tv/hdmi_cec.h
 *
 * Copyright (c) 2012 NDS
 *
 * Abhijeet Dev <abhijeet@abhijeet-dev.net>
 *
 * Header file for interface of CEC operations for HDMI controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SAMSUNG_TVOUT_HDMI_CEC_H_
#define _SAMSUNG_TVOUT_HDMI_CEC_H_ __FILE__

/* Starts CEC event processing thread */
void hdmi_cec_start(void);
/* Stops CEC event processing thread */
void hdmi_cec_stop(void);
/* Handles a CEC event received by vendor CEC module */
void hdmi_cec_event_rx(void);

#endif				/* _SAMSUNG_TVOUT_HDMI_CEC_H_ */
