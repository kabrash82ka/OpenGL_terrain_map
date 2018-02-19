#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "load_character.h"

static int Count_Vertices(FILE *pFile, int * p_num_verts, int * p_num_normals, int * p_num_texcoords, int * p_num_materials);
static int Count_Material_Vertices(FILE * pFile, int num_materials, int * num_verts_per_material);
static int Read_Vertices(FILE * pFile, float * positions, float * normals, float * texcoords);
static int Read_Triangles(FILE * pFile, float * p_pos, float * p_normals, float * p_texcoords, struct chr_character_file_struct * p_file_stats);
static int FindMinMaxInEachAxis(float * positions, int num_pos, float * p_minmaxs);

extern int Load_Character_Model(char * filename, struct chr_character_file_struct * p_file_stats)
{
	int r;
	int i;
	FILE * pFile=0;
	int num_verts=0;
	int num_normals=0;
	int num_texcoords=0;
	int num_materials=0;
	int * num_verts_per_material=0;
	float * temp_pos=0;
	float * temp_normals=0;
	float * temp_texcoords=0;
	float minmaxs[6];
	
	//zero the file structure
	memset(p_file_stats, 0, sizeof(struct chr_character_file_struct));

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("Load_Character_Model: error. fopen() failed for %s\n", filename);
		return 0;
	}
	
	//count the number of 'v','vt' and 'vn' lines in the obj file.
	r = Count_Vertices(pFile, &num_verts, &num_normals, &num_texcoords, &num_materials);
	if(r == 0)
	{
		fclose(pFile);
		return 0;
	}
	//print the counts for debug purposes:
	printf("%s: num_verts=%d\n", filename, num_verts);
	printf("%s: num_normals=%d\n", filename, num_normals);
	printf("%s: num_texcoords=%d\n", filename, num_texcoords);
	printf("%s: num_mats=%d\n", filename, num_materials);
	
	//allocate temporary arrays for vertex position, normals, textures
	temp_pos = (float*)malloc(num_verts*3*sizeof(float));
	if(temp_pos == 0)
	{
		printf("Load_Character_Model: error. malloc() for temp_pos array failed.\n");
		return 0;
	}
	temp_normals = (float*)malloc(num_normals*3*sizeof(float));
	if(temp_normals == 0)
	{
		printf("Load_Character_Model: error. malloc() for temp_normals array failed.\n");
		return 0;
	}
	temp_texcoords = (float*)malloc(num_texcoords*2*sizeof(float));
	if(temp_texcoords == 0)
	{
		printf("Load_Character_Model: error. malloc() for temp_texcoords array failed.\n");
		return 0;
	}
	
	//load the data for 'v', 'vt', 'vn' lines from the obj file
	r = Read_Vertices(pFile, temp_pos, temp_normals, temp_texcoords);
	if(r == 0)
	{
		fclose(pFile);
		return 0;
	}
	
	//Print the min & max position values
	FindMinMaxInEachAxis(temp_pos, num_verts, minmaxs);
	printf("%s: min x=%f\n", filename, minmaxs[0]);
	printf("%s: max x=%f\n", filename, minmaxs[1]);
	printf("%s: min y=%f\n", filename, minmaxs[2]);
	printf("%s: max y=%f\n", filename, minmaxs[3]);
	printf("%s: min z=%f\n", filename, minmaxs[4]);
	printf("%s: max z=%f\n", filename, minmaxs[5]);
	
	//save the number of materials to the file stats
	p_file_stats->num_materials = num_materials;
	
	//allocate memory for the arrays of number of vertices & indices
	p_file_stats->num_vertices = (int*)malloc(num_materials*sizeof(int));
	if(p_file_stats->num_vertices == 0)
	{
		printf("Load_Character_Model: error. malloc() failed for num_vertices array.\n");
		return 0;
	}
	p_file_stats->num_indices = (int*)malloc(num_materials*sizeof(int));
	if(p_file_stats->num_indices == 0)
	{
		printf("Load_Character_Model: error. malloc() failed for num_indices array.\n");
		return 0;
	}
	p_file_stats->p_vert_data = (float**)malloc(num_materials*sizeof(float*));
	p_file_stats->p_indices = (int**)malloc(num_materials*sizeof(int*));

	//Now based on how many materials we have allocate resources
	num_verts_per_material = (int*)malloc(num_materials*sizeof(int));
	if(num_verts_per_material == 0)
	{
		printf("Load_Character_Model: error. malloc() failed for num_verts_per_material array.\n");
		return 0;
	}
	r = Count_Material_Vertices(pFile, num_materials, num_verts_per_material);
	if(r == 0)
	{
		fclose(pFile);
		return 0;
	}
	
	//print the number of verts in each material for debug purposes:
	for(i = 0; i < num_materials; i++)
	{
		printf("material %d: %d verts.\n", i, num_verts_per_material[i]);
	}
	
	//allocate space for the vertices & indices in the material
	for(i = 0; i < num_materials; i++)
	{
		p_file_stats->num_vertices[i] = num_verts_per_material[i];
		p_file_stats->p_vert_data[i] = (float*)malloc(num_verts_per_material[i]*8*sizeof(float)); //3 floats for position + 3 floats for normal vec + 2 floats for texcoord
		if(p_file_stats->p_vert_data[i] == 0)
		{
			printf("Load_Character_Model: error malloc() failed for vertices for material %d\n", i);
			fclose(pFile);
			return 0;
		}
		
		p_file_stats->num_indices[i] = num_verts_per_material[i]; //TODO: This makes have an EBO pointless. Need to change it so it handles duplicate vertices.
		p_file_stats->p_indices[i] = (int*)malloc(num_verts_per_material[i]*sizeof(int));
		if(p_file_stats->p_indices[i] == 0)
		{
			printf("Load_Character_Model: error malloc() failed for indices for material %d\n", i);
			fclose(pFile);
			return 0;
		}
	}
	
	r = Read_Triangles(pFile, temp_pos, temp_normals, temp_texcoords, p_file_stats);
	if(r == 0)
	{
		fclose(pFile);
		return 0;
	}
	
	fclose(pFile);
	free(temp_pos);
	free(temp_normals);
	free(temp_texcoords);
	
	//success
	return 1;
}

