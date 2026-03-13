#include "pet_storage.h"
#include <Preferences.h>
#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>

const char *PetStorage::TAG = "PetStorage";

static Preferences pet_prefs;

bool PetStorage::initialize() {
  if (!pet_prefs.begin(PET_NVS_NAMESPACE, false)) {
    Serial.printf("[%s] Failed to open Preferences namespace\n", TAG);
    return false;
  }
  Serial.printf("[%s] Initialized NVS namespace: %s\n", TAG, PET_NVS_NAMESPACE);
  return true;
}

bool PetStorage::load_pet_state(PetState &state) {
  if (!pet_prefs.isKeyExists("pet_id")) {
    Serial.printf("[%s] No pet state saved yet\n", TAG);
    return false;
  }

  // Load each field
  pet_prefs.getString("pet_id", state.pet_id, sizeof(state.pet_id));
  pet_prefs.getString("pet_name", state.name, sizeof(state.name));
  state.birth_time = pet_prefs.getUInt("birth_time", 0);
  state.current_state = pet_prefs.getUChar("state", PET_STATE_EGG);
  state.hunger = pet_prefs.getUChar("hunger", 0);
  state.happiness = pet_prefs.getUChar("happiness", 100);
  state.energy = pet_prefs.getUChar("energy", 100);
  state.level = pet_prefs.getUChar("level", 1);
  state.exp = pet_prefs.getUShort("exp", 0);
  state.last_fed = pet_prefs.getUInt("last_fed", 0);
  state.last_played = pet_prefs.getUInt("last_played", 0);
  state.total_games = pet_prefs.getUShort("total_games", 0);
  state.total_wins = pet_prefs.getUShort("total_wins", 0);
  state.tic_tac_toe_wins = pet_prefs.getUShort("ttt_wins", 0);
  state.version = pet_prefs.getUChar("version", 1);

  Serial.printf("[%s] Loaded pet state: %s (age state: %d, level: %d)\n", 
    TAG, state.name, state.current_state, state.level);
  return true;
}

bool PetStorage::save_pet_state(const PetState &state) {
  pet_prefs.putString("pet_id", state.pet_id);
  pet_prefs.putString("pet_name", state.name);
  pet_prefs.putUInt("birth_time", state.birth_time);
  pet_prefs.putUChar("state", state.current_state);
  pet_prefs.putUChar("hunger", state.hunger);
  pet_prefs.putUChar("happiness", state.happiness);
  pet_prefs.putUChar("energy", state.energy);
  pet_prefs.putUChar("level", state.level);
  pet_prefs.putUShort("exp", state.exp);
  pet_prefs.putUInt("last_fed", state.last_fed);
  pet_prefs.putUInt("last_played", state.last_played);
  pet_prefs.putUShort("total_games", state.total_games);
  pet_prefs.putUShort("total_wins", state.total_wins);
  pet_prefs.putUShort("ttt_wins", state.tic_tac_toe_wins);
  pet_prefs.putUChar("version", state.version);

  return true;
}

int PetStorage::load_game_history(GameHistoryEntry *history, int max_entries) {
  if (!history || max_entries <= 0) return 0;

  int count = pet_prefs.getInt("game_history_count", 0);
  if (count == 0) {
    Serial.printf("[%s] No game history found\n", TAG);
    return 0;
  }

  count = (count > max_entries) ? max_entries : count;
  
  for (int i = 0; i < count; i++) {
    char key[32];
    snprintf(key, sizeof(key), "game_%d", i);
    
    // Load game as serialized string (timestamp:type:pmove:bmove:result:duration)
    String game_data = pet_prefs.getString(key, "");
    if (game_data.length() == 0) continue;

    // Parse: "timestamp:type:pmove:bmove:result:duration"
    int parts[6] = {0};
    char *ptr = (char *)game_data.c_str();
    for (int j = 0; j < 6 && ptr; j++) {
      parts[j] = atoi(ptr);
      ptr = strchr(ptr, ':');
      if (ptr) ptr++;
    }

    history[i].timestamp = parts[0];
    history[i].game_type = parts[1];
    history[i].player_move = parts[2];
    history[i].bot_move = parts[3];
    history[i].result = parts[4];
    history[i].duration_sec = parts[5];
  }

  return count;
}

bool PetStorage::add_game_history(const GameHistoryEntry &entry) {
  int count = pet_prefs.getInt("game_history_count", 0);

  // Shift older entries down (remove oldest if at max)
  if (count >= PET_HISTORY_MAX_ENTRIES) {
    for (int i = 0; i < PET_HISTORY_MAX_ENTRIES - 1; i++) {
      char old_key[32], new_key[32];
      snprintf(old_key, sizeof(old_key), "game_%d", i + 1);
      snprintf(new_key, sizeof(new_key), "game_%d", i);
      
      String game_data = pet_prefs.getString(old_key, "");
      if (game_data.length() > 0) {
        pet_prefs.putString(new_key, game_data);
      }
    }
    count = PET_HISTORY_MAX_ENTRIES - 1;
  }

  // Add new entry at the end
  char key[32];
  snprintf(key, sizeof(key), "game_%d", count);
  
  // Serialize: "timestamp:type:pmove:bmove:result:duration"
  char value[64];
  snprintf(value, sizeof(value), "%u:%u:%u:%u:%u:%u",
    entry.timestamp, entry.game_type, entry.player_move,
    entry.bot_move, entry.result, entry.duration_sec);
  
  pet_prefs.putString(key, value);
  pet_prefs.putInt("game_history_count", count + 1);

  return true;
}

bool PetStorage::reset() {
  pet_prefs.clear();
  Serial.printf("[%s] Pet storage cleared\n", TAG);
  return true;
}

void PetStorage::generate_uuid(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size < 37) return;

  uint32_t rand1 = esp_random();
  uint32_t rand2 = esp_random();
  uint32_t rand3 = esp_random();
  uint32_t rand4 = esp_random();

  snprintf(buffer, buffer_size, "%08x-%04x-%04x-%04x-%08x%04x",
    rand1,
    (rand2 >> 16) & 0xFFFF, rand2 & 0xFFFF,
    (rand3 >> 16) & 0xFFFF, (rand3 & 0xFFFF),
    (rand4 >> 16) & 0xFFFF);
}

size_t PetStorage::get_free_space() {
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) + 
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}
