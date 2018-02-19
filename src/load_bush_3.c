/*
This file holds functions for loading the bush model
from an obj file.

This version assumes there is one 'o' line in the obj file.

*/
#include "load_bush_3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
This is a local structure that holds information
about the file for loading.
*/
struct obj_file_count_struct
{
	int num_leaf_verts;
	int num_verts;
	int num_normals;
	int num_texcoords;
	float * pPos;
	float * pNormal;
	float * pTexcoord;
};

/*Local function prototypes*/
static int Count_Vertices(FILE* pFile, struct obj_file_count_struct * pCounts, char * object_name, long * lobject_fpos);
static int Load_VertexData(FILE* pFile, struct obj_file_count_struct * pCounts, long lobj_fpos);
static int Load_Triangles(FILE* pFile, struct obj_file_count_struct * pCounts, struct bush_model_struct * pbush, char * object_name, long lobj_fpos);

extern int Load_Bush_Model(struct bush_model_struct * pbush, char * object_name, char * filename)
{
	FILE* pFile;
	long lobj_fpos = 0;
	int r;
	struct obj_file_count_struct obj_stats;
	
	pFile = 0;
	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("Load_Bush_Model: failed to open obj file.\n");
		return 0;
	}

	r = Count_Vertices(pFile, &obj_stats, object_name, &lobj_fpos);
	if(r == 0)
	{
		printf("Load_Bush_Model: error. Count_Vertices() failed.\n");
		fclose(pFile);
		return 0;
	}
	
	//allocate memory for the vertex positions, normals, and texture coordinates
	obj_stats.pPos = (float*)malloc(sizeof(float)*3*obj_stats.num_verts);
	if(obj_stats.pPos == 0)
	{
		printf("Load_Bush_Model: error. malloc() failed for vertex positions.\n");
		fclose(pFile);
		return 0;
	}
	obj_stats.pNormal = (float*)malloc(sizeof(float)*3*obj_stats.num_normals);
	if(obj_stats.pNormal == 0)
	{
		printf("Load_Bush_Model: error. malloc() failed for normal positions.\n");
		fclose(pFile);
		return 0;
	}
	obj_stats.pTexcoord = (float*)malloc(sizeof(float)*2*obj_stats.num_texcoords);
	if(obj_stats.pTexcoord == 0)
	{
		printf("Load_Bush_Model: error. malloc() failed for tex coordinates.\n");
		fclose(pFile);
		return 0;
	}
	r = Load_VertexData(pFile, &obj_stats, lobj_fpos);
	if(r == 0)
	{
		printf("Load_Bush_Model: error. Load_VertexData() failed.\n");
		fclose(pFile);
		return 0;
	}
	//save obj_stats information in pbush
	pbush->num_leaf_verts = obj_stats.num_leaf_verts;
	pbush->num_leaf_elements = obj_stats.num_leaf_verts; //TODO: take advantage of indexing to reduce duplicate vertices, but for now each vertex has a unique index
	
	//allocate memory for the VBO & IBO.
	pbush->p_leaf_vertex_data = (float*)malloc(sizeof(float)*8*obj_stats.num_leaf_verts);
	if(pbush->p_leaf_vertex_data == 0)
	{
		printf("Load_Bush_Model: error. malloc() for leaf VBO failed.\n");
		fclose(pFile);
		return 0;
	}
	pbush->p_leaf_elements = (int*)malloc(sizeof(int)*obj_stats.num_leaf_verts);
	if(pbush->p_leaf_elements == 0)
	{
		printf("Load_Bush_Model: error. malloc() for leaf elements failed.\n");
		fclose(pFile);
		return 0;
	}
	
	r = Load_Triangles(pFile, &obj_stats, pbush, object_name, lobj_fpos);
	if(r == 0)
	{
		printf("Load_Bush_Model: error. Load_Triangles() failed.\n");
		fclose(pFile);
		return 0;
	}
	
	printf("Load_Bush_Model: errno=%d\n", errno);
	fclose(pFile);
	return 1;
}

