#ifndef MY_CHARACTER_H
#define MY_CHARACTER_H

#include <load_collada_4.h>
#include <my_item.h>

/*
These are animation state flags that define
the animation being played
*/
struct character_anim_flags_struct
{
	char position;
	char speed;
	char hands;
	char aim;
};
/*These are flags for position anim state:*/
#define ANIMSTATE_STAND 	1
#define ANIMSTATE_CROUCH 	2
#define ANIMSTATE_CRAWL		3
#define ANIMSTATE_VEHICLE	4
/*These are flags for speed anim state:*/
#define ANIMSTATE_SLOW		1
#define ANIMSTATE_RUN		2
/*These flags are for hands anim state:*/
#define ANIMSTATE_EMPTY		1
#define ANIMSTATE_PISTOL	2
#define ANIMSTATE_RIFLE		3
/*These flags are for the aim anim state:*/
#define ANIMSTATE_NOAIM	0
#define ANIMSTATE_AIM		1

struct character_animation_struct
{
	int framesBtwnKeyframes;	//# frames between the current keyframes
	int curFrame;				//current frame. controls interpolation btwn prev-keyframe and next-keyframe. gets decremented each time step. 0 means use next keyframe completely. max means use prev keyframe completely.
	int curAnim;				//index of current animation in dae_animation_struct array
	int iPrevKeyframe;			//index into keyframe times to the previous keyframe
	int idNextAnim;				//index of next animation
	int iNextKeyframe;			//index into keyframe times to the next keyframe
	struct character_anim_flags_struct delayedAddFlags;	//This flags will get added to character flags at start of next animation (match flags in character_struct)
	char maskDelayedAddFlags;	//mask for which delayed flags to add. bit for each char in character_anim_flags_struct. bit 0=position, etc.
	int animToAddDelayedFlags;		//animation id, that curAnim needs to be transitioned to for delayedFlags to be added
	char curAnimFlags;
	char nextAnimFlags;
	struct character_anim_flags_struct stateFlags;	//these flags define the animation
};

#define ANIM_FLAGS_REPEAT 				1
#define ANIM_FLAGS_REVERSE 				2
#define ANIM_FLAGS_FREEZE 				4
#define ANIM_FLAGS_SET_DELAYEDADDFLAGS	8
//if DELAYEDADDFLAGS is set, then at the start of the next animation
//the flags in delayedAddFlags will be added to character flags.

/*
This structure is an improved model structure for
use by draw scene for bone animation.
*/
struct character_model_struct
{
	int num_materials; //this could be # of meshes really. a model is made up of 1 or more meshes.
	int num_verts;
	float * vert_data;	//array of vec4, need this because Bone Animation will update
	float * new_vert_data;	//array of vec3, this is a copy of vert_data that is overwrriten as part of bone animation.
	GLuint vbo;
	GLuint vao; 
	GLuint ebo;
	GLuint * textureIds;
	int * baseVertices;
	int * num_indices;
	GLuint sampler;
	float walk_speed;
	float crouch_speed;
	float crawl_speed;
	float run_speed;
	float model_origin_foot_bias; //This is the distance from the model's origin to the bottom of the foot. The model needs to be translated to take this distance into account when drawing the scene.
	float model_origin_to_seat_bottom; //This is a distance on Y axis from the model's origin to the bottom of the okole when the character is seated.
	struct dae_model_bones_struct testBones; //TODO: This is a test struct. Need to find a home for it.
	struct dae_animation_struct testAnim[23];    //TODO: This is also just a test struct. Need to find final home. Each element corresponds to a CHARACTER_ANIM_? value
	float * final_bone_mat_array; //array of mat4's, one for each bone. This is used by UpdateCharacterBoneModel() to hold the final transforms for each bone.
};

struct vehicle_physics_struct2;

/*This struct is for representing a unique character.*/
struct character_struct
{
	struct character_model_struct * p_common;
	struct vehicle_physics_struct2 * p_vehicle;	//pointer to the vehicle that the character is in
	struct item_inventory_struct * p_inventory;
	struct item_struct * p_actionSlots[2];
	float pos[3];
	float vel[3];
	float accel[3];
	float walk_vel[3];
	float rotY; //rotation in degrees around the Y axis.
	float biasY; //translate the model in the Y axis by this amount.
	unsigned int flags;		//these are broad state flags of the character
	unsigned int events;	//these events control flags
	struct character_animation_struct anim;
};

