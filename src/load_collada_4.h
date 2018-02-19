#ifndef LOAD_COLLADA_H
#define LOAD_COLLADA_H

struct dae_model_info
{
	int * vert_indices;
	float * vert_data;
	char vertex_data_format[3]; /*eg: 'PNT' or 'PN'. represents order of position/normals/texcord.*/
	int num_verts;
	int num_indices;
	int floats_per_vert;
};

struct dae_model_info2
{
	int * vert_indices;
	float * vert_data;
	char vertex_data_format[3];
	int num_verts;
	int num_indices;
	int floats_per_vert;
	int num_materials; //really # of meshes
	int * base_index_offsets; //index in the vert_indices where the individual materials start.
	int * mesh_counts; //number of vertices in each mesh
};

/*
a new struct to hold the dae <source> element arrays
*/
struct dae_model_vert_data_struct
{
	float * positions;
	float * normals;
	float * texcoords;
	int num_positions;
	int num_normals;
	int num_texcoords;
};

struct dae_polylists_struct
{
	int ** polylist_indices;	//array of arrays of vertex indices from the <p> element
	int * polylist_len;			//array of lengths of each polylist, this is length based on .dae file so it includes an extra vertex attribue (COLOR) so there are 4 pieces of data per vert instead of the normal 3 (POS,NORMAL,TEXCOORD)
	int num_polylists;
};

struct dae_texture_names_struct
{
	char ** names;	//array of strings with texture names
	int num_textures;
};

struct dae_bone_tree_leaf;
struct dae_bone_tree_leaf
{
	struct dae_bone_tree_leaf * parent;
	int num_children;
	struct dae_bone_tree_leaf ** children;//array
};

struct dae_model_bones_struct
{
	float * temp_bone_mat_array; //array of mat4's that is a scratchpad for calculating bone transform.
	float * inverse_bind_mat4_array; //array of mat4's. one for each bone.
	float * bone_transform_mat4_array; //[UNUSED}TODO: Check if this array is actually used for anything old:array of mat4's. one for each bone.
	float * weight_array;//array of scalars. set of weights for each vertex. each vertex has a weight from every bone.
	struct dae_bone_tree_leaf * bone_tree;
	int num_bones;
	int num_pos;	//TODO: This field isn't used anywhere.
	int num_verts;	//one index of the weight array
	float bind_shape_mat4[16];
	float armature_mat4[16];
};

struct dae_animation_struct
{
	float * bone_frame_transform_mat4_array; //2d array of [bone][transform] mat4's
	int * key_frame_times;
	int num_frames;
};

struct dae_keyframe_info_struct
{
	int	i_master_keyframe;	//index of the keyframe in the bone transforms
	int time;				//time that keyframe occurs
};

struct dae_extra_anim_info_struct
{
	int num_animations;		//(KEEP)
	int total_keyframes;	//(KEEP) total keyframes from all animations
	int * keyframe_list;	//(don't need) this is really keyframe times
	int * anim_id_list;		//(don't need) says which anim index each keyframe belongs to
	int * num_keyframes;	//(KEEP) array [anim] gives total keyframes in each animation.
	struct dae_keyframe_info_struct ** anim_keyframes;  //(KEEP) 2d array [anim][keyframe_index] of dae_keyframe_info_structs
};

int Load_DAE_Model_Vertices(char * filename, struct dae_model_info * p_model_info);
int Load_DAE_Model_Vertices2(char * filename, struct dae_model_info * p_model_info);
int Load_DAE_Model_Vertices3(FILE* pFile, struct dae_model_vert_data_struct * vert_data);
int Load_DAE_BindShapeMat(char * filename, float * out_bind_shape_mat4);
int Load_DAE_InverseBindMatrixArray(char * filename, int num_bones, float * p_inv_bind_mat_array);
int Load_DAE_GetNumBones(char * filename);
int Load_DAE_JointMatArray(char * filename, int num_bones, float * p_joint_mats);
int Load_DAE_weight_array(char * filename, int num_pos, int num_bones, float * weight_array);
int Load_DAE_get_min_max_vcount(char * filename, int * min, int * max);
int Load_DAE_GetArmatureMat(char * filename, float * mat4);
int Load_DAE_GetBoneHierarchy(char * filename, int num_bones, struct dae_bone_tree_leaf * tree_root);
int Load_DAE_GetNumAnimationFrames(char * filename);
int Load_DAE_GetAnimationTransforms(char * filename, int num_bones, int num_frames, struct dae_animation_struct * anim_info);
int Load_DAE_BinaryGetNumAnims(char * filename, int * num_anims);
int Load_DAE_Bones(char * filename, int num_pos, struct dae_polylists_struct * polylists, int num_inputs, struct dae_model_bones_struct * model_info);
int Load_DAE_Animation(char * filename, int num_bones, struct dae_animation_struct * anim_info);
int Load_DAE_GetBoneNames(FILE * pFile, int num_bones, char ** bone_name_array);
void Load_DAE_Convert_Matrix_Orientations(struct dae_model_bones_struct * bones, struct dae_animation_struct * anim, int num_anims);
int Load_DAE_ExtraAnimInfoFile(char * filename, struct dae_extra_anim_info_struct * info);
int Load_DAE_Polylists(FILE * pFile, struct dae_polylists_struct * polylists);
int Load_DAE_CustomBinaryModel(char * filename, struct dae_model_info2 * model_info);
int Load_DAE_CustomTextureNames(struct dae_texture_names_struct * texture_names);
int Load_DAE_CustomBinaryBones(char * filename, struct dae_model_bones_struct * bones, int num_anims, struct dae_animation_struct * anim);
int DAE_CheckVertsForZeroWeight(struct dae_model_bones_struct * bones);
void Load_DAE_GetTriangleFaceVector(float * a, float * b, float * c, float * normal_out);

#endif
