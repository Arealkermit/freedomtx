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
#include "storage/modelslist.h"


#define CATEGORIES_WIDTH               62
#define MODELSEL_W 62


uint16_t categoriesVerticalOffset = 0;
uint16_t categoriesVerticalPosition = 0;
#define MODEL_INDEX()       (menuVerticalPosition*2+menuHorizontalPosition)

enum ModelSelectMode {
  MODE_SELECT_MODEL,
  MODE_RENAME_CATEGORY,
  MODE_MOVE_MODEL,
};

enum ModelDeleteMode {
  MODE_DELETE_MODEL,
  MODE_DELETE_CATEGORY,
};

uint8_t selectMode, deleteMode;

ModelsCategory * currentCategory = NULL;
int currentCategoryIndex;
ModelCell * currentModel = NULL;
ModelCell * selectedModel = NULL;


bool eeModelExists(uint8_t id)
{
  int index = 0;

  for (ModelsCategory::iterator it = currentCategory->begin(); it != currentCategory->end(); ++it, ++index) {
    if (id == index){
      return true;
    }
  }

  return false;
}

void setCurrentModel(unsigned int index)
{
  std::list<ModelCell *>::iterator it = currentCategory->begin();
  std::advance(it, index);
  currentModel = *it;
}

void getSelectedModel(unsigned int index)
{
  std::list<ModelCell *>::iterator it = currentCategory->begin();
  std::advance(it, index);
  selectedModel = *it;
}

void setCurrentCategory(unsigned int index)
{
  currentCategoryIndex = index;
  const std::list<ModelsCategory *>& cats = modelslist.getCategories();
  std::list<ModelsCategory *>::const_iterator it = cats.begin();
  std::advance(it, index);
  currentCategory = *it;
  categoriesVerticalPosition = index;
  categoriesVerticalOffset = limit<int>(categoriesVerticalPosition-4, categoriesVerticalOffset, min<int>(categoriesVerticalPosition, max<int>(0, cats.size()-5)));
}

void initModelsList()
{
  modelslist.load();

  categoriesVerticalOffset = 0;
  bool found = false;
  int index = 0;
  const std::list<ModelsCategory *>& cats = modelslist.getCategories();
  for (std::list<ModelsCategory *>::const_iterator it = cats.begin(); it != cats.end(); ++it, ++index) {
    if (*it == modelslist.getCurrentCategory()) {
      setCurrentCategory(index);
      found = true;
      break;
    }
  }
  if (!found) {
    setCurrentCategory(0);
  }

  menuVerticalOffset = 0;
  found = false;
  index = 0;
  for (ModelsCategory::iterator it = currentCategory->begin(); it != currentCategory->end(); ++it, ++index) {
    if (*it == modelslist.getCurrentModel()) {
      setCurrentModel(index);
      menuVerticalPosition = index;
      found = true;
      break;
    }
  }
  if (!found) {
    modelslist.addModel(currentCategory, g_eeGeneral.currModelFilename);
    setCurrentModel(0);
    modelslist.setCurrentModel(currentModel);
    modelslist.save();
    storageDirty(EE_GENERAL);
    storageDirty(EE_MODEL);
    storageCheck(true);
  }
}


