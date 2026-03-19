#include "tic_tac_toe.h"
#include <Arduino.h>
#include <cstring>

TicTacToe::TicTacToe() : game_state(0), difficulty(DIFFICULTY_MEDIUM), move_count(0) {
  memset(board, 0, sizeof(board));
}

void TicTacToe::reset_game() {
  memset(board, 0, sizeof(board));
  game_state = 0;
  move_count = 0;
}

void TicTacToe::set_difficulty(uint8_t pet_level) {
  // Scale difficulty with pet level
  if (pet_level <= 2) {
    difficulty = DIFFICULTY_EASY;
  } else if (pet_level <= 5) {
    difficulty = DIFFICULTY_MEDIUM;
  } else {
    difficulty = DIFFICULTY_HARD;
  }
}

bool TicTacToe::player_move(uint8_t position) {
  if (!is_position_valid(position) || game_state != 0) {
    return false;
  }
  
  board[position] = 1;  // Player is X
  move_count++;
  update_game_state();
  return true;
}

uint8_t TicTacToe::bot_move() {
  if (game_state != 0) {
    return 255;  // Game is over
  }

  uint8_t move = 255;

  if (difficulty == DIFFICULTY_RANDOM) {
    // Random valid move
    int valid_positions[9];
    int valid_count = 0;
    for (int i = 0; i < 9; i++) {
      if (board[i] == 0) {
        valid_positions[valid_count++] = i;
      }
    }
    if (valid_count > 0) {
      move = valid_positions[rand() % valid_count];
    }
  } else {
    // Minimax-based moves
    int max_depth = (difficulty == DIFFICULTY_EASY) ? 1 :
                    (difficulty == DIFFICULTY_MEDIUM) ? 2 : 4;
    move = find_best_move(max_depth);
  }

  if (move != 255) {
    board[move] = 2;  // Bot is O
    move_count++;
    update_game_state();
  }

  return move;
}

uint8_t TicTacToe::get_game_state() {
  return game_state;
}

bool TicTacToe::is_game_over() {
  return game_state != 0;
}

bool TicTacToe::is_position_valid(uint8_t position) {
  return position < 9 && board[position] == 0;
}

void TicTacToe::print_board() {
  Serial.println("\nTic-Tac-Toe Board:");
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      uint8_t cell = board[i * 3 + j];
      char ch = (cell == 0) ? '.' : (cell == 1) ? 'X' : 'O';
      Serial.printf(" %c ", ch);
    }
    Serial.println();
  }
  Serial.println();
}

bool TicTacToe::check_winner(uint8_t player) {
  // Check rows
  for (int i = 0; i < 3; i++) {
    if (board[i * 3] == player && board[i * 3 + 1] == player && board[i * 3 + 2] == player) {
      return true;
    }
  }

  // Check columns
  for (int j = 0; j < 3; j++) {
    if (board[j] == player && board[j + 3] == player && board[j + 6] == player) {
      return true;
    }
  }

  // Check diagonals
  if (board[0] == player && board[4] == player && board[8] == player) return true;
  if (board[2] == player && board[4] == player && board[6] == player) return true;

  return false;
}

bool TicTacToe::is_board_full() {
  for (int i = 0; i < 9; i++) {
    if (board[i] == 0) return false;
  }
  return true;
}

void TicTacToe::update_game_state() {
  if (check_winner(1)) {
    game_state = 1;  // Player wins
  } else if (check_winner(2)) {
    game_state = 3;  // Bot wins
  } else if (is_board_full()) {
    game_state = 2;  // Draw
  }
  // else game_state remains 0 (ongoing)
}

int TicTacToe::evaluate_board_state() {
  if (check_winner(2)) return 10;   // Bot wins
  if (check_winner(1)) return -10;  // Player wins
  return 0;                          // Draw / ongoing
}

int TicTacToe::minimax(int depth, bool is_maximizing, int alpha, int beta) {
  int eval = evaluate_board_state();
  
  if (eval != 0) return eval - (is_maximizing ? depth : -depth);  // Prefer faster wins
  if (is_board_full()) return 0;
  if (depth == 0) return 0;  // Depth limit

  if (is_maximizing) {
    // Bot's turn (maximize)
    int max_eval = -999;
    for (int i = 0; i < 9; i++) {
      if (board[i] == 0) {
        board[i] = 2;
        int score = minimax(depth - 1, false, alpha, beta);
        board[i] = 0;
        max_eval = (score > max_eval) ? score : max_eval;
        alpha = (score > alpha) ? score : alpha;
        if (beta <= alpha) break;  // Alpha-beta cutoff
      }
    }
    return max_eval;
  } else {
    // Player's turn (minimize)
    int min_eval = 999;
    for (int i = 0; i < 9; i++) {
      if (board[i] == 0) {
        board[i] = 1;
        int score = minimax(depth - 1, true, alpha, beta);
        board[i] = 0;
        min_eval = (score < min_eval) ? score : min_eval;
        beta = (score < beta) ? score : beta;
        if (beta <= alpha) break;  // Alpha-beta cutoff
      }
    }
    return min_eval;
  }
}

uint8_t TicTacToe::find_best_move(int max_depth) {
  int best_score = -999;
  uint8_t best_move = 255;

  for (int i = 0; i < 9; i++) {
    if (board[i] == 0) {
      board[i] = 2;
      int score = minimax(max_depth - 1, false, -999, 999);
      board[i] = 0;

      if (score > best_score) {
        best_score = score;
        best_move = i;
      }
    }
  }

  return best_move;
}
