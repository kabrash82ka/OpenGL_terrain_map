#ifndef MY_KEYBOARD_H
#define MY_KEYBOARD_H

struct keyboard_info_struct
{
	int state;
	char prev_keys_return[32];
};

#define KEYBOARD_MODE_CAMERA	1
#define KEYBOARD_MODE_PLAYER	2
#define KEYBOARD_MODE_TRUCK		3
#define KEYBOARD_MODE_INVGUI	4
#define KEYBOARD_MODE_MAPGUI	5

#endif
