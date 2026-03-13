#include "pet_poc.h"

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include "pet_config.h"
#include "tic_tac_toe.h"

extern const lv_font_t* get_font_12();
extern const lv_font_t* get_font_14();
extern const lv_font_t* get_font_16();
extern void create_ui();
extern void fetch_and_update_weather();

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
static lv_obj_t *hungerBar = nullptr;
static lv_obj_t *happyBar = nullptr;
static lv_obj_t *energyBar = nullptr;
static lv_obj_t *levelLabel = nullptr;
static lv_obj_t *gameStatusLabel = nullptr;
static lv_obj_t *boardButtons[9] = {nullptr};
static lv_obj_t *gameOverlay = nullptr;

static uint32_t lastMinuteTickMs = 0;
static uint32_t lastSaveMs = 0;
static uint32_t lastFedMinute = 0;

#define FEED_COOLDOWN_MIN 30U
#define MIN_HUNGER_TO_FEED 10U

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
  if (pet.age_minutes < (24U * 60U)) {
    pet.state = PET_STATE_EGG;
  } else if (pet.age_minutes < (7U * 24U * 60U)) {
    pet.state = PET_STATE_BABY;
  } else if (pet.age_minutes < (14U * 24U * 60U)) {
    pet.state = PET_STATE_CHILD;
  } else if (pet.age_minutes < (60U * 24U * 60U)) {
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
}

