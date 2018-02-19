/*
This file holds structures for the bush model and
the external function for loading a bush model
*/

struct bush_model_struct
{
	float * p_leaf_vertex_data;
	int num_leaf_verts;
	int * p_leaf_elements;
	int num_leaf_elements;
};

//struct character_model_file_struct
//{
//	float ** p_vert_data;
//	int * num_verts;
//	int ** p_elements;
//	int * num_elements;
//	int num_mats;
//};

/*
Loads model from obj file.
	object_name - name of object in .obj file to get data for
*/
int Load_Bush_Model(struct bush_model_struct * pbush, char * object_name, char * filename);

int Scale_Bush_Model(struct bush_model_struct * bush, float fscale);
float Bush_Find_Bottom(struct bush_model_struct * bush);