void onModelSelectMenu(const char * result)
{
  if (result == STR_SELECT_MODEL) {
    setCurrentModel(menuVerticalPosition);
    storageFlushCurrentModel();
    storageCheck(true);
    memcpy(g_eeGeneral.currModelFilename, currentModel->modelFilename, LEN_MODEL_FILENAME);
    modelslist.setCurrentModel(currentModel);
    loadModel(g_eeGeneral.currModelFilename, true);
    modelslist.setCurrentCategorie(currentCategory);
    modelslist.save();
    if (g_model.moduleData[EXTERNAL_MODULE].type == MODULE_TYPE_NONE) {
      crossfireTurnOnRf();
      set_model_id_needed = true;
    }
    else {
      crossfireTurnOffRf(false);
    }
    storageDirty(EE_GENERAL);
    storageCheck(true);
    chainMenu(menuMainView);
  }
  else if (result == STR_DELETE_MODEL) {
    getSelectedModel(menuVerticalPosition);
    POPUP_CONFIRMATION(STR_DELETEMODEL, nullptr);
    SET_WARNING_INFO(selectedModel->modelName, LEN_MODEL_NAME, 0);
    deleteMode = MODE_DELETE_MODEL;
  }
  else if (result == STR_CREATE_MODEL) {
    storageCheck(true);
    modelslist.addModel(currentCategory, createModel());
    selectMode = MODE_SELECT_MODEL;
    setCurrentModel(currentCategory->size()-1);
    modelslist.setCurrentModel(currentModel);
    modelslist.onNewModelCreated(currentModel, &g_model);
    modelslist.setCurrentCategorie(currentCategory);
    modelslist.save();
    storageDirty(EE_GENERAL);
    storageCheck(true);
    menuVerticalPosition = currentCategory->size()-1;
  }
  else if (result == STR_DUPLICATE_MODEL) {
    char duplicatedFilename[LEN_MODEL_FILENAME+1];
    setCurrentModel(menuVerticalPosition);
    memcpy(duplicatedFilename, currentModel->modelFilename, sizeof(duplicatedFilename));
    if (findNextFileIndex(duplicatedFilename, LEN_MODEL_FILENAME, MODELS_PATH)) {
      sdCopyFile(currentModel->modelFilename, MODELS_PATH, duplicatedFilename, MODELS_PATH);
      ModelCell* dup_model = modelslist.addModel(currentCategory, duplicatedFilename);
      dup_model->fetchRfData();
      menuVerticalPosition = currentCategory->size()-1;
    }
    else {
      POPUP_WARNING("Invalid File");
    }
  }
  else if (result == STR_MOVE_MODEL) {
    setCurrentModel(menuVerticalPosition);
    selectMode = MODE_MOVE_MODEL;
  }
  else if (result == STR_CREATE_CATEGORY) {
    currentCategory = modelslist.createCategory();
    setCurrentCategory(modelslist.getCategories().size() - 1);
    modelslist.save();
    storageDirty(EE_GENERAL);
    storageCheck(true);
  }
  else if (result == STR_RENAME_CATEGORY) {
    selectMode = MODE_RENAME_CATEGORY;
    s_editMode = EDIT_MODIFY_STRING;
    editNameCursorPos = 0;
  }
  else if (result == STR_DELETE_CATEGORY) {
    if (currentCategory->size() > 0){
      POPUP_WARNING(STR_DELETE_ERROR);
      SET_WARNING_INFO(STR_CAT_NOT_EMPTY, sizeof(TR_CAT_NOT_EMPTY), 0);
    }
    else {
      POPUP_CONFIRMATION(STR_DELETEMODEL, nullptr);
      SET_WARNING_INFO(currentCategory->name, LEN_MODEL_FILENAME, 0);
      deleteMode = MODE_DELETE_CATEGORY;
    }
  }
}

#define PHASE_ONE_FIRST_LINE (1+1*FH)