static void refresh_pet_labels() {
  if (!petRoot) return;

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
    uint32_t minsSinceFeed = (pet.age_minutes >= lastFedMinute) ? (pet.age_minutes - lastFedMinute) : 0;
    if (minsSinceFeed < FEED_COOLDOWN_MIN) {
      char hintBuf[64];
      snprintf(hintBuf, sizeof(hintBuf), "Feed cooldown: %lum", (unsigned long)(FEED_COOLDOWN_MIN - minsSinceFeed));
      lv_label_set_text(hintLabel, hintBuf);
    } else {
      lv_label_set_text(hintLabel, "Feed/Play/Sleep | Weather button opens forecast");
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
  gameActive = false;
  if (petModeActive) {
    refresh_pet_labels();
  }
}

static void game_back_cb(lv_event_t *e) {
  LV_UNUSED(e);
  close_game_overlay();
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

static void board_btn_cb(lv_event_t *e) {
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

  ticTacToe.bot_move();
  board_refresh();
  if (ticTacToe.is_game_over()) {
    finish_game(ticTacToe.get_game_state());
  }
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
  lv_obj_set_size(gameOverlay, 220, 250);
  lv_obj_center(gameOverlay);

  lv_obj_t *title = lv_label_create(gameOverlay);
  lv_label_set_text(title, "Tic Tac Toe");
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  int startX = 18;
  int startY = 40;
  int size = 56;
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
  lv_obj_align(gameStatusLabel, LV_ALIGN_BOTTOM_MID, 0, -34);

  lv_obj_t *backBtn = lv_btn_create(gameOverlay);
  lv_obj_set_size(backBtn, 70, 26);
  lv_obj_align(backBtn, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_add_event_cb(backBtn, game_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *backLbl = lv_label_create(backBtn);
  lv_label_set_text(backLbl, "Back");
  lv_obj_center(backLbl);

  board_refresh();
  gameActive = true;
}

static void feed_cb(lv_event_t *e) {
  LV_UNUSED(e);
  uint32_t minsSinceFeed = (pet.age_minutes >= lastFedMinute) ? (pet.age_minutes - lastFedMinute) : 0;
  if (minsSinceFeed < FEED_COOLDOWN_MIN) {
    if (hintLabel) {
      char hintBuf[64];
      snprintf(hintBuf, sizeof(hintBuf), "Feed cooldown: %lum", (unsigned long)(FEED_COOLDOWN_MIN - minsSinceFeed));
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
  if (!gameActive) {
    show_game_overlay();
  }
}

static void sleep_cb(lv_event_t *e) {
  LV_UNUSED(e);
  pet.energy = 100;
  pet.happiness = (pet.happiness + 5 > 100) ? 100 : pet.happiness + 5;
  refresh_pet_labels();
  save_pet();
}

static void weather_cb(lv_event_t *e) {
  LV_UNUSED(e);
  pet_poc_deactivate();
  if (gameActive && gameOverlay) {
    lv_obj_del(gameOverlay);
    gameOverlay = nullptr;
    gameActive = false;
  }
  lv_obj_clean(lv_scr_act());
  create_ui();
  fetch_and_update_weather();
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

  petRoot = lv_obj_create(lv_scr_act());
  lv_obj_set_size(petRoot, 240, 320);
  lv_obj_set_pos(petRoot, 0, 0);
  lv_obj_clear_flag(petRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(petRoot, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(petRoot, 0, LV_PART_MAIN);

  nameLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(nameLabel, get_font_16(), 0);
  lv_label_set_text(nameLabel, pet.name);
  lv_obj_align(nameLabel, LV_ALIGN_TOP_LEFT, 10, 8);

  ageLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(ageLabel, get_font_12(), 0);
  lv_label_set_text(ageLabel, "Age: 0h");
  lv_obj_align(ageLabel, LV_ALIGN_TOP_RIGHT, -10, 10);

  stateLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(stateLabel, get_font_14(), 0);
  lv_label_set_text(stateLabel, "State: Egg");
  lv_obj_align(stateLabel, LV_ALIGN_TOP_LEFT, 10, 34);

  hintLabel = lv_label_create(petRoot);
  lv_obj_set_style_text_font(hintLabel, get_font_12(), 0);
  lv_label_set_text(hintLabel, "Feed/Play/Sleep | Weather button opens forecast");
  lv_obj_set_width(hintLabel, 210);
  lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(hintLabel, LV_ALIGN_TOP_LEFT, 10, 52);

  make_stat_row(petRoot, "Hunger", 90, &hungerBar);
  make_stat_row(petRoot, "Happy", 115, &happyBar);
  make_stat_row(petRoot, "Energy", 140, &energyBar);

  levelLabel = lv_label_create(petRoot);
  lv_label_set_text(levelLabel, "Lvl 1 | EXP 0");
  lv_obj_align(levelLabel, LV_ALIGN_TOP_LEFT, 10, 165);

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

  lv_obj_t *weatherBtn = lv_btn_create(petRoot);
  lv_obj_set_size(weatherBtn, 92, 34);
  lv_obj_align(weatherBtn, LV_ALIGN_BOTTOM_MID, 0, -52);
  lv_obj_add_event_cb(weatherBtn, weather_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *weatherLbl = lv_label_create(weatherBtn);
  lv_label_set_text(weatherLbl, "Go Weather");
  lv_obj_center(weatherLbl);

  refresh_pet_labels();
  lastMinuteTickMs = millis();
  lastSaveMs = millis();
}

void pet_poc_loop() {
  if (!petModeActive || !petRoot) {
    return;
  }

  uint32_t now = millis();
  if (now - lastMinuteTickMs >= 60000) {
    lastMinuteTickMs = now;
    pet.age_minutes += 1;
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

void pet_poc_deactivate() {
  petModeActive = false;
  if (gameOverlay) {
    lv_obj_del(gameOverlay);
    gameOverlay = nullptr;
  }
  gameActive = false;
  petRoot = nullptr;
  nameLabel = nullptr;
  ageLabel = nullptr;
  stateLabel = nullptr;
  hintLabel = nullptr;
  hungerBar = nullptr;
  happyBar = nullptr;
  energyBar = nullptr;
  levelLabel = nullptr;
  gameStatusLabel = nullptr;
  for (int i = 0; i < 9; i++) {
    boardButtons[i] = nullptr;
  }
}