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
#include "string.h"
#include "storage/modelslist.h"

#include "datastructs_130.h"

#define LOG_SOURCE_CH1_130            157
#define MIX_SOURCE_CH1_130            157
#define INPUT_SOURCE_CH1_130          157
#define SPE_FUNC_TRAINER_130          1
#define SPE_FUNC_SET_FAILSAFE_130     7

Conversion_130::RadioData_v130 oldRadio;
Conversion_130::ModelData_v130 oldModel;

int convertLogicalSource_130_to_137(int source)
{
  if (source >= LOG_SOURCE_CH1_130)
    source += 16;
  return source;
}

void convertMixSource_130_to_137(int source, int *new_source)
{
  if (source >= MIX_SOURCE_CH1_130)
    source += 16;

  *new_source = source;
}

int convertInputSource_130_to_137(int source)
{
  if (source >= INPUT_SOURCE_CH1_130)
    source += 16;

  return source;
}

static const char * writeDataFile(const char * filename, const uint8_t * data, uint16_t size)
{
  TRACE("writeFile(%s)", filename);

  FIL file;
  unsigned char buf[8];
  UINT written;

  FRESULT result = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) {
    return SDCARD_ERROR(result);
  }

  *(uint32_t*)&buf[0] = OTX_FOURCC;
  buf[4] = EEPROM_VER;
  buf[5] = 'M';
  *(uint16_t*)&buf[6] = size;

  result = f_write(&file, buf, 8, &written);
  if (result != FR_OK || written != 8) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  result = f_write(&file, data, size, &written);
  if (result != FR_OK || written != size) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  f_close(&file);
  return NULL;
}

static const char * openDataFile(const char * fullpath, FIL * file, uint16_t * size, uint8_t * version)
{
  FRESULT result = f_open(file, fullpath, FA_OPEN_EXISTING | FA_READ);
  if (result != FR_OK) {
    return SDCARD_ERROR(result);
  }

  if (f_size(file) < 8) {
    f_close(file);
    return STR_INCOMPATIBLE;
  }

  UINT read;
  char buf[8];

  result = f_read(file, (uint8_t *)buf, sizeof(buf), &read);
  if ((result != FR_OK) || (read != sizeof(buf))) {
    f_close(file);
    return SDCARD_ERROR(result);
  }

  *version = (uint8_t)buf[4];
  *size = *(uint16_t*)&buf[6];
  return nullptr;
}

static const char * readDataFile(const char * fullpath, uint8_t * data, uint16_t maxsize, uint8_t * version)
{
  FIL      file;
  UINT     read;
  uint16_t size;

  TRACE("loadFile(%s)", fullpath);

  const char * err = openDataFile(fullpath, &file, &size, version);
  if (err)
    return err;

  size = min<uint16_t>(maxsize, size);
  FRESULT result = f_read(&file, data, size, &read);
  if (result != FR_OK || read != size) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  f_close(&file);
  return nullptr;
}

static const char * readModelFile(const char * filename, uint8_t * buffer, uint32_t size, uint8_t * version)
{
  char path[256];
  getModelPath(path, filename);
  return readDataFile(path, buffer, size, version);
}

