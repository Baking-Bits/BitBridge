#ifndef PET_GAME_H
#define PET_GAME_H

#include <cstdint>
#include "pet_config.h"
#include "pet_storage.h"
#include "tic_tac_toe.h"

class PetGame {
public:
  // Initialize pet game (load or create new pet)
  static bool initialize();

  // Get current pet state
  static PetState &get_pet_state() { return current_pet; }

  // Pet lifecycle actions
  static void feed_pet();
  static void pet_interact();  // Touch pet for happiness
  static bool can_play_game();
  static void start_game();
  static void end_game(uint8_t result);  // result: 0=loss, 1=draw, 2=win

  // Update pet stats (call periodically, e.g., every 1 minute)
  static void update_stats();

  // Get age string (e.g., "3 days", "2 weeks")
  static const char *get_age_string();

  // Check and apply state transitions (baby→child→adult, etc.)
  static void check_state_transition();

  // Get difficulty for current pet
  static DifficultyLevel get_difficulty();

  // Save pet to persistent storage
  static void save_pet();

  // Reset pet (full wipe)
  static void reset_pet();

  // Get tictactoe game instance during gameplay
  static TicTacToe &get_current_game() { return tictactoe_game; }

  // Stats update interval in milliseconds
  static const uint32_t UPDATE_INTERVAL_MS = 60000;  // 1 minute

private:
  static PetState current_pet;
  static TicTacToe tictactoe_game;
  static uint32_t last_update_time;
  static uint32_t last_save_time;
  static bool has_unsaved_changes;

  // Internal helpers
  static void apply_stat_decay();
  static void check_evolution();
  static uint16_t calc_level_from_exp(uint16_t exp);
  static uint32_t get_current_time();
};

#endif // PET_GAME_H
