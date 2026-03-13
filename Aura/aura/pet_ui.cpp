#include <lvgl.h>
#include "pet_game.h"

// Forward declarations
extern const lv_font_t *get_font_12();
extern const lv_font_t *get_font_14();
extern const lv_font_t *get_font_16();
extern const lv_font_t *get_font_20();
extern const lv_font_t *get_font_42();

// Global game object
static lv_obj_t *pet_screen = nullptr;
static lv_obj_t *pet_sprite = nullptr;
static lv_obj_t *pet_name_label = nullptr;
static lv_obj_t *pet_age_label = nullptr;
static lv_obj_t *hunger_bar = nullptr;
static lv_obj_t *happiness_bar = nullptr;
static lv_obj_t *energy_bar = nullptr;
static lv_obj_t *level_label = nullptr;
static lv_obj_t *game_screen = nullptr;
static lv_obj_t *board_btns[9] = {nullptr};
static lv_obj_t *game_status_label = nullptr;
static bool game_active = false;

// Simple pet sprite (ASCII art in text, or we can make it a placeholder rect)
static void draw_pet_sprite() {
  if (!pet_sprite) {
    pet_sprite = lv_label_create(pet_screen);
  }
  
  PetState pet = PetGame::get_pet_state();
  const char *pet_display = nullptr;
  
  switch (pet.current_state) {
    case PET_STATE_EGG:
      pet_display = "🥚";
      break;
    case PET_STATE_BABY:
      pet_display = "🐥";
      break;
    case PET_STATE_CHILD:
      pet_display = "🐤";
      break;
    case PET_STATE_ADULT:
      pet_display = "🐔";
      break;
    case PET_STATE_ELDERLY:
      pet_display = "🦜";
      break;
    default:
      pet_display = "🐦";
  }
  
  lv_label_set_text(pet_sprite, pet_display);
  lv_obj_set_style_text_font(pet_sprite, get_font_42(), 0);
  lv_obj_center(pet_sprite);
  lv_obj_align(pet_sprite, LV_ALIGN_CENTER, 0, -30);
}

static void update_pet_display() {
  PetState pet = PetGame::get_pet_state();
  
  // Update name and age
  if (pet_name_label) {
    lv_label_set_text(pet_name_label, pet.name);
  }
  
  if (pet_age_label) {
    lv_label_set_text(pet_age_label, PetGame::get_age_string());
  }
  
  // Update bars
  if (hunger_bar) {
    lv_bar_set_value(hunger_bar, pet.hunger, LV_ANIM_OFF);
  }
  if (happiness_bar) {
    lv_bar_set_value(happiness_bar, pet.happiness, LV_ANIM_OFF);
  }
  if (energy_bar) {
    lv_bar_set_value(energy_bar, pet.energy, LV_ANIM_OFF);
  }
  
  // Update level
  if (level_label) {
    char level_str[16];
    snprintf(level_str, sizeof(level_str), "Lvl %d", pet.level);
    lv_label_set_text(level_label, level_str);
  }
  
  draw_pet_sprite();
}

static void btn_feed_cb(lv_event_t *e) {
  PetGame::feed_pet();
  update_pet_display();
}

static void btn_play_cb(lv_event_t *e) {
  if (!PetGame::can_play_game()) {
    if (game_status_label) {
      lv_label_set_text(game_status_label, "Pet too tired to play!");
    }
    return;
  }
  
  PetGame::start_game();
  create_game_screen();  // Show game screen
}

static void btn_sleep_cb(lv_event_t *e) {
  PetState &pet = PetGame::get_pet_state();
  pet.energy = 100;
  PetGame::save_pet();
  update_pet_display();
}