bool ModelCell::forceFetchRfData()
{
  //TODO: use g_model in case fetching data for current model
  //
  char buf[256];
  getModelPath(buf, modelFilename);

  FIL      file;
  uint16_t size;
  uint8_t  version;

  const char * err = openDataFile(buf, &file, &size, &version);
  if (err || version != EEPROM_VER) return false;

  FSIZE_t start_offset = f_tell(&file);

  UINT read;
  if ((f_read(&file, buf, LEN_MODEL_NAME, &read) != FR_OK) || (read != LEN_MODEL_NAME))
    goto error;

  setModelName(buf);

  // 1. fetch modelId: NUM_MODULES @ offsetof(ModelHeader, modelId)
  // if (f_lseek(&file, start_offset + offsetof(ModelHeader, modelId)) != FR_OK)
  //   goto error;
  if ((f_read(&file, modelId, NUM_MODULES, &read) != FR_OK) || (read != NUM_MODULES))
    goto error;

  // 2. fetch ModuleData: sizeof(ModuleData)*NUM_MODULES @ offsetof(ModelData, moduleData)
  if (f_lseek(&file, start_offset + offsetof(ModelData, moduleData)) != FR_OK)
    goto error;

  for(uint8_t i=0; i<NUM_MODULES; i++) {
    ModuleData modData;
    if ((f_read(&file, &modData, NUM_MODULES, &read) != FR_OK) || (read != NUM_MODULES))
      goto error;

    setRfModuleData(i, &modData);
  }

  valid_rfData = true;
  f_close(&file);
  return true;

  error:
  f_close(&file);
  return false;
}

bool ModelsList::forceLoad()
{
  char line[LEN_MODELS_IDX_LINE+1];
  ModelsCategory * category = NULL;

  categories.clear();
  FRESULT result = f_open(&file, RADIO_MODELSLIST_PATH, FA_OPEN_EXISTING | FA_READ);
  if (result == FR_OK) {
    while (readNextLine(line, LEN_MODELS_IDX_LINE)) {
      int len = strlen(line); // TODO could be returned by readNextLine
      if (len > 2 && line[0] == '[' && line[len-1] == ']') {
        line[len-1] = '\0';
        category = new ModelsCategory(&line[1]);
        categories.push_back(category);
      }
      else if (len > 0) {

        //char* rf_data_str = cutModelFilename(line);
        ModelCell * model = new ModelCell(line);
        if (!category) {
          category = new ModelsCategory("Models");
          categories.push_back(category);
        }
        if (!strncmp(line, g_eeGeneral.currModelFilename, LEN_MODEL_FILENAME)) {
          currentCategory = category;
          currentModel = model;
        }
        if (model->forceFetchRfData()) {
          category->push_back(model);
          modelsCount += 1;
        }
        else {
          category->removeModel(model);
        }
      }
    }
    f_close(&file);

    if (!getCurrentModel()) {
      TRACE("currentModel is NULL");
    }
  }

  if (categories.size() == 0) {
    category = new ModelsCategory("Models");
    categories.push_back(category);
  }

  save();
  return true;
}

