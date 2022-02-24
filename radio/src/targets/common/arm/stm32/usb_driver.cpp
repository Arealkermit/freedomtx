/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#if defined(__cplusplus)
extern "C" {
#endif
#include "usb_dcd_int.h"
#include "usb_bsp.h"
#if defined(__cplusplus)
}
#endif

#include "opentx.h"
#include "debug.h"

#if defined(AGENT)
#include "io/crsf/crsf.h"
#endif

static bool usbDriverStarted = false;
#if defined(BOOT)
static usbMode selectedUsbMode = USB_MASS_STORAGE_MODE;
#else
#if defined(AGENT)
static usbMode selectedUsbMode = USB_AGENT_MODE;
#else
static usbMode selectedUsbMode = USB_UNSELECTED_MODE;
#endif
#endif

int getSelectedUsbMode()
{
  return selectedUsbMode;
}

void setSelectedUsbMode(int mode)
{
#if defined(AGENT)
  usbMode selectedUsbModePrev = selectedUsbMode;
#endif
  selectedUsbMode = usbMode(mode);

#if defined(AGENT)
  // for disconnecting usb from host without unplugging
  if(selectedUsbModePrev != selectedUsbMode){
    usbStop();
  }
#endif
}

int usbPlugged()
{
  static PinDebounce debounce;
  return debounce.debounce(USB_GPIO, USB_GPIO_PIN_VBUS);
}

USB_OTG_CORE_HANDLE USB_OTG_dev;

extern "C" void OTG_FS_IRQHandler()
{
  DEBUG_INTERRUPT(INT_OTG_FS);
  USBD_OTG_ISR_Handler(&USB_OTG_dev);
}

void usbInit()
{
  // Initialize hardware
  USB_OTG_BSP_Init(&USB_OTG_dev);
  usbDriverStarted = false;
}

void usbStart()
{
  switch (getSelectedUsbMode()) {
#if !defined(BOOT)
    case USB_JOYSTICK_MODE:
      // initialize USB as HID device
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_HID_cb, &USR_cb);
      break;
#endif
#if defined(AGENT) && !defined(BOOT)
    case USB_AGENT_MODE:
      // initialize USB as HID device
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_AGENT_cb, &USR_cb);
      break;
    case USB_CHARGING_MODE:
      // don't initialize USB in charging mode
      break;
#endif
#if defined(USB_SERIAL)
    case USB_SERIAL_MODE:
      // initialize USB as CDC device (virtual serial port)
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_CDC_cb, &USR_cb);
      break;
#endif
    default:
    case USB_MASS_STORAGE_MODE:
      // initialize USB as MSC device
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_MSC_cb, &USR_cb);
      break;
  }
  usbDriverStarted = true;
}

void usbStop()
{
#if defined(AGENT)
  // facilitate switching usb mode in runtime
  USB_OTG_WRITE_REG32(&USB_OTG_dev.regs.GREGS->GCCFG, 0);
#endif
  usbDriverStarted = false;
  USBD_DeInit(&USB_OTG_dev);
}

bool usbStarted()
{
  return usbDriverStarted;
}

#if !defined(BOOT)
/*
  Prepare and send new USB data packet

  The format of HID_Buffer is defined by
  USB endpoint description can be found in
  file usb_hid_joystick.c, variable HID_JOYSTICK_ReportDesc
*/
void usbJoystickUpdate()
{
  static uint8_t HID_Buffer[HID_IN_PACKET];

  // test to se if TX buffer is free
  if (USBD_HID_SendReport(&USB_OTG_dev, 0, 0) == USBD_OK) {
    //buttons
    HID_Buffer[0] = 0;
    HID_Buffer[1] = 0;
    HID_Buffer[2] = 0;
    for (int i = 0; i < 8; ++i) {
      if ( channelOutputs[i+8] > 0 ) {
        HID_Buffer[0] |= (1 << i);
      }
      if ( channelOutputs[i+16] > 0 ) {
        HID_Buffer[1] |= (1 << i);
      }
      if ( channelOutputs[i+24] > 0 ) {
        HID_Buffer[2] |= (1 << i);
      }
    }

    //analog values
    //uint8_t * p = HID_Buffer + 1;
    for (int i = 0; i < 8; ++i) {

      int16_t value = channelOutputs[i] + 1024;
      if ( value > 2047 ) value = 2047;
      else if ( value < 0 ) value = 0;
      HID_Buffer[i*2 +3] = static_cast<uint8_t>(value & 0xFF);
      HID_Buffer[i*2 +4] = static_cast<uint8_t>((value >> 8) & 0x07);

    }
    USBD_HID_SendReport(&USB_OTG_dev, HID_Buffer, HID_IN_PACKET);
  }
}

