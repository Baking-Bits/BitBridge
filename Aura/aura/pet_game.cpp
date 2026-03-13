#include "pet_game.h"
#include <Arduino.h>
#include <cstring>
#include <time.h>

// Static member initialization
PetState PetGame::current_pet = {};
TicTacToe PetGame::tictactoe_game = TicTacToe();
uint32_t PetGame::last_update_time = 0;
uint32_t PetGame::last_save_time = 0;
bool PetGame::has_unsaved_changes = false;

const char *TAG = "PetGame";

bool PetGame::initialize() {
  if (!PetStorage::initialize()) {
    Serial.printf("[%s] Failed to initialize storage\n", TAG);
    return false;
  }

  // Try to load existing pet
  if (PetStorage::load_pet_state(current_pet)) {
    Serial.printf("[%s] Loaded existing pet: %s\n", TAG, current_pet.name);
  } else {
    // Create new pet
    Serial.printf("[%s] Creating new pet\n", TAG);
    memset(&current_pet, 0, sizeof(current_pet));
    
    PetStorage::generate_uuid(current_pet.pet_id, sizeof(current_pet.pet_id));
    strcpy(current_pet.name, "Aura");
    current_pet.birth_time = get_current_time();
    current_pet.current_state = PET_STATE_EGG;
    current_pet.hunger = 0;
    current_pet.happiness = 100;
    current_pet.energy = 100;
    current_pet.level = 1;
    current_pet.exp = 0;
    current_pet.last_fed = current_pet.birth_time;
    current_pet.last_played = 0;
    current_pet.total_games = 0;
    current_pet.total_wins = 0;
    current_pet.tic_tac_toe_wins = 0;
    current_pet.version = 1;

    save_pet();
  }

  last_update_time = millis();
  last_save_time = millis();
  
  return true;
}

void PetGame::feed_pet() {
  if (current_pet.hunger >= FEED_HUNGER_REDUCTION) {
    current_pet.hunger -= FEED_HUNGER_REDUCTION;
  } else {
    current_pet.hunger = 0;
  }

  current_pet.last_fed = get_current_time();
  current_pet.exp += FEED_EXP_GAIN;
  has_unsaved_changes = true;

  Serial.printf("[%s] Pet fed. Hunger: %d, Exp: %d\n", TAG, current_pet.hunger, current_pet.exp);
}

void PetGame::pet_interact() {
  current_pet.happiness = (current_pet.happiness + TOUCH_HAPPINESS_GAIN > STAT_MAX) ? 
    STAT_MAX : current_pet.happiness + TOUCH_HAPPINESS_GAIN;
  has_unsaved_changes = true;

  Serial.printf("[%s] Pet interacted. Happiness: %d\n", TAG, current_pet.happiness);
}

bool PetGame::can_play_game() {
  uint32_t pet_age_seconds = get_current_time() - current_pet.birth_time;
  return pet_age_seconds >= GAME_UNLOCK_AGE && current_pet.energy >= 20;
}

void PetGame::start_game() {
  tictactoe_game.reset_game();
  tictactoe_game.set_difficulty(current_pet.level);
  Serial.printf("[%s] Started game. Difficulty: %d\n", TAG, current_pet.level);
}

void PetGame::end_game(uint8_t result) {
  // result: 0=loss, 1=draw, 2=win
  uint32_t duration_sec = (tictactoe_game.get_move_count() * 3) / 2;  // Rough estimate

  current_pet.energy -= PLAY_ENERGY_COST;
  if (current_pet.energy < 0) current_pet.energy = 0;

  current_pet.total_games++;

  if (result == GAME_WIN) {
    current_pet.total_wins++;
    current_pet.tic_tac_toe_wins++;
    current_pet.happiness = (current_pet.happiness + PLAY_HAPPINESS_GAIN > STAT_MAX) ? 
      STAT_MAX : current_pet.happiness + PLAY_HAPPINESS_GAIN;
    current_pet.exp += PLAY_EXP_GAIN;
  } else if (result == GAME_DRAW) {
    current_pet.exp += (PLAY_EXP_GAIN / 2);
  }

  current_pet.last_played = get_current_time();
  has_unsaved_changes = true;

  // Record in history
  GameHistoryEntry game_entry = {
    get_current_time(),
    GAME_TIC_TAC_TOE,
    0,
    0,
    result,
    (uint16_t)duration_sec
  };
  PetStorage::add_game_history(game_entry);

  Serial.printf("[%s] Game ended. Result: %d, Total wins: %d\n", 
    TAG, result, current_pet.total_wins);
}