/*
Count_Vertices() counts the number of lines starting with 'v', 'vn','vt' and materials in an obj file.
*/
static int Count_Vertices(FILE *pFile, int * p_num_verts, int * p_num_normals, int * p_num_texcoords, int * p_num_materials)
{
	int i;
	int line_num = 1;
	char c = 0; //c needs to be initialized to 0 to enter a while loop.
	char line[255];
	
	//clear the counts for vertices, normals and texture coordinates.
	*p_num_verts = 0;
	*p_num_normals = 0;
	*p_num_texcoords = 0;
	*p_num_materials = 0;

	//loop till the end of the file
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
				break;
			}
			
			//check the index to make sure it does not go past the end of the line array.
			if(i == 254)
			{
				printf("Count_Vertices: error line %d too long.\n", line_num);
				return 0;
			}
			line[i] = c;
			i += 1;
		}
		
		//do a check for the end of file before continuing:
		//(if it was set before then it's still going to be set now.
		if(feof(pFile) != 0)
			break;
		
		line[i] = 0; //replace the '\n' with a null termination.
		c = 0; //clear c so we can enter the line-reading loop again.
		
		//look at the first letter of the line to tell what to do
		
		if(line[0] == '#')
			continue; //lines starting with '#' are comments so skip them.
		
		if(line[0] == 'v' && line[1] == ' ')
		{
			*p_num_verts += 1;
		}
		if(line[0] == 'v' && line[1] == 'n')
		{
			*p_num_normals += 1;
		}
		if(line[0] == 'v' && line[1] == 't')
		{
			*p_num_texcoords += 1;
		}
		if(strncmp(line, "usemtl", 6) == 0)
		{
			*p_num_materials += 1;
		}
	}
	
	//restore the file pointer to the beginning of the file
	fseek(pFile, 0, SEEK_SET);
	
	return 1;
}

/*Count_Material_Vertices()*/
static int Count_Material_Vertices(FILE * pFile, int num_materials, int * num_verts_per_material)
{
	int r;
	int i;
	char c = 0;
	char line[255];
	int line_num = 1;
	int i_mat=-1;
	int f_count=0; //count of vertices from each f line.
	
	//loop till end of file
	while(feof(pFile) == 0)
	{
		i = 0;
		
		//get a line
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			
			if(feof(pFile) != 0)
				break;
			
			if(i == 254)
			{
				printf("Count_Material_Vertices: error line %d too long.\n", line_num);
				return 0;
			}
			line[i] = c;
			i += 1;
		}
		
		if(feof(pFile) != 0)
			break;
		
		line[i] = 0;
		c = 0;
		if(line[0] == '#')
			continue;
		if(strncmp(line, "usemtl", 6) == 0)
		{
			//when we hit a line starting with 'usemtl' store the count we have already, but not if it
			//is the first 'usemtl'.
			if(i_mat == -1)
			{
				i_mat = 0;
			}
			else 
			{
				num_verts_per_material[i_mat] = f_count;
				f_count = 0; //reset 'f' line count
				i_mat += 1; //increment the i_mat
			}
		}
		if(line[0] == 'f' && line[1] == ' ')
		{
			f_count += 3; //for each line add 3 vertices.
		}
	}
	
	//at this point, we are at the end of the file, so
	//store the f line count we have so far
	num_verts_per_material[i_mat] = f_count;
	
	//restore the file pointer to the beginning of the file
	fseek(pFile, 0, SEEK_SET);
	
	return 1;
}

