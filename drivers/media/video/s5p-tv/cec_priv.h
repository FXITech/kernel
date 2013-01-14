/*
 * HDMI CEC Structures and Constants
 *
 * Copyright (c) 2012 NDS
 *
 * Abhijeet Dev <abhijeet@abhijeet-dev.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#ifndef _CEC_PRIV_H_
#define _CEC_PRIV_H_

/* Maximum CEC frame size */
#define CEC_MAX_FRAME_SIZE                16
/* Not valid CEC physical address */
#define CEC_NOT_VALID_PHYSICAL_ADDRESS    0xFFFF

/* CEC broadcast address (as destination address) */
#define CEC_MSG_BROADCAST        0x0F
/* CEC unregistered address (as initiator address) */
#define CEC_LADDR_UNREGISTERED   0x0F

#define CEC_POWER_STATUS_ON                 0x00
#define CEC_POWER_STATUS_STANDBY            0x01
#define CEC_POWER_STATUS_TRANSITION_ON      0x02
#define CEC_POWER_STATUS_TRANSITION_STANDBY 0x03

/*
 * CEC Messages
 */

/* Messages for the One Touch Play Feature */
#define CEC_OPCODE_ACTIVE_SOURCE            0x82
#define CEC_OPCODE_IMAGE_VIEW_ON            0x04
#define CEC_OPCODE_TEXT_VIEW_ON             0x0D

/* Messages for the Routing Control Feature */
#define CEC_OPCODE_INACTIVE_SOURCE          0x9D
#define CEC_OPCODE_REQUEST_ACTIVE_SOURCE    0x85
#define CEC_OPCODE_ROUTING_CHANGE           0x80
#define CEC_OPCODE_ROUTING_INFORMATION      0x81
#define CEC_OPCODE_SET_STREAM_PATH          0x86

/* Messages for the Standby Feature */
#define CEC_OPCODE_STANDBY                  0x36

/* Messages for the One Touch Record Feature */
#define CEC_OPCODE_RECORD_OFF               0x0B
#define CEC_OPCODE_RECORD_ON                0x09
#define CEC_OPCODE_RECORD_STATUS            0x0A
#define CEC_OPCODE_RECORD_TV_SCREEN         0x0F

/* Messages for the Timer Programming Feature */
#define CEC_OPCODE_CLEAR_ANALOGUE_TIMER     0x33
#define CEC_OPCODE_CLEAR_DIGITAL_TIMER      0x99
#define CEC_OPCODE_CLEAR_EXTERNAL_TIMER     0xA1
#define CEC_OPCODE_SET_ANALOGUE_TIMER       0x34
#define CEC_OPCODE_SET_DIGITAL_TIMER        0x97
#define CEC_OPCODE_SET_EXTERNAL_TIMER       0xA2
#define CEC_OPCODE_SET_TIMER_PROGRAM_TITLE  0x67
#define CEC_OPCODE_TIMER_CLEARED_STATUS     0x43
#define CEC_OPCODE_TIMER_STATUS             0x35

/* Messages for the System Information Feature */
#define CEC_OPCODE_CEC_VERSION              0x9E
#define CEC_OPCODE_GET_CEC_VERSION          0x9F
#define CEC_OPCODE_GIVE_PHYSICAL_ADDRESS    0x83
#define CEC_OPCODE_GET_MENU_LANGUAGE        0x91
/*#define CEC_OPCODE_POLLING_MESSAGE*/
#define CEC_OPCODE_REPORT_PHYSICAL_ADDRESS  0x84
#define CEC_OPCODE_SET_MENU_LANGUAGE        0x32

/* Messages for the Deck Control Feature */
#define CEC_OPCODE_DECK_CONTROL             0x42
#define CEC_OPCODE_DECK_STATUS              0x1B
#define CEC_OPCODE_GIVE_DECK_STATUS         0x1A
#define CEC_OPCODE_PLAY                     0x41

/* Messages for the Tuner Control Feature */
#define CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS 0x08
#define CEC_OPCODE_SELECT_ANALOGUE_SERVICE  0x92
#define CEC_OPCODE_SELECT_DIGITAL_SERVICE   0x93
#define CEC_OPCODE_TUNER_DEVICE_STATUS      0x07
#define CEC_OPCODE_TUNER_STEP_DECREMENT     0x06
#define CEC_OPCODE_TUNER_STEP_INCREMENT     0x05

/* Messages for the Vendor Specific Commands Feature */
#define CEC_OPCODE_DEVICE_VENDOR_ID         0x87
#define CEC_OPCODE_GET_DEVICE_VENDOR_ID     0x8C
#define CEC_OPCODE_VENDOR_COMMAND           0x89
#define CEC_OPCODE_VENDOR_COMMAND_WITH_ID   0xA0
#define CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN 0x8A
#define CEC_OPCODE_VENDOR_REMOVE_BUTTON_UP  0x8B

/* Messages for the OSD Display Feature */
#define CEC_OPCODE_SET_OSD_STRING           0x64

/* Messages for the Device OSD Transfer Feature */
#define CEC_OPCODE_GIVE_OSD_NAME            0x46
#define CEC_OPCODE_SET_OSD_NAME             0x47

/* Messages for the Device Menu Control Feature */
#define CEC_OPCODE_MENU_REQUEST             0x8D
#define CEC_OPCODE_MENU_STATUS              0x8E
#define CEC_OPCODE_USER_CONTROL_PRESSED     0x44
#define CEC_OPCODE_USER_CONTROL_RELEASED    0x45

/* Messages for the Remote Control Passthrough Feature */

/* Messages for the Power Status Feature */
#define CEC_OPCODE_GIVE_DEVICE_POWER_STATUS 0x8F
#define CEC_OPCODE_REPORT_POWER_STATUS      0x90

/* Messages for General Protocol messages */
#define CEC_OPCODE_FEATURE_ABORT            0x00
#define CEC_OPCODE_ABORT                    0xFF

/* Messages for the System Audio Control Feature */
#define CEC_OPCODE_GIVE_AUDIO_STATUS        0x71
#define CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS 0x7D
#define CEC_OPCODE_REPORT_AUDIO_STATUS      0x7A
#define CEC_OPCODE_SET_SYSTEM_AUDIO_MODE    0x72
#define CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST 0x70
#define CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS 0x7E

/* Messages for the Audio Rate Control Feature */
#define CEC_OPCODE_SET_AUDIO_RATE           0x9A


/* CEC Operands */
/*TODO: not finished*/

#define CEC_DECK_CONTROL_MODE_STOP      0x03
#define CEC_PLAY_MODE_PLAY_FORWARD      0x24

/**
 * CECDeviceType
 * Type of CEC device
 */
enum CECDeviceType {
    /** TV */
    CEC_DEVICE_TV,
    /** Recording Device */
    CEC_DEVICE_RECODER,
    /** Tuner */
    CEC_DEVICE_TUNER,
    /** Playback Device */
    CEC_DEVICE_PLAYER,
    /** Audio System */
    CEC_DEVICE_AUDIO,
};

#endif /* _CEC_PRIV_H_ */

