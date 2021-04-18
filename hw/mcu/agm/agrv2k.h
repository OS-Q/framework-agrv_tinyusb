#ifndef AGRV2K_H
#define AGRV2K_H

#include "common/tusb_common.h"
#include "alta.h"

#define QHD_MAX (USB_ENDPOINTS * 2)
typedef union {
#if TUSB_OPT_DEVICE_ENABLED
  struct {
    USB_DQH dQH[QHD_MAX] TU_ATTR_ALIGNED(64);
    USB_DTD dTD[QHD_MAX] TU_ATTR_ALIGNED(32); // 1 TD for each QH
  };
#endif
#if TUSB_OPT_HOST_ENABLED
  // Host controller data is defined in portable/ehci/ehci.h
#endif
} USB_DataTypeDef;

#if TUSB_OPT_DEVICE_ENABLED
static inline uint8_t dcd_ep_idx2bit(uint8_t ep_idx)
{
  return ep_idx / 2 + ((ep_idx % 2) ? 16 : 0);
}

static inline uint8_t dcd_ep_num2idx(uint8_t epnum, uint8_t dir)
{
  return 2 * epnum + dir;
}

extern void (*dcd_sof_cb)(uint32_t status);
#endif

extern USB_DataTypeDef usb_data;

#endif