static void board_btn_cb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  
    // Find which button was pressed
    uint8_t position = 255;
    for (int i = 0; i < 9; i++) {
      if (board_btns[i] == btn) {
        position = i;
        break;
      }
    }
    
    if (position >= 9) return;
    
    TicTacToe &game = PetGame::get_current_game();
    if (!game.player_move(position)) {
      return;  // Invalid move
    }
    
    // Update board display
    const uint8_t *board = game.get_board();
    for (int i = 0; i < 9; i++) {
      char cell_text[2] = {0};
      if (board[i] == 1) cell_text[0] = 'X';
      else if (board[i] == 2) cell_text[0] = 'O';
      else cell_text[0] = ' ';
      
      // Get label inside button and set text
      lv_obj_t *label = lv_obj_get_child(board_btns[i], 0);
      if (label) {
        lv_label_set_text(label, cell_text);
      }
    }
    
    // Check if game is over
    if (game.is_game_over()) {
      uint8_t result = GAME_DRAW;
      if (game.get_game_state() == 1) result = GAME_WIN;
      else if (game.get_game_state() == 3) result = GAME_LOSS;
      
      PetGame::end_game(result);
      lv_label_set_text(game_status_label, 
        result == GAME_WIN ? "You win!" : 
        result == GAME_LOSS ? "You lose!" : "Draw!");
      return;
    }
    
    // Bot's turn
    uint8_t bot_move = game.bot_move();
    if (bot_move < 9) {
      const uint8_t *updated_board = game.get_board();
      for (int i = 0; i < 9; i++) {
        char cell_text[2] = {0};
        if (updated_board[i] == 1) cell_text[0] = 'X';
        else if (updated_board[i] == 2) cell_text[0] = 'O';
        else cell_text[0] = ' ';
        
        lv_obj_t *label = lv_obj_get_child(board_btns[i], 0);
        if (label) {
          lv_label_set_text(label, cell_text);
        }
      }
    }
    
    // Check if game over after bot move
    if (game.is_game_over()) {
      uint8_t result = GAME_DRAW;
      if (game.get_game_state() == 1) result = GAME_WIN;
      else if (game.get_game_state() == 3) result = GAME_LOSS;
      
      PetGame::end_game(result);
      lv_label_set_text(game_status_label, 
        result == GAME_WIN ? "You win!" : 
        result == GAME_LOSS ? "You lose!" : "Draw!");
    }
}

static void btn_back_to_pet_cb(lv_event_t *e) {
  if (game_screen) {
    lv_obj_del(game_screen);
    game_screen = nullptr;
  }
  game_active = false;
  update_pet_display();
}

