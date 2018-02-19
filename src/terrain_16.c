/*
The purpose of this program is to create a terrain map.

This verion focuses on the following:
-Soldier animations for crawl, crouch

To compile:
make

Note:
-Made a directory ../OpenGL/terrain_map/resources that contains all the
image, model, and dem files that the executable needs. This way I don't need
to keep copying these files over and over.
-There's a symbolic link to wake_island_1_3sec.asc in .../OpenGL/terrain_map/
since that file is 80MB
-This program needs permissions to open the /dev/input/event# file to get
mouse input.

Issues:
See log.txt
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <errno.h>

#define PI 3.14159265359

/*
gravity constant calculated by: g / fps
-g is estimated at 10 m/s^2
-fps is assumed at 60 Hz
10/60
*/
#define G_K_GRAVITY 0.1666666f

/*my_mat_math: contains functions for matrices & vectors*/
#include "my_mat_math_6.h"

/*my_tga.h: contains functions for loading tga files*/
#include "my_tga_2.h"

/*load_bush.h: contains functions for load .obj files into plant models (e.g. bushes)*/
#include "load_bush_3.h"

#include "my_character2.h"
#include "my_vehicle.h"
#include "my_item.h"
#include "my_collision.h"
#include "my_milbase.h"

/*load_character.h: contains functions to load a character .obj file (character .obj files contain multiple meshes)*/
#include "load_character.h"
#include "load_collada_4.h"

#include "my_keyboard.h"
#include "my_camera.h"
#include "my_mouse_2.h"
#include "my_gui.h"


/*OpenGL Definitions*/
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

/*
plant_billboard structure used by Draw call. (formerly the bush_billboard)
this is used to draw both a single bush and a tile of bushes
(note: there's another structure that holds the file data)
*/
struct plant_billboard
{
	GLuint vbo;
	GLuint vao;
	GLuint ebo;
	GLint * p_elements;
	float * p_vertex_data;
	int num_verts;
	int num_indices;
	GLuint texture_id;
};

/*
simple_billboard holds drawing information for billboards
that always face the camera and is used with the simple billboard
shader.
*/
struct simple_billboard
{
	GLuint  vbo;
	GLuint  vao;
	float * p_vertex_data;
	float   size[12]; //height and halfwidth of billboard, vec2 array of 6 elements (width,height) 
	int     num_verts;
	GLuint  texture_ids[6]; //this is an array of texture ids, one for each plant
};

/*
This struct holds OpenGL info about a simple object for DrawScene
*/
struct simple_model_struct
{
	GLuint vbo;
	GLuint vao;
	GLuint ebo;
	GLuint texture_id;
	int num_indices;
	int num_verts;
	float offset_groundToCg;	//vector from cg to ground
};

/*
This structure holds shader information for bushes
*/
struct bush_shader_struct
{
	GLuint shaderList[2];
	GLuint program;
	GLuint perspectiveMatrixUnif;
	GLuint modelToCameraMatrixUnif;
	GLuint lightDirUnif;
	GLuint colorTextureUnif;
	int    colorTexUnit;
};

/*
This structure holds information for the shader for simple billboards
that always face the camera.
*/
struct simple_billboard_shader_struct
{
	GLuint shaderList[2];
	GLuint program;
	GLuint perspectiveMatrixUnif;
	GLuint modelToCameraMatrixUnif;
	GLuint billboardSizeUnif;
	GLuint colorTextureUnif;
	int    colorTexUnit;
};

struct simple_wave_shader_struct
{
	GLuint shaderList[2];
	GLuint program;
	GLuint perspectiveMatrixUnif;	//vertex shader uniform
	GLuint modelToCameraMatrixUnif;	//vertex shader uniform
	GLuint lightDirUnif;		//fragment shader uniform
	GLuint shininessFactorUnif;	//fragment shader uniform
	GLuint modelSpaceCameraPosUnif;	//fragment shader uniform
};

struct simple_wave_vbo_struct
{
	GLuint vbo;
	GLuint vao;
	float * p_vertex_data;
	int num_verts;
};

/*
This structure holds information about a single
tile in the moveable items grid. This is for crates/barrels
*/
struct moveable_items_tile_struct
{
	struct moveable_object_struct * moveables_list; //linkedlist of crate/barrel info
	int num_moveables;
	float urcorner[2];
};

/*
This structure holds the moveable items grid. It
consists of a grid of tiles.
*/
struct moveables_grid_struct
{
	int num_tiles;
	int num_cols;	//1207
	int num_rows;	//1207
	float tileSize;	//width of tile (tiles are square)
	struct moveable_items_tile_struct * p_tiles;
	int local_grid[2116];	//46x46 grid of working moveables, -1 indicates no entry
	int localGridSize[2]; //0=row, 1=col, 46x46
	int numLocalTiles; //total # of local grid tiles, 2116
};

/*
bush_group structure holds an array of bushes
and counting variables for debug.
*/
struct bush_group
{
	float * pPos;
	int max_pos; //number of positions in pPos (note: this is not num floats) 
	int i_pos;   //keeps track of the last valid position (starts at 0)
};

/*
This structure has info about a single plant in a tile
*/
struct plant_info_struct
{
	float pos[3];
	float yrot;			//y rotation in degrees
	char plant_type;	//index of the plant type used to render
};

/*
This structure holds positions of bushes in a terrain map
and also moveable objects
*/
struct plant_tile
{
	//float * m16_model_mat; //array of 4x4 matrices for each plant (used for the more detailed model)
	//float * plant_pos_yrot_list; //array of vec4 (xyz pos, y rot angle in degs)
	struct plant_info_struct * plants; //array of individual plant info
	struct item_struct * items_list; //linked list of any items on the ground.
	int num_plants;
	int num_items;
	float urcorner[2];	//origin corner of tile
};

/*
This structure holds plant_tiles and moveable objects
*/
struct plant_grid
{
	int num_tiles;
	int num_cols;
	int num_rows;
	struct plant_tile * p_tiles;
	int draw_grid[9]; //this holds tiles around the camera in a 3x3 fashion (This is row major form)
	float detail_boundaries[4]; //-x,+x,-z,+z boundaries for drawing detailed plants. 
	float nodraw_dist[6]; //dist
	float nodraw_boundaries[24]; //-x,+x,-z,+z boundaries for not-drawing plants. 6 plants * 4 floats
};

/*
This is the smallest renderable piece of the terrain
*/
struct lvl_1_tile
{
	GLuint vbo;
	GLuint vao;
	float * pPos;
	float urcorner[2]; //origin corner position of the tile
	int num_verts;	//10,000 verts
	int num_z; //number of verts in the z direction, 100 verts
	int num_x; //number of verts in the x direction, 100 verts
};

/*
This structure holds lvl_1_tile's
*/
struct lvl_1_terrain_struct
{
	int num_tiles;		//1521
	int num_cols;		//39
	int num_rows;		//39
	int tile_num_quads[2]; //length of tile in quads, 0=x axis(col), 1=z axis(rows). 99,99. (need this because there is +1 more vertex than quads and num_z, num_x hold # of vertices but sometimes I need # of quads)
	float tile_len[2]; //length of a tile (0 = len in x, 1 = len in z). (990.0, 990.0)
	struct lvl_1_tile * pTiles; //tiles in row-major
	int num_floats_per_vert;
	GLshort * pElements; //element array
	GLuint ebo; //element buffer object
	int num_indices;
	int colorTexUnit;
	GLuint colorTextureUnif;
	GLuint textureId;
	GLuint sampler;	
	float nodraw_boundaries[4]; //boundaries within a tile is draw, -x,+x,-z,+z boundaries
	float nodrawDist;
};

struct DEM_info_struct
{
	FILE * pFile;
	int num_col;
	int num_row;
	int num_total;
	float x_llcenter;
	float y_llcenter;
	float cellsize;
	float nodata_value;
	long data_start_pos; //file position indicator of where the data starts
};

struct camera_frustum_struct
{
	float left_normal[3];
	float right_normal[3];
	float camera[3];	//position of camera
	float fzNear;
	float fzFar;
};

/*This structure holds information about the dots.tga
which holds the location of where vegetation is planted on the map. it holds several
dots.tga
*/
struct dots_struct
{
	unsigned char * data[2]; //pixel data from the tga file.
	int num_black_dots[2];	//number of black dots in the particular tga file.
	int num_bytes[2];	//tga file's number of bytes
	int components[2];	//# of components in the tga file's format (normally 3 or 4 depending on if there is an Alpha channel)
	int num_dot_tgas;
	float size[2]; //world-space width & height
	int num_x[2]; //length of dots.tga in x direction
	int num_z[2]; //length of dots.tga in z direction
};

/*This structure holds various statistics for debugging*/
struct debug_stats_struct
{
	//draw call frequency statistics
	struct timespec min_drawcall_period;
	unsigned int step_at_min_drawcall;
	struct timespec max_drawcall_period;
	unsigned int step_at_max_drawcall;

	//draw call duration statistics
	struct timespec min_drawcall_duration;
	unsigned int step_atMinDrawDuration;
	struct timespec max_drawcall_duration;
	unsigned int step_atMaxDrawDuration;

	int reset_flag; //if set then code will just fill in whatever the next time difference is
};

/*Global variables*/
GLenum e;
void (*g_DrawFunc)(void);
struct lvl_1_terrain_struct g_big_terrain;
struct camera_frustum_struct g_camera_frustum;
struct plant_billboard g_bush_billboard;
struct simple_billboard g_bush_smallbillboard;
struct simple_billboard g_bush_big_tile;
struct plant_billboard g_bush_test_tile;
//struct plant_billboard g_pine_trunk;	//old pine model
//struct plant_billboard g_pine_billboard; //old pine model
//struct plant_billboard g_pine_trunk_test_tile; //old pine model
//struct plant_billboard g_pine_needles_test_tile; //old pine model
//struct plant_billboard g_palm_bottom; //old palm
//struct plant_billboard g_palm_top; //old palm
struct plant_billboard g_palm_fronds;
struct plant_billboard g_palm_trunk;
struct plant_billboard g_pemphis_shrub;
struct plant_billboard g_scaevola_shrub;
struct plant_billboard g_tournefortia_shrub;
struct plant_billboard g_tournefortia_trunk;
struct plant_billboard g_ironwood_branches;
struct plant_billboard g_ironwood_trunk;
struct bush_group g_bush_group;
struct plant_grid g_bush_grid;
struct moveables_grid_struct g_moveables_grid;
struct bush_shader_struct g_bush_shader;
struct simple_billboard_shader_struct g_billboard_shader;
struct simple_wave_shader_struct g_simple_wave_shader;
struct simple_wave_vbo_struct g_simple_wave;
struct dots_struct g_dots_tgas;
struct debug_stats_struct g_debug_stats;
struct character_shader_struct g_character_shader;
struct character_model_struct g_triangle_man;
struct character_list_struct g_soldier_list;
struct character_struct g_a_man;
struct soldier_ai_controller_struct g_ai_soldier;
struct vehicle_common_struct g_vehicle_common;
struct vehicle_common_struct g_wheel_common;	//TODO: This is an oddball. Its really like some kind of common graphics object.
struct vehicle_physics_struct g_a_vehicle;	//TODO: Remove this global. The struct is OBSOLETE.
//struct wheel_physics_struct g_a_wheel;
struct vehicle_physics_struct2 g_b_vehicle;
struct keyboard_info_struct g_keyboard_state;
struct camera_info_struct g_camera_state;
struct item_common_struct g_rifle_common;
struct item_common_struct g_pistol_common;
struct item_common_struct g_canteen_common;
struct item_common_struct g_beans_common;
struct simple_model_struct g_crate_model_common;
struct simple_model_struct g_barrel_model_common;
struct simple_model_struct g_dock_common;
struct simple_model_struct g_bunker_model_common;
struct simple_model_struct g_warehouse_model_common;
struct crate_common_physics_struct g_crate_physics_common;
struct milbase_info_struct g_a_milBase;
struct simple_gui_info_struct g_gui_info;
struct map_gui_info_struct g_gui_map;
struct simple_model_struct g_textModel;
struct gui_shader_struct g_gui_shaders;	//this is solid color shader
struct gui_shader_struct g_guitex_shaders; //this is a texture shader
struct gui_shader_struct g_textShader; //this is the shader for text
struct item_inventory_struct g_temp_ground_slots;
struct gui_cursor_struct g_gui_inv_cursor;
float g_camera_pos[3]; //this is really the negative camera position
float g_ws_camera_pos[3]; //camera position not inverted.
float g_camera_rotY;   //this is really the negative camera rotY
float g_camera_rotX;   //this is really the negative camera rotX
float g_camera_speed;
float g_perspectiveMatrix[16];
float g_orthoMat4[16]; //orthographic projection matrix for inventory screen
float g_mapOrthoMat4[16]; //orthographic projection mat4 for map screen
float g_near_distance; //near plane distance of perspectivematrix
float g_anim_interp;
GLuint g_shaderlist[2];
GLuint g_theProgram;
GLuint g_test_vbo;
GLuint g_test_vao;
GLuint g_test_ibo;
GLuint g_perspectiveMatrixUnif; 	//vertex shader
GLuint g_modelToCameraMatrixUnif; 	//vertex shader
GLuint g_modelSpaceCameraPosUnif;	//vertex shader
GLuint g_lightDirUnif;			//vertex shader
GLuint g_bush_branchtex_sampler;	//sampler for bush branch billboard textures
GLuint g_bush_trunktex_sampler;		//sampler for bush trunk billboard textures
unsigned int g_screen_width;
unsigned int g_screen_height;
unsigned int g_simulation_step;
unsigned int g_debug_num_simple_billboard_draws; //count of draw calls for wholely simple tiles
unsigned int g_debug_num_detail_billboard_draws; //count of billboard drawcalls in detail tiles
unsigned int g_debug_num_himodel_plant_draws; //count of draw calls for detailed plant models
int g_render_mode; //0=draw scene, 1=draw inventory
int g_debug_freeze_culling;
int g_debug_keyframe;
int g_pause_simulation_step;

/*Global functions*/
int InitGL(unsigned int width, unsigned int height);
char * LoadShaderSource(char * filename);
void CalculatePerspectiveMatrix(unsigned int width, unsigned int height);
static void SetOrthoMat(float * ortho_mat, unsigned int width, unsigned int height);
void SetMapOrthoMat(float * orthoMat, float fsize);
int InitCamera(struct camera_info_struct * p_camera);
void DrawScene(void);
int DEMLoadInfo(struct DEM_info_struct * pDemInfo);
int DEMGetMinMaxElevation(struct DEM_info_struct * pDemInfo, float * pMin, float * pMax);
int InitTerrain(void);
int MakeTerrainElementArray(GLshort ** ppElements, int * num_indices, int num_x, int num_z);
void MakeTerrainCalcNormal(float * normal, float * origin_pos, float * u, float * v);
int GetLvl1Tile(float * pos);
int GetLvl1Tileij(float * pos, int * i, int * j);
int GetTileRowColFromIndex(struct lvl_1_tile * ptile, int * pi, int * pj);
int GetTileSurfPoint(float * pos, float * surf_pos, float * surf_norm);
int RaycastTileSurf(float * pos, float * ray, float * surf_pos, float * surf_norm);
static int RaycastQuadTileSurf(float * pos, float * ray, struct lvl_1_tile * ptile, int quad_row, int quad_col, float * surf_pos, float * surf_norm);
static struct lvl_1_tile* GetNewTileQuad(float * pos, float * ray, struct lvl_1_tile * ptile, int * pi, int * pj);
void ClipTilesSetupFrustum(void);
int IsTileInCameraFrustum(struct lvl_1_terrain_struct * pTerrain, struct lvl_1_tile * pTile);
void UpdateTerrainDrawBox(float * camera_pos);
int IsTileInTerrainDrawBox(struct lvl_1_tile * tile);
int IsTileInPlantViewBox(struct lvl_1_tile * p_tile);
void GetElapsedTime(struct timespec * start, struct timespec * end, struct timespec * result);
void SimulationStep(void);
void DebugUpdateDrawCallStats(struct timespec * diff, struct timespec * drawStart, struct timespec * drawEnd);
int InitCharacterShaders(struct character_shader_struct * p_shader);
int InitCharacterCommon2(struct character_model_struct * character);
int InitCharacterList(struct character_list_struct * plist);
int InitCharacterPerson(struct character_struct * p_character, float * newPos);
void UpdateCharacterSimulation(struct character_struct * p_character);
void UpdateLocalPlayerDirection(void);
void UpdateCameraForCharacter(float * icamera_pos, float * ws_camera_pos, struct character_struct * p_character);
void UpdateCameraForThirdPerson(struct character_struct * p_character, float * icamera_pos, float * ws_camera_pos, float * cameraRotY, float * cameraRotX);
void UpdateCharacterCameraForVehicle(struct character_struct * p_character, float * matCamera4);
void UpdateCharacterAnimation(struct character_struct * p_character);
void UpdateCharacterBoneModel(struct character_struct * guy);
int CharacterStartAnim(struct character_animation_struct * characterAnim, int animToStart, int timeToNextKeyframe);
int CharacterDetermineStandAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
int CharacterDetermineUprightMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
int CharacterDetermineCrouchAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
int CharacterDetermineCrouchMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
int CharacterDetermineCrawlAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
int CharacterDetermineCrawlMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags);
float CharacterDetermineSpeed(struct character_struct * p_character);
void CharacterGetEyeOffset(struct character_struct * p_character, float * offset);
int EnterVehicle(struct vehicle_physics_struct2 * vehicle, struct character_struct * character);
int LeaveVehicle(struct character_struct * character);
static void CharacterBoneAnimCalcInterp(struct character_struct * this_character, int * i_prev_keyframe, int * i_next_keyframe, float * finterp);
void CalculateSingleBoneTransform(struct character_struct * p_character, int boneIndex, float * outMat4);
void CharacterCalculateHandTransform(struct character_struct * this_character, struct item_common_struct * p_item, float * outMat4);

/*Inventory GUI functions*/
int InitInvGUIVBO(struct simple_gui_info_struct * guiInfo);
float * GUIGetItemTexCoords(char itemType);
int GUIIsItemMultiSlot(char itemType);
void GUIGetItemCursorOffset(float * offset, char itemType);
int FindSlotCursorIsOver(int * pgridId, int * pslotIndex, struct item_struct ** pitem);
int GUIIsInGridBlock(float * pos, float * boxLowerLeftCorner, char itemType);
int GUIPickupItemInInventory(int slotIndex, struct item_struct * dragItem, struct item_inventory_struct * itemList);
int GUIPickupItemFromGround(int slotIndex, struct item_struct * dragItem, struct item_inventory_struct * itemList);
int InventoryGetFreeItemPtr(struct item_inventory_struct * itemInventory);
int GUIIsPosInInvGridArea(float * pos, int gridIndex, int * ij_grid, int * i_slot);
int GUISetDraggedItemDown(void);
int InitGUICursor(struct gui_cursor_struct * cursor, unsigned int width, unsigned int height);
int InitGUIShaders(struct gui_shader_struct * guiShader, char * vert_shader_filename, char * frag_shader_filename, int init_shader_index);
void DrawInvGUI(void);
void UpdateGUI(void);
void SwitchRenderMode(int mode);

/*Keyboard functions*/
int CheckKey(char * keys_return, int key_bit_index);
static int HandleKeyboardInput(Display * dpy, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct character_struct * p_character, struct vehicle_physics_struct * p_vehicle);
static int UpdateCameraFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos);
static int UpdatePlayerFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct character_struct * p_character);
static int UpdateVehicleFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct vehicle_physics_struct * p_vehicle);
static int UpdateGUIFromKeyboard(char * keys_return);
static int UpdateMapFromKeyboard(char * keys_return);
static void DebugAdjustItemMatFromKeyboard(char * keys_return);
int UpdateFromMouseInput(float * camera_rotX, float * camera_rotY);
void GetXZComponents(float speed, float invRotY, float * z, float * x, int invert_rot);
void MakeRayFromCamera(float * irotX, float * irotY, float * ray);

/*Global vehicle functions*/
int InitVehicleCommon(struct vehicle_common_struct * p_common);
int InitWheelCommon(struct vehicle_common_struct * p_common);
void VehicleConvertDisplacementMat3To4(float * mat3, float * mat4);
void FillInMat4withMat3(float * mat3, float * mat4);
/*new vehicle functions*/
int InitSomeVehicle2(struct vehicle_physics_struct2 * p_vehicle);
void UpdateVehicleSimulation2(struct vehicle_physics_struct2 * p_vehicle);
void CalculateWheelSpringForce2(struct wheel_struct2 * pwheel, struct vehicle_physics_struct2 * pvehicle, float * forceOut, float * torqueOut);
static void UpdateVehicleFromKeyboard2(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct vehicle_physics_struct2 * p_vehicle);
void CalculateSteeringForce(struct vehicle_physics_struct2 * pvehicle, float * torqueOut, float * forceOut);
void UpdateCameraPosAtVehicle2(float follow_dist, struct vehicle_physics_struct2 * p_vehicle, float * camera_pos, float * icamera_pos, float * camera_rotY);
void ClearLowMotion(float * vec3);
void CalcVehicleEngineAndResistanceForce(struct vehicle_physics_struct2 * pvehicle, float * forceOut);
void CalcWheelSpin(struct vehicle_physics_struct2 * pvehicle);

/*Global vegetation functions*/
int InitBushGroup(struct bush_group * p_group);
int LoadBushVBO(struct plant_billboard * p_billboard, char * mesh_filename, char * mesh_name, char * tex_filename, float fscale_factor, char flags);
int LoadSimpleBillboardVBO(struct simple_billboard * p_billboard);
int LoadSimpleBillboardTextures(struct simple_billboard * billboard);
int MakeDetailedBushTile(struct plant_billboard * p_tile, struct plant_billboard * single_bush, float * v3_origin, int dot_tga_file_index);
int MakeSimpleBushTile(struct simple_billboard * p_tile, struct simple_billboard * single_bush, float * v3_origin, int dot_tga_file_index);
int InitBushShaders(struct bush_shader_struct * p_shader);
int InitBillboardShaders(struct simple_billboard_shader_struct * p_shader);
int InitDotsTga(struct dots_struct * p_dots_info);
//int InitPlantGrid(struct plant_grid * p_grid, struct dots_struct * p_dots_info, int dots_index, float plant_cluster_scale);
int InitPlantGrid2(struct plant_grid * p_grid);
int WritePlantGridToFile(struct plant_grid * p_grid, char * filename);
void UpdatePlantDrawGrid(struct plant_grid * p_grid, int cam_tile, float * camera_pos);
int GenRandomPlantType(float * pos, char * plant_type);

/*Base functions*/
int FlattenTerrain(float * centerPos, float x_width, float z_width, float y_rot_deg, float * y_height);
int ClearPlantsAroundTerrainVert(int i_tile, int j_tile, int i_quad, int j_quad, int * numPlantsRemoved);
int ClearPlantsInBoundary(struct plant_tile * plantTile, float * boundaryCoords, int * numPlantsRemoved);
int CreateBase(float * pos);
static int PlaceCrateGroup(float * clusterCenter, float clusterOffset, int moveable_type);
int CreateDock(float * pos);
int CreateBunker(float * pos);
int CreateWarehouse(float * pos);

/*Player Item functions*/
int InitItemCommon(struct item_common_struct * p_common, char * obj_filename, char * objname, char * texture_filename);
struct item_struct * CreateItem(float * initialPos, unsigned char newType);
float GetItemGroundOffset(unsigned char itemType);
int InitRifleCommon(struct item_common_struct * p_item);
int InitMoveablesGrid(struct moveables_grid_struct * p_grid);
void UpdateMoveablesLocalGrid(struct moveables_grid_struct * p_grid, float * pos);
int GetMoveablesTileIndex(float * pos);
struct moveable_object_struct * AddMoveableToTile(float * moveablePos);
int InitCrateCommon(struct crate_common_physics_struct * p_common);
int InitCrate(struct moveable_object_struct * crate);
int TestFillCrate(struct moveable_object_struct * crate);
int FillCrateWithItem(struct item_inventory_struct * crateInv, int num_items, char item_type, struct item_struct * pitemToAdd, char flags);
struct moveable_object_struct * SelectCrateToOpen(float * pos, float * ray);
int RaycastCrateHull(float * cratePos, float * origin, float * ray);
int AddItemToTile(struct item_struct * newItem);
int InitMoveableModelCommon(struct simple_model_struct * simpleModel, char * obj_name, char * obj_filename, char * texture_filename);
int InitPlayerInventory(struct character_struct * pcharacter);
int InitGroundSlots(struct item_inventory_struct * ground);
void ClearGroundSlots(struct item_inventory_struct * ground);
int MakeGroundItemsInventory(float * pos);
int PlayerGetItem(void);

/*Map functions*/
int InitMapGUIVBO(struct map_gui_info_struct * mapInfo);
void MapCountDetailedVerticesInTile(struct lvl_1_tile * ptile, int * num_land_verts);
int MapCalcMidpointAboveWater(float * startPos, float * endPos, float * outPos, float felevation);
int MapCreateDetailedVerticesInTile(struct lvl_1_tile * ptile, float * map_pos, int * i_map);
int InitMapElevationLinesVBO(struct map_gui_info_struct * mapInfo);
int CreateElevationLinesInTile(struct lvl_1_tile * ptile, struct line_load_struct ** inLineData, float felevation, int * num_lineverts_added);
int MapLineAddVert(struct line_load_struct ** inLineData, float * newPos);
int InitMapSymbols(float * positions, int * symbolOffsets, int * numVertsWrittenArray, int * totalNumVerts, int flags);
void DrawMapGUI(void);

/*AI functions*/
int InitSoldierAI(struct soldier_ai_controller_struct * aiInfo, struct character_struct * soldierToControl);
void UpdateSoldierAI(struct soldier_ai_controller_struct * aiInfo);
int StartSoldierAIPath(struct soldier_ai_controller_struct * aiInfo, float * targetPos);
void HandleSoldierAIMoveToState(struct soldier_ai_controller_struct * aiInfo);

/*Text Drawing functions*/
static void TextGetUVOffset(char inChar, float * uvCoord);
static int InitTextVBO(struct simple_model_struct * texInfo);
static void DrawText(char * printString, float * startPos2, float * color3);

/*Global wave functions*/
int InitSimpleWavePlaneShader(struct simple_wave_shader_struct * p_shader);
int MakeSimpleWaveVBO(struct simple_wave_vbo_struct * p_wave);

/*Global texture support functions*/
void LoadTexture(void);
int LoadBushTextureManualMip(GLuint * p_tex_id, char * tga_filename);
int LoadBushTextureGenMip(GLuint * p_tex_id, char * tga_filename);
void SetupBushSamplers(void);

/*Global random number functions*/
float RandomFloat(void);
void CalcGaussRandomPair(float *a, float *b);

/*Debug helper function*/
void PrintVector(char * name, float * vec3);

int main(int argc, char ** argv)
{
	static int visual_attribs[] =
	{
		GLX_X_RENDERABLE, 1,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
		GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8,
		GLX_ALPHA_SIZE, 8,
		GLX_DEPTH_SIZE, 24,
		GLX_DOUBLEBUFFER, 1,
		None
	};
	Display * display;
	int r;
	int glx_major;
	int glx_minor;
	int fbcount;
	int running=1;
	unsigned int width = 1024;
	unsigned int height = 768;
	GLXFBConfig * fbc;
	GLXFBConfig chosen_fbc;
	XVisualInfo *vi;
	Colormap cmap;
	XSetWindowAttributes swa;
	Window win;
	Atom wmDelete;
	XEvent event;
	glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
	GLXContext ctx = 0;
	int context_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, None};
	struct timespec last_drawcall;
	struct timespec last_simulatecall;
	struct timespec curr_time;
	struct timespec tdrawSceneStart;
	struct timespec tdrawSceneEnd;
	const struct timespec diff_simulate = {0, 16000000}; //~60Hz, 16,000,000
	const struct timespec diff_drawcall = {0, 15000000};
	struct timespec diff;

	//initialize global variables
	g_DrawFunc = DrawScene;
	g_debug_freeze_culling = 0;
	g_render_mode = 0; //draw scene
	
	//g_camera_pos[0] = -7.0f;	
	//g_camera_pos[1] = 3028.0f;
	//g_camera_pos[1] = 0.0f; //this won't show the ocean layer
	
	//g_camera_pos[0] = -7.0f;
	//g_camera_pos[1] = -10.0f;
	//g_camera_pos[2] = -11.0f;
	
	//g_camera_pos[0] = 0.0f;
	//g_camera_pos[1] = 0.0f;
	//g_camera_pos[2] = 0.0f; //water plane won't show, need to go up in altitude
	g_camera_pos[0] = -12319.272461f; //put the camera over land
	g_camera_pos[1] = -25.0f;
	g_camera_pos[2] = -11268.877930f;
	
	g_camera_rotY = -190.0f; //look down +z so we can see something
	g_camera_rotX = 0.0f;
	g_ws_camera_pos[0] = -1.0f*g_camera_pos[0];
	g_ws_camera_pos[1] = -1.0f*g_camera_pos[1];
	g_ws_camera_pos[2] = -1.0f*g_camera_pos[2];
	g_camera_speed = 10.0f;
	//g_near_distance = 1.0f;
	g_near_distance = 0.136548979f; //Set this to make the near plane closer so that you can get closer to objects.
	g_screen_width = width;
	g_screen_height = height;
	g_simulation_step = 0;
	g_debug_stats.reset_flag = 1;
	g_debug_keyframe = 0;
	g_anim_interp = 0.0f;
	g_keyboard_state.state = KEYBOARD_MODE_CAMERA;
	g_pause_simulation_step = 1;	//start the simulation paused
	
	srand(0x53F8E6A2);
		
	//setup the mouse handling
	r = in_InitMouseInput();
	if(r == 0)
	{
		printf("main: in_InitMouseInput() failed.\n");
		return 0;
	}

	display = XOpenDisplay(0);
	if(display == 0)
	{
		printf("main: error. XOpenDisplay() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	
	//get the GLX version
	r = glXQueryVersion(display, &glx_major, &glx_minor);
	if(!r)
	{
		printf("main: glXQueryVersion() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	printf("glXVersion: %d %d\n", glx_major, glx_minor);
	if((glx_major == 1) && (glx_minor < 3))
	{
		printf("main: error. need glx 1.3\n");
		in_CloseMouseInput();
		return 0;
	}
	fbc = glXChooseFBConfig(display, DefaultScreen(display), visual_attribs, &fbcount);
	if(!fbc)
	{
		printf("main: glXChooseFBConfig() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	/* Just give me something...
	for(i=0; i < fbcount; i++)
	{
		vi = glXGetVisualFromFBConfig(display, fbc[i]);
		if(vi)
		{
		}
		XFree(vi);	
	}
	*/
	//screw it -- just pick the first one
	chosen_fbc = fbc[0];
	
	//free the FBConfig list
	XFree(fbc);
	
	vi = glXGetVisualFromFBConfig(display, chosen_fbc);
	
	//Create colormap
	cmap = XCreateColormap(display, RootWindow(display, vi->screen), vi->visual, AllocNone);
	swa.colormap = cmap;
	swa.background_pixmap = 0;
	swa.border_pixel = 0;
	//swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
	swa.event_mask = ExposureMask | StructureNotifyMask;
	
	//Create the window
	win = XCreateWindow(display,
		RootWindow(display, vi->screen),
		0, 0, width, height, 0, vi->depth, InputOutput,
		vi->visual,
		CWBorderPixel | CWColormap | CWEventMask, &swa);
	if(!win)
	{
		printf("main: Error. XCreateWindow() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	
	XFree(vi);

	//this fixes an XIO fatal error when closing.
	//This tells the window manager to send this program a message if the client
	//closes the window (by default we will not be notified, so turn on the notifications)
	wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(display, win, &wmDelete, 1);
	
	//Get the GLX context
	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddressARB("glXCreateContextAttribsARB");
	if(glXCreateContextAttribsARB == 0)
	{
		printf("main: Error. could not get glXCreateContextAttribsARB address.\n");
		in_CloseMouseInput();
		return 0;
	}
	ctx = glXCreateContextAttribsARB(display, chosen_fbc, 0, True, context_attribs);
	if(ctx == 0)
	{
		printf("main: Error. could not get gl context.\n");
		in_CloseMouseInput();
		return 0;
	}
	glXMakeCurrent(display, win, ctx);
	
	//print if we are direct rendering & OpenGL version
	if(glXIsDirect(display, ctx))
		printf("direct rendering enabled.\n");
	else
		printf("no direct rendering.\n");
	printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
	printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	
	//Load elevation data and build terrain map
	r = InitTerrain();
	if(r == 0)
	{
		printf("main: InitTerrain() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	
	//initialize the structure that holds bush position data
	r = InitBushGroup(&g_bush_group);
	if(r == -1)
	{
		printf("main: InitBushGroup() failed.\n");
		in_CloseMouseInput();
		return 0;
	}
	
	//Initialize OpenGL objects and shaders
	running = InitGL(width, height);
	e = glGetError();
	if(e != GL_NO_ERROR)
	{
		printf("main: glGetError() returned 0x%X\n", e);
	}
	
	printf("setup complete. entering message loop.\n");
	XMapRaised(display, win);
	
	//getting a starting time point to use when looping
	clock_gettime(CLOCK_MONOTONIC, &last_drawcall);
	clock_gettime(CLOCK_MONOTONIC, &last_simulatecall);
	clock_gettime(CLOCK_MONOTONIC, &curr_time);
	
	//message loop
	while(running)
	{
		//handle any pending X messages
		while(XPending(display) > 0)
		{
			XNextEvent(display, &event);
			if(event.type == Expose)
			{
				//printf("main: expose event\n");

				//Draw stuff & swap the buffer
				g_DrawFunc();
				glXSwapBuffers(display, win);
			}
			if(event.type == ClientMessage)
			{
				running = 0;
				break;
			}
			if(event.type == ConfigureNotify)
			{
				//glUseProgram(g_theProgram);
				//glViewport(0, 0, (GLsizei)event.xconfigure.width, (GLsizei)event.xconfigure.height);
				//TODO: This is messed up, CalculatePerspectiveMatrix updates one of the shader programs but not all of them
				//CalculatePerspectiveMatrix(event.xconfigure.width, event.xconfigure.height);
				//glUniformMatrix4fv(g_bush_shader.perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
				//glUseProgram(0);
			}
		}
		r = UpdateFromMouseInput(&g_camera_rotX, &g_camera_rotY);
		if(r == 0)
		{
			running = 0;
			break;
		}

		//get the current time and check to see if the enough time has passed
		//to call update again
		clock_gettime(CLOCK_MONOTONIC, &curr_time);
		GetElapsedTime(&last_simulatecall, &curr_time, &diff);
		if(diff.tv_sec > diff_simulate.tv_sec || (diff.tv_sec == diff_simulate.tv_sec && diff.tv_nsec >= diff_simulate.tv_nsec))
		{
			last_simulatecall.tv_sec = curr_time.tv_sec;
			last_simulatecall.tv_nsec = curr_time.tv_nsec;
			r = HandleKeyboardInput(display, &g_camera_rotX, &g_camera_rotY, g_camera_pos, &g_a_man, &g_a_vehicle);
			if(r == 0) //error
			{
				running = 0;
				break;
			}
			if(g_pause_simulation_step == 0)
			{
				SimulationStep(); 
				//printf("***simulation step end***\n");
			}
			if(g_render_mode == 1) //Inventory Screen
			{
				UpdateGUI();
			}
		}

		//get the current time and check to see if enough time has elapsed to call DrawScene()
		clock_gettime(CLOCK_MONOTONIC, &curr_time);
		GetElapsedTime(&last_drawcall, &curr_time, &diff);
		if(diff.tv_sec > diff_drawcall.tv_sec || (diff.tv_sec == diff_drawcall.tv_sec && diff.tv_nsec >= diff_drawcall.tv_nsec))
		{
			clock_gettime(CLOCK_MONOTONIC, &tdrawSceneStart);
			g_DrawFunc();
			clock_gettime(CLOCK_MONOTONIC, &tdrawSceneEnd);
			glXSwapBuffers(display, win);
			last_drawcall.tv_sec = curr_time.tv_sec;
			last_drawcall.tv_nsec = curr_time.tv_nsec;
			DebugUpdateDrawCallStats(&diff, &tdrawSceneStart, &tdrawSceneEnd);	//keep track of the latest time since last draw
		}
	}
	
	in_CloseMouseInput();
	//release glx context
	glXMakeCurrent(display, None, 0);
	glXDestroyContext(display, ctx);
	XCloseDisplay(display);
	
	return 0;
}

int InitGL(unsigned int width, unsigned int height)
{
/*This vertex shader changes darkens the color for vertices below the waterline (world pos.y==0.0)*/
	char * vertexShaderString;
	char * fragmentShaderString;
	struct item_struct * pitem=0;
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;
	int i;
	int r;
	float origin[3] = {0.0f, 0.0f, 0.0f};
	float tempVec[3];
	//TODO: I really have to remove these unrelated vars to OpenGL from here...
	struct character_struct * pNewSoldier=0;
	
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);
	
	//compile the vertex shader
	vertexShaderString = LoadShaderSource("shaders/terrain.vert");
	if(vertexShaderString == 0)
		return 0;
	g_shaderlist[0] = glCreateShader(GL_VERTEX_SHADER);
	printf("created shader %d\n", g_shaderlist[0]);
	glShaderSource(g_shaderlist[0], 1, &vertexShaderString, 0);
	glCompileShader(g_shaderlist[0]);
	glGetShaderiv(g_shaderlist[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(g_shaderlist[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitGL: error. malloc() failed to allocate for strInfoLength.\n");
			return 0;
		}
		glGetShaderInfoLog(g_shaderlist[0], infoLogLength, 0, strInfoLog);
		printf("InitGL: Compile failure in shader(%d):\n%s\n", g_shaderlist[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource("shaders/terrain.frag");
	if(fragmentShaderString == 0)
		return 0;
	g_shaderlist[1] = glCreateShader(GL_FRAGMENT_SHADER);
	printf("created shader %d\n", g_shaderlist[1]);
	glShaderSource(g_shaderlist[1], 1, &fragmentShaderString, 0);
	glCompileShader(g_shaderlist[1]);
	glGetShaderiv(g_shaderlist[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(g_shaderlist[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitGL: error. malloc() failed to allocate for strInfoLength.\n");
			return 0;
		}
		glGetShaderInfoLog(g_shaderlist[1], infoLogLength, 0, strInfoLog);
		printf("InitGL: compile failure in shader(%d):\n%s\n", g_shaderlist[1], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//link the gl program
	status = 0;
	g_theProgram = glCreateProgram();
	glAttachShader(g_theProgram, g_shaderlist[0]);
	glAttachShader(g_theProgram, g_shaderlist[1]);
	glLinkProgram(g_theProgram);
	glGetProgramiv(g_theProgram, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(g_theProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		glGetProgramInfoLog(g_theProgram, infoLogLength, 0, strInfoLog);
		printf("InitGL: Link Failure: %s\n", strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//detach the shaders after linking
	glDetachShader(g_theProgram, g_shaderlist[0]);
	glDetachShader(g_theProgram, g_shaderlist[1]);
	
	glUseProgram(g_theProgram);
	
	//setup the perspective matrix
	g_perspectiveMatrixUnif = glGetUniformLocation(g_theProgram, "perspectiveMatrix");
	if(g_perspectiveMatrixUnif == -1)
	{
		printf("InitGL: failed to get uniform location of '%s'\n", "perspectiveMatrix");
		return 0;
	}
	CalculatePerspectiveMatrix(width, height);
	glUniformMatrix4fv(g_perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
	
	//get the location of the model-to-camera matrix
	g_modelToCameraMatrixUnif = glGetUniformLocation(g_theProgram, "modelToCameraMatrix");
	if(g_modelToCameraMatrixUnif == -1)
	{
		printf("InitGL: failed to get uniform location of %s\n", "modelToCameraMatrix");
		return 0;
	}
	g_lightDirUnif = glGetUniformLocation(g_theProgram, "lightDir");
	if(g_lightDirUnif == -1)
	{
		printf("InitGL: failed to get uniform location of %s\n", "lightDir");
	}
	g_modelSpaceCameraPosUnif = glGetUniformLocation(g_theProgram, "modelSpaceCameraPos");
	if(g_modelSpaceCameraPosUnif == -1)
	{
		printf("InitGL: failed to get uniform location of %s\n", "modelSpaceCameraPos");
	}
	
	//texture uniforms
	g_big_terrain.colorTexUnit = 0;
	g_big_terrain.colorTextureUnif = glGetUniformLocation(g_theProgram, "colorTexture");
	glUniform1iv(g_big_terrain.colorTextureUnif, 1, &(g_big_terrain.colorTexUnit));
	
	glUseProgram(0);
	
	//Setup the shaders for the detailed bush
	r = InitBushShaders(&g_bush_shader);
	if(r == 0)
		return 0;
	
	//Setup the simple camera-facing billboard shaders
	r = InitBillboardShaders(&g_billboard_shader);
	if(r == 0)
		return 0;
		
	//Setup the simple wave plane shader
	r = InitSimpleWavePlaneShader(&g_simple_wave_shader);
	if(r == 0)
		return 0;
		
	//setup the animation shader
	r = InitCharacterShaders(&g_character_shader);
	if(r == 0)
		return 0;
	
	//setup the VAO and buffers for the terrain map
	glGenBuffers(1, &(g_big_terrain.ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_big_terrain.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_big_terrain.num_indices*sizeof(GLshort), g_big_terrain.pElements, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	for(i = 0; i < g_big_terrain.num_tiles; i++)
	{
		glGenBuffers(1, &(g_big_terrain.pTiles[i].vbo));
		glBindBuffer(GL_ARRAY_BUFFER, g_big_terrain.pTiles[i].vbo);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_big_terrain.pTiles[i].num_verts*8*sizeof(float)), g_big_terrain.pTiles[i].pPos, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glGenVertexArrays(1, &(g_big_terrain.pTiles[i].vao));
		glBindVertexArray(g_big_terrain.pTiles[i].vao);
		glBindBuffer(GL_ARRAY_BUFFER, g_big_terrain.pTiles[i].vbo);
		glEnableVertexAttribArray(0); //position
		glEnableVertexAttribArray(1); //normal vector
		glEnableVertexAttribArray(2); //texcoord
		glVertexAttribPointer(0,	//index
			3,			//size
			GL_FLOAT,		//type
			GL_FALSE,		//normalized => no
			8*sizeof(float),        //stride
			0);			//pointer offset
		glVertexAttribPointer(1,
			3,
			GL_FLOAT,
			GL_FALSE,
			8*sizeof(float),	//stride
			(GLvoid*)(3*sizeof(float)));
		glVertexAttribPointer(2,
			2,
			GL_FLOAT,
			GL_FALSE,
			8*sizeof(float),
			(GLvoid*)(6*sizeof(float)));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_big_terrain.ebo);
		glBindVertexArray(0);
	}
	
	//load the terrain map's sand texture
	LoadTexture();

	//check to make sure GLint is the same size as int
	if(sizeof(GLint) != sizeof(int))
	{
		printf("InitGL: error. bush element array size mismatch. GLint not same size as int.\n");
		return 0;
	}

	//Init some global camera state
	r = InitCamera(&g_camera_state);
	if(r != 1)
		return 0;

	//Setup some texture samplers that will be used by all bush textures (trunk and branch)
	SetupBushSamplers();

	//Load rifle object
	r = InitItemCommon(&g_rifle_common, "./resources/models/m40_rifle.obj", "m40_body", "./resources/textures/m40sniper2.tga");
	if(r != 1)
		return 0;
	InitRifleCommon(&g_rifle_common);

	r = InitItemCommon(&g_canteen_common, "./resources/models/canteen.obj", "body_Circle", "./resources/textures/canteen.tga");
	if(r != 1)
		return 0;
	r = InitItemCommon(&g_beans_common, "./resources/models/beans.obj", "Cylinder.002", "./resources/textures/canny.tga");
	if(r != 1)
		return 0;

	r = InitItemCommon(&g_pistol_common, "./resources/models/colt45.obj", "gun_Circle.000", "./resources/textures/coltdiff.tga");
	if(r != 1)
		return 0;
	g_pistol_common.handBoneIndex = 10; //see InitRifleCommon

	//load bush model
	r = LoadBushVBO(&g_bush_billboard, 	//address of plant billboard
		"./resources/models/bush_billboard_02.obj",	//.obj filename
		"Plane",			//mesh name to use in .obj file
		"./resources/textures/m00_tourne_billboard_00.tga",	//texture filename (note: texture must have an alpha value)
		0.0f,
		1); 			//flags. manually load mipmap
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed.\n");
		return 0;
	}

	//Setup the model for the palm_2.obj (palm tree version 2)
	r = LoadBushVBO(&g_palm_fronds,
			"./resources/models/palm_2.obj",	//filename
			"Plane",	//name of 'o' line in obj file of mesh to load
			"./resources/textures/palm_frond_0.tga",
			0.0f,
			0);				//flags. 
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s o=%s.\n", "palm_2.obj", "Plane");
		return 0;
	}
	//for fronds use default texture sampler settings
	//now load the palm trunk
	r = LoadBushVBO(&g_palm_trunk,
			"./resources/models/palm_2.obj",			//filename
			"tree.001_Mesh.001",	//name of 'o' line in obj file of mesh to load
			"./resources/textures/palm_bark_4.tga",
			0.0f,
			0);						//flags.
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s o=%s.\n", "palm_2.obj", "tree.001_Mesh.001");
		return 0;
	}

	//Setup model for pemphis_shrub
	r = LoadBushVBO(&g_pemphis_shrub,
			"./resources/models/pemphis_shrub.obj",
			"small_branch.013_Plane.014",
			"./resources/textures/pemphis_branch_2.tga",
			0.0f,
			0);		//flags
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "pemphis_shrub.obj");
		return 0;
	}

	//Setup model for Scaevola shrub
	r = LoadBushVBO(&g_scaevola_shrub,
			"./resources/models/scaevola_billboard_00.obj",
			"Plane.002",
			"./resources/textures/scaevola_branch.tga",
			0.5f,
			2);		//flags
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "scaevola_billboard_00.obj");
		return 0;
	}

	//Setup model for Tourne Fortia shrub
	r = LoadBushVBO(&g_tournefortia_trunk,
			"./resources/models/tourne_fortia_tree.obj",
			"trunk.001_Cylinder",
			"./resources/textures/bark_1.tga",
			0.0f,
			0);
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "tourne_fortia_tree.obj");
		return 0;
	}
	r = LoadBushVBO(&g_tournefortia_shrub,
			"./resources/models/tourne_fortia_tree_branches.obj",
			"branchBillboard.023_Plane.058",
			"./resources/textures/tourneFortia_branch_billboard.tga",
			0.0f,
			0);
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "tourne_fortia_tree_branches.obj");
		return 0;
	}

	//Setup model for ironwood
	r = LoadBushVBO(&g_ironwood_trunk,
			"./resources/models/ironwood_02_trunk.obj",
			"Cube",
			"./resources/textures/crap_gray_bark.tga",
			0.2104f,
			2);
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "ironwood_02_trunk.obj");
		return 0;
	}
	r = LoadBushVBO(&g_ironwood_branches,
			"./resources/models/ironwood_02_branches.obj",
			"longBranchBillboard.011_Plane.004",
			"./resources/textures/ironwood_branch_02.tga",
			0.2104f,
			2);
	if(r == 0)
	{
		printf("InitGL: error. LoadBushVBO() failed for %s.\n", "ironwood_02_branches.obj");
		return 0;
	}
	
	//int InitDotsTga(struct dots_struct * p_dots_info)
	//Initialize the dots.tga data structures that are used by vegetation tiles
	r = InitDotsTga(&g_dots_tgas);
	if(r == -1)
	{
		printf("InitGL: error InitDotsTga() failed.\n");
		return 0;
	}
	
	//load a simple vbo
	//r = LoadSimpleBillboardVBO(&g_bush_smallbillboard, 1.0f, 1.0f, "./resources/textures/m00_tourne_billboard_00.tga");
	r = LoadSimpleBillboardVBO(&g_bush_smallbillboard);
	if(r == -1)
	{
		printf("InitGL: error LoadSimpleBillboardVBO() failed.\n");
		return 0;
	}
	r = LoadSimpleBillboardTextures(&g_bush_smallbillboard);
	if(r != 1)
	{
		printf("InitGL: error LoadSimpleBillboardTextures() failed.\n");
		return 0;
	}
	
	//r = InitPlantGrid(&g_bush_grid, &g_dots_tgas, 0, 0.05f);
	//if(r == -1)
	//{
	//	printf("InitGL: error InitPlantGrid() failed.\n");
	//	return 0;
	//}
	r = InitPlantGrid2(&g_bush_grid);
	if(r == -1)
		return 0;

	//TODO: Remove this test of writing plant grid to file:
	//r = WritePlantGridToFile(&g_bush_grid, "plantpos.dat");
	//if(r == -1)
	//	return 0;

	r = InitMoveablesGrid(&g_moveables_grid);
	if(r == 0)
		return 0;
	
	//load a simple wave plane VBO
	r = MakeSimpleWaveVBO(&g_simple_wave);
	if(r == -1)
	{
		printf("InitGL: error MakeSimpleWaveVBO() failed.\n");
		return 0;
	}
	
	//Load vehicle models and physics:
	r = InitVehicleCommon(&g_vehicle_common);
	if(r == -1)
	{
		return 0;
	}
	
    r = InitWheelCommon(&g_wheel_common);
	if(r == -1)
	{
		return 0;
	}
	
	r = InitSomeVehicle2(&g_b_vehicle);
	//end load vehicle stuff
	
	//load character animation
	memset(&g_triangle_man, 0, sizeof(struct character_model_struct));
	r = InitCharacterCommon2(&g_triangle_man);
	if(r == 0)
	{
		printf("InitGL: error InitCharacterCommon2() failed.\n");
		return 0;
	}

	r = InitCharacterList(&g_soldier_list);
	if(r == 0)
		return 0;

	//this initializes data particular to an instance of triangle man (e.g. position, velocity...)
	//Needs to have InitCharacterList() called before calling.
	tempVec[0] = 12319.272461f;
    tempVec[1] = 25.0f;
	tempVec[2] = 11279.0f;	
	r = InitCharacterPerson(&g_a_man, tempVec);
	if(r == 0)
	{
		printf("InitGL: error InitCharacterPerson() failed.\n");
		return 0;
	}
	//Make some more guys
	//for(i = 0; i < 31; i++)
	for(i = 0; i < 2; i++)
	{
		pNewSoldier = (struct character_struct*)malloc(sizeof(struct character_struct));
		if(pNewSoldier == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		tempVec[0] = 12309.272461f + (3.0f*(float)i); //put him 10 to the right of the first guy
		tempVec[1] = 25.0f;
		tempVec[2] = 11279.0f;
		r = InitCharacterPerson(pNewSoldier, tempVec);
		if(r == 0)
			return 0;
	}

	//Setup an AI for 1 soldier
	r = InitSoldierAI(&g_ai_soldier, g_soldier_list.ptrsToCharacters[1]); //use the 2nd soldier in the list (1st soldier is local player)
	if(r == 0) //error
		return 0;

	r = InitGroundSlots(&g_temp_ground_slots);
	if(r == 0)
		return 0;

	//Load crate
	r = InitMoveableModelCommon(&g_crate_model_common, "Crate_Cube.003", "./resources/models/crate.obj", "./resources/textures/crate.tga");
	if(r == 0)
		return 0;
	g_crate_model_common.offset_groundToCg = 0.4578f;
	r = InitCrateCommon(&g_crate_physics_common);
	if(r == 0)
		return 0;

	r = InitMoveableModelCommon(&g_barrel_model_common, "barrel_Cylinder.007", "./resources/models/barrel.obj", "./resources/textures/barrelONE.tga");
	if(r == 0)
		return 0;
	g_barrel_model_common.offset_groundToCg = 0.3999f;

	r = InitMoveableModelCommon(&g_dock_common, "Cube", "./resources/models/dock00.obj", "./resources/textures/dock_skin2.tga");
	if(r == 0)
		return 0;

	r = InitMoveableModelCommon(&g_bunker_model_common, "bunker_Plane.002", "./resources/models/bunker00.obj", "./resources/textures/bunkerONE.tga");
	if(r == 0)
		return 0;
	g_bunker_model_common.offset_groundToCg = 0.0f;

	r = InitMoveableModelCommon(&g_warehouse_model_common, "Cylinder", "./resources/models/warehouse00.obj", "./resources/textures/corrugated02.tga");
	if(r == 0)
		return 0;

	//Initialize Base stuff
	tempVec[0] = 12464.0f;
	tempVec[1] = 13.799957f;
	tempVec[2] = 11081.0f;
	r = CreateBase(tempVec);
	if(r == 0)
	{
		return 0;
	}
	r = CreateBunker(tempVec);
	if(r == 0)
		return 0;

	tempVec[0] = 12442.5195f;
	tempVec[1] = 0.0f; //doesn't matter will get filled in
	tempVec[2] = 11044.5684f;
	r = CreateDock(tempVec);
	if(r == 0)
		return 0;

	tempVec[0] = 12464.0f;
	tempVec[1] = 0.0f; //doesn't matter will get filled in
	tempVec[2] = 11066.1838f;
	r = CreateWarehouse(tempVec);
	if(r == 0)
		return 0;

	//TODO: This is debug to check items on ground. Just create some items on ground to test.
	tempVec[0] = 12469.0f;
	tempVec[1] = 0.0f;	//y coord will be filled in by CreateItem()
	tempVec[2] = 11074.0f;
	pitem = CreateItem(tempVec, ITEM_TYPE_RIFLE);
	if(pitem == 0)
		return 0;
   	r = AddItemToTile(pitem);
	if(r != 1)
	{
		return 0;
	}	

	tempVec[0] = 12465.0f;
	tempVec[1] = 0.0f;
	tempVec[2] = 11074.0f;
	pitem = CreateItem(tempVec, ITEM_TYPE_CANTEEN);
	if(pitem == 0)
		return 0;
   	r = AddItemToTile(pitem);
	if(r != 1)
	{
		return 0;
	}

	tempVec[0] = 12460.0f;
	tempVec[1] = 0.0f;
	tempVec[2] = 11074.0f;
	pitem = CreateItem(tempVec, ITEM_TYPE_BEANS);
	if(pitem == 0)
		return 0;
   	r = AddItemToTile(pitem);
	if(r != 1)
	{
		return 0;
	}

	tempVec[0] = 12459.0f;
	tempVec[1] = 0.0f;
	tempVec[2] = 11074.0f;
	pitem = CreateItem(tempVec, ITEM_TYPE_PISTOL);
	if(pitem == 0)
		return 0;
	r = AddItemToTile(pitem);
	if(r != 1)
		return 0;

	//Setup GUI stuff
	r = InitGUIShaders(&g_gui_shaders, "shaders/gui.vert", "shaders/gui.frag", 0);
	if(r == 0)
		return 0;
	r = InitGUIShaders(&g_guitex_shaders, "shaders/gui_tex.vert", "shaders/gui_tex.frag", 1);
	if(r == 0)
		return 0;
	SetOrthoMat(g_orthoMat4, width, height);
	glUseProgram(g_gui_shaders.program);
	glUniformMatrix4fv(g_gui_shaders.uniforms[0], 1, GL_FALSE, g_orthoMat4);
	glUseProgram(g_guitex_shaders.program);
	glUniformMatrix4fv(g_guitex_shaders.uniforms[0], 1, GL_FALSE, g_orthoMat4);
	r = InitInvGUIVBO(&g_gui_info);
	if(r == 0)
		return 0;
	r = InitGUICursor(&g_gui_inv_cursor, width, height);
	if(r == 0)
		return 0;

	SetMapOrthoMat(g_mapOrthoMat4, 1.0f);
	//Remove these lines below because they are now performed by the keyboard handler
	//r = InitMapGUIVBO(&g_gui_map);
	//if(r == 0)
	//	return 0;
	//r = InitMapElevationLinesVBO(&g_gui_map);
	//if(r == 0)
	//	return 0;

	//Setup text shaders
	r = InitGUIShaders(&g_textShader, "shaders/text.vert", "shaders/text.frag", 1);
	if(r == 0)
		return 0;
	//set the projection matrix since InitGUIShaders doesn't do this.
	glUseProgram(g_textShader.program);
	glUniformMatrix4fv(g_textShader.uniforms[0], 1, GL_FALSE, g_mapOrthoMat4); 
	glUseProgram(0);
	r = InitTextVBO(&g_textModel);
	if(r == 0)
		return 0;

	//Back to just general OpenGL
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	
	//wireframe if necessary
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glDepthRange(0.0f, 1.0f);
	
	//glClearColor(0.0f,0.0f,0.0f,0.0f); //black background
	glClearColor(1.0f,1.0f,1.0f,0.0f); //white background
	glClearDepth(1.0f);
		
	return 1;
}

int InitBushShaders(struct bush_shader_struct * p_shader)
{
	char * vertexShaderString;
	char * fragmentShaderString;
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;

	//compile the bush vertex shader
	vertexShaderString = LoadShaderSource("shaders/bush.vert");
	if(vertexShaderString == 0)
		return 0;
	p_shader->shaderList[0] = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(p_shader->shaderList[0], 1, &vertexShaderString, 0);
	glCompileShader(p_shader->shaderList[0]);
	glGetShaderiv(p_shader->shaderList[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitBushShaders: error. malloc() failed to allocate for strInfoLength.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[0], infoLogLength, 0, strInfoLog);
		printf("InitBushShaders: Compile failure in shader(%d):\n%s\n", p_shader->shaderList[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource("shaders/bush.frag");
	if(fragmentShaderString == 0)
		return 0;
	p_shader->shaderList[1] = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(p_shader->shaderList[1], 1, &fragmentShaderString, 0);
	glCompileShader(p_shader->shaderList[1]);
	glGetShaderiv(p_shader->shaderList[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitBushShaders: error. malloc() failed to allocate for strInfoLength.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[1], infoLogLength, 0, strInfoLog);
		printf("InitBushShaders: compile failure in shader(%d):\n%s\n", p_shader->shaderList[1], strInfoLog);
		return 0;
	}
	
	//link the gl program
	status = 0;
	p_shader->program = glCreateProgram();
	glAttachShader(p_shader->program, p_shader->shaderList[0]);
	glAttachShader(p_shader->program, p_shader->shaderList[1]);
	glLinkProgram(p_shader->program);
	glGetProgramiv(p_shader->program, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(p_shader->program, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		glGetProgramInfoLog(p_shader->program, infoLogLength, 0, strInfoLog);
		printf("InitBushShaders: Link Failure:\n%s\n", strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//detach the shaders after linking
	glDetachShader(p_shader->program, p_shader->shaderList[0]);
	glDetachShader(p_shader->program, p_shader->shaderList[1]);
	
	glUseProgram(p_shader->program);
	
	//Get the uniforms
	//setup the perspective matrix uniform
	p_shader->perspectiveMatrixUnif = glGetUniformLocation(p_shader->program, "perspectiveMatrix");
	if(p_shader->perspectiveMatrixUnif == -1)
	{
		printf("InitBushShaders: failed to get uniform location of %s\n", "perspectiveMatrix");
		return 0;
	}
	//assume that CalculatePerspectiveMatrix() was already called...
	glUniformMatrix4fv(p_shader->perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
	
	//setup the model-to-camera matrix uniform
	p_shader->modelToCameraMatrixUnif = glGetUniformLocation(p_shader->program, "modelToCameraMatrix");
	if(p_shader->modelToCameraMatrixUnif == -1)
	{
		printf("InitBushShaders: failed to get uniform location of %s\n", "modelToCameraMatrix");
		return 0;
	}
	
	//setup the light direction uniform
	p_shader->lightDirUnif = glGetUniformLocation(p_shader->program, "lightDir");
	if(p_shader->lightDirUnif == -1)
	{
		printf("InitBushShaders: failed to get uniform location of %s\n", "lightDir");
		return 0;
	}
	
	//setup the texture uniform
	p_shader->colorTexUnit = 0;
	p_shader->colorTextureUnif = glGetUniformLocation(p_shader->program, "colorTexture");
	if(p_shader->colorTextureUnif == -1)
	{
		printf("InitBushShaders: failed to get uniform location of %s\n", "colorTexture");
		return 0;
	}
	glUniform1iv(p_shader->colorTextureUnif, 1, &(p_shader->colorTexUnit));
	
	glUseProgram(0);
	
	return 1;
}

int InitBillboardShaders(struct simple_billboard_shader_struct * p_shader)
{
	char * vertexShaderString;
	char * fragmentShaderString;
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;
	
	//compile the vertex shader
	vertexShaderString = LoadShaderSource("shaders/bush_billboard.vert");
	if(vertexShaderString == 0)
		return 0;
	p_shader->shaderList[0] = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(p_shader->shaderList[0], 1, &vertexShaderString, 0);
	glCompileShader(p_shader->shaderList[0]);
	glGetShaderiv(p_shader->shaderList[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitBillboardShaders: error. malloc() failed to allocate for strInfoLog.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[0], infoLogLength, 0, strInfoLog);
		printf("InitBillboardShaders: compile failure in shader(%d):\n%s\n", p_shader->shaderList[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource("shaders/bush_billboard.frag");
	if(fragmentShaderString == 0)
		return 0;
	p_shader->shaderList[1] = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(p_shader->shaderList[1], 1, &fragmentShaderString, 0);
	glCompileShader(p_shader->shaderList[1]);
	glGetShaderiv(p_shader->shaderList[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitBillboardShaders: error. malloc() failed to allocate for strInfoLog.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[1], infoLogLength, 0, strInfoLog);
		printf("InitBillboardShaders: compile failure in shader(%d):\n%s\n", p_shader->shaderList[1], strInfoLog);
		return 0;
	}
	
	//link the gl program
	status = 0;
	p_shader->program = glCreateProgram();
	glAttachShader(p_shader->program, p_shader->shaderList[0]);
	glAttachShader(p_shader->program, p_shader->shaderList[1]);
	glLinkProgram(p_shader->program);
	glGetProgramiv(p_shader->program, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(p_shader->program, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		glGetProgramInfoLog(p_shader->program, infoLogLength, 0, strInfoLog);
		printf("InitBillboardShaders: Link Failure:\n%s\n", strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//detach the shaders after linking
	glDetachShader(p_shader->program, p_shader->shaderList[0]);
	glDetachShader(p_shader->program, p_shader->shaderList[1]);
	
	glUseProgram(p_shader->program);
	
	//Get the uniforms
	p_shader->perspectiveMatrixUnif = glGetUniformLocation(p_shader->program, "perspectiveMatrix");
	if(p_shader->perspectiveMatrixUnif == -1)
	{
		printf("InitBillboardShaders: failed to get uniform location of %s\n", "perspectiveMatrix");
		return 0;
	}
	//assume that CalculatePerspectiveMatrix() was already called.
	glUniformMatrix4fv(p_shader->perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
	
	p_shader->modelToCameraMatrixUnif = glGetUniformLocation(p_shader->program, "modelToCameraMatrix");
	if(p_shader->modelToCameraMatrixUnif == -1)
	{
		printf("InitBillboardShaders: failed to get uniform location of %s\n", "modelToCameraMatrix");
		return 0;
	}
	
	//setup the texture uniform
	p_shader->colorTexUnit = 0;
	p_shader->colorTextureUnif = glGetUniformLocation(p_shader->program, "colorTexture");
	//if(p_shader->colorTextureUnif == -1)
	//{
	//	printf("InitBillboardShaders: failed to get uniform location of %s\n", "colorTexture");
	//	return 0;
	//}
	glUniform1iv(p_shader->colorTextureUnif, 1, &(p_shader->colorTexUnit));
	
	//setup the billboard size uniforms (these get set depending on the billboard)
	p_shader->billboardSizeUnif = glGetUniformLocation(p_shader->program, "billboardSize");
	
	glUseProgram(0);
	
	return 1;
}

int InitSimpleWavePlaneShader(struct simple_wave_shader_struct * p_shader)
{
	char * vertexShaderString;
	char * fragmentShaderString;
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;
	
	//compile the vertex shader
	vertexShaderString = LoadShaderSource("shaders/wave_simple.vert");
	if(vertexShaderString == 0)
		return 0;
	p_shader->shaderList[0] = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(p_shader->shaderList[0], 1, &vertexShaderString, 0);
	glCompileShader(p_shader->shaderList[0]);
	glGetShaderiv(p_shader->shaderList[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitSimpleWavePlaneShader: error. malloc() failed for vertex shader's error log.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[0], infoLogLength, 0, strInfoLog);
		printf("InitSimpleWavePlaneShader: vertex shader(%d) compile failure:\n%s\n", p_shader->shaderList[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource("shaders/wave_simple.frag");
	if(fragmentShaderString == 0)
		return 0;
	p_shader->shaderList[1] = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(p_shader->shaderList[1], 1, &fragmentShaderString, 0);
	glCompileShader(p_shader->shaderList[1]);
	glGetShaderiv(p_shader->shaderList[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitSimpleWavePlaneShader: error. malloc() failed for fragment shader's error log.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[1], infoLogLength, 0, strInfoLog);
		printf("InitSimplewavePlaneShader: fragment shader(%d) compile failure:\n%s\n", p_shader->shaderList[1], strInfoLog);
		return 0;
	}
	
	//link the program
	status = 0;
	p_shader->program = glCreateProgram();
	glAttachShader(p_shader->program, p_shader->shaderList[0]);
	glAttachShader(p_shader->program, p_shader->shaderList[1]);
	glLinkProgram(p_shader->program);
	glGetProgramiv(p_shader->program, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(p_shader->program, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		glGetProgramInfoLog(p_shader->program, infoLogLength, 0, strInfoLog);
		printf("InitSimpleWavePlaneShaders: Link Failure:\n%s\n", strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//detach the shaders after linking
	glDetachShader(p_shader->program, p_shader->shaderList[0]);
	glDetachShader(p_shader->program, p_shader->shaderList[1]);
	
	glUseProgram(p_shader->program);
	
	//Get all uniform locations
	p_shader->perspectiveMatrixUnif = glGetUniformLocation(p_shader->program, "perspectiveMatrix");
	if(p_shader->perspectiveMatrixUnif == -1)
	{
		printf("InitSimpleWavePlaneShaders: failed to get uniform location of %s\n", "perspectiveMatrix");
		return 0;
	}
	//assume that CalculatePerspectiveMatrix() was already called:
	glUniformMatrix4fv(p_shader->perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
	
	p_shader->modelToCameraMatrixUnif = glGetUniformLocation(p_shader->program, "modelToCameraMatrix");
	if(p_shader->modelToCameraMatrixUnif == -1)
	{
		printf("InitSimpleWavePlaneShaders: failed to get uniform location of %s\n", "modelToCameraMatrix");
		return 0;

	}
	
	p_shader->lightDirUnif = glGetUniformLocation(p_shader->program, "lightDir");
	if(p_shader->lightDirUnif == -1)
	{
		printf("InitSimpleWavePlaneShaders: failed to get uniform location of %s\n", "lightDir");
		//return 0;

	}
	p_shader->shininessFactorUnif = glGetUniformLocation(p_shader->program, "shininessFactor");
	if(p_shader->shininessFactorUnif == -1)
	{
		printf("InitSimpleWavePlaneShaders: failed to get uniform location of %s\n", "shininessFactor");
		//return 0;

	}
	p_shader->modelSpaceCameraPosUnif = glGetUniformLocation(p_shader->program, "modelSpaceCameraPos");
	if(p_shader->modelSpaceCameraPosUnif == -1)
	{
		printf("InitSimpleWavePlaneShaders: failed to get uniform location of %s\n", "modelSpaceCameraPos");
		//return 0;

	}
	glUseProgram(0);
	
	return 1;
}

int InitCharacterShaders(struct character_shader_struct * p_shader)
{
	char * vertexShaderString;
	char * fragmentShaderString;
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;
	
	//compile the vertex shader
	vertexShaderString = LoadShaderSource("shaders/person.vert");
	if(vertexShaderString == 0)
		return 0;
	p_shader->shaderList[0] = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(p_shader->shaderList[0], 1, &vertexShaderString, 0);
	glCompileShader(p_shader->shaderList[0]);
	glGetShaderiv(p_shader->shaderList[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitCharacterShaders: error. malloc() failed for vert strInfoLog.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[0], infoLogLength, 0, strInfoLog);
		printf("InitCharacterShaders: vertex shader (%d) compile fail:\n%s\n", p_shader->shaderList[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource("shaders/person.frag");
	if(fragmentShaderString == 0)
		return 0;
	p_shader->shaderList[1] = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(p_shader->shaderList[1], 1, &fragmentShaderString, 0);
	glCompileShader(p_shader->shaderList[1]);
	glGetShaderiv(p_shader->shaderList[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(p_shader->shaderList[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitCharacterShaders: error. malloc() failed for frag strInfoLog.\n");
			return 0;
		}
		glGetShaderInfoLog(p_shader->shaderList[1], infoLogLength, 0, strInfoLog);
		printf("InitCharacterShaders: frag shader (%d) compile fail:\n%s\n", p_shader->shaderList[1], strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	//link the program
	status = 0;
	p_shader->program = glCreateProgram();
	glAttachShader(p_shader->program, p_shader->shaderList[0]);
	glAttachShader(p_shader->program, p_shader->shaderList[1]);
	glLinkProgram(p_shader->program);
	glGetProgramiv(p_shader->program, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(p_shader->program, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("InitCharacterShaders: error. malloc() failed for link error string.\n");
			return 0;
		}
		glGetProgramInfoLog(p_shader->program, infoLogLength, 0, strInfoLog);
		printf("InitCharacterShaders: Link failure:\n%s\n", strInfoLog);
		free(strInfoLog);
		return 0;
	}
	
	glDetachShader(p_shader->program, p_shader->shaderList[0]);
	glDetachShader(p_shader->program, p_shader->shaderList[1]);
	
	glUseProgram(p_shader->program);
	
	//Get all uniform locations
	p_shader->perspectiveMatrixUnif = glGetUniformLocation(p_shader->program, "perspectiveMatrix");
	if(p_shader->perspectiveMatrixUnif == -1)
	{
		printf("InitCharacterShaders: error. failed to get uniform location of %s\n", "perspectiveMatrix");
		return 0;
	}
	//assume that CalculatePerspectiveMatrix() has already been called.
	glUniformMatrix4fv(p_shader->perspectiveMatrixUnif, 1, GL_FALSE, g_perspectiveMatrix);
	
	p_shader->modelToCameraMatrixUnif = glGetUniformLocation(p_shader->program, "modelToCameraMatrix");
	if(p_shader->modelToCameraMatrixUnif == -1)
	{
		printf("InitCharacterShaders: error. failed to get uniform location of %s\n", "modelToCameraMatrix");
		return 0;		
	}
	
	p_shader->lightDirUnif = glGetUniformLocation(p_shader->program, "lightDir");
	//if(p_shader->lightDirUnif == -1)
	//{
	//	printf("InitCharacterShaders: error. failed to get uniform location of %s\n", "lightDir");
	//	return 0;		
	//}
	
	p_shader->colorTextureUnif = glGetUniformLocation(p_shader->program, "colorTexture");
	//if(p_shader->colorTextureUnif == -1)
	//{
	//	printf("InitCharacterShaders: error. failed to get uniform location of %s\n", "colorTexture");
	//	return 0;
	//}
	p_shader->colorTexUnit = 0;
	glUniform1iv(p_shader->colorTextureUnif, 1, &(p_shader->colorTexUnit));
	
	p_shader->modelSpaceCameraPosUnif = glGetUniformLocation(p_shader->program, "modelSpaceCameraPos");
	//if(p_shader->modelSpaceCameraPosUnif == -1)
	//{
	//	printf("InitCharacterShaders: error. failed to get uniform location of %s\n", "modelSpaceCameraPos");
	//	return 0;
	//}
	
	glUseProgram(0);
	
	return 1;
}

void DrawScene(void)
{
	struct moveable_object_struct * pmoveable=0;
	struct item_struct * pitem=0;
	struct character_struct * psoldier=0;
	float mCameraMatrix[16];
	float mTranslateCameraMatrix[16];
	float mRotateCameraMatrix[16];
	float mRotateCameraMat_X[16];
	float mRotateCameraMat_Y[16];
	float mModelToCameraMatrix[16];
	float mBaseModelMatrix[16];
	float mChildModelMatrix[16];
	float mModelMatrix[16];
	float mTranslateModelMatrix[16];
	float mRotateModelMatrix_Y[16];
	float mRotateModelMatrix[16];
	float iModelMatrix[16];
	float mag;
	float vCameraPos[4];
	float lightDir[] = {0.0f, 1.0f, 0.0f};
	float shininess = 40.0f;
	float * v3;
	int i;
	int j;
	int k;
	int r;
	int local_tile;
	int is_detail_bush_tile;
	int iplant_type;
	int ilastplant_type = -1; //set to an invalid plant type.

	g_debug_num_simple_billboard_draws = 0;
	g_debug_num_detail_billboard_draws = 0;
	g_debug_num_himodel_plant_draws = 0;

	//prepare for doing frustum clipping tests
	if(g_debug_freeze_culling == 0) //if debug freeze culling is set then skip.
	{
		ClipTilesSetupFrustum();
		local_tile = GetLvl1Tile(g_camera_frustum.camera);
	}

	//update a grid that holds tiles that are around the camera, and a bounding box
	//around the camera that is later used to render bushes.
	UpdatePlantDrawGrid(&g_bush_grid, local_tile, g_ws_camera_pos);

	UpdateTerrainDrawBox(g_ws_camera_pos);

	UpdateMoveablesLocalGrid(&g_moveables_grid, g_ws_camera_pos);
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	//normalize the light dir to make it a unit vector
	mag = vMagnitude(lightDir);
	lightDir[0] = lightDir[0] / mag;
	lightDir[1] = lightDir[1] / mag;
	lightDir[2] = lightDir[2] / mag;
	
	mmMakeIdentityMatrix(mCameraMatrix);
	mmTranslateMatrix(mTranslateCameraMatrix, g_camera_pos[0], g_camera_pos[1], g_camera_pos[2]);

	//update the camera rotation for vehicle if needed:
	//update the camera for vehicle if needed
	if(g_keyboard_state.state == KEYBOARD_MODE_TRUCK && g_camera_state.playerCameraMode == PLAYERCAMERA_VEHICLE)
	{
		UpdateCharacterCameraForVehicle(&g_a_man, mRotateCameraMatrix);
	}
	else
	{
		mmRotateAboutY(mRotateCameraMat_Y, g_camera_rotY);
		mmRotateAboutX(mRotateCameraMat_X, g_camera_rotX);
		mmMultiplyMatrix4x4(mRotateCameraMat_X, mRotateCameraMat_Y, mRotateCameraMatrix);
	}
	
	//We want to translate and then rotate the world by the opposite of the camera's position
	//and rotation. So we want to translate and then rotate the world. Thus we
	//need to multiply the matrices in the opposite sequence: rotate then translate.
	mmMultiplyMatrix4x4(mRotateCameraMatrix, mTranslateCameraMatrix, mCameraMatrix);
	mmMakeIdentityMatrix(mModelMatrix);
	mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);
	
	//draw the wave plane
	glUseProgram(g_simple_wave_shader.program);
		glUniform3fv(g_simple_wave_shader.lightDirUnif, 1, lightDir);
		glUniform1fv(g_simple_wave_shader.shininessFactorUnif, 1, &shininess);
		glUniformMatrix4fv(g_simple_wave_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
		
		//This will only work as long as the model transform matrix is the identity matrix
		glUniform3fv(g_simple_wave_shader.modelSpaceCameraPosUnif, 1, g_ws_camera_pos);
		
		glBindVertexArray(g_simple_wave.vao);
		glDrawArrays(GL_TRIANGLES, 0, g_simple_wave.num_verts);
	
	//setup textures
	glActiveTexture(GL_TEXTURE0 + g_big_terrain.colorTexUnit); //this active texture unit is used for all subequent draw calls
	glBindTexture(GL_TEXTURE_2D, g_big_terrain.textureId);
	glBindSampler(g_big_terrain.colorTexUnit, g_big_terrain.sampler);
	
	//Need to put in camera pos
	//vCameraPos[0] = -1.0f*g_camera_pos[0];
	//vCameraPos[1] = -1.0f*g_camera_pos[1];
	//vCameraPos[2] = -1.0f*g_camera_pos[2];
	//vCameraPos[3] = 1.0f;
	//memcpy(iModelMatrix, mModelMatrix, (sizeof(float)*16));
	//mmInverse4x4(iModelMatrix);
	//mmTransformVec(iModelMatrix, vCameraPos);
	//glUniform3fv(g_modelSpaceCameraPosUnif, 1, vCameraPos);
			
	//and draw the terrain
	glUseProgram(g_theProgram);
		glUniform3fv(g_lightDirUnif, 1, lightDir);
		glUniformMatrix4fv(g_modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
		for(i = 0; i < 	g_big_terrain.num_tiles; i++)
		{
			//check if tile is too far away to draw
			r = IsTileInTerrainDrawBox(&(g_big_terrain.pTiles[i]));
			if(r == 0)
				continue;

			//check to see that tile is in camera frustum
			r = IsTileInCameraFrustum(&g_big_terrain, &(g_big_terrain.pTiles[i]));
			if(i == local_tile) //override if this is the local tile.
				r = 1;
			if(r == 0)
				continue;
			
			//are we in a tile around the camera?
			
			//draw the terrain tile	
			glBindVertexArray(g_big_terrain.pTiles[i].vao);
			glDrawElements(GL_TRIANGLES, 			//mode
					g_big_terrain.num_indices,	//count
					GL_UNSIGNED_SHORT, 		//type
					0);				//0 since VAO has IBO stored
		}

	//Loop only through local moveables grid tiles since there are too many grid tiles
	//on the map to go through
	//mmMakeIdentityMatrix(mModelMatrix);
	glBindSampler(0, g_bush_branchtex_sampler); //use branch sampler because it clamps to edge
	glUseProgram(g_bush_shader.program);
	for(i = 0; i < g_moveables_grid.numLocalTiles; i++)
	{
		//if entry is marked invalid skip it
		if(g_moveables_grid.local_grid[i] == -1)
			continue;

		k = g_moveables_grid.local_grid[i];

		//if there are no things to draw in the tiles skip it
		if(g_moveables_grid.p_tiles[k].num_moveables == 0)
			continue;

		//pmoveable is a linked list
		pmoveable = g_moveables_grid.p_tiles[k].moveables_list;
		for(j = 0; j < g_moveables_grid.p_tiles[k].num_moveables; j++) //pmoveable is a linked list
		{
			mmRotateAboutY(mModelMatrix, pmoveable->yrot); 
			mModelMatrix[12] = pmoveable->pos[0];
			mModelMatrix[13] = pmoveable->pos[1];
			mModelMatrix[14] = pmoveable->pos[2];
			mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);
			glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);

			switch(pmoveable->moveable_type)
			{
			case MOVEABLE_TYPE_CRATE:	//crate
				glBindTexture(GL_TEXTURE_2D, g_crate_model_common.texture_id);
				glBindVertexArray(g_crate_model_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_crate_model_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case MOVEABLE_TYPE_BARREL: //barrel
				glBindTexture(GL_TEXTURE_2D, g_barrel_model_common.texture_id);
				glBindVertexArray(g_barrel_model_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_barrel_model_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case MOVEABLE_TYPE_DOCK:
				glBindTexture(GL_TEXTURE_2D, g_dock_common.texture_id);
				glBindVertexArray(g_dock_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_dock_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case MOVEABLE_TYPE_BUNKER:
				glBindTexture(GL_TEXTURE_2D, g_bunker_model_common.texture_id);
				glBindVertexArray(g_bunker_model_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_bunker_model_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case MOVEABLE_TYPE_WAREHOUSE:
				glBindSampler(0, g_bush_trunktex_sampler); //bind the trunk sampler because we need repeat for the siding texture
				glBindTexture(GL_TEXTURE_2D, g_warehouse_model_common.texture_id);
				glBindVertexArray(g_warehouse_model_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_warehouse_model_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				glBindSampler(0, g_bush_branchtex_sampler); //restore the sampler that the other objects use.
				break;
			default:
				printf("%s: error. %d is unknown moveable_type.\n", __func__, pmoveable->moveable_type);
			}

			pmoveable = pmoveable->pNext; //advance the LINKED LIST
		}
	}
		
	//draw simple bush billboards, skip tiles that may need to have detailed bushes drawn
	mmMakeIdentityMatrix(mModelMatrix);
	glUseProgram(g_billboard_shader.program);
		glBindSampler(g_billboard_shader.colorTexUnit, g_bush_branchtex_sampler);
		glBindVertexArray(g_bush_smallbillboard.vao);
		for(i = 0; i < g_bush_grid.num_tiles; i++)
		{
			if(g_bush_grid.p_tiles[i].num_plants == 0)
				continue;
			
			//check to see if the tile is in the camera frustum (use the terrain tile to do this)
			r = IsTileInCameraFrustum(&g_big_terrain, &(g_big_terrain.pTiles[i]));
			if(i == local_tile)
				r = 1;
			if(r == 0)
				continue;

			//check to see if the tile is too far away and outside of a large box
			r = IsTileInPlantViewBox(&(g_big_terrain.pTiles[i]));
			if(r == 0)
				continue;
			
			//check to see if the tile is in the list of tiles that need to be detailed.
			//if so skip drawing that tile
			is_detail_bush_tile = 0;
			for(k = 0; k < 9; k++)
			{
				if(i == g_bush_grid.draw_grid[k])
				{
					is_detail_bush_tile = 1;
				}
			}
			
			//only draw bushes that are not in the bush grid's draw grid
			if(is_detail_bush_tile == 0)
			{
					//draw all bushes in the tile as billboards
					for(j = 0; j < g_bush_grid.p_tiles[i].num_plants; j++)
					{
						mModelMatrix[12] = g_bush_grid.p_tiles[i].plants[j].pos[0]; //xpos
						mModelMatrix[13] = g_bush_grid.p_tiles[i].plants[j].pos[1]; //ypos
						mModelMatrix[14] = g_bush_grid.p_tiles[i].plants[j].pos[2]; //zpos
						//mmMultiplyMatrix4x4(mCameraMatrix, (g_bush_grid.p_tiles[i].m16_model_mat + (j*16)), mModelToCameraMatrix);
						mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);
						glUniformMatrix4fv(g_billboard_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
						
						//check plant type, use it to select billboard size uniforms and texture id.
						iplant_type = (int)g_bush_grid.p_tiles[i].plants[j].plant_type;
						if(iplant_type > 5)
						{
							printf("DrawScene: error. unknown plant_type=%d for simplebillboard tile=%d plant index=%d\n", g_bush_grid.p_tiles[i].plants[j].plant_type, i, j);
							continue;
						}

						//only change if this plant type is different than the last one
						if(ilastplant_type != iplant_type)
						{
							glUniform2fv(g_billboard_shader.billboardSizeUnif, 1, (g_bush_smallbillboard.size+(iplant_type*2)));
							glBindTexture(GL_TEXTURE_2D, g_bush_smallbillboard.texture_ids[iplant_type]);
						}
					
						glDrawArrays(GL_TRIANGLES,				//mode
							0,									//starting index. start at 0.
							g_bush_smallbillboard.num_verts);	//count of vertices to draw
						ilastplant_type = iplant_type;
						g_debug_num_simple_billboard_draws += 1;
					}
			}
			
		}
	
	//setup the more detailed shader
	glUseProgram(g_bush_shader.program);
	glUniform3fv(g_lightDirUnif, 1, lightDir);
	glUseProgram(0);
	
	//Now handle the detailed bush tiles
	for(k = 0; k < 9; k++)
	{
		if(g_bush_grid.draw_grid[k] == -1)
			continue;
		
		for(j = 0; j < g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].num_plants; j++)
		{
			//get the plant's world-space coordinate out of its transform matrix
			//v3 = g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].m16_model_mat + (j*16) + 12;
			//v3 = (g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].plant_pos_yrot_list+(j*4));
			v3 = g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].plants[j].pos;

			iplant_type = (int)g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].plants[j].plant_type;
			
			//calculate the model-to-camera matrix for the bush. This doesn't matter if it's detailed
			//or not
			//mmMultiplyMatrix4x4(mCameraMatrix, (g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].m16_model_mat + (j*16)), mModelToCameraMatrix);
			mmRotateAboutY(mModelMatrix, g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].plants[j].yrot);
			mModelMatrix[12] = v3[0];
			mModelMatrix[13] = v3[1];
			mModelMatrix[14] = v3[2];
			mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);

			//determine if this bush is close to a box around the camera
			if(v3[0] > g_bush_grid.detail_boundaries[0]
				&& v3[0] < g_bush_grid.detail_boundaries[1]
				&& v3[2] > g_bush_grid.detail_boundaries[2]
				&& v3[2] < g_bush_grid.detail_boundaries[3])
			{
				//draw it detailed quad billboard
				glUseProgram(g_bush_shader.program);
				glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
				switch(iplant_type)
				{
				case 0: //1st bush
					glBindTexture(GL_TEXTURE_2D, g_bush_billboard.texture_id);
					glBindSampler(g_bush_shader.colorTexUnit, g_bush_branchtex_sampler);
					glBindVertexArray(g_bush_billboard.vao);
					glDrawElements(GL_TRIANGLES,		//mode
						g_bush_billboard.num_indices,	//number of indices to be rendered
						GL_UNSIGNED_INT,		//type of value in indices.
						0);				//pointer to location where indices are. (VAO state has EBO)
					break;
				case 1: //palm_2
					glBindTexture(GL_TEXTURE_2D, g_palm_trunk.texture_id);
					glBindSampler(0, g_bush_trunktex_sampler);
					glBindVertexArray(g_palm_trunk.vao);
					glDrawElements(GL_TRIANGLES,		//mode
						g_palm_trunk.num_indices,	//number of indices to render.
						GL_UNSIGNED_INT,			//type of indices.
						0);							//pointer to location where indices are (VAO state has EBO)
					glDisable(GL_CULL_FACE);			//draw both face sides, blender will only export one triangle.
					glBindTexture(GL_TEXTURE_2D, g_palm_fronds.texture_id);
					glBindSampler(0, g_bush_branchtex_sampler);
					glBindVertexArray(g_palm_fronds.vao);
					glDrawElements(GL_TRIANGLES,		//mode
						g_palm_fronds.num_indices,	//number of indices to render.
						GL_UNSIGNED_INT,			//type of indices.
						0);							//pointer to location where indices are (VAO state has EBO)
					glEnable(GL_CULL_FACE);
					break;
				case 2: //scaevola
					glDisable(GL_CULL_FACE);
					glBindTexture(GL_TEXTURE_2D, g_scaevola_shrub.texture_id);
					glBindSampler(0, g_bush_branchtex_sampler);
					glBindVertexArray(g_scaevola_shrub.vao);
					glDrawElements(GL_TRIANGLES,			//mode
						g_scaevola_shrub.num_indices,	//number of indices to render
						GL_UNSIGNED_INT,				//type of indices
						0);								//pointer to location where indices are (VAO state has EBO)
					glEnable(GL_CULL_FACE);
					break;
				case 3: //fake pemphis
					glDisable(GL_CULL_FACE);
					glBindTexture(GL_TEXTURE_2D, g_pemphis_shrub.texture_id);
					glBindSampler(0, g_bush_branchtex_sampler);
					glBindVertexArray(g_pemphis_shrub.vao);
					glDrawElements(GL_TRIANGLES,			//mode
						g_pemphis_shrub.num_indices,	//number of indices to render
						GL_UNSIGNED_INT,				//type of indices
						0);								//pointer to location where indices are (VAO state has EBO)
					glEnable(GL_CULL_FACE);
					break;
				case 4: //tourne fortia
					glBindTexture(GL_TEXTURE_2D, g_tournefortia_trunk.texture_id);
					glBindSampler(0, g_bush_trunktex_sampler);
					glBindVertexArray(g_tournefortia_trunk.vao);
					glDrawElements(GL_TRIANGLES,				//mode
						g_tournefortia_trunk.num_indices,	//number of indices to render
						GL_UNSIGNED_INT,					//type of indices
						0);									//pointer to location where indices are (VAO state has EBO)
					glDisable(GL_CULL_FACE);
					glBindTexture(GL_TEXTURE_2D, g_tournefortia_shrub.texture_id);
					glBindSampler(0, g_bush_branchtex_sampler);
					glBindVertexArray(g_tournefortia_shrub.vao);
					glDrawElements(GL_TRIANGLES,				//mode
						g_tournefortia_shrub.num_indices,	//number of indices to render
						GL_UNSIGNED_INT,					//type of indices
						0);									//pointer to location where indices are (VAO state has EBO)
					glEnable(GL_CULL_FACE);
					break;
				case 5: //ironwood
					glBindTexture(GL_TEXTURE_2D, g_ironwood_trunk.texture_id);
					glBindSampler(0, g_bush_trunktex_sampler);
					glBindVertexArray(g_ironwood_trunk.vao);
					glDrawElements(GL_TRIANGLES,				//mode
						g_ironwood_trunk.num_indices,		//number of indices to render
						GL_UNSIGNED_INT,					//type of indices
						0);									//pointer to location where indices are (VAO state has EBO)
					glDisable(GL_CULL_FACE);
					glBindTexture(GL_TEXTURE_2D, g_ironwood_branches.texture_id);
					glBindSampler(0, g_bush_branchtex_sampler);
					glBindVertexArray(g_ironwood_branches.vao);
					glDrawElements(GL_TRIANGLES,				//mode
						g_ironwood_branches.num_indices,	//number of indices to render
						GL_UNSIGNED_INT,					//type of indices
						0);									//pointer to location where indices are (VAO state has EBO)
					glEnable(GL_CULL_FACE);
					break;
				default:
					printf("DrawScene: unknown plant type %d. tile index=%d plant index=%d\n", (int)g_bush_grid.p_tiles[(g_bush_grid.draw_grid[k])].plants[j].plant_type, g_bush_grid.draw_grid[k], j);
					break;
				}
				g_debug_num_himodel_plant_draws += 1;
			}
			else if(v3[0] > g_bush_grid.nodraw_boundaries[(iplant_type*4)]
				&& v3[0] < g_bush_grid.nodraw_boundaries[(iplant_type*4)+1]
				&& v3[2] > g_bush_grid.nodraw_boundaries[(iplant_type*4)+2]
				&& v3[2] < g_bush_grid.nodraw_boundaries[(iplant_type*4)+3])
			{
				//draw it as a camera-facing billboard
				glUseProgram(g_billboard_shader.program);
					
					//check plant type. change billboard size uniforms and texture id based on this plant type.
					if(iplant_type > 5)
					{
						printf("DrawScene: error. unknown plant type=%d for close-in simplebillboard tile=%d planti=%d\n", iplant_type, g_bush_grid.draw_grid[k], j);
					}	
					glUniform2fv(g_billboard_shader.billboardSizeUnif, 1, (g_bush_smallbillboard.size+(iplant_type*2)));
					glBindTexture(GL_TEXTURE_2D, g_bush_smallbillboard.texture_ids[iplant_type]);
					glBindSampler(g_billboard_shader.colorTexUnit, g_bush_branchtex_sampler);
					
					glUniformMatrix4fv(g_billboard_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
					glBindVertexArray(g_bush_smallbillboard.vao);
					glDrawArrays(GL_TRIANGLES,			//mode
						0,					//starting index. start at 0.
						g_bush_smallbillboard.num_verts);	//count of vertices to draw
				g_debug_num_detail_billboard_draws += 1;
			}
			//and if it is not in the boundary don't draw it
		}	
	}

	//Loop through the tiles marked for detail and draw any items on the ground.
	glBindSampler(0, g_bush_branchtex_sampler);
	glUseProgram(g_bush_shader.program);
	for(k = 0; k < 9; k++)
	{
		if(g_bush_grid.draw_grid[k] == -1)
			continue;

		//get the head of the linked list. This may be 0 if there are no items on the tile.
		pitem = g_bush_grid.p_tiles[g_bush_grid.draw_grid[k]].items_list;

		while(pitem != 0)
		{
			mmTranslateMatrix(mTranslateModelMatrix, pitem->pos[0], pitem->pos[1], pitem->pos[2]);
			mmMultiplyMatrix4x4(mCameraMatrix, mTranslateModelMatrix, mModelToCameraMatrix);
			glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
			switch(pitem->type)
			{
			case ITEM_TYPE_BEANS:
				glBindTexture(GL_TEXTURE_2D, g_beans_common.texture_id);
				glBindVertexArray(g_beans_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_beans_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case ITEM_TYPE_CANTEEN:
				glBindTexture(GL_TEXTURE_2D, g_canteen_common.texture_id);
				glBindVertexArray(g_canteen_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_canteen_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case ITEM_TYPE_RIFLE:
				glBindTexture(GL_TEXTURE_2D, g_rifle_common.texture_id);
				glBindVertexArray(g_rifle_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_rifle_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			case ITEM_TYPE_PISTOL:
				glBindTexture(GL_TEXTURE_2D, g_pistol_common.texture_id);
				glBindVertexArray(g_pistol_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_pistol_common.num_indices,
						GL_UNSIGNED_INT,
						0);
				break;
			default:
				printf("%s: error. '%d' unknown item type.\n", __func__, (int)pitem->type);
			}

			pitem = pitem->pNext;
		}
	}
	
	//Draw vehicle b:
	VehicleConvertDisplacementMat3To4(g_b_vehicle.orientation, mRotateModelMatrix);
	mmTranslateMatrix(mTranslateModelMatrix, g_b_vehicle.cg[0], g_b_vehicle.cg[1], g_b_vehicle.cg[2]);
	mmMultiplyMatrix4x4(mTranslateModelMatrix, mRotateModelMatrix, mBaseModelMatrix);
	mmMultiplyMatrix4x4(mCameraMatrix, mBaseModelMatrix, mModelToCameraMatrix);
	glBindTexture(GL_TEXTURE_2D, g_wheel_common.texture_id); //wheel texture is just a flat blue square, use it here for car as well.
	glBindSampler(g_big_terrain.colorTexUnit, g_big_terrain.sampler); //use the terrain's sampler for the car.
	glUseProgram(g_theProgram);
	glUniformMatrix4fv(g_modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
	glBindVertexArray(g_vehicle_common.vao);
	glDrawElements(GL_TRIANGLES,
				g_vehicle_common.num_indices,
				GL_UNSIGNED_INT,
				0);
	//draw wheels
	for(i = 0; i < 4; i++) //Assume mBaseModelMatrix contains vehicle local-to-world transform
	{
		mmRotateAboutXRad(mRotateModelMatrix, g_b_vehicle.wheelOrientation_X); //TODO: fix the name 'mRotateModelMatrix_Y' since the name should end with _X based on the rotation
		mmTranslateMatrix(mTranslateModelMatrix, g_b_vehicle.wheels[i].connectPos[0], (g_b_vehicle.wheels[i].connectPos[1]-g_b_vehicle.wheels[i].x+0.5f), g_b_vehicle.wheels[i].connectPos[2]); //adjust y pos pased on wheel displacement x

		//add yaw for front wheels
		if(i == 0 || i == 1) //front wheels
		{
			mmRotateAboutYRad(mRotateModelMatrix_Y, g_b_vehicle.steeringAngle);
			mmMultiplyMatrix4x4(mRotateModelMatrix_Y, mRotateModelMatrix, mRotateModelMatrix);
		}

		//start on the right-side: R = (Car_Mat) * (wheel_pos_mat) * (wheel_rot_mat)
		mmMultiplyMatrix4x4(mTranslateModelMatrix, mRotateModelMatrix, mModelMatrix);
		mmMultiplyMatrix4x4(mBaseModelMatrix, mModelMatrix, mModelMatrix);

		mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);
		//glUseProgram(g_theProgram) //use the car shader
		glUniformMatrix4fv(g_modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
		glBindVertexArray(g_wheel_common.vao);
		glDrawElements(GL_TRIANGLES,
				g_wheel_common.num_indices,
				GL_UNSIGNED_INT,
				0);
	}
	
	//Draw the triangle men:
	for(i = 0; i < g_soldier_list.num_soldiers; i++)
	{
		//don't draw the local player when in the player keyboard state
		if(g_soldier_list.ptrsToCharacters[i] == &g_a_man 
				&& g_keyboard_state.state == KEYBOARD_MODE_PLAYER 
				&& g_camera_state.playerCameraMode == PLAYERCAMERA_FP)
			continue;
	
		//draw everybody else	
		psoldier = g_soldier_list.ptrsToCharacters[i];
		UpdateCharacterBoneModel(psoldier); //updates the vbo
		mmTranslateMatrix(mChildModelMatrix, psoldier->pos[0], (psoldier->pos[1]+psoldier->biasY), psoldier->pos[2]);
		
		//check if character is in a car, if so adjust transform so that they are attached to car
		if(psoldier->flags & CHARACTER_FLAGS_IN_VEHICLE)
		{
			mmMultiplyMatrix4x4(mBaseModelMatrix, mChildModelMatrix, mModelMatrix); //TODO: Fix this. This assumes whatever was last vehicle to set mBaseModelMatrix is the car the man is in.
		}
		else
		{
			mmRotateAboutY(mRotateModelMatrix_Y, psoldier->rotY);
			mmMultiplyMatrix4x4(mChildModelMatrix, mRotateModelMatrix_Y, mModelMatrix);
		}

		mmMultiplyMatrix4x4(mCameraMatrix, mModelMatrix, mModelToCameraMatrix);
		
		glUseProgram(g_character_shader.program);
			glUniform3fv(g_character_shader.modelSpaceCameraPosUnif, 1, g_ws_camera_pos);
			glUniformMatrix4fv(g_character_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
			glBindSampler(g_character_shader.colorTexUnit, g_triangle_man.sampler);
			
			//Loop through each material in the character model.
			glBindVertexArray(g_triangle_man.vao); //select the same double-buffered VBO as the update call.
			for(j = 0; j < g_triangle_man.num_materials; j++)
			{
				glBindTexture(GL_TEXTURE_2D, g_triangle_man.textureIds[j]);
				glDrawElementsBaseVertex(GL_TRIANGLES,	//mode.
					g_triangle_man.num_indices[j],		//count. number of elements to be rendered
					GL_UNSIGNED_INT,					//type of indices.
					0,									//address of indices. set to 0 to use EBO bound in VAO.
					g_triangle_man.baseVertices[j]);	//basevertex. constant to add to each index. 
			}

		//try to draw rifle. If CHARACTER_FLAGS_ARM_FILE is set then the player has a rifle out
		//TODO: comment out this since it is the old way of drawing items
		if(psoldier->flags & CHARACTER_FLAGS_ARM_RIFLE)
		{
			//Assume that the character's transform is in mModelMatrix
			glBindTexture(GL_TEXTURE_2D, g_rifle_common.texture_id);
			glBindSampler(0, g_bush_branchtex_sampler);
			CharacterCalculateHandTransform(psoldier, &g_rifle_common, mTranslateModelMatrix); //borrow mTranslateModelMatrix since it is available but its name doesn't really match its use here.
			//mmTranslateMatrix(mChildModelMatrix, 12319.0f, 25.0f, 11279.0f);
			//Assume: mModelMatrix contains the character transform.
			//mmMakeIdentityMatrix(mTranslateModelMatrix);
			mmMultiplyMatrix4x4(mModelMatrix, mTranslateModelMatrix, mChildModelMatrix);
			mmMultiplyMatrix4x4(mCameraMatrix, mChildModelMatrix, mModelToCameraMatrix);
			glUseProgram(g_bush_shader.program);
				glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
				glBindVertexArray(g_rifle_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_rifle_common.num_indices,
						GL_UNSIGNED_INT,
						0);
		}
		if(psoldier->flags & CHARACTER_FLAGS_ARM_PISTOL)
		{
			glBindTexture(GL_TEXTURE_2D, g_pistol_common.texture_id);
			glBindSampler(0, g_bush_branchtex_sampler);
			CharacterCalculateHandTransform(psoldier, &g_pistol_common, mTranslateModelMatrix); //borrow mTranslateModelMatrix since it is available but its name doesn't match its use here.
			mmMultiplyMatrix4x4(mModelMatrix, mTranslateModelMatrix, mChildModelMatrix);
			mmMultiplyMatrix4x4(mCameraMatrix, mChildModelMatrix, mModelToCameraMatrix);
			glUseProgram(g_bush_shader.program);
				glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelToCameraMatrix);
				glBindVertexArray(g_pistol_common.vao);
				glDrawElements(GL_TRIANGLES,
						g_pistol_common.num_indices,
						GL_UNSIGNED_INT,
						0);
		}
		
	}
	glBindVertexArray(0);
	glUseProgram(0);
}

void CalculatePerspectiveMatrix(unsigned int width, unsigned int height)
{
	float y_scale;
	float x_scale;
	float fzNear;
	float fzFar;
	
	//fzNear = 1.0f;
	fzNear = g_near_distance;
	fzFar = 10000.0f;

	//save the near and far planes for later
	g_camera_frustum.fzNear = fzNear;
	g_camera_frustum.fzFar = fzFar;

	//calculate the projection matrix
	memset(g_perspectiveMatrix, 0, sizeof(float)*16);
	y_scale = 1.0f/(tanf((PI/180.0f)*(45.0f/2.0f)));
	x_scale = y_scale / ((float)width/(float)height);
	g_perspectiveMatrix[0] = x_scale;
	g_perspectiveMatrix[5] = y_scale;
	g_perspectiveMatrix[10] = (fzFar + fzNear) / (fzNear - fzFar);
	g_perspectiveMatrix[14] = (2 * fzFar * fzNear) / (fzNear - fzFar);
	g_perspectiveMatrix[11] = -1.0f;
	//printf("x_scale:%f y_scale:%f\n", x_scale, y_scale);
}

int InitTerrain(void)
{
	struct DEM_info_struct demInfo;
	FILE * pFile;
	//char filename[255] = "./resources/maps/wake_island_1_3sec.asc";
	//char filename[255] = "./resources/maps/dem5.asc";
	//char filename[255] = "./resources/maps/dem6.asc"; //last good map
	char filename[255] = "./resources/maps/dem7.asc"; //test map with noise
	float min, max;
	float f;
	float v1[3]; //for normal calculation
	float v2[3]; //for normal calculation
	float sum[3]; //for normal calculation
	float temp_normal[3]; //for normal calculation
	int f_count;
	int f_col_count;
	int i,j;
	int k,l;
	int r;
	int tile_i;
	int vert_i;
	int lastrow_i;
	int lastcol_i;
	int t; //overall iterator for verts
	int found_eof=0;
	int num_floats_per_vert; //make this a local variable so we don't have to keep referencing it all over the place.
	
	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("InitTerrain: error. could not open %s\n", filename);
		return 0;
	}
	printf("opened dem file.\n");
	demInfo.pFile = pFile;
	DEMLoadInfo(&demInfo);
	DEMGetMinMaxElevation(&demInfo, &min, &max);
	
	//allocate a map of lvl 1 tiles to cover the DEM file: 38 rows, 38 columns
	g_big_terrain.num_tiles = 1521; //39*39
	g_big_terrain.num_cols = 39;
	g_big_terrain.num_rows = 39;
	num_floats_per_vert = 8; //{pos[3]; normal[3]; texcoord[2]}
	g_big_terrain.num_floats_per_vert = num_floats_per_vert; //pos only
	g_big_terrain.nodrawDist = 10000.0f;
	g_big_terrain.tile_len[0] = 990.0f; //size of a tile in x dir (TODO: Don't make this value hard-coded) (1.0 => 1 meter)
	g_big_terrain.tile_len[1] = 990.0f; //size of a tile in z dir
	g_big_terrain.tile_num_quads[0] = 99;
	g_big_terrain.tile_num_quads[1] = 99;
	g_big_terrain.pTiles = (struct lvl_1_tile*)malloc(1521*sizeof(struct lvl_1_tile)); //1521 = 39x39 tiles
	if(g_big_terrain.pTiles == 0)
	{
		printf("InitTerrain: malloc failed for g_big_terrain.pTiles\n");
		fclose(pFile);
		return 0;
	}
	
	//allocate memory for each tile
	k = 0; //row
	l = 0; //column
	for(i = 0; i < g_big_terrain.num_tiles; i++)
	{
		g_big_terrain.pTiles[i].num_z = 100; //number of vertices along one tile's x edge
		g_big_terrain.pTiles[i].num_x = 100; //number of vertices along one tile's z edge
		g_big_terrain.pTiles[i].num_verts = 10000;
		g_big_terrain.pTiles[i].pPos = (float*)malloc(10000*g_big_terrain.num_floats_per_vert*sizeof(float));
		if(g_big_terrain.pTiles[i].pPos == 0)
		{
			printf("InitTerrain: malloc failed for tile %d\n", i);
			fclose(pFile);
			return 0;
		}
		
		/*
		On a side, for level 1 tiles n, the number of verts, is 100, so
		the length of the side is n-1, so 990.0f if the distance between
		each vertex is 10.0f.
		distance between each vertex is 10.0f so each side is 990.0f
		*/
		g_big_terrain.pTiles[i].urcorner[0] = l*990.0f; //x
		g_big_terrain.pTiles[i].urcorner[1] = k*990.0f; //z
		l += 1;
		if(l == g_big_terrain.num_cols)
		{
			k += 1; //increment the row
			l = 0; //reset the column index
		}
	}
	printf("initialized %d tiles. k=%d l=%d\n", i, k, l);
	
	//set the file stream to the start of the elevation data
	r = fseek(demInfo.pFile, demInfo.data_start_pos, SEEK_SET);
	if(r == -1)
	{
		printf("InitTerrain: fseek() failed.\n");
		fclose(pFile);
		return 0;
	}
	
	/*
	the idea here is to loop overall all verts in the entire
	tile map, but keep smaller iterators that actually are used
	to index the tile arrays.
	*/
	i = 0; //row of vert
	j = 0; //column of vert
	k = 0; //row of tile
	l = 0; //column of tile
	f_count = 0;
	f_col_count = 0;
	for(t = 0; t < (g_big_terrain.num_tiles*g_big_terrain.pTiles[0].num_x*g_big_terrain.pTiles[0].num_z); t++) //iterate for 1521 tiles * 10,000 verts per tile.
	{
		tile_i = (k*g_big_terrain.num_cols) + l;
		vert_i = ((i*g_big_terrain.pTiles[tile_i].num_x) + j)*(g_big_terrain.num_floats_per_vert); 
		
		//special case: first vert in the whole map...just fetch a data point
		if(k == 0 && l == 0 && i == 0 && j == 0)
		{
			r = feof(demInfo.pFile);
			if(r != 0) //we hit the end of the file
			{
				printf("InitTerrain: found end of file at t=%d k=%d l=%d i=%d j=%d\n", t, k, l, i, j);
				fclose(demInfo.pFile);
				return 0;
			}
			
			fscanf(demInfo.pFile, "%f", &f);
			f_count += 1;
			f_col_count += 1;
			g_big_terrain.pTiles[0].pPos[1] = f;
		}
		else if(k != 0 && //not a tile in the first row
			i == 0) //vertex is in the first row
		{
			//get the elevation from the tile in the row above
			lastrow_i = ((99*100) + j)*(g_big_terrain.num_floats_per_vert);
			g_big_terrain.pTiles[tile_i].pPos[(vert_i+1)] = g_big_terrain.pTiles[(tile_i-g_big_terrain.num_cols)].pPos[(lastrow_i+1)];
		}
		else if(l != 0 && //not a tile in the first column
			j == 0) //vertex is in the first column
		{
			//get the elevation from the previous tile in the row
			lastcol_i = ((i*100) + 99)*g_big_terrain.num_floats_per_vert;
			g_big_terrain.pTiles[tile_i].pPos[(vert_i+1)] = g_big_terrain.pTiles[(tile_i-1)].pPos[(lastcol_i+1)];
		}
		else if(f_count > demInfo.num_total) //we are past the point of where the DEM file has data.
		{
			g_big_terrain.pTiles[tile_i].pPos[(vert_i+1)] = min; //use the minimum elevation of the DEM file as default.
		}
		else if(f_col_count > demInfo.num_col) //we are past the end of a row of the DEM file.
		{
			g_big_terrain.pTiles[tile_i].pPos[(vert_i+1)] = min; //use the minimum elevation of the DEM file as default.
		}
		else
		{
			r = feof(demInfo.pFile);
			if(r != 0)
			{
				if(found_eof == 0)
				{
					printf("InitTerrain: found end of file at t=%d k=%d l=%d i=%d j=%d\n", t, k, l, i, j);
					found_eof = 1;
				}
				f = min;	
			}
			else
			{
				fscanf(demInfo.pFile, "%f", &f);
			}
			f_count += 1;
			f_col_count += 1;
			
			g_big_terrain.pTiles[tile_i].pPos[(vert_i+1)] = f;
		}
		
		//set the x & z coordinates of the vertex
		g_big_terrain.pTiles[tile_i].pPos[vert_i] = j*10.0f; //10.0f is the distance between vertices in the tile.
		g_big_terrain.pTiles[tile_i].pPos[(vert_i+2)] = i*10.0f;
		g_big_terrain.pTiles[tile_i].pPos[vert_i] += g_big_terrain.pTiles[tile_i].urcorner[0];
		g_big_terrain.pTiles[tile_i].pPos[(vert_i+2)] += g_big_terrain.pTiles[tile_i].urcorner[1];
		
		//set the texture coordinate of the vertex
		g_big_terrain.pTiles[tile_i].pPos[(vert_i+6)] = j*2.0f;
		g_big_terrain.pTiles[tile_i].pPos[(vert_i+7)] = i*2.0f;
		
		/*
		update the indices. Need to make sure to move through the tile map in the same way that
		data is presented in the data file. This means filling in a row for all tiles in a tile row before
		going onto to the next vertex row.
		*/
		j += 1; //increment vertex column
		if(j == g_big_terrain.pTiles[tile_i].num_x)
		{
			l += 1; //move to the next tile
			j = 0; //reset vertex column index since we are in a new tile
			
			if(l == g_big_terrain.num_cols)
			{
				i += 1; //go to the next vertex row
				j = 0;  //reset the vertex column
				l = 0; //put l back at the first tile.
				f_col_count = 0;
				
				//check to see if we completed filling data for a row of tiles
				if(i == g_big_terrain.pTiles[tile_i].num_z)
				{
					k += 1; //goto the next tile row
					l = 0; //reset the tile column index
					i = 0; //reset the vertex indices
					j = 0;
					
					if(k == g_big_terrain.num_rows)
					{
						printf("InitTerrain: k and l == num_rows and num_cols. completed loading data. why did we get here. i=%d j=%d k=%d l=%d t=%d f_count=%d\n", i, j, k, l, t, f_count);
					}
				}
			}
		}
	}
	
	fclose(pFile);
	printf("closed dem file.\n");
	
	//first-pass at calculating vertex normals
	//loop through all tiles calculte vertex normals (ignore handling additional issues with vertices on tile edges)
	printf("calclating normals.\n");
	for(k = 0; k < g_big_terrain.num_rows; k++)
	{
		for(l = 0; l < g_big_terrain.num_cols; l++)
		{
			tile_i = (k*g_big_terrain.num_cols) + l;
			for(i = 0; i < g_big_terrain.pTiles[tile_i].num_z; i++)
			{
				for(j = 0; j < g_big_terrain.pTiles[tile_i].num_x; j++)
				{
					vert_i = ((i*g_big_terrain.pTiles[tile_i].num_x) + j)*num_floats_per_vert;
					
					//zero sum from last iteration
					sum[0] = 0.0f;
					sum[1] = 0.0f;
					sum[2] = 0.0f;
					
					//top-left vertex
					if(i == 0 && j == 0)
					{
						MakeTerrainCalcNormal(sum,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]), //origin vertex
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]), //vertex below
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]) );
							
					}
					//top-right vertex
					else if(i == 0 && j == 99)
					{
						MakeTerrainCalcNormal(sum,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]) );
					}
					//bottom-left vertex
					else if(i == 99 && j == 0)
					{
						MakeTerrainCalcNormal(sum,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]), //vertex to the right
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]) );//vertex above
					}
					//bottom-right vertex
					else if(i == 99 && j == 99)
					{
						MakeTerrainCalcNormal(sum,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]) );
					}
					//left-edge vertices
					else if(j == 0)
					{
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						//re-normalize since its no longer normal
						vNormalize(sum);
					}
					//bottom-edge vertices
					else if(i == 99)
					{
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						vNormalize(sum);
					}
					//right-edge vertices
					else if(j == 99)
					{
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						vNormalize(sum);
					}
					//top edges
					else if(i == 0)
					{
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						vNormalize(sum);
					}
					//all other interior vertices
					else
					{
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+(100*num_floats_per_vert))]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-num_floats_per_vert)]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						MakeTerrainCalcNormal(temp_normal,
							&(g_big_terrain.pTiles[tile_i].pPos[vert_i]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i+num_floats_per_vert)]),
							&(g_big_terrain.pTiles[tile_i].pPos[(vert_i-(100*num_floats_per_vert))]) );
						sum[0] += temp_normal[0];
						sum[1] += temp_normal[1];
						sum[2] += temp_normal[2];
						
						vNormalize(sum);
					}
					
					//fill in the final normal here
					g_big_terrain.pTiles[tile_i].pPos[(vert_i+3)] = sum[0];
					g_big_terrain.pTiles[tile_i].pPos[(vert_i+4)] = sum[1];
					g_big_terrain.pTiles[tile_i].pPos[(vert_i+5)] = sum[2];	
				}
			}
		}
	}
	printf("finished calculating normals.\n");
	
	//TODO: Handle Normal vectors that are on tile boundaries (currently you can easily see the tile boundary because of this)
	
	//setup indices for the enumeration buffer
	r = MakeTerrainElementArray(&(g_big_terrain.pElements), &(g_big_terrain.num_indices),100, 100);
	if(r == 0)
	{
		return 0;
	}
	
	return 1;
}

int DEMLoadInfo(struct DEM_info_struct * pDemInfo)
{
	char s[255];
	FILE * pFile;
	
	if(pDemInfo->pFile == 0)
	{
		printf("DEMLoadInfo: error. pFile is 0.\n");
		return 0;
	}
	else
	{
		pFile = pDemInfo->pFile;
	}
	
	//get ncols
	fscanf(pFile, "%s",s);
	if(strcmp(s, "ncols") != 0)
	{
		printf("DEMLoadInfo: format error. 'ncols' missing.\n");
		return 0;
	}
	fscanf(pFile, "%d", &(pDemInfo->num_col));
	
	//get nrows
	fscanf(pFile, "%s", s);
	if(strcmp(s, "nrows") != 0)
	{
		printf("DEMLoadInfo: format error. 'nrows' missing.\n");
		return 0;
	}
	fscanf(pFile, "%d", &(pDemInfo->num_row));
	
	pDemInfo->num_total = pDemInfo->num_col*pDemInfo->num_row;
	
	//get xllcenter
	fscanf(pFile, "%s", s);
	if(strcmp(s, "xllcenter") != 0)
	{
		printf("DEMLoadInfo: format error. 'xllcenter' missing.\n");
		return 0;
	}
	fscanf(pFile, "%f", &(pDemInfo->x_llcenter));
	
	//get yllcenter
	fscanf(pFile, "%s", s);
	if(strcmp(s, "yllcenter") != 0)
	{
		printf("DEMLoadInfo: format error. 'yllcenter' missing.\n");
		return 0;
	}
	fscanf(pFile, "%f", &(pDemInfo->y_llcenter));
	
	//read cellsize data
	fscanf(pFile, "%s", s);
	if(strcmp(s, "cellsize") != 0)
	{
		printf("DEMLoadInfo: format error. 'cellsize' missing.\n");
		return 0;
	}
	fscanf(pFile, "%f", &(pDemInfo->cellsize));
	
	//read nodata_value
	fscanf(pFile, "%s", s);
	if(strcmp(s, "nodata_value") != 0)
	{
		printf("DEMLoadInfo: format error. 'nodata_value' missing.\n");
		return 0;
	}
	fscanf(pFile, "%f", &(pDemInfo->nodata_value));
	
	//save the file position location where the data starts
	pDemInfo->data_start_pos = ftell(pFile);
	
	printf("ncols: %d\nnrows: %d\n", pDemInfo->num_col, pDemInfo->num_row);
	printf("xllcenter: %f\nyllcenter: %f\n", pDemInfo->x_llcenter, pDemInfo->y_llcenter);
	
	return 1;
}

int DEMGetMinMaxElevation(struct DEM_info_struct * pDemInfo, float * pMin, float * pMax)
{
	FILE * pFile;
	float f, min, max;
	int i_min, i_max; //index of the min and max in the data
	int i;
	int r;
	
	if(pDemInfo->pFile == 0)
	{
		printf("DEMGetMinMaxElevation: error. pFile is 0\n");
		return 0;
	}
	pFile = pDemInfo->pFile;
	
	//go to the start of the data
	r = fseek(pFile, pDemInfo->data_start_pos, SEEK_SET);
	if(r == -1)
	{
		printf("DEMGetMinMaxElevation: fseek failed.\n");
		return 0;
	}
	
	//get one elevation from the file to setup min max
	fscanf(pFile, "%f", &f);
	min = f;
	i_min = 0;
	max = f;
	i_max = 0;
	i = 1;
	
	//now read the remaining data and find the min and max values
	while(feof(pFile) == 0)
	{
		fscanf(pFile, "%f", &f);
		if(f < min)
		{
			i_min = i;
			min = f;
		}
		if(f > max)
		{
			i_max = i;
			max = f;
		}
		
		i += 1;
	}
	
	printf("found min %f at i=%d\nfound max %f at i=%d out of %d heights\n", min, i_min, max, i_max, i);
	
	*pMin = min;
	*pMax = max;
	return 1;
}

/*
Given a world space position, returns a tile index
-returns -1 if position is not in tile map.
*/
int GetLvl1Tile(float * pos)
{
	int i, j;
	
	//check to see if the position is outside of the tilemap:
	if(pos[0] < 0.0f 
		|| pos[2] < 0.0f 
		|| pos[0] > (g_big_terrain.num_cols*g_big_terrain.tile_len[0])
		|| pos[2] > (g_big_terrain.num_rows*g_big_terrain.tile_len[1]) )
	{
		return -1;
	}
	
	j = (int)(pos[0]/g_big_terrain.tile_len[0]);
	i = (int)(pos[2]/g_big_terrain.tile_len[1]);
	
	return (i*g_big_terrain.num_cols)+j;
}

/*
This func given a vec3 pos returns the i,j indices in the g_big_terrain's pTiles array
*/
int GetLvl1Tileij(float * pos, int * i, int * j)
{
	//check for position out of tilemap
	if(pos[0] < 0.0f 
		|| pos[2] < 0.0f 
		|| pos[0] > (g_big_terrain.num_cols*g_big_terrain.tile_len[0])
		|| pos[2] > (g_big_terrain.num_rows*g_big_terrain.tile_len[1]) )
	{
		return -1;
	}

	*j = (int)(pos[0]/g_big_terrain.tile_len[0]);
	*i = (int)(pos[2]/g_big_terrain.tile_len[1]);
	
	return ((*i)*g_big_terrain.num_cols)+(*j);
}


/*
This function given a tile address, will return the tile's row and col in 
the lvl_1_terrain_struct's pTiles array.
*/
int GetTileRowColFromIndex(struct lvl_1_tile * ptile, int * pi, int * pj)
{
	int overall_index;

	overall_index = ptile - g_big_terrain.pTiles;

	*pi = overall_index/g_big_terrain.num_cols;
	*pj = overall_index % g_big_terrain.num_cols;

	return 1;
}

/*
This function takes a pos and returns a point in the plane along with the surface normal of terrain
of the surface of the tile.
	-float * pos expects a vec3
	-surface point is returned in the address passed by surf_pos
	-the surface normal is returned in the vec3 passed in surf_norm
*/
int GetTileSurfPoint(float * pos, float * surf_pos, float * surf_norm)
{
	int tile_i;
	int quad_i;
	float quad_origin[3]; //this and the three following arrays make up the four points of a quad
	float quad_pos_z[3];
	float quad_pos_x[3];
	float quad_opposite[3];
	float local_pos[3];
	int vert_i;
	int i,j;
	struct lvl_1_tile * ptile;
	float * p_pos; //vertex data array of the tile
	float check_vec[3];
	float r_dot; //dot-product result
	float d; //constant in the plane equation
	float n[3]; //normal vector of the triangle
	
	//setup the check vector (it points in the -1,1 direction and really is a vec2)
	check_vec[0] = 0.707106781f;
	check_vec[1] = 0.0f;
	check_vec[2] = 0.707106781f;
	
	//first see if we can get a tile
	tile_i = GetLvl1Tile(pos);
	if(tile_i == -1) 
		return -1; //the point isn't in the tilemap, so return
	ptile = (g_big_terrain.pTiles+tile_i);
	p_pos = ptile->pPos;
	
	//figure out the origin vertex of the quad in the tile
	local_pos[0] = pos[0] - ptile->urcorner[0];
	local_pos[2] = pos[2] - ptile->urcorner[1]; //note: urcorner is only a vec2 but local_pos and pos are vec3
	j = (int)(local_pos[0]/10.0f); //column
	i = (int)(local_pos[2]/10.0f); //row
	quad_i = (i*ptile->num_x)+j;
	
	//check quad_i to make sure it is still in the array TODO: Remove this later
	if(quad_i >= 10000)
	{
		printf("GetTileSurfPoint: error. quad_i = %d and is out of range.", quad_i);
		return -1;
	}
	
	//get all four points of the tile
	quad_origin[0] = p_pos[(quad_i*g_big_terrain.num_floats_per_vert)];
	quad_origin[1] = p_pos[((quad_i*g_big_terrain.num_floats_per_vert)+1)];
	quad_origin[2] = p_pos[((quad_i*g_big_terrain.num_floats_per_vert)+2)];
	quad_pos_x[0] = p_pos[((quad_i+1)*g_big_terrain.num_floats_per_vert)];
	quad_pos_x[1] = p_pos[(((quad_i+1)*g_big_terrain.num_floats_per_vert)+1)];
	quad_pos_x[2] = p_pos[(((quad_i+1)*g_big_terrain.num_floats_per_vert)+2)];
	quad_pos_z[0] = p_pos[((quad_i+ptile->num_x)*g_big_terrain.num_floats_per_vert)];
	quad_pos_z[1] = p_pos[(((quad_i+ptile->num_x)*g_big_terrain.num_floats_per_vert)+1)];
	quad_pos_z[2] = p_pos[(((quad_i+ptile->num_x)*g_big_terrain.num_floats_per_vert)+2)];
	quad_opposite[0] = p_pos[((quad_i+1+ptile->num_x)*g_big_terrain.num_floats_per_vert)];
	quad_opposite[1] = p_pos[(((quad_i+1+ptile->num_x)*g_big_terrain.num_floats_per_vert)+1)];
	quad_opposite[2] = p_pos[(((quad_i+1+ptile->num_x)*g_big_terrain.num_floats_per_vert)+2)];
	
	//debug
	//printf("\tquad_origin: (%f,%f,%f)\n",quad_origin[0], quad_origin[1], quad_origin[2]);
	//printf("\tquad_pos_x: (%f,%f,%f)\n",quad_pos_x[0], quad_pos_x[1], quad_pos_x[2]);
	//printf("\tquad_pos_z: (%f,%f,%f)\n",quad_pos_z[0], quad_pos_z[1], quad_pos_z[2]);
	//printf("\tquad_opposite: (%f,%f,%f)\n",quad_opposite[0], quad_opposite[1], quad_opposite[2]);

	//calculate a relative point that will be used to determine which triangle in
	//the quad will be used
	local_pos[0] = pos[0] - quad_pos_z[0]; //recalculate local_pos using an origin from the +z corner of the quad
	local_pos[1] = 0.0f;
	local_pos[2] = pos[2] - quad_pos_z[2];
	
	//calculate the dot product and determine which triangle to use
	r_dot = vDotProduct(local_pos, check_vec);
	
	//calculate the normal for the triangle
	if(r_dot >= 0.0f) //point is in triangle adjacent to origin
	{
		vGetPlaneNormal(quad_pos_z, quad_opposite, quad_pos_x, n);
	}
	else //point is in the opposite triangle
	{
		vGetPlaneNormal(quad_pos_z, quad_pos_x, quad_origin, n);
	}
	d = 0.0f - (quad_pos_z[0]*n[0]) - (quad_pos_z[1]*n[1]) - (quad_pos_z[2]*n[2]);

	//calculate the y-coordinate and point on the surface
	surf_pos[0] = pos[0];
	surf_pos[1] = (0.0f - d - (pos[0]*n[0]) - (pos[2]*n[2]))/n[1];
	surf_pos[2] = pos[2];
	//printf("\tsurf_pos: (%f,%f,%f)\n", surf_pos[0], surf_pos[1], surf_pos[2]);
	surf_norm[0] = n[0];
	surf_norm[1] = n[1];
	surf_norm[2] = n[2];
	return 1;
}

/*
This function looks for ray intersection over the whole terrain map
*/
int RaycastTileSurf(float * pos, float * ray, float * surf_pos, float * surf_norm)
{
	struct lvl_1_tile * ptile;
	struct lvl_1_tile * pendtile;
	struct lvl_1_tile * pnextTile;
	float cur_pos[3];
	float xz_pos[2];
	int i_quad;
	int j_quad;
	int i_end_quad;
	int j_end_quad;
	int start_tile_i;
	int start_tile_j;
	int end_tile_i;
	int end_tile_j;
	int r;

	//First find the terrain tile that the origin of the ray is under
	start_tile_i = GetLvl1Tile(pos);
	if(start_tile_i == -1)
		return -1;
	
	//Get the terrain tile that the end position is under
	vAdd(cur_pos, pos, ray);
	end_tile_i = GetLvl1Tile(cur_pos);
	if(end_tile_i == -1)
		return -1;
	ptile = (g_big_terrain.pTiles+start_tile_i);
	pendtile = (g_big_terrain.pTiles+end_tile_i);
	GetTileRowColFromIndex(ptile, &start_tile_i, &start_tile_j);
	GetTileRowColFromIndex(pendtile, &end_tile_i, &end_tile_j);
	
	//determine current quad
	xz_pos[0] = pos[0] - ptile->urcorner[0];
	xz_pos[1] = pos[2] - ptile->urcorner[1];
	j_quad = (int)(xz_pos[0]/10.0f);
	i_quad = (int)(xz_pos[1]/10.0f);

	//determine end quad
	j_end_quad = (int)((cur_pos[0] - pendtile->urcorner[0])/10.0f);
	i_end_quad = (int)((cur_pos[2] - pendtile->urcorner[1])/10.0f);

	//Step through quads in different tiles until you get to the entile+quad of end_tile_i
	while(1)
	{
		r = RaycastQuadTileSurf(pos, 
				ray, 
				ptile,			//addr of tile to check 
				i_quad, 		//row index of quad to check for ray intersect
				j_quad, 		//column index of quad to check for ray intersect
				surf_pos, 		//[out], xyz of point of intersection on terrain surf
				surf_norm);		//[out], terrain surface normal at intersection point
		if(r == 1) //if we found an intersection with the ray
		{
			r = 1;
			break;
		}
		else if((j_quad == j_end_quad) && (i_quad == i_end_quad) && (end_tile_i == (ptile - g_big_terrain.pTiles)))
		{
			r = 0;
			break;
		}
		else
		{
			//Go to next quad
			pnextTile = GetNewTileQuad(pos, ray, ptile, &i_quad, &j_quad);
			if(pnextTile == 0)
			{
				r = 0;
				break;
			}
			ptile = pnextTile;
		}
	}

	return r;
}

/*
This function when given a starting ptile i,j that is assumed to have pos within it
and given a ray providing direction will replace the ptile and i,j with an adjacent tile,grid
this function is a helper function for RaycastTileSurf()
Return values:
	<ptr>	; ptile, pi, pj updated with adjacent quad
	0		; could not determine new quad
*/
static struct lvl_1_tile* GetNewTileQuad(float * pos, float * ray, struct lvl_1_tile * ptile, int * pi, int * pj)
{
	struct lvl_1_tile* pnew_tile=0;
	float quad_origin[3];
	float quad_pos_z[3];
	float quad_pos_x[3];
	float quad_opposite[3];
	float side_normals[8] = { 0.0f, -1.0f,	//+z side. normal points towards center of square
	                          -1.0f, 0.0f,	//+x side
	                          0.0f, 1.0f,	//-z side
	                          1.0f, 0.0f};	//-x side
	float local_pos[2];
	float normalized_ray[2];
	float ray_2d[2];
	float side_to_pos[2];
	float pos_in_side_plane[2];
	float r_dot;
	float t_factor;
	float dist_in_normal;
	int quad_i;
	int num_floats = g_big_terrain.num_floats_per_vert;
	int i_tile;
	int j_tile;

	quad_i = ((*pi)*ptile->num_x)+(*pj);
	quad_origin[0] = ptile->pPos[(quad_i*num_floats)];
	quad_origin[1] = ptile->pPos[(quad_i*num_floats)+1];
	quad_origin[2] = ptile->pPos[(quad_i*num_floats)+2];
	quad_pos_x[0] = ptile->pPos[((quad_i+1)*num_floats)];
	quad_pos_x[1] = ptile->pPos[((quad_i+1)*num_floats)+1];
	quad_pos_x[2] = ptile->pPos[((quad_i+1)*num_floats)+2];
	quad_pos_z[0] = ptile->pPos[((quad_i+ptile->num_x)*num_floats)];
	quad_pos_z[1] = ptile->pPos[((quad_i+ptile->num_x)*num_floats)+1];
	quad_pos_z[2] = ptile->pPos[((quad_i+ptile->num_x)*num_floats)+2];
	quad_opposite[0] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats)];
	quad_opposite[1] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats)+1];
	quad_opposite[2] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats)+2];
	local_pos[0] = pos[0] - ptile->urcorner[0];
	local_pos[1] = pos[2] - ptile->urcorner[1];
	ray_2d[0] = ray[0];
	ray_2d[1] = ray[2];
	normalized_ray[0] = ray[0];
	normalized_ray[1] = ray[2];
	vNormalize2(normalized_ray);

	//Check each side of the square. If r_dot and dist_in_normal have opposite signs then there is
	//an intersection. If they are both positive or both negative then no intersection can occur
	
	//check +z side of grid square
	side_to_pos[0] = pos[0] - quad_pos_z[0];
	side_to_pos[1] = pos[2] - quad_pos_z[2];
	r_dot = vDotProduct2(ray_2d, side_normals);
	dist_in_normal = vDotProduct2(side_to_pos, side_normals);
	t_factor = dist_in_normal/r_dot;
	pos_in_side_plane[0] = (fabs(t_factor))*ray_2d[0] + pos[0]; //just look at x coord, because side of quad square is along x-axis
	if((r_dot <= 0.0f) && (pos_in_side_plane[0] >= quad_origin[0]) && (pos_in_side_plane[0] < quad_opposite[0]))
	{
		//it is the quad in the row below
		*pi += 1;

		//check if this row index is outside of the boundaries of a tile,
		//if it is move to a ptile in the row below
		if(*pi >= (g_big_terrain.tile_num_quads[1]))	//use tile_num_quads instead of num_z because num_z gives # of vertices
		{
			//check if we are still in map boundaries
			GetTileRowColFromIndex(ptile, &i_tile, &j_tile);
			if((i_tile+1) >= g_big_terrain.num_rows) //check if past # of rows in the map
				return 0;

			//finally, go to the tile in the row below
			i_tile += 1;
			*pi = 0; //0 out the row since it is in new tile, and leave pj the same
			pnew_tile = g_big_terrain.pTiles+(i_tile*g_big_terrain.num_cols)+j_tile;
			return pnew_tile;
		}
		return ptile;
	}

	//check +x size of grid square
	side_to_pos[0] = pos[0] - quad_opposite[0];
	side_to_pos[1] = pos[2] - quad_opposite[2];
	r_dot = vDotProduct2(ray_2d, (side_normals+2));
	dist_in_normal = vDotProduct2(side_to_pos, (side_normals+2));
	t_factor = dist_in_normal/r_dot; //if there is an intersection then r_dot and dist_in_normal will have opposite signs
	pos_in_side_plane[1] = (fabs(t_factor))*ray_2d[1] + pos[2]; //just look at z coord, because side of square is along z-axis
	if((r_dot <= 0.0f) && (pos_in_side_plane[1] < quad_opposite[2]) && (pos_in_side_plane[1] >= quad_pos_x[2])) //check if ray passes through +x side
	{
		*pj += 1; //increment the column index

		//check if the column index is still within the tile
		if(*pj >= (g_big_terrain.tile_num_quads[0]))
		{
			GetTileRowColFromIndex(ptile, &i_tile, &j_tile);
			if((j_tile+1) >= g_big_terrain.num_cols) //check if tile would be past # of columns in the map
				return 0;

			//shift to new tile
			j_tile += 1;
			*pj = 0; //0 out the column since this will be in the new tile, leave pi the same
			pnew_tile = g_big_terrain.pTiles+(i_tile*g_big_terrain.num_cols)+j_tile;
			return pnew_tile;
		}
		return ptile;
	}

	//check -z side
	side_to_pos[0] = pos[0] - quad_pos_x[0];
	side_to_pos[1] = pos[2] - quad_pos_x[2];
	r_dot = vDotProduct2(ray_2d, (side_normals+4));
	dist_in_normal = vDotProduct2(side_to_pos, (side_normals+4));
	t_factor = dist_in_normal/r_dot;
	pos_in_side_plane[0] = (fabs(t_factor))*ray_2d[0] + pos[0];
	if((r_dot <= 0.0f) && (pos_in_side_plane[0] <= quad_pos_x[0]) && (pos_in_side_plane[0] > quad_origin[0]))
	{
		//change to quad in the row above
		*pi -= 1;

		//check if the new pi,pj are still in a quad in the same tile
		if(*pi < 0)
		{
			//check if we are still in map boundaries
			GetTileRowColFromIndex(ptile, &i_tile, &j_tile);
			if((i_tile-1) < 0) //is this an out of range row index
				return 0;
			*pi = (g_big_terrain.tile_num_quads[1]); //set the quad in the new tile at the bottom row of the tile
			pnew_tile = g_big_terrain.pTiles+(i_tile*g_big_terrain.num_cols)+j_tile;
			return pnew_tile;
		}
		return ptile;
	}

	//check -x side
	side_to_pos[0] = pos[0] - quad_origin[0];
	side_to_pos[1] = pos[2] - quad_origin[2];
	r_dot = vDotProduct(ray_2d, (side_normals+6));
	dist_in_normal = vDotProduct2(side_to_pos, (side_normals+6));
	t_factor = dist_in_normal/r_dot;
	pos_in_side_plane[1] = (fabs(t_factor))*ray_2d[1] + pos[2];
	if((r_dot <= 0.0f) && (pos_in_side_plane[1] >= quad_origin[2]) && (pos_in_side_plane[1] < quad_pos_z[2]))
	{
		//change the quad to the previous column
		*pj -= 1;

		//check if the new pi, pj are still in a quad in the same tile
		if(*pj < 0)
		{
			//check if we are still in map boundaries if we go to new tile
			GetTileRowColFromIndex(ptile, &i_tile, &j_tile);
			if((j_tile-1) < 0)
				return 0;
			*pj = (g_big_terrain.tile_num_quads[0]); //set the quad in the new tile to be at the last column in a row
			pnew_tile = g_big_terrain.pTiles+(i_tile*g_big_terrain.num_cols)+j_tile;
			return pnew_tile;
		}
		return ptile;
	}

	//This means that the ray/pos didn't intersect any of the sides. Return a value
	//to indicate to the caller that a quad to move to was not determined.
	return 0;
}

/*
This function looks for ray intersection over a quad within a lvl 1 tile.
This function is a helper function for RaycastTileSurf()
Returns:
	0	;not intersect found
	1	;intersect found. insert coordinates returned as vec3 to surf_pos. normal of terrain at intersect returned in vec3 surf_norm
*/
static int RaycastQuadTileSurf(float * pos, float * ray, struct lvl_1_tile * ptile, int quad_row, int quad_col, float * surf_pos, float * surf_norm)
{
	float endpos[3]; //end position of ray (pos+ray)
	float quad_origin[3];
	float quad_pos_z[3];
	float quad_pos_x[3];
	float quad_opposite[3];
	float plane_normal[3];
	float plane_d;
	float vec_to_endpos[3]; //vector from tri to endpos
	float vec_to_startpos[3]; //vector from triangle to start pos
	float vec_in_plane[3];
	float r_dot[3];
	float normal_v23[3];
	float normal_v31[3];
	float normal_v12[3];
	float normal_ray[3];
	float t_factor;
	float dist_in_planenormal;
	int quad_i;
	int num_floats_per_vert = g_big_terrain.num_floats_per_vert;
	int r=0;

	vAdd(endpos, pos, ray);

	//Get all four corners of the quad id'd by quad_i and quad_j
	quad_i = (quad_row*ptile->num_x)+quad_col;
	quad_origin[0] = ptile->pPos[(quad_i*num_floats_per_vert)];
	quad_origin[1] = ptile->pPos[(quad_i*num_floats_per_vert)+1];
	quad_origin[2] = ptile->pPos[(quad_i*num_floats_per_vert)+2];
	quad_pos_x[0] = ptile->pPos[((quad_i+1)*num_floats_per_vert)];
	quad_pos_x[1] = ptile->pPos[((quad_i+1)*num_floats_per_vert)+1];
	quad_pos_x[2] = ptile->pPos[((quad_i+1)*num_floats_per_vert)+2];
	quad_pos_z[0] = ptile->pPos[((quad_i+ptile->num_x)*num_floats_per_vert)];
	quad_pos_z[1] = ptile->pPos[((quad_i+ptile->num_x)*num_floats_per_vert)+1];
	quad_pos_z[2] = ptile->pPos[((quad_i+ptile->num_x)*num_floats_per_vert)+2];
	quad_opposite[0] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats_per_vert)];
	quad_opposite[1] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats_per_vert)+1];
	quad_opposite[2] = ptile->pPos[((quad_i+1+ptile->num_x)*num_floats_per_vert)+2];

	//quad consists of two triangles:
	//1. quad_opposite, quad_pos_x, quad_pos_z
	//2. quad_origin, quad_pos_z, quad_pos_x

	//Check triangle 1. quad_opposite, quad_pos_x, quad_pos_z
	//First see if the end point of the ray is in front or behind the plane

	vGetPlaneNormal(quad_pos_z, quad_opposite, quad_pos_x, plane_normal);
	plane_d = 0.0f - (quad_pos_z[0]*plane_normal[0]) - (quad_pos_z[1]*plane_normal[1]) - (quad_pos_z[2]*plane_normal[2]);
	vSubtract(vec_to_endpos, endpos, quad_pos_z);

	//take the dot product of the triangle's normal and a vector the end position of the ray
	//if the dot product is positive then the ray never breaks the plane of the triangle, if it
	//is negative then it has broken the triangle's plane and now we check to see if the ray
	//is within the triangle's boundaries. see triangle_ray_intersection_diag.png
	r_dot[0] = vDotProduct(vec_to_endpos, plane_normal);
	if(r_dot[0] <= 0.0f)
	{
		vSubtract(vec_to_startpos, pos, quad_pos_x);
		vSubtract(vec_in_plane, quad_pos_z, quad_pos_x);
		vCrossProduct(normal_v12, vec_to_startpos, vec_in_plane);
		r_dot[0] = vDotProduct(normal_v12, ray);
		
		vSubtract(vec_to_startpos, pos, quad_pos_z);
		vSubtract(vec_in_plane, quad_opposite, quad_pos_z);
		vCrossProduct(normal_v23, vec_to_startpos, vec_in_plane);
		r_dot[1] = vDotProduct(normal_v23, ray);

		vSubtract(vec_to_startpos, pos, quad_opposite);
		vSubtract(vec_in_plane, quad_pos_x, quad_opposite);
		vCrossProduct(normal_v31, vec_to_startpos, vec_in_plane);
		r_dot[2] = vDotProduct(normal_v31, ray);

		//the dot products should be all >0 if the ray will intersect the triangle
		if((r_dot[0] >= 0.0f) && (r_dot[1] >= 0.0f) && (r_dot[2] >= 0.0f))
		{
			r = 1;
		}
		else
		{
			r = 0;
		}
	}

	//Check triangle 2 only if triangle 1 never hit.
	if(r == 0)
	{
		//First see if the end point of the ray is in front of or behind the plane of the triangle
		vGetPlaneNormal(quad_pos_z, quad_pos_x, quad_origin, plane_normal);
		plane_d = 0.0f - (quad_pos_z[0]*plane_normal[0]) - (quad_pos_z[1]*plane_normal[1]) - (quad_pos_z[2]*plane_normal[2]);
		vSubtract(vec_to_endpos, endpos, quad_pos_z);
		
		r_dot[0] = vDotProduct(vec_to_endpos, plane_normal);
		if(r_dot[0] <= 0.0f)
		{
			//calc vec v2-to-1
			vSubtract(vec_to_startpos, pos, quad_pos_z);
			vSubtract(vec_in_plane, quad_pos_x, quad_pos_z);
			vCrossProduct(normal_v12, vec_to_startpos, vec_in_plane); //normal_v12, name is bad really goes from 2 to 1
			r_dot[0] = vDotProduct(normal_v12, ray);

			//calc vec v1-to-3
			vSubtract(vec_to_startpos, pos, quad_pos_x);
			vSubtract(vec_in_plane, quad_origin, quad_pos_x);
			vCrossProduct(normal_v31, vec_to_startpos, vec_in_plane);
			r_dot[1] = vDotProduct(normal_v31, ray);

			//calc vec v3-to-2
			vSubtract(vec_to_startpos, pos, quad_origin);
			vSubtract(vec_in_plane, quad_pos_z, quad_origin);
			vCrossProduct(normal_v23, vec_to_startpos, vec_in_plane);
			r_dot[2] = vDotProduct(normal_v23, ray);

			if((r_dot[0] >= 0.0f) && (r_dot[1] >= 0.0f) && (r_dot[2] >= 0.0f))
			{
				r = 1;
			}
			else
			{
				r = 0;
			}
		}
	}

	//if there is an intersection with the triangle, determine the surface point of
	//the intersection.
	if(r == 1)
	{
			//prepare a normalized ray. What we will do is take this normalized ray
			//project it to the normal of the triangle, this gives us a factor of how
			//much of the normalized ray to multiply to get a ray that will hit the
			//triangle surface exactly.
			normal_ray[0] = ray[0];
			normal_ray[1] = ray[1];
			normal_ray[2] = ray[2];
			vNormalize(normal_ray);
			t_factor = vDotProduct(ray, plane_normal);  
			t_factor = fabs(t_factor);	//since normal_ray intersects triangle, normal_ray and plane_normal point in opposite dir, so t_factor will be negative
			vSubtract(vec_to_startpos, pos, quad_pos_z);
			dist_in_planenormal = vDotProduct(vec_to_startpos, plane_normal);

			//Multiply t_factor by the normalized ray and we get a vector that goes from
			//pos to the surface of the triangle. Save our results and indicate that we
			//have an intersection
			t_factor = dist_in_planenormal/t_factor;

			surf_pos[0] = (t_factor*ray[0]) + pos[0];
			surf_pos[1] = (t_factor*ray[1]) + pos[1];
			surf_pos[2] = (t_factor*ray[2]) + pos[2];

			surf_norm[0] = plane_normal[0];
			surf_norm[1] = plane_normal[1];
			surf_norm[2] = plane_normal[2];
	}

	return r;
}

/*
MakeTerrainElementArray() allocates and places an array of GLshorts that
it will fill in with element indices
pElements: array of GLshorts
num_x: # of vertices in the x direction
num_z: # of vertices in the z direction
*/
int MakeTerrainElementArray(GLshort ** ppElements, int * num_indices, int num_x, int num_z)
{
	int i; //row
	int j; //column
	int quad_i;
	int num_quad;
	int num_bytes;
	int num_triangles;
	int num_floats;
	GLshort * elements; 
	
	num_triangles = 2*(num_x-1)*(num_z-1);
	num_floats = 3*num_triangles;
	*num_indices = num_floats;//idk why i called this num_floats
	num_bytes = num_floats*sizeof(GLshort); 
	elements = (GLshort*)malloc(num_bytes);
	if(elements == 0)
	{
		printf("MakeTerrainElementArray: malloc failed for elements array.\n");
		return 0;
	}
	
	num_quad = 0;
	for(i = 0; i < (num_z-1); i++) //subtract 1 because we are dealing with the squares instead of the vertices
	{
		for(j = 0; j < (num_x-1); j++)
		{
			quad_i = ((i*(num_x-1)) + j)*6; //there are 6 elements in a quad. 3 for each triangle.
			/*
			This will make two triangles:
				1st triangle: (origin, vert in +z dir, vert in +x dir)
				2nd triangle: (vert in +z dir, vert opposite origin, vert in +x dir)
			*/
			elements[quad_i] = num_quad+i; //add + i 
			elements[(quad_i+1)] = (num_quad+i) + num_x;
			elements[(quad_i+2)] = (num_quad+i) + 1;
			elements[(quad_i+3)] = (num_quad+i) + num_x;
			elements[(quad_i+4)] = (num_quad+i) + num_x + 1;
			elements[(quad_i+5)] = (num_quad+i) + 1;
			num_quad += 1;		
		}
	}
	printf("exited enumeration loop: num_quad=%d quad_i=%d i=%d j=%d\n", num_quad, quad_i, i, j);
	//debug prints:
	//printf("quad[0,0]: %hd %hd %hd %hd %hd %hd\n", elements[0], elements[1], elements[2], elements[3], elements[4], elements[5]);
	//printf("quad[0,1]: %hd %hd %hd %hd %hd %hd\n", elements[6], elements[7], elements[8], elements[9], elements[10], elements[11]);
	//printf("quad[98,0]: %hd %hd %hd %hd %hd %hd\n", elements[57624], elements[57625], elements[57626], elements[57627], elements[57628], elements[57629]);
	//printf("quad[98,98]: %hd %hd %hd %hd %hd %hd\n", elements[58800], elements[58801], elements[58802], elements[58803], elements[58804], elements[58805]);
	*ppElements = elements;
	return 1;
}

/*
all parameters are vectors.
u _cross_ v = normal
*/
void MakeTerrainCalcNormal(float * normal, float * origin_pos, float * u, float * v)
{
	float v1[3];
	float v2[3];
	
	vSubtract(v1, u, origin_pos);
	vSubtract(v2, v, origin_pos);
	vNormalize(v1);
	vNormalize(v2);
	vCrossProduct(normal, v1, v2); //this cross-product is a left-hand-rule cross product.
	vNormalize(normal);
}

static int HandleKeyboardInput(Display * dpy, 
	float * camera_rotX, 
	float * camera_rotY, 
	float * camera_ipos, 
	struct character_struct * p_character,
	struct vehicle_physics_struct * p_vehicle)
{
	float temp_vec[3];
	char keys_return[32];
	static int l_key_is_up;
	static int space_key_is_up;
	static int zero_key_is_up;
	int i;
	int r;

	XQueryKeymap(dpy, keys_return);
	switch(g_keyboard_state.state)
	{
		case KEYBOARD_MODE_CAMERA: //keys control camera only
			UpdateCameraFromKeyboard(keys_return, camera_rotX, camera_rotY, camera_ipos);
			break;
		case KEYBOARD_MODE_PLAYER:	//keys control player
			r = UpdatePlayerFromKeyboard(keys_return, camera_rotX, camera_rotY, camera_ipos, p_character);
			if(r == 0) //error. (like map vbo wouldn't init)
				return 0;
			break;
		case KEYBOARD_MODE_TRUCK:
			//UpdateVehicleFromKeyboard(keys_return, camera_rotX, camera_rotY, camera_ipos, p_vehicle);
			UpdateVehicleFromKeyboard2(keys_return, camera_rotX, camera_rotY, camera_ipos, &g_b_vehicle);
			break;
		case KEYBOARD_MODE_INVGUI:
			UpdateGUIFromKeyboard(keys_return);
			break;
		case KEYBOARD_MODE_MAPGUI:
			r = UpdateMapFromKeyboard(keys_return);
			if(r == 0) //error
				return 0;
			break;
		//case 99:
			//DebugAdjustItemMatFromKeyboard(keys_return);
			break;
	}

	//Now handle overall keys & input related stuff that is always applicable:
	//check Spacebar (this has turned into the key for debug features)
	if(CheckKey(keys_return, 65) == 1)
	{
		if(space_key_is_up == 1)
		{
			//TODO: Just place this here for debug
			//temp_vec[0] = 12319.0f;
			//temp_vec[1] = 0.0f;
			//temp_vec[2] = 11279.0f;
			//FlattenTerrain(temp_vec, 100.0f, 100.0f, 0.0f);

			//Debug: teleport triangle man to the base for test
			//g_a_man.pos[0] = 12454.0f;
			//g_a_man.pos[1] = 25.0f;
			//g_a_man.pos[2] = 11081.0f;

			//Debug: tell AI soldier to walk to base.
			temp_vec[0] = 12454.0f;
			temp_vec[1] = 25.0f;
			temp_vec[2] = 11081.0f;
			StartSoldierAIPath(&g_ai_soldier, temp_vec);

			//SimulationStep(); //normally spacebar did 1 simulation step, but for now ignore that while we do inv stuff.
		}
		space_key_is_up = 0;
	}
	else
	{
		space_key_is_up = 1;
	}

	//check 0 key
	if(CheckKey(keys_return, 19) == 1)
	{
		if(zero_key_is_up == 1)
		{
			if(g_pause_simulation_step == 0)
			{
				g_pause_simulation_step = 1;
				printf("simulation step paused.\n");
			}
			else
			{
				g_pause_simulation_step = 0;
			}
		}
		zero_key_is_up = 0;
	}
	else
	{
		zero_key_is_up = 1;
	}

	//check the 'o' key and print debug stats for drawing
	if((CheckKey(keys_return, 32)) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 32) == 0)
	{
		printf("min time btwn draws:%u s=%ld nsec=%ld\n", g_debug_stats.step_at_min_drawcall, g_debug_stats.min_drawcall_period.tv_sec, g_debug_stats.min_drawcall_period.tv_nsec);
		printf("max time btwn draws:%u s=%ld nsec=%ld\n", g_debug_stats.step_at_max_drawcall,g_debug_stats.max_drawcall_period.tv_sec, g_debug_stats.max_drawcall_period.tv_nsec);
		printf("min draw duration: step=%u s=%ld nsec=%ld\n", g_debug_stats.step_atMinDrawDuration, g_debug_stats.min_drawcall_duration.tv_sec, g_debug_stats.min_drawcall_duration.tv_nsec);
		printf("max draw duration: step=%u s=%ld nsec=%ld\n", g_debug_stats.step_atMaxDrawDuration, g_debug_stats.max_drawcall_duration.tv_sec, g_debug_stats.max_drawcall_duration.tv_nsec);
		
		g_debug_stats.reset_flag = 1;

		//print some # of drawcall info
		printf("number of simple bush billboard drawcalls=%d\n", g_debug_num_simple_billboard_draws);
		printf("number of detail bush billboard drawcalls=%d\n", g_debug_num_detail_billboard_draws);
		printf("number of detailed plant drawcalls=%d\n", g_debug_num_himodel_plant_draws);
		
		//debug advance the animation:
		//g_debug_keyframe += 1;
		//if(g_debug_keyframe == 61)
		//	g_debug_keyframe = 0;
		//printf("debug_keyframe=%d\n", g_debug_keyframe);
		
		//debug reset the animation:
		//g_triangle_man.p_anims[g_a_man.current_anim].keyframe_phase = (g_triangle_man.p_anims[g_a_man.current_anim].anim_len - (g_simulation_step % g_triangle_man.p_anims[g_a_man.current_anim].anim_len));
		
		//debug the simulation step:
		//g_simulation_step += 1;
		//SimulationStep();
		//printf("step=%d\n", g_simulation_step);
		
		//print wheel stats
		//printf("wheel lM= %f,%f,%f\n", g_a_wheel.linearMomentum[0],g_a_wheel.linearMomentum[1],g_a_wheel.linearMomentum[2]);
		//printf("wheel lV= %f,%f,%f\n", g_a_wheel.linearVel[0],g_a_wheel.linearVel[1],g_a_wheel.linearVel[2]);
		//printf("wheel aM= %f,%f,%f\n", g_a_wheel.angularMomentum[0],g_a_wheel.angularMomentum[1],g_a_wheel.angularMomentum[2]);
		//printf("wheel aV= %f,%f,%f\n", g_a_wheel.angularVel[0],g_a_wheel.angularVel[1],g_a_wheel.angularVel[2]);
		for(i = 0; i < 4; i++)
		{
			printf("wheel %d cg=%f %f %f\n", i, g_a_vehicle.wheels[i].cg[0],g_a_vehicle.wheels[i].cg[1],g_a_vehicle.wheels[i].cg[2]);
		}
		printf("steering angle=%f\n", g_b_vehicle.steeringAngle);

		//print map scale
		printf("gui map scale=%f\n", g_gui_map.mapScale);
	}

	//Check 'F1' key
	if(CheckKey(keys_return, 67) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 67) == 0) //key is down, and previous frame it was up.
	{
		g_keyboard_state.state = KEYBOARD_MODE_CAMERA;
		g_camera_state.playerCameraMode = 0; //reset the player camera mode
		printf("%s: in KEYBOARD_MODE_CAMERA\n", __func__);
	}

	//Check 'F2' key
	if(CheckKey(keys_return, 68) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 68) == 0)
	{
		g_keyboard_state.state = KEYBOARD_MODE_PLAYER;
		g_camera_state.playerCameraMode = PLAYERCAMERA_FP;
		//g_camera_rotY = g_a_man.rotY + 180.0f; //sync camera to man
		//g_camera_rotX = 0.0f;
		printf("%s: in KEYBOARD_MODE_PLAYER\n", __func__);
	}

	//Check 'F3' key
	if(CheckKey(keys_return, 69) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 69) == 0)
	{
		g_keyboard_state.state = KEYBOARD_MODE_TRUCK;
		printf("%s: in KEYBOARD_MODE_TRUCK\n", __func__);
	}

	//clamp Y rotation to < 360.0f
	if(*camera_rotY >= 360.0f)
		*camera_rotY -= 360.0f;
	if(*camera_rotY <= -360.0f) //check negative as well
		*camera_rotY += 360.0f;
	
	//clamp X rotation
	if(*camera_rotX >= 360.0f)
		*camera_rotX -= 360.0f;
	if(*camera_rotX <= -360.0f)
		*camera_rotX += 360.0f;

	//update the global that holds the non-inverted camera position
	//if(g_keyboard_state.state == KEYBOARD_MODE_TRUCK) //vehicle mode, set the camera based on vehicle position.
	//{
		//note: the nomenclature is a bit messed up. "camera_pos" is actually the inverse position that
		//is used by DrawScene().
	//	UpdateCameraPosAtVehicle2(15.0f, &g_b_vehicle, g_ws_camera_pos, camera_ipos, camera_rotY);	
	//}
	else //all other modes.
	{
		g_ws_camera_pos[0] = -1.0f*camera_ipos[0];
		g_ws_camera_pos[1] = -1.0f*camera_ipos[1];
		g_ws_camera_pos[2] = -1.0f*camera_ipos[2];
	}

	//Now that we are finished handling all keys, save the copy of key positions
	memcpy(g_keyboard_state.prev_keys_return, keys_return, 32);

	return 1;
} 

/*
This function adjusts the bone space transform for the item for
debug purpose only.
*/
/*static void DebugAdjustItemMatFromKeyboard(char * keys_return)
{
	float ftranslateSpeed=0.01f;
	float mat4[16];
	static p_key_is_up;

	//left-arrow
	if(CheckKey(keys_return, 113) == 1)
	{
		if(CheckKey(keys_return, 28) == 0) //If just 'left-arrow' key pressed then rotate, but if left arrow and 't' key pressed then move item.
			g_rifle_common.debugRot[0] += 0.1f;
		else
			g_rifle_common.debugTrans[0] += ftranslateSpeed;
	}
	//right-arrow
	if(CheckKey(keys_return, 114) == 1)
	{
		if(CheckKey(keys_return, 28) == 0)
			g_rifle_common.debugRot[0] -= 0.1f;
		else
			g_rifle_common.debugTrans[0] -= ftranslateSpeed;
	}

	//up-arrow
	if(CheckKey(keys_return, 111) == 1)
	{
		if(CheckKey(keys_return, 28) == 0)
			g_rifle_common.debugRot[1] += 0.1f;
		else
			g_rifle_common.debugTrans[1] += ftranslateSpeed;
	}
	//down-arrow
	if(CheckKey(keys_return, 116) == 1)
	{
		if(CheckKey(keys_return, 28) == 0)
			g_rifle_common.debugRot[1] -= 0.1f;
		else
			g_rifle_common.debugTrans[1] -= ftranslateSpeed;
	}

	//w key
	if(CheckKey(keys_return, 25) == 1)
	{
		if(CheckKey(keys_return, 28) == 0)
			g_rifle_common.debugRot[2] += 0.1f;
		else
			g_rifle_common.debugTrans[2] += ftranslateSpeed;
	}
	//s key
	if(CheckKey(keys_return, 39) == 1)
	{
		if(CheckKey(keys_return, 28) == 0)
			g_rifle_common.debugRot[2] -= 0.1f;
		else
			g_rifle_common.debugTrans[2] -= ftranslateSpeed;
	}

	//p key
	if(CheckKey(keys_return, 33) == 1)
	{
		if(p_key_is_up == 1)
		{
			printf("debugRot: x=%f y=%f z=%f degs\n", g_rifle_common.debugRot[0], g_rifle_common.debugRot[1], g_rifle_common.debugRot[2]);
			printf("debugTrans: x=%f y=%f z=%f units.\n", g_rifle_common.debugTrans[0], g_rifle_common.debugTrans[1], g_rifle_common.debugTrans[2]);
		}
		p_key_is_up = 0;
	}
	else
	{
		p_key_is_up = 1;
	}

	//order of rotations: Y, X, Z
	mmTranslateMatrix(mat4, g_rifle_common.debugTrans[0], g_rifle_common.debugTrans[1], g_rifle_common.debugTrans[2]);
	mmRotateAboutY(g_rifle_common.itemBoneSpaceAimTransformMat4, -148.299850f);
	mmMultiplyMatrix4x4(g_rifle_common.itemBoneSpaceAimTransformMat4, mat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	mmRotateAboutX(mat4, 53.199760f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	mmRotateAboutZ(mat4, -67.599541f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	//mmTranslateMatrix(mat4, g_rifle_common.debugTrans[0], g_rifle_common.debugTrans[1], g_rifle_common.debugTrans[2]);
	mmTranslateMatrix(mat4, -0.89964f, 1.26315f, 0.153880f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);

}*/

static int UpdateCameraFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos)
{
	struct lvl_1_tile * ptile=0;
	float temp_x;
	float temp_z;
	float ws_camera_pos[3];
	float d_surf_point[3];
	float v3_normal[3];
	float tempVec[3];
	static int p_key_is_up;
	static int o_key_is_up;
	int i;
	int j;
	int r;

	//check left-arrow
	if(CheckKey(keys_return, 113) == 1)
	{
		*camera_rotY -= 1.0f;
	}
	
	//check right-arrow
	if(CheckKey(keys_return, 114) == 1)
	{

		*camera_rotY += 1.0f;
	}
	
	//check w key
	if(CheckKey(keys_return, 25)==1)
	{
		GetXZComponents(g_camera_speed, *camera_rotY, &temp_z, &temp_x, 1);
		camera_ipos[0] += temp_x;
		camera_ipos[2] += temp_z;
	}
	
	//check s key
	if(CheckKey(keys_return, 39)==1)
	{
		GetXZComponents(g_camera_speed, *camera_rotY, &temp_z, &temp_x, 1);
		camera_ipos[0] -= temp_x;
		camera_ipos[2] -= temp_z;
	}

	//check a key
	if(CheckKey(keys_return, 38)==1)
	{
		GetXZComponents(g_camera_speed, (*camera_rotY-90.0f), &temp_z, &temp_x, 1);
		camera_ipos[0] += temp_x;
		camera_ipos[2] += temp_z;
	}
	
	//check d key
	if(CheckKey(keys_return, 40) == 1)
	{
		GetXZComponents(g_camera_speed, (*camera_rotY+90.0f), &temp_z, &temp_x, 1);
		camera_ipos[0] += temp_x;
		camera_ipos[2] += temp_z;
	}
	
	//up-arrow
	if(CheckKey(keys_return, 111)==1)
	{
		camera_ipos[1] -= g_camera_speed;
	}
	
	//down-arrow
	if(CheckKey(keys_return, 116)==1)
	{
		camera_ipos[1] += g_camera_speed;
	}

	//check the 'p' key (This prints information, m key plants bushses but I wanted to print information without planting a bush)
	if(CheckKey(keys_return, 33) == 1)
	{
		if(p_key_is_up == 1)
		{
			ws_camera_pos[0] = -1.0f*g_camera_pos[0];
			ws_camera_pos[1] = -1.0f*g_camera_pos[1];
			ws_camera_pos[2] = -1.0f*g_camera_pos[2];
			printf("icamera: (%f,%f,%f) rotY:%f rotX:%f deg speed: %f ", ws_camera_pos[0], ws_camera_pos[1], ws_camera_pos[2], g_camera_rotY, g_camera_rotX, g_camera_speed);
			r = GetLvl1Tile(ws_camera_pos);
			if(r != -1)
			{
				printf("local_tile=%d", r);

				//commented out below is an old test of surface raycast
				/*ptile = g_big_terrain.pTiles+r;
				tempVec[0] = ws_camera_pos[0] - ptile->urcorner[0];
				tempVec[2] = ws_camera_pos[2] - ptile->urcorner[1];
				j = (int)(tempVec[0]/10.0f);
				i = (int)(tempVec[2]/10.0f);

				tempVec[0] = 552.7275f;
				tempVec[1] = -16.0f;
				tempVec[2] = -376.8779f;
				r = RaycastTileSurf(ws_camera_pos, 
					tempVec, 
					d_surf_point, 
					v3_normal);
				printf("RaycastTileSurf: r=%d isect=%f,%f,%f\n", r, d_surf_point[0], d_surf_point[2], d_surf_point[3]);*/
			}
			printf("\n");

			printf("character: rotY=%f\n", g_a_man.rotY);
		}
		p_key_is_up = 0;
	}
	else
	{
		p_key_is_up = 1;
	}

	//check the '+' key
	if(CheckKey(keys_return, 21) == 1)
	{
		g_camera_speed = 10.0f;
	}
	
	//check the '-' key
	if(CheckKey(keys_return, 20) == 1)
	{
		g_camera_speed = 0.1f;
	}
	
	//check the 'f' key
	if(CheckKey(keys_return, 41) == 1)
	{
		g_debug_freeze_culling = 1;
	}
	else
	{
		g_debug_freeze_culling = 0; //if you aren't holding the f key then don't freeze culling
	}

	return 1;
}

static int UpdatePlayerFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct character_struct * p_character)
{
	float guy_rotY;
	static int j_key_is_up;
	int r;

	guy_rotY = p_character->rotY;

	//left-arrow
	if(CheckKey(keys_return, 113) == 1)
	{
		guy_rotY += 1.0f;
	}
	
	//check right-arrow
	if(CheckKey(keys_return, 114) == 1)
	{
		guy_rotY -= 1.0f;
	}
	
	//check w key
	if(CheckKey(keys_return, 25)==1)
	{
		p_character->events |= CHARACTER_FLAGS_WALK_FWD; //the event flag gets cleared by CharacterUpdate()
	}
	//check w and shift key
	if(CheckKey(keys_return, 25) == 1 && CheckKey(keys_return, 50) == 1)
	{
		p_character->events |= CHARACTER_FLAGS_RUN;
	}

	//check s key
	if(CheckKey(keys_return, 39)==1)
	{
		p_character->events |= CHARACTER_FLAGS_WALK_BACK; //the event flag gets cleared by CharacterUpdate()
	}

	//check a key
	if(CheckKey(keys_return, 38) == 1)
	{
		p_character->events |= CHARACTER_FLAGS_WALK_LEFT;
	}

	//check d key
	if(CheckKey(keys_return, 40) == 1)
	{
		p_character->events |= CHARACTER_FLAGS_WALK_RIGHT;
	}

	//check the 'e' key
	if(CheckKey(keys_return, 26) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 26) == 0)
	{
		EnterVehicle(&g_b_vehicle, p_character);
	}

	//check the 'g' key
	if(CheckKey(keys_return, 42) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 42) == 0)
	{
		if(g_render_mode == 0) //3d
		{
			PlayerGetItem();
		}
		else
		{
			SwitchRenderMode(0);
		}
	}

	//check the '1' key
	if(CheckKey(keys_return, 10) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 10) == 0)
	{
		p_character->events |= CHARACTER_FLAGS_ARM_RIFLE;
	}

	//check the '2' key
	if(CheckKey(keys_return, 11) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 11) == 0)
	{
		p_character->events |= CHARACTER_FLAGS_ARM_PISTOL;
	}

	//check the 'z' key
	if(CheckKey(keys_return, 52) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 52) == 0)
	{
		p_character->events |= CHARACTER_FLAGS_STAND;
	}
	//check the 'x' key
	if(CheckKey(keys_return, 53) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 53) == 0)
	{
		p_character->events |= CHARACTER_FLAGS_CROUCH;
	}
	//check the 'c' key
	if(CheckKey(keys_return, 54) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 54) == 0)
	{
		p_character->events |= CHARACTER_FLAGS_PRONE;
	}

	//check the '<' key
	if(CheckKey(keys_return, 59) == 1)
	{
		g_camera_state.playerCameraMode = PLAYERCAMERA_FP;
	}

	//check the '>' key
	if(CheckKey(keys_return, 60) == 1)
	{
		g_camera_state.playerCameraMode = PLAYERCAMERA_TP;
	}

	//check the 'j' key
	if(CheckKey(keys_return, 44) == 1)
	{
		if(j_key_is_up == 1)
		{
			p_character->events |= CHARACTER_FLAGS_AIM;
		}
		j_key_is_up = 0;
	}
	else
	{
		j_key_is_up = 1;
	}



	if(CheckKey(keys_return, 31) == 1)
	{
		printf("entering player item orientation debug mode...\n");
		g_keyboard_state.state = 99; //0 is debug mode
	}

	//Check the 'm' key, map
	if(CheckKey(keys_return, 58) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 58) == 0)
	{
		g_keyboard_state.state = KEYBOARD_MODE_MAPGUI;

		//Initialize map VBOs
		r = InitMapGUIVBO(&g_gui_map);
		if(r == 0) //error
			return 0;
		r = InitMapElevationLinesVBO(&g_gui_map);
		if(r == 0)
			return 0;

		SwitchRenderMode(2); //switch to map render mode
	}

	//clamp Y rotation of guy < 360.0f
	if(guy_rotY >= 360.0f)
		guy_rotY -= 360.0f;
	if(guy_rotY <= -360.0f)
		guy_rotY += 360.0f;

	p_character->rotY = guy_rotY;
	
	return 1;
}

static int UpdateVehicleFromKeyboard(char * keys_return, float * camera_rotX, float * camera_rotY, float * camera_ipos, struct vehicle_physics_struct * p_vehicle)
{
	//check left-arrow
	if(CheckKey(keys_return, 113) == 1)
	{

		p_vehicle->flags |= VEHICLEFLAGS_STEER_LEFT;
	}
	else
	{
		p_vehicle->flags = p_vehicle->flags & (~VEHICLEFLAGS_STEER_LEFT);
	}

	//check right-arrow
	if(CheckKey(keys_return, 114) == 1)
	{
		p_vehicle->flags |= VEHICLEFLAGS_STEER_RIGHT;
	}
	else
	{
		p_vehicle->flags = p_vehicle->flags & (~VEHICLEFLAGS_STEER_RIGHT);
	}

	//check up-arrow (zero steering so wheels face forward)
	if(CheckKey(keys_return, 111) == 1)
	{
		p_vehicle->flags |= VEHICLEFLAGS_ZERO_STEER;
	}
	else
	{
		p_vehicle->flags = p_vehicle->flags & (~VEHICLEFLAGS_ZERO_STEER);
	}

	//check w key
	if(CheckKey(keys_return, 25)==1)
	{
		p_vehicle->flags |= VEHICLEFLAGS_ACCELERATE; //set the accelerate bit
	}
	else
	{
		p_vehicle->flags = p_vehicle->flags & (~VEHICLEFLAGS_ACCELERATE); //clear the accelerate flag bit.
	}

	return 1;
}

static int UpdateGUIFromKeyboard(char * keys_return)
{
	
	//check 'g' (g gets us into the inventory screen so here its kinda like a toggle)
	if(CheckKey(keys_return, 42) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 42) == 0)
	{
		SwitchRenderMode(0);
		g_keyboard_state.state = KEYBOARD_MODE_PLAYER; //switch back

		//clear the temporary ground slots
		ClearGroundSlots(&g_temp_ground_slots);
	}

	return 1;
}

static int UpdateMapFromKeyboard(char * keys_return)
{
	float fratio;
	int i_tile;

	//check 'right arrow'
	if(CheckKey(keys_return, 114) == 1)
	{
		g_gui_map.mapCameraPos[0] += g_gui_map.cameraSpeed;
	}

	//check 'left arrow'
	if(CheckKey(keys_return, 113) == 1)
	{
		g_gui_map.mapCameraPos[0] -= g_gui_map.cameraSpeed;
	}

	//check 'up arrow'
	if(CheckKey(keys_return, 111) == 1)
	{
		g_gui_map.mapCameraPos[2] -= g_gui_map.cameraSpeed;
	}

	//check 'down arrow'
	if(CheckKey(keys_return, 116) == 1)
	{
		g_gui_map.mapCameraPos[2] += g_gui_map.cameraSpeed;
	}

	//check '+'
	if(CheckKey(keys_return, 21) == 1)
	{
		g_gui_map.mapScale += 0.01f;

		if(g_gui_map.mapScale > 1.0f)
			g_gui_map.mapScale = 1.0f;

		//adjust camera speed based on zoom (want to move faster when zoomed out)
		//g_gui_map.cameraSpeed = g_gui_map.mapScale*5.0f;	//TODO: Fix this calc, because it doesn't work right
		fratio = (g_gui_map.mapScale - 0.01989f)/(1-0.01989f);
		fratio = 1.0f - fratio;
		g_gui_map.cameraSpeed = (90.0f*fratio) + 10.0f;
	}

	//check '-'
	if(CheckKey(keys_return, 20) == 1)
	{
		g_gui_map.mapScale -= 0.01f;
		if(g_gui_map.mapScale < 0.01989f)
			g_gui_map.mapScale = 0.01989f;

		//adjust camera speed based on zoom (want to move faster when zoomed out)
		fratio = (g_gui_map.mapScale - 0.01989f)/(1-0.01989f);
		fratio = 1.0f - fratio;
		g_gui_map.cameraSpeed = (90.0f*fratio) + 10.0f;
	}

	return 1;
}

/*
This func calls in_MouseRelPos() to get relative mouse movement, and mouse clicks.
mouseState is an array of flags of the following:
	bit 0	;current state of left-mouse-button. bit set=mouse btn currently down
	bit 1	;sticky bit that is set if there was a mouse-down in the events in_MouseRelPos() read.
*/
int UpdateFromMouseInput(float * camera_rotX, float * camera_rotY)
{
	char mouseState;
	int mouse_rel[2];
	int r;
	
	mouse_rel[0] = 0;
	mouse_rel[1] = 0;
	
	r = in_MouseRelPos(mouse_rel, &mouseState);
	if(r == 0)
	{
		return 0;
	}

	if(g_render_mode == 0) //draw scene
	{
		//scale the raw mouse coordinates to rotation
		*camera_rotY += 0.1f*((float)mouse_rel[0]);
		*camera_rotX += 0.1f*((float)mouse_rel[1]);
	}
	if(g_render_mode == 1) //inventory
	{
		g_gui_inv_cursor.pos[0] += 0.1f*((float)mouse_rel[0]);
		g_gui_inv_cursor.pos[1] -= 0.1f*((float)mouse_rel[1]); //why is this negative>?
		
		//clamp cursor to boundaries
		if(g_gui_inv_cursor.pos[0] < g_gui_inv_cursor.x_limits[0])
			g_gui_inv_cursor.pos[0] = g_gui_inv_cursor.x_limits[0];
		if(g_gui_inv_cursor.pos[0] > g_gui_inv_cursor.x_limits[1])
			g_gui_inv_cursor.pos[0] = g_gui_inv_cursor.x_limits[1];
		if(g_gui_inv_cursor.pos[1] < g_gui_inv_cursor.y_limits[0])
			g_gui_inv_cursor.pos[1] = g_gui_inv_cursor.y_limits[0];
		if(g_gui_inv_cursor.pos[1] > g_gui_inv_cursor.y_limits[1])
			g_gui_inv_cursor.pos[1] = g_gui_inv_cursor.y_limits[1];

		//handle mouse buttons
		if((mouseState & 2) == 2) //was there a left-mouse-button-down event?
		{
			g_gui_inv_cursor.events = GUI_EVENTS_LEFTMOUSEDOWN;
		}

	}
	
	return 1;
}

/*
hint: use the program 'xev' to print xevents to find new keycodes
*/
int CheckKey(char * keys_return, int key_bit_index)
{
	int i;
	int bit;
	int mask=1;
	int r=0;
	
	i = key_bit_index/8;
	bit = key_bit_index % 8;
	mask = mask << bit;
	
	if((keys_return[i] & mask) != 0)
	{
		r = 1;
	}
	return r;
}

/*
When rotY is 0 the resulting vector point to by z and x points in the -z dir
rotY is inverse because it is the camera's rotation.
*/
void GetXZComponents(float speed, float invRotY, float * z, float * x, int invert_rot)
{
	float rotY;
	
	//the rotation passed is the inverse of what we need to calculate
	if(invert_rot)
		rotY = -1.0f*invRotY;
	else
		rotY = invRotY;
	
	*x = sinf(rotY*RATIO_DEGTORAD); 
	*z = cosf(rotY*RATIO_DEGTORAD);
	*x = (*x)*speed;
	*z = (*z)*speed;
}

/*
LoadBushVBO()
-uses flags to control how textures are loaded:
	1	;manually generate mipmaps (default is auto-generate mipmaps)
	2	;scale model position data using fscale_factor
*/
int LoadBushVBO(struct plant_billboard * p_billboard, char * mesh_filename, char * mesh_name, char * tex_filename, float fscale_factor, char flags)
{
	struct bush_model_struct bush_file_info;
	int r;

	//load bush model
	printf("loading %s model ....\n", mesh_filename);
	r = Load_Bush_Model(&bush_file_info, 	//address of empty structure for data from file
		mesh_name, 			//name of mesh to look for in file
		mesh_filename);			//filename
	if(r == 0)
	{
		printf("LoadBushVBO: Load_Bush_Model failed for %s.\n", mesh_filename);
		return 0;
	}

	//check to see if we should scale the vertex positions
	if((flags & 2) == 2)
	{
		Scale_Bush_Model(&bush_file_info, fscale_factor);
	}	
	
	//transfer information from the bush file structure into the bush model
	p_billboard->num_verts = bush_file_info.num_leaf_verts;
	p_billboard->num_indices = bush_file_info.num_leaf_elements;
	p_billboard->p_elements = (GLint*)bush_file_info.p_leaf_elements;
	p_billboard->p_vertex_data = bush_file_info.p_leaf_vertex_data;
	printf("%s model: num_verts=%d, num_indices=%d\n", mesh_filename, p_billboard->num_verts, p_billboard->num_indices);
	
	//setup the enumeration buffer
	glGenBuffers(1, &(p_billboard->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_billboard->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, p_billboard->num_indices*sizeof(GLint), p_billboard->p_elements, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	//setup the vbo's & vao's
	glGenBuffers(1, &(p_billboard->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_billboard->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p_billboard->num_verts)*8*sizeof(float), p_billboard->p_vertex_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	glGenVertexArrays(1, &(p_billboard->vao));
	glBindVertexArray(p_billboard->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_billboard->vbo);
	glEnableVertexAttribArray(0); //position
	glEnableVertexAttribArray(1); //normal vector
	glEnableVertexAttribArray(2); //texture-coordinate
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_billboard->ebo); //save the GL_ELEMENT_ARRAY_BUFFER state in the VAO
	glBindVertexArray(0);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//load bush texture
	if((flags & 1) == 1) //set bit for loading manually generate texture mip-maps
		r = LoadBushTextureManualMip(&(p_billboard->texture_id), tex_filename);
	else	//call glGenerateMipmap() to generate mipmaps
		r = LoadBushTextureGenMip(&(p_billboard->texture_id), tex_filename);
	if(r != 1) //check for error
		return 0;

	printf("done loading %s model.\n", mesh_filename);
	return 1;
}

/*
This function setups a VBO & VAO for a camera-facing billboard that is used by its
accompanying shader.
*/
int LoadSimpleBillboardVBO(struct simple_billboard * p_billboard)
{
	float init_vertex_data[] = {
		0.0f, 0.0f, 0.0f,  0.0f, 0.0f, //lower-left corner
		0.0f, 0.0f, 0.0f,  1.0f, 0.0f, //lower-right corner
		0.0f, 0.0f, 0.0f,  1.0f, 1.0f, //top-right corner
		0.0f, 0.0f, 0.0f,  0.0f, 0.0f,
		0.0f, 0.0f, 0.0f,  1.0f, 1.0f,
		0.0f, 0.0f, 0.0f,  0.0f, 1.0f
	};
	
	p_billboard->num_verts = 6;
	p_billboard->p_vertex_data = (float*)malloc(p_billboard->num_verts*5*sizeof(float));
	if(p_billboard->p_vertex_data == 0)
	{
		printf("LoadSimpleBillboardVBO: error. malloc failed for vertex data.\n");
		return -1;
	}
	memcpy(p_billboard->p_vertex_data, init_vertex_data, (p_billboard->num_verts*5*sizeof(float)));
	
	//save billboard size information
	//p_billboard->size[0] = width;
	//p_billboard->size[1] = height;
	
	//setup the vbo & vao's
	glGenBuffers(1, &(p_billboard->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_billboard->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p_billboard->num_verts*5*sizeof(float)), p_billboard->p_vertex_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	glGenVertexArrays(1, &(p_billboard->vao));
	glBindVertexArray(p_billboard->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_billboard->vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//LoadBushTextureManualMip(&(p_billboard->texture_id), tex_filename);
	
	//printf("done loading simple billboard %s\n", tex_filename);
	return 1;
}

/*
LoadSimpleBillboardTextures() initializes the struct simple_billboard.texture_id[6] array
with textures. the array is indexed by plant type.
*/
int LoadSimpleBillboardTextures(struct simple_billboard * billboard)
{
	int r;

	memset(billboard->size, 0, 12*sizeof(float));
	memset(billboard->texture_ids, 0, 6*sizeof(GLuint));

	//simple bush
	billboard->size[0] = 1.0f;
	billboard->size[1] = 1.0f;
	r = LoadBushTextureManualMip(&(billboard->texture_ids[0]), "./resources/textures/m00_tourne_billboard_00.tga"); //load this one manual mip because that is what I did for simple bush.
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 0);
		return 0;
	}

	//palm_2
	billboard->size[2] = 7.8159f;
	billboard->size[3] = 7.8159f;
	r = LoadBushTextureGenMip(&(billboard->texture_ids[1]), "./resources/textures/palm_2_lowres_billboard.tga");
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 1);
		return 0;
	}

	//scaevola
	billboard->size[4] = 1.0f;
	billboard->size[5] = 1.0f;
	r = LoadBushTextureGenMip(&(billboard->texture_ids[2]), "./resources/textures/scaevola_branch.tga");
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 2);
		return 0;
	}

	//fake pemphis
	billboard->size[6] = 1.5f;
	billboard->size[7] = 1.5f;
	r = LoadBushTextureGenMip(&(billboard->texture_ids[3]), "./resources/textures/pemphis_simplebillboard.tga");
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 3);
		return 0;
	}

	//tourne fortia
	billboard->size[8] = 6.0f;
	billboard->size[9] = 6.0f;
	r = LoadBushTextureGenMip(&(billboard->texture_ids[4]), "./resources/textures/tourne_simplebillboard.tga");
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 4);
		return 0;
	}

	//ironwood
	billboard->size[10] = 8.0f;
	billboard->size[11] = 8.0f;
	r = LoadBushTextureGenMip(&(billboard->texture_ids[5]), "./resources/textures/ironwood_simplebillboard.tga");
	if(r != 1)
	{
		printf("LoadSimpleBillboardTextures: error. LoadBushTextureManualMip() failed. texture id %d\n", 5);
		return 0;
	}

	return 1;
}

/*LoadTexture() is hard-coded to load the map tile texture*/
void LoadTexture(void)
{
	int 	r;
	image_t tgaFile;
	GLenum  format_to_use;

	glGenTextures(1, &(g_big_terrain.textureId));
	glBindTexture(GL_TEXTURE_2D, g_big_terrain.textureId);
	
	printf("loading texture tga...\n");
	//r = LoadTga("sand1.tga", &tgaFile);
	r = LoadTga("./resources/textures/sand2.tga", &tgaFile);
	//r = LoadTga("sand3.tga", &tgaFile);
	if(r == 0)
		return;
	DisplayTGAHeader(&tgaFile);
	
	//Set the format based on whether or not the TGA file had alpha
	if(tgaFile.info.components == 3)
	{
		format_to_use = GL_RGB;
	}
	else if(tgaFile.info.components == 4)
	{
		format_to_use = GL_RGBA;
	}
	else
	{
		printf("LoadTexture: error. unexpected # of components: %d\n", tgaFile.info.components);
		return;	
	}
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(	GL_TEXTURE_2D,	
			0,			//level. we will set multiple levels of the texture later.
			format_to_use,			//internal format. specifies number of color components
			tgaFile.info.width,
			tgaFile.info.height,
			0,			//width of border. Set to 0 so there isn't a border
			format_to_use,			//specify what the data is that's in the tga file
			GL_UNSIGNED_BYTE,	//specify data type of texture data in the tga file
			tgaFile.data);
	free(tgaFile.data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 9); //log 2 (512) => 9  (each mipmap is half the dimension of the previous. so we go from 512->256->128..etc.)
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	//Create samplers
	glGenSamplers(1, &(g_big_terrain.sampler));
	
	//glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	//glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR); //<--- glGetError() 0x500
	glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	
	glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(g_big_terrain.sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);

}

/*
This function takes a path to a bush texture and sets it up.
This function expects mipmaps to be done manually and follow special naming
Texture sizes:
	black_rough.tga		1024x1024
	brown_rough.tga		1024x1024
	palm_branch_00.tga	512x512
	pine_billboard_00.tga	1024x1024
	tourne_billboard_00.tga	1024x1024
*/
int LoadBushTextureManualMip(GLuint * p_tex_id, char * tga_filename)
{
	int r;
	int i;
	image_t tgaFile;
	GLint tex_max_level;
	char num_string[3];
	char mipmap_filename[255];
	char * p_filename_sequence; //location of the '00' in the mipmap filename
		
	glGenTextures(1, p_tex_id);
	glBindTexture(GL_TEXTURE_2D, *p_tex_id);
	
	printf("loading bush %s texture tga...\n", tga_filename);
	
	//load the base-level texture
	r = LoadTga(tga_filename, &tgaFile);
	if(r == 0)
		return -1;
	DisplayTGAHeader(&tgaFile);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	//Assume the bush texture has an alpha
	if(tgaFile.info.components != 4)
	{
		printf("LoadBushTextureManualMip: error. # of TGA file components != 4. components is %d\n", tgaFile.info.components);
		return -1;
	}
	
	//Check to see that the texture is of the allowed sizes (because I'm dumb and just want to restrict mip-map levels to 9 or 10)
	if(IsTgaAllowedSize(&(tgaFile.info)) == 0)
	{
		printf("LoadBushTextureManualMip: error. %s size not allowed.\n", tga_filename);
		return -1;
	}
	else
	{
		if(tgaFile.info.width == 512)
			tex_max_level = 9;
		else if(tgaFile.info.width == 1024)
			tex_max_level = 10;
		else
			tex_max_level = 0;
	}
	glTexImage2D(GL_TEXTURE_2D,
		0,//level. this is the base level for this texture.
		GL_RGBA,//internal texture format.
		tgaFile.info.width,
		tgaFile.info.height,
		0, //width of border. Set to 0 so there is not a border.
		GL_RGBA,//specify the format of the data in the TGA file.
		GL_UNSIGNED_BYTE, //specify the data type of texture data in the tga file.
		tgaFile.data);
	free(tgaFile.data);

	//setup the filename string for the mipmap files
	strcpy(mipmap_filename, tga_filename);
	
	//find the location of the '00' in the filename
	p_filename_sequence = strstr(mipmap_filename, "00");
	if(p_filename_sequence == 0)
	{
		printf("LoadBushTextureManualMip: error. could not find '00' in %s.\n", mipmap_filename);
	}
	
	//load all the mipmap textures
	for(i = 1; i <= tex_max_level; i++)
	{
		//make the first part of the filename based on the mipmap level
		sprintf(num_string, "%.2d", i);
		p_filename_sequence[0] = num_string[0]; //these are hardcoded.
		p_filename_sequence[1] = num_string[1];
		printf("loading bush %s texture tga...\n", mipmap_filename);
		r = LoadTga(mipmap_filename, &tgaFile);
		if(r == 0)
			return -1;
		if(tgaFile.info.components != 4)
		{
			printf("LoadBushTextureManualMip: error. # of TGA file components != 4. components is %d\n", tgaFile.info.components);
			return -1;
		}
		glTexImage2D(GL_TEXTURE_2D,
			(GLint)i, //mipmap level
			GL_RGBA, //internal texture format
			tgaFile.info.width,
			tgaFile.info.height,
			0, //width of border. Set to 0 so there is no border.
			GL_RGBA, //specify the format of the data in the TGA file
			GL_UNSIGNED_BYTE, //specify the data type of texture data in the tga file
			tgaFile.data);
		free(tgaFile.data);
	}
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, tex_max_level);
	
	glBindTexture(GL_TEXTURE_2D, 0);
	
	return 1;	
}

int LoadBushTextureGenMip(GLuint * p_tex_id, char * tga_filename)
{
	int r;
	int max_base_level;
	image_t tgaFile;
	GLenum format_to_use;

	glGenTextures(1, p_tex_id);
	glBindTexture(GL_TEXTURE_2D, *p_tex_id);

	r = LoadTga(tga_filename, &tgaFile);
	if(r == 0)
		return 0;
	DisplayTGAHeader(&tgaFile);

	//Verify that bush textures have an alpha channel
	if(tgaFile.info.components != 4)
	{
		printf("LoadBushTextureGenMip: error. %s only has %d components. needs 4.\n", tga_filename, tgaFile.info.components);
		return 0;
	}

	format_to_use = GL_RGBA;

	//check the tga dimensions. only allow certain sizes
	if(tgaFile.info.width == 512 && tgaFile.info.height == 512)
	{
		max_base_level = 9;
	}
	else if(tgaFile.info.width == 1024 && tgaFile.info.height == 1024)
	{
		max_base_level = 10;
	}
	else if(tgaFile.info.width == 256 && tgaFile.info.height == 256)
	{
		max_base_level = 8;
	}
	else
	{
		printf("LoadBushTextureGenMip: error. %s is an unsupported texture size.\n", tga_filename);
		return 0;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D,
			0,		//level. we will generate multiple levels of the texture later.
			format_to_use,
			tgaFile.info.width,
			tgaFile.info.height,
			0,		//width of border.
			format_to_use,	//specify what the data is inside the tga file
			GL_UNSIGNED_BYTE,	//specify data type of texture data in tga file
			tgaFile.data);
	free(tgaFile.data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_base_level); //(each mipmap is half the dimension of the previous. so we go from 512->256->128..etc.)
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	return 1;
}

void SetupBushSamplers(void)
{
	glGenSamplers(1, &g_bush_branchtex_sampler);
	glGenSamplers(1, &g_bush_trunktex_sampler);

	glSamplerParameteri(g_bush_branchtex_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(g_bush_branchtex_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glSamplerParameteri(g_bush_branchtex_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(g_bush_branchtex_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glSamplerParameteri(g_bush_trunktex_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(g_bush_trunktex_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glSamplerParameteri(g_bush_trunktex_sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(g_bush_trunktex_sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void ClipTilesSetupFrustum(void)
{
	float r;
	float l;
	float t;
	float b;
	float ltn[4];
	float lbn[4];
	float rtn[4];
	float rbn[4];
	float mTransform[16];
	float rotY;
	float mRotate[16];
	float mTranslate[16];
	
	//calculate right,left,bottom,top for the camera frustum (assume symmetric frustum)
	r = g_camera_frustum.fzNear/g_perspectiveMatrix[0];
	r = r/0.5f; //open the sides of the frustum up so that more tiles along the edges are included.
	t = g_camera_frustum.fzNear/g_perspectiveMatrix[5];
	l = -1.0f*r;
	b = -1.0f*t;
	ltn[0] = l;
	ltn[1] = t;
	ltn[2] = g_camera_frustum.fzNear;
	ltn[3] = 1.0f;
	lbn[0] = l;
	lbn[1] = b;
	lbn[2] = g_camera_frustum.fzNear;
	lbn[3] = 1.0f;
	rtn[0] = r;
	rtn[1] = t;
	rtn[2] = g_camera_frustum.fzNear;
	rtn[3] = 1.0f;
	rbn[0] = r;
	rbn[1] = b;
	rbn[2] = g_camera_frustum.fzNear;
	rbn[3] = 1.0f;
	
	//calculate the position of the camera
	rotY = -1.0f*g_camera_rotY;
	g_camera_frustum.camera[0] = -1.0f*g_camera_pos[0];
	g_camera_frustum.camera[1] = -1.0f*g_camera_pos[1];
	g_camera_frustum.camera[2] = -1.0f*g_camera_pos[2];
	
	//form a transform matrix using the camera's position
	mmTranslateMatrix(mTranslate, 
		g_camera_frustum.camera[0],
		g_camera_frustum.camera[1],
		g_camera_frustum.camera[2]);
	mmRotateAboutY(mRotate, rotY);
	//mmMultiplyMatrix4x4(mRotate, mTranslate, mTransform);
	mmMultiplyMatrix4x4(mTranslate, mRotate, mTransform);
	
	//transform the near plane vertices to the camera's world position
	mmTransformVec(mTransform, ltn);
	mmTransformVec(mTransform, lbn);
	mmTransformVec(mTransform, rtn);
	mmTransformVec(mTransform, rbn);
	
	//calculate the planes
	vGetPlaneNormal(g_camera_frustum.camera, rtn, rbn, g_camera_frustum.right_normal);
	vGetPlaneNormal(g_camera_frustum.camera, lbn, ltn, g_camera_frustum.left_normal);
}

/*
Returns 1 or 0 depending on if the Tile is in the frustum.
Requires ClipTilesSetupFrustum be called first
*/
int IsTileInCameraFrustum(struct lvl_1_terrain_struct * pTerrain, struct lvl_1_tile * pTile)
{
	float p[12];     //result of corners subtracted by camera pos
	float d[8];
	float * corner[4];
	int num_z;
	int num_x;
	int floats_per_vert;
	int i;
	
	//get the coordinates of the four corners of the tile from the tile map.
	num_z = pTile->num_z;
	num_x = pTile->num_x;
	floats_per_vert = pTerrain->num_floats_per_vert;
	corner[0] = pTile->pPos; //origin corner
	corner[1] = pTile->pPos+((num_x-1)*floats_per_vert); //+x, z=0 corner
	corner[2] = pTile->pPos+((num_z-1)*num_x*floats_per_vert); //x=0, +z corner
	corner[3] = pTile->pPos+(((num_x*num_z)-1)*floats_per_vert); //+x,+z corner
	
	//prepare the corner for checking if it is inside the frustum.
	vSubtract(p, corner[0], g_camera_frustum.camera);
	vSubtract((p+3), corner[1], g_camera_frustum.camera);
	vSubtract((p+6), corner[2], g_camera_frustum.camera);
	vSubtract((p+9), corner[3], g_camera_frustum.camera);
	
	//calculate dot products for the left plane
	d[0] = vDotProduct(p, g_camera_frustum.left_normal);
	d[1] = vDotProduct((p+3), g_camera_frustum.left_normal);
	d[2] = vDotProduct((p+6), g_camera_frustum.left_normal);
	d[3] = vDotProduct((p+9), g_camera_frustum.left_normal);
	
	//calculate dot products for the right plane
	d[4] = vDotProduct(p, g_camera_frustum.right_normal);
	d[5] = vDotProduct((p+3), g_camera_frustum.right_normal);
	d[6] = vDotProduct((p+6), g_camera_frustum.right_normal);
	d[7] = vDotProduct((p+9), g_camera_frustum.right_normal);
	
	//check to see if corners are in frustum. All d > 0 then in frustum
	for(i = 0; i < 4; i++)
	{
		if((d[i] > 0.0f) && (d[(i+4)] > 0.0f))
			return 1; //indicate tile is in frustum
	}
	return 0; //all corners are out of frustum, don't draw
}

/*
This function initializes the members of the bush_group
structure passed in.
*/
int InitBushGroup(struct bush_group * p_group)
{
	p_group->max_pos = 100;
	
	p_group->pPos = (float*)malloc(p_group->max_pos*3*sizeof(float));
	if(p_group->pPos == 0)
	{
		printf("InitBushGroup: malloc() failed for positions.\n");
		return -1;
	}
	
	//initialize the first position because i_pos will be set to 0
	p_group->pPos[0] = 10.0f;
	p_group->pPos[1] = -3019.0f;
	p_group->pPos[2] = 0.0f;
	
	p_group->i_pos = 0;
	return 1;
}

/*
Fills out data for a bush tile in *p_tile using a single bush in *single_bush. Places
everything offset of v3_origin.
-assumes that single_bush has been loaded and already has textures.
-assumes g_dots_tgas has already been initialized.
*/
int MakeDetailedBushTile(struct plant_billboard * p_tile, struct plant_billboard * single_bush, float * v3_origin, int dot_tga_file_index)
{
	int		r;
	int		i,j;
	int		c,o; //index variables for going through pixels in the dots.tga, o is row
	int		num_dots = 0; //count used to allocate memory for VBOs
	int		num_dots_set = 0; //counts dots used to plant vegetation
	int		alloc_bytes;
	int		row,col; //pixel row & column in picture
	int		i_col_start, i_col_end;
	int		i_row_start, i_row_end;
	int             c_lkup, o_lkup; //index variables used to actually read the dots.tga
	unsigned char * p_pixel;
	float		input_point[3];
	float		surface_point[3];
	float		v3_normal[3];
	int		element_bias = 0;
	float	      * p_temp_bush;
	int           * p_temp_element;
	int		i_bush = 0;
	float           angle_buffer[2];
	int		i_angle_buffer = 0;
	float		rand_angle;
	float           rot_mat[16];
	float           pos_vec[4];
	float		tile_size[2]; //size of a vegetation tile
	struct timespec t_start, t_end, t_elapsed;
	
	//test this with a small tile size
	tile_size[0] = 30.0f;
	tile_size[1] = 30.0f;

	clock_gettime(CLOCK_MONOTONIC, &t_start);

	//setup the index variables to control how the dots tga will
	//be iterated
	i_col_start = (int)((v3_origin[0]/g_dots_tgas.size[0])*512.0f);
	i_col_end = (int)(((v3_origin[0]+tile_size[0])/g_dots_tgas.size[0])*512.0f);
	i_row_start = (int)((v3_origin[2]/g_dots_tgas.size[1])*512.0f);
	i_row_end = (int)(((v3_origin[2]+tile_size[1])/g_dots_tgas.size[1])*512.0f);

	//loop through area of dots.tga to count dots for memory allocation
	for(o = i_row_start; o < i_row_end; o++)
	{
		o_lkup = o % 512;
		for(c = i_col_start; c < i_col_end; c++)
		{
			c_lkup = c % 512;
			p_pixel = (g_dots_tgas.data[dot_tga_file_index]+(((o_lkup*512)+c_lkup)*g_dots_tgas.components[dot_tga_file_index]) );
			if(p_pixel[0] != 0 && p_pixel[1] != 0 && p_pixel[2] != 0)
				continue;
			num_dots += 1;
		}
	}

	//allocate memory for bush tile vertex data (assume 3 floats pos + 3 floats normal-vector + 2 floats tex-coord
	p_tile->num_verts = num_dots*single_bush->num_verts;
	alloc_bytes = p_tile->num_verts*8*sizeof(float);
	p_tile->p_vertex_data = (float*)malloc(alloc_bytes);
	if(p_tile->p_vertex_data == 0)
	{
		printf("MakeDetailedBushTile: error. could not allocate %d bytes for vertex data.\n", alloc_bytes);
		r = -1;
		goto cleanup;
	}
	printf("allocated %d bytes for %d vertices plant tile vertex data.\n", alloc_bytes, p_tile->num_verts);
	
	//allocate memory for bush tile indices
	p_tile->num_indices = num_dots*(single_bush->num_indices); 
	alloc_bytes = p_tile->num_indices*sizeof(int);
	p_tile->p_elements = (GLint*)malloc(alloc_bytes);
	if(p_tile->p_elements == 0)
	{
		printf("MakeDetailedBushTile: error. could not allocate %d bytes for index data.\n", alloc_bytes);
		r = -1;
		goto cleanup;
	}
	
	//loop through area of dots.tga
	for(o = i_row_start; o < i_row_end; o++)
	{
		o_lkup = o % 512;
		for(c = i_col_start; c < i_col_end; c++)
		{
			c_lkup = c % 512;
			p_pixel = (g_dots_tgas.data[dot_tga_file_index]+(((o_lkup*512)+c_lkup)*g_dots_tgas.components[dot_tga_file_index]) );
			if(p_pixel[0] != 0 && p_pixel[1] != 0 && p_pixel[2] != 0)
				continue;
			num_dots_set += 1;
			input_point[0] = (((float)(c-i_col_start))/512.0f)*(90.0f);
			input_point[1] = 0.0f;
			input_point[2] = (((float)(o-i_row_start))/512.0f)*(90.0f);
			
			input_point[0] += v3_origin[0];
			input_point[2] += v3_origin[2];
			
			r = GetTileSurfPoint(input_point, //a vec3 to find the surface point of
			surface_point, //returns a vec3 of the surface point
			v3_normal);    //returns a vec3 of the normal at the surface point
			if(r == -1)
			{
				printf("MakeDetailedBushTile: error. trying to calculate surface at (%f,%f,%f) could not find tile.\n", input_point[0], input_point[1], input_point[2]);
				r = -1;
				goto cleanup;
			}
		
			//get a random Y-axis angle
			if(i_angle_buffer == 0) //check to see if we need to get more random angles
			{
				CalcGaussRandomPair(angle_buffer, (angle_buffer+1));
				i_angle_buffer = 2; //indicate that the angle buffer now has two angles
			}
			rand_angle = angle_buffer[(i_angle_buffer - 1)];
			angle_buffer[(i_angle_buffer - 1)] = 0.0f;
			i_angle_buffer -= 1;
			rand_angle *= 360.0f;
			mmRotateAboutY(rot_mat, rand_angle);
		
			//setup pointers for copying of data to bush tile from single_bush
			p_temp_bush = p_tile->p_vertex_data+(i_bush*single_bush->num_verts*8); //8 is # of floats in a vertex
		
			//translate bush data to surface point and fill it into the bush tile vertex data
			for(j = 0; j < single_bush->num_verts; j++)
			{
				//transform the bush position vertices
				pos_vec[0] = single_bush->p_vertex_data[(j*8)]; 
				pos_vec[1] = single_bush->p_vertex_data[(j*8)+1];
				pos_vec[2] = single_bush->p_vertex_data[(j*8)+2];
				pos_vec[3] = 1.0f;
				mmTransformVec(rot_mat, pos_vec);
			
				p_temp_bush[(j*8)] = pos_vec[0] + surface_point[0];
				p_temp_bush[(j*8)+1] = pos_vec[1] + surface_point[1];
				p_temp_bush[(j*8)+2] = pos_vec[2] + surface_point[2];
			
				p_temp_bush[(j*8)+3] = single_bush->p_vertex_data[(j*8)+3];
				p_temp_bush[(j*8)+4] = single_bush->p_vertex_data[(j*8)+4];
				p_temp_bush[(j*8)+5] = single_bush->p_vertex_data[(j*8)+5];
			
				p_temp_bush[(j*8)+6] = single_bush->p_vertex_data[(j*8)+6];
				p_temp_bush[(j*8)+7] = single_bush->p_vertex_data[(j*8)+7];
			}
		
			//copy over the bush elements
			p_temp_element = p_tile->p_elements+(i_bush*single_bush->num_indices);
			for(j = 0; j < single_bush->num_indices; j++)
			{
				p_temp_element[j] = element_bias+single_bush->p_elements[j]; //adjust index to account for bushes copied in previous iterations
			}

			//update element_bias for the next bush
			element_bias += single_bush->num_indices;
		
			i_bush++;
			}
	}
	
	//Check to make sure the number of dots we used to fill in the VBOs matches
	//the number of dots we counted to allocate memory
	if(num_dots != num_dots_set)
	{
		printf("MakeDetailedBushTile: dot counters don't match. num_dots=%d, num_dots_set=%d\n", num_dots, num_dots_set);
		r = -1;
		goto cleanup;
	}
	
	//setup the enumeration buffer
	glGenBuffers(1, &(p_tile->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_tile->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
		p_tile->num_indices*sizeof(GLint),
		p_tile->p_elements,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	//setup the vbo's & vao's
	glGenBuffers(1, &(p_tile->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_tile->vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(p_tile->num_verts*8*sizeof(float)),
		p_tile->p_vertex_data,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	glGenVertexArrays(1, &(p_tile->vao));
	glBindVertexArray(p_tile->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_tile->vbo);
	glEnableVertexAttribArray(0); //position
	glEnableVertexAttribArray(1); //normal vector
	glEnableVertexAttribArray(2); //texture-coords
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_tile->ebo); //save the GL_ELEMENT_ARRAY_BUFFER state in the vao
	glBindVertexArray(0);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//copy texture properties
	p_tile->texture_id = single_bush->texture_id;
	//p_tile->sampler = g_bush_branchtex_sampler;
	
	//Report the time it took:
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	GetElapsedTime(&t_start, &t_end, &t_elapsed);
	printf("MakeDetailedBushTile: elapsed time (s:ns) = %ld:%ld\n", t_elapsed.tv_sec, t_elapsed.tv_nsec);
	
	r = 1; //indicate success
cleanup:
	return r;
}

/*MakeSmipleBushTile() uses the simple billboard to make a tile of bushes*/
int MakeSimpleBushTile(struct simple_billboard * p_tile, struct simple_billboard * single_bush, float * v3_origin, int dot_tga_file_index)
{
	int		r;
	int		i,j;
	int		c,o; //index variables for going through pixels in the dots.tga, o is row
	int		num_dots = 0; //count used to allocate memory for VBOs
	int		num_dots_set = 0; //counts dots used to plant vegetation
	int		alloc_bytes;
	int		row,col; //pixel row & column in picture
	int		i_col_start, i_col_end;
	int		i_row_start, i_row_end;
	int             c_lkup, o_lkup; //index variables used to actually read the dots.tga
	unsigned char * p_pixel;
	float		input_point[3];
	float		surface_point[3];
	float		normal[3];
	float	      * p_temp_bush;
	int		i_bush = 0;
	float           angle_buffer[2];
	int		i_angle_buffer = 0;
	float		rand_angle;
	float           rot_mat[16];
	float           pos_vec[4];
	float		tile_size[2]; //size of a vegetation tile
	struct timespec t_start, t_end, t_elapsed;
	
	//test this with a small tile size
	tile_size[0] = 180.0f;
	tile_size[1] = 180.0f;

	clock_gettime(CLOCK_MONOTONIC, &t_start);

	//setup the index variables to control how the dots tga will
	//be iterated
	i_col_start = (int)((v3_origin[0]/g_dots_tgas.size[0])*512.0f);
	i_col_end = (int)(((v3_origin[0]+tile_size[0])/g_dots_tgas.size[0])*512.0f);
	i_row_start = (int)((v3_origin[2]/g_dots_tgas.size[1])*512.0f);
	i_row_end = (int)(((v3_origin[2]+tile_size[1])/g_dots_tgas.size[1])*512.0f);

	//loop through area of dots.tga to count dots for memory allocation
	for(o = i_row_start; o < i_row_end; o++)
	{
		o_lkup = o % 512;
		for(c = i_col_start; c < i_col_end; c++)
		{
			c_lkup = c % 512;
			p_pixel = (g_dots_tgas.data[dot_tga_file_index]+(((o_lkup*512)+c_lkup)*g_dots_tgas.components[dot_tga_file_index]) );
			if(p_pixel[0] != 0 && p_pixel[1] != 0 && p_pixel[2] != 0)
				continue;
			num_dots += 1;
		}
	}

	//allocate memory for bush tile vertex data (assume 3 floats pos + 2 floats tex-coord
	p_tile->num_verts = num_dots*single_bush->num_verts;
	alloc_bytes = p_tile->num_verts*5*sizeof(float);
	p_tile->p_vertex_data = (float*)malloc(alloc_bytes);
	if(p_tile->p_vertex_data == 0)
	{
		printf("MakeSimpleBushTile: error. could not allocate %d bytes for vertex data.\n", alloc_bytes);
		r = -1;
		goto cleanup;
	}
	//printf("allocated %d bytes for %d vertices plant tile vertex data.\n", alloc_bytes, p_tile->num_verts);
	
	//loop through area of dots.tga
	for(o = i_row_start; o < i_row_end; o++)
	{
		o_lkup = o % 512;
		for(c = i_col_start; c < i_col_end; c++)
		{
			c_lkup = c % 512;
			p_pixel = (g_dots_tgas.data[dot_tga_file_index]+(((o_lkup*512)+c_lkup)*g_dots_tgas.components[dot_tga_file_index]) );
			if(p_pixel[0] != 0 && p_pixel[1] != 0 && p_pixel[2] != 0)
				continue;
			num_dots_set += 1;
			input_point[0] = (((float)(c-i_col_start))/512.0f)*(90.0f);
			input_point[1] = 0.0f;
			input_point[2] = (((float)(o-i_row_start))/512.0f)*(90.0f);
			
			input_point[0] += v3_origin[0];
			input_point[2] += v3_origin[2];
			
			r = GetTileSurfPoint(input_point, //a vec3 to find the surface point of
			surface_point, //returns a vec3 of the surface point
			normal); 
			if(r == -1)
			{
				printf("MakeSimpleBushTile: error. trying to calculate surface at (%f,%f,%f) could not find tile.\n", input_point[0], input_point[1], input_point[2]);
				r = -1;
				goto cleanup;
			}
		
			//get a random Y-axis angle
			if(i_angle_buffer == 0) //check to see if we need to get more random angles
			{
				CalcGaussRandomPair(angle_buffer, (angle_buffer+1));
				i_angle_buffer = 2; //indicate that the angle buffer now has two angles
			}
			rand_angle = angle_buffer[(i_angle_buffer - 1)];
			angle_buffer[(i_angle_buffer - 1)] = 0.0f;
			i_angle_buffer -= 1;
			rand_angle *= 360.0f;
			mmRotateAboutY(rot_mat, rand_angle);
		
			//setup pointers for copying of data to bush tile from single_bush
			p_temp_bush = p_tile->p_vertex_data+(i_bush*single_bush->num_verts*5); //8 is # of floats in a vertex
		
			//translate bush data to surface point and fill it into the bush tile vertex data
			for(j = 0; j < single_bush->num_verts; j++)
			{
				//transform the bush position vertices
				pos_vec[0] = single_bush->p_vertex_data[(j*5)]; 
				pos_vec[1] = single_bush->p_vertex_data[(j*5)+1];
				pos_vec[2] = single_bush->p_vertex_data[(j*5)+2];
				pos_vec[3] = 1.0f;
				mmTransformVec(rot_mat, pos_vec);
			
				p_temp_bush[(j*5)] = pos_vec[0] + surface_point[0];
				p_temp_bush[(j*5)+1] = pos_vec[1] + surface_point[1];
				p_temp_bush[(j*5)+2] = pos_vec[2] + surface_point[2];
			
				p_temp_bush[(j*5)+3] = single_bush->p_vertex_data[(j*5)+3];
				p_temp_bush[(j*5)+4] = single_bush->p_vertex_data[(j*5)+4];
			}
		
			i_bush++;
		}
	}
	
	//Check to make sure the number of dots we used to fill in the VBOs matches
	//the number of dots we counted to allocate memory
	if(num_dots != num_dots_set)
	{
		printf("MakeSimpleBushTile: dot counters don't match. num_dots=%d, num_dots_set=%d\n", num_dots, num_dots_set);
		r = -1;
		goto cleanup;
	}
	
	//setup the vbo's & vao's
	glGenBuffers(1, &(p_tile->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_tile->vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(p_tile->num_verts*5*sizeof(float)),
		p_tile->p_vertex_data,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	glGenVertexArrays(1, &(p_tile->vao));
	glBindVertexArray(p_tile->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_tile->vbo);
	glEnableVertexAttribArray(0); //position
	glEnableVertexAttribArray(1); //tex coords
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glBindVertexArray(0);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//copy texture properties
	//TODO: Need to fix this. idk what is going on on the left side of eqn anymore.
	p_tile->texture_ids[0] = single_bush->texture_ids[0];
	//p_tile->sampler = g_bush_branchtex_sampler;
	p_tile->size[0] = single_bush->size[0];
	p_tile->size[1] = single_bush->size[1];
	
	//Report the time it took:
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	GetElapsedTime(&t_start, &t_end, &t_elapsed);
	//printf("MakeSimpleBushTile: elapsed time (s:ns) = %ld:%ld\n", t_elapsed.tv_sec, t_elapsed.tv_nsec);
	
	r = 1; //indicate success
cleanup:
	return r;
}

/*
Calculates the elapsed time between end and start and returns result.
*/
void GetElapsedTime(struct timespec * start, struct timespec * end, struct timespec * result)
{
	time_t * p_larger_sec;
	time_t * p_smaller_sec;
	long * p_larger_nsec;
	long * p_smaller_nsec;
	
	//determine rollover
	if(start->tv_sec > end->tv_sec)
	{
		result->tv_sec = (0x7FFFFFFFFFFFFFFF - start->tv_sec) + end->tv_sec;//assume time_t is long because i'm an idiot
	}
	else
	{
		result->tv_sec = end->tv_sec - start->tv_sec;
	}
	
	//determine rollover
	if(end->tv_nsec < start->tv_nsec)
	{
		result->tv_sec -= 1; //carry the seconds
		result->tv_nsec = (1000000000 - start->tv_nsec) + end->tv_nsec;
	}
	else
	{
		result->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

char * LoadShaderSource(char * filename)
{
	FILE * pSrcFile;
	int shader_src_len;
	char * shader_source;
	int i;
	
	pSrcFile = fopen(filename,"r");
	if(pSrcFile == 0)
	{
		printf("LoadShaderSource: error. could not open %s\n", filename);
		return 0;
	}
	
	//count how big the vertex shader source is
	i = 0;
	while(feof(pSrcFile) == 0)
	{
		fgetc(pSrcFile);
		i++;
	}
	//this will make i one greater than the end of the file but this ok because
	//we will need to null-terminate the string anyway
	rewind(pSrcFile);
	shader_src_len = i;
	shader_source = (char*)malloc(shader_src_len);
	if(shader_source == 0)
	{
		printf("LoadShaderSource: error. malloc() failed for shader_source string.\n");
		fclose(pSrcFile);
		return 0;
	}
	i = 0;
	while(feof(pSrcFile) == 0)
	{
		shader_source[i] = (char)fgetc(pSrcFile);
		i++;
	}
	shader_source[(i-1)] = '\x00'; //null-terminate the string since it read one past the EOF
	fclose(pSrcFile);
	
	return shader_source;
}

/*
RAND_MAX: 2147483647
size of time_t: 8
tv_sec: 1408820898 to hex is 53F8E6A2h
*/

float RandomFloat(void)
{
	float conv_constant = (1.0f / (2147483647.0f));
	return ((float)rand())*conv_constant;
}

void CalcGaussRandomPair(float *a, float *b)
{
	float dMean = 0.0f;
	float dStdDeviation = 1.0f;
	float x1,x2,w,y1,y2;
	
	do
	{
		x1 = 2.0f * RandomFloat() - 1.0f;
		x2 = 2.0f * RandomFloat() - 1.0f;
		w = (x1*x1) + (x2*x2);
	}while(w >= 1.0f);
	
	w = sqrtf((-2.0f * logf(w)) / w);
	y1 = x1 * w;
	y2 = x2 * w;
	
	*a = (dMean + y1 * dStdDeviation);
	*b = (dMean + y2 * dStdDeviation);
}

/*
This function loads all dota.tga files (currently there are 2)
-Assumges dots.tga is 512x512
*/
int InitDotsTga(struct dots_struct * p_dots_info)
{
	char filenames[2][35] = {"./resources/textures/dots.tga",
		"./resources/textures/dots_pine.tga"};
	image_t tgaFile;
	unsigned char * p_pixel;
	int i,j;
	int r;
	
	p_dots_info->size[0] = 90.0f; //x length in world space
	p_dots_info->size[1] = 90.0f; //z length in world space
	p_dots_info->num_dot_tgas = 2;
	for(j = 0; j < p_dots_info->num_dot_tgas; j++)
	{
		r = LoadTga(filenames[j], &tgaFile);
		if(r == 0)
		{
			printf("InitDotsTga: error. could not open %s\n", filenames[j]);
			return -1;
		}
		
		//only conintue if dots.tga if 512x512 (this is assumed throughout)
		if(tgaFile.info.width != 512 || tgaFile.info.height != 512)
		{
			printf("InitDotsTga: error. %s is not 512x512\n", filenames[j]);
			return -1;
		}
		
		//save characteristics of the dots image
		p_dots_info->data[j] = tgaFile.data;
		p_dots_info->num_bytes[j] = tgaFile.info.bytes;
		p_dots_info->components[j] = tgaFile.info.components;
		p_dots_info->num_black_dots[j] = 0;
		p_dots_info->num_x[j] = 512;
		p_dots_info->num_z[j] = 512;
		
		//count the number of black dots
		for(i = 0; i < tgaFile.info.bytes; i += tgaFile.info.components)
		{
			p_pixel = (tgaFile.data+i);
			if(p_pixel[0] == 0 && p_pixel[1] == 0 && p_pixel[2] == 0)
				p_dots_info->num_black_dots[j] += 1;
		}
		
		if(p_dots_info->num_black_dots[j] == 0)
		{
			printf("InitDotsTga: error There were not any dots in %s\n", filenames[j]);
			return -1;
		}
		printf("%s has %d dots.\n", filenames[j], p_dots_info->num_black_dots[j]);
	}

	return 1;
}

/*
//Commented this out because it refers to old plant_tile struct
int InitPlantGrid(struct plant_grid * p_grid, struct dots_struct * p_dots_info, int dots_index, float plant_cluster_scale)
{
	int i,j;
	int i_cluster,j_cluster;
	int num_x;
	int num_z;
	int num_black_dots = 0;
	int r;
	int i_tile; //index for grid of plant tiles
	int i_plant[1521]; //indices for current plant index in a tile
	const int cluster_size = 100;
	unsigned char * p_pixel;
	unsigned char * p_cluster_pixel;
	float origin[2] = {8910.0f, 7920.0f};
	float cluster_origin[3];
	float grid_len = 24750.0f;
	float plant_position[3];
	float surface_pos[3];
	float v3_normal[3];
	//float t_mat[16]; //temporary matrix
	struct plant_tile * tile;
	
	//zero the i_plant[1521] array
	memset(i_plant, 0, (1521*sizeof(int)));
	
	p_grid->draw_grid[0] = -1;
	p_grid->draw_grid[1] = -1;
	p_grid->draw_grid[2] = -1;
	p_grid->draw_grid[3] = -1;
	p_grid->draw_grid[4] = -1;
	p_grid->draw_grid[5] = -1;
	p_grid->draw_grid[6] = -1;
	p_grid->draw_grid[7] = -1;
	p_grid->draw_grid[8] = -1;
	
	p_grid->num_tiles = 1521;
	p_grid->num_cols = 39;
	p_grid->num_rows = 39;
	p_grid->p_tiles = (struct plant_tile*)malloc(1521*sizeof(struct plant_tile));
	if(p_grid->p_tiles == 0)
	{
		printf("InitPlantGrid: error. malloc() failed for plant tiles.\n");
		return -1;
	}
	
	//Initialize the plant grid to known values //TODO: Fix the hard-coding of the map size
	for(i = 0; i < 39; i++)
	{
		for(j = 0; j < 39; j++)
		{
			tile = p_grid->p_tiles + ((i*39)+j);
			//tile->m16_model_mat = 0;
			tile->plant_pos_yrot_list = 0;
			tile->num_plants = 0;
			tile->urcorner[0] = j*10.0f;
			tile->urcorner[1] = i*10.0f;
		}
	}
	
	
	num_x = p_dots_info->num_x[dots_index];
	num_z = p_dots_info->num_z[dots_index];
	
	//loop through all pixels in the dots.tga and increment plant counts for tiles in the plant grid
	for(i = 0; i < num_z; i++)
	{
		for(j = 0; j < num_x; j++)
		{
			p_pixel = p_dots_info->data[dots_index] + (p_dots_info->components[dots_index]*((i*num_z)+j));
			
			//black pixel
			if(p_pixel[0] == 0 && p_pixel[1] == 0 && p_pixel[2] == 0)
			{
				num_black_dots += 1;
				
				//calculate position cluster of plants from position of the pixel in the dots.tga
				cluster_origin[0] = origin[0] + (((float)j)/((float)num_x))*grid_len;
				cluster_origin[1] = 0.0f; //this isn't used set to 0
				cluster_origin[2] = origin[1] + (((float)i)/((float)num_z))*grid_len;
				
				//now loop again through dots.tga and place individual plants using the dots.tga
				//again.
				for(i_cluster = 0; i_cluster < cluster_size; i_cluster++)
				{
					for(j_cluster = 0; j_cluster < cluster_size; j_cluster++)
					{
						p_cluster_pixel = p_dots_info->data[dots_index] + (p_dots_info->components[dots_index]*((i_cluster*num_z)+j_cluster));
						//only process black pixels
						if((i_cluster==0 && j_cluster==0) || (p_cluster_pixel[0] == 0 && p_cluster_pixel[1] == 0 && p_cluster_pixel[2] == 0))
						{
							plant_position[0] = cluster_origin[0] + (((float)j_cluster)/((float)num_x))*grid_len*plant_cluster_scale;
							plant_position[1] = 0.0f;
							plant_position[2] = cluster_origin[2] + (((float)i_cluster)/((float)num_z))*grid_len*plant_cluster_scale;
							
							i_tile = GetLvl1Tile(plant_position);
							if(i_tile == -1)
							{
								printf("InitPlantGrid: error. plant position (%f,%f,%f) not on map.\n",
									cluster_origin[0],
									cluster_origin[1],
									cluster_origin[2]);
								continue;
							}
							else
							{
								r = GetTileSurfPoint(plant_position, surface_pos, v3_normal);
								if(r == -1)
								{
									printf("InitPlantGrid: error. plant position (%f,%f,%f) not on grid.\n",
										plant_position[0],
										plant_position[1],
										plant_position[2]);
									continue;
								}
				
								//only draw plants above 0.0f height
								if(surface_pos[1] > 0.0f) 
								{
									//increment the plant count for that tile
									p_grid->p_tiles[i_tile].num_plants += 1;
								}
							}
						}
					}
				}
				
			}
		}
	}
	
	//allocate memory for the plants
	for(i = 0; i < 1521; i++)
	{
		tile = p_grid->p_tiles + i;
		
		//tile->m16_model_mat = (float*)malloc(tile->num_plants*16*sizeof(float));
		//if(tile->m16_model_mat == 0)
		//{
		//	printf("InitPlantGrid: malloc() failed at tile %d when allocating matrices\n", i);
		//	return -1;
		//}
		tile->plant_pos_yrot_list = (float*)malloc(tile->num_plants*4*sizeof(float)); //each plant has 4 floats associated with it. xyz pos and Y rotation.
		if(tile->plant_pos_yrot_list == 0)
		{
			printf("InitPlantGrid: malloc() failed at tile %d when allocating pos\n", i);
			return -1;
		}
	}
	
	//Now that we have memory allocated for the positions loop back again
	//loop through the dots.tga and calculate positions and matrices:
	for(i = 0; i < num_z; i++)
	{
		for(j = 0; j < num_x; j++)
		{
			p_pixel = p_dots_info->data[dots_index] + (p_dots_info->components[dots_index]*((i*num_z)+j));
			
			//black pixel
			if(p_pixel[0] == 0 && p_pixel[1] == 0 && p_pixel[2] == 0)
			{
				num_black_dots += 1;
				
				//calculate plant position from position of the pixel in the dots.tga
				cluster_origin[0] = origin[0] + (((float)j)/((float)num_x))*grid_len;
				cluster_origin[1] = 0.0f; //this isn't used set to 0
				cluster_origin[2] = origin[1] + (((float)i)/((float)num_z))*grid_len;

				for(i_cluster = 0; i_cluster < cluster_size; i_cluster++)
				{
					for(j_cluster = 0; j_cluster < cluster_size; j_cluster++)
					{
						p_cluster_pixel = p_dots_info->data[dots_index] + (p_dots_info->components[dots_index]*((i_cluster*num_z)+j_cluster));
						
						//only process black pixels and always put a bush at the first pixel
						if((i_cluster==0 && j_cluster==0) || (p_cluster_pixel[0] == 0 && p_cluster_pixel[1] == 0 && p_cluster_pixel[2] == 0))
						{
							plant_position[0] = cluster_origin[0] + (((float)j_cluster)/((float)num_x))*grid_len*plant_cluster_scale;
							plant_position[1] = 0.0f;
							plant_position[2] = cluster_origin[2] + (((float)i_cluster)/((float)num_z))*grid_len*plant_cluster_scale;
							
							i_tile = GetLvl1Tile(plant_position);
							if(i_tile == -1)
							{
								printf("InitPlantGrid: error. plant position (%f,%f,%f) not on map.\n",
									plant_position[0],
									plant_position[1],
									plant_position[2]);
								continue;
							}
							else
							{
								r = GetTileSurfPoint(plant_position, surface_pos, v3_normal);
								if(r == -1)
								{
									printf("InitPlantGrid: error. plant position (%f,%f,%f) not on grid.\n",
										plant_position[0],
										plant_position[1],
										plant_position[2]);
									continue;
								}
				
								//only draw plants above 0.0f height
								if(surface_pos[1] > 0.0f) 
								{
									//increment the plant count for that tile
									tile = p_grid->p_tiles + i_tile;
				
									//mmTranslateMatrix(t_mat, surface_pos[0], surface_pos[1], surface_pos[2]);
				
									//memcpy((tile->m16_model_mat + (16*i_plant[i_tile])), t_mat, (16*sizeof(float)));
									*(tile->plant_pos_yrot_list + (4*i_plant[i_tile]) + 0) = surface_pos[0];
									*(tile->plant_pos_yrot_list + (4*i_plant[i_tile]) + 1) = surface_pos[1];
									*(tile->plant_pos_yrot_list + (4*i_plant[i_tile]) + 2) = surface_pos[2];
									*(tile->plant_pos_yrot_list + (4*i_plant[i_tile]) + 3) = 0.0f;	//Y axis rotation. (yaw)
				
									i_plant[i_tile]++;
								}
							}
						}
					}
				}
				
			}
		}
	}
	
	//This error check isn't going to work because more bushes will be made then the initial point
	//error check. make sure that the number of black dots matched when the file was loaded.
	//if(num_black_dots != p_dots_info->num_black_dots[dots_index])
	//{
	//	printf("InitPlantGrid: error. processed %d dots but dots info said there were %d dots.\n", 
	//		num_black_dots,
	//		p_dots_info->num_black_dots[dots_index]);
	//	return -1;
	//}

	//success
	return 0;
};
*/

/*
InitPlantGrid2() is a different implementation of initializing the plant grid.
returns:
	-1	error occurred
	0	ok
*/
int InitPlantGrid2(struct plant_grid * p_grid)
{
	char * temp_plants_type_array=0;
	struct plant_tile * p_tile=0;
	float * temp_plants_pos_array=0;
	float rpos[3];
	float surf_pos[3];
	float v3_normal[3];
	int max_plants_per_tile = 1000;
	int num_plants;
	int i;
	int j;
	int k;
	int i_type; //plant type index
	int i_newPlant; //plant index in the new array
	int r;
	char temp_plant_type;

	p_grid->num_tiles = 1521; //TODO: Remove this hard-coded map info
	p_grid->num_cols = 39;
	p_grid->num_rows = 39;

	p_grid->p_tiles = (struct plant_tile*)malloc(p_grid->num_tiles*sizeof(struct plant_tile));
	if(p_grid->p_tiles == 0)
	{
		printf("%s: error. malloc fail.\n", __func__);
		return -1;
	}
	memset(p_grid->p_tiles, 0, (p_grid->num_tiles*sizeof(struct plant_tile)));

	temp_plants_pos_array = (float*)malloc(max_plants_per_tile*3*sizeof(float));
	if(temp_plants_pos_array == 0)
	{
		printf("%s: error. malloc fail.\n", __func__);
		return -1;
	}
	memset(temp_plants_pos_array, 0, (max_plants_per_tile*3*sizeof(float)));

	temp_plants_type_array = (char*)malloc(max_plants_per_tile*sizeof(char));
	if(temp_plants_type_array == 0)
	{
		printf("%s: error. malloc fail.\n", __func__);
		return -1;
	}
	memset(temp_plants_type_array, 0, max_plants_per_tile);

	p_grid->draw_grid[0] = -1;
	p_grid->draw_grid[1] = -1;
	p_grid->draw_grid[2] = -1;
	p_grid->draw_grid[3] = -1;
	p_grid->draw_grid[4] = -1;
	p_grid->draw_grid[5] = -1;
	p_grid->draw_grid[6] = -1;
	p_grid->draw_grid[7] = -1;
	p_grid->draw_grid[8] = -1;

	//initialize some distances for when to draw plants
	p_grid->nodraw_dist[0] = 50.0f; //bush
	p_grid->nodraw_dist[1] = 2000.0f; //palm
	p_grid->nodraw_dist[2] = 50.0f; //scaevola
	p_grid->nodraw_dist[3] = 50.0f; //fake pemphis
	p_grid->nodraw_dist[4] = 50.0f; //tourne fortia
	p_grid->nodraw_dist[5] = 2000.0f; //ironwood

	//terrain map has tiles ordered row major
	for(i = 0; i < p_grid->num_cols; i++)
	{
		for(j = 0; j < p_grid->num_cols; j++)
		{
			p_tile = p_grid->p_tiles + (i*p_grid->num_cols) + j;
			
			//set the corner towards the origin (upper-right...yea idk)
			//each tile is 990.0f x 990.0f
			p_tile->urcorner[0] = j*990.0f;
			p_tile->urcorner[1] = i*990.0f;

			num_plants = 0;

			//try to get positions for at most max_plants_per_tile
			for(k = 0; k < max_plants_per_tile; k++)
			{
				//get a random x,z vector in the tile
				rpos[0] = (float)(rand() % 990);
				rpos[1] = 0.0f;
				rpos[2] = (float)(rand() % 990);

				//add the offset from the origin, to get the global coordinates
				rpos[0] += p_tile->urcorner[0];
				rpos[2] += p_tile->urcorner[1];
				
				r = GetTileSurfPoint(rpos, surf_pos, v3_normal);
				if(r == -1)
				{
					printf("%s: error plant pos x=%f z=%f not on terrain grid.\n", __func__, rpos[0], rpos[2]);
					continue;
				}
				if(surf_pos[1] > 0.0f)
				{
					r = GenRandomPlantType(surf_pos, &temp_plant_type);
					if(r == 1)
					{
						temp_plants_type_array[num_plants] = temp_plant_type;
						temp_plants_pos_array[(num_plants*3)] = surf_pos[0];
						temp_plants_pos_array[(num_plants*3)+1] = surf_pos[1];
						temp_plants_pos_array[(num_plants*3)+2] = surf_pos[2];
						num_plants += 1;
					}
				}
			}

			//num_plants now has the # of positions in the temp_plant_pos_array
			//allocate memory for the plant positions and place it in a plant_tile struct
			p_tile->num_plants = num_plants;

			if(num_plants == 0)
			{
				p_tile->plants = 0;
				continue;
			}
		
			p_tile->plants = (struct plant_info_struct*)malloc(num_plants*sizeof(struct plant_info_struct));
			if(p_tile->plants == 0)
			{
				printf("%s: malloc file on tile (%d,%d)\n", __func__, i, j);
				return -1;
			}
			memset(p_tile->plants, 0, num_plants*sizeof(struct plant_info_struct));

			//now loop through the temp_plants_pos_array but organize the different plant types together.
			//so loop through the plants array looking for a plant type each iteration.
			i_newPlant = 0;
			for(i_type = 0; i_type < 6; i_type++) //there are 6 plant types
			{
				for(k = 0; k < num_plants; k++)
				{
					if(temp_plants_type_array[k] == i_type) //if this is the plant type we are looking for copy it
					{
						p_tile->plants[i_newPlant].pos[0] = temp_plants_pos_array[(k*3)];
						p_tile->plants[i_newPlant].pos[1] = temp_plants_pos_array[(k*3)+1];
						p_tile->plants[i_newPlant].pos[2] = temp_plants_pos_array[(k*3)+2];
						p_tile->plants[i_newPlant].plant_type = temp_plants_type_array[k];
						p_tile->plants[i_newPlant].yrot = 360.0f * RandomFloat();
						i_newPlant += 1;
					}
				}
			}
			//check that i_newPlant matches the # of plants allocated for
			if(i_newPlant != num_plants)
			{
				printf("%s: error. plant allocated mismatch i_newPlant=%d num_plants=%d\n", __func__, i_newPlant, num_plants);
				return 0;
			}
		}
	}

	free(temp_plants_pos_array);
	return 0;
}

/*
This function fills in the draw_grid for plant tiles.
*/
void UpdatePlantDrawGrid(struct plant_grid * p_grid, int cam_tile, float * camera_pos)
{
	float * p_boundary=0;
	int i;
	const float detail_dist = 200.0f; 
	
	//setup camera boundaries
	p_grid->detail_boundaries[0] = camera_pos[0] - detail_dist;//-x
	p_grid->detail_boundaries[1] = camera_pos[0] + detail_dist;//+x
	p_grid->detail_boundaries[2] = camera_pos[2] - detail_dist;//-z
	p_grid->detail_boundaries[3] = camera_pos[2] + detail_dist;//+z

	//setup plant no draw boundaries
	for(i = 0; i < 6; i++)
	{
		p_boundary = p_grid->nodraw_boundaries + (i*4);
		p_boundary[0] = camera_pos[0] - p_grid->nodraw_dist[i]; //-x
		p_boundary[1] = camera_pos[0] + p_grid->nodraw_dist[i]; //+x
		p_boundary[2] = camera_pos[2] - p_grid->nodraw_dist[i]; //-z
		p_boundary[3] = camera_pos[2] + p_grid->nodraw_dist[i]; //+z
	}
	
	p_grid->draw_grid[4] = cam_tile;
	
	//if the cam_tile came back invalid (-1) 
	//then clear the draw_grid
	if(cam_tile == -1)
	{
		for(i = 0; i < 9; i++)
		{
			p_grid->draw_grid[i] = -1;
		}
	}
	
	p_grid->draw_grid[0] = cam_tile - 39 - 1;
	p_grid->draw_grid[1] = cam_tile - 39;
	p_grid->draw_grid[2] = cam_tile - 39 + 1;
	p_grid->draw_grid[3] = cam_tile - 1;
	p_grid->draw_grid[5] = cam_tile + 1;
	p_grid->draw_grid[6] = cam_tile + 39 - 1;
	p_grid->draw_grid[7] = cam_tile + 39;
	p_grid->draw_grid[8] = cam_tile + 39 + 1;
	
	//now check for tiles that are out of range
	for(i = 0; i < 9; i++)
	{
		if((p_grid->draw_grid[i] < 0) || (p_grid->draw_grid[i] > 1521))
			p_grid->draw_grid[i] = -1;
	}
}

void UpdateTerrainDrawBox(float * camera_pos)
{
	g_big_terrain.nodraw_boundaries[0] = camera_pos[0] - g_big_terrain.nodrawDist; //-x
	g_big_terrain.nodraw_boundaries[1] = camera_pos[0] + g_big_terrain.nodrawDist; //+x
	g_big_terrain.nodraw_boundaries[2] = camera_pos[2] - g_big_terrain.nodrawDist; //-z
	g_big_terrain.nodraw_boundaries[3] = camera_pos[2] + g_big_terrain.nodrawDist; //+z
}

int IsTileInTerrainDrawBox(struct lvl_1_tile * tile)
{
	float pos[3];

	//calculate center of tile
	pos[0] = tile->urcorner[0] + 495.0f; //990/2
	pos[1] = 0.0f;
	pos[2] = tile->urcorner[1] + 495.0f; //990/2

	if(pos[0] < g_big_terrain.nodraw_boundaries[1] //+x
			&& pos[0] > g_big_terrain.nodraw_boundaries[0] //-x
			&& pos[2] < g_big_terrain.nodraw_boundaries[3] //+z
			&& pos[2] > g_big_terrain.nodraw_boundaries[2]) //-z
	{
		return 1;
	}
	return 0;
}

int MakeSimpleWaveVBO(struct simple_wave_vbo_struct * p_wave)
{
	//The y coord is the water level which is set to 0.0
	float init_vertex_data[] = { 
		-10000.0f, 0.0f,  10000.0f,
		 10000.0f, 0.0f,  10000.0f,
		 10000.0f, 0.0f, -10000.0f,
		-10000.0f, 0.0f,  10000.0f,
		 10000.0f, 0.0f, -10000.0f,
		-10000.0f, 0.0f, -10000.0f
	};
	
	p_wave->num_verts = 6;
	p_wave->p_vertex_data = (float*)malloc(p_wave->num_verts*3*sizeof(float));
	if(p_wave->p_vertex_data == 0)
	{
		printf("MakeSimpleWaveVBO: malloc() failed for vertex data.\n");
		return -1;
	}
	memcpy(p_wave->p_vertex_data, init_vertex_data, (p_wave->num_verts*3*sizeof(float)));
	
	//setup vbo
	glGenBuffers(1, &(p_wave->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_wave->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p_wave->num_verts*3*sizeof(float)), p_wave->p_vertex_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//setup vao
	glGenVertexArrays(1, &(p_wave->vao));
	glBindVertexArray(p_wave->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_wave->vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	printf("done loading simple wave plane.\n");	
	return 1;
}

/*
This function is a placeholder for where the simulation code will be, where the physics
code will be.
*/
void SimulationStep(void)
{
	int i;

	UpdateSoldierAI(&g_ai_soldier);

	//update player rot with 
	UpdateLocalPlayerDirection();

	for(i = 0; i < g_soldier_list.num_soldiers; i++)
	{
		UpdateCharacterSimulation(g_soldier_list.ptrsToCharacters[i]);
	}

	UpdateVehicleSimulation2(&g_b_vehicle);

	//update the camera based on the local player
	UpdateCameraForCharacter(g_camera_pos,	//inverse camera pos, 
		g_ws_camera_pos,					//world-space camera pos
		&g_a_man);							//triangle man instance

	//update the camera for 3rd person if needed
	if(g_keyboard_state.state == KEYBOARD_MODE_PLAYER && g_camera_state.playerCameraMode == PLAYERCAMERA_TP)
	{
		UpdateCameraForThirdPerson(&g_a_man, 
				g_camera_pos,	//icamera pos
				g_ws_camera_pos, 	//ws camera pos
				&g_camera_rotY, 	//icamera rot Y
				&g_camera_rotX);	//icamera rot X
	}

	g_simulation_step += 1; 
	//printf("***\n");
}

/*
Updates the global debug stats based on time around the draw call
*/
void DebugUpdateDrawCallStats(struct timespec * diff, struct timespec * tdrawStart, struct timespec * tdrawEnd)
{
	struct timespec tdrawDuration;

	GetElapsedTime(tdrawStart, tdrawEnd, &tdrawDuration);

	if(g_debug_stats.reset_flag)
	{
		g_debug_stats.reset_flag = 0;
		g_debug_stats.min_drawcall_period.tv_sec = diff->tv_sec;
		g_debug_stats.min_drawcall_period.tv_nsec = diff->tv_nsec;
		g_debug_stats.max_drawcall_period.tv_sec = diff->tv_sec;
		g_debug_stats.max_drawcall_period.tv_nsec = diff->tv_nsec;

		g_debug_stats.min_drawcall_duration.tv_sec = tdrawDuration.tv_sec;
		g_debug_stats.min_drawcall_duration.tv_nsec = tdrawDuration.tv_nsec;
		g_debug_stats.max_drawcall_duration.tv_sec = tdrawDuration.tv_sec;
		g_debug_stats.max_drawcall_duration.tv_nsec = tdrawDuration.tv_nsec;
	}
	
	//check to see if the diff is now the greatest drawcall period so far
	if( (diff->tv_sec > g_debug_stats.max_drawcall_period.tv_sec)
		|| ((diff->tv_sec == g_debug_stats.max_drawcall_period.tv_sec) && (diff->tv_nsec > g_debug_stats.max_drawcall_period.tv_nsec)))
	{
		g_debug_stats.max_drawcall_period.tv_sec = diff->tv_sec;
		g_debug_stats.max_drawcall_period.tv_nsec = diff->tv_nsec;
		g_debug_stats.step_at_max_drawcall = g_simulation_step;
	}
	
	//check to see if the diff is the lowest drawcall period so far
	if((diff->tv_sec < g_debug_stats.max_drawcall_period.tv_sec)
	 || (diff->tv_sec == g_debug_stats.min_drawcall_period.tv_sec && diff->tv_nsec < g_debug_stats.min_drawcall_period.tv_nsec))
	{
		g_debug_stats.min_drawcall_period.tv_sec = diff->tv_sec;
		g_debug_stats.min_drawcall_period.tv_nsec = diff->tv_nsec;
		g_debug_stats.step_at_min_drawcall = g_simulation_step;
	}

	//check to see if the draw duration is the greatest seen
   if((tdrawDuration.tv_sec > g_debug_stats.max_drawcall_duration.tv_sec) 
		|| (tdrawDuration.tv_sec == g_debug_stats.max_drawcall_duration.tv_sec && (tdrawDuration.tv_nsec > g_debug_stats.max_drawcall_duration.tv_nsec)))	
   {
	   g_debug_stats.max_drawcall_duration.tv_sec = tdrawDuration.tv_sec;
	   g_debug_stats.max_drawcall_duration.tv_nsec = tdrawDuration.tv_nsec;
	   g_debug_stats.step_atMaxDrawDuration = g_simulation_step;
   }

	//check to see if the draw duration is the smallest seen
   if((tdrawDuration.tv_sec < g_debug_stats.max_drawcall_duration.tv_sec) 
		|| (tdrawDuration.tv_sec == g_debug_stats.max_drawcall_duration.tv_sec && (tdrawDuration.tv_nsec < g_debug_stats.max_drawcall_duration.tv_nsec)))	
   {
	   g_debug_stats.min_drawcall_duration.tv_sec = tdrawDuration.tv_sec;
	   g_debug_stats.min_drawcall_duration.tv_nsec = tdrawDuration.tv_nsec;
	   g_debug_stats.step_atMinDrawDuration = g_simulation_step;
   }

}

int InitCharacterCommon2(struct character_model_struct * character)
{
	FILE * pDebugFile=0;
	char full_tex_filename[128];
	char prefix_name[22] = "./resources/textures/";
	struct dae_model_info2 modelFileInfo;
	struct dae_texture_names_struct temp_texinfo;
	struct dae_polylists_struct polylist_data;	//TODO: Remove this debug struct
	struct dae_model_vert_data_struct vert_data; //TODO: Remove this debug struct
	float * mat4=0;
	float rot_dae_to_gl[16];
	float rot_gl_to_dae[16];
	image_t tgaFile;
	int num_anims_in_file=0;
	int i;
	int j;
	int r;

	r = Load_DAE_CustomTextureNames(&temp_texinfo);
	if(r != 0)
	{
		printf("%s: error. Load_DAE_CustomTextureNames() fail.\n", __func__);
		return 0;
	}

	r = Load_DAE_CustomBinaryModel("soldier.dat", &modelFileInfo);
	if(r != 0)
	{
		printf("%s: error. Load_DAE_CustomBinaryModel fail.\n", __func__);
		return 0;
	}

	//save model info in character_model_struct
	character->num_materials = modelFileInfo.num_materials;
	character->vert_data = modelFileInfo.vert_data;
	character->num_verts = modelFileInfo.num_verts;
	character->baseVertices = (int*)malloc(modelFileInfo.num_materials*sizeof(int));
	if(character->baseVertices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	character->num_indices = (int*)malloc(modelFileInfo.num_materials*sizeof(int));
	if(character->num_indices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < modelFileInfo.num_materials; i++)
	{
		character->baseVertices[i] = modelFileInfo.base_index_offsets[i];
		character->num_indices[i] = modelFileInfo.mesh_counts[i];
	}

	//make a second copy of the array to be used in bone animation
	character->new_vert_data = (float*)malloc(modelFileInfo.num_verts*8*sizeof(float)); //8 = 3 pos + 3 coord + 2 uv
	if(character->new_vert_data == 0)
	{
		printf("%s: malloc failed for new_vert_data\n", __func__);
		return 0;
	}
	memcpy(character->new_vert_data, modelFileInfo.vert_data, (modelFileInfo.num_verts*8*sizeof(float)));

	//Setup EBO
	glGenBuffers(1, &(character->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, character->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,							//target buffer. use ebo
			(GLsizeiptr)(modelFileInfo.num_indices*sizeof(int)),	//size of data to go in ebo.
			modelFileInfo.vert_indices,								//address of data to put in ebo
			GL_STATIC_DRAW);										//usage hint.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	//Setup VBO
	glGenBuffers(1, &(character->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, character->vbo);
	glBufferData(GL_ARRAY_BUFFER,									//target buffer. use vbo
			(GLsizeiptr)(modelFileInfo.num_verts*8*sizeof(float)),	//3 pos + 3 normal + 2 texcoords
			modelFileInfo.vert_data,								//address of data to put in vbo
			GL_DYNAMIC_DRAW);										//usage hint
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	

	//setup the VAO
	glGenVertexArrays(1, &(character->vao));
	glBindVertexArray(character->vao);
	glBindBuffer(GL_ARRAY_BUFFER, character->vbo);
	glEnableVertexAttribArray(0);	//vertex position
	glEnableVertexAttribArray(1);	//vertex normal
	glEnableVertexAttribArray(2);	//vertex texture coordinates
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, character->ebo);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	

	//load textures
	character->textureIds = (GLuint*)malloc(modelFileInfo.num_materials*sizeof(GLuint));
	if(character->textureIds == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < modelFileInfo.num_materials; i++)
	{
		glGenTextures(1, &(character->textureIds[i]));
		glBindTexture(GL_TEXTURE_2D, character->textureIds[i]);

		strcpy(full_tex_filename, prefix_name);
		strcat(full_tex_filename, temp_texinfo.names[i]);

		r = LoadTga(full_tex_filename, &tgaFile);
		if(r == 0)
		{
			printf("%s: error. LoadTga() failed for %s\n", __func__, full_tex_filename);
			return 0;
		}

		//Check to make sure the tga file has no alpha, idk why but ok
		if(tgaFile.info.components != 3)
		{
			printf("%s: error %s is not RGB. It has %d channels.\n", full_tex_filename, tgaFile.info.components);
			return 0;
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D,
				0,
				GL_RGB,
				tgaFile.info.width,
				tgaFile.info.height,
				0,
				GL_RGB,
				GL_UNSIGNED_BYTE,
				tgaFile.data);
		free(tgaFile.data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 9);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	//Create sampler for textures
	glGenSamplers(1, &(character->sampler));
	glSamplerParameteri(character->sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(character->sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glSamplerParameteri(character->sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(character->sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//Now load bone animation structs. Soldier_converter_dae utility program reads whatever .dae files are
	//available and writes all animations found among those files to out_anim.dat
	r = Load_DAE_BinaryGetNumAnims("out_anim.dat", &num_anims_in_file);
	if(r != 0)
		return 0;
	//# of animations is hardcoded in character_model_struct at 7.
	if(num_anims_in_file != 23) //TODO: For now just assume we have a fixed # of animations. Need to update this to be more versatile.
	{
		printf("%s: error. Load_DAE_BinaryGetNumAnims() shows %d anims in file. Only 7 allowed.\n", __func__, num_anims_in_file);
		return 0;
	}

	r = Load_DAE_CustomBinaryBones("out_anim.dat", 	//filename of bone and animation data.
			&(character->testBones), 				//dae_model_bones_struct
			23, 										//size of dae_animation_struct array
			character->testAnim);				//addr of dae_animation_struct array
	if(r != 0)
	{
		printf("%s: Load_DAE_CustomBinaryBones() failed. r=%d\n", __func__, r);
		return 0;
	}
	r = DAE_CheckVertsForZeroWeight(&(character->testBones));
	if(r != 0)
		return 0;

	//Allocate arrays in the dae_model_bones_struct that aren't set
	//by the binary file.
	character->testBones.temp_bone_mat_array = (float*)malloc(character->testBones.num_bones*16*sizeof(float));
	if(character->testBones.temp_bone_mat_array == 0)
	{
		printf("%s: malloc failed for temp_bone_mat_array\n", __func__);
		return 0;
	}
	memset(character->testBones.temp_bone_mat_array, 0, (character->testBones.num_bones*16*sizeof(float)));

	character->final_bone_mat_array = (float*)malloc(character->testBones.num_bones*16*sizeof(float));
	if(character->final_bone_mat_array == 0)
	{
		printf("%s: malloc failed for final_bone_mat_array\n", __func__);
		return 0;
	}
	memset(character->final_bone_mat_array, 0, (character->testBones.num_bones*16*sizeof(float)));

	//Store some useful dimension information
	character->model_origin_foot_bias = 0.5523f;	//this value already has model scaling taken into account (the model in blender is larger). Move the movel up along the +Y axis to put in the origin at the foot.
	//character->model_origin_to_seat_bottom = 0.443534f; //this result in the guy being way up in the air
	character->model_origin_to_seat_bottom = 0.0958f;

	//p_character->walk_speed = 0.1f;
	character->walk_speed = 0.022f;
	character->crouch_speed = 0.0182f;
	character->crawl_speed = 0.004384f;
	//character->run_speed = 0.1329f; //this seems to fast, i got it from measure foot distance in run anim
	character->run_speed = 0.088f;

	return 1;
}

/*
params:
	-newPos, vec3 pos to set the new character at.
*/
int InitCharacterPerson(struct character_struct * p_character, float * newPos)
{
	int i;
	int r;

	//check to see if there is room to make a character
	if(g_soldier_list.num_soldiers >= g_soldier_list.max_soldiers)
	{
		printf("%s: error. soldier list full. current num_soldiers=%d\n", __func__, g_soldier_list.num_soldiers);
		return 0;
	}


	//put the man over land and in front of the camera
	memset(p_character, 0, sizeof(struct character_struct));
	//p_character->pos[0] = 12319.272461f;
	//p_character->pos[1] = 25.0f;
	//p_character->pos[2] = 11279.0f;
	p_character->pos[0] = newPos[0];
	p_character->pos[1] = newPos[1];
	p_character->pos[2] = newPos[2];
	
	p_character->vel[0] = 0.0f;
	p_character->vel[1] = 0.0f;
	p_character->vel[2] = 0.0f;
	p_character->accel[0] = 0.0f;
	p_character->accel[1] = 0.0f;
	p_character->accel[2] = 0.0f;
	p_character->walk_vel[0] = 0.0f;
	p_character->walk_vel[1] = 0.0f;
	p_character->walk_vel[2] = 0.0f;

	p_character->rotY = 0.0f;

	//save a pointer to the character_model_struct for ease of access to
	//common character information
	p_character->p_common = &g_triangle_man;

	r = InitPlayerInventory(p_character);
	if(r == 0)
		return 0;

	memset(&(p_character->anim), 0, sizeof(struct character_animation_struct));
	//p_character->anim.curAnim = CHARACTER_ANIM_STAND;
	//p_character->anim.flags |= ANIM_FLAGS_FREEZE;
	p_character->anim.curAnimFlags |= ANIM_FLAGS_REPEAT;
	p_character->anim.framesBtwnKeyframes = 8;
	p_character->anim.curFrame = 8;
	p_character->anim.curAnim = CHARACTER_ANIM_WALK;
	p_character->anim.iPrevKeyframe = 0;
	p_character->anim.idNextAnim = CHARACTER_ANIM_WALK;
	p_character->anim.iNextKeyframe = 1;
	p_character->biasY = p_character->p_common->model_origin_foot_bias; 
	p_character->flags = 0;
	p_character->events = 0;

	//setup the anim_flags_struct
	p_character->anim.stateFlags.position = ANIMSTATE_STAND;
	p_character->anim.stateFlags.speed = ANIMSTATE_SLOW;
	p_character->anim.stateFlags.hands = ANIMSTATE_EMPTY;
	p_character->anim.stateFlags.aim = ANIMSTATE_NOAIM;

	//Add the new soldier to the list of soldiers
	for(i = 0; i < g_soldier_list.max_soldiers; i++)
	{
		if(g_soldier_list.ptrsToCharacters[i] == 0) //slot is open add it
		{
			g_soldier_list.ptrsToCharacters[i] = p_character;
			g_soldier_list.num_soldiers += 1;
			break;
		}
	}

	//TODO: Remove this test, only call if 1st soldier
	if(g_soldier_list.num_soldiers == 0)
		UpdateCharacterBoneModel(p_character);
	
	return 1;
}

/*
Updates the character model VBO using the bones
*/
void UpdateCharacterBoneModel(struct character_struct * guy)
{
	struct character_model_struct * character=0;
	struct dae_animation_struct * anim=0;
	struct dae_animation_struct * next_anim=0;
	struct dae_model_bones_struct * bones=0;
	float temp_mat[16];
	float r_mat[16];
	float rot_mat3[9];
	float sum_vec[4];
	float in_vec[4];
	float new_vec[4];
	float interp;
	float weight_factor;
	int i_prev_keyframe;
	int i_next_keyframe;
	int i_child;
	int i;
	int j;

	character = guy->p_common;
	anim = &(character->testAnim[guy->anim.curAnim]);
	next_anim = &(character->testAnim[guy->anim.idNextAnim]);
	bones = &(character->testBones);

	//Calculate interpolation
	i_prev_keyframe = guy->anim.iPrevKeyframe;
	i_next_keyframe = guy->anim.iNextKeyframe;

	if(guy->anim.framesBtwnKeyframes != 0)
	{
		interp = ((float)(guy->anim.framesBtwnKeyframes - guy->anim.curFrame))/((float)(guy->anim.framesBtwnKeyframes));
	}
	else
	{
		interp = 0.0f;
	}

	//fill the new bone transforms with the identity mat4
	for(i = 0; i < bones->num_bones; i++)
	{
		mmMakeIdentityMatrix(bones->temp_bone_mat_array+(i*16));
	}

	//Calculate new bone transforms
	for(i = 0; i < bones->num_bones; i++)
	{
		for(j = 0; j < 16; j++)
		{
			r_mat[j] = (anim->bone_frame_transform_mat4_array+(i*anim->num_frames*16)+(i_prev_keyframe*16))[j];
			r_mat[j] = r_mat[j] + interp*((next_anim->bone_frame_transform_mat4_array+(i*16*next_anim->num_frames)+(i_next_keyframe*16))[j] - r_mat[j]);
		}

		//Make sure the calculated matrix is orthogonal
		mmConvertMat4toMat3(r_mat, rot_mat3);
		Orthonormalize(rot_mat3);
		FillInMat4withMat3(rot_mat3, r_mat);
		
		//if the bone doesn't have a parent use the armature's transform
		if(bones->bone_tree[i].parent == 0)
		{
			memcpy(bones->temp_bone_mat_array+(i*16), bones->armature_mat4, (16*sizeof(float)));
		}

		//now apply the bones transform
		mmMultiplyMatrix4x4((bones->temp_bone_mat_array+(i*16)), r_mat, (bones->temp_bone_mat_array+(i*16)));
		//mmMultiplyMatrix4x4(r_mat, (bones->temp_bone_mat_array+(i*16)), (bones->temp_bone_mat_array+(i*16)));

		//now place the bone's final transform in its children
		for(j = 0; j < bones->bone_tree[i].num_children; j++)
		{
			//calculate the child index using pointer arithmetic
			//TODO: Just change this to use the pointer .. no arithmetic needed??
			i_child = bones->bone_tree[i].children[j] - bones->bone_tree;

			//set the child's transform to the parent
			memcpy((bones->temp_bone_mat_array+(i_child*16)),
					(bones->temp_bone_mat_array+(i*16)),
					16*sizeof(float));
		}
	}

	//combine all matrix transforms into one
	for(i = 0; i < bones->num_bones; i++)
	{
		//order of transform: bind_shape_mat4 * inverse_bind_mat4_array[i] * temp_bone_mat_array[i]
		mmMultiplyMatrix4x4(bones->temp_bone_mat_array+(i*16), bones->inverse_bind_mat4_array+(i*16), temp_mat);
		mmMultiplyMatrix4x4(temp_mat, bones->bind_shape_mat4, character->final_bone_mat_array+(i*16));
	}

	//Calculate new vertex positions
	for(i = 0; i < character->num_verts; i++)
	{
		sum_vec[0] = 0.0f;
		sum_vec[1] = 0.0f;
		sum_vec[2] = 0.0f;
		sum_vec[3] = 1.0f;

		for(j = 0; j < bones->num_bones; j++)
		{
			weight_factor = bones->weight_array[(i*bones->num_bones)+j];
			if(weight_factor == 0.0f)
				continue;

			in_vec[0] = character->vert_data[(i*8)];
			in_vec[1] = character->vert_data[(i*8)+1];
			in_vec[2] = character->vert_data[(i*8)+2];
			in_vec[3] = 1.0f; //need this 1.0 here because vert_data is only vec3

			//All transforms are replaced by one that doesn't have memcpy

			//v * bind-shape-mat
			//mmTransformVec(bones->bind_shape_mat4, new_vec);
			//mmTransformVec4Out(bones->bind_shape_mat4, in_vec, new_vec); //in_vec and new_vec names are not helpful.

			//then * inverse-bind-pose-mat of the bone
			//mmTransformVec(bones->inverse_bind_mat4_array+(j*16), new_vec);
			//mmTransformVec4Out(bones->inverse_bind_mat4_array+(j*16), new_vec, in_vec); //ok the whole in_vec and new_vec is all screwedup now!

			//then * the transform of the joint
			//mmTransformVec((bones->temp_bone_mat_array+(j*16)), new_vec);
			//mmTransformVec4Out(bones->temp_bone_mat_array+(j*16), in_vec, new_vec); //watch out the names in_vec and new_vec are getting swapped around

			//This is a single matrix that handles the 3 transforms above
			mmTransformVec4Out(character->final_bone_mat_array+(j*16), in_vec, new_vec);

			//then * the weight
			new_vec[0] *= weight_factor;
			new_vec[1] *= weight_factor;
			new_vec[2] *= weight_factor;

			sum_vec[0] += new_vec[0];
			sum_vec[1] += new_vec[1];
			sum_vec[2] += new_vec[2];
		}

		character->new_vert_data[(i*8)] = sum_vec[0];
		character->new_vert_data[(i*8)+1] = sum_vec[1];
		character->new_vert_data[(i*8)+2] = sum_vec[2];
	}

	glBindBuffer(GL_ARRAY_BUFFER, character->vbo); //there are 2 vbos, select one.
	glBufferSubData(GL_ARRAY_BUFFER,
			0, //offset
			(character->num_verts*8*sizeof(float)),
			character->new_vert_data);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/*
Updates the physics for a character. The intent is that this
function is called once each physics frame.
-A character has flags and events. Input handling functions set events,
this function then looks at those events and sets state flags. So events
are like possible transitions, and flags represent the character's state.
-This function then calls UpdateCharacterAnimation() which looks at
events and flags and sets up the animation.
*/
void UpdateCharacterSimulation(struct character_struct * p_character)
{
	float surf_pos[3] = {0.0f,0.0f,0.0f};
	float new_pos[3];
	float surf_norm[3] = {0.0f,0.0f,0.0f};
	float surf_norm_mag;
	float vel_surf_comp[3];
	float temp_x;
	float temp_z;
	float guy_walk_vel[3] = {0.0f, 0.0f, 0.0f};
	float playerSpeed;
	int setStandEventNextFrame = 0;
	int needsWalkMovement = 0;
	int r;

	//If character is in vehicle don't do any other processing
	if(p_character->flags & CHARACTER_FLAGS_IN_VEHICLE)
	{
		//clear any other flags
		p_character->flags = CHARACTER_FLAGS_IN_VEHICLE;

		//don't process anything else because we are now in a car
	}
	else
	{
		//If we are aiming with the rifle clear any movement events, because aim rifle is supposed
		//to be stationary.
		if(p_character->anim.stateFlags.hands == ANIMSTATE_RIFLE && p_character->anim.stateFlags.aim == ANIMSTATE_AIM)
		{
			p_character->events &= ~(CHARACTER_FLAGS_WALK_FWD | CHARACTER_FLAGS_WALK_BACK | CHARACTER_FLAGS_WALK_LEFT | CHARACTER_FLAGS_WALK_RIGHT);
		}

		//Calculate player velocity
		if(p_character->events & CHARACTER_FLAGS_WALK_FWD)
		{
			GetXZComponents(1.0f, p_character->rotY, &temp_z, &temp_x, 0);
			guy_walk_vel[0] += temp_x;
			guy_walk_vel[2] += temp_z;
			needsWalkMovement = 1;
		}
		if(p_character->events & CHARACTER_FLAGS_WALK_BACK)
		{
			GetXZComponents(1.0f, p_character->rotY, &temp_z, &temp_x, 0);
			guy_walk_vel[0] -= temp_x;
			guy_walk_vel[2] -= temp_z;
			needsWalkMovement = 1;
		}
		if(p_character->events & CHARACTER_FLAGS_WALK_LEFT)
		{
			GetXZComponents(1.0f, (p_character->rotY+90.0f), &temp_z, &temp_x, 0);
			guy_walk_vel[0] += temp_x;
			guy_walk_vel[2] += temp_z;
			p_character->events |= CHARACTER_FLAGS_WALK_FWD; //this is to activate the walk animation.
			needsWalkMovement = 1;
		}
		if(p_character->events & CHARACTER_FLAGS_WALK_RIGHT)
		{
			GetXZComponents(1.0f, (p_character->rotY+90.0f), &temp_z, &temp_x, 0);
			guy_walk_vel[0] -= temp_x;
			guy_walk_vel[2] -= temp_z;
			p_character->events |= CHARACTER_FLAGS_WALK_FWD; //this is to activate the walk animation.
			needsWalkMovement = 1;
		}
		if(needsWalkMovement == 1)
		{
			//check to make sure we don't try to normalize 0,0,0 and get a nan
			if(guy_walk_vel[0] != 0.0f || guy_walk_vel[1] != 0.0f || guy_walk_vel[2] != 0.0f)
			{
				vNormalize(guy_walk_vel);
				playerSpeed = CharacterDetermineSpeed(p_character);
				guy_walk_vel[0] *= playerSpeed;
				guy_walk_vel[1] *= playerSpeed;
				guy_walk_vel[2] *= playerSpeed;
			}
		}
		p_character->walk_vel[0] = guy_walk_vel[0];
		p_character->walk_vel[1] = guy_walk_vel[1];	   	
		p_character->walk_vel[2] = guy_walk_vel[2];

		r = GetTileSurfPoint(p_character->pos, surf_pos, surf_norm);
		if(r == -1) ////we're not over land. Take away the on ground flag
		{
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_ON_GROUND);
		}
		
		//If we are on the ground calculate where we are:
		if(p_character->flags & CHARACTER_FLAGS_ON_GROUND)
		{
			//get the velocity components in the direction of the surface_normal.
			//invert the magnitude because we want a vector that points into 
			//the ground.
			surf_norm_mag = vDotProduct(p_character->walk_vel, surf_norm);
			vel_surf_comp[0] = -1.0f*surf_norm_mag*surf_norm[0];
			vel_surf_comp[1] = -1.0f*surf_norm_mag*surf_norm[1];
			vel_surf_comp[2] = -1.0f*surf_norm_mag*surf_norm[2];
			
			//subtract this vector out of the original velocity so
			//that the walk_velocity doesn't point into the ground
			p_character->walk_vel[0] -= vel_surf_comp[0];
			p_character->walk_vel[1] -= vel_surf_comp[1];
			p_character->walk_vel[2] -= vel_surf_comp[2];
			
			//calculate the new character position (y coordinate doesn't mean anything yet)
			new_pos[0] = p_character->pos[0] + p_character->walk_vel[0];
			new_pos[1] = p_character->pos[1] + p_character->walk_vel[1];
			new_pos[2] = p_character->pos[2] + p_character->walk_vel[2];
			
			//calculate the new y coordinate based on the new x,z pos
			r = GetTileSurfPoint(new_pos, surf_pos, surf_norm);
			if(r == 1)
			{
				p_character->pos[1] = surf_pos[1];
			}
			p_character->pos[0] = new_pos[0];
			p_character->pos[2] = new_pos[2];
		}
		else
		{
			//apply gravity
			//vel[1] -= 0.17f; // approximately 10 meters/sec / 60  (since there is 60 frames per second)
			p_character->vel[1] -= 0.001f;//TODO: Debug fix this. It is too slow right now.
			//check for collision with ground
			if((p_character->pos[1] + p_character->vel[1]) < surf_pos[1])
			{
				p_character->pos[1] = surf_pos[1];
				p_character->flags |= CHARACTER_FLAGS_ON_GROUND;
				p_character->vel[1] = 0.0f; //clear vertical velocity since we are on the ground.
			}
			else
			{
				p_character->pos[0] += p_character->walk_vel[0] + p_character->vel[0];
				p_character->pos[1] += p_character->walk_vel[1] + p_character->vel[1];
				p_character->pos[2] += p_character->walk_vel[2] + p_character->vel[2];
			}
		}
		
		//update the character's animation and process events.
		//Need to clear events that are not valid so the animation update function
		//doesn't try to start the animation.

		//Only let the 'aim' event to get to UpdateCharacterAnimation() if the we are
		//holding a gun
		if(!(p_character->anim.stateFlags.hands == ANIMSTATE_PISTOL || p_character->anim.stateFlags.hands == ANIMSTATE_RIFLE))
		{
			p_character->events &= (~CHARACTER_FLAGS_AIM);
		}

		//Set 'aim' state only if we have switched completely to the aim animation.
		if(p_character->anim.stateFlags.aim == ANIMSTATE_AIM)
		{
			p_character->flags |= CHARACTER_FLAGS_AIM;
		}
		else
		{
			p_character->flags &= (~CHARACTER_FLAGS_AIM);
		}

		//set the arm rifle and pistol events based on the current anim flags:
		if(p_character->anim.stateFlags.hands == ANIMSTATE_PISTOL)
		{
			p_character->flags |= CHARACTER_FLAGS_ARM_PISTOL;
		}
		else
		{
			p_character->flags &= (~CHARACTER_FLAGS_ARM_PISTOL);
		}
		if(p_character->anim.stateFlags.hands == ANIMSTATE_RIFLE)
		{
			p_character->flags |= CHARACTER_FLAGS_ARM_RIFLE;
		}
		else
		{
			p_character->flags &= (~CHARACTER_FLAGS_ARM_RIFLE);
		}

		//Handle events associated with movement animations.
		if(p_character->events & CHARACTER_FLAGS_IDLE)
		{
			if(p_character->flags & CHARACTER_FLAGS_IDLE)
			{
				p_character->events &= (~CHARACTER_FLAGS_IDLE);
			}

			p_character->flags |= CHARACTER_FLAGS_IDLE;
			p_character->flags &= (~CHARACTER_FLAGS_WALK_FWD);
			p_character->flags &= (~CHARACTER_FLAGS_WALK_BACK);
			p_character->flags &= (~CHARACTER_FLAGS_RUN);
		}
		else if(p_character->events & CHARACTER_FLAGS_RUN)
		{
			//if we are already running clear the run event
			if(p_character->flags & CHARACTER_FLAGS_RUN)
				p_character->events &= (~CHARACTER_FLAGS_RUN);
		
			//if we get a run don't let a walk event thru	
			p_character->events &= (~CHARACTER_FLAGS_WALK_FWD);

			p_character->flags |= CHARACTER_FLAGS_RUN;
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_WALK_BACK);
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_IDLE);
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_WALK_FWD);	 
		}
		else if(p_character->events & CHARACTER_FLAGS_WALK_FWD)
		{
			if(p_character->flags & CHARACTER_FLAGS_WALK_FWD)
				p_character->events &= (~CHARACTER_FLAGS_WALK_FWD); //clear the event flag so the animation function doesnt try to re-start the animation that is already playing.

			//update flags
			p_character->flags |= CHARACTER_FLAGS_WALK_FWD;
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_WALK_BACK);
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_IDLE);
		}
		else if(p_character->events & CHARACTER_FLAGS_WALK_BACK)
		{
			if(p_character->flags & CHARACTER_FLAGS_WALK_BACK)
				p_character->events &= (~CHARACTER_FLAGS_WALK_BACK);
			
			//update flags
			p_character->flags |= CHARACTER_FLAGS_WALK_BACK;
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_WALK_FWD);
			p_character->flags = p_character->flags & (~CHARACTER_FLAGS_IDLE);
		}
		else //if no other events switch to stand.
		{
			//crappy way to signal that we need p_character->events to clear everything and set the stand event.
			setStandEventNextFrame = 1;
		}
	}
	
	UpdateCharacterAnimation(p_character);

	//clear events since they have all been processed.
	p_character->events = 0;
	if(setStandEventNextFrame == 1)
	{
		p_character->events = CHARACTER_FLAGS_IDLE;
	}
}

/*
This function looks at the character's state and determines the appropriate
animation.
*/
void UpdateCharacterAnimation(struct character_struct * p_character)
{
	struct dae_animation_struct * cur_anim=0;
	struct dae_animation_struct * next_anim=0;
	struct item_common_struct * p_rifle=&g_rifle_common;
	struct character_anim_flags_struct newAnimStateFlags;
	int newAnim; //variable stores what the next animation will be in some situations.
	int needsItemAnim=0; //flag when set indicates that a new item animation needs to be played.
	char newNextAnimFlags=0;

	//carry forward the old anim state flags
	memcpy(&newAnimStateFlags, &(p_character->anim.stateFlags), sizeof(struct character_anim_flags_struct));

	//Increment the frame in the animation
	if((p_character->anim.curAnimFlags & ANIM_FLAGS_FREEZE) == 0)
		p_character->anim.curFrame -= 1;

	//Handle 'carry rifle' and 'aim' events because they require an animation shift.
	if(p_character->events & CHARACTER_FLAGS_ARM_RIFLE)
	{
		//If we are already carrying the rifle switch to not carrying it.
		if(p_character->anim.stateFlags.hands == ANIMSTATE_RIFLE)
		{
			newAnimStateFlags.hands = ANIMSTATE_EMPTY;
		}
		else //if we aren't carrying a rifle switch to carrying it
		{
			newAnimStateFlags.hands = ANIMSTATE_RIFLE;

			//add a delayed flag to arm the rifle
			p_character->anim.delayedAddFlags.hands = ANIMSTATE_RIFLE;
			newNextAnimFlags |= ANIM_FLAGS_SET_DELAYEDADDFLAGS;
			p_character->anim.maskDelayedAddFlags = 4; //bit 2
		}
	
		//shift to the next animation.	
		needsItemAnim = 1;
	}

	//Handle 'carry pistol' event
	if(p_character->events & CHARACTER_FLAGS_ARM_PISTOL)
	{
		//If we already are holding the pistol, simply put it away because pistol doesn't require special animations
		if(p_character->anim.stateFlags.hands == ANIMSTATE_PISTOL && p_character->anim.stateFlags.aim == ANIMSTATE_NOAIM)
		{
			//if we are standing or walking. just clear the pistol
			newAnimStateFlags.hands = ANIMSTATE_EMPTY;

		}
		else //so we aren't holding the pistol. pull it out.
		{
			//We used to check to see if we needed to switch anim, but we won't anymore
			//because we will handle it later.
			//check to see if we are in an animation where the pistol can't be pulled out
			//immediately. In that case switch to an animation where we can pull out the pistol
			
			//add a delayed flag to arm the pistol
			p_character->anim.delayedAddFlags.hands = ANIMSTATE_PISTOL;
			newNextAnimFlags |= ANIM_FLAGS_SET_DELAYEDADDFLAGS;
			p_character->anim.maskDelayedAddFlags = 4; //bit 2
		}

		needsItemAnim = 1;
	}

	//Handle aiming event
	if(p_character->events & CHARACTER_FLAGS_AIM)
	{
		//if we are carrying a rifle
		if(p_character->anim.stateFlags.hands == ANIMSTATE_RIFLE)
		{
			if(p_character->anim.stateFlags.aim == ANIMSTATE_AIM)
			{	
				//switch to not aiming
				newAnimStateFlags.aim = ANIMSTATE_NOAIM;
			}
			else
			{
				//switch to aim for the rifle only if we are not moving:
				if(p_character->flags & CHARACTER_FLAGS_IDLE)
				{
					newAnimStateFlags.aim = ANIMSTATE_AIM;
				}
			}
		}

		//if we are carrying a pistol
		if(p_character->anim.stateFlags.hands == ANIMSTATE_PISTOL)
		{
			//if we are already aiming switch to not aiming
			if(p_character->anim.stateFlags.aim == ANIMSTATE_AIM)
			{
				//pistol doesn't require special animations so use normal ones.
				newAnimStateFlags.aim = ANIMSTATE_NOAIM;
			}
			else
			{
				//switch to aim
				newAnimStateFlags.aim = ANIMSTATE_AIM;
			}
		}

		needsItemAnim = 1;
	}

	//Handle stand event
	if(p_character->events & CHARACTER_FLAGS_STAND)
	{
		newAnimStateFlags.position = ANIMSTATE_STAND;
		needsItemAnim = 1;
	}

	//Handle crouch event
	if(p_character->events & CHARACTER_FLAGS_CROUCH)
	{
		newAnimStateFlags.position = ANIMSTATE_CROUCH;
		needsItemAnim = 1;
	}

	//Handle prone event
	if(p_character->events & CHARACTER_FLAGS_PRONE)
	{
		newAnimStateFlags.position = ANIMSTATE_CRAWL;
		needsItemAnim = 1;
	}

	//Look at character state figure out the correct animation
	if(p_character->flags & CHARACTER_FLAGS_IN_VEHICLE)
	{
		p_character->anim.curAnimFlags = ANIM_FLAGS_FREEZE;
		p_character->anim.nextAnimFlags = ANIM_FLAGS_FREEZE;
		//CharacterStartAnim(&(p_character->anim), CHARACTER_ANIM_VEHICLE, 8);
		p_character->anim.framesBtwnKeyframes = 0;
		p_character->anim.curFrame = 0;
		p_character->anim.iPrevKeyframe = 0;
		p_character->anim.iNextKeyframe = 1;
		p_character->anim.idNextAnim = CHARACTER_ANIM_VEHICLE;
		p_character->anim.curAnim = CHARACTER_ANIM_VEHICLE;
	}
	else if((p_character->events & CHARACTER_FLAGS_WALK_FWD) || (p_character->events & CHARACTER_FLAGS_RUN))
	{
		p_character->anim.nextAnimFlags = ANIM_FLAGS_REPEAT;

		//check to see if there is a run event as well
		if(p_character->events & CHARACTER_FLAGS_RUN)
		{
			newAnimStateFlags.speed = ANIMSTATE_RUN;
		}
		else
		{
			newAnimStateFlags.speed = ANIMSTATE_SLOW;
		}

		switch(newAnimStateFlags.position)
		{
		case ANIMSTATE_STAND:
			newAnim = CharacterDetermineUprightMoveAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CROUCH:
			newAnim = CharacterDetermineCrouchMoveAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CRAWL:
			newAnim = CharacterDetermineCrawlMoveAnim(p_character, newAnimStateFlags);
			break;
		//case ANIMSTATE_VEHICLE: //vehicle is handled in the if statement above
		//	break;
		}

		//if we needed delayed flags set the anim to apply the flags
		if(newNextAnimFlags & ANIM_FLAGS_SET_DELAYEDADDFLAGS)
		{
			p_character->anim.nextAnimFlags |= newNextAnimFlags;
			p_character->anim.animToAddDelayedFlags = newAnim;
		}
		else
		{
			memcpy(&(p_character->anim.stateFlags), &newAnimStateFlags, sizeof(struct character_anim_flags_struct));
		}

		CharacterStartAnim(&(p_character->anim), newAnim, 8);
		needsItemAnim = 0; //clear any special item anims pending.
	}
	else if(p_character->events & CHARACTER_FLAGS_WALK_BACK)
	{
		p_character->anim.nextAnimFlags = ANIM_FLAGS_REPEAT | ANIM_FLAGS_REVERSE;

		switch(newAnimStateFlags.position)
		{
		case ANIMSTATE_STAND:
			newAnim = CharacterDetermineUprightMoveAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CROUCH:
			newAnim = CharacterDetermineCrouchMoveAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CRAWL:
			newAnim = CharacterDetermineCrawlMoveAnim(p_character, newAnimStateFlags);
			break;
		//case ANIMSTATE_VEHICLE: //vehicle is handled in the if statement above
		//	break;
		}

		//if we needed delayed flags set the anim to apply the flags
		if(newNextAnimFlags & ANIM_FLAGS_SET_DELAYEDADDFLAGS)
		{
			p_character->anim.nextAnimFlags |= newNextAnimFlags;
			p_character->anim.animToAddDelayedFlags = newAnim;
		}
		else
		{
			memcpy(&(p_character->anim.stateFlags), &newAnimStateFlags, sizeof(struct character_anim_flags_struct));
		}

		CharacterStartAnim(&(p_character->anim), newAnim, 8);
		needsItemAnim = 0; //clear any special item anims pending
	}
	
	if(p_character->events & CHARACTER_FLAGS_IDLE 	//need to shift to idle anim
		|| needsItemAnim == 1)						//had some item related event and need to play that anim
	{
		//do stand animation
		p_character->anim.nextAnimFlags = ANIM_FLAGS_FREEZE;

		switch(newAnimStateFlags.position)
		{
		case ANIMSTATE_STAND:
			newAnim = CharacterDetermineStandAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CROUCH:
			newAnim = CharacterDetermineCrouchAnim(p_character, newAnimStateFlags);
			break;
		case ANIMSTATE_CRAWL:
			newAnim = CharacterDetermineCrawlAnim(p_character, newAnimStateFlags);
			break;
		//case ANIMSTATE_VEHICLE: //vehicle is handled in the if statement above
		//	break;
		}

		//if we needed delayed flags set the anim to apply the flags
		if(newNextAnimFlags & ANIM_FLAGS_SET_DELAYEDADDFLAGS)
		{
			p_character->anim.nextAnimFlags |= newNextAnimFlags;
			p_character->anim.animToAddDelayedFlags = newAnim;
		}
		else
		{
			memcpy(&(p_character->anim.stateFlags), &newAnimStateFlags, sizeof(struct character_anim_flags_struct));
		}

		CharacterStartAnim(&(p_character->anim), newAnim, 8);
	}

	//Check to see if the keyframes need to be updated
	if((p_character->anim.curAnimFlags & ANIM_FLAGS_FREEZE) == 0)
	{
		cur_anim = p_character->p_common->testAnim+(p_character->anim.curAnim);
		next_anim = p_character->p_common->testAnim+(p_character->anim.idNextAnim);

		//Are we at the next keyframe?
		if(p_character->anim.curFrame == -1) //we already drew the last frame of the animation sequence
		{
			//take the next anim keyframe and make it the prev keyframe
			p_character->anim.curAnim = p_character->anim.idNextAnim;
			p_character->anim.iPrevKeyframe = p_character->anim.iNextKeyframe;
			p_character->anim.curAnimFlags = p_character->anim.nextAnimFlags;

			//check to see if any delayed flags need to be activated
			if(p_character->anim.curAnimFlags & ANIM_FLAGS_SET_DELAYEDADDFLAGS)
			{
				//Is this the animation we are looking for?
				if(p_character->anim.curAnim == p_character->anim.animToAddDelayedFlags)
				{
					//add the flags to the character flags
					if(p_character->anim.maskDelayedAddFlags & 1) //bit 0 position
					{
						p_character->anim.stateFlags.position = p_character->anim.delayedAddFlags.position;
					}
					if(p_character->anim.maskDelayedAddFlags & 2) //bit 1 speed
					{
						p_character->anim.stateFlags.speed = p_character->anim.delayedAddFlags.speed;
					}
					if(p_character->anim.maskDelayedAddFlags & 4) //bit 2 hands
					{
						p_character->anim.stateFlags.hands = p_character->anim.delayedAddFlags.hands;
					}
					if(p_character->anim.maskDelayedAddFlags & 8) //bit 3 aim
					{
						p_character->anim.stateFlags.aim = p_character->anim.delayedAddFlags.aim;
					}

					//clear the delayed flags
					memset(&(p_character->anim.delayedAddFlags), 0, sizeof(struct character_anim_flags_struct));
					p_character->anim.animToAddDelayedFlags = -1; //set to invalid animation.
				}
				else
				{
					//this wasn't the anim we were looking for carry the anim flags delayed add
					//to the next animation keyframe
					p_character->anim.nextAnimFlags |= ANIM_FLAGS_SET_DELAYEDADDFLAGS;
				}
			}

			p_character->anim.iNextKeyframe += 1;

			//check if we are at the end of the animation sequence
			if(p_character->anim.iNextKeyframe == next_anim->num_frames)
			{
				p_character->anim.iPrevKeyframe = 0;
				p_character->anim.iNextKeyframe = 1;
				p_character->anim.curFrame = (next_anim->key_frame_times[p_character->anim.iNextKeyframe]-next_anim->key_frame_times[p_character->anim.iPrevKeyframe]);
				p_character->anim.framesBtwnKeyframes = p_character->anim.curFrame;
			}
			else //we are not at the end of the sequence.
			{
				p_character->anim.curFrame = (next_anim->key_frame_times[p_character->anim.iNextKeyframe]-next_anim->key_frame_times[p_character->anim.iPrevKeyframe]);
				p_character->anim.framesBtwnKeyframes = p_character->anim.curFrame;
			}
		}
	
	}

	//moved to draw scene
	//UpdateCharacterBoneModel(p_character);

}

/*
This function is a helper function that initializes the character_animation_struct to
a particular dae_animation_struct from character_model_struct (common data structure for characters)
*/
int CharacterStartAnim(struct character_animation_struct * characterAnim, int animToStart, int timeToNextKeyframe)
{
	characterAnim->curFrame = timeToNextKeyframe;//give some amount of time until next keyframe. 
	characterAnim->framesBtwnKeyframes = timeToNextKeyframe;
	characterAnim->idNextAnim = animToStart;
	characterAnim->iNextKeyframe = 0; //start at the first keyframe. Should probably have some logic that decides this.
	characterAnim->curAnimFlags &= (~ANIM_FLAGS_FREEZE);	//if we try to transition out of a STAND animation we wont be able to.

	return 1; //success
}

/*
This function is a helper function that given some animation flags figures out
what animation to play when the character is standing (and not moving)
*/
int CharacterDetermineStandAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
	{
		return CHARACTER_ANIM_STAND;
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
	{
		//Are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_STAND_AIMPISTOL;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			//if we are not aiming, there is no special animation for pistol, use stock stand animation
			return CHARACTER_ANIM_STAND;
		}
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
	{
		//are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_STAND_AIMRIFLE;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_STANDRIFLE;
		}
	}

	return 0;
}

int CharacterDetermineUprightMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.speed == ANIMSTATE_SLOW)
	{
		if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
		{
			return CHARACTER_ANIM_WALK;
		}
		else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
		{
			//are we aiming?
			if(newAnimStateFlags.aim == ANIMSTATE_AIM)
			{
				return CHARACTER_ANIM_WALK_AIMPISTOL;
			}
			else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
			{
				//we don't use anything special for the pistol
				return CHARACTER_ANIM_WALK;
			}
		}
		else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
		{
			//we can't aim while walking with rifle so return only one animation
			return CHARACTER_ANIM_WALKRIFLE;
		}
	}
	else if(newAnimStateFlags.speed == ANIMSTATE_RUN)
	{
		//we can only run if hands are empty so just do run
		return CHARACTER_ANIM_RUN;
	}

	return 0;
}

int CharacterDetermineCrouchAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
	{
		return CHARACTER_ANIM_CROUCH_STOP;
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
	{
		//Are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CROUCH_STOP_AIMPISTOL;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CROUCH_STOP;
		}
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
	{
		//Are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CROUCH_STOP_AIMRIFLE;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CROUCH_STOP_RIFLE;
		}
	}

	return 0;
}

int CharacterDetermineCrouchMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
	{
		return CHARACTER_ANIM_CROUCHWALK;
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
	{
		//are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CROUCHWALK_AIMPISTOL;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CROUCHWALK;
		}
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
	{
		//when moving the rifle can't be aimed
		return CHARACTER_ANIM_CROUCHWALK_RIFLE;
	}

	return 0;
}

int CharacterDetermineCrawlAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
	{
		return CHARACTER_ANIM_CRAWLSTOP;
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
	{
		//are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CRAWLSTOP_AIMPISTOL;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CRAWLSTOP;
		}
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
	{
		//are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CRAWLSTOP_AIMRIFLE;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CRAWLSTOP_RIFLE;
		}
	}

	return 0;
}

int CharacterDetermineCrawlMoveAnim(struct character_struct * p_character, struct character_anim_flags_struct newAnimStateFlags)
{
	if(newAnimStateFlags.hands == ANIMSTATE_EMPTY)
	{
		return CHARACTER_ANIM_CRAWL;
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_PISTOL)
	{
		//are we aiming?
		if(newAnimStateFlags.aim == ANIMSTATE_AIM)
		{
			return CHARACTER_ANIM_CRAWL_AIMPISTOL;
		}
		else if(newAnimStateFlags.aim == ANIMSTATE_NOAIM)
		{
			return CHARACTER_ANIM_CRAWL;
		}
	}
	else if(newAnimStateFlags.hands == ANIMSTATE_RIFLE)
	{
		//we can't aim the rifle while moving, so there's only one animation to choose from in this state
		return CHARACTER_ANIM_CRAWL_RIFLE;
	}

	return 0;
}

float CharacterDetermineSpeed(struct character_struct * p_character)
{
	float speed=0.0f;

	if(p_character->anim.stateFlags.position == ANIMSTATE_STAND)
	{
		if(p_character->anim.stateFlags.speed == ANIMSTATE_SLOW)
		{
			speed = p_character->p_common->walk_speed;
		}
		else if(p_character->anim.stateFlags.speed == ANIMSTATE_RUN)
		{
			speed = p_character->p_common->run_speed;
		}
	}
	else if(p_character->anim.stateFlags.position == ANIMSTATE_CROUCH)
	{
		speed = p_character->p_common->crouch_speed;
	}
	else if(p_character->anim.stateFlags.position == ANIMSTATE_CRAWL)
	{
		speed = p_character->p_common->crawl_speed;
	}

	return speed;
}

void CharacterGetEyeOffset(struct character_struct * p_character, float * offset)
{
	switch(p_character->anim.stateFlags.position)
	{
	case ANIMSTATE_CROUCH:
		offset[0] = 0.0f;
		offset[1] = 1.319f;
		offset[2] = 0.0f;
		break;
	case ANIMSTATE_CRAWL:
		offset[0] = 0.0f;
		offset[1] = 0.3228f;
		offset[2] = 0.0f;
		break;
	default:	//stand is the default. This is a vector from foot to eye when standing 
		offset[0] = 0.0f;
		offset[1] = 1.6951f;
		offset[2] = 0.0f;
		break;
	}
}

void UpdateCameraForCharacter(float * icamera_pos, float * ws_camera_pos, struct character_struct * p_character)
{
	struct character_model_struct * p_common=0;
	float eyeOffset[3];
	float vehicleOffset[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float mRot[16];
	float mTranslate[16];
	float mVehicle[16];

	if(!(g_keyboard_state.state == KEYBOARD_MODE_PLAYER || g_camera_state.playerCameraMode == PLAYERCAMERA_VEHICLE))
		return;

	if(g_camera_state.playerCameraMode == PLAYERCAMERA_TP)
		return;

	p_common = p_character->p_common;

	CharacterGetEyeOffset(p_character, eyeOffset);

	//if we are in a vehicle compute that offset:
	if(p_character->flags & CHARACTER_FLAGS_IN_VEHICLE)
	{
		//calculate the vehicle's world-space transform
		VehicleConvertDisplacementMat3To4(g_b_vehicle.orientation, mRot);
		mmTranslateMatrix(mTranslate, g_b_vehicle.cg[0], g_b_vehicle.cg[1], g_b_vehicle.cg[2]);
		mmMultiplyMatrix4x4(mTranslate, mRot, mVehicle);

		vehicleOffset[0] = p_character->pos[0];
		vehicleOffset[1] = p_character->pos[1] + eyeOffset[1];
		vehicleOffset[2] = p_character->pos[2];
		mmTransformVec(mVehicle, vehicleOffset);
		ws_camera_pos[0] = vehicleOffset[0];
		ws_camera_pos[1] = vehicleOffset[1];
		ws_camera_pos[2] = vehicleOffset[2];
	}
	else
	{
		ws_camera_pos[0] = p_character->pos[0] + eyeOffset[0];
		ws_camera_pos[1] = p_character->pos[1] + eyeOffset[1];
		ws_camera_pos[2] = p_character->pos[2] + eyeOffset[2];
	}

	icamera_pos[0] = -1.0f*ws_camera_pos[0];
	icamera_pos[1] = -1.0f*ws_camera_pos[1];
	icamera_pos[2] = -1.0f*ws_camera_pos[2];
}

/*
This function updates the passed camera parameters to provide a third person camera. But let the mouse look around where it
wants, just position the camera to be in front of the character.
*/
void UpdateCameraForThirdPerson(struct character_struct * p_character, float * icamera_pos, float * ws_camera_pos, float * cameraRotY, float * cameraRotX)
{
	struct character_model_struct * p_common=0;
	float fwdVec[4] = {0.0f, 0.0f, 5.0f, 1.0f}; //a vector down +z axis facing forward
	float mRot[16];
	float mTranslate[16];
	float mVehicle[16];
	float eyeOffset[3] = {0.0f, 1.6951f, 0.0f}; //hardcode the standing eye offset

	p_common = p_character->p_common;

	if(p_character->flags & CHARACTER_FLAGS_IN_VEHICLE)
	{
		//calculate the vehicle's world transform
		VehicleConvertDisplacementMat3To4(g_b_vehicle.orientation, mRot);
		mmTranslateMatrix(mTranslate, g_b_vehicle.cg[0], g_b_vehicle.cg[1], g_b_vehicle.cg[2]);
		mmMultiplyMatrix4x4(mTranslate, mRot, mVehicle);

		//calculate a point fwd and to the +x side of the vehicle
		fwdVec[0] = 5.0f;
		fwdVec[1] = 0.0f;
		fwdVec[2] = 5.0f;
		mmTransformVec(mVehicle, fwdVec);
	}
	else
	{
		//get a vector that points forward from the character
		mmRotateAboutY(mRot, (p_character->rotY+45.0f));
		mmTransformVec(mRot, fwdVec);
	}

	ws_camera_pos[0] = p_character->pos[0] + eyeOffset[0] + fwdVec[0];
	ws_camera_pos[1] = p_character->pos[1] + eyeOffset[1] + fwdVec[1];
	ws_camera_pos[2] = p_character->pos[2] + eyeOffset[2] + fwdVec[2];

	icamera_pos[0] = -1.0f*ws_camera_pos[0];
	icamera_pos[1] = -1.0f*ws_camera_pos[1];
	icamera_pos[2] = -1.0f*ws_camera_pos[2];
}

/*
This function updates the camera position and direction with a first person
view inside a vehicle.
*/
void UpdateCharacterCameraForVehicle(struct character_struct * p_character, float * matCamera4)
{
	float rotMat3[9];
	//struct vehicle_physics_struct2 * pvehicle=0;

	memcpy(rotMat3, p_character->p_vehicle->orientation, 9*sizeof(float));
	mmTranspose3x3(rotMat3);
	VehicleConvertDisplacementMat3To4(rotMat3, matCamera4);

	//vehicle's forward points down +z axis; camera's fwd is down -z axis. This matrix
	//transforms from vehicle to camera's orientation.
	mmMultiplyMatrix4x4(g_camera_state.vehicleToCameraCoordTransform, matCamera4, matCamera4);
}

/*
This function updates character_struct's rotY with the camera's
*/
void UpdateLocalPlayerDirection(void)
{
	//If this is the local player, Update direction character is facing based on camera
	if(g_keyboard_state.state == KEYBOARD_MODE_PLAYER && g_camera_state.playerCameraMode == PLAYERCAMERA_FP)
	{
		//g_a_man.rotY = g_camera_rotY+180.0f;
		g_a_man.rotY = -1.0f*(g_camera_rotY + 180.0f);
	}
}

int InitCharacterList(struct character_list_struct * plist)
{
	plist->num_soldiers = 0;
	plist->max_soldiers = 32;
	plist->ptrsToCharacters = (struct character_struct **)malloc(plist->max_soldiers * sizeof(struct character_struct*));
	if(plist->ptrsToCharacters == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(plist->ptrsToCharacters, 0, plist->max_soldiers * sizeof(struct character_struct*));

	return 1;
}

int InitVehicleCommon(struct vehicle_common_struct * p_common)
{
	struct bush_model_struct temp_model = {0}; //borrow the bush file structure just to load a simple mesh.
	image_t tgaFile;
	float fscale;
	int i;
	int r;
	
	memset(p_common, 0, sizeof(struct vehicle_common_struct));

	//r = Load_Bush_Model(&temp_model, "Cube", "cube.obj"); //this is a test model of a simple cube
	//r = Load_Bush_Model(&temp_model, "Cylinder.002", "truck_wheel.obj");
	r = Load_Bush_Model(&temp_model, "truck_Cube.001", "./resources/models/truck_chassis.obj");
	if(r == 0)
		return -1;

	//Loop through all bush model data and scale the positions
	fscale = 0.5f;
	for(i = 0; i < temp_model.num_leaf_verts; i++)
	{
		(temp_model.p_leaf_vertex_data + (8*i))[0] *= fscale;
		(temp_model.p_leaf_vertex_data + (8*i))[1] *= fscale;
		(temp_model.p_leaf_vertex_data + (8*i))[2] *= fscale;
	}
	
	//setup the VBO
	glGenBuffers(1, &(p_common->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(temp_model.num_leaf_verts*8*sizeof(float)),
		temp_model.p_leaf_vertex_data,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//setup the EBO
	glGenBuffers(1, &(p_common->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		(temp_model.num_leaf_elements*sizeof(int)),
		temp_model.p_leaf_elements,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	p_common->num_indices = temp_model.num_leaf_elements;
	
	//setup the VAO
	glGenVertexArrays(1, &(p_common->vao));
	glBindVertexArray(p_common->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glEnableVertexAttribArray(0); //vertex position
	glEnableVertexAttribArray(1); //vertex normal
	glEnableVertexAttribArray(2); //vertex texture coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//setup a blue texture for simplicity
	glGenTextures(1, &(p_common->texture_id));
	glBindTexture(GL_TEXTURE_2D, p_common->texture_id);
	r = LoadTga("./resources/textures/blue.tga", &tgaFile);
	if(r == 0)
	{
		printf("InitVehicleCommon: LoadTga() failed for %s.\n", "blue.tga");
		return -1;
	}
	
	//check to make sure there is no alpha channel
	if(tgaFile.info.components != 3)
	{
		printf("InitVehicleCommon: error. %s is not RGB. It has %d channels.\n", "blue.tga", tgaFile.info.components);
		return -1;
	}
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D,
		0,
		GL_RGB,
		tgaFile.info.width,
		tgaFile.info.height,
		0,
		GL_RGB,
		GL_UNSIGNED_BYTE,
		tgaFile.data);
	free(tgaFile.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 9);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	//Setup physics variables common to the vehicle
	p_common->driverSeatOffset[0] = 0.5925f;
	p_common->driverSeatOffset[1] = -0.644f;
	p_common->driverSeatOffset[2] = 0.0f;	

	//driver's side exit point
	p_common->playerExitPoint[0] = 4.0f;
	p_common->playerExitPoint[1] = 0.0f;
	p_common->playerExitPoint[2] = 0.0f;

	//passenger side exit point
	p_common->playerExitPoint[3] = -4.0f;
	p_common->playerExitPoint[4] = 0.0f;
	p_common->playerExitPoint[5] = 0.0f;
	
	return 1;
}

int InitWheelCommon(struct vehicle_common_struct * p_common)
{
	struct bush_model_struct temp_model = {0}; //borrow the bush file structure just to load a simple mesh.
	image_t tgaFile;
	float fscale;
	int i;
	int r;
	
	memset(p_common, 0, sizeof(struct vehicle_common_struct));

	//r = Load_Bush_Model(&temp_model, "Cube", "cube.obj");
	r = Load_Bush_Model(&temp_model, "Cylinder.002", "./resources/models/truck_wheel.obj");
	if(r == 0)
		return -1;

	//Loop through all bush model data and scale the positions
	fscale = 0.5f;
	for(i = 0; i < temp_model.num_leaf_verts; i++)
	{
		(temp_model.p_leaf_vertex_data + (8*i))[0] *= fscale;
		(temp_model.p_leaf_vertex_data + (8*i))[1] *= fscale;
		(temp_model.p_leaf_vertex_data + (8*i))[2] *= fscale;
	}

	//setup the VBO
	glGenBuffers(1, &(p_common->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(temp_model.num_leaf_verts*8*sizeof(float)),
		temp_model.p_leaf_vertex_data,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//setup the EBO
	glGenBuffers(1, &(p_common->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		(temp_model.num_leaf_elements*sizeof(int)),
		temp_model.p_leaf_elements,
		GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	p_common->num_indices = temp_model.num_leaf_elements;
	
	//setup the VAO
	glGenVertexArrays(1, &(p_common->vao));
	glBindVertexArray(p_common->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glEnableVertexAttribArray(0); //vertex position
	glEnableVertexAttribArray(1); //vertex normal
	glEnableVertexAttribArray(2); //vertex texture coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//borrow the blue texture from vehicle common
	p_common->texture_id = g_vehicle_common.texture_id;

	return 1;
}

/*
This function is here because the values associated with angular displacement are all 3x3 matrices, but
the matrices that actually transform the model are 4x4 matrices.
*/
void VehicleConvertDisplacementMat3To4(float * mat3, float * mat4)
{
	mat4[0] = mat3[0];
	mat4[1] = mat3[1];
	mat4[2] = mat3[2];
	mat4[3] = 0.0f;
	
	mat4[4] = mat3[3];
	mat4[5] = mat3[4];
	mat4[6] = mat3[5];
	mat4[7] = 0.0f;
	
	mat4[8] = mat3[6];
	mat4[9] = mat3[7];
	mat4[10] = mat3[8];
	mat4[11] = 0.0f;
	
	mat4[12] = 0.0f;
	mat4[13] = 0.0f;
	mat4[14] = 0.0f;
	mat4[15] = 1.0f;
}

/*
This function takes the mat3 floats and fills them into their
corresponding spots in the mat4. Other floats in the mat4 outside
of the mat3 are not touched (keep their original values).
*/
void FillInMat4withMat3(float * mat3, float * mat4)
{
	mat4[0] = mat3[0];
	mat4[1] = mat3[1];
	mat4[2] = mat3[2];

	mat4[4] = mat3[3];
	mat4[5] = mat3[4];
	mat4[6] = mat3[5];

	mat4[8] = mat3[6];
	mat4[9] = mat3[7];
	mat4[10] = mat3[8];
}

void PrintVector(char * name, float * vec3)
{
	float mag;
	float dir[3];

	memcpy(dir, vec3, 3*sizeof(float));
	vNormalize(dir);

	mag = vMagnitude(vec3);
	printf("%s: dir=%f %f %f (mag=%f)\n", name, dir[0], dir[1], dir[2], mag);
}

/*
WritePlantGridToFile() writes the all the plant tiles in the plant
grid to a binary file.
returns:
	1 = success
	-1 = error
*/
int WritePlantGridToFile(struct plant_grid * p_grid, char * filename)
{
	FILE * pFile=0;
	int i_tile;
	int j;

	if(p_grid == 0)
		return -1;

	if(filename == 0)
		return -1;

	pFile = fopen(filename, "wb");
	if(pFile == 0)
	{
		printf("%s: error opening %s\n", __func__, filename);
		return -1;
	}

	fwrite(&(p_grid->num_tiles), 4, 1, pFile);
	fwrite(&(p_grid->num_cols), 4, 1, pFile);
	fwrite(&(p_grid->num_rows), 4, 1, pFile);

	for(i_tile = 0; i_tile < p_grid->num_tiles; i_tile++)
	{
		fwrite(&(p_grid->p_tiles[i_tile].num_plants), 4, 1, pFile);
		fwrite(&(p_grid->p_tiles[i_tile].urcorner[0]), 4, 1, pFile);
		fwrite(&(p_grid->p_tiles[i_tile].urcorner[1]), 4, 1, pFile);

		//for each plant in the grid write 4 floats to file:
		for(j = 0; j < p_grid->p_tiles[i_tile].num_plants; j++)
		{
			fwrite(&(p_grid->p_tiles[i_tile].plants[j].pos[0]), 4, 1, pFile); //x pos
			fwrite(&(p_grid->p_tiles[i_tile].plants[j].pos[1]), 4, 1, pFile); //y pos
			fwrite(&(p_grid->p_tiles[i_tile].plants[j].pos[2]), 4, 1, pFile); //z pos
			fwrite(&(p_grid->p_tiles[i_tile].plants[j].pos[3]), 4, 1, pFile); //rotation about Y axis
		}
	}


	fclose(pFile);
	return 0;
}

/*
IsTileInPlantViewBox() returns 1 if the center of the terrain tile is within
a viewing box around the camera
*/
int IsTileInPlantViewBox(struct lvl_1_tile * p_tile)
{
	float center_pos[2];
	//float fboxSize = 10000.0f;
	float fboxSize = 1000.0f;

	center_pos[0] = p_tile->urcorner[0] + 495.0f;	//495 = 990/2, side of tile / 2
	center_pos[1] = p_tile->urcorner[1] + 495.0f;

	center_pos[0] -= g_ws_camera_pos[0];
	center_pos[1] -= g_ws_camera_pos[2];

	if(center_pos[0] < fboxSize 
			&& center_pos[0] > (-1.0f*fboxSize)
			&& center_pos[1] < fboxSize
			&& center_pos[1] > (-1.0f*fboxSize))
	{
		return 1;
	}

	return 0;
}

/*
GetRandomPlantType() returns plant_type which is a random plant type based on the position
given. Returns:
	0 - fail
	1 - ok
*/
int GenRandomPlantType(float * pos, char * plant_type)
{
	/*
	index:	plant:
	0		1st bush
	1		palm
	2		scaevola
	3		fake pemphis
	4		tourne fortia
	5		ironwood
	*/
	float plant_min_elevations[6] = {1.0f,  1.0f,  1.0f, 20.0f, 1.0f, 20.0f}; 
	float plant_max_elevations[6] = {-1.0f, 20.0f, 4.0f, -1.0f, 4.0f, -1.0f};	//-1 indicates don't check limit
	unsigned int rand_num;
	int i;
	int i_possible=0;
	int num_possible_plants;
	char possible_plant_types[6];
	
	memset(possible_plant_types, 0, 6);

	if(pos[1] <= 0.0f)
		return 0;

	//Go through each plant type and see if it can be placed here
	for(i = 0; i < 6; i++)
	{
		if(plant_min_elevations[i] == -1.0f || pos[1] >= plant_min_elevations[i])	//if limit is set to -1 don't check it
		{
			if(plant_max_elevations[i] == -1.0f || pos[1] <= plant_max_elevations[i])
			{
				possible_plant_types[i_possible] = i;
				i_possible += 1;		
			}
		}
	}

	if(i_possible == 0)
		return 0;

	rand_num = (unsigned int)rand();
	i = rand_num % (i_possible);
	*plant_type = possible_plant_types[i];
	return 1;
}

int EnterVehicle(struct vehicle_physics_struct2 * vehicle, struct character_struct * character)
{
	float vtemp[3];
	float fdist;

	//Check to see if we are close enough to vehicle:
	vSubtract(vtemp, vehicle->cg, character->pos);
	fdist = vMagnitude(vtemp);
	if(fdist > 3.0f)
		return 0;

	//keep track of what vehicle the player is in
	character->p_vehicle = vehicle;

	//translate character in local coordinates of vehicle... same with rotation
	character->pos[0] = g_vehicle_common.driverSeatOffset[0];
	character->pos[1] = g_vehicle_common.driverSeatOffset[1];
	character->pos[2] = g_vehicle_common.driverSeatOffset[2];
	character->rotY = 0.0f;

	//change the character y axis bias to be the bias for a seated position
	character->biasY = g_triangle_man.model_origin_to_seat_bottom;

	character->flags |= CHARACTER_FLAGS_IN_VEHICLE;

	g_camera_state.playerCameraMode = PLAYERCAMERA_VEHICLE;
	g_keyboard_state.state = KEYBOARD_MODE_TRUCK;
	printf("%s: in KEYBOARD_MODE_TRUCK\n", __func__);
	return 1;
} 

int LeaveVehicle(struct character_struct * character)
{
	struct vehicle_physics_struct2 * vehicle=0;
	struct plant_tile * p_plantTile=0;
	struct moveable_items_tile_struct * p_itemTile=0;
	float exitPos[3];
	float surfPos[3];
	float tempVec[3];
	float fwdVec[3] = {0.0f, 0.0f, 1.0f};
	float boundaryBox[6]; //array of 2 vec3s. 0=upper +x+y+z corner, 1=bottom -x-y-z corner
	float cornerPosArray[8]; //array of 4 vec2s. vec2 for each corner. 0=+x,+z 1=+x,-z 2=-x,-z 3=-x,+z
	float fboxHeight = 1.8386f; //from foot to head
	float fboxHalfWidth = 0.3197f;
	float yRad;	//rotation around y-axis in radians
	int cornerTileIndex[4] = {-1, -1, -1, -1};
	int numTileIndices = 0;
	int itile;
	int isSkipCheck;
	int i;
	int j;
	int k;
	int r;

	//check that character is in a vehicle
	if((character->flags & CHARACTER_FLAGS_IN_VEHICLE) == 0)
		return 0;

	if(character->p_vehicle == 0)
	{
		printf("%s: error. character flags have in vehicle set but character->p_vehicle is 0.\n", __func__);
		return 0; //um. you're not even in a vehicle?
	}
	vehicle = character->p_vehicle;

	//Make a bounding box around the exit point, then make sure
	//there's nothing obstructing placing the player there.

	//first get a point on the map where the vehicle exit should be
	//exitPos is in local vehicle coordinates
	//TODO: the exit points will probably depend on the vehicle
	exitPos[0] = g_vehicle_common.playerExitPoint[0];
	exitPos[1] = g_vehicle_common.playerExitPoint[1];
	exitPos[2] = g_vehicle_common.playerExitPoint[2];

	//transform exitPos to world coordinates.
	mmTransformVec3(vehicle->orientation, exitPos);
	vAdd(exitPos, exitPos, vehicle->cg);

	//Find the surface point
	r = GetTileSurfPoint(exitPos, surfPos, tempVec);
	if(r == 1) //a surface point was calculated. (if we couldnt calculate a surface point just go with exitPos)
	{
		exitPos[1] = surfPos[1]; //use height of terrain
	}

	//Now create a bounding box around the exit position
	//+x,+z corner
	boundaryBox[0] = exitPos[0] + fboxHalfWidth;
	boundaryBox[1] = exitPos[1] + fboxHeight;
	boundaryBox[2] = exitPos[2] + fboxHalfWidth;

	//-x,-z corner
	boundaryBox[3] = exitPos[0] - fboxHalfWidth;
   	boundaryBox[4] = exitPos[1];
	boundaryBox[5] = exitPos[2] - fboxHalfWidth;

	cornerPosArray[0] = boundaryBox[0];//+x,+z
	cornerPosArray[1] = boundaryBox[2];

	cornerPosArray[2] = boundaryBox[0];//+x,-z
	cornerPosArray[3] = boundaryBox[5];

	cornerPosArray[4] = boundaryBox[3];//-x,-z
	cornerPosArray[5] = boundaryBox[5];

	cornerPosArray[6] = boundaryBox[3];//-x,+z
	cornerPosArray[7] = boundaryBox[2];

	//now check to make sure trees aren't in the box
	//loop through each corner of the bounding box and get a terrain tile index
	//+x,+z
	tempVec[0] = boundaryBox[0];
	tempVec[1] = 0.0f;
	tempVec[2] = boundaryBox[2];
	cornerTileIndex[numTileIndices] = GetLvl1Tile(tempVec);
	numTileIndices++;

	//+x,-z
	tempVec[2] -= (2*fboxHalfWidth);
	itile = GetLvl1Tile(tempVec);
	for(i = 0; i < numTileIndices; i++)
	{
		//have we already seen this tile?
		if(cornerTileIndex[i] == itile)
			break;
	}
	if(i == numTileIndices) //went through array didn't find this tile index already, so add it
	{
		cornerTileIndex[numTileIndices] = itile;
		numTileIndices++;
	}

	//-x,-z
	tempVec[0] = boundaryBox[3];
	tempVec[1] = 0.0f;
	tempVec[2] = boundaryBox[5];
	itile = GetLvl1Tile(tempVec);
	for(i = 0; i < numTileIndices; i++)
	{
		//have we already seen this tile?
		if(cornerTileIndex[i] == itile)
			break;
	}
	if(i == numTileIndices) //went through array didn't this tile index already, so add it
	{
		cornerTileIndex[numTileIndices] = itile;
		numTileIndices++;
	}

	//-x,+z
	tempVec[0] = boundaryBox[3];
	tempVec[1] = 0.0f;
	tempVec[2] = boundaryBox[2];
	itile = GetLvl1Tile(tempVec);
	for(i = 0; i < numTileIndices; i++)
	{
		//have we already seen this tile?
		if(cornerTileIndex[i] == itile)
			break;
	}
	if(i == numTileIndices) //went through array didn't this tile index already, so add it
	{
		cornerTileIndex[numTileIndices] = itile;
		numTileIndices++;
	}

	//now loop through list of tiles and check trees
	for(i = 0; i < numTileIndices; i++)
	{
		p_plantTile = g_bush_grid.p_tiles + cornerTileIndex[i];
		for(j = 0; j < p_plantTile->num_plants; j++)
		{
			if(p_plantTile->plants[j].plant_type == 1 || p_plantTile->plants[j].plant_type == 5) //is plant palm or ironwood?
			{
				//check if plant pos is in the bounding box
				if(p_plantTile->plants[j].pos[0] < boundaryBox[0]		//+x 
					&& p_plantTile->plants[j].pos[0] > boundaryBox[3]	//-x
					&& p_plantTile->plants[j].pos[2] < boundaryBox[2]	//+z
					&& p_plantTile->plants[j].pos[2] > boundaryBox[5])	//-z
				{
					return 0; //can't exit tree in way
				}
			}
		}
	}

	//make sure crates and barrels aren't in the box
	numTileIndices = 0;
	for(i = 0; i < 4; i++) //loop thru each corner
	{
		tempVec[0] = cornerPosArray[(i*2)];
		tempVec[1] = 0.0f;
		tempVec[2] = cornerPosArray[(i*2)+1];
		itile = GetMoveablesTileIndex(tempVec);
		
		//check if we already checked this tile
		isSkipCheck = 0;
		for(k = 0; k < numTileIndices; k++)
		{
			if(itile == cornerTileIndex[k])
			{
				isSkipCheck = 1;
				break;
			}
		}
		if(isSkipCheck == 1) //we already checked this tile, so skip to next corner
			continue;
		cornerTileIndex[numTileIndices] = itile;
		numTileIndices++;

		//check this tile
		p_itemTile = g_moveables_grid.p_tiles + itile;
		for(j = 0; j < p_itemTile->num_moveables; j++)
		{
			if(p_itemTile->moveables_list[j].pos[0] < boundaryBox[0]
				&& p_itemTile->moveables_list[j].pos[0] > boundaryBox[3]
				&& p_itemTile->moveables_list[j].pos[2] < boundaryBox[2]
				&& p_itemTile->moveables_list[j].pos[2] > boundaryBox[5])
			{
				return 0; //can't exit truck, box or something is in way
			}
		}
	}
//TODO: NEED TO PAY ATTENTION AND DO DIFFERENT THINGS IF THIS IS THE
//LOCAL PLAYER OR IF THIS IS AI
	//ok if we made it to this point then player is ok to get out of car.
	character->pos[0] = exitPos[0];
	character->pos[1] = exitPos[1];
	character->pos[2] = exitPos[2];

	//clear the in vehicle flag
	character->flags &= (~CHARACTER_FLAGS_IN_VEHICLE);

	//set character y rotation to match vehicle
	mmTransformVec3(vehicle->orientation, fwdVec);
	yRad = atan2f(fwdVec[0], fwdVec[2]);
	yRad *= RATIO_RADTODEG; //convert to degrees
	character->rotY = yRad;
	character->biasY = character->p_common->model_origin_foot_bias;

	if(character == &g_a_man)
	{
		g_keyboard_state.state = KEYBOARD_MODE_PLAYER;
		g_camera_state.playerCameraMode = PLAYERCAMERA_FP;
		g_camera_rotY = -1.0f*(yRad + 180.0f);
		g_camera_rotX = 0.0f;
	}

	return 1;
}

int InitItemCommon(struct item_common_struct * p_common, char * obj_filename, char * objname, char * texture_filename)
{
	struct bush_model_struct temp_model = {0}; //borrow the bush file structure just to load a simple mesh.
	image_t tgaFile;
	int powerOfTwo;
	int r;

	memset(p_common, 0, sizeof(struct vehicle_common_struct));

	r = Load_Bush_Model(&temp_model, objname, obj_filename);
	if(r == 0)
		return -1;

	//Use the model data to set the above ground offset.
	p_common->y_AboveGroundOffset = Bush_Find_Bottom(&temp_model);
	p_common->y_AboveGroundOffset *= -1.0f;
	//p_common->y_AboveGroundOffset += 0.05f;

	//setup the VBO
	glGenBuffers(1, &(p_common->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glBufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(temp_model.num_leaf_verts*8*sizeof(float)),	//8 verts=3 pos + 3 normal + 2 texcoords
			temp_model.p_leaf_vertex_data,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//setup the EBO
	glGenBuffers(1, &(p_common->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			(temp_model.num_leaf_elements*sizeof(int)),
			temp_model.p_leaf_elements,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	p_common->num_indices = temp_model.num_leaf_elements;

	//setup the VAO
	glGenVertexArrays(1, &(p_common->vao));
	glBindVertexArray(p_common->vao);
	glBindBuffer(GL_ARRAY_BUFFER, p_common->vbo);
	glEnableVertexAttribArray(0);	//vertex position
	glEnableVertexAttribArray(1);	//vertex normal
	glEnableVertexAttribArray(2);	//vertex texture coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_common->ebo);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenTextures(1, &(p_common->texture_id));
	glBindTexture(GL_TEXTURE_2D, p_common->texture_id);
	r = LoadTga(texture_filename, &tgaFile);
	if(r == 0)
	{
		printf("%s: error. LoadTga() %s failed.\n", __func__, texture_filename);
		return -1;
	}

	//check to make sure there is no alpha channel
	if(tgaFile.info.components != 3)
	{
		printf("%s: error. %s has %d channels, need 3 channels\n", __func__, texture_filename, tgaFile.info.components);
		return -1;
	}

	//check that the texture is square
	if(tgaFile.info.width != tgaFile.info.height)
	{
		printf("%s: error. %s dimensions not square.\n", __func__, texture_filename);
		return -1;
	}

	//check that the texture size is a power of 2
	if((tgaFile.info.width % 2) != 0)
	{
		printf("%s: error. %s dimensions width=%d not a power of 2.\n", __func__, texture_filename, tgaFile.info.width);
		return -1;
	}
	
	//put a reality check that we don't try to put in a super big texture
	if(tgaFile.info.width > 4096)
	{
		printf("%s: error. %s is larger than 4096x4096\n", __func__, texture_filename);
		return -1;
	}

	//calculate power of 2 of texture size to calculate # of mipmap levels
	powerOfTwo = (int)log2f((float)tgaFile.info.width);

	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D,
			0,
			GL_RGB,
			tgaFile.info.width,
			tgaFile.info.height,
			0,
			GL_RGB,
			GL_UNSIGNED_BYTE,
			tgaFile.data);
	free(tgaFile.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, powerOfTwo);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	return 1;
}

/*
This function updates some common graphical constants
*/
int InitRifleCommon(struct item_common_struct * p_item)
{
	float mat4[16];

	p_item->handBoneIndex = 10;	//hand_wpn_R index. determined by looking at dae file for <Name_array> element

	//TODO: These transform matrices are no longer needed as we now have
	//a weapon bone.
	/*
	//convert from OpenGL to bone coordinates and calculate transform to position rifle when just
	//holding it.
	mmRotateAboutY(p_item->itemBoneSpaceHoldTransformMat4, -90.0f);
	mmRotateAboutX(mat4, 90.0f);
	mmMultiplyMatrix4x4(mat4, p_item->itemBoneSpaceHoldTransformMat4, p_item->itemBoneSpaceHoldTransformMat4);
	//mmTranslateMatrix(mat4, -0.90291f, 1.27441f, 0.07863f); //read these coordinates by reading vertex pos off of blender in 'local' transform
	mmTranslateMatrix(mat4, -0.89964f, 1.26315f, 0.153880f);  //these coordinates move the gun just above the hand	
	mmMultiplyMatrix4x4(mat4, p_item->itemBoneSpaceHoldTransformMat4, p_item->itemBoneSpaceHoldTransformMat4);

	//calculate transform to aim rifle in bone coordinates
	//order of rotations: Y, X, Z
	mmTranslateMatrix(g_rifle_common.itemBoneSpaceAimTransformMat4, -0.01f, 0.05f, 0.26f);
	mmRotateAboutY(mat4, -148.299850f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	mmRotateAboutX(mat4, 53.199760f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	mmRotateAboutZ(mat4, -67.599541f);
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);
	mmTranslateMatrix(mat4, -0.89964f, 1.26315f, 0.153880f); //these coordinates move the gun to just above the hand
	mmMultiplyMatrix4x4(mat4, g_rifle_common.itemBoneSpaceAimTransformMat4, g_rifle_common.itemBoneSpaceAimTransformMat4);

	//Set hold as default transform
	p_item->pitemBoneSpaceMat4 = p_item->itemBoneSpaceHoldTransformMat4;
	*/
	return 1;
}

/*
This function takes a bone info struct and a particular bone and calculates its transform then
places it in outMat4.
*/
void CalculateSingleBoneTransform(struct character_struct * p_character, int boneIndex, float * outMat4)
{
	struct dae_model_bones_struct * p_bones=0;
	struct dae_bone_tree_leaf * boneLeaf=0;
	struct dae_animation_struct * anim=0;
	float mat4[16];
	float interp;
	int numFramesInCurrentAnim;
	int i_prev_keyframe;
	int i_next_keyframe;
	int i_bone;
	int j;

	p_bones = &(p_character->p_common->testBones);
	boneLeaf = p_bones->bone_tree+boneIndex;
	anim = &(p_character->p_common->testAnim[p_character->anim.curAnim]);
	numFramesInCurrentAnim = anim->num_frames;
	mmMakeIdentityMatrix(outMat4);

	CharacterBoneAnimCalcInterp(p_character,
			&i_prev_keyframe,
			&i_next_keyframe,
			&interp);

	//follow parent bones until we find root
	while(boneLeaf->parent != 0)
	{
		i_bone = boneLeaf - p_bones->bone_tree;
		for(j = 0; j < 16; j++)
		{
			//TODO: This interpolation is old see UpdateCharacterBoneModel() for new way
			mat4[j] = (anim->bone_frame_transform_mat4_array+(i_bone*numFramesInCurrentAnim*16)+(i_prev_keyframe*16))[j];
			mat4[j] = mat4[j] + interp*((anim->bone_frame_transform_mat4_array+(i_bone*16*numFramesInCurrentAnim)+(i_next_keyframe*16))[j] - mat4[j]);
		}
		mmMultiplyMatrix4x4(mat4, outMat4, outMat4);

		boneLeaf = boneLeaf->parent;
	}

	//multiply by armature mat4
	mmMultiplyMatrix4x4(p_bones->armature_mat4, outMat4, outMat4);

	//success. outMat4 contains the bone transform.
}

static void CharacterBoneAnimCalcInterp(struct character_struct * this_character, int * i_prev_keyframe, int * i_next_keyframe, float * finterp)
{
	struct character_model_struct * p_common_character=0;
	struct dae_animation_struct * anim=0;
	int prev_keyframe_time;
	int next_keyframe_time;

	p_common_character = this_character->p_common;
	anim = &(p_common_character->testAnim[this_character->anim.curAnim]);

	*i_prev_keyframe = this_character->anim.iPrevKeyframe;
	*i_next_keyframe = this_character->anim.iNextKeyframe;
	prev_keyframe_time = anim->key_frame_times[*i_prev_keyframe];
	next_keyframe_time = anim->key_frame_times[*i_next_keyframe];
	if((next_keyframe_time - prev_keyframe_time) != 0)
	{
		*finterp = ((float)(this_character->anim.curFrame - prev_keyframe_time))/((float)(next_keyframe_time - prev_keyframe_time));
	}
	else
	{
		*finterp = 0.0f;
	}
}

void CharacterCalculateHandTransform(struct character_struct * this_character, struct item_common_struct * p_item, float * outMat4)
{
	struct dae_model_bones_struct * p_bones=0;
	float mat4[16];

	p_bones = &(this_character->p_common->testBones);

	//The order of transforms: (where v is vector)
	//	full-transform-for-handBone * inverse-bind-mat4-for-handBone * bind_shape_mat4 * v
	//ASSUME that temp_bone_mat_array holds the bone's transform for that frame
	//mmMultiplyMatrix4x4((p_bones->inverse_bind_mat4_array+(p_item->handBoneIndex*16)), p_bones->bind_shape_mat4, mat4); 
	//mmMultiplyMatrix4x4((p_bones->temp_bone_mat_array+(p_item->handBoneIndex*16)), mat4, outMat4);

	//TODO: Test transform
	memcpy(outMat4, (p_bones->temp_bone_mat_array+(p_item->handBoneIndex*16)), 16*sizeof(float));
}

void UpdateVehicleSimulation2(struct vehicle_physics_struct2 * p_vehicle)
{
	float newLinearVel[3];	//new linear velocity
	float wheelForce[3];
	float wheelTorque[3];
	float steerForce[3];
	float steerTorque[3];
	float powerForce[3];
	float sumForces[3] = {0.0f, 0.0f, 0.0f};
	float sumTorques[3] = {0.0f, 0.0f, 0.0f};
	float newAngularVelQ[4];
	float newAngularMomentum[3];
	float imomentOfInertiaWorld[9]; //moment of inertia in world coordinates
	float iorient[9];
	float ray[3];
	float mag;
	int i;

	//Calculate Forces + Torques

	//Add gravity
	sumForces[1] = p_vehicle->mass*(-G_K_GRAVITY);

	//calculate wheel spring force and torques
	for(i = 0; i < 4; i++)
	{
		CalculateWheelSpringForce2((p_vehicle->wheels+i), 
				p_vehicle, 
				wheelForce,
				wheelTorque);
		vAdd(sumForces, sumForces, wheelForce);
		vAdd(sumTorques, sumTorques, wheelTorque);
	}
	//printf("wheel: sumForces=(%f,%f,%f) sumTorques=(%f,%f,%f) wheel-x: %f %f %f %f\n", sumForces[0], sumForces[1], sumForces[2], sumTorques[0], sumTorques[1], sumTorques[2], p_vehicle->wheels[0].x, p_vehicle->wheels[1].x, p_vehicle->wheels[2].x, p_vehicle->wheels[3].x);

	CalcVehicleEngineAndResistanceForce(p_vehicle, powerForce);
	vAdd(sumForces, sumForces, powerForce);

	//Add a low-speed steer force/torque
	CalculateSteeringForce(p_vehicle, steerTorque, steerForce);
	vAdd(sumForces, sumForces, steerForce);
	vAdd(sumTorques, sumTorques, steerTorque);

	//Prepare to calculate angular velocity
	//L_i+1 = L_i + h*T
	vAdd(newAngularMomentum, p_vehicle->angularMomentum, sumTorques);

	//Transform InverseMomentOfInertia from local to world coordinates
	//I_i^-1 = (R_i)*(I_0^-1)*(R_i^-1)
	memcpy(iorient, p_vehicle->orientation, (9*sizeof(float)));
	mmTranspose3x3(iorient); //orientation should be orthogonal mat so can just transpose it vice calculating full matrix
	memcpy(imomentOfInertiaWorld, p_vehicle->imomentOfInertia, (9*sizeof(float)));
	mmMultiplyMatrix3x3(imomentOfInertiaWorld, iorient, imomentOfInertiaWorld);
	mmMultiplyMatrix3x3(p_vehicle->orientation, imomentOfInertiaWorld, imomentOfInertiaWorld);

	//Calculate angular velocity for real
	//w_i+1 = (I_i^-1)*(L_i+1)
	newAngularVelQ[0] = newAngularMomentum[0];
	newAngularVelQ[1] = newAngularMomentum[1];
	newAngularVelQ[2] = newAngularMomentum[2];
	mmTransformVec3(imomentOfInertiaWorld, newAngularVelQ);
	newAngularVelQ[3] = 0.0f;

	//if calculated angularvelocity is small, clear it and angular momentum
	/*mag = vMagnitude(newAngularVelQ);
	if(mag < 0.001f)
	{
		newAngularVelQ[0] = 0.0f;
		newAngularVelQ[1] = 0.0f;
		newAngularVelQ[2] = 0.0f;
		newAngularMomentum[0] = 0.0f;
		newAngularMomentum[1] = 0.0f;
		newAngularMomentum[2] = 0.0f;
	}*/

	//save the angular velocity
	p_vehicle->angularVel[0] = newAngularVelQ[0];
	p_vehicle->angularVel[1] = newAngularVelQ[1];
	p_vehicle->angularVel[2] = newAngularVelQ[2];

	//Calculate new orientation
	//q_i+1 = q + (h/2)*w*q		;note: here w is a quaternion q(0,w)
	qMultiply(newAngularVelQ, newAngularVelQ, p_vehicle->orientationQ);
	newAngularVelQ[0] *= 0.5f;
	newAngularVelQ[1] *= 0.5f;
	newAngularVelQ[2] *= 0.5f;
	newAngularVelQ[3] *= 0.5f;
	qAdd(newAngularVelQ, p_vehicle->orientationQ, newAngularVelQ);

	//Re-normalize the quaternion
	qNormalize(newAngularVelQ);

	//Calculate new velocity
	newLinearVel[0] = p_vehicle->linearVel[0] + (sumForces[0]/p_vehicle->mass);
	newLinearVel[1] = p_vehicle->linearVel[1] + (sumForces[1]/p_vehicle->mass);
	newLinearVel[2] = p_vehicle->linearVel[2] + (sumForces[2]/p_vehicle->mass);

	//clear linear velocity if it is too small
	//ClearLowMotion(newLinearVel);

	//update linear position
	p_vehicle->cg[0] += newLinearVel[0];
	p_vehicle->cg[1] += newLinearVel[1];
	p_vehicle->cg[2] += newLinearVel[2];

	//save new velocity, new orientation, new angular-momentum
	p_vehicle->linearVel[0] = newLinearVel[0];
	p_vehicle->linearVel[1] = newLinearVel[1];
	p_vehicle->linearVel[2] = newLinearVel[2];

	p_vehicle->orientationQ[0] = newAngularVelQ[0];
	p_vehicle->orientationQ[1] = newAngularVelQ[1];
	p_vehicle->orientationQ[2] = newAngularVelQ[2];
	p_vehicle->orientationQ[3] = newAngularVelQ[3];
	qConvertToMat3(newAngularVelQ, p_vehicle->orientation);

	p_vehicle->angularMomentum[0] = newAngularMomentum[0];
	p_vehicle->angularMomentum[1] = newAngularMomentum[1];
	p_vehicle->angularMomentum[2] = newAngularMomentum[2];

	//figure out how much to spin the wheels:
	CalcWheelSpin(p_vehicle);
}

/*
This function will 0 linearVel or angularMomentum if they are lower than a small value
*/
void ClearLowMotion(float * vec3)
{
	float mag;
	float limit = VEHICLE_SMALLMOTION_LIMIT;

	mag = vMagnitude(vec3);
	if(mag < limit)
	{
		vec3[0] = 0.0f;
		vec3[1] = 0.0f;
		vec3[2] = 0.0f;
	}
}

int InitSomeVehicle2(struct vehicle_physics_struct2 * p_vehicle)
{
	float vh = 1.7f;
	float vw = 4.8f;
	float vl = 2.2f;
	int i;

	memset(p_vehicle, 0, sizeof(struct vehicle_physics_struct2));

	//this block of vectors doesn't need to be scaled from blender coordinates.
	//I calculated the proper distance already in calc.
	p_vehicle->cg[0] = 12307.0f;
	p_vehicle->cg[1] = 14.0f;
	p_vehicle->cg[2] = 11278.0f;
	p_vehicle->orientationQ[0] = 0.0f; //x
	p_vehicle->orientationQ[1] = 0.0f; //y
	p_vehicle->orientationQ[2] = 0.0f; //z
	p_vehicle->orientationQ[3] = 1.0f; //w
	qConvertToMat3(p_vehicle->orientationQ, p_vehicle->orientation);
	p_vehicle->mass = 3000.0f;
	p_vehicle->momentOfInertia[0] = ((vh*vh)+(vw*vw))*p_vehicle->mass*(1/12.0f);
	p_vehicle->momentOfInertia[4] = ((vh*vh)+(vl*vl))*p_vehicle->mass*(1/12.0f);
	p_vehicle->momentOfInertia[8] = ((vl*vl)+(vw*vw))*p_vehicle->mass*(1/12.0f);
	p_vehicle->imomentOfInertia[0] = 1.0f/p_vehicle->momentOfInertia[0];
	p_vehicle->imomentOfInertia[4] = 1.0f/p_vehicle->momentOfInertia[4];
	p_vehicle->imomentOfInertia[8] = 1.0f/p_vehicle->momentOfInertia[8];

	//these vectors are in vehicle coordinates. These vectors need to be scaled
	//from blender coordinates. I left them in blender coordinates because it is easier
	//to look at blender and figure out if these are correct.
	//front-left
	p_vehicle->wheels[0].connectPos[0] = 1.6734f;
	//p_vehicle->wheels[0].connectPos[1] = -2.8028f;
	p_vehicle->wheels[0].connectPos[2] = 2.8094f;

	//front-right
	p_vehicle->wheels[1].connectPos[0] = -1.6734f;
	//p_vehicle->wheels[1].connectPos[1] = -2.8028f;
	p_vehicle->wheels[1].connectPos[2] = 2.8094f;

	//rear-right
	p_vehicle->wheels[2].connectPos[0] = -1.6734f;
	//p_vehicle->wheels[2].connectPos[1] = -2.8028f;
	p_vehicle->wheels[2].connectPos[2] = -2.8265f;

	//rear-left
	p_vehicle->wheels[3].connectPos[0] = 1.6734f;
	//p_vehicle->wheels[3].connectPos[1] = -2.8028f;
	p_vehicle->wheels[3].connectPos[2] = -2.8265f;

	//scale the vectors from blend file dimensions
	for(i = 0; i < 4; i++)
	{
		p_vehicle->wheels[i].mass = 0.5f;

		//set connection pos here to make wheels consistent
		//lower connection point from original so chassis sits naturally on tires (instead of high up in the air)
		p_vehicle->wheels[i].connectPos[1] = -1.8028f;

		p_vehicle->wheels[i].connectPos[0] *= 0.5f;
		p_vehicle->wheels[i].connectPos[1] *= 0.5f;
		p_vehicle->wheels[i].connectPos[2] *= 0.5f;

		//also while we are at set the min and max extension of the wheel
		p_vehicle->wheels[i].min_x = 0.0f;
		p_vehicle->wheels[i].max_x = 1.0f;

		//Set spring constant calculate this by assuming overall vehicle weight is 500N, divide by 4 for wheels -> 125N per wheel
		//Allow for 0.5 dist of compression for 125N, k = 125/0.5 = 500
		p_vehicle->wheels[i].k_spring = 250.0f; //last good constant

		p_vehicle->wheels[i].k_damp = -125.0f;	//make sign negative so that force gets applied in opposite direction of velocity
	}

	p_vehicle->wheelbase = 2.81795f; //in world coords (not blender)
	p_vehicle->wheelRadius = 0.5f; //in world coords (in blender wheel radius is 1.0f)

	//test rotation
	//p_vehicle->angularMomentum[0] = 10.0f;
	//p_vehicle->angularMomentum[1] = 10.0f;
	//p_vehicle->angularMomentum[2] = 10.0f;

	//set a 'parking brake' so the vehicle doesn't roll
	p_vehicle->flags |= VEHICLEFLAGS_PARK;

	return 1;
}

void CalculateWheelSpringForce2(struct wheel_struct2 * pwheel, struct vehicle_physics_struct2 * pvehicle, float * forceOut, float * torqueOut)
{
	float dir[3] = {0.0f, -1.0f, 0.0f};
	float ray[3] = {0.0f, -1.0f, 0.0f};
	float connectionPointInWorld[3];	//connection point of wheel spring in world coordinates	
	float start_pos[3];
	float surf_pos[3];
	float surf_norm[3];
	float start_to_surf[3];
	float relVel[3];	//relative velocity at spring
	float dampForce[3];
	float r_dot;
	int r;

	forceOut[0] = 0.0f;
	forceOut[1] = 0.0f;
	forceOut[2] = 0.0f;
	torqueOut[0] = 0.0f;
	torqueOut[1] = 0.0f;
	torqueOut[2] = 0.0f;

	//determine position of the connection point of the wheel in world coords
	memcpy(connectionPointInWorld, pwheel->connectPos, (3*sizeof(float)));
	mmTransformVec3(pvehicle->orientation, connectionPointInWorld);
	vAdd(start_pos, connectionPointInWorld, pvehicle->cg);

	//determine a ray, first calculate len of ray
	//ray_len = pwheel->x + pwheel->radius; //ray goes from bottom of car using length of spring then uses radius of wheel.
	ray[1] = 0.0f-pwheel->max_x;
	mmTransformVec3(pvehicle->orientation, ray);

	r = RaycastTileSurf(start_pos, 
			ray, 
			surf_pos, 
			surf_norm);
	if(r == 1)	//hit terrain surface
	{
		//calculate relative velocity at spring point
		vCrossProduct(relVel, pvehicle->angularVel, connectionPointInWorld);
		vAdd(relVel, relVel, pvehicle->linearVel);
		r_dot = vDotProduct(dir, relVel);

		//If we hit the terrain surface, then compress the wheel spring until it reaches minimum displacement
		vSubtract(start_to_surf, surf_pos, start_pos);
		
		//Check that surf_pos isn't above the start_pos
		r_dot = vDotProduct(dir, start_to_surf);
		if(r_dot < 0.0f) //start_to_surf is pointing upward
		{
			pwheel->x = 0.0f;
		}
		else
		{
			pwheel->x = vMagnitude(start_to_surf);
		}

		forceOut[0] = 0.0f;
		forceOut[1] = pwheel->k_spring*(pwheel->max_x-pwheel->x);
		forceOut[2] = 0.0f;

		//apply damp
		//Calculate relative velocity along the wheel spring axis
		//this now means forceOut (at this point in time) is along the same
		//axis as dampForce
		r_dot = vDotProduct(relVel, dir);
		relVel[0] = r_dot*dir[0];
		relVel[1] = r_dot*dir[1];
		relVel[2] = r_dot*dir[2];
		dampForce[0] = pwheel->k_damp*relVel[0];
		dampForce[1] = pwheel->k_damp*relVel[1];
		dampForce[2] = pwheel->k_damp*relVel[2];
		vAdd(forceOut, forceOut, dampForce);

		//transform to world space
		mmTransformVec3(pvehicle->orientation, forceOut);

		//determine the torque in world coordinates
		vCrossProduct(torqueOut, connectionPointInWorld, forceOut);
		torqueOut[1] = 0.0f; //not sure if this helps with slow spinning with 0 steeringAngle
	}

	//uh what if I only fix the linear force in the upward direction?
	forceOut[0] = 0.0f;
	forceOut[2] = 0.0f;

}

static void UpdateVehicleFromKeyboard2(
	char * keys_return, 
	float * camera_rotX, 
	float * camera_rotY, 
	float * camera_ipos, 
	struct vehicle_physics_struct2 * p_vehicle)
{
	//clear the parking brake if set
	p_vehicle->flags = (~VEHICLEFLAGS_PARK) & p_vehicle->flags;

	//check w key
	if(CheckKey(keys_return, 25)==1)
	{
		//p_vehicle->throttle = 1.0f;
		p_vehicle->events |= VEHICLEFLAGS_ACCELERATE;
	}

	//check s key 
	if(CheckKey(keys_return, 39) == 1)
	{
		//memset(g_b_vehicle.angularMomentum, 0, 3*sizeof(float));
		p_vehicle->events |= VEHICLEFLAGS_BRAKE;
	}

	//check 'a' key
	if(CheckKey(keys_return, 38) == 1)
	{
		p_vehicle->events |= VEHICLEFLAGS_STEER_LEFT;
	}

	//check 'd' key	
	if(CheckKey(keys_return, 40) == 1)
	{
		p_vehicle->events |= VEHICLEFLAGS_STEER_RIGHT;
	}

	//Check 'e' key
	if(CheckKey(keys_return, 26) == 1 && CheckKey(g_keyboard_state.prev_keys_return, 26) == 0)
	{
		//if player is not in vehicle put them in vehicle:
		if(g_a_man.flags & CHARACTER_FLAGS_IN_VEHICLE)
		{
			LeaveVehicle(&g_a_man);
		}
	}
}

/*
This function given a steeringAngle calculates a torqueOut and forceOut vec3's
to apply to the vehilce that cause a turn.
This function also handles steering events
*/
void CalculateSteeringForce(struct vehicle_physics_struct2 * pvehicle, float * torqueOut, float * forceOut)
{
	float turnRadius;
	float speed;
	float wheelBaseVec[3] = {0.0f, 0.0f, 0.0f};
	float iorient[9];
	float momentOfInertiaWorld[9];
	float angVel[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float newOrientationQ[4];
	float newOrientMat[9];
	float up[3] = {0.0f, 1.0f, 0.0f};
	float linearVelFwd[3];	//linear-velocity in the fwd direction
	float linearVelUp[3];
	float torqueDiff[3];
	float angMomentumUp[3]; //angular momentum in the up direction
	float mag;
	float r_dot;

	if((pvehicle->events & VEHICLEFLAGS_ACCELERATE) == VEHICLEFLAGS_ACCELERATE)
	{
		pvehicle->throttle += 0.1f;
		
		//limit throttle to 1.0
		if(pvehicle->throttle > 1.0f)
			pvehicle->throttle = 1.0f;

		pvehicle->events = pvehicle->events & (~VEHICLEFLAGS_ACCELERATE);
	}
	else
	{
		//slowly lower throttle
		pvehicle->throttle -= 0.1f;
		if(pvehicle->throttle < 0.0f)
		{
			pvehicle->throttle = 0.0f;
		}
	}
	//printf("throttle=%f\n", pvehicle->throttle);

	if((pvehicle->events & VEHICLEFLAGS_STEER_LEFT) == VEHICLEFLAGS_STEER_LEFT)
	{
		pvehicle->steeringAngle += 0.01f;
		if(pvehicle->steeringAngle > 0.5f)
			pvehicle->steeringAngle = 0.5f;
		pvehicle->events = pvehicle->events & (~VEHICLEFLAGS_STEER_LEFT);
	}
	else if((pvehicle->events & VEHICLEFLAGS_STEER_RIGHT) == VEHICLEFLAGS_STEER_RIGHT)
	{
		pvehicle->steeringAngle -= 0.01f;
		if(pvehicle->steeringAngle < -0.5f)
			pvehicle->steeringAngle = -0.5f;
		pvehicle->events = pvehicle->events & (~VEHICLEFLAGS_STEER_RIGHT);
	}
	else //no steering events so start to 0 the steering angle
	{
		if(pvehicle->steeringAngle > 0.0f)
		{
			pvehicle->steeringAngle -= 0.01f;

			//if we switched signs because we passed 0, 0 the steering
			if(pvehicle->steeringAngle < 0.0f)
			{
				pvehicle->steeringAngle = 0.0f;
			}
		}
		else if(pvehicle->steeringAngle < 0.0f)
		{
			pvehicle->steeringAngle += 0.01f;

			//if steering angle was neg, now its positive, that means we
			//passed 0, so 0 the steering.
			if(pvehicle->steeringAngle > 0.0f)
			{
				pvehicle->steeringAngle = 0.0f;
			}
		}
		//else if((pvehicle->steeringAngle <= 0.015f) || (pvehicle->steeringAngle >= -0.015f))
		//{
		//	pvehicle->steeringAngle = 0.0f;
		//}
	}
	//printf("steeringAngle=%f\n", pvehicle->steeringAngle);

	//if(pvehicle->steeringAngle == 0.0f)
	//{
	//	torqueOut[0] = 0.0f;
	//	torqueOut[1] = 0.0f;
	//	torqueOut[2] = 0.0f;
	//	forceOut[0] = 0.0f;
	//	forceOut[1] = 0.0f;
	//	forceOut[2] = 0.0f;
	//	return;
	//}

	//calculate linear velocity component in the x-z plane
	mmTransformVec3(pvehicle->orientation, up);
	r_dot = vDotProduct(up, pvehicle->linearVel);
	linearVelUp[0] = r_dot*pvehicle->linearVel[0];
	linearVelUp[1] = r_dot*pvehicle->linearVel[1];
	linearVelUp[2] = r_dot*pvehicle->linearVel[2];
	vSubtract(linearVelFwd, pvehicle->linearVel, linearVelUp);

	//TODO: For debug, calculate how much angular momentum we have in the up direction
	r_dot = vDotProduct(up, pvehicle->angularMomentum);
	angMomentumUp[0] = r_dot*pvehicle->angularMomentum[0];
	angMomentumUp[1] = r_dot*pvehicle->angularMomentum[1];
	angMomentumUp[2] = r_dot*pvehicle->angularMomentum[2];

	//I_i = (R_i) * (I_0) * (R_i^-1)
	memcpy(iorient, pvehicle->orientation, 9*sizeof(float));
	mmTranspose3x3(iorient);
	mmMultiplyMatrix3x3(pvehicle->momentOfInertia, iorient, momentOfInertiaWorld);
	mmMultiplyMatrix3x3(pvehicle->orientation, momentOfInertiaWorld, momentOfInertiaWorld);

	//R = wheelbase/sin(steer-angle)
	speed = vMagnitude(linearVelFwd);
	if(pvehicle->steeringAngle != 0.0f)
	{
		turnRadius = pvehicle->wheelbase/(sinf(pvehicle->steeringAngle));

		//w = v/R	
		angVel[1] = speed/turnRadius;

		//transform angularVel to world-coords
		mmTransformVec3(pvehicle->orientation, angVel);

		//Calculate a torque to apply to get the proper rotation
		//dL = w*I_i
		torqueOut[0] = angVel[0];
		torqueOut[1] = angVel[1];
		torqueOut[2] = angVel[2];
		mmTransformVec3(momentOfInertiaWorld, torqueOut);

		//calculate difference between old and new torque
		vSubtract(torqueDiff, torqueOut, pvehicle->prevSteeringTorque);

		//save the torque because we need to adjust for it next iteration
		memcpy(pvehicle->prevSteeringTorque, torqueOut, 3*sizeof(float));

		//set torqueOut to difference between old and new needed torque
		memcpy(torqueOut, torqueDiff, 3*sizeof(float));

		//calculate the orientation that will occur from this new
		//angularVelocity. And use that to point linearVelocity in that same direction
	
		//q_1 = q + (1/2)*w*q
		memcpy(newOrientationQ, pvehicle->orientationQ, 4*sizeof(float));
		qMultiply(newOrientationQ, angVel, newOrientationQ);
		newOrientationQ[0] *= 0.5f;
		newOrientationQ[1] *= 0.5f;
		newOrientationQ[2] *= 0.5f;
		newOrientationQ[3] *= 0.5f;
		qAdd(newOrientationQ, pvehicle->orientationQ, newOrientationQ);
		qNormalize(newOrientationQ);
		qConvertToMat3(newOrientationQ, newOrientMat);
	}
	else //steering angle is 0 make sure linearVel is aligned to wheels
	{
		//remove the prevSteeringTorque from the angularMomentum
		torqueOut[0] = 0.0f - pvehicle->prevSteeringTorque[0];
		torqueOut[1] = 0.0f - pvehicle->prevSteeringTorque[1];
		torqueOut[2] = 0.0f - pvehicle->prevSteeringTorque[2];
		pvehicle->prevSteeringTorque[0] = 0.0f;
		pvehicle->prevSteeringTorque[1] = 0.0f;
		pvehicle->prevSteeringTorque[2] = 0.0f;

		memcpy(newOrientMat, pvehicle->orientation, 9*sizeof(float));
	}

	//set new re-oriented linear velocity, but add back in the left over velocity that wasn't in
	//the fwd direction
	linearVelFwd[0] = 0.0f;
	linearVelFwd[1] = 0.0f;
	linearVelFwd[2] = speed;
	mmTransformVec3(newOrientMat, linearVelFwd);
	vAdd(pvehicle->linearVel, linearVelFwd, linearVelUp);

	//clear forceOut so it doesnt do anything
	forceOut[0] = 0.0f;
	forceOut[1] = 0.0f;
	forceOut[2] = 0.0f;
}

void UpdateCameraPosAtVehicle2(float follow_dist, 
		struct vehicle_physics_struct2 * p_vehicle, 
		float * camera_pos, 
		float * icamera_pos, 
		float * camera_rotY)
{
	float tempVec[3];
	float fdegYAxis;

	//set a ray that points towards the back of the vehicle
	//for the vehicle -z faces back
	tempVec[0] = 5.0f;
	tempVec[1] = 5.0f;
	tempVec[2] = -1.0f*follow_dist;

	mmTransformVec3(p_vehicle->orientation, tempVec);

	//Calculate camera position & inverted position
	camera_pos[0] = p_vehicle->cg[0] + tempVec[0];
	camera_pos[1] = p_vehicle->cg[1] + tempVec[1];
	camera_pos[2] = p_vehicle->cg[2] + tempVec[2];

	icamera_pos[0] = -1.0f*camera_pos[0];
	icamera_pos[1] = -1.0f*camera_pos[1];
	icamera_pos[2] = -1.0f*camera_pos[2];

	//Calculate camera rotation
	//flip the vector so it points towards the car
	tempVec[0] *= -1.0f;
	tempVec[1] *= -1.0f;
	tempVec[2] *= -1.0f;
	vNormalize(tempVec);

	//atan2() is going to give an angle that is rotated in the wrong direction if you were to
	//consider x-axis as the x param. But if I swap params I think it will be rotated 
	//counter-clockwise and now the vector that would cause atan2 to return 0 would be
	//pointing down the +z axis
	fdegYAxis = atan2(tempVec[0], tempVec[2]);
	fdegYAxis *= RATIO_RADTODEG;
	fdegYAxis += 180.0f;
	fdegYAxis *= -1.0f; //negate the angle, because camera rotation really rotates the world
	*camera_rotY = fdegYAxis;
}

void CalcVehicleEngineAndResistanceForce(struct vehicle_physics_struct2 * pvehicle, float * forceOut)
{
	float k_drag = -0.5f;
	float k_rollingResistance = -15.0f;
	float max_engine_force = 10.0f;
	float max_brake_force;
	float speed;
	float up[3] = {0.0f, 1.0f, 0.0f};
	float linearFwd[3];
	float r_dot;

	//calculate engine force such that it balances resistance forces at 37 m/s
	//F_engine_max = C_drag*(37^2) + C_rr*37 = 684.5 + 555 = 1239.5

	//calculate vehicle speed only in the x-z plane
	mmTransformVec3(pvehicle->orientation, up);
	r_dot = vDotProduct(pvehicle->linearVel, up);
	up[0] = r_dot*pvehicle->linearVel[0];
	up[1] = r_dot*pvehicle->linearVel[1];
	up[2] = r_dot*pvehicle->linearVel[2];
	vSubtract(linearFwd, pvehicle->linearVel, up);
	speed = vMagnitude(linearFwd);

	//Just for debug, calculate a force that points where the car is facing
	forceOut[0] = 0.0f;
	forceOut[1] = 0.0f;

	if(((pvehicle->events & VEHICLEFLAGS_BRAKE) == VEHICLEFLAGS_BRAKE)
		|| ((pvehicle->flags & VEHICLEFLAGS_PARK) == VEHICLEFLAGS_PARK)) //only difference is that the park event has to be cleared elsewhere
	{
		forceOut[2] = (k_drag*speed) + (k_rollingResistance*speed*speed);

		//only add braking force if the speed of the car is not 0
		if(speed != 0.0f)
		{
			forceOut[2] -= pvehicle->mass*0.01f;

			//calculate max brake force possible without causing car to reverse direction
			//max brake is based on speed.
			max_brake_force = speed*pvehicle->mass*-1.0f;
			if(forceOut[2] < max_brake_force)
			{
				forceOut[2] = max_brake_force;
			}
		}

		pvehicle->throttle = 0.0f;

		//clear brake flag since we handled it
		pvehicle->events = pvehicle->events & (~VEHICLEFLAGS_BRAKE);
	}
	else
	{
		forceOut[2] = (k_drag*speed) + (k_rollingResistance*speed*speed) + (max_engine_force*pvehicle->throttle);
	}
	
	//printf("resistForce=(%f, %f, %f)\n", forceOut[0], forceOut[1], forceOut[2]);

	mmTransformVec3(pvehicle->orientation, forceOut);
}

void CalcWheelSpin(struct vehicle_physics_struct2 * pvehicle)
{
	float w;
	float speed;

	speed = vMagnitude(pvehicle->linearVel);

	//use arc-dist = rad*radius eqn, so: rad = arc-dist/radius
	w = speed/(pvehicle->wheelRadius);
	pvehicle->wheelOrientation_X += w;
}

/*
This function takes dimensions for a rectangle and sets all terrain heights within the
boundaries to the same value
	centerPos	;[in] vec3
	x_width		;[in] float
	z_width		;[in] float
	y_rot_deg	;[in] degrees
	y_height	;[out] float. height that the terrain was flattened to.
*/
int FlattenTerrain(float * centerPos, float x_width, float z_width, float y_rot_deg, float * y_height)
{
	struct lvl_1_tile * pTile=0;
	float * quad_origin=0;
	float * quad_pos_x=0;
	float * quad_pos_z=0;
	float * quad_opposite=0;
	float * curVertPos=0;
	float rotMat4[16];
	float rotMat3[9];
	float cornersPos[12]; 	//array of 4 vec3's, one for each corner, need vec3's because we will need to rotate them around the y-axis
	float clipPlaneNormals[12]; //array of 4 vec3's. one for each side. plane 0-to-1, plane 1-to-2, 
	float boundaryBox[4];	//2 vec2 coords: x0,y0 then x1,y1
	float x_half_width;
	float z_half_width;
	float overallHeight;
	float surf_norm[3];
	float surf_pos[3];
	float temp_vec[3];
	float down_vec[3] = {0.0f, -1.0f, 0.0f};
	float dot;
	int num_plants_deleted;
	int total_plants_deleted=0;
	int num_floats_per_vert;
	int i_tile;	//index of level 1 tile (lvl_1_tile struct)
	int j_tile;
	int i_quad;	//index of quad in a level 1 tile
	int j_quad;
	int i_vert;
	int j_vert;
	int i_startTile;
	int j_startTile;
	int i_startQuad;
	int j_startQuad;
	int i_endTile;
	int j_endTile;
	int i_endQuad;
	int j_endQuad;
	int curQuad_i_range[2]; //0=start, 1=stop
	int curQuad_j_range[2]; //0=start, 1=stop
	int is_in_boundary_box;
	int was_vert_changed;
	int i_clipPlane;
	int i;
	int r;

	num_floats_per_vert = g_big_terrain.num_floats_per_vert;

	//Get an overallHeight at the center
	r = GetTileSurfPoint(centerPos, surf_pos, surf_norm);
	if(r != 1)
		return 0; //fail

	*y_height = surf_pos[1];

	x_half_width = x_width*0.5f;
	z_half_width = z_width*0.5f;

	//0: -x,-z corner (origin)
	cornersPos[0] = centerPos[0] - x_half_width;
	cornersPos[1] = 0.0f; //height doesn't matter here
	cornersPos[2] = centerPos[2] - z_half_width;

	//1: -x,+z corner
	cornersPos[3] = centerPos[0] - x_half_width;
	cornersPos[4] = 0.0f; //height doesn't matter here
	cornersPos[5] = centerPos[2] + z_half_width;

	//2: +z,+z corner (corner opposite origin)
	cornersPos[6] = centerPos[0] + x_half_width;
	cornersPos[7] = 0.0f; //height doesn't matter here
	cornersPos[8] = centerPos[2] + z_half_width;

	//3: +x,-z corner
	cornersPos[9] = centerPos[0] + x_half_width;
	cornersPos[10] = 0.0f; //height doesn't matter here
	cornersPos[11] = centerPos[2] - z_half_width;

	//Rotate the corners around the y-axis
	mmRotateAboutY(rotMat4, y_rot_deg);
	mmConvertMat4toMat3(rotMat4, rotMat3);
	for(i = 0; i < 4; i++)
	{
		mmTransformVec3(rotMat3, (cornersPos+(i*3)));
	}

	//calculate clipping plane normals
	//clip plane 0 -> 1
	vSubtract(temp_vec, (cornersPos+3), (cornersPos));
	vCrossProduct(clipPlaneNormals, temp_vec, down_vec);
	vNormalize(clipPlaneNormals);

	//clip plane 1 -> 2
	vSubtract(temp_vec, (cornersPos+6), (cornersPos+3));
	vCrossProduct((clipPlaneNormals+3), temp_vec, down_vec);
	vNormalize(clipPlaneNormals+3);

	//clip plane 2 -> 3
	vSubtract(temp_vec, (cornersPos+9), (cornersPos+6));
	vCrossProduct((clipPlaneNormals+6), temp_vec, down_vec);
	vNormalize(clipPlaneNormals+6);

	//clip plane 3 -> 0
	vSubtract(temp_vec, cornersPos, (cornersPos+9));
	vCrossProduct((clipPlaneNormals+9), temp_vec, down_vec);
	vNormalize(clipPlaneNormals+9);

	//now that the box is rotated get the boundary box for it
	boundaryBox[0] = cornersPos[0];
	boundaryBox[1] = cornersPos[2];
	boundaryBox[2] = cornersPos[0];
	boundaryBox[3] = cornersPos[2];
	for(i = 0; i < 4; i++)
	{
		//find lowest x coordinate
		if(cornersPos[(i*3)] < boundaryBox[0])
			boundaryBox[0] = cornersPos[(i*3)];

		//find greatest x coordinate
		if(cornersPos[(i*3)] > boundaryBox[2])
			boundaryBox[2] = cornersPos[(i*3)];

		//find lowest z coordinate
		if(cornersPos[(i*3)+2] < boundaryBox[1])
			boundaryBox[1] = cornersPos[(i*3)+2];
		
		//find greatest z coordinate
		if(cornersPos[(i*3)+2] > boundaryBox[3])
			boundaryBox[3] = cornersPos[(i*3)+2];
	}

	//calculate the end tile and its end quad
	temp_vec[0] = boundaryBox[2];
	temp_vec[1] = 0.0f;
	temp_vec[2] = boundaryBox[3];
	r = GetLvl1Tileij(temp_vec, &i_endTile, &j_endTile);
	if(r == -1)
		return 0; //fail. probably should clip here?

	//TODO: Remove the i_quad etc. calculations since they aren't needed
	//pTile = g_big_terrain.pTiles+r;
	//temp_vec[0] -= pTile->urcorner[0]; //x coord of -x,-z corner (towards origin)
	//temp_vec[2] -= pTile->urcorner[1]; //z coord of -x,-z corner
	//i_endQuad = (int)(temp_vec[0]/10.0f);
	//j_endQuad = (int)(temp_vec[2]/10.0f);
	//so now i_endTile,j_endTile and i_endQuad, j_endQuad hold the stopping point of the search

	//ok now get a starting point
	temp_vec[0] = boundaryBox[0];
	temp_vec[1] = 0.0f;
	temp_vec[2] = boundaryBox[1];
	r = GetLvl1Tileij(temp_vec, &i_startTile, &j_startTile);
	if(r == -1)
		return 0; //fail
	//pTile = g_big_terrain.pTiles+r;
	//temp_vec[0] -= pTile->urcorner[0]; //x
	//temp_vec[2] -= pTile->urcorner[1]; //z
	//i_startQuad = (int)(temp_vec[0]/10.0f);
	//j_startQuad = (int)(temp_vec[2]/10.0f);

	i_tile = i_startTile;
	j_tile = j_startTile;
	//i_quad = i_startQuad;
	//j_quad = j_startQuad;

	//loop over lvl_1_tiles, from -z to z direction
	while(i_tile <= i_endTile)
	{
		//loop over lvl_1_tiles, from -x to x direction
		while(j_tile <= j_endTile)
		{
			//get new tile
			pTile = g_big_terrain.pTiles+(i_tile*g_big_terrain.num_cols)+j_tile;
			was_vert_changed = 0;

			for(i_vert=0; i_vert < pTile->num_z; i_vert++)
			{
				for(j_vert=0; j_vert < pTile->num_x; j_vert++)
				{
					curVertPos = pTile->pPos+(((i_vert*pTile->num_x)+j_vert)*num_floats_per_vert);

					//check that vertex is in bounding box
					for(i_clipPlane = 0; i_clipPlane < 4; i_clipPlane++)
					{
						vSubtract(temp_vec, curVertPos, (cornersPos+(3*i_clipPlane)));
						dot = vDotProduct(temp_vec, (clipPlaneNormals+(3*i_clipPlane)));
						if(dot < 0.0f) //if dot < 0 then vert is outside of clip plane since clipplane normal points towards center of boundary box
						{
							is_in_boundary_box = 0;
							break;
						}
						else
						{
							is_in_boundary_box = 1;
						}
					}

					//set height only if vertex is in boundary box
					if(is_in_boundary_box == 1)
					{
						curVertPos[1] = surf_pos[1];
						was_vert_changed = 1;

						//also clear any plants around the vertex that is being changed
						r = ClearPlantsAroundTerrainVert(i_tile,  	//row of terrain tile
								j_tile, 							//col of terrain tile
								i_vert, 							//tile vertex row index
								j_vert, 							//tile vertex col index
								&num_plants_deleted);
						if(r == 0) //error
							return 0;
						total_plants_deleted += num_plants_deleted;
					}
				}
			}

			//update the tile's vbo
			if(was_vert_changed == 1)
			{
				glBindBuffer(GL_ARRAY_BUFFER, pTile->vbo);
				glBufferSubData(GL_ARRAY_BUFFER,
						0,	//offset
						(pTile->num_verts*num_floats_per_vert*sizeof(float)),
						pTile->pPos);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
			}

			//set index to next tile
			j_tile += 1;
		}

		i_tile += 1;
	}

	return 1;
}

/*
This func give a particular vert in the terrain will look at all surrounding
quads and delete any plants.
*/
int ClearPlantsAroundTerrainVert(int i_tile, int j_tile, int i_quadvert, int j_quadvert, int * numPlantsRemoved)
{
	struct plant_info_struct * newPlantsArray=0;
	struct lvl_1_tile * pTile=0;
	struct plant_tile * plantTile=0;
	float * pPos=0;
	float boundaries[4]; //array of vec2: x0,z0 and x1,z1
	float boundaryHalfWidth = 10.0f;
	int plantTileCoord[2]; //0=row, 1=col
	int num_floats_per_vert;
	int i;
	int r;

	*numPlantsRemoved = 0;

	//check if there are even any plants in this tile:
	plantTile = g_bush_grid.p_tiles + (i_tile*g_big_terrain.num_cols) + j_tile;
	if(plantTile->num_plants == 0)
		return 1; //success

	num_floats_per_vert = g_big_terrain.num_floats_per_vert;

	//Get the pos of the vert
	pTile = g_big_terrain.pTiles + (i_tile*g_big_terrain.num_cols) + j_tile;
	pPos = pTile->pPos + (((i_quadvert*pTile->num_x) + j_quadvert)*num_floats_per_vert);

	//calculate the corner positions of a square around the vert
	boundaries[0] = pPos[0] - boundaryHalfWidth;
	boundaries[1] = pPos[2] - boundaryHalfWidth;
	boundaries[2] = pPos[0] + boundaryHalfWidth;
	boundaries[3] = pPos[2] + boundaryHalfWidth;

	//check all plants in the tile and see if any are within in the box
	//get a count of the # of plants to delete, because we have to reallocate the array
	r = ClearPlantsInBoundary(plantTile, boundaries, numPlantsRemoved);
	if(r == 0) //error
		return 0;

	//Check if the quad vert is along any of the edges of the tile, and if so check the
	//plants in the adjacent tile.
	if(i_quadvert == 0) //-z border
	{
		plantTileCoord[0] = i_tile-1; //row
		plantTileCoord[1] = j_tile; //col

		//check if this new plant tile coords are valid
		if(plantTileCoord[0] >= 0 && plantTileCoord[0] < g_bush_grid.num_rows
				&& plantTileCoord[1] >= 0 && plantTileCoord[1] < g_bush_grid.num_cols)
		{
			plantTile = g_bush_grid.p_tiles + (plantTileCoord[0]*g_bush_grid.num_cols) + plantTileCoord[1];
			r = ClearPlantsInBoundary(plantTile, boundaries, numPlantsRemoved);
			if(r == 0) //error
				return 0;
		}
	}

	if(i_quadvert == (pTile->num_z-1)) //+z border
	{
		plantTileCoord[0] = i_tile+1; //row
		plantTileCoord[1] = j_tile; //col

		//check if this new plant tile coords are valid
		if(plantTileCoord[0] >= 0 && plantTileCoord[0] < g_bush_grid.num_rows
				&& plantTileCoord[1] >= 0 && plantTileCoord[1] < g_bush_grid.num_cols)
		{
			plantTile = g_bush_grid.p_tiles + (plantTileCoord[0]*g_bush_grid.num_cols) + plantTileCoord[1];
			r = ClearPlantsInBoundary(plantTile, boundaries, numPlantsRemoved);
			if(r == 0) //error
				return 0;
		}
	}

	if(j_quadvert == 0) //-x border
	{
		plantTileCoord[0] = i_tile; //row
		plantTileCoord[1] = j_tile-1; //col

		//check if this new plant tile coords are valid
		if(plantTileCoord[0] >= 0 && plantTileCoord[0] < g_bush_grid.num_rows
				&& plantTileCoord[1] >= 0 && plantTileCoord[1] < g_bush_grid.num_cols)
		{
			plantTile = g_bush_grid.p_tiles + (plantTileCoord[0]*g_bush_grid.num_cols) + plantTileCoord[1];
			r = ClearPlantsInBoundary(plantTile, boundaries, numPlantsRemoved);
			if(r == 0) //error
				return 0;
		}
	}

	if(j_quadvert == (pTile->num_x-1)) //+x border
	{
		plantTileCoord[0] = i_tile; //row
		plantTileCoord[1] = j_tile+1; //col

		//check if this new plant tile coords are valid
		if(plantTileCoord[0] >= 0 && plantTileCoord[0] < g_bush_grid.num_rows
				&& plantTileCoord[1] >= 0 && plantTileCoord[1] < g_bush_grid.num_cols)
		{
			plantTile = g_bush_grid.p_tiles + (plantTileCoord[0]*g_bush_grid.num_cols) + plantTileCoord[1];
			r = ClearPlantsInBoundary(plantTile, boundaries, numPlantsRemoved);
			if(r == 0) //error
				return 0;
		}
	}

	return 1;
}

/*
This function loops through all plants in a plant tile and recreates the array
of plants with any plants inside the boundary deleted.
-boundaryCoords[8] //array of vec2: x0,z0 and x1,z1
*/
int ClearPlantsInBoundary(struct plant_tile * plantTile, float * boundaryCoords, int * numPlantsRemoved)
{
	struct plant_info_struct * newPlantsArray=0;
	int num_to_delete = 0;
	int new_num_plants;
	int i;
	int i_new;

	//check all plants in the tile and see if any are within in the box
	//get a count of the # of plants to delete, because we have to reallocate the array
	for(i = 0; i < plantTile->num_plants; i++)
	{
		//is plant within bounding box?
		if(plantTile->plants[i].pos[0] > boundaryCoords[0] 
			&& plantTile->plants[i].pos[0] < boundaryCoords[2]
			&& plantTile->plants[i].pos[2] > boundaryCoords[1]
			&& plantTile->plants[i].pos[2] < boundaryCoords[3])
		{
			num_to_delete += 1;
		}
	}
	
	if(num_to_delete > 0)
	{
		new_num_plants = plantTile->num_plants - num_to_delete;

		//allocate a new plant_info_struct array
		newPlantsArray = (struct plant_info_struct *)malloc(new_num_plants*sizeof(struct plant_info_struct));
		if(newPlantsArray == 0)
		{
			printf("%s: error. malloc fail when trying to create new plants array.\n", __func__);
			return 0;
		}

		i_new = 0;
		for(i = 0; i < plantTile->num_plants; i++)
		{
			//is plant NOT in the bounding box then copy it over 
			if(!(plantTile->plants[i].pos[0] > boundaryCoords[0] 
				&& plantTile->plants[i].pos[0] < boundaryCoords[2]
				&& plantTile->plants[i].pos[2] > boundaryCoords[1]
				&& plantTile->plants[i].pos[2] < boundaryCoords[3]))
			{
				memcpy((newPlantsArray+i_new), (plantTile->plants+i), sizeof(struct plant_info_struct));
				i_new += 1;
			}
		}

		//check to make sure that the same # of plants copied to new array was the same # allocated for.
		if(i_new != new_num_plants)
		{
			printf("%s: error. mismatch between new plants array size and # copied.\n", __func__);
			return 0;
		}

		//get rid of the old plants array and update the plant tile with a new # of plants
		free(plantTile->plants);
		plantTile->num_plants = new_num_plants;
		plantTile->plants = newPlantsArray;
		*numPlantsRemoved += num_to_delete;
	}

	return 1;
}

int InitMoveableModelCommon(struct simple_model_struct * simpleModel, char * obj_name, char * obj_filename, char * texture_filename)
{
	struct bush_model_struct temp_model;
	image_t tgaFile;
	int powerOfTwo;
	int r;

	memset(&temp_model, 0, sizeof(struct bush_model_struct));

	r = Load_Bush_Model(&temp_model, obj_name, obj_filename);
	if(r == 0)
	{
		printf("%s: error. load obj file %s fail.\n", __func__, obj_filename);
		return 0;
	}

	//setup the VBO
	glGenBuffers(1, &(simpleModel->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, simpleModel->vbo);
	glBufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(temp_model.num_leaf_verts*8*sizeof(float)),	//8 verts = 3 pos + 3 normal + 2 texcoords
			temp_model.p_leaf_vertex_data,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//setup the EBO
	glGenBuffers(1, &(simpleModel->ebo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, simpleModel->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			(temp_model.num_leaf_elements*sizeof(int)),
			temp_model.p_leaf_elements,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	simpleModel->num_indices = temp_model.num_leaf_elements;

	//setup the VAO
	glGenVertexArrays(1, &(simpleModel->vao));
	glBindVertexArray(simpleModel->vao);
	glBindBuffer(GL_ARRAY_BUFFER, simpleModel->vbo);
	glEnableVertexAttribArray(0);	//vertex position
	glEnableVertexAttribArray(1);	//vertex normal
	glEnableVertexAttribArray(2);	//vertex texture coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (GLvoid*)(6*sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, simpleModel->ebo);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//setup texture
	glGenTextures(1, &(simpleModel->texture_id));
	glBindTexture(GL_TEXTURE_2D, simpleModel->texture_id);
	r = LoadTga(texture_filename, &tgaFile);
	if(r == 0)
	{
		printf("%s: error. LoadTga() %s failed.\n", __func__, texture_filename);
		return 0;
	}

	//check that texture has no alpha
	if(tgaFile.info.components != 3)
	{
		printf("%s: error. %s has %d channels. only 3 channels allowed.\n", __func__, texture_filename, tgaFile.info.components);
		return 0;
	}

	//check that texture is square
	if(tgaFile.info.width != tgaFile.info.height)
	{
		printf("%s: error. %s dimensions not square.\n", __func__, texture_filename);
		return 0;
	}

	//check that texture size is a power of 2
	if((tgaFile.info.width % 2) != 0)
	{
		printf("%s: error. %s dimensions need to be a power of 2.\n", __func__, texture_filename);
		return 0;
	}

	//put a reality check so we don't try to load a huge texture
	if(tgaFile.info.width > 4096)
	{
		printf("%s: error. size of %s is larger than 4096.\n", __func__, texture_filename);
		return 0;
	}

	//calculate the power of 2 of texture size to calculate the # of mipmap levels
	powerOfTwo = (int)log2f((float)tgaFile.info.width);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D,
			0,
			GL_RGB,
			tgaFile.info.width,
			tgaFile.info.height,
			0,
			GL_RGB,
			GL_UNSIGNED_BYTE,
			tgaFile.data);
	free(tgaFile.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, powerOfTwo);
	glGenerateMipmap(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);

	return 1;
}

int InitMoveablesGrid(struct moveables_grid_struct * p_grid)
{
	struct moveable_items_tile_struct * p_tile=0;
	int i,j;

	memset(p_grid, 0, sizeof(struct moveables_grid_struct));

	p_grid->tileSize = 32.0f;
	p_grid->num_cols = 1207;	//this is to get 32x1207 = 38,624 which is > 38,610
	p_grid->num_rows = 1207;
	p_grid->num_tiles = p_grid->num_rows * p_grid->num_cols;

	p_grid->p_tiles = (struct moveable_items_tile_struct*)malloc(p_grid->num_tiles*sizeof(struct moveable_items_tile_struct));
	if(p_grid->p_tiles == 0)
	{
		printf("%s: malloc fail\n", __func__);
		return 0;
	}

	for(i = 0; i < p_grid->num_rows; i++)
	{
		for(j = 0; j < p_grid->num_cols; j++)
		{
			p_tile = p_grid->p_tiles + ((i*p_grid->num_cols) + j);
			memset(p_tile, 0, sizeof(struct moveable_items_tile_struct));
			p_tile->urcorner[0] = i * p_grid->tileSize;
			p_tile->urcorner[1] = j * p_grid->tileSize;
		}
	}

	//setup the working grid.
	p_grid->localGridSize[0] = 46;
	p_grid->localGridSize[1] = 46;
	p_grid->numLocalTiles = 2116; //size of the local_grid array
	for(i = 0; i < p_grid->localGridSize[0]; i++)
	{
		for(j = 0; j < p_grid->localGridSize[1]; j++)
		{
			p_grid->local_grid[(i*p_grid->localGridSize[1])+j] = -1;	//-1 = no entry. 
		}
	}

	return 1;
}

int GetMoveablesTileIndex(float * pos)
{
	int i,j;

	//check that position is inside the map boundary (check against terrain grid since that is the real map and the moveable grid is slightly larger)
	if(pos[0] < 0.0f 
		|| pos[2] < 0.0f 
		|| pos[0] > (g_big_terrain.num_cols*g_big_terrain.tile_len[0])
		|| pos[2] > (g_big_terrain.num_rows*g_big_terrain.tile_len[1]) )
	{
		return -1;
	}

	i = (int)(pos[2]/g_moveables_grid.tileSize);
	//j = (int)(pos[1]/g_moveables_grid.tileSize);
	j = (int)(pos[0]/g_moveables_grid.tileSize);

	return ((i*g_moveables_grid.num_cols)+j);
}

/*
-pos is a vec3
*/
void UpdateMoveablesLocalGrid(struct moveables_grid_struct * p_grid, float * pos)
{
	float relToGridCorner[2]; //rel vec2 that points from center to -x,-z grid corner
	float tempVec[3];
	int curTile[2];
	int startTile[2]; //index of the first square in the grid
	int i,j;

	//check that pos is inside the map boundaries
	if(pos[0] < 0.0f 
			|| pos[2] < 0.0f 
			|| pos[0] > (g_big_terrain.num_rows*g_big_terrain.tile_len[0]) 
			|| pos[2] > (g_big_terrain.num_rows*g_big_terrain.tile_len[1]))
	{
		//Set all grid entries to invalid
		for(i = 0; i < g_moveables_grid.localGridSize[0]; i++)
		{
			for(j = 0; j < g_moveables_grid.localGridSize[1]; j++)
			{
				g_moveables_grid.local_grid[(i*g_moveables_grid.localGridSize[1])+j] = -1;
			}
		}
		return;
	}

	//figure out what tile pos is in
	relToGridCorner[0] = -0.5f*(g_moveables_grid.localGridSize[1] * g_moveables_grid.tileSize);
	relToGridCorner[1] = -0.5f*(g_moveables_grid.localGridSize[0] * g_moveables_grid.tileSize);
	tempVec[0] = pos[0] + relToGridCorner[1];
	tempVec[1] = 0.0f;
	tempVec[2] = pos[2] + relToGridCorner[0];
	startTile[0] = (int)(tempVec[2]/g_moveables_grid.tileSize); //row
	startTile[1] = (int)(tempVec[0]/g_moveables_grid.tileSize); //col

	//Loop through all local grid tiles
	for(i = 0; i < g_moveables_grid.localGridSize[0]; i++)
	{
		for(j = 0; j < g_moveables_grid.localGridSize[1]; j++)
		{
			curTile[0] = startTile[0] + i;
			curTile[1] = startTile[1] + j;

			//check that this tile is in the map
			if(curTile[0] < 0 || curTile[0] > g_moveables_grid.num_rows
					|| curTile[1] < 0 || curTile[1] > g_moveables_grid.num_cols)
			{
				g_moveables_grid.local_grid[(i*g_moveables_grid.localGridSize[1])+j] = -1; //mark the entry invalid
				continue;
			}
			else
			{
				g_moveables_grid.local_grid[(i*g_moveables_grid.localGridSize[1])+j] = (curTile[0]*g_moveables_grid.num_cols) + curTile[1];
			}
		}
	}
}

/*
This function given a position vec3 finds the applicable plant tile and
adds a moveable object to the moveables_list.
If successful a pointer to the moveable is returned. If an error occurs
0 is returned.
*/
struct moveable_object_struct * AddMoveableToTile(float * moveablePos)
{
	struct moveable_object_struct * pmoveable=0;
	int i;

	//figure out which tile to place the moveable
	i = GetMoveablesTileIndex(moveablePos);
	if(i == -1)
	{
		printf("%s: error tried to make moveable (%f,%f,%f) outside of map.\n", __func__, moveablePos[0], moveablePos[1], moveablePos[2]);
		return 0;
	}

	//Check if moveables list has been initialized or not
	if(g_moveables_grid.p_tiles[i].moveables_list == 0)
	{
		pmoveable = (struct moveable_object_struct*)malloc(sizeof(struct moveable_object_struct));
		if(pmoveable == 0)
		{
			printf("%s: malloc of moveables_list fail.\n", __func__);
			return 0;
		}
		
		memset(pmoveable, 0, sizeof(struct moveable_object_struct));
		pmoveable->pos[0] = moveablePos[0];
		pmoveable->pos[1] = moveablePos[1];
		pmoveable->pos[2] = moveablePos[2];
		g_moveables_grid.p_tiles[i].moveables_list = pmoveable;
		g_moveables_grid.p_tiles[i].num_moveables = 1;
		return pmoveable;
	}
	else
	{
		pmoveable = g_moveables_grid.p_tiles[i].moveables_list;
		while(pmoveable->pNext != 0)
		{
			pmoveable = pmoveable->pNext;
		}

		pmoveable->pNext = (struct moveable_object_struct*)malloc(sizeof(struct moveable_object_struct));
		if(pmoveable->pNext == 0)
		{
			printf("%s: malloc of moveabes_list fail.\n", __func__);
			return 0;
		}
		pmoveable = pmoveable->pNext;
		memset(pmoveable, 0, sizeof(struct moveable_object_struct));
		pmoveable->pos[0] = moveablePos[0];
		pmoveable->pos[1] = moveablePos[1];
		pmoveable->pos[2] = moveablePos[2];
		g_moveables_grid.p_tiles[i].num_moveables += 1;
		return pmoveable;
	}
	
	return 0; //can't get here
}

/*
This function creates a base with the center at param pos
*/
int CreateBase(float * pos)
{
	float x_width = 45.73f;
	float z_width = 45.73f;
	float urCornerPos[3];
	float flatten_height;
	float clusterCenter[3];
	float clusterOffset=1.0f;
	int r;

	//calculate the origin corner of the base. (this is helpful for placement of objects within the base)
	urCornerPos[0] = pos[0] - (0.5f*x_width);
	urCornerPos[1] = pos[1]; //idk what to make height
	urCornerPos[2] = pos[2] - (0.5f*z_width);

	//flatten an area 50x50 feet (45.73 meters) 
	r = FlattenTerrain(pos, 	//pos of center.
			x_width, z_width, 	//x-axis-width, z-axis-width
			0.0f,	//rotation. degs
			&flatten_height);
	if(r != 1)
	{
		printf("%s: error. FlattenTerrain() fail at (%f,%f,%f).\n", __func__, pos[0], 0.0f, pos[2]);
		return 0;
	}
	urCornerPos[1] = flatten_height; //save the height that the terrain was flattened to.

	//place some crates
	clusterCenter[0] = urCornerPos[0] + (0.25f*x_width);
	clusterCenter[1] = urCornerPos[1] + g_crate_model_common.offset_groundToCg;
	clusterCenter[2] = urCornerPos[2] + (0.25f*z_width);
	
	r = PlaceCrateGroup(clusterCenter, //pos of center of cluster
			clusterOffset, //distance between crates
			0);	//type=0 crate
	if(r == 0)
		return 0;

	//place some barrels
	clusterCenter[0] = urCornerPos[0] + (0.75f*x_width);
	clusterCenter[1] = urCornerPos[1] + g_barrel_model_common.offset_groundToCg;
	clusterCenter[2] = urCornerPos[2] + (0.5f*z_width);

	r = PlaceCrateGroup(clusterCenter, clusterOffset, 1);
	if(r == 0)
		return 0;

	//Put some info in a base struct
	g_a_milBase.pos[0] = pos[0];
	g_a_milBase.pos[1] = urCornerPos[1]; //use the flattened terrain height
	g_a_milBase.pos[2] = pos[2];
	g_a_milBase.i_team = 0;

	return 1; //success
}

/*
This function adds moveables to the map. can place crates or barrels even though name has Crate in it.
*/
static int PlaceCrateGroup(float * clusterCenter, float clusterOffset, int moveable_type)
{
	struct moveable_object_struct * pmoveable=0;
	float tempVec[3];
	int i_crate=0;
	int r;

	//0
	tempVec[0] = clusterCenter[0] - clusterOffset;
	tempVec[1] = clusterCenter[1];
	tempVec[2] = clusterCenter[2] - clusterOffset;
	pmoveable = AddMoveableToTile(tempVec);
	if(pmoveable == 0)
		return 0;
	pmoveable->moveable_type = (char)moveable_type;
	if(moveable_type == 0)//crate
	{
		r = InitCrate(pmoveable);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 5, ITEM_TYPE_PISTOL_AMMO, 0, 0);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 1, ITEM_TYPE_BEANS, 0, 0);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 1, ITEM_TYPE_CANTEEN, 0, 0);
		if(r == 0)
			return 0;
		i_crate += 1;
	}

	//1
	tempVec[0] = clusterCenter[0] + clusterOffset;
	tempVec[1] = clusterCenter[1];
	tempVec[2] = clusterCenter[2] - clusterOffset;
	pmoveable = AddMoveableToTile(tempVec);
	if(pmoveable == 0)
		return 0;
	pmoveable->moveable_type = (char)moveable_type;
	if(moveable_type == 0)//crate
	{
		r = InitCrate(pmoveable);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 5, ITEM_TYPE_RIFLE_AMMO, 0, 0);
		if(r == 0)
			return 0;
		i_crate += 1;
	}

	//2
	tempVec[0] = clusterCenter[0] - clusterOffset;
	tempVec[1] = clusterCenter[1];
	tempVec[2] = clusterCenter[2] + clusterOffset;
	pmoveable = AddMoveableToTile(tempVec);
	if(pmoveable == 0)
		return 0;
	pmoveable->moveable_type = (char)moveable_type;
	if(moveable_type == 0)//crate
	{
		r = InitCrate(pmoveable);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 1, ITEM_TYPE_PISTOL, 0, 0);
		if(r == 0)
			return 0;
		i_crate += 1;
	}

	//3
	tempVec[0] = clusterCenter[0] + clusterOffset;
	tempVec[1] = clusterCenter[1];
	tempVec[2] = clusterCenter[2] + clusterOffset;
	pmoveable = AddMoveableToTile(tempVec);
	if(pmoveable == 0)
		return 0;
	pmoveable->moveable_type = (char)moveable_type;
	if(moveable_type == 0)//crate
	{
		r = InitCrate(pmoveable);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 1, ITEM_TYPE_RIFLE, 0, 0);
		if(r == 0)
			return 0;
		r = FillCrateWithItem(pmoveable->items, 1, ITEM_TYPE_CANTEEN, 0, 0);
		if(r == 0)
			return 0;
		i_crate += 1;
	}

	return 1;
}

/*
This function initializes some common information shared by all crates   
*/
int InitCrateCommon(struct crate_common_physics_struct * p_common)
{
	float * posArray=0;
	struct face_struct * faceArray=0;
	struct edge_struct * edgeArray=0;
	float boxHalfWidth = 0.4573f;	//from inspection of crate model in blender

	memset(p_common, 0, sizeof(struct crate_common_physics_struct));

	posArray = (float*)malloc(8*3*sizeof(float));	//8 vertices of vec3
	if(posArray == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}

	//init the vertex positions
	//+x,-y,-z vert 0
	posArray[0] = boxHalfWidth;
	posArray[1] = -1.0f*boxHalfWidth;
	posArray[2] = -1.0f*boxHalfWidth;

	//+x,-y,+z vert 1
	posArray[3] = boxHalfWidth;
	posArray[4] = -1.0f*boxHalfWidth;
	posArray[5] = boxHalfWidth;

	//-x, -y, +z vert 2
	posArray[6] = -1.0f*boxHalfWidth;
	posArray[7] = -1.0f*boxHalfWidth;
	posArray[8] = boxHalfWidth;

	//-x,-y,-z vert 3
	posArray[9] = -1.0f*boxHalfWidth;
	posArray[10] = -1.0f*boxHalfWidth;
	posArray[11] = -1.0f*boxHalfWidth;

	//+x,+y,-z vert 4
	posArray[12] = boxHalfWidth;
	posArray[13] = boxHalfWidth;
	posArray[14] = -1.0f*boxHalfWidth;

	//+x,+y,+z vert 5
	posArray[15] = boxHalfWidth;
	posArray[16] = boxHalfWidth;
	posArray[17] = boxHalfWidth;

	//-x,+y,+z vert 6
	posArray[18] = -1.0f*boxHalfWidth;
	posArray[19] = boxHalfWidth;
	posArray[20] = boxHalfWidth;

	//-x,+y,-z vert 7
	posArray[21] = -1.0f*boxHalfWidth;
	posArray[22] = boxHalfWidth;
	posArray[23] = -1.0f*boxHalfWidth;
	p_common->box_hull.positions = posArray;
	p_common->box_hull.num_pos = 8;

	//initialize faces
	p_common->box_hull.num_faces = 6;
	faceArray = (struct face_struct*)malloc(6*sizeof(struct face_struct));
	if(faceArray == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(faceArray, 0, 6*sizeof(struct face_struct));

	//+x face
	faceArray[0].num_verts = 4;
	faceArray[0].normal[0] = 1.0f;
	faceArray[0].normal[1] = 0.0f;
	faceArray[0].normal[2] = 0.0f;
	faceArray[0].i_vertices[0] = 0;
	faceArray[0].i_vertices[1] = 4;
	faceArray[0].i_vertices[2] = 5;
	faceArray[0].i_vertices[3] = 1;

	//+z face
	faceArray[1].num_verts = 4;
	faceArray[1].normal[0] = 0.0f;
	faceArray[1].normal[1] = 0.0f;
	faceArray[1].normal[2] = 1.0f;
	faceArray[1].i_vertices[0] = 1;
	faceArray[1].i_vertices[1] = 5;
	faceArray[1].i_vertices[2] = 6;
	faceArray[1].i_vertices[3] = 2;

	//-x face
	faceArray[2].num_verts = 4;
	faceArray[2].normal[0] = -1.0f;
	faceArray[2].normal[1] = 0.0f;
	faceArray[2].normal[2] = 0.0f;
	faceArray[2].i_vertices[0] = 2;
	faceArray[2].i_vertices[1] = 6;
	faceArray[2].i_vertices[2] = 7;
	faceArray[2].i_vertices[3] = 3;

	//-z face
	faceArray[3].num_verts = 4;
	faceArray[3].normal[0] = 0.0f;
	faceArray[3].normal[1] = 0.0f;
	faceArray[3].normal[2] = -1.0f;
	faceArray[3].i_vertices[0] = 3;
	faceArray[3].i_vertices[1] = 7;
	faceArray[3].i_vertices[2] = 4;
	faceArray[3].i_vertices[3] = 0;

	//+y face
	faceArray[4].num_verts = 4;
	faceArray[4].normal[0] = 0.0f;
	faceArray[4].normal[1] = 1.0f;
	faceArray[4].normal[2] = 0.0f;
	faceArray[4].i_vertices[0] = 4;
	faceArray[4].i_vertices[1] = 7;
	faceArray[4].i_vertices[2] = 6;
	faceArray[4].i_vertices[3] = 5;

	//-y face
	faceArray[5].num_verts = 4;
	faceArray[5].normal[0] = 0.0f;
	faceArray[5].normal[1] = -1.0f;
	faceArray[5].normal[2] = 0.0f;
	faceArray[5].i_vertices[0] = 1;
	faceArray[5].i_vertices[1] = 2;
	faceArray[5].i_vertices[2] = 3;
	faceArray[5].i_vertices[3] = 0;
	p_common->box_hull.faces = faceArray;

	edgeArray = (struct edge_struct*)malloc(12*sizeof(struct edge_struct));
	if(edgeArray == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	p_common->box_hull.num_edges = 12;
	//0 - 1
	edgeArray[0].i_vertices[0] = 0;
	edgeArray[0].i_vertices[1] = 1;
	edgeArray[0].i_face[0] = 0;
	edgeArray[0].i_face[1] = 5;
	vCrossProduct(edgeArray[0].normal, faceArray[0].normal, faceArray[5].normal);

	//1 - 2
	edgeArray[1].i_vertices[0] = 1;
	edgeArray[1].i_vertices[1] = 2;
	edgeArray[1].i_face[0] = 1;
	edgeArray[1].i_face[1] = 5;
	vCrossProduct(edgeArray[1].normal, faceArray[1].normal, faceArray[5].normal);

	//2 - 3
	edgeArray[2].i_vertices[0] = 2;
	edgeArray[2].i_vertices[1] = 3;
	edgeArray[2].i_face[0] = 2;
	edgeArray[2].i_face[1] = 5;
	vCrossProduct(edgeArray[2].normal, faceArray[2].normal, faceArray[5].normal);

	//3 - 0
	edgeArray[3].i_vertices[0] = 3;
	edgeArray[3].i_vertices[1] = 0;
	edgeArray[3].i_face[0] = 3;
	edgeArray[3].i_face[1] = 5;
	vCrossProduct(edgeArray[3].normal, faceArray[3].normal, faceArray[5].normal);

	//4 - 5
	edgeArray[4].i_vertices[0] = 4;
	edgeArray[4].i_vertices[1] = 5;
	edgeArray[4].i_face[0] = 4;
	edgeArray[4].i_face[1] = 0;
	vCrossProduct(edgeArray[4].normal, faceArray[4].normal, faceArray[0].normal);

	//5 - 6
	edgeArray[5].i_vertices[0] = 5;
	edgeArray[5].i_vertices[1] = 6;
	edgeArray[5].i_face[0] = 4;
	edgeArray[5].i_face[1] = 1;
	vCrossProduct(edgeArray[5].normal, faceArray[4].normal, faceArray[1].normal);

	//6 - 7
	edgeArray[6].i_vertices[0] = 6;
	edgeArray[6].i_vertices[1] = 7;
	edgeArray[6].i_face[0] = 4;
	edgeArray[6].i_face[1] = 2;
	vCrossProduct(edgeArray[6].normal, faceArray[4].normal, faceArray[2].normal);

	//7 - 4
	edgeArray[7].i_vertices[0] = 7;
	edgeArray[7].i_vertices[1] = 4;
	edgeArray[7].i_face[0] = 4;
	edgeArray[7].i_face[1] = 3;
	vCrossProduct(edgeArray[7].normal, faceArray[4].normal, faceArray[3].normal);

	//0 - 4
	edgeArray[8].i_vertices[0] = 0;
	edgeArray[8].i_vertices[1] = 4;
	edgeArray[8].i_face[0] = 0;
	edgeArray[8].i_face[1] = 3;
	vCrossProduct(edgeArray[8].normal, faceArray[0].normal, faceArray[3].normal);

	//1 - 5
	edgeArray[9].i_vertices[0] = 1;
	edgeArray[9].i_vertices[1] = 5;
	edgeArray[9].i_face[0] = 0;
	edgeArray[9].i_face[1] = 1;
	vCrossProduct(edgeArray[9].normal, faceArray[0].normal, faceArray[1].normal);

	//2 - 6
	edgeArray[10].i_vertices[0] = 2;
	edgeArray[10].i_vertices[1] = 6;
	edgeArray[10].i_face[0] = 1;
	edgeArray[10].i_face[1] = 2;
	vCrossProduct(edgeArray[10].normal, faceArray[1].normal, faceArray[2].normal);

	//3 - 7
	edgeArray[11].i_vertices[0] = 2;
	edgeArray[11].i_vertices[1] = 6;
	edgeArray[11].i_face[0] = 1;
	edgeArray[11].i_face[1] = 2;
	vCrossProduct(edgeArray[11].normal, faceArray[1].normal, faceArray[2].normal);
	p_common->box_hull.edges = edgeArray;
	
	return 1;
}

/*
This function creates an inventory for a crate.
*/
int InitCrate(struct moveable_object_struct * crate)
{
	int i;

	crate->moveable_type = (char)0; //set to crate
	
	crate->items = (struct item_inventory_struct*)malloc(sizeof(struct item_inventory_struct));
	if(crate->items == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	crate->items->num_items = 0;
	crate->items->max_items = 60; //6 rows of 10 slots.
	crate->items->dimensions[0] = 6;	//row
	crate->items->dimensions[1] = 10; 	//col
	crate->items->item_ptrs = (struct item_struct**)malloc(60*sizeof(struct item_struct *));
	if(crate->items->item_ptrs == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(crate->items->item_ptrs, 0, 60*sizeof(struct item_struct*));
	
	crate->items->slots = (struct inv_slot_struct*)malloc(60*sizeof(struct inv_slot_struct));
	if(crate->items->slots == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < crate->items->max_items; i++)
	{
		crate->items->slots[i].slot_index = -1;
		crate->items->slots[i].flags = (char)0;
	}

	return 1;
}

int TestFillCrate(struct moveable_object_struct * crate)
{
	struct item_struct * pitem=0;
	struct item_inventory_struct * crateInv=0;

	crateInv = crate->items;

	//Add rifle
	pitem = (struct item_struct *)malloc(sizeof(struct item_struct));
	if(pitem == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(pitem, 0, sizeof(struct item_struct));
	pitem->type = ITEM_TYPE_RIFLE;
	crateInv->item_ptrs[0] = pitem;
	crateInv->num_items += 1;
	crateInv->slots[0].slot_index = 0; //index of rifle in the linked list
	crateInv->slots[0].flags = 0;
	crateInv->slots[10].slot_index = 0; //fill all multi-slots with the index of the item in the linked list
	crateInv->slots[10].flags = SLOT_FLAGS_MULTISLOT;
	crateInv->slots[20].slot_index = 0;
	crateInv->slots[20].flags = SLOT_FLAGS_MULTISLOT;

	//Add bullets
	pitem = (struct item_struct *)malloc(sizeof(struct item_struct));
	if(pitem == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(pitem, 0, sizeof(struct item_struct));
	pitem->type = ITEM_TYPE_RIFLE_AMMO;
	crateInv->item_ptrs[1] = pitem;
	crateInv->num_items += 1;
	crateInv->slots[1].slot_index = 1; //index of bullets in the linked list

	//Add pistol bullets
	pitem = (struct item_struct*)malloc(sizeof(struct item_struct));
	if(pitem == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(pitem, 0, sizeof(struct item_struct));
	pitem->type = ITEM_TYPE_PISTOL_AMMO;
	crateInv->item_ptrs[2] = pitem;
	crateInv->num_items += 1;
	crateInv->slots[11].slot_index = 2; //index of pistol bullets in linked list

	//Add a pistol
	pitem = (struct item_struct*)malloc(sizeof(struct item_struct));
	if(pitem == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(pitem, 0, sizeof(struct item_struct));
	pitem->type = ITEM_TYPE_PISTOL;
	crateInv->item_ptrs[3] = pitem;
	crateInv->num_items += 1;
	crateInv->slots[2].slot_index = 3; //index of the pistol in the linked list

	return 1;
}

/*
This function places an item and puts it in the crateInv. This function will create an item
if flag not set.
-flags if set to 1, function must only add num_items = 1 and will not create item and instead 
place pitemToAdd
*/
int FillCrateWithItem(struct item_inventory_struct * crateInv, int num_items, char item_type, struct item_struct * pitemToAdd, char flags)
{
	struct item_struct * pitem=0;
	int itemDimensions[2]; //0=row, 1=col
	int numItemsToAdd;
	int wasMultislotSpotFound;
	int wasItemPlaced;
	int i;
	int j;
	int i_slot;
	int i_tempSlot; 
	int i_newItem; //index of new item in item_ptrs

	//Check parameters
	if((flags & 1) == 1) //mode is don't create new item
	{
		if(pitemToAdd == 0)
		{
			printf("%s: error. flags bit 1 set, but pitemToAdd is 0\n", __func__);
			return 0;
		}

		if(num_items != 1)
		{
			printf("%s: error. flags bit 1 set, but num_items is not 1\n", __func__);
			return 0;
		}
	}

	//Check if there is room, determine # of items to add
	if((crateInv->max_items - crateInv->num_items) <= 0)
	{
		printf("%s: trying to add %d items but crate is full.\n", __func__, num_items);
		return 0;
	}
	if(num_items > (crateInv->max_items - crateInv->num_items))
	{
		numItemsToAdd = crateInv->max_items - crateInv->num_items;
	}
	else //plenty of room
	{
		numItemsToAdd = num_items;
	}

	//determine the size of the item
	if(GUIIsItemMultiSlot(item_type))
	{
		itemDimensions[0] = 3; //# rows
		itemDimensions[1] = 1; //# cols
	}
	else
	{
		itemDimensions[0] = 1; //# rows
		itemDimensions[1] = 1; //# cols
	}

	i_slot = 0; //start at the bottom-left slot
 	for(i = 0; i < numItemsToAdd; i++)
	{
		wasItemPlaced = 0; //reset placement flag

		//get where the new item will go in item_ptrs
		i_newItem = InventoryGetFreeItemPtr(crateInv);
		if(i_newItem == -1)
		{
			printf("%s: error. no free elements in item_ptrs.\n", __func__);
			return 0;
		}

		//loop through all slots
		while(i_slot < crateInv->max_items)
		{
			//is spot in grid empty?
			if(crateInv->slots[i_slot].slot_index == -1)
			{
				//Are we trying to place a multi-slot item?
				if(GUIIsItemMultiSlot(item_type))
				{
					wasMultislotSpotFound=0; //initialize this flag. This flag will be set if there is enough room.

					//for multi-slot item need to check the slots above.
					for(j = 1; j < itemDimensions[0]; j++)
					{
						i_tempSlot = i_slot + (j*crateInv->dimensions[1]);
						
						//check that this index is still in the grid
						if(i_tempSlot >= crateInv->max_items)
						{
							wasMultislotSpotFound = 0;
							break;
						}

						if(crateInv->slots[i_tempSlot].slot_index == -1) //empty
						{
							wasMultislotSpotFound = 1;
							continue;
						}
						else //slot is not empty
						{
							wasMultislotSpotFound = 0;
							break;
						}
					}

					//did all the spots we check for the multislot item available?
					if(wasMultislotSpotFound == 1)
					{
						if((flags & 1) == 1)
						{
							crateInv->item_ptrs[i_newItem] = pitemToAdd; 
						}
						else
						{

							pitem = (struct item_struct*)malloc(sizeof(struct item_struct));
							if(pitem == 0)
							{
								printf("%s: error line %d\n", __func__, __LINE__);
								return 0;
							}
							memset(pitem, 0, sizeof(struct item_struct));
							pitem->type = item_type;
							crateInv->item_ptrs[i_newItem] = pitem;
						}
						crateInv->num_items += 1;

						//now mark the spots for the item
						for(j = 0; j < itemDimensions[0]; j++)
						{
							//do one last check to make sure the slots are free
							i_tempSlot = i_slot + (j*crateInv->dimensions[1]);
							if(crateInv->slots[i_tempSlot].slot_index != -1)
							{
								printf("%s: error. trying to add multislot item type=%hhu slot=%d not free.\n", __func__, item_type, (i_tempSlot+j));
								if(pitem != 0)
									free(pitem);
								crateInv->item_ptrs[i_newItem] = 0;
								crateInv->num_items -= 1;
								return 0;
							}

							crateInv->slots[i_tempSlot].slot_index = i_newItem;
							if(j == 0)
							{
								crateInv->slots[i_tempSlot].flags = 0; //for the bottom-most slot of multi-item set to 0
							}
							else
							{
								crateInv->slots[i_tempSlot].flags = SLOT_FLAGS_MULTISLOT;
							}
						}
						wasItemPlaced = 1; //indicate we placed the item
					}
				}
				else
				{
					//single slot item, easy
					if((flags && 1) == 1)
					{
						crateInv->item_ptrs[i_newItem] = pitemToAdd;
					}
					else
					{
						pitem = (struct item_struct *)malloc(sizeof(struct item_struct));
						if(pitem == 0)
						{
							printf("%s: error line %d\n", __func__, __LINE__);
							return 0;
						}
						memset(pitem, 0, sizeof(struct item_struct));
						pitem->type = item_type;
						crateInv->item_ptrs[i_newItem] = pitem;
					}
					crateInv->num_items += 1;
					crateInv->slots[i_slot].slot_index = i_newItem;
					crateInv->slots[i_slot].flags = 0;
					wasItemPlaced = 1;
				}
			}

			//go to next inventory gridslot
			if(i_slot >= crateInv->max_items) //max_items is total # of gridslots
				break;//ran out of room
			i_slot += 1;

			//stop looking for empty slots and go to the next new item.
			if(wasItemPlaced == 1)
				break;	
		}
	}

	return 1;
}

/*
This function places a dock at pos.
-only pos x and z coord are used. y is calculated based on terrain.
*/
int CreateDock(float * pos)
{
	struct moveable_object_struct * pdock=0;
	float surfPos[3];
	float surfNorm[3];
	int r;

	//Get a surface y coord
	r = GetTileSurfPoint(pos, surfPos, surfNorm);
	if(r != 1)
		return 0; //fail

	pdock = AddMoveableToTile(surfPos);
	if(pdock == 0)
		return 0; //fail
	pdock->moveable_type = MOVEABLE_TYPE_DOCK;
	pdock->items = 0; //dock has no items

	//set the dock to point towards -z axis
	pdock->yrot = 180.0f;

	return 1;
}

int CreateBunker(float * pos)
{
	struct moveable_object_struct * pbunker=0;
	float surfPos[3];
	float surfNorm[3];
	int r;

	//Get a surface y coord
	r = GetTileSurfPoint(pos, surfPos, surfNorm);
	if(r != 1)
		return 0;

	pbunker = AddMoveableToTile(surfPos);
	if(pbunker == 0)
		return 0;

	pbunker->moveable_type = MOVEABLE_TYPE_BUNKER;
	pbunker->items = 0; //bunker has no items

	return 1;
}

int CreateWarehouse(float * pos)
{
	struct moveable_object_struct * pwarehouse=0;
	float surfPos[3];
	float surfNorm[3];
	int r;

	//Get a surface y coord
	r = GetTileSurfPoint(pos, surfPos, surfNorm);
	if(r != 1)
		return 0;

	pwarehouse = AddMoveableToTile(surfPos);
	if(pwarehouse == 0)
		return 0;

	pwarehouse->moveable_type = MOVEABLE_TYPE_WAREHOUSE;
	pwarehouse->items = 0; //warehouse has no items

	return 1;
}

static void SetOrthoMat(float * ortho_mat, unsigned int width, unsigned int height)
{
	float fzNear;
	float fzFar;
	float fzWidth;
	float fzHeight;

	//These were original near,far planes and VBOs were being drawn at 0.0
	//fzNear = -1.0f;
	//fzFar = 1.0f;

	fzNear = 1.0f;
	fzFar = 100.0f;
	fzWidth = (float)width;		//assume: right-left=+width, so right=0 and left=width
	fzHeight = (float)height;

	memset(ortho_mat, 0, 16*sizeof(float));

	ortho_mat[0] = 2.0f/fzWidth;
	ortho_mat[5] = 2.0f/fzHeight;
	ortho_mat[10] = -2.0f/(fzFar - fzNear);
	ortho_mat[12] = -1.0f;
	ortho_mat[13] = -1.0f;
	ortho_mat[14] = (-1.0f*(fzFar+fzNear))/(fzFar-fzNear);
	ortho_mat[15] = 1.0f;
}

int InitInvGUIVBO(struct simple_gui_info_struct * guiInfo)
{
	//50 x 50 box on 1024x768 screen seems good for an inventory grid square.
	//6 vertices of 5 pos floats
	float * vbo_data=0;		//this is to allocate enough memory for all GUI objects.
	float * temp_data=0;	//this is just for a box for testing
	float * pinventoryGridPos=0;
	float lowerLeftCornerPos[3] = {404.0f, 385.0f, 0.0f};	//coords are from bottom-left
	float grid_block_width = 50.0f;
	float fcrosshairWidth = 5.0f;
	float uvboxwidth;
	//float upperLeftVert[2];	//x,y coordinate
	//float upperRightVert[2];
	//float lowerLeftVert[2];		//x,y coordinate
	//float lowerRightVert[2];
	float * curVert=0;
	int i;
	int j;
	int r;

	memset(guiInfo, 0, sizeof(struct simple_gui_info_struct));

	//calculate the uv coords for box width
	uvboxwidth = ((float)64.0f)/((float)512.0f);

	//save the size of a grid block
	guiInfo->grid_block_width = grid_block_width;

	//Get total number of vertices from all GUI objects
	guiInfo->item_box.first_vert=0;
	guiInfo->item_box.vert_count=6;

	guiInfo->long_box.first_vert=6;
	guiInfo->long_box.vert_count=6;

	guiInfo->crosshair.first_vert = 12;
	guiInfo->crosshair.vert_count = 4;

	//this should equal 16
	guiInfo->num_verts = guiInfo->item_box.vert_count + guiInfo->long_box.vert_count + guiInfo->crosshair.vert_count;
	if(guiInfo->num_verts != 16)
	{
		printf("%s: error. guiInfo->num_verts=%d. expected %d\n", __func__, guiInfo->num_verts, 114);
		return 0;
	}

	vbo_data = (float*)malloc(guiInfo->num_verts*5*sizeof(float));//3 pos + 2 tex-coords
	if(vbo_data == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}

	//fill in temp_data to make a simple cube in the center of the screen
	temp_data = vbo_data;

	//3 pos + 2 tex-coord floats per vertex = 5 total floats-per-vert
	//top-right
	temp_data[0] = 50.0f;
	temp_data[1] = 50.0f;
	temp_data[2] = 0.0f;
	temp_data[3] = uvboxwidth;
	temp_data[4] = uvboxwidth;

	//top-left
	temp_data[5] = 0.0f;
	temp_data[6] = 50.0f;
	temp_data[7] = 0.0f;
	temp_data[8] = 0.0f;
	temp_data[9] = uvboxwidth;

	//bottom-left
	temp_data[10] = 0.0f;
	temp_data[11] = 0.0f;
	temp_data[12] = 0.0f;
	temp_data[13] = 0.0f;	//u tex-coord. These don't do anything but I added them as a test to see if I can use different shaders with the same VBO.
	temp_data[14] = 0.0f;	//v tex-coord

	//bottom-left
	temp_data[15] = 0.0f;
	temp_data[16] = 0.0f;
	temp_data[17] = 0.0f;
	temp_data[18] = 0.0f;
	temp_data[19] = 0.0f;

	//bottom-right
	temp_data[20] = 50.0f;
	temp_data[21] = 0.0f;
	temp_data[22] = 0.0f;
	temp_data[23] = uvboxwidth;
	temp_data[24] = 0.0f;

	//top-right
	temp_data[25] = 50.0f;
	temp_data[26] = 50.0f;
	temp_data[27] = 0.0f;
	temp_data[28] = uvboxwidth;
	temp_data[29] = uvboxwidth; 

	//set vertices for long box (action boxes on either side of character, or rifle item)
	temp_data = vbo_data + (6*5);	//6 verts * 5 floats-per-vert
	//top-right
	temp_data[0] = 50.0f;
	temp_data[1] = 150.0f;
	temp_data[2] = 0.0f;
	temp_data[3] = uvboxwidth;
	temp_data[4] = 3.0f*uvboxwidth;

	//top-left
	temp_data[5] = 0.0f;
	temp_data[6] = 150.0f;
	temp_data[7] = 0.0f;
	temp_data[8] = 0.0f;
	temp_data[9] = 3.0f*uvboxwidth;

	//bottom-left
	temp_data[10] = 0.0f;
	temp_data[11] = 0.0f;
	temp_data[12] = 0.0f;
	temp_data[13] = 0.0f;	//u tex-coord. These don't do anything but I added them as a test to see if I can use different shaders with the same VBO.
	temp_data[14] = 0.0f;	//v tex-coord

	//bottom-left
	temp_data[15] = 0.0f;
	temp_data[16] = 0.0f;
	temp_data[17] = 0.0f;
	temp_data[18] = 0.0f;
	temp_data[19] = 0.0f;

	//bottom-right
	temp_data[20] = 50.0f;
	temp_data[21] = 0.0f;
	temp_data[22] = 0.0f;
	temp_data[23] = uvboxwidth;
	temp_data[24] = 0.0f;

	//top-right
	temp_data[25] = 50.0f;
	temp_data[26] = 150.0f;
	temp_data[27] = 0.0f;
	temp_data[28] = uvboxwidth;
	temp_data[29] = 3.0f*uvboxwidth;

	//set vertices for crosshair
	temp_data = vbo_data + (12*5);
	//vertical line
	temp_data[0] = 0.0f;
	temp_data[1] = fcrosshairWidth;
	temp_data[2] = 0.0f;
	temp_data[3] = 0.0f;
	temp_data[4] = 0.0f;

	temp_data[5] = 0.0f;
	temp_data[6] = -1.0f*fcrosshairWidth;
	temp_data[7] = 0.0f;
	temp_data[8] = 0.0f;
	temp_data[9] = 0.0f;

	//horizonal line
	temp_data[10] = -1.0f*fcrosshairWidth;
	temp_data[11] = 0.0f;
	temp_data[12] = 0.0f;
	temp_data[13] = 0.0f;
	temp_data[14] = 0.0f;

	temp_data[15] = fcrosshairWidth;
	temp_data[16] = 0.0f;
	temp_data[17] = 0.0f;
	temp_data[18] = 0.0f;
	temp_data[19] = 0.0f;

	//Make an inventory grid
	guiInfo->num_inv_grid_slots = 18; //item slots. 3 rows of 6 grids each.
	guiInfo->inv_grid_slot_positions = (float*)malloc(guiInfo->num_inv_grid_slots*2*sizeof(float));
	if(guiInfo->inv_grid_slot_positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	//There are 6 grids in a row and 3 rows. 18 grid-squares. Each grid has 6 vertices.
	for(i = 0; i < 3; i++)	//3 rows of grid blocks
	{
		for(j = 0; j < 6; j++)	//6 grid blocks in a row 
		{
			//calculate the positions of the 4 corners.
			curVert = guiInfo->inv_grid_slot_positions + (((i*6)+j)*2);
			curVert[0] = lowerLeftCornerPos[0] + (j * grid_block_width); //this is lower-left corner of box
			curVert[1] = lowerLeftCornerPos[1] + (i * grid_block_width);
			
		}
	}

	//Make a ground grid. 3 rows of 20 grid-squares.
	lowerLeftCornerPos[0] = 12.0f;
	lowerLeftCornerPos[1] = 0.0f;
	guiInfo->num_ground_grid_slots = 60;
	guiInfo->ground_grid_slot_positions = (float*)malloc(guiInfo->num_ground_grid_slots*2*sizeof(float));
	if(guiInfo->ground_grid_slot_positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < 3; i++)
	{
		for(j = 0; j < 20; j++)
		{
			curVert = guiInfo->ground_grid_slot_positions + (((i*20)+j)*2);
			curVert[0] = lowerLeftCornerPos[0] + (j * grid_block_width);
			curVert[1] = lowerLeftCornerPos[1] + (i * grid_block_width);
		}
	}

	//Make a crate grid.  6 rows of 10.
	lowerLeftCornerPos[0] = 512.0f;
	lowerLeftCornerPos[1] = 0.0f;
	guiInfo->num_crate_grid_slots = 60;
	guiInfo->crate_grid_slot_positions = (float*)malloc(guiInfo->num_crate_grid_slots*2*sizeof(float));
	if(guiInfo->crate_grid_slot_positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < 6; i++)
	{
		for(j = 0; j < 10; j++)
		{
			curVert = guiInfo->crate_grid_slot_positions + (((i*10)+j)*2);
			curVert[0] = lowerLeftCornerPos[0] + (j * grid_block_width);
			curVert[1] = lowerLeftCornerPos[1] + (i * grid_block_width);
		}
	}

	//Make action slots
	guiInfo->num_action_slots = 2;
	guiInfo->action_slot_positions = (float*)malloc(guiInfo->num_action_slots*2*sizeof(float));
	if(guiInfo->action_slot_positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	guiInfo->action_slot_positions[0] = 50.0f;
	guiInfo->action_slot_positions[1] = 384.0f;

	guiInfo->action_slot_positions[2] = 255.0f;
	guiInfo->action_slot_positions[3] = 384.0f;

	//Make an array of item texture offsets of the texture atlas
	guiInfo->num_itemUVOffsets = 8;
	guiInfo->itemUVOffsets = (float*)malloc(8*2*sizeof(float));
	if(guiInfo->itemUVOffsets == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	//unused
	guiInfo->itemUVOffsets[0] = 0.0f;
	guiInfo->itemUVOffsets[1] = 0.0f;

	//ITEM_TYPE_BEANS
	guiInfo->itemUVOffsets[2] = 3.0f*uvboxwidth;
	guiInfo->itemUVOffsets[3] = 0.0f;

	//ITEM_TYPE_CANTEEN
	guiInfo->itemUVOffsets[4] = 2.0f*uvboxwidth;
	guiInfo->itemUVOffsets[5] = 0.0f;

	//ITEM_TYPE_PISTOL_AMMO
	guiInfo->itemUVOffsets[6] = 1.0f*uvboxwidth;
	guiInfo->itemUVOffsets[7] = 0.0f;

	//ITEM_TYPE_RIFLE_AMMO
	guiInfo->itemUVOffsets[8] = 1.0f*uvboxwidth;
	guiInfo->itemUVOffsets[9] = 1.0f*uvboxwidth;

	//ITEM_TYPE_RIFLE
	guiInfo->itemUVOffsets[10] = 0.0f;
	guiInfo->itemUVOffsets[11] = 0.0f;

	//ITEM_TYPE_PISTOL
	guiInfo->itemUVOffsets[12] = 4.0f*uvboxwidth;
	guiInfo->itemUVOffsets[13] = 0.0f;

	//ITEM_TYPE_SHOVEL
	guiInfo->itemUVOffsets[14] = 0.0f; //TODO: Update these offsets
	guiInfo->itemUVOffsets[15] = 0.0f;

	//setup the VBO
	glGenBuffers(1, &(guiInfo->box_vbo));
	glBindBuffer(GL_ARRAY_BUFFER, guiInfo->box_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(5*guiInfo->num_verts*sizeof(float)), vbo_data, GL_STATIC_DRAW); //5 floats-per-vert
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//setup the VAO
	glGenVertexArrays(1, &(guiInfo->box_vao));
	glBindVertexArray(guiInfo->box_vao);
	glBindBuffer(GL_ARRAY_BUFFER, guiInfo->box_vbo);
	glEnableVertexAttribArray(0);	//vertex pos
	glEnableVertexAttribArray(1);	//tex coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), 0);	//in this case stride to skip the 2 uv-coordinates.
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glBindVertexArray(0);

	free(vbo_data);

	//setup texture atlas
	r = LoadBushTextureGenMip(&(guiInfo->texture_id), "item_atlas.tga");
	//r = LoadBushTextureGenMip(&(guiInfo->texture_id), "./resources/textures/blue.tga");
	if(r == 0)
		return 0;

	return 1; //success
}

/*
This func loads/compiles shader code.
note: param init_shader_index specifies extra uniform loading that isn't common.
*/
int InitGUIShaders(struct gui_shader_struct * guiShader, char * vert_shader_filename, char * frag_shader_filename, int init_shader_index)
{
	char * vertexShaderString=0;
	char * fragmentShaderString=0;
	float color[3] = {1.0f, 1.0f, 1.0f};
	GLint status;
	GLint infoLogLength;
	GLchar * strInfoLog;

	//compile the vertex shader
	vertexShaderString = LoadShaderSource(vert_shader_filename);
	if(vertexShaderString == 0)
		return 0;
	guiShader->shaderList[0] = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(guiShader->shaderList[0], 1, &vertexShaderString, 0);
	glCompileShader(guiShader->shaderList[0]);
	glGetShaderiv(guiShader->shaderList[0], GL_COMPILE_STATUS, &status);
	free(vertexShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(guiShader->shaderList[0], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		glGetShaderInfoLog(guiShader->shaderList[0], infoLogLength, 0, strInfoLog);
		printf("%s: vertex shader (%d) compile fail:\n%s\n", __func__, guiShader->shaderList[0], strInfoLog);
		free(strInfoLog);
		return 0;
	}

	//compile the fragment shader
	status = 0;
	fragmentShaderString = LoadShaderSource(frag_shader_filename);
	if(fragmentShaderString == 0)
		return 0;
	guiShader->shaderList[1] = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(guiShader->shaderList[1], 1, &fragmentShaderString, 0);
	glCompileShader(guiShader->shaderList[1]);
	glGetShaderiv(guiShader->shaderList[1], GL_COMPILE_STATUS, &status);
	free(fragmentShaderString);
	if(status == GL_FALSE)
	{
		glGetShaderiv(guiShader->shaderList[1], GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		glGetShaderInfoLog(guiShader->shaderList[1], infoLogLength, 0, strInfoLog);
		printf("%s: frag shader (%d) compile fail:\n%s\n", __func__, guiShader->shaderList[1], strInfoLog);
		free(strInfoLog);
		return 0;
	}

	//link the program
	status = 0;
	guiShader->program = glCreateProgram();
	glAttachShader(guiShader->program, guiShader->shaderList[0]);
	glAttachShader(guiShader->program, guiShader->shaderList[1]);
	glLinkProgram(guiShader->program);
	glGetProgramiv(guiShader->program, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(guiShader->program, GL_INFO_LOG_LENGTH, &infoLogLength);
		strInfoLog = (GLchar*)malloc((infoLogLength+1)*sizeof(GLchar));
		if(strInfoLog == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		glGetProgramInfoLog(guiShader->program, infoLogLength, 0, strInfoLog);
		printf("%s: link failure:\n%s\n", __func__, strInfoLog);
		free(strInfoLog);
		return 0;
	}

	glDetachShader(guiShader->program, guiShader->shaderList[0]);
	glDetachShader(guiShader->program, guiShader->shaderList[1]);

	glUseProgram(guiShader->program);

	//Get all common uniform locations
	guiShader->uniforms[0] = glGetUniformLocation(guiShader->program, "projectionMat");
	if(guiShader->uniforms[0] == -1)
	{
		printf("%s: error. failed to get uniform location of %s\n", __func__, "projectionMat");
		return 0;
	}

	guiShader->uniforms[1] = glGetUniformLocation(guiShader->program, "fillColor");
	if(guiShader->uniforms[1] == -1)
	{
		printf("%s: error. failed to get uniform location of %s\n", __func__, "fillColor");
		return 0;
	}
	glUniform3fv(guiShader->uniforms[1], 1, color); //set fillColor to a default white.

	guiShader->uniforms[2] = glGetUniformLocation(guiShader->program, "modelToCameraMat");
	if(guiShader->uniforms[2] == -1)
	{
		printf("%s: error. failed to get uniform location of %s\n", __func__, "modelToCameraMat");
		return 0;
	}

	//check if we need to load extra uniforms
	if(init_shader_index == 1)
	{
		guiShader->uniforms[3] = glGetUniformLocation(guiShader->program, "texCoordOffset");
		if(guiShader->uniforms[3] == -1)
		{
			printf("%s: error. failed to get uniform location of %s\n", __func__, "texCoordOffset");
			return 0;
		}
		guiShader->uniforms[4] = glGetUniformLocation(guiShader->program, "colorTexture");
		if(guiShader->uniforms[4] == -1)
		{
			printf("%s: error. failed to get uniform location of %s\n", __func__, "colorTexture");
			return 0;
		}
		guiShader->colorTexUnit = 0;
		glUniform1iv(guiShader->uniforms[4], 1, &(guiShader->colorTexUnit));
	}

	glUseProgram(0);

	return 1;
}

void SwitchRenderMode(int mode)
{
	g_render_mode = mode;
	switch(mode)
	{
	case 0:	//Normal Terrain Screen
		glClearColor(1.0f,1.0f,1.0f,0.0f); //white background
		glDisable(GL_BLEND);	//GL_BLEND is initially disabled and make sure it is disabled when switching to main screen.
		
		glUseProgram(g_bush_shader.program);
		glUniformMatrix4fv(g_bush_shader.perspectiveMatrixUnif,
				1,
				GL_FALSE,
				g_perspectiveMatrix);
		glUseProgram(0);

		glUseProgram(g_character_shader.program);
		glUniformMatrix4fv(g_character_shader.perspectiveMatrixUnif,
				1,
				GL_FALSE,
				g_perspectiveMatrix);
		glUseProgram(0);
		
		g_DrawFunc = DrawScene;

		break;
	case 1:	//Inventory Screen
		//glClearColor(1.0f,1.0f,1.0f,0.0f); //white background
		//glClearColor(0.0f,0.0f,0.0f,0.0f);//black background
		glClearColor(0.623f, 0.623f, 0.623f, 0.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//Change the projection matrix of the g_bush_shader so we
		//can use it to draw 3D objects orthographically in the
		//inventory screen.
		glUseProgram(g_bush_shader.program);
		glUniformMatrix4fv(g_bush_shader.perspectiveMatrixUnif,
				1,
				GL_FALSE,
				g_orthoMat4);

		glUseProgram(g_character_shader.program);
		glUniformMatrix4fv(g_character_shader.perspectiveMatrixUnif,
				1,
				GL_FALSE,
				g_orthoMat4);
		
		glUseProgram(g_gui_shaders.program);
		glUniformMatrix4fv(g_gui_shaders.uniforms[0], 1, GL_FALSE, g_orthoMat4);
		glUseProgram(0);

		g_DrawFunc = DrawInvGUI;

		break;
	case 2: //map screen
		glClearColor(0.823f, 0.941f, 0.933f, 0.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(g_gui_shaders.program);
		glUniformMatrix4fv(g_gui_shaders.uniforms[0], 1, GL_FALSE, g_mapOrthoMat4);

		glUseProgram(g_textShader.program);
		glUniformMatrix4fv(g_textShader.uniforms[0], 1, GL_FALSE, g_mapOrthoMat4);
		glUseProgram(0);	

		g_DrawFunc = DrawMapGUI;
		break;
	}
}

void DrawInvGUI(void)
{
	struct item_inventory_struct * crateInventory=0;
	struct item_inventory_struct * playerInventory=0;
	struct inv_slot_struct * invSlot=0;
	float color[3] = {1.0f, 1.0f, 1.0f};
	float texCoords[2];
	float temp_vec[3];
	float mModelMatrix[16];
	float mScaleMat[16];
	int i, j;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//Draw 3d crate model in inventory
	//uniform perspectiveMatrix has already been set by SwitchRenderMode
	if(g_gui_info.bottom_grid_mode == GUI_BOTTOMGRID_CRATE)
	{
		glUseProgram(g_bush_shader.program);
		mmMakeIdentityMatrix(mScaleMat);
		mScaleMat[0] = 100.0f;	//crate vertices sized so it is 1.0 width, scale so it takes up 100 px on 1024x768 screen
		mScaleMat[5] = 100.0f;
		mScaleMat[10] = 1.0f;	//don't scale in the z dir because the vertices need to stay between the near and far plane
		mmTranslateMatrix(mModelMatrix, 256.0f, 150.0f, -11.0f); //position the crate in the lower-left hand side.
		mmMultiplyMatrix4x4(mModelMatrix, mScaleMat, mModelMatrix);
		glUniformMatrix4fv(g_bush_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelMatrix);
		glBindTexture(GL_TEXTURE_2D, g_crate_model_common.texture_id);
		glBindVertexArray(g_crate_model_common.vao);
		glDrawElements(GL_TRIANGLES,
				g_crate_model_common.num_indices,
				GL_UNSIGNED_INT,
				0);
	}

	//Draw an infantry guy by the action slots (uniform perspectiveMatrix has already been set by SwitchRenderMode
	mmMakeIdentityMatrix(mScaleMat);
	mScaleMat[0] = 200.0f;
	mScaleMat[5] = 200.0f;
	mScaleMat[10] = 1.0f;
	mmTranslateMatrix(mModelMatrix, 180.0f, 384.0f, -11.0f);
	mmMultiplyMatrix4x4(mModelMatrix, mScaleMat, mModelMatrix);	
	glUseProgram(g_character_shader.program);
	glUniform3fv(g_character_shader.modelSpaceCameraPosUnif, 1, g_ws_camera_pos); //what should this be set to?
	glUniformMatrix4fv(g_character_shader.modelToCameraMatrixUnif, 1, GL_FALSE, mModelMatrix);
	glBindSampler(g_character_shader.colorTexUnit, g_triangle_man.sampler);
	glBindVertexArray(g_triangle_man.vao);
	for(i = 0; i < g_triangle_man.num_materials; i++)
	{
		glBindTexture(GL_TEXTURE_2D, g_triangle_man.textureIds[i]);
		glDrawElementsBaseVertex(GL_TRIANGLES,		//mode.
				g_triangle_man.num_indices[i],		//count. # of elements to be rendered
				GL_UNSIGNED_INT,					//type of indices
				0,									//addr of indices
				g_triangle_man.baseVertices[i]);	//basevertex. constant to add to each index.
	}


	glBindVertexArray(g_gui_info.box_vao);

	//just draw one textured block for now
	glUseProgram(g_guitex_shaders.program);
	glBindTexture(GL_TEXTURE_2D, g_gui_info.texture_id);
	glBindSampler(0, g_bush_branchtex_sampler);

	//draw textured blocks for player inventory
	playerInventory = g_a_man.p_inventory;
	for(i = 0; i < playerInventory->dimensions[0]; i++)
	{
		for(j = 0; j < playerInventory->dimensions[1]; j++)
		{
			invSlot = playerInventory->slots + ((i*playerInventory->dimensions[1])+j);

			//check that slot isn't empty
			if(invSlot->slot_index >= 0)
			{
				//skip if slot is an extra slot for a multi-slot item.
				if((invSlot->flags & SLOT_FLAGS_MULTISLOT) == SLOT_FLAGS_MULTISLOT)
					continue;

				//set the texture coordiantes based on item type.
				glUniform2fv(g_guitex_shaders.uniforms[3],
						1,
						GUIGetItemTexCoords((int)(playerInventory->item_ptrs[invSlot->slot_index]->type)));
				mmTranslateMatrix(mModelMatrix,
					g_gui_info.inv_grid_slot_positions[((playerInventory->dimensions[1]*i)+j)*2],
					g_gui_info.inv_grid_slot_positions[(((playerInventory->dimensions[1]*i)+j)*2)+1],
					-1.0f);
				glUniformMatrix4fv(g_guitex_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);

				if(GUIIsItemMultiSlot(playerInventory->item_ptrs[invSlot->slot_index]->type))
				{
					//draw 3 high slot
					glDrawArrays(GL_TRIANGLES,
							g_gui_info.long_box.first_vert,
							g_gui_info.long_box.vert_count);
				}
				else
				{
					//draw normal slot
					glDrawArrays(GL_TRIANGLES,
							g_gui_info.item_box.first_vert,
							g_gui_info.item_box.vert_count);
				}
			}
		}
	}

	//draw textured blocks for action slots
	for(i = 0; i < 2; i++)
	{
		if(g_a_man.p_actionSlots[i] == 0)
			continue;

		glUniform2fv(g_guitex_shaders.uniforms[3],
				1,
				GUIGetItemTexCoords(g_a_man.p_actionSlots[i]->type));
		mmTranslateMatrix(mModelMatrix,
				g_gui_info.action_slot_positions[(i*2)],
				g_gui_info.action_slot_positions[(i*2)+1],
				-1.0f);
		glUniformMatrix4fv(g_guitex_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
		if(GUIIsItemMultiSlot(g_a_man.p_actionSlots[i]->type))
		{
			glDrawArrays(GL_TRIANGLES,
					g_gui_info.long_box.first_vert,
					g_gui_info.long_box.vert_count);
		}
		else
		{
			glDrawArrays(GL_TRIANGLES,
					g_gui_info.item_box.first_vert,
					g_gui_info.item_box.vert_count);
		}
	}

	//draw textured blocks for ground/crate slots
	switch(g_gui_info.bottom_grid_mode)
	{
	case GUI_BOTTOMGRID_GROUND:
		for(i = 0; i < g_temp_ground_slots.dimensions[0]; i++)
		{
			for(j = 0; j < g_temp_ground_slots.dimensions[1]; j++)
			{
				invSlot = g_temp_ground_slots.slots + ((i*g_temp_ground_slots.dimensions[1]) + j);

				//slot not empty. (empty slots would be -1)
				if(invSlot->slot_index >= 0)
				{
					//skip slot if it is an extra slot for a multi-slot item.
					if((invSlot->flags & SLOT_FLAGS_MULTISLOT) == SLOT_FLAGS_MULTISLOT)
						continue;

					glUniform2fv(g_guitex_shaders.uniforms[3],
							1,
							GUIGetItemTexCoords((int)(g_temp_ground_slots.item_ptrs[invSlot->slot_index]->type)));
					mmTranslateMatrix(mModelMatrix,
							g_gui_info.ground_grid_slot_positions[(((i*g_temp_ground_slots.dimensions[1]) + j)*2)],
							g_gui_info.ground_grid_slot_positions[(((i*g_temp_ground_slots.dimensions[1]) + j)*2)+1],
							-1.0f);
					glUniformMatrix4fv(g_guitex_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);

					if(GUIIsItemMultiSlot(g_temp_ground_slots.item_ptrs[invSlot->slot_index]->type))
					{
						//three high slot
						glDrawArrays(GL_TRIANGLES,
							g_gui_info.long_box.first_vert,
							g_gui_info.long_box.vert_count);
					}
					else
					{
						//normal slot
						glDrawArrays(GL_TRIANGLES,
								g_gui_info.item_box.first_vert,
								g_gui_info.item_box.vert_count);
					}
				}
			}
		}
		break;
	case GUI_BOTTOMGRID_CRATE:
		crateInventory = g_gui_info.selectedCrate->items;
		for(i = 0; i < crateInventory->dimensions[0]; i++)
		{
			for(j = 0; j < crateInventory->dimensions[1]; j++)
			{
				invSlot = crateInventory->slots + ((i*crateInventory->dimensions[1])+j);

				//slot not empty. empty slots are set to -1.
				if(invSlot->slot_index >= 0)
				{
					//skip if slot is an extra slot for a multi-slot item.
					if((invSlot->flags & SLOT_FLAGS_MULTISLOT) == SLOT_FLAGS_MULTISLOT)
						continue;

					glUniform2fv(g_guitex_shaders.uniforms[3], //set the tex coordinates in the texture atlas based on item type.
							1, 
							GUIGetItemTexCoords((int)(crateInventory->item_ptrs[invSlot->slot_index]->type)));
					mmTranslateMatrix(mModelMatrix,
						g_gui_info.crate_grid_slot_positions[(((i*crateInventory->dimensions[1])+j)*2)],
						g_gui_info.crate_grid_slot_positions[(((i*crateInventory->dimensions[1])+j)*2)+1],
						-1.0f);
					glUniformMatrix4fv(g_guitex_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);

					if(GUIIsItemMultiSlot(crateInventory->item_ptrs[invSlot->slot_index]->type))
					{
						//three high slot
						glDrawArrays(GL_TRIANGLES,
							g_gui_info.long_box.first_vert,
							g_gui_info.long_box.vert_count);
					}
					else
					{
						//normal slot
						glDrawArrays(GL_TRIANGLES,
								g_gui_info.item_box.first_vert,
								g_gui_info.item_box.vert_count);
					}
				}
			}
		}
		break;
	}
	
	//Now Draw white gridlines
	//loop through all inventory grid slots
	glUseProgram(g_gui_shaders.program);
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	playerInventory = g_a_man.p_inventory;
	for(i = 0; i < playerInventory->dimensions[0]; i++)
	{
		for(j = 0; j < playerInventory->dimensions[1]; j++)
		{
			invSlot = playerInventory->slots + ((playerInventory->dimensions[1]*i) + j);
			if((invSlot->flags & SLOT_FLAGS_MULTISLOT) != SLOT_FLAGS_MULTISLOT)
			{
				mmTranslateMatrix(mModelMatrix,
						g_gui_info.inv_grid_slot_positions[((playerInventory->dimensions[1]*i) + j)*2],
						g_gui_info.inv_grid_slot_positions[(((playerInventory->dimensions[1]*i) + j)*2)+1],
						-1.0f);
				glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
				if(invSlot->slot_index != -1 && GUIIsItemMultiSlot(playerInventory->item_ptrs[invSlot->slot_index]->type))
				{
					glDrawArrays(GL_LINE_STRIP,
						g_gui_info.long_box.first_vert,
						g_gui_info.long_box.vert_count);
				}
				else
				{
					glDrawArrays(GL_LINE_STRIP,
						g_gui_info.item_box.first_vert,
						g_gui_info.item_box.vert_count);
				}
			}
		}
	}

	//draw white grid lines:
	switch(g_gui_info.bottom_grid_mode)
	{
	case GUI_BOTTOMGRID_GROUND:
		//loop through all ground slots
		for(i = 0; i < g_temp_ground_slots.dimensions[0]; i++)
		{
			for(j = 0; j < g_temp_ground_slots.dimensions[1]; j++)
			{
				invSlot = g_temp_ground_slots.slots + ((i*g_temp_ground_slots.dimensions[1]) + j);
				if((invSlot->flags & SLOT_FLAGS_MULTISLOT) != SLOT_FLAGS_MULTISLOT)
				{
					mmTranslateMatrix(mModelMatrix,
						g_gui_info.ground_grid_slot_positions[((i*g_temp_ground_slots.dimensions[1]) + j)*2],
						g_gui_info.ground_grid_slot_positions[(((i*g_temp_ground_slots.dimensions[1]) + j)*2)+1],
						-1.0f);
					glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
					if(invSlot->slot_index != -1 && GUIIsItemMultiSlot(g_temp_ground_slots.item_ptrs[invSlot->slot_index]->type))
					{
						glDrawArrays(GL_LINE_STRIP,
							g_gui_info.long_box.first_vert,
							g_gui_info.long_box.vert_count);
					}
					else
					{
						glDrawArrays(GL_LINE_STRIP,
							g_gui_info.item_box.first_vert,
							g_gui_info.item_box.vert_count);
					}
				}
			}
		}
		break;

	case GUI_BOTTOMGRID_CRATE:
		crateInventory = g_gui_info.selectedCrate->items;
		for(i = 0; i < crateInventory->dimensions[0]; i++)
		{
			for(j = 0; j < crateInventory->dimensions[1]; j++)
			{
				invSlot = crateInventory->slots + ((crateInventory->dimensions[1]*i) + j);
				if((invSlot->flags & SLOT_FLAGS_MULTISLOT) != SLOT_FLAGS_MULTISLOT)
				{
					mmTranslateMatrix(mModelMatrix,
							g_gui_info.crate_grid_slot_positions[((crateInventory->dimensions[1]*i)+j)*2],
							g_gui_info.crate_grid_slot_positions[(((crateInventory->dimensions[1]*i)+j)*2)+1],
							-1.0f);
					glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
					if(invSlot->slot_index != -1 && (GUIIsItemMultiSlot(crateInventory->item_ptrs[invSlot->slot_index]->type)))
					{
						glDrawArrays(GL_LINE_STRIP,
							g_gui_info.long_box.first_vert,
							g_gui_info.long_box.vert_count);
					}
					else
					{
						glDrawArrays(GL_LINE_STRIP,
							g_gui_info.item_box.first_vert,
							g_gui_info.item_box.vert_count);
					}
				}
			}
		}
		break;
	}

	//draw the action slots
	for(i = 0; i < g_gui_info.num_action_slots; i++)
	{
		mmTranslateMatrix(mModelMatrix,
				g_gui_info.action_slot_positions[(i*2)],
				g_gui_info.action_slot_positions[(i*2)+1],
				-1.0f);
		glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
		glDrawArrays(GL_LINE_STRIP,
				g_gui_info.long_box.first_vert,
				g_gui_info.long_box.vert_count);
	}

	//If we are dragging an item draw it
	if((g_gui_inv_cursor.flags & GUI_FLAGS_DRAG) == GUI_FLAGS_DRAG)
	{
		glUseProgram(g_guitex_shaders.program);
		glUniform2fv(g_guitex_shaders.uniforms[3],
				1,
				GUIGetItemTexCoords(g_gui_inv_cursor.draggedItem->type));
		GUIGetItemCursorOffset(temp_vec, g_gui_inv_cursor.draggedItem->type);
		mmTranslateMatrix(mModelMatrix,
				g_gui_inv_cursor.pos[0] + temp_vec[0],
				g_gui_inv_cursor.pos[1] + temp_vec[1],
				-1.0f);
		glUniformMatrix4fv(g_guitex_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
		if(GUIIsItemMultiSlot(g_gui_inv_cursor.draggedItem->type))
		{
			//three high slot
			glDrawArrays(GL_TRIANGLES,
				g_gui_info.long_box.first_vert,
				g_gui_info.long_box.vert_count);
		}
		else
		{
			//normal slot
			glDrawArrays(GL_TRIANGLES,
				g_gui_info.item_box.first_vert,
				g_gui_info.item_box.vert_count);
		}
	}

	//draw a test crosshair
	color[0] = 1.0f;
	color[1] = 0.0f;
	color[2] = 0.0f;
	glUseProgram(g_gui_shaders.program);
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	mmTranslateMatrix(mModelMatrix,
			g_gui_inv_cursor.pos[0],
			g_gui_inv_cursor.pos[1],
			-1.0f);
	glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelMatrix);
	glDrawArrays(GL_LINES,
			g_gui_info.crosshair.first_vert,
			g_gui_info.crosshair.vert_count);

	glBindVertexArray(0);
	glUseProgram(0);	
}

/*
This function given an itemType returns the corresponding uv tex coords in
the GUI item texture atlas.
*/
float * GUIGetItemTexCoords(char itemType)
{
	return (g_gui_info.itemUVOffsets + (2*((int)itemType)));
}

/*
This func returns 1 if the item is 3 slots high, or 0 if it is normal
*/
int GUIIsItemMultiSlot(char itemType)
{
	if(itemType == ITEM_TYPE_RIFLE || itemType == ITEM_TYPE_SHOVEL)
		return 1;
	else
		return 0;
}

void GUIGetItemCursorOffset(float * offset, char itemType)
{
	if(GUIIsItemMultiSlot(itemType))
	{
		offset[0] = -0.5f*g_gui_info.grid_block_width;
		offset[1] = -0.5f*3.0f*g_gui_info.grid_block_width;
	}
	else
	{
		offset[0] = -0.5f*g_gui_info.grid_block_width;
		offset[1] = -0.5f*g_gui_info.grid_block_width;
	}
}

int InitPlayerInventory(struct character_struct * pcharacter)
{
	struct item_inventory_struct * inventory=0;
	int i;

	//item_inventory_struct is the main data structure for an inventory.
	inventory = (struct item_inventory_struct*)malloc(sizeof(struct item_inventory_struct));
	if(inventory == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	inventory->max_items = 18;
	inventory->num_items = 0;
	inventory->dimensions[0] = 3; //row
	inventory->dimensions[1] = 6; //cols

	//item_ptrs is the list of ptrs to the actual items in the inventory
	inventory->item_ptrs = (struct item_struct**)malloc(18*sizeof(struct item_struct *));
	if(inventory->item_ptrs == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(inventory->item_ptrs, 0, (18*sizeof(struct item_struct*)));

	//slots controls the graphical indications of items.
	inventory->slots = (struct inv_slot_struct*)malloc(18*sizeof(struct inv_slot_struct));
	if(inventory->slots == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < inventory->max_items; i++)
	{
		inventory->slots[i].slot_index = -1;
		inventory->slots[i].flags = (char)0;
	}

	pcharacter->p_inventory = inventory;

	return 1;
}

/*
This function initializes an inventory for objects on the ground.
*/
int InitGroundSlots(struct item_inventory_struct * ground)
{
	int i;

	memset(ground, 0, sizeof(struct item_inventory_struct));

	ground->max_items = 60;	//3 rows of 20
	ground->dimensions[0] = 3; //row
	ground->dimensions[1] = 20; //col
	ground->item_ptrs = (struct item_struct**)malloc(60*sizeof(struct item_struct*));
	if(ground->item_ptrs == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(ground->item_ptrs, 0, 60*sizeof(struct item_struct*));
	ground->slots = (struct inv_slot_struct*)malloc(60*sizeof(struct inv_slot_struct));
	if(ground->slots == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	for(i = 0; i < ground->max_items; i++)
	{
		ground->slots[i].slot_index = -1;
		ground->slots[i].flags = (char)0;
	}

	return 1;
}

void ClearGroundSlots(struct item_inventory_struct * ground)
{
	int i;

	ground->num_items = 0;
	for(i = 0; i < ground->max_items; i++)
	{
		ground->item_ptrs[i] = 0;
		ground->slots[i].slot_index = -1; //set to empty
		ground->slots[i].flags = 0; //clear any flags
	}
}

/*
This function fills in g_temp_ground_slots with items that are nearby. 
It does not remove items from tiles.
*/
int MakeGroundItemsInventory(float * pos)
{
	struct item_struct * items_list=0;
	struct item_struct * p_item=0;
	float boundaryHalfWidth = 3.0f;
	float cornerPosArray[8]; //array of vec2's
	float tempVec[3] = {0.0f, 0.0f, 0.0f};
	int i_tileArray[4]; //array of tile indices
	int i_tile;
	int num_tiles; //number of tiles in i_tileArray;
	int isTileNew;
	int i;
	int j;
	int i_item;
	int num_tileItems;
	int r;

	//Make 4 corners of the bounding box, get a list of possible tiles from those corners
	//-x,-z
	cornerPosArray[0] = pos[0] - boundaryHalfWidth;
	cornerPosArray[1] = pos[2] - boundaryHalfWidth;
	
	//-x,+z
	cornerPosArray[2] = pos[0] - boundaryHalfWidth;
	cornerPosArray[3] = pos[2] + boundaryHalfWidth;

	//+x,+z
	cornerPosArray[4] = pos[0] + boundaryHalfWidth;
	cornerPosArray[5] = pos[2] + boundaryHalfWidth;

	//+x,-z
	cornerPosArray[6] = pos[0] + boundaryHalfWidth;
	cornerPosArray[7] = pos[2] - boundaryHalfWidth;

	//Now get a list of tiles
	num_tiles = 0;
	for(i = 0; i < 4; i++)
	{
		isTileNew = 1; //reset flag.
		tempVec[0] = cornerPosArray[2*i];
		tempVec[2] = cornerPosArray[(2*i)+1];
		i_tile = GetLvl1Tile(tempVec);

		//loop through i_tileArray to see if it is already there
		for(j = 0; j < num_tiles; j++)
		{
			if(i_tileArray[j] == i_tile)
			{
				isTileNew = 0;
				break;
			}
		}

		//if the tile isn't already in the i_tileArray, add it
		if(isTileNew == 1)
		{
			i_tileArray[num_tiles] = i_tile;
			num_tiles += 1;
		}
	}

	//Go through each tile and do a boundary box check of each item
	for(i = 0; i < num_tiles; i++)
	{
		p_item = 0;
		items_list = g_bush_grid.p_tiles[(i_tileArray[i])].items_list; //linked list of items
		num_tileItems = g_bush_grid.p_tiles[(i_tileArray[i])].num_items;
		for(i_item = 0; i_item < num_tileItems; i_item++)
		{
			if(p_item == 0)
			{
				p_item = items_list; //head
			}
			else
			{
				p_item = p_item->pNext;
			}	

			//Check if item is in bounding box
			if(p_item->pos[0] >= cornerPosArray[0] && p_item->pos[0] <= cornerPosArray[4])
			{
				if(p_item->pos[2] >= cornerPosArray[1] && p_item->pos[2] <= cornerPosArray[3])
				{
					 r = FillCrateWithItem(&g_temp_ground_slots, //inventory
							 1, 								 //# items to add
							 p_item->type, 						 //unused. but set it to something
							 p_item, 							 //item to add
							 1);								 //flag. tells func to not create item but use ptr.
					 if(r == 0)//error occurred
						 return 0;
				}
			}
		}
	}

	return 1;
}

/*This func creates a new item_struct and initializes it.
initialPos is used to find a ground pos*/
struct item_struct * CreateItem(float * initialPos, unsigned char newType)
{
	struct item_struct * newItem=0;
	float surf_pos[3];
	float surf_normal[3];
	int r;

	newItem = (struct item_struct*)malloc(sizeof(struct item_struct));
	if(newItem == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(newItem, 0, sizeof(struct item_struct));

	newItem->type = newType;

	r = GetTileSurfPoint(initialPos, surf_pos, surf_normal);
	if(r != 1)
		return 0;
	newItem->pos[0] = surf_pos[0];
	newItem->pos[1] = surf_pos[1];
	newItem->pos[2] = surf_pos[2];

	//calculate the ground offset to hover item above ground
	newItem->pos[1] += GetItemGroundOffset(newType);

	return newItem;
}

float GetItemGroundOffset(unsigned char itemType)
{
	float offset;

	//calculate the ground offset to hover item above ground
	switch(itemType)
	{
	case ITEM_TYPE_BEANS:
		offset = g_beans_common.y_AboveGroundOffset;
		break;
	case ITEM_TYPE_CANTEEN:
		offset = g_canteen_common.y_AboveGroundOffset;
		break;
	case ITEM_TYPE_RIFLE:
		offset = g_rifle_common.y_AboveGroundOffset;
		break;
	case ITEM_TYPE_PISTOL:
		offset = g_pistol_common.y_AboveGroundOffset;
		break;
	default:
		printf("%s: error. %d is unknown type.\n", __func__, (int)itemType);
		return 0.0f;
	}

	return offset;
}

int AddItemToTile(struct item_struct * newItem)
{
	struct item_struct * cur_item=0;
	int i;

	if(newItem == 0)
	{
		printf("%s: error. newItem NULL.\n", __func__);
		return 0;
	}

	i = GetLvl1Tile(newItem->pos);
	if(i == -1) //error
	{
		printf("%s: error. can't find tile at (%f,%f,%f)\n", __func__, newItem->pos[0], newItem->pos[1], newItem->pos[2]);
		return 0;
	}

	if(g_bush_grid.p_tiles[i].items_list == 0) //Is items_list not initialized? If so, need to initialize it
	{
		g_bush_grid.p_tiles[i].items_list = newItem;
		g_bush_grid.p_tiles[i].num_items = 1;		
		newItem->pNext = 0;
	}
	else
	{
		cur_item = g_bush_grid.p_tiles[i].items_list;

		while(cur_item->pNext != 0)
		{
			cur_item = cur_item->pNext;
		}
		cur_item->pNext = newItem;
		newItem->pNext = 0;
		g_bush_grid.p_tiles[i].num_items += 1;
	}

	return 1;
}

int InitGUICursor(struct gui_cursor_struct * cursor, unsigned int width, unsigned int height)
{
	cursor->pos[0] = 512.0f; //put the cursor in the center
	cursor->pos[1] = 384.0f;
	
	cursor->x_limits[0] = 0.0f;
	cursor->x_limits[1] = (float)width;
	cursor->y_limits[0] = 0.0f;
	cursor->y_limits[1] = (float)height;

	return 1;
}

/*
This function updates GUI interaction
*/
void UpdateGUI(void)
{
	struct item_struct * dragItem=0;
	int gridId;
	int slotIndex;
	int r;

	//If we get a drag event and we aren't dragging, initiate drag
	if(((g_gui_inv_cursor.flags & GUI_FLAGS_DRAG) != GUI_FLAGS_DRAG) && ((g_gui_inv_cursor.events & GUI_EVENTS_LEFTMOUSEDOWN) == GUI_EVENTS_LEFTMOUSEDOWN))
	{
		//Try to drag
		r = FindSlotCursorIsOver(&gridId, &slotIndex, &dragItem);
		if(r == 1)
		{
			g_gui_inv_cursor.flags |= GUI_FLAGS_DRAG;
		
			if(gridId == 3) //if the slot the cursor is over is an action slot
			{
				g_gui_inv_cursor.draggedItem = dragItem;
				g_gui_inv_cursor.original_inv_index = slotIndex;
				g_gui_inv_cursor.original_itemList = 0; //set to 0 because it is action slot, this will probably haunt me later
				g_a_man.p_actionSlots[slotIndex] = 0; //clear the slot
			}
			else	//all other slots
			{
				switch(gridId)
				{
				case 0: //player inventory
					GUIPickupItemInInventory(slotIndex, dragItem, g_a_man.p_inventory);
					break;
				case 1: //ground
					GUIPickupItemFromGround(slotIndex, dragItem, &g_temp_ground_slots);
					break;
				case 2: //crate
					GUIPickupItemInInventory(slotIndex, dragItem, g_gui_info.selectedCrate->items);
					break;
				}
			}
		}
	}
	else if(((g_gui_inv_cursor.flags & GUI_FLAGS_DRAG) == GUI_FLAGS_DRAG) //If we are dragging and we get a stop drag event, set the item down
		&& ((g_gui_inv_cursor.events & GUI_EVENTS_LEFTMOUSEDOWN) == GUI_EVENTS_LEFTMOUSEDOWN))
	{
		r = GUISetDraggedItemDown();
		if(r == 1) //item was set down, so clear the dragging state
		{
			g_gui_inv_cursor.flags = g_gui_inv_cursor.flags & (~GUI_FLAGS_DRAG); //clear flag.

			//draggedItem was placed, so clear it
			g_gui_inv_cursor.draggedItem = 0;
			g_gui_inv_cursor.original_inv_index = -1; //set to invalid index
			g_gui_inv_cursor.original_itemList = 0; //clear pointer
		}
	}

	//clear all events, since they have been handled.
	g_gui_inv_cursor.events = 0;
}

/*
This function checks if the cursor is over a slot. If the cursor is over a slot, the params
are filled in with info about the slot. returns:
	0 ;no slot found
	1 ;cursor is over a slot
*/
int FindSlotCursorIsOver(int * pgridId, int * pslotIndex, struct item_struct ** pitem)
{
	struct item_inventory_struct * bottomInventory=0;
	struct item_struct * temp_item=0;
	float * grid_positions=0;
	float temp_vec[2];
	int i;
	int is_slot_found=0;
	int r;

	//check all the inventory grids look for a hit
	//player inventory
	for(i = 0; i < g_a_man.p_inventory->max_items; i++)
	{
		//check that slot index is not empty and the slot isn't a hold for a multislot
		if((g_a_man.p_inventory->slots[i].slot_index >= 0) && ((g_a_man.p_inventory->slots[i].flags & SLOT_FLAGS_MULTISLOT) == 0))
		{
			temp_vec[0] = g_gui_info.inv_grid_slot_positions[2*i];
			temp_vec[1] = g_gui_info.inv_grid_slot_positions[(2*i)+1];
			temp_item = g_a_man.p_inventory->item_ptrs[g_a_man.p_inventory->slots[i].slot_index];
			r = GUIIsInGridBlock(g_gui_inv_cursor.pos,
					temp_vec,
					temp_item->type);
			if(r == 1)
			{
				is_slot_found = 1;
				*pgridId = 0; //indicate we found slot in player inventory
				*pslotIndex = i;
				*pitem = temp_item;
				break;
			}	
		}
	}

	//check ground or crate
	switch(g_gui_info.bottom_grid_mode)
	{
	case GUI_BOTTOMGRID_GROUND:
		bottomInventory = &g_temp_ground_slots;
		grid_positions = g_gui_info.ground_grid_slot_positions;
		break;
	case GUI_BOTTOMGRID_CRATE:
		bottomInventory = g_gui_info.selectedCrate->items;
		grid_positions = g_gui_info.crate_grid_slot_positions;
		break;
	}
	
	for(i = 0; i < bottomInventory->max_items; i++)
	{
		//check that slot index is not empty and the slot isn't a hold for a multislot item
		if((bottomInventory->slots[i].slot_index >= 0) && ((bottomInventory->slots[i].flags & SLOT_FLAGS_MULTISLOT) == 0))
		{
			temp_vec[0] = grid_positions[2*i];
			temp_vec[1] = grid_positions[(2*i)+1];
			temp_item = bottomInventory->item_ptrs[bottomInventory->slots[i].slot_index];
			r = GUIIsInGridBlock(g_gui_inv_cursor.pos,
					temp_vec,
					temp_item->type);
			if(r == 1)
			{
				is_slot_found = 1;
				*pslotIndex = i;
				*pitem = temp_item;
				if(g_gui_info.bottom_grid_mode == GUI_BOTTOMGRID_GROUND)
					*pgridId = 1;
				if(g_gui_info.bottom_grid_mode == GUI_BOTTOMGRID_CRATE)
					*pgridId = 2;
				break;
			}
		}
	}

	//Check the action slots
	for(i = 0; i < 2; i++)
	{
		if((g_gui_inv_cursor.pos[0] >= g_gui_info.action_slot_positions[i*2]) && (g_gui_inv_cursor.pos[0] <= (g_gui_info.action_slot_positions[i*2] + g_gui_info.grid_block_width)))
		{
			if((g_gui_inv_cursor.pos[1] >= g_gui_info.action_slot_positions[(i*2)+1]) && (g_gui_inv_cursor.pos[1] <= (g_gui_info.action_slot_positions[(i*2)+1] + (g_gui_info.grid_block_width*3))))
			{
				//check that action slot isn't empty
				if(g_a_man.p_actionSlots[i] == 0)
				{
					continue;
				}
				else
				{
					is_slot_found = 1;
					*pgridId = 3;
					*pslotIndex = i;
					*pitem = g_a_man.p_actionSlots[i];
					break;
				}
			}
		}
	}

	return is_slot_found;
}

/*
This func determines if a vec2 pos is within a inventory grid block:
	0	;pos not in block
	1	;pos inside block
*/
int GUIIsInGridBlock(float * pos, float * boxLowerLeftCorner, char itemType)
{
	float item_height;

	if(GUIIsItemMultiSlot(itemType))
	{
		item_height = 3.0f*g_gui_info.grid_block_width;
	}
	else
	{
		item_height = g_gui_info.grid_block_width;
	}

	//x coord
	if((pos[0] >= boxLowerLeftCorner[0]) && (pos[0] <= (boxLowerLeftCorner[0] + g_gui_info.grid_block_width)))
	{
		if((pos[1] >= boxLowerLeftCorner[1]) && (pos[1] <= (boxLowerLeftCorner[1] + item_height)))
		{
			return 1;
		}
	}

	return 0;
}

/*
This function given a grid index gets the grid block x,y index.
inputs:
	float * pos
	int gridIndex
outputs:
	int * ij_grid	;index 0=row, 1=col
	int * i_slot	;index in the slots array calculated using i_grid, j_grid
returns:
	1	;success pos is inside grid
	0	;pos is outside grid
*/
int GUIIsPosInInvGridArea(float * pos, int gridIndex, int * ij_grid, int * i_slot)
{
	float gridLowerLeft[2];
	float gridDimensions[2];
	struct item_inventory_struct * itemList=0;
	int gridCoord[2];	//index 0=row, 1=col

	switch(gridIndex)
	{
	case 0: //player inventory
		itemList = g_a_man.p_inventory;
		gridLowerLeft[0] = g_gui_info.inv_grid_slot_positions[0];
		gridLowerLeft[1] = g_gui_info.inv_grid_slot_positions[1];
		break;
	case 1: //ground
		itemList = &g_temp_ground_slots;
		gridLowerLeft[0] = g_gui_info.ground_grid_slot_positions[0];
		gridLowerLeft[1] = g_gui_info.ground_grid_slot_positions[1];
		break;
	case 2:	//crate
		itemList = g_gui_info.selectedCrate->items;
		gridLowerLeft[0] = g_gui_info.crate_grid_slot_positions[0];
		gridLowerLeft[1] = g_gui_info.crate_grid_slot_positions[1];
		break;
	default:
		printf("%s: error. unknown gridIndex %d\n", __func__, gridIndex);
		return 0;
	}
	gridDimensions[0] = itemList->dimensions[1]*g_gui_info.grid_block_width; //row, height
	gridDimensions[1] = itemList->dimensions[0]*g_gui_info.grid_block_width; //col, width

	//check if the pos is somewhere in the grid boundaries
	if((pos[0] >= gridLowerLeft[0]) && (pos[0] <= (gridLowerLeft[0] + gridDimensions[0])))
	{
		if((pos[1] >= gridLowerLeft[1]) && (pos[1] <= (gridLowerLeft[1] + gridDimensions[1])))
		{
			//now calculate the indices of the particular block the cursor is in
			gridCoord[1] = (pos[0] - gridLowerLeft[0])/g_gui_info.grid_block_width;	//col
			gridCoord[0] = (pos[1] - gridLowerLeft[1])/g_gui_info.grid_block_width;	//row
			*i_slot = (gridCoord[0]*itemList->dimensions[1]) + gridCoord[1];
			ij_grid[0] = gridCoord[0];
			ij_grid[1] = gridCoord[1];
			return 1;
		}
	}

	return 0;
} 

/*
This function removes an item from an inventory. The item is identified by:
	slotIndex, which is the inv_slot_struct.slot_index of the item to pick up
-This function puts the picked up item in the g_gui_inv_cursor struct
*/
int GUIPickupItemInInventory(int slotIndex, struct item_struct * dragItem, struct item_inventory_struct * itemList)
{
	int i;
	int i_item;

	if(slotIndex < 0) //error. check make sure slotIndex is valid
	{
		printf("%s: invalid slot %d\n", __func__, slotIndex);
		return 0;
	}

	if(itemList == 0)
	{
		printf("%s: itemList is null\n", __func__);
		return 0;
	}

	//put item in gui_cursor_struct
	g_gui_inv_cursor.draggedItem = dragItem;
	g_gui_inv_cursor.original_inv_index = slotIndex;
	g_gui_inv_cursor.original_itemList = itemList;

	//clear the item in the item_ptrs array
	i_item = itemList->slots[slotIndex].slot_index;
	itemList->item_ptrs[i_item] = 0;	//clear ptr in item_ptrs
	itemList->num_items -= 1;

	//loop through inventory and clear all slots that have the item's slot index (multi-slot items will have multiple slots all pointing to the same item in the item_ptrs array)
	for(i = 0; i < itemList->max_items; i++)
	{
		if(itemList->slots[i].slot_index == i_item)
		{
			itemList->slots[i].slot_index = -1; 	//set to empty
			itemList->slots[i].flags = 0;			//clear any flags
		}
	}

	return 1;
}

/*
This function removes an item from a terrain tile. The item is identified by using slotIndex to index the itemList,
and getting a pointer to the item. The item is also cleared from the ground grid in the inventory GUI.
-slotIndex	;index of the item in itemList param
-dragItem	;pointer to item to pickup
-itemList	;pointer to an inventory
*/
int GUIPickupItemFromGround(int slotIndex, struct item_struct * dragItem, struct item_inventory_struct * itemList)
{
	struct item_struct * p_tileItems=0;
	struct item_struct * p_prevItem=0;
	struct item_struct * p_item=0;
	int i_item;
	int i_tile;
	int i;

	if(slotIndex < 0)
	{
		printf("%s: invalid slot %d\n", __func__, slotIndex);
		return 0;
	}

	//put the item in gui_cursor_struct
	g_gui_inv_cursor.draggedItem = dragItem;
	g_gui_inv_cursor.original_inv_index = slotIndex;
	g_gui_inv_cursor.original_itemList = itemList;

	//clear the item in the itemList
	i_item = itemList->slots[slotIndex].slot_index;
	itemList->item_ptrs[i_item] = 0;	//clear reference to the item in item_ptrs.
	itemList->num_items -= 1;
	for(i = 0; i < itemList->max_items; i++) //clear all slots in the inventory grid with the item index
	{
		if(itemList->slots[i].slot_index == i_item)
		{
			itemList->slots[i].slot_index = -1; //set to empty
			itemList->slots[i].flags = 0;		//clear any flags
		}
	}
	
	//Now remove the item from the tile, items_list in the tile is a linked list
	i_tile = GetLvl1Tile(dragItem->pos);
	if(i_tile == -1) //error
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	p_tileItems = g_bush_grid.p_tiles[i_tile].items_list;
	p_item = p_tileItems;
	p_prevItem = 0;
	for(i = 0; i < g_bush_grid.p_tiles[i_tile].num_items; i++)
	{
		//found the item again
		if(p_item == dragItem)
		{
			//item is at head of list
			if(i == 0)
			{
				g_bush_grid.p_tiles[i_tile].items_list = p_item->pNext;
			}
			else if(i == (g_bush_grid.p_tiles[i_tile].num_items - 1)) //item is at the end
			{
				p_prevItem->pNext = 0;
			}
			else //item is in the middle
			{
				p_prevItem->pNext = p_item->pNext;
			}

			g_bush_grid.p_tiles[i_tile].num_items -= 1;
			return 1;
		}

		p_prevItem = p_item;
		p_item = p_item->pNext;
	}

	printf("%s: error. item (slotIndex=%d) not found.\n", __func__, slotIndex);
	return 0;
}

/*
This function attempts to place the dragged item into a grid.
-assumes g_gui_inv_cursor.draggedItem has the item to place.
*/
int GUISetDraggedItemDown(void)
{
	struct item_inventory_struct * bottomInventory=0;
	struct item_inventory_struct * inventories[2] = {0};	//0=playerInventory, 1=bottomInventory
	struct item_struct * temp_item=0;
	float posToCheck[2];
	float temp_vec[2];
	float surf_pos[3];
	float surf_normal[3];
	int gridCoord[2]; 
	int i_slot[3]; //this holds index into slots array for item. There are 3 elements in case of a multi-slot item.
	int freeIndex;
	int inventoriesGridIds[2];	//used with inventories
	int isItemSetDown=0;
	int i;
	int k;
	int r;

	//fill in the inventories array
	inventories[0] = g_a_man.p_inventory;
	inventoriesGridIds[0] = 0; //set to playerInventory index
	switch(g_gui_info.bottom_grid_mode)
	{
	case GUI_BOTTOMGRID_GROUND:
		inventories[1] = &g_temp_ground_slots;
		inventoriesGridIds[1] = 1; //set to ground
		break;
	case GUI_BOTTOMGRID_CRATE:
		inventories[1] = g_gui_info.selectedCrate->items;
		inventoriesGridIds[1] = 2; //set to crate
		break;
	}

	//assume cursor pos is the slot center. If item is 3 high, then adjust
	//posToCheck to be at the center of the bottom-most slot.
	posToCheck[0] = g_gui_inv_cursor.pos[0];
	posToCheck[1] = g_gui_inv_cursor.pos[1];
	if(GUIIsItemMultiSlot(g_gui_inv_cursor.draggedItem->type))
	{
		posToCheck[1] -= ((3.0f*0.5f*g_gui_info.grid_block_width) - (0.5f*g_gui_info.grid_block_width));
	}

	//Check the action slots
	for(i = 0; i < 2; i++)
	{
		if((g_gui_inv_cursor.pos[0] >= g_gui_info.action_slot_positions[(i*2)]) 
				&& (g_gui_inv_cursor.pos[0] <= (g_gui_info.action_slot_positions[(i*2)] + g_gui_info.grid_block_width)))
		{
			if((g_gui_inv_cursor.pos[1] >= g_gui_info.action_slot_positions[(i*2)+1]) 
					&& (g_gui_inv_cursor.pos[1] <= (g_gui_info.action_slot_positions[(i*2)+1] + (g_gui_info.grid_block_width*3))))
			{
				if(g_a_man.p_actionSlots[i] != 0) //action slot has something already, can't place it
					continue;

				//put item in the slot
				g_a_man.p_actionSlots[i] = g_gui_inv_cursor.draggedItem;
				return 1;
			}
		}
	}

	//Next loop over each possible grid, look for an empty slot, k=0 inv, k=1 ground/crate
	for(k = 0; k < 2; k++)
	{
		r = GUIIsPosInInvGridArea(posToCheck, 
				inventoriesGridIds[k], 	//select player inventory grid, ground grid, or crate grid
				gridCoord,
				&(i_slot[0]));
		if(r == 1) //cursor is over a slot
		{
			if(inventories[k]->slots[(i_slot[0])].slot_index < 0) //is slot empty
			{
				if(GUIIsItemMultiSlot(g_gui_inv_cursor.draggedItem->type))
				{
					//first see if the slots are open
					for(i = 0; i < 2; i++)
					{
						posToCheck[1] += g_gui_info.grid_block_width; //move posToCheck into the next block above
						r = GUIIsPosInInvGridArea(posToCheck,
								inventoriesGridIds[k],
								gridCoord,
								&(i_slot[(i+1)])); //add +1 because we are looking at the 2nd and 3rd blocks
						if(r == 1) //posToCheck is in block
						{
							if(inventories[k]->slots[(i_slot[(i+1)])].slot_index < 0) //is slot empty
							{
								continue;
							}
							else
							{
								return 0; //slot is not empty. stop.
							}
						}
						else
						{
							return 0;
						}
					}

					//if we get here than the 2 slots above the original slot are free, so we can place the item now
					//i_slot has also been filled in
					freeIndex = InventoryGetFreeItemPtr(inventories[k]);
					if(freeIndex == -1)
					{
						printf("%s: error. inventory %d full. inventory->num_items=%d\n", __func__, k, inventories[k]->num_items);
						return 0;
					}
					inventories[k]->item_ptrs[freeIndex] = g_gui_inv_cursor.draggedItem;
					inventories[k]->slots[(i_slot[0])].slot_index = freeIndex;
					inventories[k]->slots[(i_slot[0])].flags = 0;

					for(i = 1; i < 3; i++)
					{
						inventories[k]->slots[(i_slot[i])].slot_index = freeIndex; //multi-slot item slots point to the same item as the original slot
						inventories[k]->slots[(i_slot[i])].flags = SLOT_FLAGS_MULTISLOT;
					}

					inventories[k]->num_items += 1;
					isItemSetDown = 1; //flag that the multi-slot item is placed.
					break;
				}
				else //we are dropping a 1x1 item
				{
					//check that the element of item_ptrs isn't full
					freeIndex = InventoryGetFreeItemPtr(inventories[k]);
					if(freeIndex == -1)
					{
						printf("%s: error. inventory %d full. inventory->num_items=%d\n", __func__, k, inventories[k]->num_items);
						return 0;
					}
					
					//check if we are dropping bullets onto the ground, if so make them disappear
					//(bullets too small so dumping them randomly on the ground will cause them to disappear)
					if(inventoriesGridIds[k] == 1) //1 == ground
					{
						//Is the item bullet type?
						if(g_gui_inv_cursor.draggedItem->type == ITEM_TYPE_PISTOL_AMMO
							|| g_gui_inv_cursor.draggedItem->type == ITEM_TYPE_RIFLE_AMMO)
						{
							//destroy the item
							free(g_gui_inv_cursor.draggedItem);

							return 1; //return that item was handled so that it gets cleared from inv cursor struct.
						}
					}

					//set the item down
					inventories[k]->item_ptrs[freeIndex] = g_gui_inv_cursor.draggedItem;
					inventories[k]->slots[(i_slot[0])].slot_index = freeIndex;
					inventories[k]->slots[(i_slot[0])].flags = 0;
					inventories[k]->num_items += 1;
					isItemSetDown = 1; //flag that item is placed.
					break;
				}
			}
			else
			{
				//slot is not empty
				return 0;
			}
		}
	}

	//Was the item set down into the ground grid?
	//If so place it on the item
	if(isItemSetDown == 1) 	
	{
		if(k == 1) //Item was placed on ground, k==1 ground
		{
			r = GetTileSurfPoint(g_a_man.pos, surf_pos, surf_normal);
			if(r != 1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				return 0;
			}
			
			//set the dragged item with the surface position for AddItemToTile
			g_gui_inv_cursor.draggedItem->pos[0] = surf_pos[0];
			g_gui_inv_cursor.draggedItem->pos[1] = surf_pos[1] + GetItemGroundOffset(g_gui_inv_cursor.draggedItem->type);
			g_gui_inv_cursor.draggedItem->pos[2] = surf_pos[2];

			r = AddItemToTile(g_gui_inv_cursor.draggedItem);
			if(r != 1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				return 0;
			}

			//at this point item is added to tile, let parent function cleanup cursor struct.
		}
		return 1;
	}

	return 0;
}

/*
This function goes through the item_ptrs array and finds an empty index to use,
and returns that index. returns:
	-1 if a free slot cannot be found
*/
int InventoryGetFreeItemPtr(struct item_inventory_struct * itemInventory)
{
	int i;

	for(i = 0; i < itemInventory->max_items; i++)
	{
		if(itemInventory->item_ptrs[i] == 0)
			return i;
	}

	return -1;
}

/*
This function is an overall wrapper for the character picking up and item
on the ground or in a crate.
*/
int PlayerGetItem(void)
{
	float temp_vec[3];

	MakeRayFromCamera(&g_camera_rotX, &g_camera_rotY, temp_vec);
	temp_vec[0] *= 4.0f;
	temp_vec[1] *= 4.0f;
	temp_vec[2] *= 4.0f;
	g_gui_info.selectedCrate = SelectCrateToOpen(g_ws_camera_pos, //current location
			temp_vec); //ray to cast out to see if there is a crate
	if(g_gui_info.selectedCrate == 0)
	{
		printf("%s: could not find Crate!\n", __func__);

		//since we couldn't find crate, load ground inventory
		MakeGroundItemsInventory(g_ws_camera_pos);
		g_gui_info.bottom_grid_mode = GUI_BOTTOMGRID_GROUND;
	}
	else
	{
		g_gui_info.bottom_grid_mode = GUI_BOTTOMGRID_CRATE;//set bottom grid to crate.
	}

	SwitchRenderMode(1);

	//change the keyboard mode to GUI
	g_keyboard_state.state = KEYBOARD_MODE_INVGUI;

	return 1;
}

/*
This function returns a pointer to the closest crate that can be opened.
inputs:
	vec3 pos ;player position to act as origin of check
*/
struct moveable_object_struct * SelectCrateToOpen(float * pos, float * ray)
{
	struct moveable_object_struct * tileMoveables=0;
	struct moveable_object_struct * moveables=0;
	float boundaryBoxHalfWidth = 3.0f; //an axis-aligned bounding box
	int tile_i;
	int r;

	//TODO: For now just look for the closest crate, but should shoot a ray.
	tile_i = GetMoveablesTileIndex(pos);
	if(tile_i == -1)
		return 0;
	tileMoveables = g_moveables_grid.p_tiles[tile_i].moveables_list;

	moveables = tileMoveables;
	while(moveables != 0)
	{
		if(moveables->moveable_type == 0) //crate
		{
			//check that crate is in boundary box
			if((moveables->pos[0] > (pos[0] - boundaryBoxHalfWidth))
				&& (moveables->pos[0] < (pos[0] + boundaryBoxHalfWidth)))
			{
				if((moveables->pos[2] > (pos[2] - boundaryBoxHalfWidth))
					&& (moveables->pos[2] < (pos[2] + boundaryBoxHalfWidth)))
				{
					//Now shoot a ray from origin at whatever the crosshair is pointing at
					r = RaycastCrateHull(moveables->pos, pos, ray);
					if(r == 1)
						return moveables;
				}
			}
		}

		moveables = moveables->pNext;
	}

	return 0; //couldn't find a crate
}

/*
This function sends a ray from origin and checks to see if it intersects
a crate hull placed at cratePos
*/
int RaycastCrateHull(float * cratePos, float * origin, float * ray)
{
	struct box_collision_struct * hull=0;
	float * p_tempVec=0;
	float tempVec[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float rayToVecA[3];
	float rayToVecB[3];
	float temp_faceVerts[12]; //array of 4 vec3's. vec3 for each vert in the face.
	float pyramidNormal[3];
	float pyramidDot[4];
	float hullMat[16];
	float endPoint[3];
	float endPointDot;
	int i_face;
	int j;

	hull = &(g_crate_physics_common.box_hull);

	//add ground offset since pos is on surface, but origin of crate is at its cg.
	mmTranslateMatrix(hullMat, cratePos[0], cratePos[1], cratePos[2]);

	for(i_face = 0; i_face < hull->num_faces; i_face++)
	{
		//copy the pos of the 4 verts in the face to a temporary array for easier handling.
		for(j = 0; j < 4; j++)
		{
			p_tempVec = temp_faceVerts + (j*3);
			tempVec[0] = hull->positions[(hull->faces[i_face].i_vertices[j]*3)];
			tempVec[1] = hull->positions[(hull->faces[i_face].i_vertices[j]*3)+1];
			tempVec[2] = hull->positions[(hull->faces[i_face].i_vertices[j]*3)+2];
			tempVec[3] = 1.0f; //save vertex pos to tempVec first because we need vec4 for transform and temp faceVerts uses vec3s
			mmTransformVec(hullMat, tempVec);	//transform the vertex from hull local-space to world-space
			p_tempVec[0] = tempVec[0];
			p_tempVec[1] = tempVec[1];
			p_tempVec[2] = tempVec[2];
		}

		//right
		vSubtract(rayToVecA, (temp_faceVerts), origin);	//get two rays that point from origin to face
		vSubtract(rayToVecB, (temp_faceVerts+3), origin);
		vCrossProduct(pyramidNormal, rayToVecB, rayToVecA);
		vNormalize(pyramidNormal); //this normal should point inward
		pyramidDot[0] = vDotProduct(pyramidNormal, ray); //if ray points with dot product then it might intersect the face, so check for negative
		//printf("%d: pyramidDot %f\n", i_face, pyramidDot[0]);
		if(pyramidDot[0] < 0.0f) //if negative then ray breaks through pyramid triangle and can't hit face, so skip it
			continue; 

		//top
		vSubtract(rayToVecA, (temp_faceVerts+3), origin);
		vSubtract(rayToVecB, (temp_faceVerts+6), origin);
		vCrossProduct(pyramidNormal, rayToVecB, rayToVecA);
		vNormalize(pyramidNormal);
		pyramidDot[1] = vDotProduct(pyramidNormal, ray);
		//printf("%d: pyramidDot %f\n", i_face, pyramidDot[1]);
		if(pyramidDot[1] < 0.0f)
			continue;

		//left
		vSubtract(rayToVecA, (temp_faceVerts+6), origin);
		vSubtract(rayToVecB, (temp_faceVerts+9), origin);
		vCrossProduct(pyramidNormal, rayToVecB, rayToVecA);
		vNormalize(pyramidNormal);
		pyramidDot[2] = vDotProduct(pyramidNormal, ray);
		//printf("%d: pyramidDot %f\n", i_face, pyramidDot[2]);
		if(pyramidDot[2] < 0.0f)
			continue;

		//bottom		
		vSubtract(rayToVecA, (temp_faceVerts+9), origin);
		vSubtract(rayToVecB, (temp_faceVerts), origin);
		vCrossProduct(pyramidNormal, rayToVecB, rayToVecA);
		vNormalize(pyramidNormal);
		pyramidDot[3] = vDotProduct(pyramidNormal, ray);
		//printf("%d: pyramidDot %f\n", i_face, pyramidDot[3]);
		if(pyramidDot[3] < 0.0f)
			continue;

		//If we got here then all dotproducts are positive, which means the ray intersects the plane of the face.
		//now see which side of the face plane that the endpoint of the ray is on
		vAdd(endPoint, origin, ray);
		vSubtract(tempVec, endPoint, temp_faceVerts);
		endPointDot = vDotProduct(tempVec, hull->faces[i_face].normal);
		if(endPointDot < 0.0f)
			return 1; //intersect
	}

	return 0; //no intersect
}

/*
This function takes the inverse camera angles and returns a ray that points in the
direction of the camera.
-irotX and irotY are inverse camera angles about the X and Y axis
*/
void MakeRayFromCamera(float * irotX, float * irotY, float * ray)
{
	float matRotX[16];
	float matRotY[16];
	float matRot[16];
	float unitVec[4] = {0.0f, 0.0f, -1.0f, 1.0f}; //a ray that points down the -z axis which is fwd, as this is the dir the camera faces with 0 angle.
	float cameraRot[2];	//0 = x-axis, 1=y-axis

	cameraRot[0] = (*irotX);
	cameraRot[1] = (*irotY);

	mmRotateAboutX(matRotX, cameraRot[0]);
	mmRotateAboutY(matRotY, cameraRot[1]);
	mmMultiplyMatrix4x4(matRotX, matRotY, matRot); 
	mmTransposeMat4(matRot);

	mmTransformVec(matRot, unitVec);

	ray[0] = unitVec[0];
	ray[1] = unitVec[1];
	ray[2] = unitVec[2];
}

/*
This function sets a mat4 to a orthographic projection matrix specific
for the map screen. (inventory screen uses a orthographic matrix that has
the origin in the bottom left corner, map screen would be better with origin at center
of the screen).
*/
void SetMapOrthoMat(float * orthoMat, float fsize)
{
	float left;
	float right;
	float bottom;
	float top;
	float zfar;
	float znear;
	float defaultHalfWidth = 512.0f; //this is a base scene width to start at
	float defaultHalfHeight = 384.0f; //this is the base scene height to start at

	left = -1.0f*defaultHalfWidth*fsize;
	right= defaultHalfWidth*fsize;
	bottom = -1.0f*defaultHalfHeight*fsize;
	top = defaultHalfHeight*fsize;
	zfar = 100.0f;
	znear = 1.0f;

	memset(orthoMat, 0, 16*sizeof(float));

	orthoMat[0] = 2.0f/(right-left);
	orthoMat[5] = 2.0f/(top-bottom);
	orthoMat[10] = -2.0f/(zfar - znear);
	orthoMat[12] = -1.0f*(right+left)/(right-left);
	orthoMat[13] = -1.0f*(top+bottom)/(top-bottom);
	orthoMat[14] = -1.0f*(zfar + znear)/(zfar-znear);
	orthoMat[15] = 1.0f; 
}

/*
This function needs g_big_terrain initialized.
*/
int InitMapGUIVBO(struct map_gui_info_struct * mapInfo)
{
	struct lvl_1_tile * ptile=0;
	float * pvert=0; //pointer to a vert in a terrain tile
	float * positions=0; //array of vert3 positions
	float * p_position=0; //pointer to a particular vert in the positions array
	char * numAboveWaterVertsInTile=0;
	float lowerLeftCorner[2]; //this is the world-space coordinate that is the lower left corner of the GUI
	float tempVerts[12]; //array of 4 vec3's. need vec3 because we need the height to check if the vert is above water.
	int num_verts_above_water=0;		//# of verts above water total (to use to allocate memory for 2d map)
	int num_verts_in_tile_above_water;	//# of corners above water in a tile
	int i_mapvert=0;	//index for final positions array.
	int i_corner;		//index for looping around the corners of tile.
	int i; //row
	int j; //col
	int r;
	int indexLastLandTile[2]; //TODO: Debug var. indices of the last land tile

	//init the map_gui_info_struct
	memset(mapInfo, 0, sizeof(struct map_gui_info_struct));

	//set an initial camera position so it is in the center of the map
	//mapInfo->mapCameraPos[0] = 383.976f;
	mapInfo->mapCameraPos[0] = 12319.272461f;
	mapInfo->mapCameraPos[1] = 0.0f;
	//mapInfo->mapCameraPos[2] = 383.976f;
	mapInfo->mapCameraPos[2] = 11268.877930f;
	mapInfo->cameraSpeed = 100.0f;
	mapInfo->mapScale = 0.01989f;

	mapInfo->mapVboOffsets = (int*)malloc(g_big_terrain.num_tiles*sizeof(int));
	if(mapInfo->mapVboOffsets == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(mapInfo->mapVboOffsets, 0, g_big_terrain.num_tiles*sizeof(int));

	mapInfo->mapNumVerts = (int*)malloc(g_big_terrain.num_tiles*sizeof(int));
	if(mapInfo->mapNumVerts == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(mapInfo->mapNumVerts, 0, g_big_terrain.num_tiles*sizeof(int));

	numAboveWaterVertsInTile = (char*)malloc(g_big_terrain.num_tiles);
	if(numAboveWaterVertsInTile == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(numAboveWaterVertsInTile, 0, g_big_terrain.num_tiles);

	//calculate the lower left corner of the map because will need to recalculate
	//the z axis coordinates of the 2d map verts because of the diff in axis.
	lowerLeftCorner[0] = 0.0f;
	lowerLeftCorner[1] = g_big_terrain.num_rows*g_big_terrain.tile_len[1];

	//first iterate through all tiles and count how many corners are above the water line
	//look for big tiles that have all 4 corners above water.
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			num_verts_in_tile_above_water = 0;
			ptile = g_big_terrain.pTiles + (i*g_big_terrain.num_cols) + j;

			//account for the origin corner (we aren't going in counterclockwise order because it doesn't matter
			//because we are just counting the # of vertices)
			pvert = ptile->pPos;
			if(pvert[1] >= 0.0f)
			{
				num_verts_in_tile_above_water++;
			}

			//+x,-z corner
			pvert = ptile->pPos + ((ptile->num_x-1)*8);
			if(pvert[1] >= 0.0f)
			{
				num_verts_in_tile_above_water++;
			}

			//-x,+z corner
			pvert = ptile->pPos + (((ptile->num_z-1)*ptile->num_x)*8);
			if(pvert[1] >= 0.0f)
			{
				num_verts_in_tile_above_water++;
			}

			//+x,+z corner
			pvert = ptile->pPos + ((ptile->num_verts-1)*8);
			if(pvert[1] >= 0.0f)
			{
				num_verts_in_tile_above_water++;
			}

			//save the # of verts above water for the tile
			numAboveWaterVertsInTile[(i*g_big_terrain.num_cols)+j] = num_verts_in_tile_above_water;

			//if all 4 vertices are above water, then when we draw we will need two triangles
			if(num_verts_in_tile_above_water == 4)
			{
				num_verts_above_water += 6;	//3 verts * 2 triangles
			}
			else if(num_verts_in_tile_above_water != 0) //there is a mix of vertices above and below water
			{
				MapCountDetailedVerticesInTile(ptile, &r);
				num_verts_above_water += r;

				//TODO: Debug keep track of last tile that we checked
				indexLastLandTile[0] = i;
				indexLastLandTile[1] = j;
			}
		}
	}

	//allocate an array of vec3's using the # of positions
	positions = (float*)malloc(num_verts_above_water*3*sizeof(float));
	if(positions == 0)
	{
		free(numAboveWaterVertsInTile);
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	printf("%s: allocated %d vertices.\n", __func__, num_verts_above_water);

	//next iterate through all tiles and add the corners to the pos array
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			//if there's not vertices above water skip the tile
			if(numAboveWaterVertsInTile[(i*g_big_terrain.num_cols)+j] == 0)
				continue;

			ptile = g_big_terrain.pTiles + (i*g_big_terrain.num_cols) + j;
			
			if(numAboveWaterVertsInTile[(i*g_big_terrain.num_cols)+j] == 4) //if 4 do a quad
			{
				//go through the corners differently this time so that the vertices will be
				//specified in a counter-clockwise order.
				//+x,+z corner
				pvert = ptile->pPos + ((ptile->num_verts-1)*8);
				tempVerts[0] = pvert[0];
				tempVerts[1] = pvert[1];
				tempVerts[2] = pvert[2];

				//+x,-z corner
				pvert = ptile->pPos + ((ptile->num_x-1)*8);
				tempVerts[3] = pvert[0];
				tempVerts[4] = pvert[1];
				tempVerts[5] = pvert[2];

				//origin
				pvert = ptile->pPos;
				tempVerts[6] = pvert[0];
				tempVerts[7] = pvert[1];
				tempVerts[8] = pvert[2];

				//-x,+z corner
				pvert = ptile->pPos + (((ptile->num_z-1)*ptile->num_x)*8);
				tempVerts[9] = pvert[0];
				tempVerts[10] = pvert[1];
				tempVerts[11] = pvert[2];

				//If a quad just write two triangles
				p_position = positions + (i_mapvert*3);

				//+x,+z corner (0)
				p_position[0] = tempVerts[0];	//x
				p_position[1] = -2.0f;
				p_position[2] = tempVerts[2];	//z

				//+x,-z (1)
				p_position[3] = tempVerts[3];
				p_position[4] = -2.0f;
				p_position[5] = tempVerts[5];

				//origin (2)
				p_position[6] = tempVerts[6];
				p_position[7] = -2.0f;
				p_position[8] = tempVerts[8];

				//+x,+z corner (3)
				p_position[9] = tempVerts[0];
				p_position[10] = -2.0f;
				p_position[11] = tempVerts[2];

				//origin (4)
				p_position[12] = tempVerts[6];
				p_position[13] = -2.0f;
				p_position[14] = tempVerts[8];

				//-x,+z (5)
				p_position[15] = tempVerts[9];
				p_position[16] = -2.0f;
				p_position[17] = tempVerts[11];

				//save the offset to the first vert in tile
				mapInfo->mapVboOffsets[(i*g_big_terrain.num_cols)+j] = i_mapvert;
				mapInfo->mapNumVerts[(i*g_big_terrain.num_cols)+j] = 6;

				i_mapvert += 6;
			}
			else
			{
				mapInfo->mapVboOffsets[(i*g_big_terrain.num_cols)+j] = i_mapvert; //save the start offset of the 1st vertex.
				r = MapCreateDetailedVerticesInTile(ptile, positions, &i_mapvert);
				if(r == 0) //error
				{
					printf("%s: error at i=%d j=%d stop.\n", __func__, i, j);
					return 0;
				}
				mapInfo->mapNumVerts[(i*g_big_terrain.num_cols)+j] = r;
			}
		}
	}

	//check that the indices show we loaded the same # of verts as we allocated for
	if(i_mapvert != num_verts_above_water)
	{
		printf("%s: error. map vert # mismatch. i_mapvert=%d num_verts_above_water=%d\n", __func__, i_mapvert, num_verts_above_water);
		return 0;
	}

	//p_positions now contains the vertices
	mapInfo->num_land_vertices = num_verts_above_water;

	//create VBO
	glGenBuffers(1, &(mapInfo->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, mapInfo->vbo);
	glBufferData(GL_ARRAY_BUFFER,	//target buffer.
		(GLsizeiptr)(num_verts_above_water*3*sizeof(float)),	//size in bytes.
		positions,	//pointer to data to copy
		GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	//create VAO
	glGenVertexArrays(1, &(mapInfo->vao));
	glBindVertexArray(mapInfo->vao);
	glBindBuffer(GL_ARRAY_BUFFER, mapInfo->vbo);
	glEnableVertexAttribArray(0); //pos
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	free(positions);
	free(numAboveWaterVertsInTile);
	return 1;
}

/*
This function needs g_big_terrain initialized.
This function initializes a VBO that contains map GUI lines and symbols (flags, squares, circles etc)
*/   
int InitMapElevationLinesVBO(struct map_gui_info_struct * mapInfo)
{
	float * pvertData=0; //full array of all vertex data.
	float * pvertMem=0; //addr of mem alloc for vert data
	struct lvl_1_tile * ptile=0;
	struct line_load_struct * plineInfo=0;
	struct line_load_struct lineLoadInfo;	//this is a linked listish struct of vert data.
	struct timespec tstart;
	struct timespec tend;
	struct timespec tdiff;
	int num_verts=0;
	int vert_offset=0;	//index used to calculate offset for lineVboOffsets
	int i;	//row
	int j;	//col
	int i_vert;  //load index for pvertData array
	int num_symbol_verts;
	int r;

	clock_gettime(CLOCK_MONOTONIC, &tstart);

	mapInfo->num_contours = 3;

	//initialize the arrays that hold vbo offsets and # of verts for the line vbo
	mapInfo->lineVboOffsets = (int*)malloc(mapInfo->num_contours*sizeof(int));
	if(mapInfo->lineVboOffsets == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(mapInfo->lineVboOffsets, 0, mapInfo->num_contours*sizeof(int));

	mapInfo->lineVboNumVerts = (int*)malloc(mapInfo->num_contours*sizeof(int));
	if(mapInfo->lineVboNumVerts == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(mapInfo->lineVboNumVerts, 0, mapInfo->num_contours*sizeof(int));

	//Add verts for symbols to the front of this VBO

	memset(&lineLoadInfo, 0, sizeof(struct line_load_struct));

	//setup the pointer to the latest line_load_struct. CreateElevationLinesInTile can update
	//the line_load_struct param with the latest lines.
	plineInfo = &lineLoadInfo;

	//calculate 2d line verts for waterline contour
	mapInfo->lineVboOffsets[0] = vert_offset; //save the vert offset
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			ptile = g_big_terrain.pTiles + (i*g_big_terrain.num_cols) + j;
			r = CreateElevationLinesInTile(ptile,
					&plineInfo,
					0.0f,	//elevation to draw a line at
					//150.0f,
					&num_verts);
			if(r == 0) //error
				return 0;

			vert_offset += num_verts;

			//TODO: Remove this Debug below. This is an early exit for the first block
			//that has line verts to verify they are drawn ok.
			//if(vert_offset != 0)
			//goto quick_exit;
		}
	}
	mapInfo->lineVboNumVerts[0] = vert_offset;

	//Calculate 2d line verts for 100ft
	mapInfo->lineVboOffsets[1] = vert_offset; //save the vert offset
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			ptile = g_big_terrain.pTiles + (i*g_big_terrain.num_cols) + j;
			r = CreateElevationLinesInTile(ptile,
					&plineInfo,
					30.487f,	//elevation to draw a line at
					&num_verts);
			if(r == 0) //error
				return 0;

			vert_offset += num_verts;
		}
	}
	mapInfo->lineVboNumVerts[1] = vert_offset;

	//Calculate 2d line verts for 200ft
	mapInfo->lineVboOffsets[2] = vert_offset; //save the vert offset
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			ptile = g_big_terrain.pTiles + (i*g_big_terrain.num_cols) + j;
			r = CreateElevationLinesInTile(ptile,
					&plineInfo,
					152.439f,	//elevation to draw a line at
					&num_verts);
			if(r == 0) //error
				return 0;

			vert_offset += num_verts;
		}
	}
	mapInfo->lineVboNumVerts[2] = vert_offset;

//quick_exit:

	//get a total count of all vertices
	num_verts = 0;
	plineInfo = &lineLoadInfo;
	while(plineInfo != 0)
	{
		num_verts += plineInfo->num_verts;

		plineInfo = plineInfo->pNext;
	}

	//call InitMapSymbols here to get a vert count
	r = InitMapSymbols(0, 0, 0, &num_symbol_verts, 1);	//tell InitMapSymbols to just get a vert count.
	if(r == 0)
		return 0;
	num_verts += num_symbol_verts;

	//allocate mem for lines VBO
	pvertMem = (float*)malloc(num_verts*3*sizeof(float));
	if(pvertMem == 0)
		return 0;
	printf("%s: allocated for %d vertices.\n", __func__, num_verts);
	pvertData = pvertMem;

	//put symbol vertices in the beginngin of the VBO
	r = InitMapSymbols(pvertData, mapInfo->lineSymbolOffsets, mapInfo->lineSymbolNumVerts, &num_symbol_verts, 0);
	if(r == 0)
		return 0;
	//advance pvertData by the # of floats we wrote in InitMapSymbols
	pvertData += (3*num_symbol_verts);
	
	//copy all vertices into the newly formed array.
	i_vert = 0;
	plineInfo = &lineLoadInfo;
	while(plineInfo != 0)
	{
		memcpy((pvertData + (i_vert*3)), plineInfo->positions, plineInfo->num_verts*3*sizeof(float)); 
		i_vert += plineInfo->num_verts;

		plineInfo = plineInfo->pNext;
	}

	//i_vert and num_verts should now match
	if((i_vert+num_symbol_verts) != num_verts)
	{
		printf("%s: error. # of verts %d loaded mismatch from %d allocated.\n", __func__, i_vert, num_verts);
		return 0;
	}
	mapInfo->num_line_verts = num_verts;

	//create VBO
	glGenBuffers(1, &(mapInfo->lineVbo));
	glBindBuffer(GL_ARRAY_BUFFER, mapInfo->lineVbo);
	glBufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(num_verts*3*sizeof(float)),
			pvertMem,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//create VAO
	glGenVertexArrays(1, &(mapInfo->lineVao));
	glBindVertexArray(mapInfo->lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, mapInfo->lineVbo);
	glEnableVertexAttribArray(0); //pos
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//free all the mem we allocated for setup
	free(pvertMem);
	plineInfo = &lineLoadInfo;
	while(plineInfo != 0)
	{
		free(plineInfo->positions);
		plineInfo = plineInfo->pNext;
	}

	clock_gettime(CLOCK_MONOTONIC, &tend);
	GetElapsedTime(&tstart, &tend, &tdiff);
	printf("%s: elapsed time. sec=%ld nsec=%ld\n", __func__, tdiff.tv_sec, tdiff.tv_nsec);

	return 1;
}

/*
This func counts or fills in vert data into a float array passed as positions, and then places
the vert offsets into symbolOffsets. numVertsWritten is updated with the # of verts
written to positions.
-"flags" param controls whether or not the function counts how many vertices it needs or
actually writes to positions, symbolOffsets.
	flags=0	;write vert data to positions.
	flags=1	;count # of verts needed and write # to numVertsWritten. (this is so we call with flags=1 to allocate verts)
-assume symbolOffsets is an array of 3 ints.
-assume positions is an array of vec3s:
	0: square
	1: circle
	2: flag
-stuff on the map has positions that are on the x,z plane.
*/
int InitMapSymbols(float * positions, int * symbolOffsets, int * numVertsWrittenArray, int * totalNumVerts, int flags)
{
	float * pPos=0;
	float fsquareHalfWidth=5.0f;
	float fcircleRadius;
	float fcircleSlice; //circle slice in radians
	int numVerts=0;
	int i;
	int i_slice; //intermediate slice that is mod 12

	*totalNumVerts = 0;

	if(flags == 0) //only write if flags is 0
	{	
		pPos = positions;
		symbolOffsets[0] = 0;

		//Square (6 verts)
		//0 +x,-z
		pPos[0] = fsquareHalfWidth;
		pPos[1] = -2.0f;
		pPos[2] = -1.0f*fsquareHalfWidth;

		//1 -x,-z
		pPos[3] = -1.0f*fsquareHalfWidth;
		pPos[4] = -2.0f;
		pPos[5] = -1.0f*fsquareHalfWidth;

		//2  -x,+z
		pPos[6] = -1.0f*fsquareHalfWidth;
		pPos[7] = -2.0f;
		pPos[8] = fsquareHalfWidth;

		//3 -x,+z
		pPos[9] = -1.0f*fsquareHalfWidth;
		pPos[10] = -2.0f;
		pPos[11] = fsquareHalfWidth;

		//4 +x,+z
		pPos[12] = fsquareHalfWidth;
		pPos[13] = -2.0f;
		pPos[14] = fsquareHalfWidth;

		//5 +x,-z
		pPos[15] = fsquareHalfWidth;
		pPos[16] = -2.0f;
		pPos[17] = -1.0f*fsquareHalfWidth;
	}
	
	if(flags == 0)
		numVertsWrittenArray[0] = 6;
	numVerts = 6;
	*totalNumVerts += numVerts;

	//Circle (36 verts)
	if(flags == 0)
	{
		symbolOffsets[1] = 6;
		pPos += (numVerts*3);
	}
	
	//make a cirlce with 12 slices in it
	fcircleSlice = (2.0f*PI)/12.0f;
	fcircleRadius = 11.0f;
	numVerts = 0;
	for(i = 0; i < 12; i++)
	{
		if(flags == 0)
		{
			//center
			pPos[0] = 0.0f;
			pPos[1] = -2.0f;
			pPos[2] = 0.0f;

			//go out to the circumference of the circle
			pPos[3] = fcircleRadius*cosf(((float)i)*fcircleSlice);
			pPos[4] = -2.0f;
			pPos[5] = -1.0f*fcircleRadius*sinf(((float)i)*fcircleSlice);

			i_slice = (i + 1) % 12;
			pPos[6] = fcircleRadius*cosf(((float)(i_slice))*fcircleSlice);
			pPos[7] = -2.0f;
			pPos[8] = -1.0f*fcircleRadius*sinf(((float)i_slice)*fcircleSlice);

			pPos += 9;	//3 verts * 3 floats per vert
		}
		numVerts += 3;
	}
	if(flags == 0)
		numVertsWrittenArray[1] = numVerts;
	*totalNumVerts += numVerts;

	//Flag (12 verts)
	//origin for Flag is the bottom of the flag pole
	if(flags == 0)
	{
		symbolOffsets[2] = *totalNumVerts;
		//flag mast
		//0 +x,-z
		pPos[0] = 1.0f;
		pPos[1] = -2.0f;
		pPos[2] = -8.0f;

		//1 -x,-z
		pPos[3] = 0.0f;
		pPos[4] = -2.0f;
		pPos[5] = -8.0f;

		//2 -x,+z
		pPos[6] = 0.0f;
		pPos[7] = -2.0f;
		pPos[8] = 0.0f;
		
		//3 -x,+z
		pPos[9] = 0.0f;
		pPos[10] = -2.0f;
		pPos[11] = 0.0f;

		//4 +x,+z
		pPos[12] = 1.0f;
		pPos[13] = -2.0f;
		pPos[14] = 0.0f;

		//5 +x,-z
		pPos[15] = 1.0f;
		pPos[16] = -2.0f;
		pPos[17] = -8.0f;

		//flag body
		//0 +x,-z
		pPos[18] = 19.0f;
		pPos[19] = -2.0f;
		pPos[20] = -25.0f;

		//1 -x,-z
		pPos[21] = 0.0f;
		pPos[22] = -2.0f;
		pPos[23] = -25.0f;

		//2 -x,+z
		pPos[24] = 0.0f;
		pPos[25] = -2.0f;
		pPos[26] = -8.0f;
		
		//3 -x,+z
		pPos[27] = 0.0f;
		pPos[28] = -2.0f;
		pPos[29] = -8.0f;

		//4 +x,+z
		pPos[30] = 19.0f;
		pPos[31] = -2.0f;
		pPos[32] = -8.0f;

		//5 +x,-z
		pPos[33] = 19.0f;
		pPos[34] = -2.0f;
		pPos[35] = -25.0f;
	}

	if(flags == 0)
		numVertsWrittenArray[2] = 12;
	*totalNumVerts += 12;

	return 1;
}

/*
This function loops through all quads in a terrain tile and comes up with a count of
vertices needed for the 2d map. This is so memory can be allocated for vertices.
*/
void MapCountDetailedVerticesInTile(struct lvl_1_tile * ptile, int * num_land_verts)
{
	float * p_pos=0;
	char isCornerAboveWater[4]; //flag for each corner in the quad. 1=above water. 0=below water.
	int i; //row index
	int j; //column index
	int num_verts_to_alloc=0;
	int quad_num_verts_above_water=0;
	int num_triangles;

	for(i = 0; i < g_big_terrain.tile_num_quads[0]; i++)
	{
		for(j = 0; j < g_big_terrain.tile_num_quads[1]; j++)
		{
			quad_num_verts_above_water = 0;

			//get origin corner
			p_pos = ptile->pPos + (((i*ptile->num_x) + j)*8);
			if(p_pos[1] >= 0.0f)
			{
				quad_num_verts_above_water += 1;
				isCornerAboveWater[0] = 1;
			}

			//get +x,-z corner
			p_pos = ptile->pPos + (((i*ptile->num_x) + j+1)*8);		
			if(p_pos[1] >= 0.0f)
			{
				quad_num_verts_above_water += 1;
				isCornerAboveWater[1] = 1;
			}

			//get +x,+z corner
			p_pos = ptile->pPos + ((((i+1)*ptile->num_x) + j+1)*8);		
			if(p_pos[1] >= 0.0f)
			{
				quad_num_verts_above_water += 1;
				isCornerAboveWater[2] = 1;
			}

			//get -x,+z corner
			p_pos = ptile->pPos + ((((i+1)*ptile->num_x) + j)*8);		
			if(p_pos[1] >= 0.0f)
			{
				quad_num_verts_above_water += 1;
				isCornerAboveWater[3] = 1;
			}

			if(quad_num_verts_above_water == 4)
			{
				num_verts_to_alloc += 6;
			}
			else
			{
				num_triangles = quad_num_verts_above_water;
				num_verts_to_alloc += (3*num_triangles);	//will specify 3 verts for each triangle
			}

		}
	}

	*num_land_verts = num_verts_to_alloc;
}

/*
This function fills in vertices into map_pos to make triangles. Assumes that i_map is pointing
to the next free vertex in map_pos.
-returns # of verts added to map_pos array.
*/
int MapCreateDetailedVerticesInTile(struct lvl_1_tile * ptile, float * map_pos, int * i_map)
{
	int num_verts_loaded; //# of verts in the triangle_verts array
	float * quad_pos[4]; //0=origin, 1=+z, 2=+x,+z, 3=+x
	float * p_map_vert=0;
	float triangle_verts[18]; //array of 6 vec3's
	float tempVec[3];
	float lastPos[3];	//temporary vec3 that keeps track of last vertex placed in map_pos. used when creating triangles.
	char isVertLand[4]; //flags associated with quad_pos that is set to 1 if vert above water.
	int num_corners_above_water;
	int num_new_verts; //just a debug counter of new vertices that were added
	int num_exp_new_verts; //expected # of verts that were added.
	int total_num_new_verts=0; //total # of created verts
	int i; //row index
	int j; //col index
	int k; //some index
	int i_vert;
	int i_start; //just a temporary index for finding the vert to start with (indexes quad_pos array)(only indexes corners)
	int i_next; //indexes quad_pos array
	int i_prev; //indexes quad_pos array
	int r;

	memset(isVertLand, 0, 4);
	p_map_vert = map_pos + ((*i_map)*3);

	for(i = 0; i < g_big_terrain.tile_num_quads[0]; i++)
	{
		for(j = 0; j < g_big_terrain.tile_num_quads[1]; j++)
		{
			num_new_verts = 0;

			//get the positions of the quad corners
			quad_pos[0] = ptile->pPos + (((i*ptile->num_x) + j)*8); 		//origin
			quad_pos[1] = ptile->pPos + ((((i+1)*ptile->num_x) + j)*8); 	//+z
			quad_pos[2] = ptile->pPos + ((((i+1)*ptile->num_x) + j+1)*8);	//+x,+z
			quad_pos[3] = ptile->pPos + (((i*ptile->num_x) + j+1)*8); 		//+x

			//count how many vertices are above water
			num_corners_above_water = 0;
			for(i_vert = 0; i_vert < 4; i_vert++)
			{
				if((quad_pos[i_vert])[1] >= 0.0f)
				{
					num_corners_above_water += 1;
					isVertLand[i_vert] = 1;
				}
				else
				{
					isVertLand[i_vert] = 0;
				}
			}

			//if there are no vertices above water skip this quad
			if(num_corners_above_water == 0)
				continue;

			//create the verts array, this is the array from which the triangles
			//will be made.
			//Find a vertex that is above water as a starting point
			for(i_vert = 0; i_vert < 4; i_vert++)
			{
				i_start = (6 - i_vert) % 4; //2=+x+z vert, then move counter-clockwise around the quad. Start at 6 so when you subtract you still get a positive #.
				if(isVertLand[i_start] == 1)	
					break;
			}
			//at this point i_start is pointing to the first vert, now go counter-clockwise around
			//the quad, and fill in triangle_verts with all vertices
			num_verts_loaded = 0;
			i_vert = i_start; //save i_start as the starting point
			for(k = 0; k < 4; k++) //this just loops 4 times, but k doesn't keep track. i_vert is the loop tracker.
			{
				//if current vertex is above water add it
				if(isVertLand[i_vert] == 1) //the mod calc is because I decided to reset where 0 was
				{
					triangle_verts[(num_verts_loaded*3)] = (quad_pos[i_vert])[0];
					triangle_verts[(num_verts_loaded*3)+1] = (quad_pos[i_vert])[1];
					triangle_verts[(num_verts_loaded*3)+2] = (quad_pos[i_vert])[2];
					num_verts_loaded += 1;
				}

				//calculate the index of the next vertex
				i_next = (i_vert + 1) % 4;

				//if next vertex is below water and this vertex was above water, then make a midpoint vertex
				if(isVertLand[i_vert] == 1 && isVertLand[i_next] == 0)
				{
					r = MapCalcMidpointAboveWater(quad_pos[i_vert],	//start vertex
							quad_pos[i_next], //end vertex
							tempVec, //waterline position
							0.0f);
					if(r == 0) //error.
					{
						return 0;
					}
					triangle_verts[(num_verts_loaded*3)] = tempVec[0];
					triangle_verts[(num_verts_loaded*3)+1] = tempVec[1];
					triangle_verts[(num_verts_loaded*3)+2] = tempVec[2];
					num_verts_loaded += 1;
				}
				//get the opposite case as above. going from underwater to above land.
				else if(isVertLand[i_vert] == 0 && isVertLand[i_next] == 1)
				{
					r = MapCalcMidpointAboveWater(quad_pos[i_next],	//start vertex
							quad_pos[i_vert], //end vertex
							tempVec,	//waterline position
							0.0f);
					if(r == 0) //error.
					{
						return 0;
					}
					triangle_verts[(num_verts_loaded*3)] = tempVec[0];
					triangle_verts[(num_verts_loaded*3)+1] = tempVec[1];
					triangle_verts[(num_verts_loaded*3)+2] = tempVec[2];
					num_verts_loaded += 1;
				}

				//move to next vertex
				i_vert = (i_vert + 1) % 4;
			}
			if(num_verts_loaded > 6)
			{
				printf("%s: error. loaded %d verts but triangle_verts only has room for 6.\n", __func__, num_verts_loaded);
				return 0;
			}
			
			//so now we have triangle_verts filled out in counter-clockwise order a set of border verts
			//Now make the triangles. 
			//i_start saves a starting point on a corner vertex
			//index 0 of triangle_verts is i_start

			//special case, 2 corners above water, but separated from each other
			i_next = (i_start + 1) % 4;
			i_prev = (i_start - 1) % 4;
			if(num_corners_above_water == 2 && isVertLand[i_start] == 1 && isVertLand[i_next] == 0 && isVertLand[i_prev] == 0)
			{
				//triangle 1
				p_map_vert[0] = triangle_verts[0];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[2];
				*i_map += 1;

				//re-purpose i_next, i_prev so they are indexing in triangle_verts
				p_map_vert += 3; //pointer arithmetic
				i_next = 1;
				i_prev = num_verts_loaded - 1;
				
				//get the midpoint vert following this vert
				p_map_vert[0] = triangle_verts[(i_next*3)];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[(i_next*3)+2];
				*i_map += 1;
				p_map_vert += 3; //pointer arithmetic

				//get the midpoint vert behind this vert
				p_map_vert[0] = triangle_verts[(i_prev*3)];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[(i_prev*3)+2];
				*i_map += 1;
				p_map_vert += 3;//pointer arithmetic
				
				//triangle 2
				//go to the next above-land vertex
				i_vert = 3; //the next two verts should be mid-points and then the 3rd vert should be an original above-land one.
				i_next = (i_vert + 1) % num_verts_loaded; //this should be a midpoint vert
				i_prev = (i_vert - 1) % num_verts_loaded; //this should be a midpoint vert
				p_map_vert[0] = triangle_verts[(i_vert*3)];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[(i_vert*3)+2];
				*i_map += 1;
				p_map_vert += 3; //pointer arithmetic

				p_map_vert[0] = triangle_verts[(i_next*3)];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[(i_next*3)+2];
				*i_map += 1;
				p_map_vert += 3; //pointer arithmetic

				p_map_vert[0] = triangle_verts[(i_prev*3)];
				p_map_vert[1] = -2.0f;
				p_map_vert[2] = triangle_verts[(i_prev*3)+2];
				*i_map += 1;
				p_map_vert += 3; //pointer arithmetic

				num_new_verts += 6;
			}
			else //all other cases vertices are together
			{
				//pre-load lastPos with next vert
				if(num_verts_loaded < 2)
				{
					printf("%s: error. unexpected # of vertices calculated.\n", __func__);
					return 0;
				}
				lastPos[0] = triangle_verts[3];
				lastPos[1] = triangle_verts[4];
				lastPos[2] = triangle_verts[5];

				//repurpose i_vert
				//loop through each vertex in triangle_verts array. 
				//index 0 corresponds to i_start, but start at i_vert=2 which is the last vertex in the
				//triangle, because we will pre-fill the previous 2 vertices in each iteration
				for(i_vert = 2; i_vert < num_verts_loaded; i_vert++)
				{
					//add this same vertex everytime. It will be the start of each land triangle in the quad
					p_map_vert[0] = triangle_verts[0];
					p_map_vert[1] = -2.0f;
					p_map_vert[2] = triangle_verts[2];
					*i_map += 1;
					p_map_vert += 3; //pointer arithmetic
					num_new_verts += 1;

					p_map_vert[0] = lastPos[0];
					p_map_vert[1] = -2.0f;
					p_map_vert[2] = lastPos[2];
					*i_map += 1;
					p_map_vert += 3; //pointer arithmetic
					num_new_verts += 1;

					p_map_vert[0] = triangle_verts[(i_vert*3)];
					p_map_vert[1] = -2.0f;
					p_map_vert[2] = triangle_verts[(i_vert*3)+2];
					lastPos[0] = p_map_vert[0]; //save this position because we need to re-specify it for the next triangle
					lastPos[1] = p_map_vert[1];
					lastPos[2] = p_map_vert[2];
					*i_map += 1;
					p_map_vert += 3; //pointer arithmetic
					num_new_verts += 1;
				}

			}

			//add a check to make sure we added the expected # of vertices
			if(num_corners_above_water == 4)
			{
				num_exp_new_verts = 6;	
			}
			else
			{
				num_exp_new_verts = num_corners_above_water*3;
			}
			if(num_new_verts != num_exp_new_verts)
			{
				printf("%s: error. added more new vertices than allocated.\n", __func__);
			}

			total_num_new_verts += num_new_verts;
		}
	}

	//check that # of new verts created in this tile match the estimate
	//TODO: Remove this debug check.
	MapCountDetailedVerticesInTile(ptile, &r);
	if(r != total_num_new_verts)
	{
		printf("%s: error. # of created verts doesn't match allocated.\n alloc=%d created=%d\n", __func__, r, total_num_new_verts);
	}
	
	return total_num_new_verts;
}

/*
This function finds a point on the side of a terrain quad that is at the water line, y=0
-startPos must be above water
-endPos must be below water
-felevation must be positive
*/
int MapCalcMidpointAboveWater(float * startPos, float * endPos, float * outPos, float felevation)
{
	float ray[3];
	float unitVec[3];
	float distToElevation;
	float t;

	//check that endPos is below the water and startPos is above it
	if(startPos[1] < felevation)
	{
		printf("%s: error. startPos=(%f,%f,%f) is below water.\n", __func__, startPos[0], startPos[1], startPos[2]);
		return 0;
	}
	if(endPos[1] >= felevation)
	{

		printf("%s: error. endPos=(%f,%f,%f) is above water.\n", __func__, endPos[0], endPos[1], endPos[2]);
		return 0;
	}

	distToElevation = startPos[1] - felevation;

	vSubtract(ray, endPos, startPos);
    unitVec[0] = ray[0];
	unitVec[1] = ray[1];
	unitVec[2] = ray[2];
	vNormalize(unitVec);
	
	t = fabs(distToElevation/unitVec[1]);
	
	outPos[0] = startPos[0] + t*unitVec[0];
	outPos[1] = startPos[1] + t*unitVec[1];
	outPos[2] = startPos[2] + t*unitVec[2];

	return 1;
}

/*
This function given a terrain tile, goes and creates a set of line points and puts them in
a line_load_struct. The idea is that the line_load_struct is a temporary one used only at
initialization. Once the line data is complete, the line verts are copied into a VBO.
*/
int CreateElevationLinesInTile(struct lvl_1_tile * ptile, struct line_load_struct ** inLineData, float felevation, int * num_lineverts_added)
{
	char * hasSearchedQuad;	//byte for each quad in tile. 99 x 99 = 9801
	struct line_load_struct * curLineData=0;
	float * quad_pos[4]; //array of pointers. each pointer points to a vec3 that represents a corner.
	float * ptopPos;
	float * pbottomPos;
	float curLinePos[3];
	float lastLinePos[3]; //keeps track of last pos, for use with making line segments.
	int startQuad[2];	//0=row index, 1=col index
	char startSide; //can be 0=-x, 1=+z, 2=+x, 3=-z. 
	char curSide; //can be 0=-x, 1=+z, 2=+x, 3=-z.
	char isAboveHeight[4]; //flag for each corner
	int tripCounter; //this counts up while loop iterations to prevent an infinite loop.
	int i_corner;	//index of corner in a quad.
	int i_nextCorner; //index of the next corner.
	int hasFoundStart;
	int curQuad[2]; //0=row, 1=col. current quad when following a line.
	int isLineStart; //set to 1 if the vert is a start of a new line.
	int isFollowingLine;
	int i; //row index
	int j; //col index
	int k; //arbitrary index
	int r;

	*num_lineverts_added = 0;

	hasSearchedQuad = (char*)malloc(9801);
	if(hasSearchedQuad == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return 0;
	}
	memset(hasSearchedQuad, 0, 9801);

	//if the line_load_struct is uninitialized, allocate one block of data.
	if((*inLineData)->positions == 0)
	{
		(*inLineData)->positions = (float*)malloc(1024*sizeof(float));
		if((*inLineData)->positions == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		memset((*inLineData)->positions, 0, 1024*sizeof(float));
		(*inLineData)->num_verts = 0;
		(*inLineData)->pNext = 0;
	}

	//make a working variable to let inLineData act as a head for the linked list
	curLineData = *inLineData;

	//find a spot to start
	for(i = 0; i < g_big_terrain.tile_num_quads[0]; i++)
	{
		for(j = 0; j < g_big_terrain.tile_num_quads[1]; j++)
		{
			hasFoundStart = 0;

			//we might have already searched this quad while following a line. If so skip it
			if(hasSearchedQuad[(i*g_big_terrain.tile_num_quads[1])+j])
				continue;

			//flag that we are looking for the start of a line.
			isLineStart = 1;

			//get the positions of the corners of the quad
			quad_pos[0] = ptile->pPos + (((i*ptile->num_x) + j)*8);			//origin
			quad_pos[1] = ptile->pPos + ((((i+1)*ptile->num_x) + j)*8);		//+z
			quad_pos[2] = ptile->pPos + ((((i+1)*ptile->num_x) + j+1)*8);	//+x,+z
			quad_pos[3] = ptile->pPos + (((i*ptile->num_x) + j+1)*8);		//+x

			//create isAboveHeight array so it has flags for each corner
			for(i_corner = 0; i_corner < 4; i_corner++)
			{
				if(quad_pos[i_corner][1] >= felevation)
					isAboveHeight[i_corner] = 1;
				else
					isAboveHeight[i_corner] = 0;
			}

			//start by looking for the start of a line
			//loop thru all corners and look for where one corner is above the height
			//and the other is below it
			for(i_corner = 0; i_corner < 4; i_corner++)
			{
				i_nextCorner = (i_corner + 1) % 4;
				
				//use XOR to identify if the corner and the nextCorner are on opposite
				//sides of the elevation line.
				if(isAboveHeight[i_corner] ^ isAboveHeight[i_nextCorner])
				{
					//save the info about where the start is
					startQuad[0] = i;
					startQuad[1] = j;
					curQuad[0] = i;
					curQuad[1] = j;
					startSide = (char)i_corner;
					curSide = (char)i_corner;

					//calculate the line vert pos
					if(isAboveHeight[i_corner])
					{
						ptopPos = quad_pos[i_corner];
						pbottomPos = quad_pos[i_nextCorner];
					}
					else
					{
						ptopPos = quad_pos[i_nextCorner];
						pbottomPos = quad_pos[i_corner];
					}
					r = MapCalcMidpointAboveWater(ptopPos, 
							pbottomPos, 
							lastLinePos, 
							felevation);
					if(r == 0)	//error
						return 0;
					lastLinePos[1] = -1.5f;
					isLineStart = 0; //flag that we have loaded a valid pos into lastLinePos.

					//Add the line
					//r = MapLineAddVert(&curLineData, curLinePos);
					//if(r == 0)
					//	return 0; //error
					//*num_lineverts_added += 1;
					hasSearchedQuad[(i*g_big_terrain.tile_num_quads[1])+j] = (char)1;

					hasFoundStart = 1;
					break; //stop since we found a starting point
				}
			}

			//we didn't find the start of a line so continue to the next quad.
			if(hasFoundStart == 0)
			{
				hasSearchedQuad[(i*g_big_terrain.tile_num_quads[1])+j] = (char)1;
				continue; 
			}

			//At this point, we have identified a starting point with: startQuad coord and startSide.
			//Now start tracking the line through the quads.
			tripCounter = 0;
			while(1)
			{
				//This is a check to make sure the loop didn't run too many times
				if(tripCounter > 9801)
				{
					printf("%s: error. while loop stuck in loop.\n", __func__);
					return 0;
				}

				//Move to a new quad based on the side the line intersected.
				switch(curSide)
				{
				case 0:	//-x side
					curQuad[1] -= 1;
					break;
				case 1: //+z side
					curQuad[0] += 1;
					break;
				case 2: //+x side
					curQuad[1] += 1;
					break;
				case 3: //-z side
					curQuad[0] -= 1;
					break;
				default:
					printf("%s: error. invalid side while finding next quad, curSide=%hhd\n", __func__, curSide);
					return 0;
				}

				//check that quad we want to move to is in the tile. If it isn't then
				//move on.
				if(curQuad[0] < 0 || curQuad[0] > 98)
				{
					isLineStart = 1; //reset start of line flag.
					break;
				}
				if(curQuad[1] < 0 || curQuad[1] > 98)
				{
					isLineStart = 1;
					break;
				}

				//now we are in the new quad.
				//we need to convert the curSide to a new side that it is on in the quad
				switch(curSide)
				{
				case 0: //-x side
					curSide = 2; //-x to +x side
					break;
				case 1: //+z side
					curSide = 3; //+z to -z side
					break;
				case 2: //+x side
					curSide = 0; //+x to -x side
					break;
				case 3: //-z side
					curSide = 1; //-z to +z side
					break;
				}

				if(hasSearchedQuad[(curQuad[0]*g_big_terrain.tile_num_quads[1])+curQuad[1]])
				{
					//printf("%s: following line (k=%d) in already searched quad r=%d c=%d\n", __func__, tripCounter, curQuad[0], curQuad[1]);
					isLineStart = 1;
					break;
				}

				//fill in the corner positions for the new quad
				quad_pos[0] = ptile->pPos + (((curQuad[0]*ptile->num_x) + curQuad[1])*8); 			//origin
				quad_pos[1] = ptile->pPos + ((((curQuad[0]+1)*ptile->num_x) + curQuad[1])*8); 		//+z
				quad_pos[2] = ptile->pPos + ((((curQuad[0]+1)*ptile->num_x) + curQuad[1] + 1)*8); 	//+x,+z
				quad_pos[3] = ptile->pPos + (((curQuad[0]*ptile->num_x) + curQuad[1] + 1)*8);		//+x
				memset(isAboveHeight, 0, 4);
				for(i_corner = 0; i_corner < 4; i_corner++)
				{
					if(quad_pos[i_corner][1] >= felevation)
						isAboveHeight[i_corner] = 1;
					else
						isAboveHeight[i_corner] = 0;
				}

				//now that we have the current point figured out start the search for the line in this
				//quad. Start by searching the other 3 sides.
				isFollowingLine = 0;
				for(k = 0; k < 3; k++)
				{
					i_corner = (curSide + 1 + k) % 4; //add 1 so we don't search the side we start at (this is the side that lead us to this quad)
					i_nextCorner = (i_corner + 1) % 4;
					curSide = (char)i_corner; //update the current i_corner

					if(isAboveHeight[i_corner] ^ isAboveHeight[i_nextCorner])
					{
						if(isAboveHeight[i_corner])
						{
							ptopPos = quad_pos[i_corner];
							pbottomPos = quad_pos[i_nextCorner];
						}
						else
						{
							ptopPos = quad_pos[i_nextCorner];
							pbottomPos = quad_pos[i_corner];
						}
						r = MapCalcMidpointAboveWater(ptopPos, pbottomPos, curLinePos, felevation);
						if(r == 0) //error
							return 0;
						curLinePos[1] = -1.5f;

						//add the last line pos as the start of the line.
						r = MapLineAddVert(&curLineData, lastLinePos);
						if(r == 0) //error
							return 0;
						*num_lineverts_added += 1;

						//add the current pos as the finish of the line segment.
						r = MapLineAddVert(&curLineData, curLinePos);
						if(r == 0) //error
							return 0;
						*num_lineverts_added += 1;

						//setup for the next line segment
						lastLinePos[0] = curLinePos[0];
						lastLinePos[1] = curLinePos[1];
						lastLinePos[2] = curLinePos[2];
						isLineStart = 0;

						hasSearchedQuad[(curQuad[0]*g_big_terrain.tile_num_quads[1]) + curQuad[1]] = (char)1;
						
						//move on to the next quad based on curSide
						tripCounter += 1;
						isFollowingLine = 1;	//flag that we are following a line and need to stay in the loop
						break;
					}
				}
				
				if(isFollowingLine)
				{
					continue;
				}
				else
				{
					//we didn't find any side where the line could be. move on.
					hasSearchedQuad[((curQuad[0]*g_big_terrain.tile_num_quads[1]) + curQuad[1])] = (char)1;
					break;
				}
			}
		}
	}

	//Error check. Make sure that the number of verts we find is divisble by 2. Since we can only add line
	//segments.
	if((*num_lineverts_added % 2) != 0)
	{
		printf("%s: error. added %d verts, which is not divisble by 2.\n", __func__, *num_lineverts_added);
		return 0;
	}
	
	free(hasSearchedQuad);
	*inLineData = curLineData; //set the inLineData param to the latest load_info_struct
	return 1;
}

/*
This is a helper func for adding a new vert.
*/   
int MapLineAddVert(struct line_load_struct ** inLineData, float * newPos)
{
	struct line_load_struct * lineData = *inLineData;
	struct line_load_struct * newLineData = 0;

	//check if the line_load_struct is full
	if(lineData->num_verts >= 341) //341 vec3's can fit into a 0x1000 mem page.
	{
		//make a new line_load_struct
		newLineData = (struct line_load_struct*)malloc(sizeof(struct line_load_struct));
		if(newLineData == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		memset(newLineData, 0, sizeof(struct line_load_struct));
		newLineData->positions = (float*)malloc(1024*sizeof(float));
		if(newLineData->positions == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return 0;
		}
		memset(newLineData->positions, 0, 1024*sizeof(float));

		//now make lineData point to the newly created line data
		lineData->pNext = newLineData;
		lineData = newLineData;
		*inLineData = newLineData;
	}

	lineData->positions[(lineData->num_verts*3)] = newPos[0];
	lineData->positions[(lineData->num_verts*3)+1] = newPos[1];
	lineData->positions[(lineData->num_verts*3)+2] = newPos[2];
	
	lineData->num_verts += 1;

	//do a check just to make sure num_verts is still within bounds
	if(lineData->num_verts > 341)
	{
		printf("%s: error. lineData->num_verts out of range at %d\n", __func__, lineData->num_verts);
		return 0;
	}

	return 1;
}

/*
-because this func for now uses the orthographic matrix of the inventory gui, the
origin is the bottom-left of the screen, therefore in order to be positioned sensibly
the map needs to be shifted up and to the right.
*/
void DrawMapGUI(void)
{
	float tempVec4[4];
	float mModelToCameraMat[16];
	float mCameraMat[16];
	float mCameraRotMat[16];
	float mModelMat[16];
	float mTranslateMat[16]; //this is for just any general transforms
	float mCameraTranslateMat[16]; //This is for the inverse camera translation
	float mScaleMat[16];
	float color[3];
	float fmapScale;
	int i; //row index
	int j; //col index

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//Calculate the camera orientation
	//by default camera faces down -z axis with an up vector up the y-axis, but we need it
	//pointing down the -y axis to show the terrain as a 2d map. So we need to rotate around the x-axis by
	//-90.0f, So we need to transform the world by the opposite amount: 90.0f
	mmRotateAboutX(mCameraRotMat, 90.0f);

	//Need to scale the map 2d model so it fits in the 1024x768 GUI projection view
	//fmapScale = 0.01989f;
	fmapScale = g_gui_map.mapScale;
	mmMakeIdentityMatrix(mScaleMat);
	mScaleMat[0] = fmapScale;
	mScaleMat[5] = 1.0f;	//don't mess with y-coord since it has to be between near and far plane
	mScaleMat[10] = fmapScale;
	
	//mmTranslateMatrix(mTranslateMat, 0.0f, 0.0f, -767.9529f); //move the land down the z-axis so it appears given the orthographic mat
	//mmTranslateMatrix(mTranslateMat, -383.976f, 0.0f, -383.976f); //this aligns the land right in the middle of the window
	mmTranslateMatrix(mCameraTranslateMat, (-1.0f*g_gui_map.mapCameraPos[0]), (-1.0f*g_gui_map.mapCameraPos[1]), (-1.0f*g_gui_map.mapCameraPos[2])); //invert the camera position, since this will translate the world.
	mmMultiplyMatrix4x4(mScaleMat, mCameraTranslateMat, mCameraMat);

	//set the land fill color to a sandy color
	color[0] = 0.8745f;
	color[1] = 0.8235f;
	color[2] = 0.647f;

	//color[0] = 1.0f;//TODO: Debug with a red color
	//color[1] = 0.0f;
	//color[2] = 0.0f;

	//Draw the map
	mmMultiplyMatrix4x4(mCameraRotMat, mCameraMat, mModelToCameraMat);
	glUseProgram(g_gui_shaders.program);
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelToCameraMat);
	glBindVertexArray(g_gui_map.vao);
	for(i = 0; i < g_big_terrain.num_rows; i++)
	{
		for(j = 0; j < g_big_terrain.num_cols; j++)
		{
			glDrawArrays(GL_TRIANGLES, 										//mode.
					g_gui_map.mapVboOffsets[(i*g_big_terrain.num_cols)+j],	//start index.
					g_gui_map.mapNumVerts[(i*g_big_terrain.num_cols)+j]);	//number indices to render.
		}
	}

	//Draw lines
	color[0] = 0.1647f; //bright-blue
	color[1] = 0.9411f;
	color[2] = 0.8901f;
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	
	//switch to the VAO with lines
	glBindVertexArray(g_gui_map.lineVao);
	
	//bright blue line waterline
	glDrawArrays(GL_LINES, 0, g_gui_map.lineVboNumVerts[0]);

	color[0] = 0.5098f; //brown
	color[1] = 0.4f;
	color[2] = 0.2235f;
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	glDrawArrays(GL_LINES, g_gui_map.lineVboOffsets[1], g_gui_map.lineVboNumVerts[1]);
	glDrawArrays(GL_LINES, g_gui_map.lineVboOffsets[2], g_gui_map.lineVboNumVerts[2]);

	//Disable depth testing since the symbols will overlay everything
	glDisable(GL_DEPTH_TEST);
	//Draw the current player location
	color[0] = 0.9019f; //orange
	color[1] = 0.6078f;
	color[2] = 0.1450f;
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	tempVec4[0] = g_a_man.pos[0];
	tempVec4[1] = 0.0f;
	tempVec4[2] = g_a_man.pos[2];
	tempVec4[3] = 1.0f;
	mmTransformVec(mCameraMat, tempVec4); //we only want to transform the translation not the model(model is sized for the screen). use model mat from earlier which is mapScaleMat x CameraTranslateMat
	mmTranslateMatrix(mTranslateMat, tempVec4[0], 0.0f, tempVec4[2]);  
	mmMultiplyMatrix4x4(mCameraRotMat, mTranslateMat, mModelToCameraMat);
	glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelToCameraMat);
	glDrawArrays(GL_TRIANGLES, g_gui_map.lineSymbolOffsets[1], g_gui_map.lineSymbolNumVerts[1]);//[1]=circle

	//Draw the base flag
	color[0] = 0.0f;
	color[1] = 0.0f;
	color[2] = 0.0f;
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	tempVec4[0] = g_a_milBase.pos[0];
	tempVec4[1] = 0.0f;
	tempVec4[2] = g_a_milBase.pos[2];
	tempVec4[3] = 1.0f;
	mmTransformVec(mCameraMat, tempVec4); //we only want to transform the translation not the model. use model mat from earlier which is mapScaleMat x CameraTranslateMat
	mmTranslateMatrix(mTranslateMat, tempVec4[0], 0.0f, tempVec4[2]);  
	mmMultiplyMatrix4x4(mCameraRotMat, mTranslateMat, mModelToCameraMat);
	glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelToCameraMat);
	glDrawArrays(GL_TRIANGLES, g_gui_map.lineSymbolOffsets[2], g_gui_map.lineSymbolNumVerts[2]); //offset to flag in VBO

	//Draw a big gray area on the right side using the square symbol
	color[0] = 0.6210f;
	color[1] = 0.6210f;
	color[2] = 0.6210f;
	glUniform3fv(g_gui_shaders.uniforms[1], 1, color);
	mScaleMat[0] = 25.6f;	//scale x so the square will be a rectangle 256 px wide
	mScaleMat[5] = 1.0f;
	mScaleMat[10] = 76.8f;  //scale z so that the square will be a rectangle 768 px high (the full screen height)
	//center of gray area is at 512 - 0.5*256 ;where 512=dist to left-edge of gray area from screen center, 256=width of gray area
	mmTranslateMatrix(mTranslateMat, 384.0f, 0.0f ,0.0f);
	mmMultiplyMatrix4x4(mTranslateMat, mScaleMat, mModelMat);
	mmMultiplyMatrix4x4(mCameraRotMat, mModelMat, mModelToCameraMat);
	glUniformMatrix4fv(g_gui_shaders.uniforms[2], 1, GL_FALSE, mModelToCameraMat);
	glDrawArrays(GL_TRIANGLES, g_gui_map.lineSymbolOffsets[0], g_gui_map.lineSymbolNumVerts[0]);

	//TODO: Finish text function
	//Draw some test text
	color[0] = 0.0f;
	color[1] = 0.0f;
	color[2] = 0.0f;
	glUseProgram(g_textShader.program);
	glBindVertexArray(g_textModel.vao);
	glBindTexture(GL_TEXTURE_2D, g_textModel.texture_id);
	tempVec4[0] = 0.0f;
	tempVec4[1] = 0.0f;
	
	tempVec4[0] = g_a_milBase.pos[0];
	tempVec4[1] = 0.0f;
	tempVec4[2] = g_a_milBase.pos[2];
	tempVec4[3] = 1.0f;
	mmTransformVec(mCameraMat, tempVec4);
	tempVec4[2] -= 30.0f;
	//now we have to switch the z coord around because DrawText prints on the x,y plane.
	tempVec4[1] = -1.0f*tempVec4[2];
	tempVec4[2] = 0.0f; //clear this to prevent any confusion, since it was just a scratch var.

	DrawText("Base 201", 
			tempVec4, //param is vec2, only use first 2 elements.
			color);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glEnable(GL_DEPTH_TEST);
}

static int InitTextVBO(struct simple_model_struct * texInfo)
{
	float * vertData = 0;
	image_t tgaFile;
	float boxwidth=16.0f;
	float uvwidth;
	int r;

	memset(texInfo, 0, sizeof(struct simple_model_struct));

	vertData = (float*)malloc(6*5*sizeof(float)); //3 pos + 2 uvcoords = 5 floats per vert.
	if(vertData == 0)
	{
		printf("%s: malloc fail.\n", __func__);
		return 0;
	}
	memset(vertData, 0, 6*5*sizeof(float));

	uvwidth = 0.0625f; //16/256 = 0.0625, texture size is 256 px, and each character

	//1st triangle 
	//0 origin
	vertData[0] = 0.0f;
	vertData[1] = 0.0f;
	vertData[3] = 0.0f;
	vertData[4] = 0.0f;

	//1 +x
	vertData[5] = boxwidth;
	vertData[6] = 0.0f;
	vertData[8] = uvwidth;
	vertData[9] = 0.0f;

	//2 +x,+y
	vertData[10] = boxwidth;
	vertData[11] = boxwidth;
	vertData[13] = uvwidth;
	vertData[14] = uvwidth;

	//2nd triangle
	//0 origin
	vertData[15] = 0.0f;
	vertData[16] = 0.0f;
	vertData[18] = 0.0f;
	vertData[19] = 0.0f;

	//2 +x,+y
	vertData[20] = boxwidth;
	vertData[21] = boxwidth;
	vertData[23] = uvwidth;
	vertData[24] = uvwidth;

	//3 +y
	vertData[25] = 0.0f;
	vertData[26] = boxwidth;
	vertData[28] = 0.0f;
	vertData[29] = uvwidth;

	texInfo->num_verts = 6;

	//Setup the VBO
	glGenBuffers(1, &(texInfo->vbo));
	glBindBuffer(GL_ARRAY_BUFFER, texInfo->vbo);
	glBufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(6*5*sizeof(float)),	//5 floats per vert.
			vertData,
			GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Setup the VAO
	glGenVertexArrays(1, &(texInfo->vao));
	glBindVertexArray(texInfo->vao);
	glBindBuffer(GL_ARRAY_BUFFER, texInfo->vbo);
	glEnableVertexAttribArray(0); //vertex pos
	glEnableVertexAttribArray(1); //texture coord
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (GLvoid*)(3*sizeof(float)));
	glBindVertexArray(0);

	//Setup the texture
	r = LoadTga("./resources/textures/canvas1_alpha.tga", &tgaFile);
	if(r == 0)
		return 0;

	glGenTextures(1, &(texInfo->texture_id));
	glBindTexture(GL_TEXTURE_2D, texInfo->texture_id);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	//make sure the texture file has only 1 component
	if(tgaFile.info.components != 1)
	{
		printf("%s: error. tga file has %d components, expected 1 component.\n", __func__, tgaFile.info.components);
		free(tgaFile.data);
		return 0;		
	}

	glTexImage2D(GL_TEXTURE_2D,
			0,	//level. 0 is base level.
			GL_RED,	//internal texture format.
			tgaFile.info.width,
			tgaFile.info.height,
			0,	//width of border.
			GL_RED,	//specify the format of the data in the TGA file.
			GL_UNSIGNED_BYTE,	//specify the data type of texture data in the tga file.
			tgaFile.data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);

	free(tgaFile.data);
	free(vertData);
	return 1;
}

/*
This function is meant to be run as an auxiliary function inside a main draw routine.
-This function is setup to draw onto the x,y plane and doesn't need the camera rotation to get the VBOs
to face down the -z axis.
It needs:
	startPos2 - vec2 of pos for the bottom-left corner of the first character.
-Assumes that the text shader, text VAO and text textureId have already been selected.
*/
static void DrawText(char * printString, float * startPos2, float * color3)
{
	float mTranslate[16];
	float charPos[2];
	float uvcoords[2];
	float ftextAdvance[2] = {9.0f, -16.0f}; //0=advance in x direction, 1=advance in y direction
	int i;

	glUniform3fv(g_textShader.uniforms[1], 1, color3);

	//loop through all chars in the string
	charPos[0] = startPos2[0];
	charPos[1] = startPos2[1];
	for(i = 0; i < strlen(printString); i++)
	{
		if(!(printString[i] == 32 || printString[i] == 10)) //if this isn't a space or newline then draw the char
		{
			mmTranslateMatrix(mTranslate, charPos[0], charPos[1], -1.0f);
			glUniformMatrix4fv(g_textShader.uniforms[2], 1, GL_FALSE, mTranslate);
			TextGetUVOffset(printString[i], uvcoords);
			glUniform2fv(g_textShader.uniforms[3], 1, uvcoords);
			glDrawArrays(GL_TRIANGLES, 0, g_textModel.num_verts);
		}

		//advance the character position
		if(printString[i] == 10)
		{
			charPos[1] += ftextAdvance[1];
			charPos[0] = startPos2[0];
		}
		else
		{
			charPos[0] += ftextAdvance[0];
		}
	}
}

/*
This function given a character calculates the uv coordinate offset.
-the 256 px square texture has 16 rows and 16 cols of characters
-
*/
static void TextGetUVOffset(char inChar, float * uvCoord)
{
	float uvwidth = 0.0625f;	//16/256 = 0.0625f
	int i_tex[2]; //index of the character in the character texture.

	i_tex[0] = inChar/16;
	i_tex[1] = inChar % 16;

	uvCoord[1] = (15-i_tex[0])*uvwidth;	//row, u coord
	uvCoord[0] = i_tex[1]*uvwidth;		//col, v coord
}

int InitSoldierAI(struct soldier_ai_controller_struct * aiInfo, struct character_struct * soldierToControl)
{
	memset(aiInfo, 0, sizeof(struct soldier_ai_controller_struct));
	aiInfo->controlledSoldier = soldierToControl;

	return 1;
}

//-targetPos is a vec3
int StartSoldierAIPath(struct soldier_ai_controller_struct * aiInfo, float * targetPos)
{
	aiInfo->mapTargetPos[0] = targetPos[0];
	aiInfo->mapTargetPos[1] = targetPos[1];
	aiInfo->mapTargetPos[2] = targetPos[2];

	aiInfo->state = SOLDIER_AI_STATE_MOVETO;
}

void UpdateSoldierAI(struct soldier_ai_controller_struct * aiInfo)
{
	if(aiInfo->ticksTillNextThink > 0)
	{
		aiInfo->ticksTillNextThink -= 1;
		return;
	}

	//no more ticks to wait do something
	switch(aiInfo->state)
	{
	case SOLDIER_AI_STATE_MOVETO:
		HandleSoldierAIMoveToState(aiInfo);
		break;
	default: //also SOLDIER_AI_STATE_STOP
		break;
	}
}

/*
This function updates an AI soldier when in the MoveTo state.
*/
void HandleSoldierAIMoveToState(struct soldier_ai_controller_struct * aiInfo)
{
	struct character_struct * soldier=0;
	float curSoldierDir[3]; //unit vector that points where the soldier is facing
	float tempTorque[3];
	float pathDir[3];
	float invMomentOfInertia = 0.1f; //1/momentOfInertia,
	float fmag;
	float fangleAdjust;
	float dot;

	soldier = aiInfo->controlledSoldier;

	//calculate a vector that points where the soldier is facing
	curSoldierDir[2] = cosf(soldier->rotY*RATIO_DEGTORAD);
	curSoldierDir[1] = 0.0f; //soldier only has a rotation about the Y-axis so zero y coord.
	curSoldierDir[0] = sinf(soldier->rotY*RATIO_DEGTORAD);

	//calculate a vector pointing towards where we want to go
	vSubtract(pathDir, aiInfo->mapTargetPos, soldier->pos);
	pathDir[0] = aiInfo->mapTargetPos[0] - soldier->pos[0];
	pathDir[1] = 0.0f;
	pathDir[2] = aiInfo->mapTargetPos[2] - soldier->pos[2];
	fmag = vMagnitude(pathDir);
	if(fmag < 2.0f) //don't continue if we are basically already on the point. This stops early
	{
		//set the state to stop since we have arrived at the destination
		aiInfo->state = SOLDIER_AI_STATE_STOP;
		return;
	}
	vNormalize(pathDir);

	//check if the soldier is pointing away from the target, if it is apply
	//a large torque to swing the soldier to point at the target.
	dot = vDotProduct(curSoldierDir, pathDir);
	if(dot < 0.0f)
	{
		tempTorque[1] = 1.57f; //pi/2 ~ 1.57f
	}
	else
	{
		vCrossProduct(tempTorque, curSoldierDir, pathDir);
	}

	//a soldier can only face by rotating around the y-axis so only take the y-coord
	fangleAdjust = tempTorque[1]*invMomentOfInertia*RATIO_RADTODEG;
	soldier->rotY += fangleAdjust;

	if(soldier->rotY > 360.0f)
		soldier->rotY -= 360.0f;

	//set the walking flag so the soldier moves towards the objective
	soldier->events |= CHARACTER_FLAGS_WALK_FWD;
}

int InitCamera(struct camera_info_struct * p_camera)
{
	p_camera->playerCameraMode = PLAYERCAMERA_FP;

	//Initialize a transform that converts from the vehicle coordinate system (where forward is down the +z axis
	//to the camera system where forward is down the -z axis)
	mmRotateAboutY(p_camera->vehicleToCameraCoordTransform, 180.0f);

	return 1;
}
