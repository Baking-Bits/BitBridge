#ifndef PET_STORAGE_H
#define PET_STORAGE_H

#include <cstdint>
#include <string>
#include "pet_config.h"

// Pet state structure
struct PetData {
  char pet_id[37];                  // UUID string (36 chars + null)
  char name[21];                    // Pet name (max 20 chars + null)
  uint32_t birth_time;              // Seconds since epoch
  uint8_t current_state;            // PetState enum (0-4)
  uint8_t hunger;                   // 0-100
  uint8_t happiness;                // 0-100
  uint8_t energy;                   // 0-100
  uint8_t level;                    // 1+
  uint16_t exp;                     // Total experience points
  uint32_t last_fed;                // Seconds since epoch
  uint32_t last_played;             // Seconds since epoch
  uint16_t total_games;             // Total games played
  uint16_t total_wins;              // Total games won
  uint16_t tic_tac_toe_wins;        // Wins at tic-tac-toe
  uint8_t version;                  // Storage format version
};

// Game history entry
struct GameHistoryEntry {
  uint32_t timestamp;               // Seconds since epoch
  uint8_t game_type;                // GameType enum
  uint8_t player_move;              // Board position (0-8 for tic-tac-toe)
  uint8_t bot_move;                 // Board position
  uint8_t result;                   // GameResult enum (0=loss, 1=draw, 2=win)
  uint16_t duration_sec;            // Game duration
};

class PetStorage {
public:
  // Initialize storage (call once on boot)
  static bool initialize();

  // Load pet state from NVS
  static bool load_pet_state(PetData &state);

  // Save pet state to NVS
  static bool save_pet_state(const PetData &state);

  // Load game history (returns number of entries loaded, up to max)
  static int load_game_history(GameHistoryEntry *history, int max_entries);

  // Add game to history (shifts older entries out if at max)
  static bool add_game_history(const GameHistoryEntry &entry);

  // Clear all pet data
  static bool reset();

  // Generate a new pet UUID
  static void generate_uuid(char *buffer, size_t buffer_size);

  // Get NVS free space in bytes
  static size_t get_free_space();

private:
  static const char *TAG;
};

#endif // PET_STORAGE_H
