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

#define CROSSFIRE_CH_CENTER                   0x3E0
#define CROSSFIRE_CH_BITS                     11
#define CROSSFIRE_CH_START_BITS               5
#define CROSSFIRE_CHANNELS_START_MAX          15
#define CROSSFIRE_SUBSET_CHANNELS_MAX         12


uint8_t createCrossfireModelIDFrame(uint8_t * frame)
{
  uint8_t * buf = frame;
  *buf++ = UART_SYNC;                                 /* device address */
  *buf++ = 8;                                         /* frame length */
  *buf++ = COMMAND_ID;                                /* cmd type */
  *buf++ = MODULE_ADDRESS;                            /* Destination Address */
  *buf++ = RADIO_ADDRESS;                             /* Origin Address */
  *buf++ = SUBCOMMAND_CRSF;                           /* sub command */
  *buf++ = COMMAND_MODEL_SELECT_ID;                   /* command of set model/receiver id */
  *buf++ = g_model.header.modelId[EXTERNAL_MODULE];   /* model ID */
  *buf++ = command_crc8(frame + 2, 6);
  *buf++ = crc8(frame + 2, 7);
  return buf - frame;
}

uint8_t createCrossfireSeedProposalFrame(uint8_t * frame)
{
  uint8_t response = 0;
  uint8_t * buf = frame;
  *buf++ = UART_SYNC;                                 /* device address */
  *buf++ = 9;                                         /* frame length */
  *buf++ = COMMAND_ID;                                /* cmd type */
  *buf++ = MODULE_ADDRESS;                            /* Destination Address */
  *buf++ = RADIO_ADDRESS;                             /* Origin Address */
  *buf++ = SUBCOMMAND_GENERAL;                        /* sub command */
  *buf++ = COMMAND_CRSF_SPEED_RESPONSE;               /* response to the proposed CRSF port speed */
  *buf++ = crsfSpeed.portID;                          /* port id */

  if ((1 << crsfSpeed.baudIndex) & crsfSpeed.invalidFlags) {
    response = 0;
  }
  else {
    response = 1;
  }
  *buf++ = response;                                  /* 1 = accepted / 0 = rejected */
  *buf++ = command_crc8(frame + 2, 7);
  *buf++ = crc8(frame + 2, 8);
  return buf - frame;
}

// Range for pulses (channels output) is [-1024:+1024]
uint8_t createCrossfireChannelsFrame(uint8_t * frame, int16_t * pulses)
{
  uint8_t * buf = frame;
  *buf++ = MODULE_ADDRESS;
  *buf++ = 24; // 1(ID) + 22 + 1(CRC)
  uint8_t * crc_start = buf;
  *buf++ = CHANNELS_ID;
  uint32_t bits = 0;
  uint8_t bitsavailable = 0;
  for (int i=0; i<CROSSFIRE_CHANNELS_COUNT; i++) {
    uint32_t val = limit(0, CROSSFIRE_CH_CENTER + (((pulses[i]) * 4) / 5), 2*CROSSFIRE_CH_CENTER);
    bits |= val << bitsavailable;
    bitsavailable += CROSSFIRE_CH_BITS;
    while (bitsavailable >= 8) {
      *buf++ = bits;
      bits >>= 8;
      bitsavailable -= 8;
    }
  }
  *buf++ = crc8(crc_start, 23);
  return buf - frame;
}

#define CRSF_SUBSET_CHANNEL_COUNT(channelsCnt) \
                  ((channelsCnt * CROSSFIRE_CH_BITS + CROSSFIRE_CH_START_BITS) % 8) ?       \
                  (((channelsCnt * CROSSFIRE_CH_BITS + CROSSFIRE_CH_START_BITS) / 8) + 1) : \
                  ((channelsCnt * CROSSFIRE_CH_BITS + CROSSFIRE_CH_START_BITS) / 8)

