#include "pet_poc.h"

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include "pet_config.h"
#include "tic_tac_toe.h"

extern const lv_font_t* get_font_12();
extern const lv_font_t* get_font_14();
extern const lv_font_t* get_font_16();
extern void open_weather_screen(bool open_settings);
extern void on_pet_name_changed(const char *newName);

struct PetPocData {
  char name[21];
  uint32_t age_minutes;
  uint8_t state;
  uint8_t hunger;
  uint8_t happiness;
  uint8_t energy;
  uint8_t level;
  uint16_t exp;
  uint16_t total_games;
  uint16_t total_wins;
};

static Preferences petPrefs;
static PetPocData pet = {};
static TicTacToe ticTacToe;
static bool gameActive = false;
static bool petModeActive = false;

static lv_obj_t *petRoot = nullptr;
static lv_obj_t *nameLabel = nullptr;
static lv_obj_t *ageLabel = nullptr;
static lv_obj_t *stateLabel = nullptr;
static lv_obj_t *hintLabel = nullptr;
static lv_obj_t *petVisualLabel = nullptr;
static lv_obj_t *stageHintLabel = nullptr;
static lv_obj_t *hungerBar = nullptr;
static lv_obj_t *happyBar = nullptr;
static lv_obj_t *energyBar = nullptr;
static lv_obj_t *levelLabel = nullptr;
static lv_obj_t *gameStatusLabel = nullptr;
static lv_obj_t *boardButtons[9] = {nullptr};
static lv_obj_t *gameOverlay = nullptr;
static lv_obj_t *renameOverlay = nullptr;
static lv_obj_t *renameTextArea = nullptr;
static lv_obj_t *renameKeyboard = nullptr;
static lv_obj_t *toastLabel = nullptr;
static lv_timer_t *toastTimer = nullptr;
static lv_timer_t *botMoveTimer = nullptr;
static bool botMovePending = false;

static uint32_t lastMinuteTickMs = 0;
static uint32_t lastSaveMs = 0;
static uint32_t lastFedMinute = 0;
static uint32_t lastUiTapMs = 0;
static bool navTransitionInProgress = false;
static bool petTestMode = false;

#define PET_TEST_TICK_MS 3000U
#define PET_NORMAL_TICK_MS 60000U
#define PET_TEST_MINUTES_PER_TICK 15U
#define PET_NORMAL_MINUTES_PER_TICK 1U
#define PET_TEST_EGG_END_MINUTES 5U
#define PET_TEST_BABY_END_MINUTES 20U
#define PET_TEST_CHILD_END_MINUTES 60U
#define PET_TEST_ADULT_END_MINUTES 180U
#define PET_NORMAL_EGG_END_MINUTES (24U * 60U)
#define PET_NORMAL_BABY_END_MINUTES (7U * 24U * 60U)
#define PET_NORMAL_CHILD_END_MINUTES (14U * 24U * 60U)
#define PET_NORMAL_ADULT_END_MINUTES (60U * 24U * 60U)
#define PET_TEST_FEED_COOLDOWN_MIN 2U
#define PET_NORMAL_FEED_COOLDOWN_MIN 30U

#define MIN_HUNGER_TO_FEED 10U
#define UI_TAP_DEBOUNCE_MS 90U

static uint32_t pet_tick_ms() {
  return petTestMode ? PET_TEST_TICK_MS : PET_NORMAL_TICK_MS;
}

static uint32_t pet_minutes_per_tick() {
  return petTestMode ? PET_TEST_MINUTES_PER_TICK : PET_NORMAL_MINUTES_PER_TICK;
}

static uint32_t pet_egg_end_minutes() {
  return petTestMode ? PET_TEST_EGG_END_MINUTES : PET_NORMAL_EGG_END_MINUTES;
}

static uint32_t pet_baby_end_minutes() {
  return petTestMode ? PET_TEST_BABY_END_MINUTES : PET_NORMAL_BABY_END_MINUTES;
}

static uint32_t pet_child_end_minutes() {
  return petTestMode ? PET_TEST_CHILD_END_MINUTES : PET_NORMAL_CHILD_END_MINUTES;
}