//These are flags for the character_struct flags element and the events element:
//I need these for events, but I don't want the flags other than the movement ones
//to set the state of the character
#define CHARACTER_FLAGS_WALK_FWD	2
#define CHARACTER_FLAGS_WALK_BACK	4
#define CHARACTER_FLAGS_WALK_LEFT	8
#define CHARACTER_FLAGS_WALK_RIGHT	16
#define CHARACTER_FLAGS_IDLE		32
#define CHARACTER_FLAGS_ON_GROUND	64
#define CHARACTER_FLAGS_IN_VEHICLE	128
#define CHARACTER_FLAGS_AIM			256
#define CHARACTER_FLAGS_ARM_RIFLE	512
#define CHARACTER_FLAGS_ARM_PISTOL	1024
#define CHARACTER_FLAGS_STAND		2048
#define CHARACTER_FLAGS_CROUCH		4096
#define CHARACTER_FLAGS_PRONE		8192
#define CHARACTER_FLAGS_RUN			16384

//These constants are for the current_anim index
//TODO: CHARACTER_ANIM_VEHICLE is not in out_anim.dat. Need to add it.
#define CHARACTER_ANIM_WALK     		0
#define CHARACTER_ANIM_STAND    		1
#define CHARACTER_ANIM_STANDRIFLE		2
#define CHARACTER_ANIM_WALKRIFLE		3
#define CHARACTER_ANIM_STAND_AIMRIFLE	4
#define CHARACTER_ANIM_STAND_AIMPISTOL	5
#define CHARACTER_ANIM_WALK_AIMPISTOL	6
#define CHARACTER_ANIM_RUN				7
#define CHARACTER_ANIM_CROUCHWALK		8
#define CHARACTER_ANIM_CROUCHWALK_RIFLE	9
#define CHARACTER_ANIM_CROUCHWALK_AIMPISTOL 	10
#define CHARACTER_ANIM_CROUCH_STOP				11
#define CHARACTER_ANIM_CROUCH_STOP_RIFLE		12
#define CHARACTER_ANIM_CROUCH_STOP_AIMRIFLE		13
#define CHARACTER_ANIM_CROUCH_STOP_AIMPISTOL	14
#define CHARACTER_ANIM_CRAWLSTOP				15
#define CHARACTER_ANIM_CRAWLSTOP_RIFLE			16
#define CHARACTER_ANIM_CRAWLSTOP_AIMRIFLE		17
#define CHARACTER_ANIM_CRAWLSTOP_AIMPISTOL		18
#define CHARACTER_ANIM_CRAWL					19
#define CHARACTER_ANIM_CRAWL_RIFLE				20
#define CHARACTER_ANIM_CRAWL_AIMPISTOL			21
#define CHARACTER_ANIM_VEHICLE  				22

struct character_shader_struct
{
	GLuint shaderList[2];
	GLuint program;
	GLuint perspectiveMatrixUnif;
	GLuint modelToCameraMatrixUnif;
	GLuint lightDirUnif;
	GLuint modelSpaceCameraPosUnif;
	GLuint colorTextureUnif;
	int    colorTexUnit;
};

/*This struct holds the list of all current characters*/
struct character_list_struct
{
	struct character_struct ** ptrsToCharacters; //array of ptrs to characters
	int num_soldiers; //# of characters that exist in ptrsToCharacters
	int max_soldiers; //# of total ptrs in ptrsToCharacters
};

struct soldier_ai_controller_struct
{
	struct character_struct * controlledSoldier;	//addr of the soldier currently under control
	float mapTargetPos[3]; //vec3 of some target position on the map
	float angVelY; //angular velocity in the y-axis
	char state;
	char ticksTillNextThink;
};
/*These constants are for the state variable.*/
#define SOLDIER_AI_STATE_STOP   0
#define SOLDIER_AI_STATE_MOVETO 1

#endif