void create_pet_main_screen() {
  if (pet_screen) {
    lv_obj_del(pet_screen);
  }
  
  pet_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(pet_screen, 240, 320);
  lv_obj_set_pos(pet_screen, 0, 0);
  
  // Pet name and age (top)
  lv_obj_t *header_cont = lv_obj_create(pet_screen);
  lv_obj_set_size(header_cont, 230, 40);
  lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_set_style_bg_opa(header_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(header_cont, LV_OPA_TRANSP, 0);
  
  pet_name_label = lv_label_create(header_cont);
  lv_obj_set_style_text_font(pet_name_label, get_font_16(), 0);
  lv_label_set_text(pet_name_label, "Pet");
  lv_obj_align(pet_name_label, LV_ALIGN_TOP_LEFT, 0, 0);
  
  pet_age_label = lv_label_create(header_cont);
  lv_obj_set_style_text_font(pet_age_label, get_font_12(), 0);
  lv_label_set_text(pet_age_label, "0 hours");
  lv_obj_align(pet_age_label, LV_ALIGN_TOP_RIGHT, 0, 0);
  
  // Pet sprite (center)
  draw_pet_sprite();
  
  // Stats (middle)
  lv_obj_t *stats_cont = lv_obj_create(pet_screen);
  lv_obj_set_size(stats_cont, 230, 80);
  lv_obj_align(stats_cont, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_bg_opa(stats_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(stats_cont, LV_OPA_TRANSP, 0);
  
  // Hunger bar
  lv_obj_t *hunger_label = lv_label_create(stats_cont);
  lv_label_set_text(hunger_label, "Hunger");
  lv_obj_set_style_text_font(hunger_label, get_font_12(), 0);
  lv_obj_align(hunger_label, LV_ALIGN_TOP_LEFT, 0, 0);
  
  hunger_bar = lv_bar_create(stats_cont);
  lv_obj_set_size(hunger_bar, 100, 12);
  lv_obj_align_to(hunger_bar, hunger_label, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  lv_bar_set_range(hunger_bar, 0, 100);
  
  // Happiness bar
  lv_obj_t *happy_label = lv_label_create(stats_cont);
  lv_label_set_text(happy_label, "Happy");
  lv_obj_set_style_text_font(happy_label, get_font_12(), 0);
  lv_obj_align(happy_label, LV_ALIGN_TOP_LEFT, 0, 20);
  
  happiness_bar = lv_bar_create(stats_cont);
  lv_obj_set_size(happiness_bar, 100, 12);
  lv_obj_align_to(happiness_bar, happy_label, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  lv_bar_set_range(happiness_bar, 0, 100);
  
  // Energy bar
  lv_obj_t *energy_label = lv_label_create(stats_cont);
  lv_label_set_text(energy_label, "Energy");
  lv_obj_set_style_text_font(energy_label, get_font_12(), 0);
  lv_obj_align(energy_label, LV_ALIGN_TOP_LEFT, 0, 40);
  
  energy_bar = lv_bar_create(stats_cont);
  lv_obj_set_size(energy_bar, 100, 12);
  lv_obj_align_to(energy_bar, energy_label, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  lv_bar_set_range(energy_bar, 0, 100);
  
  // Level
  level_label = lv_label_create(stats_cont);
  lv_obj_set_style_text_font(level_label, get_font_12(), 0);
  lv_label_set_text(level_label, "Lvl 1");
  lv_obj_align(level_label, LV_ALIGN_TOP_RIGHT, 0, 60);
  
  // Action buttons (bottom)
  lv_obj_t *btn_feed = lv_button_create(pet_screen);
  lv_obj_set_size(btn_feed, 50, 30);
  lv_obj_align(btn_feed, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_obj_t *feed_label = lv_label_create(btn_feed);
  lv_label_set_text(feed_label, "Feed");
  lv_obj_center(feed_label);
  lv_obj_add_event_cb(btn_feed, btn_feed_cb, LV_EVENT_CLICKED, nullptr);
  
  lv_obj_t *btn_play = lv_button_create(pet_screen);
  lv_obj_set_size(btn_play, 50, 30);
  lv_obj_align(btn_play, LV_ALIGN_BOTTOM_CENTER, -30, -5);
  lv_obj_t *play_label = lv_label_create(btn_play);
  lv_label_set_text(play_label, "Play");
  lv_obj_center(play_label);
  lv_obj_add_event_cb(btn_play, btn_play_cb, LV_EVENT_CLICKED, nullptr);
  
  lv_obj_t *btn_sleep = lv_button_create(pet_screen);
  lv_obj_set_size(btn_sleep, 50, 30);
  lv_obj_align(btn_sleep, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
  lv_obj_t *sleep_label = lv_label_create(btn_sleep);
  lv_label_set_text(sleep_label, "Sleep");
  lv_obj_center(sleep_label);
  lv_obj_add_event_cb(btn_sleep, btn_sleep_cb, LV_EVENT_CLICKED, nullptr);
  
  update_pet_display();
}

void create_game_screen() {
  if (game_screen) {
    lv_obj_del(game_screen);
  }
  
  game_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(game_screen, 240, 320);
  lv_obj_set_pos(game_screen, 0, 0);
  
  lv_obj_t *title = lv_label_create(game_screen);
  lv_label_set_text(title, "Tic Tac Toe");
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
  
  // Create 3x3 button grid
  lv_obj_t *board_cont = lv_obj_create(game_screen);
  lv_obj_set_size(board_cont, 180, 180);
  lv_obj_align(board_cont, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_opa(board_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(board_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(board_cont, 2, 0);
  
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int pos = row * 3 + col;
      board_btns[pos] = lv_button_create(board_cont);
      lv_obj_set_size(board_btns[pos], 54, 54);
      lv_obj_set_grid_cell(board_btns[pos], LV_GRID_ALIGN_CENTER, col, 1, LV_GRID_ALIGN_CENTER, row, 1);
      lv_obj_add_event_cb(board_btns[pos], board_btn_cb, LV_EVENT_CLICKED, nullptr);
      
      lv_obj_t *cell_label = lv_label_create(board_btns[pos]);
      lv_label_set_text(cell_label, " ");
      lv_obj_set_style_text_font(cell_label, get_font_20(), 0);
      lv_obj_center(cell_label);
    }
  }
  
  // Static grid to hold board buttons
  lv_obj_set_layout(board_cont, LV_LAYOUT_GRID);
  lv_obj_set_style_grid_column_dsc_array(board_cont, (const int32_t[]){LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), 0}, 0);
  lv_obj_set_style_grid_row_dsc_array(board_cont, (const int32_t[]){LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), 0}, 0);
  
  // Game status
  game_status_label = lv_label_create(game_screen);
  lv_obj_set_style_text_font(game_status_label, get_font_12(), 0);
  lv_label_set_text(game_status_label, "Your turn (X)");
  lv_obj_align(game_status_label, LV_ALIGN_BOTTOM_MID, 0, -40);
  
  // Back button
  lv_obj_t *btn_back = lv_button_create(game_screen);
  lv_obj_set_size(btn_back, 60, 30);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_t *back_label = lv_label_create(btn_back);
  lv_label_set_text(back_label, "Back");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(btn_back, btn_back_to_pet_cb, LV_EVENT_CLICKED, nullptr);
  
  game_active = true;
}

void pet_ui_init() {
  create_pet_main_screen();
}

void pet_ui_update() {
  if (!game_active) {
    update_pet_display();
  }
}
