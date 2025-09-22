//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM keyboard input via linux input events tunnelled over UART
//


#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input-event-codes.h>

#include "config.h"
#include "deh_str.h"
#include "doomtype.h"
#include "doomkeys.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_swap.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_scale.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

int vanilla_keyboard_mapping = 1;

static int initialized = 0;

// Is the shift key currently down?

static int shiftdown = 0;

static int old_mode = -1;
static struct termios old_term;

static void input_shutdown(void)
{
    /* Shut down nicely. */

    printf("Exiting normally.\n");
    if (old_mode != -1) {
        tcsetattr(STDIN_FILENO, 0, &old_term);
    }

    exit(0);
}

static int input_init(void)
{
    struct termios new_term;
    int flags;

    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        printf("Unable to query terminal settings.\n");
        input_shutdown();
    }

    new_term = old_term;
    new_term.c_iflag = 0;
    new_term.c_lflag &= ~(ECHO | ICANON | ISIG);

    /* TCSAFLUSH discards unread input before making the change.
       A good idea. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term) != 0) {
        printf("Unable to change terminal settings.\n");
        input_shutdown();
    }

   /* Put in non-blocking mode */
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("Ready to read input. Press Backspace to exit.\n");

    initialized = 1;

    return 0;
}

#define INPUT_TUNNEL_KEY_DOWN 0xfe
#define INPUT_TUNNEL_KEY_UP 0xff

static int input_read(int *pressed, unsigned char *key)
{
    if (!initialized)
        return 0;

    unsigned char data[2];
    size_t len = read(STDIN_FILENO, &data, 2);

    if (len == 0) {
        return 0;
    }

    if ((len == -1) && (errno == EAGAIN)) {
        return 0;
    }

    if (len == 1) {
        printf("Bad input, only read 1 byte 0x%x\n", data[0]);
        exit(1);
    }
    if (len != 2) {
        printf("Bad input, read %ld bytes\n", len);
        exit(1);
    }

    if ((data[0] != INPUT_TUNNEL_KEY_DOWN) && (data[0] != INPUT_TUNNEL_KEY_UP)) {
        printf("Bad input, byte 0 = 0x%x\n", data[0]);
        exit(1);
    }

    *pressed = (data[0] == INPUT_TUNNEL_KEY_DOWN);
    *key = data[1];

    printf("%s: 0x%2X (%i)\n", *pressed ? "Released" : " Pressed", (unsigned int)*key, (unsigned int)*key);

    return 1;
}