/*
Read_Vertices() parses the .obj text file and loads the values of vertex attributes these are the 'v', 'vn', 'vt' lines.
*/
static int Read_Vertices(FILE * pFile, float * positions, float * normals, float * texcoords)
{
	char line[255];
	char c = 0; //initialize to 0 so the while loop that reads a line can be entered
	char temp_c[2]; //temporary string for holding the first part of a line
	int i;
	int line_num = 1;
	int i_pos=0;
	int i_normal=0;
	int i_texcoord=0;
	float temp_float[3];
	
	//loop through the file
	while(feof(pFile) == 0)
	{
		i = 0;
		//get a line
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			
			if(feof(pFile) != 0)
				break;
			
			//check the index to make sure we didn't walk off the end of the line array.
			if(i == 255)
			{
				printf("Read_Vertices: error line %d too long.\n", line_num);
				return 0;
			}
			
			line[i] = c;
			i += 1;
		}

		//break out of the loop
		if(feof(pFile) != 0)
			break;
		
		line_num += 1;
		c = 0;
		line[i] = 0; //replace the '\n' with a null termination

		if(line[0] == 'v' && line[1] == ' ')
		{
			sscanf(line, "%c%c %f %f %f", temp_c, (temp_c+1), temp_float, (temp_float+1), (temp_float+2));
			positions[(i_pos)] = temp_float[0];
			positions[(i_pos+1)] = temp_float[1];
			positions[(i_pos+2)] = temp_float[2];
			i_pos += 3;
		}
		if(line[0] == 'v' && line[1] == 't')
		{
			sscanf(line, "%c%c %f %f", temp_c, (temp_c+1), temp_float, (temp_float+1));
			texcoords[(i_texcoord)] = temp_float[0];
			texcoords[(i_texcoord+1)] = temp_float[1];
			i_texcoord += 2;
		}
		if(line[0] == 'v' && line[1] == 'n')
		{
			sscanf(line, "%c%c %f %f %f", temp_c, (temp_c+1), temp_float, (temp_float+1), (temp_float+2));
			normals[(i_normal)] = temp_float[0];
			normals[(i_normal+1)] = temp_float[1];
			normals[(i_normal+2)] = temp_float[2];
			i_normal += 3;
		}
	}
	//restore the file pointer to the beginning of the file
	fseek(pFile, 0, SEEK_SET);
	
	return 1;
}