void menuModelSelect(event_t event) {
  static int16_t subModelIndex = -1;
  static bool model_selected = false;

  if (warningResult) {
    warningResult = 0;
    if (deleteMode == MODE_DELETE_CATEGORY) {
      TRACE("DELETE CATEGORY");
      modelslist.removeCategory(currentCategory);
      modelslist.save();
      setCurrentCategory(currentCategoryIndex > 0 ? currentCategoryIndex-1 : currentCategoryIndex);
    }
    else if (deleteMode == MODE_DELETE_MODEL){
      int modelIndex = menuVerticalPosition;
      setCurrentModel(modelIndex);
      modelslist.removeModel(currentCategory, currentModel);
      selectMode = 0;
      menuVerticalPosition = currentCategory->size()-1;
    }
  }

  const std::list<ModelsCategory*>& cats = modelslist.getCategories();

  event_t _event_ = ((event==EVT_KEY_BREAK(KEY_ENTER) || event==EVT_KEY_LONG(KEY_ENTER)) ? 0 : event);

  check_submenu_simple(_event_, currentCategory?currentCategory->size():30);

  switch (event) {
    case EVT_ENTRY:
      selectMode = MODE_SELECT_MODEL;
      initModelsList();
      break;
    case EVT_KEY_BREAK(KEY_ENTER):
      if (selectMode == MODE_MOVE_MODEL)
        selectMode = MODE_SELECT_MODEL;
      killEvents(event);
      break;
    case EVT_KEY_FIRST(KEY_EXIT):
      switch (selectMode) {
        case MODE_MOVE_MODEL:
          selectMode = MODE_SELECT_MODEL;
          break;
        case MODE_SELECT_MODEL:
          chainMenu(menuMainView);
          return;
      }
      break;
    case EVT_KEY_BREAK(KEY_PAGE):
    {
      if (categoriesVerticalPosition >= cats.size()-1)
        categoriesVerticalPosition = 0;
      else
        categoriesVerticalPosition += 1;

      if (selectMode == MODE_SELECT_MODEL) {
        setCurrentCategory(categoriesVerticalPosition);
        menuVerticalPosition = 0;
      }
      if (selectMode == MODE_MOVE_MODEL) {
        ModelsCategory * previous_category = currentCategory;
        ModelCell * model = currentModel;
        setCurrentCategory(categoriesVerticalPosition);
        modelslist.moveModel(model, previous_category, currentCategory);
        menuVerticalPosition = currentCategory->size()-1;
      }

      subModelIndex = -1;
      model_selected = false;
      killEvents(event);
      break;
    }
    case EVT_KEY_LONG(KEY_PAGE):
    {
      if (categoriesVerticalPosition == 0)
        categoriesVerticalPosition = cats.size() - 1;
      else
        categoriesVerticalPosition -= 1;

      if (selectMode == MODE_SELECT_MODEL) {
        setCurrentCategory(categoriesVerticalPosition);
        menuVerticalPosition = 0;
      }
      if (selectMode == MODE_MOVE_MODEL) {
        ModelsCategory * previous_category = currentCategory;
        ModelCell * model = currentModel;
        setCurrentCategory(categoriesVerticalPosition);
        modelslist.moveModel(model, previous_category, currentCategory);
      }

      subModelIndex = -1;
      model_selected = false;
      killEvents(event);
      break;
    }
    case EVT_KEY_LONG(KEY_ENTER):
      if (selectMode == MODE_SELECT_MODEL) {
        killEvents(event);
        if (!model_selected && !currentCategory->empty()) {
            POPUP_MENU_ADD_ITEM(STR_SELECT_MODEL);
        }
        POPUP_MENU_ADD_ITEM(STR_CREATE_MODEL);
        if (!currentCategory->empty()) {
          if (currentModel) {
            POPUP_MENU_ADD_ITEM(STR_DUPLICATE_MODEL);
            POPUP_MENU_ADD_ITEM(STR_MOVE_MODEL);
          }
          if (!model_selected) {
            POPUP_MENU_ADD_ITEM(STR_DELETE_MODEL);
          }
        }
        POPUP_MENU_ADD_ITEM(STR_CREATE_CATEGORY);
        POPUP_MENU_ADD_ITEM(STR_RENAME_CATEGORY);
        if (cats.size() > 1) {
          POPUP_MENU_ADD_ITEM(STR_DELETE_CATEGORY);
        }
        POPUP_MENU_START(onModelSelectMenu);
      }
      killEvents(event);
      break;
    default:
      break;
  }

  int index = 0;
  coord_t y = 18;

  drawVerticalScrollbar(CATEGORIES_WIDTH-1, y-1, 4*(FH+7)-5, categoriesVerticalOffset, cats.size(), 5);

  // Categories
  for (std::list<ModelsCategory *>::const_iterator it = cats.begin(); it != cats.end(); ++it, ++index) {
    if (index >= categoriesVerticalOffset && index < categoriesVerticalOffset+5) {
      coord_t y = MENU_HEADER_HEIGHT*2 + 1 + (index - categoriesVerticalOffset)*FH*4/3;
      uint8_t k = index ;

      if (selectMode == MODE_RENAME_CATEGORY && currentCategory == *it) {
        lcdDrawSolidFilledRect(9, y, MODELSEL_W - 1 - 9, 7);
        lcdDrawRect(8, y - 1, MODELSEL_W - 1 - 7, 9,  DOTTED);
        editName(4, y, currentCategory->name, sizeof(currentCategory->name), event, BLINK, 0);

        if (s_editMode == 0 || event == EVT_KEY_BREAK(KEY_EXIT)) {
          modelslist.save();
          selectMode = MODE_SELECT_MODEL;
        }
      }
      else {
        lcdDrawSizedText(2, y, (*it)->name, sizeof((*it)->name),  ((categoriesVerticalPosition == k) ? INVERS : 0));
      }
      if (selectMode == MODE_MOVE_MODEL && categoriesVerticalPosition == k) {
        lcdDrawSolidFilledRect(9, y, MODELSEL_W - 1 - 9, 7);
        lcdDrawRect(8, y - 1, MODELSEL_W - 1 - 7, 9, selectMode == COPY_MODE ? SOLID : DOTTED);
      }
    }
  }

  // Models
  index = 0;
  bool selected = false;
  bool current = false;

  for (uint8_t i = 0; i < currentCategory->size(); i++) {
    coord_t y = MENU_HEADER_HEIGHT + 1 + i*FH;
    uint8_t k = i+menuVerticalOffset;

    if (k >= currentCategory->size())
      break;

    std::list<ModelCell *>::iterator it = currentCategory->begin();
    std::advance(it, k);

    selected = ((selectMode == MODE_SELECT_MODEL || selectMode == MODE_MOVE_MODEL) && k == menuVerticalPosition);
    current = !strncmp((*it)->modelFilename, g_eeGeneral.currModelFilename, LEN_MODEL_FILENAME);

    if (current) {
      lcdDrawChar(9 * FW + 11, y, '*');
      lcdDrawText(9 * FW + 18, y, g_model.header.name, LEADING0 | ((selected) ? INVERS : 0) | ZCHAR);
      subModelIndex = k;
    }
    else {
      lcdDrawText(9 * FW + 18, y, (*it)->modelName, LEADING0 | ((selected) ? INVERS : 0));
    }

    y += 16;

    if (selected) {
      lcdDrawText(5, LCD_H - FH - 1, (*it)->modelFilename, SMLSIZE);
    }
  }

  if (subModelIndex == menuVerticalPosition) {
    model_selected = true;
  }
  else {
    model_selected = false;
  }
}