#if defined(AGENT)
#define USB_HID_FIFO_SIZE                       128
#define USB_HID_INEP1_NUM                       1
#define USB_HID_INEP1_TXFIFO_EMPTY              0x80U
#define USB_HID_INEP1_TXFIFO_FREE_WORD          0x80U
#define USB_HID_INEP1_TXFIFO_FREE_WORD_MASK     0xFFFFU
#define LIBCRSF_BF_LINK_STATISTICS              0x14

static Fifo<uint8_t, USB_HID_FIFO_SIZE> *hidTxFifo = 0;

void usbAgentWrite( uint8_t *pData )
{
  static uint8_t HID_Buffer[HID_AGENT_IN_PACKET];
  memcpy(HID_Buffer, pData, HID_AGENT_IN_PACKET);
  USBD_AGENT_SendReport(&USB_OTG_dev, HID_Buffer, HID_AGENT_IN_PACKET);
}

static uint8_t isUsbIdle(){
  extern uint8_t ReportSent;
  return ReportSent;
  // workaround for usb stuck during send data but the host app is not ready to receive.
  uint8_t idle = (ReportSent &&
                (USB_OTG_dev.regs.INEP_REGS[USB_HID_INEP1_NUM]->DIEPINT & USB_HID_INEP1_TXFIFO_EMPTY) &&
                ((USB_OTG_dev.regs.INEP_REGS[USB_HID_INEP1_NUM]->DTXFSTS & USB_HID_INEP1_TXFIFO_FREE_WORD_MASK ) == USB_HID_INEP1_TXFIFO_FREE_WORD)) ? 1 : 0;
  return idle;
}

void usb_tx(){
  static uint8_t isbusy = 0;
  if(!isbusy){
    isbusy = 1;
    uint8_t sendData[HID_AGENT_IN_PACKET];
    memset(sendData, 0, HID_AGENT_IN_PACKET);
    if(hidTxFifo != 0 && hidTxFifo->size() > 0 && isUsbIdle()){
      for(uint8_t i = 0; i < HID_AGENT_IN_PACKET; i++){
        if(!hidTxFifo->pop(sendData[i])){
          break;
        }
      }
      USBD_AGENT_SendReport(&USB_OTG_dev, sendData, HID_AGENT_IN_PACKET);
    }

    memset(sendData, 0, HID_AGENT_IN_PACKET);
    if(hidTxFifo != 0 && selectedUsbMode != USB_AGENT_MODE){
      free(hidTxFifo);
      hidTxFifo = 0;
    }
    isbusy = 0;
  }
}

void CRSF_To_USB_HID( uint8_t *p_arr )
{
  *p_arr = LIBCRSF_UART_SYNC;
  if(hidTxFifo == 0 && selectedUsbMode == USB_AGENT_MODE && usbStarted()){
    hidTxFifo = (Fifo<uint8_t, USB_HID_FIFO_SIZE>*)malloc(sizeof(Fifo<uint8_t, USB_HID_FIFO_SIZE>));
    if(hidTxFifo != 0){
      memset(hidTxFifo, 0, sizeof(Fifo<uint8_t, USB_HID_FIFO_SIZE>));
    }
  }

  // block sending telemetry and opentx related to usb
  if( ( *(p_arr + LIBCRSF_TYPE_ADD ) != LIBCRSF_BF_LINK_STATISTICS ) && ( *(p_arr + LIBCRSF_TYPE_ADD) != LIBCRSF_OPENTX_RELATED ) ){
    if(hidTxFifo != 0 && hidTxFifo->hasSpace(p_arr[LIBCRSF_LENGTH_ADD] + 2)){
      for(uint8_t i = 0; i < HID_AGENT_IN_PACKET; i++){
        hidTxFifo->push(p_arr[i]);
      }
    }
  }
  usb_tx();
}

void AgentHandler(){
  /* handle TBS Agent requests */
  extern uint8_t ReportReceived;
  extern uint8_t HID_Buffer[HID_AGENT_OUT_PACKET];
  static _libCrsf_CRSF_PARSE_DATA HID_CRSF_Data;
  uint8_t hid_buffer[HID_AGENT_OUT_PACKET];

  usb_tx();

  while(ReportReceived){
    // making a copy of HID_Buffer is a workaround for preventing changing data in hid_buffer during parsing crsf data,
    ReportReceived = 2;
    memcpy(hid_buffer, HID_Buffer, HID_AGENT_OUT_PACKET);
    ReportReceived = 0;
    for( uint8_t i = 0; i < HID_AGENT_OUT_PACKET; i++ ){
      if ( libCrsf_CRSF_Parse( &HID_CRSF_Data, hid_buffer[i] )) {
        libCrsf_CRSF_Routing( USB_HID, HID_CRSF_Data.Payload );
        break;
      }
    }
  }
}
#endif // AGENT
#endif