/*
This function counts vertices for objects that
have the name like Cylinder.114_Cylinder. It returns 1 if success
or 0 if it failed.
*/
static int Count_Vertices(FILE* pFile, struct obj_file_count_struct * pCounts, char * object_name, long * lobject_fpos)
{
	char line[255];
	char c = 0;
	int i;
	int num_leaf_verts = 0; //this is based on 'f' lines
	int num_file_verts = 0; //number of verts total based on 'v' lines (ignores what object they belong to)
	int num_file_normals = 0; //number of normals total based on 'vn' lines
	int num_file_texcoords = 0; //number of textures coordinates based on 'vt' lines
	int line_num = 1;
	int num_leaf_objects = 0;
	int is_counting_leaf_verts = 0; //flag that when set to 1, counts all 'v' lines towards the vertex sum.
	int is_end_of_file = 0;
	int is_object_name_found = 0;
	
	//loop till end of file
	while(feof(pFile) == 0)
	{
		i = 0;
		//get a line
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			
			//check for end of file:
			if(feof(pFile) != 0)
			{
				is_end_of_file = 1;
				break;
			}
			
			//check the index to make sure it does not go past
			//the end of the array.
			if(i == 254)
			{
				printf("Count_Vertices: error line %d too long\n", line_num);
				return 0;
			}
			
			line[i] = c;
			i += 1;
		}
		
		//do a check for end of file before continuing:
		if(is_end_of_file)
			break;
		
		line_num += 1;
		c = 0; //clear c so we can enter the loop that reads the line again
		line[i] = 0;//replace the '\n' with a null termination
		
		//look at the first letter of the line to tell what to do
		if(line[0] == '#')
			continue; //this line is a comment so skip it.
		
		//find a line that starts with 'o'
		if(line[0] == 'o')
		{
			//printf("\n+");
			//determine if this line is for 'bark'
			if(strstr(line, object_name) != 0)
			{
				is_counting_leaf_verts = 1;
				is_object_name_found = 1;
				num_leaf_objects += 1;

				//save the file position to aid parsing of vertex data
				*lobject_fpos = ftell(pFile);
			}
			else
			{
				//if we already found the object we were looking for and
				//we come across a new 'o' line, just stop
				if(is_object_name_found == 1)
					break;

				is_counting_leaf_verts = 0;
				printf("unaccounted for line at %d\n", line_num);
			}
		}
		
		//if the line starts with 'f' count it
		if(line[0] == 'f' && line[1] == ' ')
		{
			if(is_counting_leaf_verts == 1)
			{
				num_leaf_verts += 3;
				//printf(".");
			}
			else
			{
				//take out this print because it just spams
				//printf("Count_Vertices: error. found 'v' line without 'o' line.\n");
			}
		}
	
		//count all 'v', 'vn', 'vt' lines here because the indices of the 'f' lines
		//are over the whole file and are not restricted to the objects	
		if(line[0] == 'v' && line[1] == ' ')
		{
			num_file_verts += 1;
		}
		
		if(line[0] == 'v' && line[1] == 't')
		{
			num_file_texcoords += 1;
		}
		
		if(line[0] == 'v' && line[1] == 'n')
		{
			num_file_normals += 1;
		}
	}
	printf("end of file found.\n");
	printf("number of leaf verts: %d\nnumber of leaf objects %d\n", num_leaf_verts, num_leaf_objects);
	printf("number of 'v' lines: %d\n", num_file_verts);
	printf("number of 'vn' lines: %d\n", num_file_normals);
	printf("number of 'vt' lines: %d\n", num_file_texcoords);

	pCounts->num_leaf_verts = num_leaf_verts;
	pCounts->num_verts = num_file_verts;
	pCounts->num_normals = num_file_normals;
	pCounts->num_texcoords = num_file_texcoords;
	
	//restore the file pointer to the beginning of the file
	fseek(pFile, 0, SEEK_SET);

	return 1;
}