static uint32_t pet_adult_end_minutes() {
  return petTestMode ? PET_TEST_ADULT_END_MINUTES : PET_NORMAL_ADULT_END_MINUTES;
}

static uint32_t feed_cooldown_min() {
  return petTestMode ? PET_TEST_FEED_COOLDOWN_MIN : PET_NORMAL_FEED_COOLDOWN_MIN;
}

static bool ui_tap_allowed() {
  if (gameActive) {
    return true;
  }
  uint32_t now = millis();
  if (now - lastUiTapMs < UI_TAP_DEBOUNCE_MS) {
    return false;
  }
  lastUiTapMs = now;
  return true;
}

static void finish_game(uint8_t state);
static const char *state_visual_text(uint8_t state, uint32_t stageProgress);
static void get_stage_progress(uint32_t *outPercent, uint32_t *outMinutesLeft);

static const char *state_to_text(uint8_t state) {
  switch (state) {
    case PET_STATE_EGG: return "Egg";
    case PET_STATE_BABY: return "Baby";
    case PET_STATE_CHILD: return "Child";
    case PET_STATE_ADULT: return "Adult";
    case PET_STATE_ELDERLY: return "Elderly";
    default: return "Unknown";
  }
}

static void update_pet_state_from_age() {
  if (pet.age_minutes < pet_egg_end_minutes()) {
    pet.state = PET_STATE_EGG;
  } else if (pet.age_minutes < pet_baby_end_minutes()) {
    pet.state = PET_STATE_BABY;
  } else if (pet.age_minutes < pet_child_end_minutes()) {
    pet.state = PET_STATE_CHILD;
  } else if (pet.age_minutes < pet_adult_end_minutes()) {
    pet.state = PET_STATE_ADULT;
  } else {
    pet.state = PET_STATE_ELDERLY;
  }

  pet.level = (pet.exp / EXP_PER_LEVEL) + 1;
}

static void load_pet() {
  if (!petPrefs.begin(PET_NVS_NAMESPACE, false)) {
    return;
  }

  String savedName = petPrefs.getString("name", "Aura");
  savedName.toCharArray(pet.name, sizeof(pet.name));
  pet.age_minutes = petPrefs.getUInt("age_min", 0);
  pet.state = petPrefs.getUChar("state", PET_STATE_EGG);
  pet.hunger = petPrefs.getUChar("hunger", 10);
  pet.happiness = petPrefs.getUChar("happy", 90);
  pet.energy = petPrefs.getUChar("energy", 90);
  pet.level = petPrefs.getUChar("level", 1);
  pet.exp = petPrefs.getUShort("exp", 0);
  pet.total_games = petPrefs.getUShort("games", 0);
  pet.total_wins = petPrefs.getUShort("wins", 0);
  lastFedMinute = petPrefs.getUInt("last_fed_m", 0);
  petTestMode = petPrefs.getBool("test_mode", false);

  if (strlen(pet.name) == 0) {
    strncpy(pet.name, "Aura", sizeof(pet.name) - 1);
    pet.name[sizeof(pet.name) - 1] = '\0';
  }

  update_pet_state_from_age();
}

static void save_pet() {
  petPrefs.putString("name", pet.name);
  petPrefs.putUInt("age_min", pet.age_minutes);
  petPrefs.putUChar("state", pet.state);
  petPrefs.putUChar("hunger", pet.hunger);
  petPrefs.putUChar("happy", pet.happiness);
  petPrefs.putUChar("energy", pet.energy);
  petPrefs.putUChar("level", pet.level);
  petPrefs.putUShort("exp", pet.exp);
  petPrefs.putUShort("games", pet.total_games);
  petPrefs.putUShort("wins", pet.total_wins);
  petPrefs.putUInt("last_fed_m", lastFedMinute);
  petPrefs.putBool("test_mode", petTestMode);
}

