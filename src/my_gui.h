#ifndef MY_GUI_H
#define MY_GUI_H

#include <my_item.h>

/*
This struct describes a subset of verts in the VBO that are
used to render some GUI object (e.g. a grid or box)
*/
struct gui_grid_vbo_info
{
	int first_vert;
	int vert_count;
};

struct simple_gui_info_struct
{
	GLuint box_vbo;
	GLuint box_vao;
	GLuint texture_id; //have all textures from 1 texture atlas
	int num_itemUVOffsets;
	float * itemUVOffsets;
	float grid_block_width;
	int num_verts;
	struct gui_grid_vbo_info item_box; //50x50 item box
	struct gui_grid_vbo_info long_box; //150x50 long action box
	struct gui_grid_vbo_info crosshair; //crosshair. GL_LINES only.
	//struct gui_grid_vbo_info inventory_grid_icons;
	//struct gui_grid_vbo_info inventory_grid_lines;
	int num_inv_grid_slots;
	float * inv_grid_slot_positions; //array x,y pos coords.
	int num_ground_grid_slots;
	float * ground_grid_slot_positions;
	int num_action_slots;
	float * action_slot_positions;
	int num_crate_grid_slots;
	float * crate_grid_slot_positions;
	int bottom_grid_mode; //0=ground, 1=crate. Determines what grid is drawn on bottom of inventory screen.
	struct moveable_object_struct * selectedCrate;
};

#define GUI_BOTTOMGRID_GROUND 0
#define GUI_BOTTOMGRID_CRATE 1

struct gui_shader_struct
{
	GLuint shaderList[2];
	GLuint program;
	GLuint uniforms[5];	//0=projectionMat, 1=fillColor, 2=modelToCameraMat, 3=texCoordOffset, 4=colorTexture
	int colorTexUnit;
};

struct gui_cursor_struct
{
	float pos[2];
	float x_limits[2]; //0=left limit, 1=right limit
	float y_limits[2]; //0=bottom, 1=top
	char events;
	char flags;
	struct item_struct * draggedItem;
	int original_inv_index;	//index in the slots array.
	struct item_inventory_struct * original_itemList;
};
/*events*/
#define GUI_EVENTS_LEFTMOUSEDOWN	1

/*flags*/
#define GUI_FLAGS_DRAG 1

//This struct is meant to be used temporarily to build
//a list of line segments. positions array is supposed
//to hold a page of memory, and a linked list of these
//pages is meant to be formed holding the line
//segment data.
struct line_load_struct;
struct line_load_struct
{
	struct line_load_struct * pNext;
	float * positions; //always an array of 1024 floats.
	int num_verts;	//number of verts in the local positions array.
};

struct map_gui_info_struct
{
	GLuint vbo;
	GLuint vao;
	GLuint lineVbo;
	GLuint lineVao;
	int num_land_vertices;
	float mapCameraPos[3];	//position of camera in map gui.
	float cameraSpeed;
	float mapScale;
	int * mapVboOffsets;	//offset into map vbo of 1st vert. 1 for each tile. TODO: remove this debug array. 
	int * mapNumVerts;      //# of map verts to render for tile. 1 # for each tile.
	int num_contours;	//# of entries in the lineVbo arrays below.
	int num_line_verts;
	int * lineVboOffsets;	//offset into line vbo for 1st vert for each contour.
	int * lineVboNumVerts;  //num line verts in each contour 
	int lineSymbolOffsets[3]; //offsets into lines VBO for different map symbols
	int lineSymbolNumVerts[3]; //# of verts for each symbol.
};

#endif
