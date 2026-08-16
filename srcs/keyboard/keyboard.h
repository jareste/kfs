#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../utils/utils.h"

#define QWERTY_ENG 0
#define QWERTY_ES 1
#define AZERTY_FR 2
#define QWERTZ_DE 3
#define MAX_KEYBOARD 4

void keyboard_handler();

char get_last_char();
char get_last_char_blocking();
#define getc() get_last_char()
char* get_kb_buffer();
void clear_kb_buffer();

char* get_line();

void kb_raise_eof(void);
bool kb_eof_pending(void);
void kb_clear_eof(void);

char get_ascii_char(uint8_t scancode, bool shifted);
void set_keyboard_layout(uint8_t layout);

int write_stdin_wrapper(int fd, const char *buf, size_t count);

#endif