void convertModelData_130_to_137(Conversion_130::ModelData_v130 &oldModel, ModelData &g_model)
{
  int new_source;

  TRACE("*** old model size = %d", sizeof(Conversion_130::ModelData_v130));

  static_assert(sizeof(Conversion_130::ModelData_v130) >= sizeof(ModelData), "ModelData size has been reduced");

  memcpy(&g_model.header.name, &oldModel.header.name, sizeof(oldModel.header.name));
  memcpy(&g_model.header.modelId, &oldModel.header.modelId, sizeof(oldModel.header.modelId));
  memcpy(&g_model.timers, &oldModel.timers, sizeof(oldModel.timers));
  g_model.telemetryProtocol = oldModel.telemetryProtocol;
  g_model.thrTrim = oldModel.thrTrim;
  g_model.noGlobalFunctions = oldModel.noGlobalFunctions;
  g_model.displayTrims = oldModel.displayTrims;
  g_model.ignoreSensorIds = oldModel.ignoreSensorIds;
  g_model.trimInc = oldModel.trimInc;
  g_model.disableThrottleWarning = oldModel.disableThrottleWarning;
  g_model.displayChecklist = oldModel.displayChecklist;
  g_model.extendedLimits = oldModel.extendedLimits;
  g_model.extendedTrims = oldModel.extendedTrims;
  g_model.throttleReversed = oldModel.throttleReversed;

  g_model.beepANACenter = oldModel.beepANACenter;
  memcpy(&g_model.mixData, &oldModel.mixData, sizeof(oldModel.mixData));
  memcpy(&g_model.limitData, &oldModel.limitData, sizeof(oldModel.limitData));
  memcpy(&g_model.expoData, &oldModel.expoData, sizeof(oldModel.expoData));

  memcpy(&g_model.curves, &oldModel.curves, sizeof(oldModel.curves));
  memcpy(&g_model.points, &oldModel.points, sizeof(oldModel.points));
  memcpy(&g_model.logicalSw, &oldModel.logicalSw, sizeof(oldModel.logicalSw));

  // customFn
  for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++) {
    RTOS_WAIT_MS(1);
    g_model.customFn[i].swtch = oldModel.customFn[i].swtch;
    g_model.customFn[i].func = oldModel.customFn[i].func;
    memmove(&g_model.customFn[i].play.name, &oldModel.customFn[i].play.name, sizeof(oldModel.customFn[i].play.name));
    g_model.customFn[i].all.val = oldModel.customFn[i].all.val;
    g_model.customFn[i].all.mode = oldModel.customFn[i].all.mode;
    g_model.customFn[i].all.param = oldModel.customFn[i].all.param;
    g_model.customFn[i].all.spare = oldModel.customFn[i].all.spare;
    g_model.customFn[i].clear.val1 = oldModel.customFn[i].clear.val1;
    g_model.customFn[i].clear.val2 = oldModel.customFn[i].clear.val2;
    g_model.customFn[i].active = oldModel.customFn[i].active;
  }

  memcpy(&g_model.swashR, &oldModel.swashR, sizeof(oldModel.swashR));
  memcpy(&g_model.flightModeData, &oldModel.flightModeData, sizeof(oldModel.flightModeData));
  g_model.thrTraceSrc = oldModel.thrTraceSrc;
  g_model.switchWarningState = oldModel.switchWarningState;
  g_model.switchWarningEnable = oldModel.switchWarningEnable;
  memcpy(&g_model.gvars, &oldModel.gvars, sizeof(oldModel.gvars));
  memcpy(&g_model.varioData, &oldModel.varioData, sizeof(oldModel.varioData));
  g_model.rssiSource = oldModel.rssiSource;
  memcpy(&g_model.rssiAlarms, &oldModel.rssiAlarms, sizeof(oldModel.rssiAlarms));

  g_model.spare1 = oldModel.spare1;
  g_model.potsWarnMode = oldModel.potsWarnMode;
  memmove(&g_model.moduleData, &oldModel.moduleData, sizeof(oldModel.moduleData));
  // The internal crossfire was previously used as an external module
  if (oldModel.moduleData[EXTERNAL_MODULE].type == MODULE_TYPE_NONE) {
    g_model.moduleData[INTERNAL_MODULE].type = MODULE_TYPE_CROSSFIRE;
    g_model.header.modelId[INTERNAL_MODULE] =  oldModel.header.modelId[EXTERNAL_MODULE];
  }

  memcpy(&g_model.failsafeChannels, &oldModel.failsafeChannels, sizeof(oldModel.failsafeChannels));
  memcpy(&g_model.trainerData, &oldModel.trainerData, sizeof(oldModel.trainerData));

  // customer script data
  for (int i = 0; i < MAX_SCRIPTS; i++)
  {
    RTOS_WAIT_MS(1);
    memcpy(g_model.scriptsData[i].file, oldModel.scriptsData[i].file, LEN_SCRIPT_FILENAME);
    memcpy(g_model.scriptsData[i].name, oldModel.scriptsData[i].name, LEN_SCRIPT_NAME);

    for (int input = 0; input < MAX_SCRIPT_INPUTS; input++)
    {
      g_model.scriptsData[i].inputs[input].source = convertLogicalSource_130_to_137(oldModel.scriptsData[i].inputs[input].source);
    }
  }

  memcpy(&g_model.inputNames, &oldModel.inputNames, sizeof(oldModel.inputNames));
  g_model.potsWarnEnabled = oldModel.potsWarnEnabled;
  memcpy(&g_model.potsWarnPosition, &oldModel.potsWarnPosition, sizeof(oldModel.potsWarnPosition));

  // telemetrySensors
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    RTOS_WAIT_MS(1);
    g_model.telemetrySensors[i].id = oldModel.telemetrySensors[i].id;
    g_model.telemetrySensors[i].persistentValue = oldModel.telemetrySensors[i].persistentValue;
    g_model.telemetrySensors[i].frskyInstance.physID = oldModel.telemetrySensors[i].frskyInstance.physID;
    g_model.telemetrySensors[i].frskyInstance.rxIndex = oldModel.telemetrySensors[i].frskyInstance.rxIndex;
    g_model.telemetrySensors[i].instance = oldModel.telemetrySensors[i].instance;
    g_model.telemetrySensors[i].formula = oldModel.telemetrySensors[i].formula;
    memmove(g_model.telemetrySensors[i].label, oldModel.telemetrySensors[i].label, sizeof(g_model.telemetrySensors[i].label));
    g_model.telemetrySensors[i].subId = oldModel.telemetrySensors[i].subId;
    g_model.telemetrySensors[i].type = oldModel.telemetrySensors[i].type;
    g_model.telemetrySensors[i].spare1 = oldModel.telemetrySensors[i].spare1;
    g_model.telemetrySensors[i].unit = oldModel.telemetrySensors[i].unit;
    g_model.telemetrySensors[i].prec = oldModel.telemetrySensors[i].prec;
    g_model.telemetrySensors[i].autoOffset = oldModel.telemetrySensors[i].autoOffset;
    g_model.telemetrySensors[i].filter = oldModel.telemetrySensors[i].filter;
    g_model.telemetrySensors[i].logs = oldModel.telemetrySensors[i].logs;
    g_model.telemetrySensors[i].persistent = oldModel.telemetrySensors[i].persistent;
    g_model.telemetrySensors[i].onlyPositive = oldModel.telemetrySensors[i].onlyPositive;
    g_model.telemetrySensors[i].spare2 = oldModel.telemetrySensors[i].spare2;
    g_model.telemetrySensors[i].custom.ratio = oldModel.telemetrySensors[i].custom.ratio;
    g_model.telemetrySensors[i].custom.offset = oldModel.telemetrySensors[i].custom.offset;
    g_model.telemetrySensors[i].cell.source = oldModel.telemetrySensors[i].cell.source;
    g_model.telemetrySensors[i].cell.index = oldModel.telemetrySensors[i].cell.index;
    g_model.telemetrySensors[i].cell.spare = oldModel.telemetrySensors[i].cell.spare;
    memmove(g_model.telemetrySensors[i].calc.sources, oldModel.telemetrySensors[i].calc.sources, sizeof(g_model.telemetrySensors[i].calc.sources));
    g_model.telemetrySensors[i].consumption.source = oldModel.telemetrySensors[i].consumption.source;
    memmove(g_model.telemetrySensors[i].consumption.spare, oldModel.telemetrySensors[i].consumption.spare, sizeof(g_model.telemetrySensors[i].consumption.spare));
    g_model.telemetrySensors[i].dist.gps = oldModel.telemetrySensors[i].dist.gps;
    g_model.telemetrySensors[i].dist.alt = oldModel.telemetrySensors[i].dist.alt;
    g_model.telemetrySensors[i].dist.spare = oldModel.telemetrySensors[i].dist.spare;
    g_model.telemetrySensors[i].param = oldModel.telemetrySensors[i].param;
  }

  //customer screens data
  g_model.screensType = oldModel.screensType;
  g_model.view = oldModel.view;
  for (int i = 0; i < MAX_TELEMETRY_SCREENS; i++)
  {
    uint8_t screenType = (oldModel.screensType >> (2*i)) & 0x03;

    if (screenType == TELEMETRY_SCREEN_TYPE_BARS)
    {
      //bars
      for (int j = 0; j < 4; j++)
      {
        g_model.screens[i].bars[j].source = convertLogicalSource_130_to_137(oldModel.screens[i].bars[j].source);
        g_model.screens[i].bars[j].barMin = oldModel.screens[i].bars[j].barMin;
        g_model.screens[i].bars[j].barMax = oldModel.screens[i].bars[j].barMax;
      }
    }
    else if (screenType == TELEMETRY_SCREEN_TYPE_VALUES)
    {
      //numbers
      for (int line = 0; line < 4; line++)
      {
        for (int k = 0; k < NUM_LINE_ITEMS; k++)
        {
          g_model.screens[i].lines[line].sources[k] = convertLogicalSource_130_to_137(oldModel.screens[i].lines[line].sources[k]);
        }
      }
    }
    else if (screenType == TELEMETRY_SCREEN_TYPE_SCRIPT)
    {
      memcpy(g_model.screens[i].script.file, oldModel.screens[i].script.file, LEN_SCRIPT_FILENAME);
      memcpy(g_model.screens[i].script.inputs, oldModel.screens[i].script.inputs, sizeof(oldModel.screens[i].script.inputs));
    }
  };

  memcpy(&g_model.modelRegistrationID, &oldModel.modelRegistrationID, sizeof(oldModel.modelRegistrationID));


  for (int i = 0; i < MAX_EXPOS; i++)
  {
    g_model.expoData[i].srcRaw = convertInputSource_130_to_137(oldModel.expoData[i].srcRaw);
  }

  for (int i = 0; i < MAX_MIXERS; i++)
  {
    convertMixSource_130_to_137(g_model.mixData[i].srcRaw, &new_source);
    g_model.mixData[i].srcRaw = new_source;
  }

  for (int i = 0; i < MAX_LOGICAL_SWITCHES; i++)
  {
    g_model.logicalSw[i].v1 = convertLogicalSource_130_to_137(oldModel.logicalSw[i].v1);
  }

