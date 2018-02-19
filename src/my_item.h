#ifndef MY_ITEM_H
#define MY_ITEM_H

#include "my_collision.h"

/*
This is for 3d model for hand items
*/
struct item_common_struct
{
	GLuint vbo;
	GLuint vao;
	GLuint ebo;
	GLuint texture_id;
	int num_indices;
	int handBoneIndex;
	float debugRot[3]; //degs for rotation about X,Y,Z axis. TODO: Remove this when no longer needed.
	float debugTrans[3]; //translation for x,y,z. TODO: Remove this when no longer needed.
	float y_AboveGroundOffset; //When item on ground, use this offset to translate item above ground.
};

/*this represents an inventory item. This struct will be used
in both the 3d terrain and the inventory screen*/
struct item_struct;
struct item_struct
{
	struct item_struct * pNext;	//for use with linked list by plant_tile.
	unsigned char type;
	float pos[3];	//terrain map position
	float yrot;		//rot for drawing on the terrain map.
};
#define ITEM_TYPE_BEANS			1
#define ITEM_TYPE_CANTEEN		2
#define ITEM_TYPE_PISTOL_AMMO	3
#define ITEM_TYPE_RIFLE_AMMO	4
#define ITEM_TYPE_RIFLE			5
#define ITEM_TYPE_PISTOL		6
#define ITEM_TYPE_SHOVEL		7

/*
This is an inventory slot structure used by item_inventory_struct
*/
struct inv_slot_struct
{
	int slot_index;
	char flags;
};
//SLOT_FLAGS_MULTISLOT tells DrawInvGui() to skip the slot when
//drawing because it is an item that takes up multiple slots, and
//the bottom-most slot will cause the item to be drawn.
#define SLOT_FLAGS_MULTISLOT 1

/*this represents an inventory. The idea here is that items are allocated structs in the heap, and then the inventory
of a character just points to those item structs.*/
struct item_inventory_struct
{
	int num_items;
	int max_items; //determine's length of item_ptrs and slots_index array
	int dimensions[2]; //0=row, 1=col
	struct item_struct ** item_ptrs;	//18 because there are 6x3 possible slots, if you were carrying single items.
	struct inv_slot_struct * slots; //each slot holds an index into the item_ptrs array. This reserves a slot. -1 means the slot is empty. slot 0, the origin, is bottom left. 
};

/*This structure holds info about a single moveable object e.g. crate, barrel*/
struct moveable_object_struct;
struct moveable_object_struct
{
	struct moveable_object_struct * pNext;
	struct item_inventory_struct * items;
	float pos[3];
	float yrot;
	char moveable_type;	//index of type used to render/physics. 0=crate, 1=barrel
};
#define MOVEABLE_TYPE_CRATE 	0
#define MOVEABLE_TYPE_BARREL 	1
#define MOVEABLE_TYPE_DOCK		2
#define MOVEABLE_TYPE_BUNKER	3
#define MOVEABLE_TYPE_WAREHOUSE	4

/*
This structure holds common information used for raycasting a crate
*/
struct crate_common_physics_struct
{
	struct box_collision_struct box_hull;
};

#endif
