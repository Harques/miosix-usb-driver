#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for host can be defined by board.mk, default to port 0
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

// RHPort max operational speed can be defined by board.mk
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_STM32F4
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_CUSTOM
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        1
#endif

// Host-only build
#define CFG_TUD_ENABLED       0
#define CFG_TUH_ENABLED       1

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUH_DEVICE_MAX            4
#define CFG_TUH_ENUMERATION_BUFSIZE   256

//------------- CLASS -------------//
#define CFG_TUH_HUB              1
#define CFG_TUH_CDC              1
#define CFG_TUH_MSC              1
#define CFG_TUH_HID              0
#define CFG_TUH_VENDOR           0

// Keep host CDC buffers modest for FS operation
#define CFG_TUH_CDC_RX_BUFSIZE   64
#define CFG_TUH_CDC_TX_BUFSIZE   64

// STM32F4 OTG_FS host (RHPort0 = OTG_FS, Full Speed only).
// OTG_FS on STM32F407 is hardware Full Speed - do not add OPT_MODE_HIGH_SPEED here.
// If using OTG_HS+ULPI on RHPort1, change these accordingly.
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_NONE)

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