uint8_t createCrossfireSubsetChannelsFrame(uint8_t * frame, int16_t * pulses, uint8_t channelsStart, uint8_t channelsCount)
{
  uint8_t * buf = frame;
  uint32_t bits = 0;
  uint8_t bitsavailable = 0;
  uint8_t frameChannelCount = 0;

  if (channelsStart > CROSSFIRE_CHANNELS_START_MAX)
    channelsStart = CROSSFIRE_CHANNELS_START_MAX;
  if (channelsCount > CROSSFIRE_SUBSET_CHANNELS_MAX)
    channelsCount = CROSSFIRE_SUBSET_CHANNELS_MAX;

  *buf++ = MODULE_ADDRESS;

  frameChannelCount = CRSF_SUBSET_CHANNEL_COUNT(channelsCount);
  *buf++ = 2 + frameChannelCount;             // 1(ID) + channels + 1(CRC)
  uint8_t * crc_start = buf;
  *buf++ = SUBSET_CHANNELS_ID;

  bits = channelsStart;                       // starting channel
  bitsavailable = CROSSFIRE_CH_START_BITS;

  bits &= ~(0x03 << bitsavailable);           // configuration for the RC data resolution
  bits |= 0x01 << bitsavailable;              // 11 bits by default
  bitsavailable += 2;

  bits &= ~(0x01 << bitsavailable);           // reserved bit
  bitsavailable++;



  for (int i = channelsStart; i < channelsStart + channelsCount; i++) {
    uint32_t val;
    val = limit(0, CROSSFIRE_CH_CENTER + (((pulses[i]) * 4) / 5), 2 * CROSSFIRE_CH_CENTER);
    bits |= val << bitsavailable;
    bitsavailable += CROSSFIRE_CH_BITS;
    while (bitsavailable >= 8) {
      *buf++ = bits;
      bits >>= 8;
      bitsavailable -= 8;
    }
  }
  if (bitsavailable)
    *buf++ = bits;

  *buf++ = crc8(crc_start, frameChannelCount + 1);
  return buf - frame;
}

void setupPulsesCrossfire()
{
  if (telemetryProtocol == PROTOCOL_TELEMETRY_CROSSFIRE) {
    uint8_t * pulses = extmodulePulsesData.crossfire.pulses;

#if defined(LUA)
    if (outputTelemetryBuffer.destination == TELEMETRY_ENDPOINT_SPORT) {
      memcpy(pulses, outputTelemetryBuffer.data, outputTelemetryBuffer.size);
      extmodulePulsesData.crossfire.length = outputTelemetryBuffer.size;
      outputTelemetryBuffer.reset();
    }
    else
#endif
    {
      switch (moduleState[EXTERNAL_MODULE].counter) {
        case CRSF_FRAME_MODELID:
          extmodulePulsesData.crossfire.length = createCrossfireModelIDFrame(pulses);
          crsfSpeed.invalidFlags = 0;
          moduleState[EXTERNAL_MODULE].counter = CRSF_FRAME_MODELID_SENT;
          break;
        case CRSF_FRAME_SPEED_PROPOSAL:
          extmodulePulsesData.crossfire.length = createCrossfireSeedProposalFrame(pulses);
          moduleState[EXTERNAL_MODULE].counter = CRSF_FRAME_SPEED_PROPOSAL_SENT;
          break;
        case CRSF_FRAME_SPEED_PROPOSAL_SENT:
          if (!(1 << crsfSpeed.baudIndex & crsfSpeed.invalidFlags)) {
            telemetryProtocol = 0xFF;
            crsfSpeed.newSpeedRequest = true;
          }
          moduleState[EXTERNAL_MODULE].counter = CRSF_FRAME_CHANNEL;
          break;
        default:
          if (isCrossfireInHighSpeed(EXTERNAL_MODULE))
            extmodulePulsesData.crossfire.length = createCrossfireSubsetChannelsFrame(pulses, channelOutputs, g_model.moduleData[EXTERNAL_MODULE].channelsStart, 8 + g_model.moduleData[EXTERNAL_MODULE].channelsCount);
          else
            extmodulePulsesData.crossfire.length = createCrossfireChannelsFrame(pulses, &channelOutputs[g_model.moduleData[EXTERNAL_MODULE].channelsStart]);
      }
    }
  }
}
