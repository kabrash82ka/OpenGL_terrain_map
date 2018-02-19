#ifndef MY_COLLISION_H
#define MY_COLLISION_H

struct face_struct
{
	float normal[3];
	int i_vertices[4]; //indices of the vertices that make up the face.
	int num_verts;	//could be 3 or 4
};

struct edge_struct
{
	float normal[3];	//unit vector that is cross product of faces 
	int i_vertices[2]; //indices of the two vertices that make up the face.
	int i_face[2];  //adjacent faces index
};

struct box_collision_struct
{
	float * positions;	//array of vec3's (use array from no_tex_model_struct)
	struct face_struct * faces;
	struct edge_struct * edges;
	int num_pos;	//# of pos vertices
	int num_faces;
	int num_edges;
};

#endif