#if defined(PCBMAMBO)
  for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++)
  {
    if (oldModel.customFn[i].func == SPE_FUNC_TRAINER_130 || oldModel.customFn[i].func == SPE_FUNC_SET_FAILSAFE_130)
    {
      //clear useless function
      memset(&g_model.customFn[i], 0, sizeof(g_model.customFn[i]));
    }
  }
#endif
}

void convertRadio_130_137()
{
  uint8_t version;

  readDataFile(RADIO_SETTINGS_PATH, (uint8_t *)&oldRadio, sizeof(oldRadio), &version);

  drawProgressScreen("converting radio data", "radio.bin", 0, 0);

  g_eeGeneral.version = oldRadio.version;
  g_eeGeneral.variant = oldRadio.variant;
  memcpy(&g_eeGeneral.calib, &oldRadio.calib, sizeof(oldRadio.calib));
  g_eeGeneral.chkSum = oldRadio.chkSum;
  g_eeGeneral.currModel = oldRadio.currModel;
  g_eeGeneral.contrast = oldRadio.contrast;
  g_eeGeneral.vBatWarn = oldRadio.vBatWarn;
  g_eeGeneral.txVoltageCalibration = oldRadio.txVoltageCalibration;
  g_eeGeneral.backlightMode = oldRadio.backlightMode;
  g_eeGeneral.antennaMode = oldRadio.antennaMode;
  g_eeGeneral.disableRtcWarning = oldRadio.disableRtcWarning;

  RTOS_WAIT_MS(2);

  memcpy(&g_eeGeneral.trainer, &oldRadio.trainer, sizeof(oldRadio.trainer));
  g_eeGeneral.view = oldRadio.view;
  g_eeGeneral.spare4 = oldRadio.spare4;
  g_eeGeneral.fai = oldRadio.fai;
  g_eeGeneral.beepMode = oldRadio.beepMode;
  g_eeGeneral.alarmsFlash = oldRadio.alarmsFlash;
  g_eeGeneral.disableMemoryWarning = oldRadio.disableMemoryWarning;
  g_eeGeneral.disableAlarmWarning = oldRadio.disableAlarmWarning;
  g_eeGeneral.stickMode = oldRadio.stickMode;
  g_eeGeneral.timezone = oldRadio.timezone;
  g_eeGeneral.adjustRTC = oldRadio.adjustRTC;
  g_eeGeneral.inactivityTimer = oldRadio.inactivityTimer;
  g_eeGeneral.telemetryBaudrate = oldRadio.telemetryBaudrate;
  g_eeGeneral.splashMode = oldRadio.splashMode;
  g_eeGeneral.hapticMode = oldRadio.hapticMode;
  g_eeGeneral.switchesDelay = oldRadio.switchesDelay;
  g_eeGeneral.lightAutoOff = oldRadio.lightAutoOff;
  g_eeGeneral.templateSetup = oldRadio.templateSetup;
  g_eeGeneral.PPM_Multiplier = oldRadio.PPM_Multiplier;
  g_eeGeneral.hapticLength = oldRadio.hapticLength;
  g_eeGeneral.beepLength = oldRadio.beepLength;
  g_eeGeneral.hapticStrength = oldRadio.hapticStrength;
  g_eeGeneral.gpsFormat = oldRadio.gpsFormat;
  g_eeGeneral.unexpectedShutdown = oldRadio.unexpectedShutdown;
  g_eeGeneral.speakerPitch = oldRadio.speakerPitch;
  g_eeGeneral.speakerVolume = oldRadio.speakerVolume;
  g_eeGeneral.vBatMin = oldRadio.vBatMin;
  g_eeGeneral.vBatMax = oldRadio.vBatMax;

  RTOS_WAIT_MS(2);

  g_eeGeneral.backlightBright = oldRadio.backlightBright;
  g_eeGeneral.globalTimer = oldRadio.globalTimer;
  g_eeGeneral.bluetoothBaudrate = oldRadio.bluetoothBaudrate;
  g_eeGeneral.bluetoothMode = oldRadio.bluetoothMode;
  g_eeGeneral.countryCode = oldRadio.countryCode;
  g_eeGeneral.pwrOnSpeed = oldRadio.pwrOnSpeed;
  g_eeGeneral.pwrOffSpeed = oldRadio.pwrOffSpeed;
  g_eeGeneral.imperial = oldRadio.imperial;
  g_eeGeneral.jitterFilter = oldRadio.jitterFilter;
  g_eeGeneral.disableRssiPoweroffAlarm = oldRadio.disableRssiPoweroffAlarm;
  g_eeGeneral.USBMode = oldRadio.USBMode;
  g_eeGeneral.jackMode = oldRadio.jackMode;
  g_eeGeneral.sportUpdatePower = oldRadio.sportUpdatePower;
  g_eeGeneral.ttsLanguage[0] = oldRadio.ttsLanguage[0];
  g_eeGeneral.ttsLanguage[1] = oldRadio.ttsLanguage[1];
  g_eeGeneral.beepVolume = oldRadio.beepVolume;
  g_eeGeneral.wavVolume = oldRadio.wavVolume;
  g_eeGeneral.varioVolume = oldRadio.varioVolume;
  g_eeGeneral.backgroundVolume = oldRadio.backgroundVolume;
  g_eeGeneral.varioPitch = oldRadio.varioPitch;
  g_eeGeneral.varioRange = oldRadio.varioRange;
  g_eeGeneral.varioRepeat = oldRadio.varioRepeat;

  // customFn
  for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++) {
    RTOS_WAIT_MS(1);
    g_eeGeneral.customFn[i].swtch = oldRadio.customFn[i].swtch;
    g_eeGeneral.customFn[i].func = oldRadio.customFn[i].func;
    memmove(&g_eeGeneral.customFn[i].play.name, &oldRadio.customFn[i].play.name, sizeof(oldRadio.customFn[i].play.name));
    g_eeGeneral.customFn[i].all.val = oldRadio.customFn[i].all.val;
    g_eeGeneral.customFn[i].all.mode = oldRadio.customFn[i].all.mode;
    g_eeGeneral.customFn[i].all.param = oldRadio.customFn[i].all.param;
    g_eeGeneral.customFn[i].all.spare = oldRadio.customFn[i].all.spare;
    g_eeGeneral.customFn[i].clear.val1 = oldRadio.customFn[i].clear.val1;
    g_eeGeneral.customFn[i].clear.val2 = oldRadio.customFn[i].clear.val2;
    g_eeGeneral.customFn[i].active = oldRadio.customFn[i].active;
  }

  RTOS_WAIT_MS(2);
  g_eeGeneral.auxSerialMode = oldRadio.auxSerialMode;
  g_eeGeneral.slidersConfig = oldRadio.slidersConfig;
  g_eeGeneral.potsConfig = oldRadio.potsConfig;
