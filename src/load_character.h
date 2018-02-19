#ifndef LOAD_CHARACTER_H
#define LOAD_CHARACTER_H

/*Structures*/
struct chr_character_file_struct
{
	int num_materials;	//number of mesh materials that make up the character
	float ** p_vert_data;	//An array for each mesh that contains the vertex data (pos, normal, texcoord)
	int * num_vertices;	//an array where each element has the # of vertices for that material.
	int ** p_indices;
	int * num_indices;
};

/*External Functions*/
int Load_Character_Model(char * filename, struct chr_character_file_struct * p_file_stats);
int Load_Character_Scale(struct chr_character_file_struct * file_stats, float fscale);
#endif
