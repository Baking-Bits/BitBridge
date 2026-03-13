#ifndef PET_UI_H
#define PET_UI_H

// Initialize pet UI (call once on startup)
void pet_ui_init();

// Update pet display (call periodically)
void pet_ui_update();

// Create screens
void create_pet_main_screen();
void create_game_screen();

#endif // PET_UI_H