/*Read_Triangles() reads the 'f' lines from the obj file*/
static int Read_Triangles(FILE * pFile, float * p_pos, float * p_normals, float * p_texcoords, struct chr_character_file_struct * p_file_stats)
{
	char line[255];
	char c = 0; //initialize to 0 so the while loop that reads a line can be entered
	char temp_c; //temporary string for holding the first part of a line
	int i;
	int line_num = 1;
	int i_temp_pos[3];
	int i_temp_norm[3];
	int i_temp_texcoord[3];
	int i_vertex=0;
	int i_index=0;
	int i_mat=-1; //use -1 as a flag to indicate that this is the first encounter of a 'usemtl' line.
	float * temp_vert_data=0;	//array of the vertex data of the current material
	int * temp_vert_index=0; //array of the indices for the current material
	
	//loop through the file
	while(feof(pFile) == 0)
	{
		i = 0;
		//get a line
		while(c != '\n')
		{
			c = (char)fgetc(pFile);
			
			if(feof(pFile) != 0)
				break;
			
			//check the index to make sure we didn't walk off the end of the line array.
			if(i == 255)
			{
				printf("Read_Vertices: error line %d too long.\n", line_num);
				return 0;
			}
			
			line[i] = c;
			i += 1;
		}

		//break out of the loop
		if(feof(pFile) != 0)
			break;
		
		line_num += 1;
		c = 0;
		line[i] = 0; //replace the '\n' with a null termination

		if(strncmp("usemtl",line,6) == 0)
		{
			i_mat += 1; //go to the next material

			//reset the indices since we are now filling into a new material array
			i_vertex = 0;
			i_index = 0;
			
			//point to the vertex data and indices of the new material
			temp_vert_data = p_file_stats->p_vert_data[i_mat];
			temp_vert_index = p_file_stats->p_indices[i_mat];
		}
		if(line[0] == 'f' && line[1] == ' ')
		{
			sscanf(line, "%c %d/%d/%d %d/%d/%d %d/%d/%d", &temp_c, 
				i_temp_pos, i_temp_texcoord, i_temp_norm,
				(i_temp_pos+1), (i_temp_texcoord+1), (i_temp_norm+1),
				(i_temp_pos+2), (i_temp_texcoord+2), (i_temp_norm+2));
				
			//decrement the indexes read from the file because they start at 1
			for(i = 0; i < 3; i++)
			{
				i_temp_pos[i] -= 1;
				i_temp_texcoord[i] -= 1;
				i_temp_norm[i] -= 1;
			}
			
			//convert the index by vertex to a float array index
			i_temp_pos[0] *= 3;
			i_temp_pos[1] *= 3;
			i_temp_pos[2] *= 3;
			i_temp_texcoord[0] *= 2;
			i_temp_texcoord[1] *= 2;
			i_temp_texcoord[2] *= 2;
			i_temp_norm[0] *= 3;
			i_temp_norm[1] *= 3;
			i_temp_norm[2] *= 3;
			
			//copy the data from the arrays that hold position, normal, and texture coordinates into
			//the interleaved final array.
			//each 'f' line in the file is a triangle, so 3 vertices
			for(i = 0; i < 3; i++)
			{
				temp_vert_data[i_vertex] = p_pos[(i_temp_pos[i])];
				temp_vert_data[(i_vertex+1)] = p_pos[(i_temp_pos[i]+1)];
				temp_vert_data[(i_vertex+2)] = p_pos[(i_temp_pos[i]+2)];
			
				temp_vert_data[(i_vertex+3)] = p_normals[(i_temp_norm[i])];
				temp_vert_data[(i_vertex+4)] = p_normals[(i_temp_norm[i]+1)];
				temp_vert_data[(i_vertex+5)] = p_normals[(i_temp_norm[i]+2)];
			
				temp_vert_data[(i_vertex+6)] = p_texcoords[(i_temp_texcoord[i])];
				temp_vert_data[(i_vertex+7)] = p_texcoords[(i_temp_texcoord[i]+1)];
			
				i_vertex += 8;
				
				//TODO: Fix this to actually take advantage of duplicate vertices.
				temp_vert_index[(i_index)] = (i_index);
				temp_vert_index[(i_index+1)] = (i_index+1);
				temp_vert_index[(i_index+2)] = (i_index+2);
			}
			
			i_index += 3;
		}
	}
	//restore the file pointer to the beginning of the file
	fseek(pFile, 0, SEEK_SET);
	
	return 1;
}

/*
This function searches the positions array and returns the min & max values in each axis
in the p_minmaxs array.
index:	value:
0	x axis min
1	x axis max
2	y axis min
3	y axis max
4	z axis min
5	z axis max
*/
static int FindMinMaxInEachAxis(float * positions, int num_verts, float * p_minmaxs)
{
	int i;
	
	//load the first vertex into the minmax array so we have something to
	//compare against.
	p_minmaxs[0] = positions[0];
	p_minmaxs[1] = positions[0];
	p_minmaxs[2] = positions[1];
	p_minmaxs[3] = positions[1];
	p_minmaxs[4] = positions[2];
	p_minmaxs[5] = positions[2];
	
	for(i = 0; i < (num_verts-1); i++)
	{
		//check x-axis
		if(positions[i] < p_minmaxs[0])
			p_minmaxs[0] = positions[i];
		if(positions[i] > p_minmaxs[1])
			p_minmaxs[1] = positions[i];

		//check y-axis
		if(positions[(i*3)+1] < p_minmaxs[2])
			p_minmaxs[2] = positions[(i*3)+1];
		if(positions[(i*3)+1] > p_minmaxs[3])
			p_minmaxs[3] = positions[(i*3)+1];
		
		//check z-axis
		if(positions[(i*3)+2] < p_minmaxs[4])
			p_minmaxs[4] = positions[(i*3)+2];
		if(positions[(i*3)+2] > p_minmaxs[5])
			p_minmaxs[5] = positions[(i*3)+2];
	}
	
	return 1;
}

/*
Load_Character_Scale() - scales the vertex positions
*/
extern int Load_Character_Scale(struct chr_character_file_struct * file_stats, float fscale) 
{
	float * vert=0;
	int i;
	int j;

	//loop through all materials
	for(i = 0; i < file_stats->num_materials; i++)
	{
		for(j = 0; j < file_stats->num_vertices[i]; j++)
		{
			//assume data for a vertex is in this order: pos(3 floats), normal(3 floats), texcoord (2 flots)
			vert = file_stats->p_vert_data[i] + (j*8);
			vert[0] *= fscale;
			vert[1] *= fscale;
			vert[2] *= fscale;
		}
	}
}
