#ifndef PET_CONFIG_H
#define PET_CONFIG_H

#include <cstdint>

// Pet state enum
enum PetState : uint8_t {
  PET_STATE_EGG = 0,
  PET_STATE_BABY = 1,
  PET_STATE_CHILD = 2,
  PET_STATE_ADULT = 3,
  PET_STATE_ELDERLY = 4
};

// Game type enum
enum GameType : uint8_t {
  GAME_TIC_TAC_TOE = 0
};

// Game result enum
enum GameResult : uint8_t {
  GAME_LOSS = 0,
  GAME_DRAW = 1,
  GAME_WIN = 2
};

// ===== Pet Mechanics Config =====

// Time-based constants (seconds)
#define BABY_DURATION (7 * 24 * 3600)        // 7 days
#define CHILD_DURATION (7 * 24 * 3600)       // 7 days
#define ADULT_THRESHOLD (14 * 24 * 3600)     // 14 days
#define ELDERLY_THRESHOLD (60 * 24 * 3600)   // 60 days

// Stat decay/gain rates (per hour)
#define HUNGER_GAIN_PER_HOUR 5                // hunger increases by 5/hr
#define HAPPINESS_DECAY_PER_HOUR 2            // happiness decreases by 2/hr if idle
#define ENERGY_GAIN_PER_HOUR 3                // energy increases by 3/hr during sleep
#define ENERGY_DECAY_PER_HOUR 1               // energy decreases by 1/hr while awake

// Action rewards/penalties
#define FEED_HUNGER_REDUCTION 30
#define FEED_EXP_GAIN 10
#define PLAY_HAPPINESS_GAIN 15
#define PLAY_EXP_GAIN 50
#define PLAY_ENERGY_COST 20
#define TOUCH_HAPPINESS_GAIN 5
#define IDLE_EXP_PER_HOUR 5

// Evolution thresholds
#define BABY_TO_CHILD_MIN_AGE (7 * 24 * 3600)
#define BABY_TO_CHILD_MIN_EXP 100
#define CHILD_TO_ADULT_MIN_AGE (14 * 24 * 3600)
#define CHILD_TO_ADULT_MIN_HAPPINESS 60
#define CHILD_TO_ADULT_MIN_LEVEL 3

// Leveling
#define EXP_PER_LEVEL 500

// Game unlock conditions
#define GAME_UNLOCK_AGE (7 * 24 * 3600)      // Unlock games at 7 days (CHILD state)

// NVS & Storage
#define PET_NVS_NAMESPACE "pet_data"
#define PET_HISTORY_MAX_ENTRIES 50
#define PET_SAVE_INTERVAL_MS 30000            // Save every 30 seconds

// Stat bounds
#define STAT_MIN 0
#define STAT_MAX 100

#endif // PET_CONFIG_H