static void refresh_pet_labels() {
  if (!petRoot) return;

  if (nameLabel) {
    lv_label_set_text(nameLabel, pet.name);
  }

  char ageBuf[32];
  snprintf(ageBuf, sizeof(ageBuf), "Age: %luh", (unsigned long)(pet.age_minutes / 60U));
  lv_label_set_text(ageLabel, ageBuf);

  char levelBuf[48];
  snprintf(levelBuf, sizeof(levelBuf), "Lvl %u | EXP %u | W %u/%u", pet.level, pet.exp, pet.total_wins, pet.total_games);
  lv_label_set_text(levelLabel, levelBuf);

  char stateBuf[24];
  snprintf(stateBuf, sizeof(stateBuf), "State: %s", state_to_text(pet.state));
  lv_label_set_text(stateLabel, stateBuf);

  if (hintLabel) {
    uint32_t cooldown = feed_cooldown_min();
    uint32_t minsSinceFeed = (pet.age_minutes >= lastFedMinute) ? (pet.age_minutes - lastFedMinute) : 0;
    if (minsSinceFeed < cooldown) {
      char hintBuf[64];
      snprintf(hintBuf, sizeof(hintBuf), "Feed cooldown: %lum", (unsigned long)(cooldown - minsSinceFeed));
      lv_label_set_text(hintLabel, hintBuf);
    } else {
      lv_label_set_text(hintLabel, "Feed/Play/Sleep | Use top bar for navigation");
    }
  }

  uint32_t stagePercent = 0;
  uint32_t stageMinutesLeft = 0;
  get_stage_progress(&stagePercent, &stageMinutesLeft);

  if (petVisualLabel) {
    lv_label_set_text(petVisualLabel, state_visual_text(pet.state, stagePercent));
  }

  if (stageHintLabel) {
    if (pet.state == PET_STATE_EGG && stagePercent >= 80U) {
      char hatchBuf[64];
      snprintf(hatchBuf, sizeof(hatchBuf), "Hatching soon (%lum)", (unsigned long)stageMinutesLeft);
      lv_label_set_text(stageHintLabel, hatchBuf);
    } else if (pet.state == PET_STATE_ELDERLY) {
      lv_label_set_text(stageHintLabel, "Final stage");
    } else {
      char stageBuf[64];
      snprintf(stageBuf, sizeof(stageBuf), "Stage progress: %lu%%", (unsigned long)stagePercent);
      lv_label_set_text(stageHintLabel, stageBuf);
    }
  }

  lv_bar_set_value(hungerBar, pet.hunger, LV_ANIM_OFF);
  lv_bar_set_value(happyBar, pet.happiness, LV_ANIM_OFF);
  lv_bar_set_value(energyBar, pet.energy, LV_ANIM_OFF);
}

static void board_refresh() {
  const uint8_t *board = ticTacToe.get_board();
  for (int i = 0; i < 9; i++) {
    lv_obj_t *cellLabel = lv_obj_get_child(boardButtons[i], 0);
    if (!cellLabel) continue;
    if (board[i] == 1) lv_label_set_text(cellLabel, "X");
    else if (board[i] == 2) lv_label_set_text(cellLabel, "O");
    else lv_label_set_text(cellLabel, " ");
  }
}

static void close_game_overlay() {
  if (gameOverlay) {
    lv_obj_del(gameOverlay);
    gameOverlay = nullptr;
  }
  if (botMoveTimer) {
    lv_timer_del(botMoveTimer);
    botMoveTimer = nullptr;
  }
  botMovePending = false;
  gameActive = false;
  if (petModeActive) {
    refresh_pet_labels();
  }
}

static void bot_move_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (botMoveTimer) {
    lv_timer_del(botMoveTimer);
    botMoveTimer = nullptr;
  }

  if (!gameActive || !gameOverlay) {
    botMovePending = false;
    return;
  }

  ticTacToe.bot_move();
  board_refresh();

  if (ticTacToe.is_game_over()) {
    finish_game(ticTacToe.get_game_state());
  } else if (gameStatusLabel) {
    lv_label_set_text(gameStatusLabel, "Your turn");
  }

  botMovePending = false;
}

static void close_rename_overlay() {
  if (renameOverlay) {
    lv_obj_del(renameOverlay);
    renameOverlay = nullptr;
    renameTextArea = nullptr;
    renameKeyboard = nullptr;
  }
}

static void toast_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (toastLabel) {
    lv_obj_del(toastLabel);
    toastLabel = nullptr;
  }
  if (toastTimer) {
    lv_timer_del(toastTimer);
    toastTimer = nullptr;
  }
}