#if !defined(PCBMAMBO)
  g_eeGeneral.backlightColor = oldRadio.backlightColor;
#endif
  g_eeGeneral.switchUnlockStates = oldRadio.switchUnlockStates;
  g_eeGeneral.switchConfig = oldRadio.switchConfig;
  memcpy(&g_eeGeneral.switchNames, &oldRadio.switchNames, sizeof(oldRadio.switchNames));
  memcpy(&g_eeGeneral.anaNames, &oldRadio.anaNames, sizeof(oldRadio.anaNames));
  memcpy(&g_eeGeneral.currModelFilename, &oldRadio.currModelFilename, sizeof(oldRadio.currModelFilename));
  memcpy(&g_eeGeneral.ownerRegistrationID, &oldRadio.ownerRegistrationID, sizeof(oldRadio.ownerRegistrationID));
  g_eeGeneral.rotEncDirection = oldRadio.enableRotaryInverse;

#if defined(PCBMAMBO)
  for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++)
  {
    if (oldRadio.customFn[i].func == SPE_FUNC_TRAINER_130 || oldRadio.customFn[i].func == SPE_FUNC_SET_FAILSAFE_130)
    {
      //clear useless function
      memset(&g_eeGeneral.customFn[i], 0, sizeof(g_eeGeneral.customFn[i]));
    }
  }
