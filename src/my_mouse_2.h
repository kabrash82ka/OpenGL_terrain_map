/*
This file holds function headers for the mouse
handling function.
*/
#ifndef MY_MOUSE_H
#define MY_MOUSE_H

int in_InitMouseInput(void);
int in_CloseMouseInput(void);
int in_MouseRelPos(int * mouse_rel, char * pmouseState);

/*Mouse Flags*/
#define IN_MOUSE_UP_STATE 	0
#define IN_MOUSE_DOWN_STATE	1
#define IN_MOUSE_WAS_DOWN	2

#endif