void PetGame::update_stats() {
  uint32_t now = millis();
  if (now - last_update_time < UPDATE_INTERVAL_MS) {
    return;  // Not time to update yet
  }

  uint32_t elapsed_ms = now - last_update_time;
  uint32_t elapsed_hours = (elapsed_ms / 3600000) + 1;  // At least 1 hour

  // Apply decay
  apply_stat_decay();

  // Add idle EXP
  if (current_pet.current_state >= PET_STATE_CHILD) {
    current_pet.exp += (IDLE_EXP_PER_HOUR * elapsed_hours);
  }

  // Check evolution
  check_evolution();

  last_update_time = now;
  has_unsaved_changes = true;

  // Auto-save if changes accumulated
  if (now - last_save_time > PET_SAVE_INTERVAL_MS) {
    save_pet();
  }
}

void PetGame::apply_stat_decay() {
  uint32_t now_time = get_current_time();
  uint32_t hours_since_fed = (now_time - current_pet.last_fed) / 3600;

  // Hunger increases over time
  uint8_t hunger_gain = (hours_since_fed * HUNGER_GAIN_PER_HOUR);
  current_pet.hunger = (current_pet.hunger + hunger_gain > STAT_MAX) ? STAT_MAX : current_pet.hunger + hunger_gain;

  // Happiness decreases if idle
  if (hours_since_fed > 2) {
    uint8_t happiness_loss = hours_since_fed * HAPPINESS_DECAY_PER_HOUR;
    current_pet.happiness = (happiness_loss > current_pet.happiness) ? 0 : current_pet.happiness - happiness_loss;
  }
}

void PetGame::check_evolution() {
  uint32_t pet_age_seconds = get_current_time() - current_pet.birth_time;

  PetState old_state = current_pet.current_state;

  if (current_pet.current_state == PET_STATE_EGG) {
    if (pet_age_seconds >= BABY_TO_CHILD_MIN_AGE && current_pet.exp >= BABY_TO_CHILD_MIN_EXP) {
      current_pet.current_state = PET_STATE_BABY;
      Serial.printf("[%s] Pet evolved to BABY!\n", TAG);
    }
  } else if (current_pet.current_state == PET_STATE_BABY) {
    if (pet_age_seconds >= CHILD_TO_ADULT_MIN_AGE && 
        current_pet.happiness >= CHILD_TO_ADULT_MIN_HAPPINESS && 
        current_pet.level >= CHILD_TO_ADULT_MIN_LEVEL) {
      current_pet.current_state = PET_STATE_CHILD;
      Serial.printf("[%s] Pet evolved to CHILD!\n", TAG);
    }
  } else if (current_pet.current_state == PET_STATE_CHILD) {
    if (pet_age_seconds >= ADULT_THRESHOLD) {
      current_pet.current_state = PET_STATE_ADULT;
      Serial.printf("[%s] Pet evolved to ADULT!\n", TAG);
    }
  } else if (current_pet.current_state == PET_STATE_ADULT) {
    if (pet_age_seconds >= ELDERLY_THRESHOLD) {
      current_pet.current_state = PET_STATE_ELDERLY;
      Serial.printf("[%s] Pet evolved to ELDERLY!\n", TAG);
    }
  }

  // Recalculate level from exp
  current_pet.level = calc_level_from_exp(current_pet.exp);
}

uint16_t PetGame::calc_level_from_exp(uint16_t exp) {
  return 1 + (exp / EXP_PER_LEVEL);
}

const char *PetGame::get_age_string() {
  static char age_str[32];
  uint32_t pet_age_seconds = get_current_time() - current_pet.birth_time;
  
  uint32_t days = pet_age_seconds / 86400;
  uint32_t hours = (pet_age_seconds % 86400) / 3600;

  if (days > 0) {
    snprintf(age_str, sizeof(age_str), "%ld day%s", days, days == 1 ? "" : "s");
  } else {
    snprintf(age_str, sizeof(age_str), "%ld hour%s", hours, hours == 1 ? "" : "s");
  }

  return age_str;
}

DifficultyLevel PetGame::get_difficulty() {
  if (current_pet.level <= 2) {
    return DIFFICULTY_EASY;
  } else if (current_pet.level <= 5) {
    return DIFFICULTY_MEDIUM;
  } else {
    return DIFFICULTY_HARD;
  }
}

void PetGame::save_pet() {
  PetStorage::save_pet_state(current_pet);
  has_unsaved_changes = false;
  last_save_time = millis();
  Serial.printf("[%s] Pet saved to NVS\n", TAG);
}

void PetGame::reset_pet() {
  PetStorage::reset();
  memset(&current_pet, 0, sizeof(current_pet));
  Serial.printf("[%s] Pet reset\n", TAG);
}

uint32_t PetGame::get_current_time() {
  time_t now = time(nullptr);
  return (uint32_t)now;
}