static unsigned char TranslateKey(unsigned char key)
{
    switch (key) {
        case KEY_TAB: return DOOM_KEY_TAB;
        case KEY_ENTER: return DOOM_KEY_ENTER;
        case KEY_ESC: return DOOM_KEY_ESCAPE;
        case KEY_SPACE: return DOOM_KEY_USE;
        case KEY_APOSTROPHE: return '\'';
        case KEY_COMMA: return ',';
        case KEY_MINUS: return DOOM_KEY_MINUS;
        case KEY_DOT: return '.';
        case KEY_SLASH: return '/';
        case KEY_0: return '0';
        case KEY_1: return '1';
        case KEY_2: return '2';
        case KEY_3: return '3';
        case KEY_4: return '4';
        case KEY_5: return '5';
        case KEY_6: return '6';
        case KEY_7: return '7';
        case KEY_8: return '8';
        case KEY_9: return '9';
        case KEY_SEMICOLON: return ';';
        case KEY_EQUAL: return DOOM_KEY_EQUALS;
        case KEY_LEFTBRACE: return '[';
        case KEY_RIGHTBRACE: return ']';
        case KEY_A: return 'a';
        case KEY_B: return 'b';
        case KEY_C: return 'c';
        case KEY_D: return 'd';
        case KEY_E: return 'e';
        case KEY_F: return 'f';
        case KEY_G: return 'g';
        case KEY_H: return 'h';
        case KEY_I: return 'i';
        case KEY_J: return 'j';
        case KEY_K: return 'k';
        case KEY_L: return 'l';
        case KEY_M: return 'm';
        case KEY_N: return 'n';
        case KEY_O: return 'o';
        case KEY_P: return 'p';
        case KEY_Q: return 'q';
        case KEY_R: return 'r';
        case KEY_S: return 's';
        case KEY_T: return 't';
        case KEY_U: return 'u';
        case KEY_V: return 'v';
        case KEY_W: return 'w';
        case KEY_X: return 'x';
        case KEY_Y: return 'y';
        case KEY_Z: return 'z';
        case KEY_BACKSPACE: return DOOM_KEY_BACKSPACE;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: return DOOM_KEY_FIRE;
        case KEY_LEFT: return DOOM_KEY_LEFTARROW;
        case KEY_UP: return DOOM_KEY_UPARROW;
        case KEY_RIGHT: return DOOM_KEY_RIGHTARROW;
        case KEY_DOWN: return DOOM_KEY_DOWNARROW;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT: return DOOM_KEY_RSHIFT;
        case KEY_LEFTALT: return DOOM_KEY_LALT;
        case KEY_RIGHTALT: return DOOM_KEY_RALT;
        case KEY_F1: return DOOM_KEY_F1;
        case KEY_F2: return DOOM_KEY_F2;
        case KEY_F3: return DOOM_KEY_F3;
        case KEY_F4: return DOOM_KEY_F4;
        case KEY_F5: return DOOM_KEY_F5;
        case KEY_F6: return DOOM_KEY_F6;
        case KEY_F7: return DOOM_KEY_F7;
        case KEY_F8: return DOOM_KEY_F8;
        case KEY_F9: return DOOM_KEY_F9;
        case KEY_F10: return DOOM_KEY_F10;
        case KEY_F11: return DOOM_KEY_F11;
        case KEY_F12: return DOOM_KEY_F12;
        case KEY_PAUSE: return DOOM_KEY_PAUSE;
        default: return 0;
    }
}

int get_shifted_key(int ascii_char) {
    // Handle letters
    if (ascii_char >= 'a' && ascii_char <= 'z') {
        return ascii_char - 32; // Convert to uppercase
    }

    // Handle numbers and symbols
    switch (ascii_char) {
        case '`': return '~';
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        default:
            return ascii_char; // No shift equivalent
    }
}

// Get the equivalent ASCII (Unicode?) character for a keypress.

static unsigned char GetTypedChar(unsigned char key)
{
    key = TranslateKey(key);

    // Is shift held down?  If so, perform a translation.

    if (shiftdown > 0) {
        key = get_shifted_key(key);
    }

    return key;
}

static void UpdateShiftStatus(int pressed, unsigned char key)
{
    int change;

    if (pressed) {
        change = 1;
    } else {
        change = -1;
    }

    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
        shiftdown += change;
    }
}

void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;

    while (input_read(&pressed, &key)) {
        if (key == KEY_BACKSPACE) {
            input_shutdown();
            I_Quit();
        }

        UpdateShiftStatus(pressed, key);

        if (pressed) {
            // data1 has the key pressed, data2 has the character
            // (shift-translated, etc)
            event.type = ev_keydown;
            event.data1 = TranslateKey(key);
            event.data2 = GetTypedChar(key);

            if (event.data1 != 0) {
                D_PostEvent(&event);
            }
        } else {
            event.type = ev_keyup;
            event.data1 = TranslateKey(key);

            // data2 is just initialized to zero for ev_keyup.
            // For ev_keydown it's the shifted Unicode character
            // that was typed, but if something wants to detect
            // key releases it should do so based on data1
            // (key ID), not the printable char.

            event.data2 = 0;

            if (event.data1 != 0) {
                D_PostEvent(&event);
            }

            break;
        }
    }
}

void I_InitInput(void)
{
    input_init();
}