#endif

  // write to file
  writeDataFile(RADIO_SETTINGS_PATH, (uint8_t *)&g_eeGeneral, sizeof(g_eeGeneral));
}


void convertModels_130_137()
{
  int index = 0;
  uint8_t modelVersion;

  modelslist.forceLoad();

  const std::list<ModelsCategory*>& cats = modelslist.getCategories();

  for (std::list<ModelsCategory *>::const_iterator it = cats.begin(); it != cats.end(); ++it, ++index) {
    TRACE("---- %s ---", (*it)->name);

    for (uint8_t i = 0; i < (*it)->size(); i++) {
      std::list<ModelCell *>::iterator model = (*it)->begin();
      std::advance(model, i);
      TRACE("** %s **", (*model)->modelFilename);

      const char * error = readModelFile((*model)->modelFilename, (uint8_t *)&oldModel, sizeof(oldModel), &modelVersion);
      if (error) {
        TRACE("loadModel error=%s", error);
      }
      else {
        char path[256];
        RTOS_WAIT_MS(200);
        drawProgressScreen("converting model data", (*model)->modelFilename, i, (*it)->size());
        //convert model data
        convertModelData_130_to_137(oldModel, g_model);
        getModelPath(path, (*model)->modelFilename);
        //write data
        writeDataFile(path, (uint8_t *)&g_model, sizeof(g_model));
      }
    }
  }
}