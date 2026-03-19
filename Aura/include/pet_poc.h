#ifndef PET_POC_H
#define PET_POC_H

void pet_poc_init();
void pet_poc_loop();
void pet_poc_deactivate();
const char *pet_poc_get_name();
bool pet_poc_set_name(const char *newName);
bool pet_poc_is_test_mode();
void pet_poc_set_test_mode(bool enabled);
void pet_poc_reset_data();

#endif