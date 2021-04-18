#include "host/hcd.h"
#include "portable/ehci/ehci.h"
#include "portable/ehci/ehci_api.h"
#include "agrv2k.h"

// Initialize controller to host mode
bool hcd_init(uint8_t rhport)
{
  SYS_EnableAHBClock(AHB_MASK_USB0);
  USB_InitHost();
  USB_SetIntThreshold(USB_INT_THRESHOLD0); // Default threshold is 8. A smaller number can improve performance at the cost of cpu time
#if CFG_TUD_ENABLED
  USB_OtgEnableIntID();
#endif

  return ehci_init(rhport, (uint32_t)&USB0->CAPLENGTH, (uint32_t)&USB0->USBCMD);
}

void hcd_int_enable (uint8_t rhport)
{
  INT_EnableIRQ(USB0_IRQn, PLIC_MAX_PRIORITY);
}

void hcd_int_disable(uint8_t rhport)
{
  INT_DisableIRQ(USB0_IRQn);
}
