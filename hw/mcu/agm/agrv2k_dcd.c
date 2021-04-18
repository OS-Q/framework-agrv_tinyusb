#include "device/dcd.h"
#include "agrv2k.h"

#if TUSB_OPT_DEVICE_ENABLED

void (*dcd_sof_cb)(uint32_t status);

// Initialize controller to device mode
void dcd_init(uint8_t rhport)
{
  tu_memclr(&usb_data, sizeof(USB_DataTypeDef));

  SYS_EnableAHBClock(AHB_MASK_USB0);
  USB_InitDevice();
  USB_SetEndPointListAddr((uint32_t)&usb_data);
  USB_SetIntThreshold(USB_INT_THRESHOLD0); // Default threshold is 8. A smaller number can improve performance at the cost of cpu time
#if CFG_TUH_ENABLED
  USB_OtgEnableIntID();
#endif
  if (dcd_sof_cb) {
    USB_EnableInt(USB_INT_SRI);
  }
}

void dcd_int_handler(uint8_t rhport)
{
  const uint32_t status = USB_GetStatus() & USB_GetEnabledInt();
  USB_ClearStatus(status);
  if (!status) {
    return;
  }

  if (dcd_sof_cb && (status & USB_INT_SRI)) {
    dcd_sof_cb(status);
  }

  if (status & USB_INT_URI) {
    USB_BusReset();

    tu_memclr(&usb_data, sizeof(USB_DataTypeDef));
    // Set up control endpoints 0 and 1
    for (int i = 0; i < 2; ++i) {
      USB_InitDQH(&usb_data.dQH[i]);
    }
    usb_data.dQH[0].IntOnSetup = true; // For OUT only
    dcd_event_bus_reset(rhport, (tusb_speed_t)USB_PortGetSpeed(), true);
    dcd_event_bus_signal(rhport, DCD_EVENT_UNPLUGGED, true);
  }

  if (status & USB_INT_UI) {
    const uint32_t endpt_complete = USB_GetEndPtComplete();
    USB_ClearEndPtComplete(endpt_complete);

    const uint32_t endpt_setupstatus = USB_GetEndPtSetupStatus();
    if (endpt_setupstatus) {
      USB_ClearEndPtSetupStatus(endpt_setupstatus);
      dcd_event_setup_received(rhport, (uint8_t *)(uintptr_t)usb_data.dQH[0].SetupBuffer, true);
    }
    if (endpt_complete) {
      for(uint8_t ep_idx = 0; ep_idx < QHD_MAX; ep_idx++) {
        if (tu_bit_test(endpt_complete, dcd_ep_idx2bit(ep_idx))) {
          USB_DTD *dTD = &usb_data.dTD[ep_idx];
          uint8_t result = dTD->Halted ? XFER_RESULT_STALLED : (dTD->TranError || dTD->DataError) ? XFER_RESULT_FAILED : XFER_RESULT_SUCCESS;

          const uint8_t ep_addr = tu_edpt_addr(ep_idx / 2, ep_idx & 0x01);
          dcd_event_xfer_complete(rhport, ep_addr, dTD->ExpectedBytes - dTD->TotalBytes, result, true);
        }
      }
    }
  }

  if (status & USB_INT_SLI) {
    if (USB_PortIsSuspended() && USB_GetDeviceAddress()) {
      dcd_event_bus_signal(rhport, DCD_EVENT_SUSPEND, true);
    }
    dcd_event_bus_signal(rhport, DCD_EVENT_UNPLUGGED, true);
  }
  if (status & USB_INT_PCI) {
    if (!USB_PortIsSuspended()) {
      dcd_event_bus_signal(rhport, DCD_EVENT_RESUME, true);
    }
  }
}

void dcd_int_enable (uint8_t rhport)
{
  INT_EnableIRQ(USB0_IRQn, PLIC_MAX_PRIORITY);
}

void dcd_int_disable(uint8_t rhport)
{
  INT_DisableIRQ(USB0_IRQn);
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
  // Response with status first before changing device address
  dcd_edpt_xfer(rhport, tu_edpt_addr(0, TUSB_DIR_IN), NULL, 0);
  USB_SetDeviceAddress(dev_addr);
}

void dcd_remote_wakeup(uint8_t rhport)
{
  USB_PortForceResume();
}

void dcd_connect(uint8_t rhport)
{
  USB_Run();
}

void dcd_disconnect(uint8_t rhport)
{
  USB_Stop();
}

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *p_endpoint_desc)
{
  TU_VERIFY(p_endpoint_desc->bmAttributes.xfer != TUSB_XFER_ISOCHRONOUS); // TODO: support ISO

  const uint8_t epnum  = tu_edpt_number(p_endpoint_desc->bEndpointAddress);
  const uint8_t dir    = tu_edpt_dir(p_endpoint_desc->bEndpointAddress);
  const uint8_t ep_idx = dcd_ep_num2idx(epnum, dir);
  if (epnum >= USB_ENDPOINTS) {
    return true;
  }

  USB_DQH *dQH = &usb_data.dQH[ep_idx];
  USB_InitDQH(dQH);
  
  USB_SetEndPtType(epnum, dir, p_endpoint_desc->bmAttributes.xfer << 2);
  USB_ResetEndPtDataToggle(epnum, dir);
  USB_EnableEndPt(epnum, dir);

  return true;
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes)
{
  const uint8_t epnum  = tu_edpt_number(ep_addr);
  const uint8_t dir    = tu_edpt_dir(ep_addr);
  const uint8_t ep_idx = dcd_ep_num2idx(epnum, dir);
  if (epnum >= USB_ENDPOINTS) {
    return true;
  }

  if (epnum == 0) {
    while(USB->ENDPTSETUPSTAT & TU_BIT(0));
  }

  USB_DQH *dQH = &usb_data.dQH[ep_idx];
  USB_DTD *dTD = &usb_data.dTD[ep_idx];
  USB_InitDTD(dTD, buffer, total_bytes);
  dTD->IntOnComplete = true;
  dQH->Overlay.Next = (uint32_t)dTD; // Link dTD to dQH
  dQH->Overlay.Active = dQH->Overlay.Halted = 0; // In case they are set from previous error
  USB_SetEndPtPrime(TU_BIT(dcd_ep_idx2bit(ep_idx))); // Start transfer
  return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
  uint8_t const epnum  = tu_edpt_number(ep_addr);
  uint8_t const dir    = tu_edpt_dir(ep_addr);
  if (epnum >= USB_ENDPOINTS) {
    return;
  }

  USB_StallEndPt(epnum, dir);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
  uint8_t const epnum  = tu_edpt_number(ep_addr);
  uint8_t const dir    = tu_edpt_dir(ep_addr);
  if (epnum >= USB_ENDPOINTS) {
    return;
  }

  USB_ClearEndPtStall     (epnum, dir);
  USB_ResetEndPtDataToggle(epnum, dir);
}

void dcd_edpt_close_all(uint8_t rhport)
{
  // Disable all non-control endpoints
  for(uint8_t ep_idx = 2; ep_idx < QHD_MAX; ep_idx++) {
    usb_data.dQH[ep_idx].Overlay.Halted = 1;
    USB_SetEndPtFlush(dcd_ep_idx2bit(ep_idx));
  }
}

#endif
