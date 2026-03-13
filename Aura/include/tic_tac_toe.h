#ifndef TIC_TAC_TOE_H
#define TIC_TAC_TOE_H

#include <cstdint>
#include "pet_config.h"

// Difficulty levels (scales AI depth and strategy)
enum DifficultyLevel {
  DIFFICULTY_RANDOM = 0,        // Random moves only
  DIFFICULTY_EASY = 1,          // Depth-1 minimax (block obvious wins)
  DIFFICULTY_MEDIUM = 2,        // Depth-2 minimax
  DIFFICULTY_HARD = 3           // Full minimax
};

// Board positions (0-8 for 3x3 grid):
// 0 | 1 | 2
// ---------
// 3 | 4 | 5
// ---------
// 6 | 7 | 8

class TicTacToe {
public:
  TicTacToe();

  // Reset board and start new game
  void reset_game();

  // Set difficulty based on pet age/level
  void set_difficulty(uint8_t pet_level);

  // Player makes a move (position 0-8)
  bool player_move(uint8_t position);

  // Bot makes a move (returns position 0-8, or 255 if no valid move)
  uint8_t bot_move();

  // Check game state
  uint8_t get_game_state();  // 0=ongoing, 1=player win, 2=draw, 3=bot win
  bool is_game_over();
  bool is_position_valid(uint8_t position);

  // Get board state (0=empty, 1=player, 2=bot)
  const uint8_t *get_board() const { return board; }

  // Debug: print board to serial
  void print_board();

  // Get move count for duration calculation
  uint32_t get_move_count() const { return move_count; }

private:
  uint8_t board[9];             // 0=empty, 1=player (X), 2=bot (O)
  uint8_t game_state;           // 0=ongoing, 1=player win, 2=draw, 3=bot win
  DifficultyLevel difficulty;
  uint32_t move_count;

  // Minimax algorithm
  int evaluate_board_state();
  int minimax(int depth, bool is_maximizing, int alpha, int beta);
  uint8_t find_best_move(int max_depth);

  // Game logic helpers
  bool check_winner(uint8_t player);  // player: 1=human, 2=bot
  bool is_board_full();
  void update_game_state();
};

#endif // TIC_TAC_TOE_H