static void show_toast(const char *message) {
  if (!petRoot || !message) {
    return;
  }

  if (toastLabel) {
    lv_obj_del(toastLabel);
    toastLabel = nullptr;
  }
  if (toastTimer) {
    lv_timer_del(toastTimer);
    toastTimer = nullptr;
  }

  toastLabel = lv_label_create(petRoot);
  lv_label_set_text(toastLabel, message);
  lv_obj_set_style_bg_color(toastLabel, lv_color_hex(0x2b2b2b), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(toastLabel, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(toastLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(toastLabel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(toastLabel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(toastLabel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(toastLabel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(toastLabel, LV_ALIGN_TOP_MID, 0, 32);

  toastTimer = lv_timer_create(toast_timer_cb, 1200, nullptr);
}

static void game_back_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  close_game_overlay();
}

static void rename_cancel_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  close_rename_overlay();
}

static void finish_game(uint8_t state) {
  pet.total_games++;
  pet.energy = (pet.energy > PLAY_ENERGY_COST) ? (pet.energy - PLAY_ENERGY_COST) : 0;

  if (state == 1) {
    pet.total_wins++;
    pet.happiness = (pet.happiness + PLAY_HAPPINESS_GAIN > 100) ? 100 : pet.happiness + PLAY_HAPPINESS_GAIN;
    pet.exp += PLAY_EXP_GAIN;
    lv_label_set_text(gameStatusLabel, "You win!");
  } else if (state == 3) {
    lv_label_set_text(gameStatusLabel, "Bot wins");
  } else {
    pet.exp += PLAY_EXP_GAIN / 2;
    lv_label_set_text(gameStatusLabel, "Draw");
  }

  update_pet_state_from_age();
  save_pet();
}

static void rename_save_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  if (!renameTextArea) {
    close_rename_overlay();
    return;
  }

  const char *candidate = lv_textarea_get_text(renameTextArea);
  if (!candidate || !pet_poc_set_name(candidate)) {
    show_toast("Name cannot be empty");
    return;
  }
  show_toast("Name saved");
  close_rename_overlay();
}

static void rename_keyboard_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    rename_save_cb(e);
  } else if (code == LV_EVENT_CANCEL) {
    rename_cancel_cb(e);
  }
}

static void show_rename_overlay() {
  if (!petRoot || renameOverlay) {
    return;
  }

  renameOverlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(renameOverlay, 228, 260);
  lv_obj_center(renameOverlay);

  lv_obj_t *title = lv_label_create(renameOverlay);
  lv_label_set_text(title, "Rename Pet");
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  renameTextArea = lv_textarea_create(renameOverlay);
  lv_obj_set_size(renameTextArea, 204, 34);
  lv_obj_align(renameTextArea, LV_ALIGN_TOP_MID, 0, 36);
  lv_textarea_set_max_length(renameTextArea, 20);
  lv_textarea_set_one_line(renameTextArea, true);
  lv_textarea_set_text(renameTextArea, pet.name);

  renameKeyboard = lv_keyboard_create(renameOverlay);
  lv_obj_set_size(renameKeyboard, 204, 126);
  lv_obj_align(renameKeyboard, LV_ALIGN_TOP_MID, 0, 78);
  lv_keyboard_set_mode(renameKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(renameKeyboard, renameTextArea);
  lv_obj_add_event_cb(renameKeyboard, rename_keyboard_event_cb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(renameKeyboard, rename_keyboard_event_cb, LV_EVENT_CANCEL, nullptr);

  lv_obj_t *saveBtn = lv_btn_create(renameOverlay);
  lv_obj_set_size(saveBtn, 88, 28);
  lv_obj_align(saveBtn, LV_ALIGN_BOTTOM_LEFT, 14, -8);
  lv_obj_add_event_cb(saveBtn, rename_save_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *saveLbl = lv_label_create(saveBtn);
  lv_label_set_text(saveLbl, "Save");
  lv_obj_center(saveLbl);

  lv_obj_t *cancelBtn = lv_btn_create(renameOverlay);
  lv_obj_set_size(cancelBtn, 88, 28);
  lv_obj_align(cancelBtn, LV_ALIGN_BOTTOM_RIGHT, -14, -8);
  lv_obj_add_event_cb(cancelBtn, rename_cancel_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
  lv_label_set_text(cancelLbl, "Cancel");
  lv_obj_center(cancelLbl);
}

static void board_btn_cb(lv_event_t *e) {
  if (!ui_tap_allowed()) {
    return;
  }
  if (botMovePending) {
    return;
  }
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  int position = -1;
  for (int i = 0; i < 9; i++) {
    if (boardButtons[i] == btn) {
      position = i;
      break;
    }
  }
  if (position < 0) return;

  if (!ticTacToe.player_move((uint8_t)position)) return;
  board_refresh();

  if (ticTacToe.is_game_over()) {
    finish_game(ticTacToe.get_game_state());
    return;
  }

  botMovePending = true;
  if (gameStatusLabel) {
    lv_label_set_text(gameStatusLabel, "Bot...");
  }
  if (botMoveTimer) {
    lv_timer_del(botMoveTimer);
  }
  botMoveTimer = lv_timer_create(bot_move_timer_cb, 1, nullptr);
  lv_timer_set_repeat_count(botMoveTimer, 1);
}

static void show_game_overlay() {
  if (pet.state < PET_STATE_CHILD) {
    lv_label_set_text(stateLabel, "State: Child required to play");
    return;
  }

  if (pet.energy < PLAY_ENERGY_COST) {
    lv_label_set_text(stateLabel, "State: Too tired to play");
    return;
  }

  ticTacToe.reset_game();
  ticTacToe.set_difficulty(pet.level);

  gameOverlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(gameOverlay, 228, 284);
  lv_obj_center(gameOverlay);

  lv_obj_t *title = lv_label_create(gameOverlay);
  lv_label_set_text(title, "Tic Tac Toe");
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  int startX = 28;
  int startY = 34;
  int size = 52;
  int gap = 4;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int idx = row * 3 + col;
      boardButtons[idx] = lv_btn_create(gameOverlay);
      lv_obj_set_size(boardButtons[idx], size, size);
      lv_obj_set_pos(boardButtons[idx], startX + col * (size + gap), startY + row * (size + gap));
      lv_obj_add_event_cb(boardButtons[idx], board_btn_cb, LV_EVENT_CLICKED, nullptr);
      lv_obj_t *lbl = lv_label_create(boardButtons[idx]);
      lv_label_set_text(lbl, " ");
      lv_obj_center(lbl);
    }
  }

  gameStatusLabel = lv_label_create(gameOverlay);
  lv_label_set_text(gameStatusLabel, "Your turn");
  lv_obj_align(gameStatusLabel, LV_ALIGN_BOTTOM_MID, 0, -42);

  lv_obj_t *backBtn = lv_btn_create(gameOverlay);
  lv_obj_set_size(backBtn, 70, 26);
  lv_obj_align(backBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(backBtn, game_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *backLbl = lv_label_create(backBtn);
  lv_label_set_text(backLbl, "Back");
  lv_obj_center(backLbl);

  board_refresh();
  gameActive = true;
}

static void feed_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  uint32_t minsSinceFeed = (pet.age_minutes >= lastFedMinute) ? (pet.age_minutes - lastFedMinute) : 0;
  uint32_t cooldown = feed_cooldown_min();
  if (minsSinceFeed < cooldown) {
    if (hintLabel) {
      char hintBuf[64];
      snprintf(hintBuf, sizeof(hintBuf), "Feed cooldown: %lum", (unsigned long)(cooldown - minsSinceFeed));
      lv_label_set_text(hintLabel, hintBuf);
    }
    return;
  }

  if (pet.hunger < MIN_HUNGER_TO_FEED) {
    if (hintLabel) {
      lv_label_set_text(hintLabel, "Not hungry yet");
    }
    return;
  }

  pet.hunger = (pet.hunger > FEED_HUNGER_REDUCTION) ? (pet.hunger - FEED_HUNGER_REDUCTION) : 0;
  pet.exp += FEED_EXP_GAIN;
  lastFedMinute = pet.age_minutes;
  update_pet_state_from_age();
  refresh_pet_labels();
  save_pet();
}

static void play_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  if (!gameActive) {
    show_game_overlay();
  }
}

static void sleep_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  pet.energy = 100;
  pet.happiness = (pet.happiness + 5 > 100) ? 100 : pet.happiness + 5;
  refresh_pet_labels();
  save_pet();
}

static void rename_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed()) {
    return;
  }
  show_rename_overlay();
}

static void weather_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed() || navTransitionInProgress) {
    return;
  }
  navTransitionInProgress = true;
  open_weather_screen(false);
}

