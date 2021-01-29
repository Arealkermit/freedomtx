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


#include "opentx.h"
#include "crossfire.h"
#include "stamp.h"

extern void CRSF_To_USB_HID( uint8_t *p_arr );
extern void usbAgentWrite( uint8_t *p_arr );

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))

_libCrsf_CRSF_Port CRSF_Ports[] = {
  { DEVICE_INTERNAL,   &CRSF_This_Device },
  { USB_HID,           &CRSF_To_USB_HID },
  { CRSF_SHARED_FIFO,  &CRSF_to_Shared_FIFO },
};

uint8_t libCrsf_MySlaveAddress = 0;
char *libCrsf_MyDeviceName = nullptr;
uint32_t libCrsf_MyHwID = 0;
uint32_t libCrsf_MySerialNo = 0;
uint32_t libCrsf_MyFwID = 0;

uint8_t current_crsf_model_id = 0;

#ifdef LIBCRSF_ENABLE_OPENTX_RELATED
void crsfRemoteRelatedHandler( uint8_t *p_arr );
#endif

void crsfPackParam( uint8_t *p_arr )
{
  uint32_t count = 0;

  libUtil_Write8(p_arr, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtil_Write8(p_arr, &count, 0);                 /* frame length */
  libUtil_Write8(p_arr, &count, LIBCRSF_EX_PARAM_SETTING_ENTRY); /* cmd type */
  libUtil_Write8(p_arr, &count, LIBCRSF_USB_HOST_ADD);     /* Destination Address */
  libUtil_Write8(p_arr, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtil_Write8(p_arr, &count, 0x0);              /* param number */

  uint8_t crc1 = libCRC8_Get_CRC_Arr(&p_arr[2], count-2, POLYNOM_1);
  libUtil_Write8(p_arr, &count, crc1);

  p_arr[LIBCRSF_LENGTH_ADD] = count - 2;
}

void CRSF_Init( void )
{
  uint32_t fw_id;
  uint32_t hw_id;
  uint32_t serial_num;

  memset( &crossfireSharedData, 0, sizeof(CrossfireSharedData) );

  fw_id = (VERSION_MAJOR << 8 | (VERSION_MINOR * 10)) + VERSION_REVISION;
  hw_id = readBackupReg(BKREG_HW_ID_FREEDOMTX);
  serial_num = readBackupReg(BKREG_SERIAL_NO_FREEDOMTX);
  writeBackupReg(BKREG_HW_ID_FREEDOMTX, 0);
  writeBackupReg(BKREG_SERIAL_NO_FREEDOMTX, 0);

  libCrsf_Init( LIBCRSF_REMOTE_ADD, (char *)MY_DEVICE_NAME, serial_num, hw_id, fw_id );

  libCrsf_CRSF_Add_Device_Function_List( &CRSF_Ports[0], ARRAY_SIZE(CRSF_Ports));

  crossfireSharedData.rtosApiVersion = RTOS_API_VERSION;

  trampolineInit();
}

void CRSF_This_Device( uint8_t *p_arr )
{

  uint8_t arr[LIBCRSF_MAX_BUFFER_SIZE];
  uint8_t i = 0;

  /* handle parameter and command frames */
  switch ( *(p_arr + LIBCRSF_TYPE_ADD) )
  {
    case LIBCRSF_EX_PARAM_PING_DEVICE:
      if ( libCrsf_checkif_devicecalled( p_arr, true )) {
        // Parameter_Pack_Device_Information( &arr[LIBCRSF_LENGTH_ADD] );
        libCrsf_crsfwrite( LIBCRSF_EX_PARAM_DEVICE_INFO, &arr[ LIBCRSF_LENGTH_ADD ] );
        libCrsf_CRSF_Routing( DEVICE_INTERNAL, &arr[0] );
      }
      break;

    case LIBCRSF_EX_PARAM_SETTING_READ:
      crsfPackParam(p_arr);
      libCrsf_CRSF_Routing( DEVICE_INTERNAL, &p_arr[0] );
      break;

#ifdef LIBCRSF_ENABLE_COMMAND
    case LIBCRSF_CMD_FRAME:
      if( ( *( p_arr + *(p_arr + LIBCRSF_LENGTH_ADD) + LIBCRSF_HEADER_OFFSET - 1 ) )
        == libCRC8_Get_CRC_Arr( ( p_arr + LIBCRSF_TYPE_ADD ), *(p_arr + LIBCRSF_LENGTH_ADD) - 2, POLYNOM_2 )) {
        if(*( p_arr + LIBCRSF_PAYLOAD_START_ADD + 2 ) == LIBCRSF_GENERAL_CMD){
          if(*( p_arr + LIBCRSF_PAYLOAD_START_ADD + 3 ) == LIBCRSF_GENERAL_START_BOOTLOADER_SUBCMD){
#if defined(PCBTANGO) || defined(PCBMAMBO)
            RTOS_DEL_TASK(menusTaskId); // avoid updating the screen
            lcdOn();
            drawDownload();
            storageDirty(EE_GENERAL|EE_MODEL);
            storageCheck(true);
            sdDone();
            volatile uint32_t delay_cnt = get_tmr10ms();
            while (get_tmr10ms() - delay_cnt <= 200);
            boardReboot2bootloader(1, libCrsf_MyHwID, libCrsf_MySerialNo);
#endif
          }
        }
        else if(*(p_arr + LIBCRSF_EXT_PAYLOAD_START_ADD) == LIBCRSF_RC_RX_CMD){
          if ( *(p_arr + LIBCRSF_EXT_PAYLOAD_START_ADD + 1) == LIBCRSF_RC_RX_REPLY_CURRENT_MODEL_SUBCMD ){
            current_crsf_model_id = *(p_arr + LIBCRSF_EXT_PAYLOAD_START_ADD + 2);
          }
        }
      }
      break;
#endif

#ifdef LIBCRSF_ENABLE_OPENTX_RELATED
    case LIBCRSF_OPENTX_RELATED:
      crsfRemoteRelatedHandler( p_arr );
      break;
#endif

    default:
      // Buffer telemetry data inside a FIFO to let telemetryWakeup read from it and keep the
      // compatibility with the existing telemetry infrastructure.
      for(i = 0; i < *(p_arr + LIBCRSF_LENGTH_ADD) + 2; i++) {
        telemetryFifo.push(*(p_arr + i));
      }
      break;
  }
}

void CRSF_to_Shared_FIFO( uint8_t *p_arr )
{
  *p_arr = LIBCRSF_UART_SYNC;
  for( uint8_t i = 0; i < (*(p_arr + LIBCRSF_LENGTH_ADD) + LIBCRSF_HEADER_OFFSET + LIBCRSF_CRC_SIZE); i++ ) {
    crossfireSharedData.crsf_rx.push(*(p_arr + i));
  }
}

void crsfSharedFifoHandler( void )
{
  uint8_t byte;
  static _libCrsf_CRSF_PARSE_DATA CRSF_Data;
  if ( crossfireSharedData.crsf_tx.pop(byte) ){
    if ( libCrsf_CRSF_Parse( &CRSF_Data, byte )) {
      libCrsf_CRSF_Routing( CRSF_SHARED_FIFO, CRSF_Data.Payload );
    }
  }
}

void crsfSetModelID(void)
{
  uint32_t count = 0;
  BYTE txBuf[LIBCRSF_MAX_BUFFER_SIZE];

  libUtil_Write8(txBuf, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtil_Write8(txBuf, &count, 0);                 /* frame length */
  libUtil_Write8(txBuf, &count, LIBCRSF_CMD_FRAME); /* cmd type */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_TX);     /* Destination Address */
  libUtil_Write8(txBuf, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_RX_CMD); /* sub command */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_RX_MODEL_SELECTION_SUBCMD);    /* command of set model/receiver id */
  libUtil_Write8(txBuf, &count, g_model.header.modelId[EXTERNAL_MODULE]); /* model ID */

  uint8_t crc2 = libCRC8_Get_CRC_Arr(&txBuf[2], count-2, POLYNOM_2);
  libUtil_Write8(txBuf, &count, crc2);
  uint8_t crc1 = libCRC8_Get_CRC_Arr(&txBuf[2], count-2, POLYNOM_1);
  libUtil_Write8(txBuf, &count, crc1);

  txBuf[LIBCRSF_LENGTH_ADD] = count - 2;

  CRSF_to_Shared_FIFO(txBuf);
}

void crsfGetModelID(void)
{
  uint32_t count = 0;
  BYTE txBuf[LIBCRSF_MAX_BUFFER_SIZE];

  libUtil_Write8(txBuf, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtil_Write8(txBuf, &count, 0);                 /* frame length */
  libUtil_Write8(txBuf, &count, LIBCRSF_CMD_FRAME); /* cmd type */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_TX);     /* Destination Address */
  libUtil_Write8(txBuf, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_RX_CMD); /* sub command */
  libUtil_Write8(txBuf, &count, LIBCRSF_RC_RX_CURRENT_MODEL_SELECTION_SUBCMD);  /* command of set model/receiver id */
  libUtil_Write8(txBuf, &count, 0);                 /* the dummy byte of model ID */

  uint8_t crc2 = libCRC8_Get_CRC_Arr(&txBuf[2], count-2, POLYNOM_2);
  libUtil_Write8(txBuf, &count, crc2);
  uint8_t crc1 = libCRC8_Get_CRC_Arr(&txBuf[2], count-2, POLYNOM_1);
  libUtil_Write8(txBuf, &count, crc1);

  txBuf[LIBCRSF_LENGTH_ADD] = count - 2;

  CRSF_to_Shared_FIFO(txBuf);
}

uint32_t crsfGetHWID(void)
{
  return libCrsf_MyHwID;
}

void UpdateCrossfireChannels( void )
{
  uint8_t i;
  for ( i = 0; i < CROSSFIRE_CHANNELS_COUNT; ++i)
    crossfireSharedData.channels[i] = channelOutputs[i];
}

#if !defined(SIMU)
uint32_t readBackupReg(uint8_t index){
  return *(__IO uint32_t *) (BKPSRAM_BASE + index*4);
}

void writeBackupReg(uint8_t index, uint32_t data){
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
  PWR_BackupRegulatorCmd(ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE);
  PWR_BackupAccessCmd(ENABLE);
  while(PWR_GetFlagStatus(PWR_FLAG_BRR) == RESET);
  *(__IO uint32_t *) (BKPSRAM_BASE + index*4) = data;
}

uint8_t bkregGetStatusFlag(uint32_t flag){
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  return ((value & (1 << flag)) ? 1 : 0);
}

void bkregSetStatusFlag(uint32_t flag){
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  value |= (1 << flag);
  writeBackupReg(BKREG_STATUS_FLAG, value);
}

void bkregClrStatusFlag(uint32_t flag){
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  value &= ~(1 << flag);
  writeBackupReg(BKREG_STATUS_FLAG, value);
}

void boardSetSkipWarning()
{
  bkregSetStatusFlag( DEVICE_RESTART_WITHOUT_WARN_FLAG );
}

void crossfirePowerOff(){
  volatile uint32_t off_timeout;
  if(isCrossfireRfOn()) {
    set_crsf_flag(CRSF_FLAG_POWER_OFF);
    off_timeout = get_tmr10ms();
    while (get_crsf_flag(CRSF_FLAG_POWER_OFF)) {
      WDG_RESET();
      if(get_tmr10ms() - off_timeout >= 200) 
        break;
    }
  }
}

bool isCrossfireRfOn()
{
  return !get_crsf_flag(CRSF_FLAG_RF_OFF);
}

void crossfireTurnOffRf(bool ask)
{
  while(!get_crsf_flag(CRSF_FLAG_RF_OFF)) {   
    if (ask) {
      lcdRefreshWait();
      lcdClear();
      POPUP_CONFIRMATION(STR_JOYSTICK_RF, nullptr);
      SET_WARNING_INFO(STR_TURN_OFF_RF, sizeof(TR_TURN_OFF_RF), 0);
      event_t evt = getEvent(false);
      DISPLAY_WARNING(evt);
      lcdRefresh();
    }
    WDG_RESET();

    if (warningResult || !ask) {
      warningResult = 0;
      set_crsf_flag(CRSF_FLAG_RF_OFF);
      break;
    }
    else if (!warningText) {
      break;
    }
  }
}

void crossfireTurnOnRf()
{
  clear_crsf_flag(CRSF_FLAG_RF_OFF);
}
#endif

#ifdef LIBCRSF_ENABLE_OPENTX_RELATED
void libCrsf_unpackremote( uint8_t *p_arr, libCrsf_Remote_Data_u *remote_data ) {
  uint32_t j = LIBCRSF_EXT_PAYLOAD_START_ADD;
  uint32_t *i = &j;
  libCrsf_Remote_Frame remote_command_id = (libCrsf_Remote_Frame)libUtil_Read8( p_arr, i );
  switch( remote_command_id ) {
#ifdef LIBCRSF_ENABLE_SD
    case LIBCRSF_REMOTE_SD_OPEN:
      for( uint8_t j = 0; j < LIBCRSF_MAX_SD_PATH_SIZE; j++ ) {
        remote_data->info.path[j] = libUtil_ReadInt8( p_arr, i );
      }
      remote_data->info.size = libUtil_ReadInt32( p_arr, i );
      break;
    case LIBCRSF_REMOTE_SD_CLOSE:
      break;
    case LIBCRSF_REMOTE_SD_READ_ACCESS:
      remote_data->data.addr = libUtil_ReadInt32( p_arr, i );
      remote_data->data.size = libUtil_ReadInt32( p_arr, i );
      remote_data->data.chunk_addr = libUtil_ReadInt32( p_arr, i );
      remote_data->data.is_reply = libUtil_ReadInt8( p_arr, i );
      break;
    case LIBCRSF_REMOTE_SD_WRITE_ACCESS:
      remote_data->data.addr = libUtil_ReadInt32( p_arr, i );
      remote_data->data.size = libUtil_ReadInt32( p_arr, i );
      remote_data->data.chunk_addr = libUtil_ReadInt32( p_arr, i );
      remote_data->data.is_reply = libUtil_ReadInt8( p_arr, i );
      for( uint8_t j = 0; j < LIBCRSF_MAX_SD_PAYLOAD_SIZE; j++ ) {
        remote_data->data.payload[j] = libUtil_ReadInt8( p_arr, i );
      }
      break;
    case LIBCRSF_REMOTE_SD_WRITE_ACK:
      break;
    case LIBCRSF_REMOTE_SD_ERASE_FILE:
      for( uint8_t j = 0; j < LIBCRSF_MAX_SD_PATH_SIZE; j++ ) {
        remote_data->info.path[j] = libUtil_ReadInt8( p_arr, i );
      }
      break;
    case LIBCRSF_REMOTE_SD_MOUNT_STATUS:
      remote_data->mount_status.is_mounted = libUtil_ReadInt8( p_arr, i );
      break;
#endif // LIBCRSF_ENABLE_SD
    default:
      break;
  }
}

void crsfRemoteRelatedHandler( uint8_t *p_arr )
{

#ifdef LIBCRSF_ENABLE_SD
  static FIL file;
  static char filePath[LIBCRSF_MAX_SD_PATH_SIZE];
  static uint32_t offset = 0;
  static uint32_t fileSize = 0;

  FRESULT result;
  static libCrsf_Remote_Data_u data;
  libCrsf_unpackremote(p_arr, &data);
  static libCrsf_Remote_Data_u reply_data;
#endif

  switch( p_arr[ LIBCRSF_EXT_PAYLOAD_START_ADD ] )
  {
#ifdef LIBCRSF_ENABLE_SD
    case LIBCRSF_REMOTE_SD_OPEN:
    {
      // if opened, close it first
      if( file.obj.fs != NULL ){
        result = f_close( &file );
        CRSF_SD_PRINTF( "open:close\r\n");
        if( result != FR_OK ) {
          CRSF_SD_PRINTF( "open:close failed\r\n");
          return;
        }
      }

      // create path if does not exist
      char str[ LIBCRSF_MAX_SD_PATH_SIZE ];
      strcpy( filePath, (const char*)data.info.path );
      strcpy( str, (const char*)data.info.path );
      uint8_t dirCount = 0;
      char *dir = strtok( str, "/" );
      while( dir != NULL ){
        dirCount++;
        dir = strtok( NULL, "/" );
      }
      char path[ LIBCRSF_MAX_SD_PATH_SIZE ];
      memset( path, 0, LIBCRSF_MAX_SD_PATH_SIZE );
      strcpy( str, (const char*)data.info.path );
      dir = strtok( str, "/" );
      for( uint8_t i = 0; i < dirCount - 1; i++ ) {
        strcat( path, "/" );
        strcat( path, dir );
        result = f_mkdir( path );
        dir = strtok( NULL, "/" );
      }

      result = f_open( &file, data.info.path, FA_OPEN_ALWAYS | FA_WRITE | FA_READ );
      if( result == FR_OK ) {
        FILINFO info;
        result = f_stat( data.info.path, &info );
        if( result == FR_OK ) {
          memcpy( &reply_data.info.path, data.info.path, LIBCRSF_MAX_SD_PATH_SIZE );
          fileSize = reply_data.info.size = info.fsize;
          libCrsf_crsfwrite( LIBCRSF_OPENTX_RELATED, &p_arr[ LIBCRSF_LENGTH_ADD ], p_arr[ LIBCRSF_EXT_HEAD_ORG_ADD ], LIBCRSF_REMOTE_SD_OPEN, &reply_data );
          libCrsf_CRSF_Routing( DEVICE_INTERNAL, &p_arr[0] );
          offset = 0;
          CRSF_SD_PRINTF( "%s opened, fileLen: %ld\r\n", reply_data.info.path, reply_data.info.size );
        }
      }
      else{
        CRSF_SD_PRINTF( "open:failed\r\n");
      }
      break;
    }
    case LIBCRSF_REMOTE_SD_CLOSE:
      if( file.obj.fs != NULL ){
        result = f_close( &file );
        if( result == FR_OK ) {
          file.obj.fs = 0;
          CRSF_SD_PRINTF( "%s closed\r\n", filePath );
        }
      }
      break;
    case LIBCRSF_REMOTE_SD_READ_ACCESS:
      // check if the range to read is valided
      if( fileSize >= ( data.data.addr + data.data.size ) ) {
        // check if the address is previous
        if( offset > data.data.chunk_addr ) {
          CRSF_SD_PRINTF( "read:addr:chunk_addr: %ld ofs: %ld\r\n", data.data.chunk_addr, offset );
          // re-open file to load the previous address
          if( file.obj.fs != NULL ){
            result = f_close( &file );
            if( result == FR_OK ) {
              CRSF_SD_PRINTF( "read:close\r\n");
              file.obj.fs = 0;
            }
            else{
              CRSF_SD_PRINTF( "read:close failed\r\n");
              return;
            }
          }
        }

        if( file.obj.fs == NULL ){
          result = f_open( &file, filePath, FA_OPEN_ALWAYS | FA_WRITE | FA_READ );
          if( result == FR_OK ) {
            CRSF_SD_PRINTF( "read:re-open\r\n");
            offset = 0;
          }
          else{
            CRSF_SD_PRINTF( "read:re-open failed\r\n");
            return;
          }
        }

        // seek the specific address
        if( data.data.chunk_addr > offset ){
          result = f_lseek(&file, data.data.chunk_addr);
          if( result == FR_OK ){
            CRSF_SD_PRINTF( "read:lseek: %ld\r\n", data.data.chunk_addr);
            offset = data.data.chunk_addr;
          }
          else{
            CRSF_SD_PRINTF( "read:lseek failed\r\n");
            return;
          }
        }

        UINT byteRead;
        UINT size = ( data.data.size - data.data.chunk_addr + data.data.addr ) >= LIBCRSF_MAX_SD_PAYLOAD_SIZE ? LIBCRSF_MAX_SD_PAYLOAD_SIZE : ( data.data.size - data.data.chunk_addr + data.data.addr );
        result = f_read( &file, reply_data.data.payload, size, &byteRead);
        if( result == FR_OK ) {
          CRSF_SD_PRINTF( "read: ofs: %ld size: %ld data_size: %ld ofs - addr: %ld\r\n", offset, size, data.data.size, offset - data.data.addr );
          reply_data.data.addr = data.data.addr;
          reply_data.data.size = data.data.size;
          reply_data.data.chunk_addr = data.data.chunk_addr;
          offset += byteRead;
          reply_data.data.is_reply = 1;
          libCrsf_crsfwrite( LIBCRSF_OPENTX_RELATED, &p_arr[ LIBCRSF_LENGTH_ADD ], p_arr[ LIBCRSF_EXT_HEAD_ORG_ADD ], LIBCRSF_REMOTE_SD_READ_ACCESS, &reply_data );
          libCrsf_CRSF_Routing( DEVICE_INTERNAL, &p_arr[0] );
          CRSF_SD_PRINTF( "read: ofs_inc: %ld\r\n", offset);
        }
        else{
          CRSF_SD_PRINTF( "read:falied\r\n");
          return;
        }
      }
      break;
    case LIBCRSF_REMOTE_SD_WRITE_ACCESS:
      // check if the range to write is valided
      if( data.data.size > 0 ) {
        // check if the address is previous
        if( offset > data.data.chunk_addr ) {
          // re-open file to load the previous address
          if( file.obj.fs != NULL ){
            result = f_close( &file );
            CRSF_SD_PRINTF( "write:close\r\n");
            if( result == FR_OK ) {
              CRSF_SD_PRINTF( "write:close\r\n");
              file.obj.fs = 0;
            }
            else{
              CRSF_SD_PRINTF( "write:close failed\r\n");
              return;
            }
          }
        }

        if( file.obj.fs == NULL ){
          result = f_open( &file, filePath, FA_OPEN_ALWAYS | FA_WRITE | FA_READ );
          if( result == FR_OK ) {
            CRSF_SD_PRINTF( "write:re-open\r\n");
            offset = 0;
          }
          else{
            CRSF_SD_PRINTF( "write:re-open failed\r\n");
            return;
          }
        }

        // seek the specific address
        if( data.data.chunk_addr > offset ){
          result = f_lseek(&file, data.data.chunk_addr);
          if( result == FR_OK ){
            offset = data.data.chunk_addr;
            CRSF_SD_PRINTF( "write:lseek: %ld\r\n", data.data.chunk_addr);
          }
          else{
            CRSF_SD_PRINTF( "write:lseek failed\r\n");
            return;
          }
        }

        UINT byteWrite;
        UINT size = ( data.data.size - data.data.chunk_addr + data.data.addr ) >= LIBCRSF_MAX_SD_PAYLOAD_SIZE ? LIBCRSF_MAX_SD_PAYLOAD_SIZE : ( data.data.size - data.data.chunk_addr + data.data.addr );
        result = f_write( &file, data.data.payload, size, &byteWrite );
        if( result == FR_OK ) {
          CRSF_SD_PRINTF( "\r\nwrite:bytes: ofs:%ld size:%ld\r\n", offset, size);
          for(uint8_t i = 0; i < byteWrite; i++){
            CRSF_SD_PRINTF( "%02X ", data.data.payload[i]);
          }
          CRSF_SD_PRINTF( "\r\n");
          CRSF_SD_PRINTF( "write: ofs: %ld\r\n", offset);
          reply_data.ack.chunk_addr = data.data.chunk_addr;
          offset += byteWrite;
          libCrsf_crsfwrite( LIBCRSF_OPENTX_RELATED, &p_arr[ LIBCRSF_LENGTH_ADD ], p_arr[ LIBCRSF_EXT_HEAD_ORG_ADD ], LIBCRSF_REMOTE_SD_WRITE_ACK, &reply_data );
          libCrsf_CRSF_Routing( DEVICE_INTERNAL, &p_arr[0] );
          CRSF_SD_PRINTF( "write: ofs_inc: %ld\r\n", offset);
        }
        else{
          CRSF_SD_PRINTF( "write:falied\r\n");
          return;
        }
      }
      break;
    case LIBCRSF_REMOTE_SD_WRITE_ACK:
      break;
    case LIBCRSF_REMOTE_SD_ERASE_FILE:
      result = f_unlink( data.info.path );
      if( result == FR_OK ) {
        CRSF_SD_PRINTF( "erase:success: %s\r\n", data.info.path);
      }
      else {
        CRSF_SD_PRINTF( "erase:falied\r\n");
      }
      break;
    case LIBCRSF_REMOTE_SD_MOUNT_STATUS:
      reply_data.mount_status.is_mounted = (uint8_t)sdMounted();
      libCrsf_crsfwrite( LIBCRSF_OPENTX_RELATED, &p_arr[ LIBCRSF_LENGTH_ADD ], p_arr[ LIBCRSF_EXT_HEAD_ORG_ADD ], LIBCRSF_REMOTE_SD_MOUNT_STATUS, &reply_data );
      libCrsf_CRSF_Routing( DEVICE_INTERNAL, &p_arr[0] );
      CRSF_SD_PRINTF( "mount:%d\r\n", reply_data.mount_status.is_mounted );
      break;
#endif
    default:
      break;
  }

  RTOS_SET_FLAG(get_task_flag(CRSF_SD_TASK_FLAG));
}
#endif // LIBCRSF_ENABLE_OPENTX_RELATED