/*
This functions loads data from a file and places it in already initialized arrays
for vertex position, normals, texcoords in an obj_file_count_struct.
*/
static int Load_VertexData(FILE* pFile, struct obj_file_count_struct * pCounts, long lobj_fpos)
{
	char line[255];
	char c = 0;
	int i;
	int line_num = 1;
	int is_end_of_file = 0;
	int pos_index = 0; //index for the vertex position array
	int norm_index = 0; //index for the vertex normal vector array
	int tex_index = 0; //index for the vertex texture coordinate array
	int i_verts = 0;
	int i_normals = 0;
	int i_texcoords = 0;
	float temp_float[3];
	float temp_c[3]; //temporary string used when parsing a line with vertex information

	while(feof(pFile) == 0)
	{
		i = 0; //reset line index
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			if(feof(pFile) != 0)
			{
				is_end_of_file = 1;
				break;
			}
			
			//check to make sure we don't go past the end of the line array
			if(i == 254)
			{
				printf("Load_VertexData: error. line %d too long.\n", line_num);
				return 0;
			}
			line[i] = c;
			i += 1;
		}
		
		if(is_end_of_file)
			break;
		
		line_num += 1;
		c = 0;
		line[(i-1)] = 0; //replace '\n' with a null-termination
		
		//look at first letter in line to figure out what to do
		if(line[0] == '#')
			continue;
		
		if(line[0] == 'v' && line[1] == ' ')
		{
			if((pos_index+2) >= (pCounts->num_verts*3))
			{
				printf("Load_VertexData: error. pos_index too large.\n");
				return 0;
			}
			sscanf(line, "%c %f %f %f", temp_c, temp_float, (temp_float + 1), (temp_float + 2));
			
			//convert the vector to opengl's coordinate system
			pCounts->pPos[pos_index] = temp_float[0];
			pCounts->pPos[(pos_index+1)] = temp_float[1]; 
			pCounts->pPos[(pos_index+2)] = temp_float[2];
			pos_index += 3;
			i_verts += 1;
		}
		if(line[0] == 'v' && line[1] == 'n')
		{
			if((norm_index+2) >= (pCounts->num_normals*3))
			{
				printf("Load_VertexData: error. norm_index too large.\n");
				return 0;
			}
			sscanf(line, "%c%c %f %f %f", temp_c, (temp_c+1), temp_float, (temp_float+1), (temp_float+2));

			//convert the vector to opengl's coordinate system
			pCounts->pNormal[norm_index] = temp_float[0];
			pCounts->pNormal[(norm_index+1)] = temp_float[1];
			pCounts->pNormal[(norm_index+2)] = temp_float[2];
			norm_index += 3;
			i_normals += 1;
		}
		if(line[0] == 'v' && line[1] == 't')
		{
			if((tex_index+1) >= (pCounts->num_texcoords*2))
			{
				printf("Load_VertexData: error. tex_index too large.\n");
				return 0;
			}
			sscanf(line, "%c%c %f %f", temp_c, (temp_c+1), temp_float, (temp_float+1));
			pCounts->pTexcoord[tex_index] = temp_float[0];
			pCounts->pTexcoord[(tex_index+1)] = temp_float[1];
			tex_index += 2;
			i_texcoords += 1;
		}

		//check to see if we have fetched all vertex data
		if(i_verts == pCounts->num_verts && i_normals == pCounts->num_normals && i_texcoords == pCounts->num_texcoords)
			break;
	}

	//check that we read all the vertex data lines in the file
	if(i_verts != pCounts->num_verts || i_normals != pCounts->num_normals || i_texcoords != pCounts->num_texcoords)
	{
		printf("Load_VertexData: error. mismatch between data read and counted data:\nexpected(v/n/t)=%d %d %d\nread(v/n/t)=%d %d %d\n", pCounts->num_verts, pCounts->num_normals, pCounts->num_texcoords, i_verts, i_normals, i_texcoords);
		return 0;
	}
	
	//rewind the file pointer to place it back where it was
	fseek(pFile, 0, SEEK_SET);
	
	return 1;
}