static void settings_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!ui_tap_allowed() || navTransitionInProgress) {
    return;
  }
  navTransitionInProgress = true;
  open_weather_screen(true);
}

static lv_obj_t *make_stat_row(lv_obj_t *parent, const char *name, int y, lv_obj_t **barOut) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, name);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, y);

  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_size(bar, 120, 12);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 80, y + 2);
  lv_bar_set_range(bar, 0, 100);
  *barOut = bar;
  return bar;
}

void pet_poc_init() {
  load_pet();
  petModeActive = true;
  navTransitionInProgress = false;
  lastUiTapMs = 0;

  petRoot = lv_obj_create(lv_scr_act());
  lv_obj_set_size(petRoot, 240, 320);
  lv_obj_set_pos(petRoot, 0, 0);
  lv_obj_clear_flag(petRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(petRoot, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(petRoot, 0, LV_PART_MAIN);

  nameLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(nameLabel, get_font_16(), 0);
  lv_label_set_text(nameLabel, pet.name);
  lv_obj_align(nameLabel, LV_ALIGN_TOP_LEFT, 10, 34);

  ageLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(ageLabel, get_font_12(), 0);
  lv_label_set_text(ageLabel, "Age: 0h");
  lv_obj_align(ageLabel, LV_ALIGN_TOP_RIGHT, -10, 36);

  stateLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(stateLabel, get_font_14(), 0);
  lv_label_set_text(stateLabel, "State: Egg");
  lv_obj_align(stateLabel, LV_ALIGN_TOP_LEFT, 10, 56);

  hintLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(hintLabel, get_font_12(), 0);
  lv_label_set_text(hintLabel, "Feed/Play/Sleep | Top bar has navigation");
  lv_obj_set_width(hintLabel, 124);
  lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(hintLabel, LV_ALIGN_TOP_LEFT, 10, 74);

  petVisualLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(petVisualLabel, get_font_12(), 0);
  lv_obj_set_style_text_align(petVisualLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_width(petVisualLabel, 96);
  lv_label_set_long_mode(petVisualLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(petVisualLabel, "  ___\n /   \\\n \\___/");
  lv_obj_align(petVisualLabel, LV_ALIGN_TOP_RIGHT, -8, 54);

  stageHintLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(stageHintLabel, get_font_12(), 0);
  lv_obj_set_width(stageHintLabel, 110);
  lv_label_set_long_mode(stageHintLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(stageHintLabel, "Stage progress");
  lv_obj_set_style_text_align(stageHintLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(stageHintLabel, LV_ALIGN_TOP_RIGHT, -8, 96);

  make_stat_row(petRoot, "Hunger", 112, &hungerBar);
  make_stat_row(petRoot, "Happy", 137, &happyBar);
  make_stat_row(petRoot, "Energy", 162, &energyBar);

  levelLabel = lv_label_create(petRoot);
  lv_label_set_text(levelLabel, "Lvl 1 | EXP 0");
  lv_obj_align(levelLabel, LV_ALIGN_TOP_LEFT, 10, 186);

  lv_obj_t *renameBtn = lv_btn_create(petRoot);
  lv_obj_set_size(renameBtn, 78, 24);
  lv_obj_align(renameBtn, LV_ALIGN_TOP_LEFT, 10, 208);
  lv_obj_add_event_cb(renameBtn, rename_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *renameLbl = lv_label_create(renameBtn);
  lv_label_set_text(renameLbl, "Rename");
  lv_obj_center(renameLbl);

  lv_obj_t *modeLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(modeLabel, get_font_12(), 0);
  lv_label_set_text(modeLabel, petTestMode ? "Mode: Test" : "Mode: Normal");
  lv_obj_align(modeLabel, LV_ALIGN_TOP_RIGHT, -10, 210);

  lv_obj_t *feedBtn = lv_btn_create(petRoot);
  lv_obj_set_size(feedBtn, 62, 30);
  lv_obj_align(feedBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(feedBtn, feed_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *feedLbl = lv_label_create(feedBtn);
  lv_label_set_text(feedLbl, "Feed");
  lv_obj_center(feedLbl);

  lv_obj_t *playBtn = lv_btn_create(petRoot);
  lv_obj_set_size(playBtn, 62, 30);
  lv_obj_align(playBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(playBtn, play_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *playLbl = lv_label_create(playBtn);
  lv_label_set_text(playLbl, "Play");
  lv_obj_center(playLbl);

  lv_obj_t *sleepBtn = lv_btn_create(petRoot);
  lv_obj_set_size(sleepBtn, 62, 30);
  lv_obj_align(sleepBtn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(sleepBtn, sleep_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sleepLbl = lv_label_create(sleepBtn);
  lv_label_set_text(sleepLbl, "Sleep");
  lv_obj_center(sleepLbl);

  lv_obj_t *btn_pet = lv_btn_create(petRoot);
  lv_obj_set_size(btn_pet, 64, 24);
  lv_obj_align(btn_pet, LV_ALIGN_TOP_LEFT, 8, 4);
  lv_obj_clear_flag(btn_pet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(btn_pet, lv_color_hex(0x3e7fae), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_t *lbl_pet = lv_label_create(btn_pet);
  lv_obj_set_style_text_font(lbl_pet, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_pet, "Pet");
  lv_obj_center(lbl_pet);

  lv_obj_t *btn_weather = lv_btn_create(petRoot);
  lv_obj_set_size(btn_weather, 76, 24);
  lv_obj_align(btn_weather, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_add_event_cb(btn_weather, weather_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_weather = lv_label_create(btn_weather);
  lv_obj_set_style_text_font(lbl_weather, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_weather, "Weather");
  lv_obj_center(lbl_weather);

  lv_obj_t *btn_settings = lv_btn_create(petRoot);
  lv_obj_set_size(btn_settings, 76, 24);
  lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, -8, 4);
  lv_obj_add_event_cb(btn_settings, settings_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_settings = lv_label_create(btn_settings);
  lv_obj_set_style_text_font(lbl_settings, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_settings, "Settings");
  lv_obj_center(lbl_settings);

  refresh_pet_labels();
  lastMinuteTickMs = millis();
  lastSaveMs = millis();
}

void pet_poc_loop() {
  if (!petModeActive || !petRoot) {
    return;
  }

  uint32_t now = millis();
  if (now - lastMinuteTickMs >= pet_tick_ms()) {
    lastMinuteTickMs = now;
    pet.age_minutes += pet_minutes_per_tick();
    pet.hunger = (pet.hunger + 1 > 100) ? 100 : pet.hunger + 1;
    pet.happiness = (pet.happiness > 0) ? pet.happiness - 1 : 0;
    pet.energy = (pet.energy > 0) ? pet.energy - 1 : 0;
    pet.exp += 1;
    update_pet_state_from_age();
    refresh_pet_labels();
  }

  if (now - lastSaveMs >= PET_SAVE_INTERVAL_MS) {
    lastSaveMs = now;
    save_pet();
  }
}

const char *pet_poc_get_name() {
  if (strlen(pet.name) == 0) {
    return "Aura";
  }
  return pet.name;
}

bool pet_poc_set_name(const char *newName) {
  if (!newName) {
    return false;
  }

  while (*newName == ' ') {
    newName++;
  }
  if (*newName == '\0') {
    return false;
  }

  strncpy(pet.name, newName, sizeof(pet.name) - 1);
  pet.name[sizeof(pet.name) - 1] = '\0';

  size_t len = strlen(pet.name);
  while (len > 0 && pet.name[len - 1] == ' ') {
    pet.name[len - 1] = '\0';
    len--;
  }

  if (strlen(pet.name) == 0) {
    return false;
  }

  if (petRoot) {
    refresh_pet_labels();
  }
  save_pet();
  on_pet_name_changed(pet.name);
  return true;
}

bool pet_poc_is_test_mode() {
  return petTestMode;
}

void pet_poc_set_test_mode(bool enabled) {
  petTestMode = enabled;
  if (petRoot) {
    refresh_pet_labels();
    show_toast(enabled ? "Testing mode ON" : "Testing mode OFF");
  }
  save_pet();
}

void pet_poc_reset_data() {
  memset(&pet, 0, sizeof(pet));
  strncpy(pet.name, "Aura", sizeof(pet.name) - 1);
  pet.name[sizeof(pet.name) - 1] = '\0';
  pet.hunger = 10;
  pet.happiness = 90;
  pet.energy = 90;
  pet.level = 1;
  pet.exp = 0;
  pet.total_games = 0;
  pet.total_wins = 0;
  pet.state = PET_STATE_EGG;
  lastFedMinute = 0;
  update_pet_state_from_age();
  save_pet();
  if (petRoot) {
    refresh_pet_labels();
    show_toast("Pet data reset");
  }
  on_pet_name_changed(pet.name);
}

void pet_poc_deactivate() {
  petModeActive = false;
  navTransitionInProgress = true;
  if (botMoveTimer) {
    lv_timer_del(botMoveTimer);
    botMoveTimer = nullptr;
  }
  botMovePending = false;
  if (toastLabel) {
    lv_obj_del(toastLabel);
    toastLabel = nullptr;
  }
  if (toastTimer) {
    lv_timer_del(toastTimer);
    toastTimer = nullptr;
  }
  if (gameOverlay) {
    lv_obj_del(gameOverlay);
    gameOverlay = nullptr;
  }
  close_rename_overlay();
  gameActive = false;
  petRoot = nullptr;
  nameLabel = nullptr;
  ageLabel = nullptr;
  stateLabel = nullptr;
  hintLabel = nullptr;
  petVisualLabel = nullptr;
  stageHintLabel = nullptr;
  hungerBar = nullptr;
  happyBar = nullptr;
  energyBar = nullptr;
  levelLabel = nullptr;
  gameStatusLabel = nullptr;
  for (int i = 0; i < 9; i++) {
    boardButtons[i] = nullptr;
  }
}

static const char *state_visual_text(uint8_t state, uint32_t stageProgress) {
  switch (state) {
    case PET_STATE_EGG:
      if (stageProgress >= 80U) {
        return "  ___\n /_ _\\\n \\_-_/";
      }
      return "  ___\n /   \\\n \\___/";
    case PET_STATE_BABY:
      return " (o_o)\n /|_|\\";
    case PET_STATE_CHILD:
      return " (^_^)\n /| |\\";
    case PET_STATE_ADULT:
      return " \\^_^/\n  / \\";
    case PET_STATE_ELDERLY:
      return " (-_-)\n /| |\\";
    default:
      return " (?)";
  }
}

static void get_stage_progress(uint32_t *outPercent, uint32_t *outMinutesLeft) {
  uint32_t percent = 100U;
  uint32_t minsLeft = 0U;
  uint32_t age = pet.age_minutes;

  uint32_t start = 0U;
  uint32_t end = 0U;
  if (pet.state == PET_STATE_EGG) {
    start = 0U;
    end = pet_egg_end_minutes();
  } else if (pet.state == PET_STATE_BABY) {
    start = pet_egg_end_minutes();
    end = pet_baby_end_minutes();
  } else if (pet.state == PET_STATE_CHILD) {
    start = pet_baby_end_minutes();
    end = pet_child_end_minutes();
  } else if (pet.state == PET_STATE_ADULT) {
    start = pet_child_end_minutes();
    end = pet_adult_end_minutes();
  } else {
    start = pet_adult_end_minutes();
    end = pet_adult_end_minutes();
  }

  if (end > start) {
    uint32_t span = end - start;
    uint32_t elapsed = (age <= start) ? 0U : (age - start);
    if (elapsed > span) elapsed = span;
    percent = (elapsed * 100U) / span;
    minsLeft = (age < end) ? (end - age) : 0U;
  }

  if (outPercent) *outPercent = percent;
  if (outMinutesLeft) *outMinutesLeft = minsLeft;
}