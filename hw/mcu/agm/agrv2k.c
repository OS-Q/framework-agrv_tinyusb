#include "agrv2k.h"
#include "device/dcd.h"
#include "host/hcd.h"

USB_DataTypeDef usb_data TU_ATTR_ALIGNED(4096) TU_ATTR_SECTION(".noinit");

void USB0_isr(void)
{
#if CFG_TUD_ENABLED
  if (!CFG_TUH_ENABLED || USB_OtgIsDevice()) {
    if (CFG_TUH_ENABLED && USB_GetControllerMode() != USB_MODE_DEIVCE) {
      USB_OtgClearIDStatus();
      dcd_init(0);
    }
    dcd_int_handler(0);
  }
#endif
#if CFG_TUH_ENABLED
  if (!CFG_TUD_ENABLED || USB_OtgIsHost()) {
    if (CFG_TUD_ENABLED && USB_GetControllerMode() != USB_MODE_HOST) {
      USB_OtgClearIDStatus();
      hcd_init(0);
    }
    if (USB_PortIsConnectStatusChange()) {
      UTIL_IdleMs(1);
    }
    hcd_int_handler(0);
  }
#endif
}