/*
This function look at 'f' lines and uses them to fill in the data
to make triangles VBO & IBO.
-assumes that obj file specifies faces with position/texture/normal coordinates
TODO: Fix line number tracking. they are all messed up by the fseek() call in the beginning.
*/
static int Load_Triangles(FILE* pFile, 
		struct obj_file_count_struct * pCounts, 
		struct bush_model_struct * pbush, 
		char * object_name,
		long lobj_fpos)
{
	char c;
	char line[255];
	int i,j;
	int line_num =1;
	int i_fline = 0;
	int num_flines = 0;
	int is_end_of_file = 0;
	int temp_int[3];
	int tri_vert_elements[3];
	int tri_normal_elements[3];
	int tri_texcoord_elements[3];
	char temp_c;
	float * p_temp_pos;
	float * p_temp_normal;
	float * p_temp_texcoord;
	int leaf_data_index = 0; //set on a per float basis
	int bark_data_index = 0; //set on a per float basis (vice per vertex)
	int leaf_element_index = 0;
	int bark_element_index = 0;
	//int load_select = 0; //0 = invalid, 1 = leaf, 2 = bark
	float * vert_buff_dest;
	int * element_buff_dest;
	int swap_i; //temporary index to swap vertex position.

	//calculate number of 'f' lines so we know when to stop
	num_flines = pCounts->num_leaf_verts/3;

	//start after the 'o' of the mesh we are looking for
	fseek(pFile, lobj_fpos, SEEK_SET);

	while(feof(pFile) == 0)
	{
		i = 0; //reset line index
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			if(feof(pFile) != 0)
			{
				is_end_of_file = 1;
				break;
			}
			
			//check to make sure we don't go off the end of the line array
			if(i == 254)
			{
				printf("Load_Triangles: error. line %d too long.\n", line_num);
				return 0;
			}
			line[i] = c;
			i += 1;
			
		}
		if(is_end_of_file)
			break;

		//remove the '\n' and null-terminate the line string
		line[(i-1)] = 0;
		c = 0;
		line_num += 1;
		
		//skip comment lines.
		if(line[0] == '#')
			continue;
		
		//'f' lines have this format in (v/vt/vn), where v = position, vt = texture-coordinate, vn = normal
		if(line[0] == 'f' && line[1] == ' ')
		{
			//check the indices to make sure they are still inside the array
			if(leaf_data_index >= (pCounts->num_leaf_verts*8))
			{
				printf("Load_Triangles: error. leaf_data_index too large:\n\tleaf_data_index=%d\n\tnum_leaf_verts*8=%d\n\tline_num=%d\n", leaf_data_index, (pCounts->num_leaf_verts*8), line_num);
				return 0;
			}
			
			//setup pointers for the copy
			vert_buff_dest = pbush->p_leaf_vertex_data+leaf_data_index;
			element_buff_dest = pbush->p_leaf_elements+leaf_element_index;
			
			sscanf(line,"%c %d/%d/%d %d/%d/%d %d/%d/%d",
				&temp_c,
				tri_vert_elements, tri_texcoord_elements, tri_normal_elements,
				(tri_vert_elements+1), (tri_texcoord_elements+1), (tri_normal_elements+1),
				(tri_vert_elements+2), (tri_texcoord_elements+2), (tri_normal_elements+2));

			//decrement the index from the file because the file index starts at 1
			for(j = 0; j < 3; j++)
			{
				tri_vert_elements[j] -= 1;
				tri_normal_elements[j] -= 1;
				tri_texcoord_elements[j] -= 1;
			}
			
			//use the indices from the file to set up pointers
			for(j = 0; j < 3; j++)
			{ 
				p_temp_pos = (pCounts->pPos+(tri_vert_elements[j]*3));
				p_temp_normal = (pCounts->pNormal+(tri_normal_elements[j]*3));
				p_temp_texcoord = (pCounts->pTexcoord+(tri_texcoord_elements[j]*2));
				
				//copy the vertex data into the buffers
				memcpy((vert_buff_dest+(j*8)), p_temp_pos, (sizeof(float)*3));
				memcpy((vert_buff_dest+3+(j*8)), p_temp_normal, (sizeof(float)*3));
				memcpy((vert_buff_dest+6+(j*8)), p_temp_texcoord, (sizeof(float)*2));
				
				//update the index variables to the new tail of the vertex data
				//update the element buffer
				leaf_data_index += 8;
				element_buff_dest[j] = leaf_element_index;
				leaf_element_index += 1;
			}
			
			//keep track of how many 'f' lines we have processed. and stop once we
			//reach count.
			i_fline += 1;
			if(i_fline >= num_flines)
				break;

		}
		
	}
	
	//update the bush_model_struct with the # of elements in the element arrays
	pbush->num_leaf_elements = pCounts->num_leaf_verts;
	
	fseek(pFile, 0, SEEK_SET);
	return 1;
}

extern int Scale_Bush_Model(struct bush_model_struct * bush, float fscale)
{
	int i;
	float * vert;

	for(i = 0; i < bush->num_leaf_verts; i++)
	{
		vert = bush->p_leaf_vertex_data + (i*8); //8 = 3 pos + 3 normal + 2 texcoord
		vert[0] *= fscale;
		vert[1] *= fscale;
		vert[2] *= fscale;
	}

	return 1;
}

extern float Bush_Find_Bottom(struct bush_model_struct * bush)
{
	int i;
	float low_y;
	float * curVert=0;

	for(i = 0; i < bush->num_leaf_verts; i++)
	{
		curVert = bush->p_leaf_vertex_data + (8*i);

		if(i == 0) //initialize low_y with first vertex
		{
			low_y = curVert[1];
		}
		else
		{
			if(curVert[1] < low_y)
				low_y = curVert[1];
		}
	}

	return low_y;
}
