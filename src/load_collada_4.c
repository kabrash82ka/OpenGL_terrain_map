#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "load_collada_4.h"
#include "my_mat_math_6.h"

struct dae_material_struct
{
	char * id;
	float diffuse[3];
};

static int GetNextTag(FILE * pFile, char * tag_string);
static int FindNextTag(FILE * pFile, char * tag_to_find, char * tag_string);
static int FindChildElement(FILE * pFile, char * tag_to_find, char * tag_string, char * parent_tag);
static int GetElementAttribute(char * tag_string, char * attribute_name, char * value);
static int GetFloatData(FILE * pFile, float * float_array, int num_floats);
static int GetIntArray(FILE * pFile, int num_ints, int * int_array);
static int CheckVcount(FILE * pFile);
static int GetPositionArray(FILE * pFile, int * num_pos, float ** pos_array);
static int GetPositionArraySize(FILE * pFile);
static int GetPolylist(FILE * pFile, int * num_inputs, int * num_polylist_verts, int ** polylist_out);
static int GetNumPolylist(FILE * pFile);
static int CheckNamesInString(char * string, int num_bones, char * name_array);
static int GetNumMaterials(FILE * pFile, int * num_materials);
static int GetMaterials(FILE * pFile, int num_materials, struct dae_material_struct * materials);
static void Convert_Mat_From_DAE_To_GL(float * mat4);

/*
Load_DAE_Model(), where 'dae' is the file extension of the COLLADA file format.
This function opens 'filename' and
//TODO: Split this into two functions. one to load the position, normal arrays and
another to combine them into the VBOish format. The reason is that we also need to put in the
joint information and that is tied to the position array. 
*/
int Load_DAE_Model_Vertices(char * filename, struct dae_model_info * p_model_info)
{
	FILE * pVertFile=0;
	int num_positions;
	int num_normals;
	char * tag_string=0;
	char * value_string=0;
	char * id_string=0;
	int ret_val = -1;
	int r;
	int i;
	int j;
	int k_cur_vert=0;
	int bookmark_library_geometries;
	int bookmark_polylist;
	int position_offset = -1;
	int normal_offset = -1;
	int texcoord_offset = -1;
	int num_position_floats;
	int num_normal_floats;
	int num_polygons;
	int num_verts;
	int num_dae_indices;
	int attribs_filled;
	int * out_indices=0;
	float * positions=0;
	float * normals=0;
	float * out_vert_array=0;
	float cur_vert[6];
	
	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("Load_DAE_Model: error. could not malloc tag_string.\n");
		return -1;
	}
	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		printf("Load_DAE_Model: error. could not malloc value_string.\n");
		return -1;
	}
	id_string = (char*)malloc(255);
	if(id_string == 0)
	{
		printf("Load_DAE_Model: error. could not malloc id_string.\n");
		return -1;
	}
	
	pVertFile = fopen(filename,"r");
	if(pVertFile == 0)
	{
		printf("Load_DAE_Model: error. %s not found.\n", filename);
		return -1;
	}
	
	//Find <library_geometries>
	tag_string[0] = '\x00';
	r = FindNextTag(pVertFile, "library_geometries", tag_string);
	if(r == -1) //indicates end of file
	{
		printf("Load_DAE_Model: error. could not find library_geometries tag.\n");
		goto cleanup_1;
	}
	
	
	//At this point tag_string contains library_geometries. Save this file position, so we
	//can go back to it later for more searching.
	//printf("%s\n", tag_string);
	bookmark_library_geometries = ftell(pVertFile);
	
	//Now find polycount to get its input tags for positions & normals.
	//From those tags we can get the id's of the float arrays for vertex positions and
	//normals.
	
	//First find vertices id under the polylist
	tag_string[0] = '\x00';
	r = FindNextTag(pVertFile, "polylist", tag_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. polylist tag not found.\n");
		goto cleanup_1;
	}
	//Get the number of triangles from the polylist element.
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. polylist did not have count attribute.\n");
		goto cleanup_1;
	}
	sscanf(value_string, "%d", &num_polygons);
	num_verts = 3*num_polygons;
	num_dae_indices = 2*num_verts;
	
	//bookmark where the <polylist> element is because we'll need to come
	//back to it to load the other vertex data.
	bookmark_polylist = ftell(pVertFile);
	
	//Now search the <polylist> for an <input> that has semantic="VERTEX"
	tag_string[0] = '\x00';
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pVertFile, "input", tag_string, "/polylist");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "semantic", value_string);
			if(strcmp("VERTEX", value_string) == 0)
			{
				//Get the offset in the polylist for the position index
				r = GetElementAttribute(tag_string, "offset", id_string);
				if(r != 0)
				{
					printf("Load_DAE_Model: error. offset attrib not in VERTEX <input>\n");
					goto cleanup_1;
				}
				sscanf(id_string, "%d", &position_offset);
				//printf("pos_offset=%d\n", position_offset);
				
				
				r = 0; //set this to flag that we found vertex.
				break;
			}
			else
			{
				continue;
			}
		}
	}
	
	//the while loop will end with r = -1 if it couldn't find VERTEX
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find VERTEX semantic in polylist.\n");
		goto cleanup_1;
	}
	
	//Now that we have the VERTEX <input> element we need the name of the source
	r = GetElementAttribute(tag_string, "source", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find source attribute in VERTEX input.\n");
		goto cleanup_1;
	}
	
	//Now search for a <vertices> that has the id in value_string
	fseek(pVertFile, (long)bookmark_library_geometries, SEEK_SET);
	tag_string[0] = '\x00';
	r = FindNextTag(pVertFile, "vertices", tag_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find vertices element.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "id", id_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find id of vertices element.\n");
		goto cleanup_1;
	}
	if(strcmp(id_string, (value_string+1)) != 0) //check that the id matches the source. source string starts with a #, so need to offset by 1 to not use the # in the comparison.
	{
		printf("Load_DAE_Model: error. could not find <vertices> with id=%s\n", (value_string+1));
		goto cleanup_1;
	}

	//Now find an <input> child element that has a semantic=POSITION
	r = FindChildElement(pVertFile, "input", tag_string, "/vertices");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find <input> in <vertices>.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "semantic", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find <input> with semantic attribute.\n");
		goto cleanup_1;
	}
	if(strcmp("POSITION", value_string) != 0)
	{
		printf("Load_DAE_Model: error. could not find POSITION in vertices input.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "source", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find source id in vertices input.\n");
		goto cleanup_1;
	}
	
	//Now we have an id for positions in value_string. Search the mesh for this.
	fseek(pVertFile, (long)bookmark_library_geometries, SEEK_SET);
	tag_string[0] = '\x00';
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pVertFile, "source", tag_string, "/mesh");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "id", id_string);
			if(r == 0)
			{
				if(strcmp(id_string, (value_string+1)) == 0)
				{
					r = 0;
					break;
				}
			}
		}
	}
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find source with id=%s\n", (value_string+1));
		goto cleanup_1;
	}
	//printf("%s\n", id_string);
	
	//Now load the float array
	r = FindChildElement(pVertFile, "float_array", tag_string, "/source");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find float_array with id=%s\n", id_string);
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find count for id=%s\n", id_string);
		goto cleanup_1;
	}
	sscanf(value_string, "%d", &num_position_floats);
	positions = (float*)malloc(num_position_floats*sizeof(float));
	if(positions == 0)
	{
		printf("Load_DAE_Model: error. malloc() failed for %d floats.\n", num_position_floats);
		goto cleanup_1;
	}
	r = GetFloatData(pVertFile, positions, num_position_floats);
	if(r != 0) //on error quit. (GetFloatData() will print a more detailed error message)
		goto cleanup_2;
	//Now the position array is loaded as 'positions'.
	
	//Now load find the normals element and load it.
	//Go back to <polylist> and get the source of the NORMAL
	fseek(pVertFile, (long)bookmark_polylist, SEEK_SET);
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pVertFile, "input", tag_string, "/polylist");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "semantic", value_string);
			if(r == 0)
			{
				if(strcmp("NORMAL", value_string) == 0)
				{
					//Now we have the <input> for normals. Get the source
					r = GetElementAttribute(tag_string, "source", value_string);
					if(r == -1)
					{
						printf("Load_DAE_Model: error. <input> for NORMAL does not have source attribute.\n");
						goto cleanup_2;
					}
					r = GetElementAttribute(tag_string, "offset", id_string);
					if(r == -1)
					{
						printf("Load_DAE_Model: error. <input> for NORMAL does not have offset attrib.\n");
						goto cleanup_2;
					}
					sscanf(id_string, "%d", &normal_offset);
					//printf("normal_offset=%d\n", normal_offset);
					
					//we found NORMAL so mission complete.
					r = 0;
					break;
				}
				else
				{
					r = 0;
					continue;
				}
			}
			else
			{
				r = 0; //set r so that we continue looping thru child elements.
				continue;
			}
		}
	}
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find <input> NORMAL.\n");
		goto cleanup_2;
	}
	//value_string now contains the id of the <source> for normals.	
	
	//Now look for the normals <source>
	fseek(pVertFile, (long)bookmark_library_geometries, SEEK_SET);
	tag_string[0] = '\x00';
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pVertFile, "source", tag_string, "/mesh");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "id", id_string);
			if(r == 0)
			{
				if(strcmp(id_string, (value_string+1)) == 0)
				{
					r = 0;
					break;
				}
			}
		}
	}
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find source with id=%s\n", (value_string+1));
		goto cleanup_2;
	}
	r = FindChildElement(pVertFile, "float_array", tag_string, "/source");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find float_array for normals.\n");
		goto cleanup_2;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could float_array for normals does not have count attribute.\n");
		goto cleanup_2;
	}
	sscanf(value_string, "%d", &num_normal_floats);
	normals = (float*)malloc(num_normal_floats*sizeof(float));
	if(normals == 0)
	{
		printf("Load_DAE_Model: error. malloc() failed to allocate normals.\n");
		goto cleanup_2;
	}
	r = GetFloatData(pVertFile, normals, num_normal_floats);
	if(r != 0)
		goto cleanup_3;
	//now normals is loaded with the normals float array
	
	//Check that vcount only contains 3's for triangles.
	fseek(pVertFile, (long)bookmark_polylist, SEEK_SET);
	tag_string[0] = '\x00';
	r = FindChildElement(pVertFile, "vcount", tag_string, "/polylist");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. vcount not child of polylist.\n");
		goto cleanup_3;
	}
	r = CheckVcount(pVertFile);
	if(r != 0)
	{
		printf("Load_DAE_Model: error. vcount can only contain triangles.\n");
		goto cleanup_3;
	}
	
	//Find the <p> element in polylist and form the vertices from the position
	//and normal arrays.
	fseek(pVertFile, (long)bookmark_polylist, SEEK_SET);
	tag_string[0] = '\x00';
	r = FindChildElement(pVertFile, "p", tag_string, "/polylist");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. <p> not found in <polylist>.\n");
		goto cleanup_3;
	}
	
	//allocate memory for the final array.
	out_vert_array = (float*)malloc(num_verts*6*sizeof(float)); //where there are 3 pos floats + 3 normal floats
	if(out_vert_array == 0)
	{
		printf("Load_DAE_Model: error. malloc() failed for out_vert_array.\n");
		goto cleanup_3;
	}
	out_indices = (int*)malloc(num_verts*sizeof(int));
	if(out_indices == 0)
	{
		printf("Load_DAE_Model: error. malloc failed for out_indices.\n");
		goto cleanup_4;
	}
	
	attribs_filled = 0;
	for(i = 0; i < num_dae_indices; i++)
	{
		//if(i == (num_dae_indices-1))
		//	printf("d401\n");
		fscanf(pVertFile, "%d", &j);
		if((i % 2) == position_offset)
		{
			cur_vert[0] = positions[(j*3)];
			cur_vert[1] = positions[(j*3)+1];
			cur_vert[2] = positions[(j*3)+2];
			attribs_filled += 1;
		}
		if((i % 2) == normal_offset)
		{
			cur_vert[3] = normals[(j*3)];
			cur_vert[4] = normals[(j*3)+1];
			cur_vert[5] = normals[(j*3)+2];
			attribs_filled += 1;
		}
		if(attribs_filled == 2) //if we got both the position and normal attribs then copy the whole vertex over to the final array.
		{
			memcpy((out_vert_array+(k_cur_vert*6)), cur_vert, (6*sizeof(float)));
			out_indices[k_cur_vert] = k_cur_vert; //TODO: Fix this because the EBO really doesn't mean anything
			k_cur_vert += 1;
			attribs_filled = 0; //reset count of vertex attribs filled.
		}
	}
	p_model_info->vertex_data_format[0] = 'P';
	p_model_info->vertex_data_format[1] = 'N';
	p_model_info->vertex_data_format[2] = '\x00';
	p_model_info->floats_per_vert = 6; //3 for pos + 3 for normal
	p_model_info->num_verts = num_verts;
	p_model_info->num_indices = num_verts;
	p_model_info->vert_indices = out_indices;
	p_model_info->vert_data = out_vert_array;
	ret_val = 0; //flag completion
	goto cleanup_3;
	
cleanup_5:
	free(out_indices);
cleanup_4:
	free(out_vert_array); //don't free this if everything worked.
cleanup_3:
	free(normals);	
cleanup_2:
	free(positions);	
cleanup_1:
	free(tag_string);
	free(value_string);
	free(id_string);
	fclose(pVertFile);
	return ret_val;
}

int Load_DAE_Model_Vertices2(char * filename, struct dae_model_info * p_model_info)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	char * source_name_arrays[3];
	float * float_arrays[3]; //array of float arrays: 0=position, 1=normal, 2=color
	float * out_vertices=0;
	int * out_indices=0;
	struct dae_material_struct * materials=0;
	long bookmark;
	float cur_vert[9];
	int float_arrays_len[3];
	int k_cur_vert;
	int num_floats;
	int num_polygons=0;
	int num_verts=0;
	int num_dae_indices=0;
	int num_sources;
	int num_inputs;
	int num_materials;
	int count;
	int attribs_filled=0;
	int i;
	int j;
	int r;
	int i_polylist;

	memset(source_name_arrays, 0, 3*sizeof(char *));
	memset(float_arrays_len, 0, 3*sizeof(int));

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: could not find %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	//before we load the vertex data, get the material information.
    r = GetNumMaterials(pFile, &num_materials);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	materials = (struct dae_material_struct *)malloc(num_materials*sizeof(struct dae_material_struct));
	if(materials == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = GetMaterials(pFile, num_materials, materials);
	if(r == -1)
	{
	 	printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup;
	}
	//materials array is now filled with color information.

	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//load all the different <source> floats
	i = 0;
	while(1)
	{
		r = FindChildElement(pFile, "source", tag_string, "/mesh");
		if(r == -1)
			break;
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		//save the name in an array so we can find it later
		source_name_arrays[i] = (char*)malloc(strlen(value_string)+1);
		if(source_name_arrays[i] == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		strcpy(source_name_arrays[i], value_string);

		//now find the <float_array> and save it
		r = FindChildElement(pFile, "float_array", tag_string, "/source");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		sscanf(value_string, "%d", &num_floats);
		float_arrays_len[i] = num_floats;
		float_arrays[i] = (float*)malloc(num_floats*sizeof(float));
		if(float_arrays[i] == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		r = GetFloatData(pFile, float_arrays[i], num_floats);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		
		//stop after load 3.	
		if(i >= 2)
			break;
		
		i += 1;
	}
	num_sources = i+1; //add 1 because i was an index and we need a #

	//how many <source> elements did we find?
	
	//now load the polylists
	//get a count of the number of triangles
	bookmark = ftell(pFile);
	i = 0;
	while(1)
	{
		r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
		if(r == -1)
			break;
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		sscanf(value_string, "%d", &count);
		num_polygons += count;
	}
	num_verts = 3*num_polygons;

	//allocate final arrays.
	out_vertices = (float*)malloc(9*num_verts*sizeof(float)); //6 = 3 for pos, 3 for norm, 3 for color
	if(out_vertices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	out_indices = (int*)malloc(num_verts*sizeof(int));
	if(out_indices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//go back to the start of the polylists:
	fseek(pFile, bookmark, SEEK_SET);
	k_cur_vert = 0;
	i_polylist = 0;
	while(1)
	{
		r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
		if(r == -1)
			break;
		bookmark = ftell(pFile); //save the point right after polylist because we need to go back to it
								 //after counting the inputs.
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}	
		//TODO: put <vcount> check to make sure it is 3 in here.
		sscanf(value_string, "%d", &count);
		num_dae_indices = count*3; //3 is since we are using triangles.

		//get a count of the number of input elements in the polylist.
		num_inputs = 0;
		while(1)
		{
			r = FindChildElement(pFile, "input", tag_string, "/polylist");
			if(r == -1)
				break;
			num_inputs += 1;
		}
		num_dae_indices *= num_inputs;

		fseek(pFile, bookmark, SEEK_SET);
		r = FindChildElement(pFile, "p", tag_string, "/polylist");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		attribs_filled=0;
		for(i = 0; i < num_dae_indices; i++)
		{
			//check k_cur_vert is not out of bounds
			if(k_cur_vert >= num_verts)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}

			fscanf(pFile, "%d", &j);
			if((i % num_inputs) == 0) //position
			{
				cur_vert[0] = (float_arrays[0])[j*3];
				cur_vert[1] = (float_arrays[0])[(j*3)+1];
				cur_vert[2] = (float_arrays[0])[(j*3)+2];
				attribs_filled += 1;
			}
			if((i % num_inputs) == 1) //normal
			{
				cur_vert[3] = (float_arrays[1])[(j*3)];
				cur_vert[4] = (float_arrays[1])[(j*3)+1];
				cur_vert[5] = (float_arrays[1])[(j*3)+2];
				attribs_filled += 1;
			}
			if((i % num_inputs) == 2) //color
			{
				cur_vert[6] = materials[i_polylist].diffuse[0];
				cur_vert[7] = materials[i_polylist].diffuse[1];
				cur_vert[8] = materials[i_polylist].diffuse[2];
				attribs_filled += 1;
			}
			if(attribs_filled == num_inputs)
			{
				memcpy((out_vertices+(k_cur_vert*9)), cur_vert, (9*sizeof(float)));
				out_indices[k_cur_vert] = k_cur_vert;
				k_cur_vert += 1;
				attribs_filled = 0;
			}
		}

		i_polylist += 1; //increment the index of polylists
	}
	p_model_info->vert_indices = out_indices;
	p_model_info->vert_data = out_vertices;
	p_model_info->vertex_data_format[0] = 'P';
	p_model_info->vertex_data_format[1] = 'N';
	p_model_info->vertex_data_format[2] = 'C';
	p_model_info->floats_per_vert = 9; //3 for pos + 3 for normal + 3 for color
	p_model_info->num_verts = num_verts;
	p_model_info->num_indices = num_verts;
	r = 0;

cleanup:
	free(tag_string);
	fclose(pFile);
	for(i = 0; i < 3; i++)
	{
		if(source_name_arrays[i] != 0)
			free(source_name_arrays[i]);
	}
	if(materials != 0)
	{
		for(i = 0; i < num_materials; i++)
		{
			if(materials[i].id != 0)
				free(materials[i].id);
		}
		free(materials);
	}
	return r;
}

/*
Load_DAE_Model_Vertivces3() loads the <source> element arrays for position, normals, and texcoords
Returns:
	0	;ok
	-1	;error occurred
*/
int Load_DAE_Model_Vertices3(FILE* pFile, struct dae_model_vert_data_struct * vert_data)
{
	char positions_substring[10] = "positions";
	char normals_substring[8] = "normals";
	char tex_substring[4] = "map";
	char * tag_string=0;
	char * value_string=0;
	long bookmark_library_geometries;
	int i;
	int j;
	int r;

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	memset(vert_data, 0, sizeof(struct dae_model_vert_data_struct));

	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	bookmark_library_geometries = ftell(pFile);

	//Loop through each <source> element
	for(i = 0; i < 3; i++)
	{
		r = FindChildElement(pFile, "source", tag_string, "/mesh");
		if(r == -1)
			break;
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		
		//check if the <source> element matches one of the substrings
		if(strstr(value_string, positions_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetElementAttribute(tag_string, "count", value_string);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			sscanf(value_string, "%d", &(vert_data->num_positions));
		}
		else if(strstr(value_string, normals_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetElementAttribute(tag_string, "count", value_string);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			sscanf(value_string, "%d", &(vert_data->num_normals));
		}
		else if(strstr(value_string, tex_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetElementAttribute(tag_string, "count", value_string);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			sscanf(value_string, "%d", &(vert_data->num_texcoords));
		}
		else
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//at this point we have now the length of all the arrays.
	//Now allocate them
	vert_data->positions = (float*)malloc(vert_data->num_positions*sizeof(float));
	if(vert_data->positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	vert_data->normals = (float*)malloc(vert_data->num_normals*sizeof(float));
	if(vert_data->normals == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	} 
	vert_data->texcoords = (float*)malloc(vert_data->num_texcoords*sizeof(float));
	if(vert_data->texcoords == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//loop through the <source> elements again this time load the data
	fseek(pFile, bookmark_library_geometries, SEEK_SET);
	for(i = 0; i < 3; i++)
	{
		r = FindChildElement(pFile, "source", tag_string, "/mesh");
		if(r == -1)
			break;
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		if(strstr(value_string, positions_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetFloatData(pFile, vert_data->positions, vert_data->num_positions);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
		else if(strstr(value_string, normals_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetFloatData(pFile, vert_data->normals, vert_data->num_normals);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
		else if(strstr(value_string, tex_substring) != 0)
		{
			r = FindChildElement(pFile, "float_array", tag_string, "/source");
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			r = GetFloatData(pFile, vert_data->texcoords, vert_data->num_texcoords);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
		else
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//ok. vert_data loaded
	r = 0;

cleanup:
	free(tag_string);
	return r;
}

/*
Load_DAE_Bones() fills in the model_info bone and weight factor data. 
*/
int Load_DAE_Bones(char * filename, 
		int num_pos,									//number of position vectors (not # of floats)
		struct dae_polylists_struct * polylists, 
		int num_inputs, 								//in the <p> element this is the # of properties associated with a vertex (e.g. pos, normal, texcoord)
		struct dae_model_bones_struct * model_info)
{
	float * positions=0;
	float * weight_pos_array=0;
	int r;
	int i;
	int j;
	int k;
	int i_src;
	int num_bones;
	int num_total_verts=0;

	memset(model_info, 0, sizeof(struct dae_model_bones_struct));

	//get # of positions in the position array.
	model_info->num_pos = num_pos;

	//calculate the total number of unique vertices
	for(i = 0; i < polylists->num_polylists; i++)
	{
		num_total_verts += polylists->polylist_len[i];
	}
	num_total_verts /= 4; //suming polylist_len gives # of indices total. There are 4 indices per vertex (POS, NORMAL, TEXCOORD, COLOR)
	model_info->num_verts = num_total_verts;
	
	//Once we have the polylist we can then use the index for the position array to construct
	//arrays with animation data. How to reference the animation data back to the vertex
	//is not discussed in the docs.

	num_bones = Load_DAE_GetNumBones(filename);
	if(num_bones == -1)
		return -1;
	model_info->num_bones = num_bones;

	//fill in inverse_bind_mat4_array
	model_info->inverse_bind_mat4_array = (float*)malloc(num_bones*16*sizeof(float));
	if(model_info->inverse_bind_mat4_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	r = Load_DAE_InverseBindMatrixArray(filename, num_bones, model_info->inverse_bind_mat4_array);
	if(r == -1)
		return -1;

	//these matrices are in row major so need to transpose them
	for(i = 0; i < num_bones; i++)
	{
		mmTransposeMat4(model_info->inverse_bind_mat4_array+(16*i));
	}

	//fill in bone transform array
	model_info->bone_transform_mat4_array = (float*)malloc(num_bones*16*sizeof(float));
	if(model_info->bone_transform_mat4_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	r = Load_DAE_JointMatArray(filename, num_bones, model_info->bone_transform_mat4_array);
	if(r == -1)
		return -1;

	//these matrices are in row major so need to transpose them:
	for(i = 0; i < num_bones; i++)
	{
		mmTransposeMat4(model_info->bone_transform_mat4_array+(16*i));
	}

	//fill in weight array
	weight_pos_array = (float*)malloc(model_info->num_pos*num_bones*sizeof(float));
	if(weight_pos_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	r = Load_DAE_weight_array(filename, model_info->num_pos, model_info->num_bones, weight_pos_array);
	if(r == -1)
		return -1;
	//the weight_pos_array is an array of weights per position. Now use the polylist array to
	//make the complete weight_array.
	model_info->weight_array = (float*)malloc(num_total_verts*num_bones*sizeof(float));
	if(model_info->weight_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	k = 0;
	for(i = 0; i < polylists->num_polylists; i++)
	{
		for(j = 0; j < (polylists->polylist_len[i]/4); j++)	//polylist_len is # of floats, so to iterate over vertices need to divide by 4
		{
			//use the polylist array to lookup the pos index for the vertex.
			i_src = polylists->polylist_indices[i][(j*num_inputs)];
			memcpy((model_info->weight_array+(k*num_bones)), (weight_pos_array+(num_bones*i_src)), num_bones*sizeof(float));
			k += 1;
		}
	}

	//fill in the bone hierarchy tree
	model_info->bone_tree = (struct dae_bone_tree_leaf*)malloc(num_bones*sizeof(struct dae_bone_tree_leaf));
	if(model_info->bone_tree == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	r = Load_DAE_GetBoneHierarchy(filename, model_info->num_bones, model_info->bone_tree);
	if(r == -1)
		return -1;

	//fill in bind shape matrix
	r = Load_DAE_BindShapeMat(filename, (model_info->bind_shape_mat4));
	if(r == -1)
		return -1;

	//fill in armature matrix
	//note: This mat doesn't need to be transposed because it is built manually
	//and is in column major format.
	r = Load_DAE_GetArmatureMat(filename, (model_info->armature_mat4));
	if(r == -1)
		return -1;

	free(weight_pos_array);
	//model_info is now loaded with bone information return success.
	return 0;
}

int Load_DAE_Animation(char * filename, int num_bones, struct dae_animation_struct * anim_info)
{
	int i;
	int r;

	r = Load_DAE_GetNumAnimationFrames(filename);
	if(r == -1)
		return -1;
	r = Load_DAE_GetAnimationTransforms(filename, num_bones, r, anim_info);
	if(r == -1)
		return -1;

	//transpose the mat4s in that were loaded by Load_DAE_GetAnimationTransforms() since they
	//are row-major in the file and need to be in colum-major form.
	for(i = 0; i < (num_bones*anim_info->num_frames); i++)
	{
		mmTransposeMat4(anim_info->bone_frame_transform_mat4_array+(16*i));
	}

	return 0;
}

int Load_DAE_BindShapeMat(char * filename, float * out_bind_shape_mat4)
{
	FILE* pFile = 0;
	char * tag_string=0;
	int i;
	int r;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	r = FindNextTag(pFile, "bind_shape_matrix", tag_string);
	if(r == -1)
	{
		printf("%s: error in %s. could not find <bind_shape_matrix>\n", __func__, filename);
		return -1;
	}
	r = GetFloatData(pFile, out_bind_shape_mat4, 16);
	if(r == -1)
	{
		printf("%s: error reading floats for <bind_shape_matrix>\n", __func__);
		return -1;
	}

	//transpose the bind shape mat.
	mmTransposeMat4(out_bind_shape_mat4);

	fclose(pFile);
	return 0;
}

/*
Load_DAE_GetNumBones() returns the number of bones in the skeleton or -1
if there was an error.
*/
int Load_DAE_GetNumBones(char * filename)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	long last_source_offset;
	int r;
	int count;

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup_bad;
	}

	r = FindNextTag(pFile, "skin", tag_string);
	if(r < 0)
	{
		printf("%s: error could not find <skin>\n", __func__);
		goto cleanup_bad;
	}
	
	while(1)
	{
		r = FindChildElement(pFile, "source", tag_string, "/skin");
		if(r < 0)
			break;
		else
		{
			last_source_offset = ftell(pFile);
			r = FindChildElement(pFile, "param", tag_string, "/source");
			if(r < 0)
				break;
		
			r = GetElementAttribute(tag_string, "name", value_string);
	    	if(r < 0)
				continue;
			if(strcmp(value_string,"JOINT") == 0)
			{
				r = 0;
				fseek(pFile, last_source_offset, SEEK_SET);
				break;
			}
		}
	}
	if(r != 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup_bad;
	}
	
	//At this point we found the <source> that has the JOINT names.
	//now look for their count under <Name_array>
	r = FindChildElement(pFile, "Name_array", tag_string, "/source");
	if(r < 0)
	{
		printf("%s: error could not find <Name_array>\n", __func__);
		goto cleanup_bad;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r < 0)
	{
		printf("%s: error could not find count in <Name_array>\n", __func__);
		goto cleanup_bad;
	}

	//Now we found the count attribute. Convert it to string.
	sscanf(value_string, "%d", &count);
	free(tag_string);
	free(value_string);
	fclose(pFile);
	return count;
cleanup_bad:
	free(tag_string);
	free(value_string);
	if(pFile != 0)
		fclose(pFile);
	return -1;
}

/*
Load_DAE_InverseBindMatrixArray() reads array of bind pose matrices.
*/
int Load_DAE_InverseBindMatrixArray(char * filename, int num_bones, float * p_inv_bind_mat_array)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	char * search_string=0;
	int r;
	int float_count;

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		free(tag_string);
		return -1;
	}
	search_string = (char*)malloc(255);
	if(search_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		free(tag_string);
		free(value_string);
		return -1;
	}

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error could not open %s\n", __func__, filename);
		return -1;
	}

	//First get the name of the inverse bind matrix element
	r = FindNextTag(pFile, "joints", tag_string);
	if(r < 0)
	{
		printf("%s: error could not open %s\n", __func__, filename);
		r = -1;
		goto cleanup_ok;
	}

	//loop through <input> elements to find INV_BIND_MATRIX
	while(1)
	{
		r = FindChildElement(pFile, "input", tag_string, "/joints");
		if(r < 0)
		{
			r = -1;
			goto cleanup_ok;
		}
		r = GetElementAttribute(tag_string, "semantic", search_string);
		if(r < 0)
		{
			r = -1;
			printf("%s: could not find semantic attribute.\n", __func__);
			goto cleanup_ok;
		}
		if(strcmp(search_string, "INV_BIND_MATRIX") == 0)
		{
			//So this is the INV_BIND_MATRIX, now get its name string.
			r = GetElementAttribute(tag_string, "source", search_string);
			if(r < 0)
			{
				printf("%s: error, INV_BIND_MATRIX has no source attribute\n", __func__);
				r = -1;
				goto cleanup_ok;
			}

			//flag success and break
			r = 0;
			break;
		}
	}
	if(r < 0)
	{
		r = -1;
		goto cleanup_ok;
	}

	//now we have INV_BIND_MATRIX name string, go back to beginning and look for it
	rewind(pFile);
	r = FindNextTag(pFile, "skin", tag_string);
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_ok;
	}

	//Now in <skin> loop through <source>'s to find name
	while(1)
	{
		r = FindChildElement(pFile, "source", tag_string, "/skin");
		if(r < 0)
		{
			r = -1;
			goto cleanup_ok;
		}

		//check id attribute for match
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r < 0)
		{
			printf("%s: error could not find id attribute.\n", __func__);
			r = -1;
			goto cleanup_ok;
		}

		//got id attribute in 'vaue_string', now check against search_string
		if(strcmp(value_string, (search_string+1)) == 0) //add 1 to search_string since it has a # prefix
		{
			r = 0;
			break;
		}
	}
	if(r < 0)
	{
		r = -1;
		goto cleanup_ok;
	}

	//find <float_array>
	r = FindChildElement(pFile, "float_array", tag_string, "/source");
	if(r < 0)
	{
		printf("%s: error, could not find <float_array>\n", __func__);
		r = -1;
		goto cleanup_ok;
	}
	
	//get the float count to cross-check with # of bones
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r < 0)
	{
		printf("%s: error, could not find count attribute.\n", __func__);
		r = -1;
		goto cleanup_ok;
	}
	sscanf(value_string, "%d", &float_count);
	if(float_count != (num_bones*16))
	{
		r = -1;
		printf("%s: error mismatch, float_array only has %d floats but there are %d bones\n", __func__, float_count, num_bones);
		goto cleanup_ok;
	}

	r = GetFloatData(pFile, p_inv_bind_mat_array, float_count);
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_ok;
	}

	//floats are loaded into p_inv_bind_mat_array, set success and let func return.
	r = 0;

cleanup_ok:
	free(tag_string);
	free(value_string);
	return r;
}

/*
Load_DAE_JointMatArray() reads joint transforms from DAE file and places them in
p_joint_mats.
-//TODO: should I assume +1 to num_bones to handle armature transform?
*/
int Load_DAE_JointMatArray(char * filename, int num_bones, float * p_joint_mats)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	int r;
	int i;
	int i_bone=0;
	int num_pushed=0;
	char temp_c;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: could not find %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		fclose(pFile);
		return -1;
	}
	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		free(tag_string);
		fclose(pFile);
		return -1;
	}

	r = FindNextTag(pFile, "visual_scene", tag_string);
	if(r < 0)
	{
		printf("%s: error could not find <visual_scene>\n", __func__);
		r = -1;
		goto cleanup_ok;
	}

	//Now find <node> with Armature
	while(1)
	{
		r = FindChildElement(pFile, "node", tag_string, "/visual_scene");
		if(r < 0)
		{
			break;
		}
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == 0)
		{
			if(strcmp(value_string, "Armature") == 0)
			{
				r = 0;
				break;
			}
		}
	}
	if(r < 0)
	{
		r = -1;
		goto cleanup_ok;
	}

	//So now we are at the line after the <node> tag of the armature.
	//now search for bones. (collada uses the term "joint" inplace of "bone")
	while(1)
	{
		//r = FindChildElement(pFile, "node", tag_string, "/node");
		r = GetNextTag(pFile, tag_string);
		if(r < 0)
		{
			break;
		}

		//Temporarily convert the tag_string into a name_string
		i = 0;
		while(tag_string[i] != 0)
		{
			if(tag_string[i] == '\x20' || tag_string[i] == '>') //0x20=space, 
			{
				temp_c = tag_string[i];
				tag_string[i] = 0; //null-terminate
				break;
			}
			else
			{
				i += 1;
			}
		}
	
		if(strcmp(tag_string, "node") == 0)
		{
			num_pushed += 1;

			r = FindChildElement(pFile, "matrix", tag_string, "/node");
			if(r < 0)
			{
				printf("%s: error expected <matrix>\n", __func__);
				r = -1;
				goto cleanup_ok;
			}
			r = GetFloatData(pFile, (p_joint_mats+(i_bone*16)), 16);
			if(r < 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup_ok;
			}

			i_bone += 1;
		}

		if(strcmp(tag_string, "/node") == 0)
		{
			num_pushed -= 1;
			if(num_pushed == 0)
				break;
		}
	}
	
	if(i_bone == num_bones)
		r = 0;

cleanup_ok:
	free(tag_string);
	fclose(pFile);
	return r;
}

/*
Load_DAE_weight_array() assumes weight_array is matrix of vertex_positions * bones. For each element
there is a weight scalar. [vertex-position][bones]
-There is an issue. positions and normals are given as separate arrays and indexed separately. Then 
<polylist> tells you which of position & normals the final vertex needs.
But for <vertex_weights> vcount lists the influences for each "vertex", but it looks like this
is referring to the position array. A new weight_array will have to be created again based on
how the position & normals are combined.
*/
int Load_DAE_weight_array(char * filename, int num_pos, int num_bones, float * weight_array)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	float * skin_weights=0;
	int * vcounts=0;
	int * v_array=0;
	long skin_offset=0;
	int r;
	int i;
	int j;
	int num_weights;
	int vcount_size;
	int v_array_size;
	int weights_per_bone;
	int i_pair; //index for influence in the <v> array

	pFile = fopen(filename,"r");
	if(pFile == 0)
	{
		printf("%s: error. could not find file %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		fclose(pFile);
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		fclose(pFile);
		printf("%s: error line %d\n", __func__, __LINE__);
		free(tag_string);
		return -1;
	}

	r = FindNextTag(pFile, "skin", tag_string);
	if(r < 0)
	{
		printf("%s: error, could not find <skin>\n", __func__);
		r = -1;
		goto cleanup;
	}

	skin_offset = ftell(pFile);

	while(FindChildElement(pFile, "source", tag_string, "/skin") >= 0)
	{
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == 0) //element has an id attribute
		{
			if(strstr(value_string, "skin-weights") != 0)
			{
				r = FindChildElement(pFile, "float_array", tag_string, "/source");
				if(r == 0)
				{
					//get the count from the weights
					r = GetElementAttribute(tag_string, "count", value_string);
					if(r == 0)
					{
						break;//found the <float_array> so move on to the next step
					}
					else
					{
						printf("%s: error, float_array doesn't have count attribute.\n", __func__);
						r = -1;
						goto cleanup;
					}
				}
			}
		}
	}
	if(r < 0)
	{
		printf("%s: error, couldn't find skin-weights <source>.\n", __func__);
		r = -1;
		goto cleanup;
	}

	//get the size of the skin-weights array and allocate memory for it
	sscanf(value_string, "%d", &num_weights);
	if(num_weights > 0)
	{
		skin_weights = (float*)malloc(num_weights*sizeof(float));
		if(skin_weights == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}
	else
	{
		r = -1;
		printf("%s: error, skin-weights count of %d < 0\n", __func__, num_weights);
		goto cleanup;
	}

	//read the floats in the float_array into skin-weights
	r = GetFloatData(pFile, skin_weights, num_weights);
	if(r < 0)
	{
		r = -1;
		printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup;
	}

	fseek(pFile, skin_offset, SEEK_SET);
	r = FindChildElement(pFile, "vertex_weights", tag_string, "/skin");
	if(r < 0)
	{
		printf("%s: error could not find <vertex_weights>\n", __func__);
		r = -1;
		goto cleanup;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r < 0)
	{
		printf("%s: error could not find count attribute in vertex_weights\n", __func__);
		r = -1;
		goto cleanup;
	}
	sscanf(value_string, "%d", &vcount_size);

	//check vcount_size. size of the <vcount> array should equal # of positions
	if(vcount_size != num_pos)
	{
		printf("%s: error vcount(%d) != number of vertexes(%d)\n", __func__, vcount_size, num_pos);
		r = -1;
		goto cleanup;
	}

	r = FindChildElement(pFile, "vcount", tag_string, "/vertex_weights");
	if(r < 0)
	{
		printf("%s: error could not find <vcount>\n", __func__);
		r = -1;
		goto cleanup;
	}

	//allocate an array to load the integers
	vcounts = (int*)malloc(vcount_size*sizeof(int));
	if(vcounts == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = GetIntArray(pFile, vcount_size, vcounts);
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	
	//so now we have vcounts loaded, calculate the size of the <v> array
	v_array_size = 0;
	for(i = 0; i < vcount_size; i++)
	{
		v_array_size += vcounts[i];
	}
	v_array_size *= 2; //multiply by 2 because each vcount consists of both a bone index & a weight
	v_array = (int*)malloc(v_array_size*sizeof(int));
	if(v_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	r = FindChildElement(pFile, "v", tag_string, "/vertex_weights");
	if(r < 0)
	{
		printf("%s: error could not find <v> in <vertex_weights>\n", __func__);
		r = -1;
		goto cleanup_2;
	}
	r = GetIntArray(pFile, v_array_size, v_array);
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}

	//zero the weight-array so unused influences will be 0
	memset(weight_array, 0, (num_pos*num_bones*sizeof(float)));

	//Now we have <v> loaded, iterate through it to fill in the weight-array
	i_pair = 0;
	for(i = 0; i < vcount_size; i++)
	{
		weights_per_bone = vcounts[i];

		for(j = 0; j < weights_per_bone; j++)
		{
			weight_array[(i*num_bones)+v_array[(i_pair*2)]] = skin_weights[v_array[(i_pair*2)+1]];
			i_pair += 1;
		}
	}

cleanup_2:
	free(v_array);
cleanup_1:
	free(vcounts);
cleanup:
	fclose(pFile);
	free(tag_string);
	free(value_string);
	return r;
}

/*
Load_DAE_get_min_max_vcount() looks at the vertex weight's vcount element and finds the vertices with
the least & most number of influencing bones. <vcount> is an array where there is an
element for each vertex. The element specifies the number of bones that influence the
array.
*/
int Load_DAE_get_min_max_vcount(char * filename, int * min, int * max)
{
	char * tag_string=0;
	char * value_string=0;
	FILE * pFile=0;
	int i;
	int r;
	int bone_count;
	int num_verts;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error, could not find %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(255);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		fclose(pFile);
		return -1;
	}
	value_string = (char*)malloc(255);
	if(value_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		free(value_string);
		fclose(pFile);
		return -1;
	}

	r = FindNextTag(pFile, "vertex_weights", tag_string);
	if(r < 0)
	{
		printf("%s: error, could not find <vcount>.\n", __func__);
		r = -1;
		goto cleanup;
	}
	
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r < 0)
	{
		printf("%s: error, could not find count attribute.\n", __func__);
		r = -1;
		goto cleanup;
	}
	sscanf(value_string, "%d", &num_verts);

	r = FindNextTag(pFile, "vcount", tag_string);
	if(r < 0)
	{
		printf("%s: error, could not find <vcount>.\n", __func__);
		r = -1;
		goto cleanup;
	}

	//before we start counting bones, set the min & max to illegal
	//values to flag that they have not been set.
	*min = -1;
	*max = -1;

	for(i = 0; i < num_verts; i++)
	{
		r = fscanf(pFile, "%d", &bone_count);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		if(feof(pFile))
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		if(*min == -1)
			*min = bone_count;
		if(*max == -1)
			*max = bone_count;
		if(bone_count < *min)
			*min = bone_count;
		if(bone_count > *max)
			*max = bone_count;
	}
	
	//indicate success
	r = 0;

cleanup:
	free(tag_string);
	free(value_string);
	fclose(pFile);
	return r;
}

/*
Load_DAE_GetBoneHierarchy() parses the DAE file to make an array of parent indices
for each bone. The array has an element for each bone. Each element is the index
of the parent.
*/
int Load_DAE_GetBoneHierarchy(char * filename, int num_bones, struct dae_bone_tree_leaf * tree_root)
{
	char * tag_string=0;
	char * value_string=0;
	int * parent_stack=0;
	int * parent_list=0;
	FILE* pFile=0;
	int i_top=0; //points to the top element in parent_stack
	int i_bone;
	int r;
	int i;
	int j;
	int child_count;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	fseek(pFile, 0, SEEK_SET);//start at the beginning

	tag_string = (char*)malloc(510);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 255; //set the value_string right after the tag_string so there is only one allocation.

	parent_list = (int*)malloc(num_bones*sizeof(int));
	if(parent_list == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	parent_stack = (int*)malloc(num_bones*sizeof(int));
	if(parent_stack == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		free(tag_string);
		return -1;
	}
	parent_stack[0] = 0;

	r = FindNextTag(pFile, "visual_scene", tag_string);
	if(r < 0)
	{
		printf("%s: error could not find <visual_scene>\n", __func__);
		r = -1;
		goto cleanup_ok;
	}

	//Now find <node> with Armature
	while(1)
	{
		r = FindChildElement(pFile, "node", tag_string, "/visual_scene");
		if(r < 0)
		{
			break;
		}
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == 0)
		{
			if(strcmp(value_string, "Armature") == 0)
			{
				r = 0;
				break;
			}
		}
	}
	if(r < 0)
	{
		r = -1;
		goto cleanup_ok;
	}

	//So now we are at the line after the <node> tag of the armature.
	//now search for bones. (collada uses the term "joint" inplace of "bone")
	memset(parent_list, 0, (num_bones*sizeof(int)));
	i_bone = 0;

	while(1)
	{
		r = FindChildElement(pFile, "node", tag_string, "/node");
		if(r < 0)
		{
			i_top -= 1;
			
			//error check. i_top shouldn't be < 0.
			if(i_top < 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup_ok;
			}
		}
		else
		{
			parent_list[i_bone] = parent_stack[i_top];
			i_top += 1;
			parent_stack[i_top] = i_bone;

			i_bone += 1;

			//check to see if we found all the bones
			if(i_bone >= num_bones)
				break;
		}
	}
	if(i_bone != num_bones)
	{
		r = -1;
		goto cleanup_ok;
	}

	//now put the parent list in a tree structure.
	//(assume tree_root is an array of size num_bones)

	for(i = 0; i < num_bones; i++)
	{
		if(i == 0) //first node
		{
			tree_root[0].parent = 0;
		}
		else //all other nodes use the parent_list array to get the parent id.
		{
			tree_root[i].parent = tree_root + (parent_list[i]);
		}

		//now look for children and get a child count.
		child_count = 0;
		for(j = (i+1); j < num_bones; j++)
		{
			if(parent_list[j] == i)
				child_count += 1;
		}

		//use the child count to allocate an array
		if(child_count == 0)
		{
			tree_root[i].children = 0;
			tree_root[i].num_children = 0;
		}
		else
		{
			tree_root[i].num_children = child_count;
			tree_root[i].children = (struct dae_bone_tree_leaf**)malloc(child_count*sizeof(struct dae_bone_tree_leaf*));
			if(tree_root[i].children == 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup_ok;
			}
		}

		//look for children again this time storing their node id.
		if(child_count != 0)
		{
			child_count = 0;
			for(j = (i+1); j < num_bones; j++)
			{
				if(parent_list[j] == i)
				{
					tree_root[i].children[child_count] = tree_root + j;
					child_count += 1;
				}	
			}
		}
	}
	r = 0; //indicate success

cleanup_ok:
	free(tag_string);
	free(parent_stack);
	return r;
}

/*
Load_DAE_GetArmatureMat() - This function find the "Armature" <node> in the
<visual_scene> and puts together its transform.
*/
int Load_DAE_GetArmatureMat(char * filename, float * mat4)
{
	FILE * pFile=0;
	char * tag_string=0;
	char * value_string=0;
	float temp_vec[4];
	int i;
	int r;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error, could not find %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(510);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	value_string = tag_string+255; //give value_string the 2nd half of the allocation.

	r = FindNextTag(pFile, "visual_scene", tag_string);
	if(r < 0)
	{
		printf("%s: error could not find <visual_scene>\n", __func__);
		r = -1;
		goto cleanup;
	}
	while(1)
	{
		r = FindChildElement(pFile, "node", tag_string, "/visual_scene");
		if(r < 0)
			break;
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r < 0)
			continue;
		if(strcmp(value_string,"Armature") == 0)
		{
			r=0;
			break;
		}
	}
	if(r != 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	
	//so now we have found the Armature
	//Get translation first
	r = FindChildElement(pFile, "translate", tag_string, "/node");
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = GetFloatData(pFile, temp_vec, 3);
	if(r < 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	memset(mat4, 0, 16*sizeof(float));
	mat4[12] = temp_vec[0];
	mat4[13] = temp_vec[1];
	mat4[14] = temp_vec[2];
	mat4[0] = 1.0f;
	mat4[5] = 1.0f;
	mat4[10] = 1.0f;
	mat4[15] = 1.0f;
	
	//now just check that <rotate> doesn't do anything. The format for collada <rotate> tag
	//is four floats. first three are a unit vector and the 4th is the degrees to rotate
	//(collada spec doesn't say if rotation is clockwise or counterclockwise)
	for(i = 0; i < 3; i++)
	{
		r = GetNextTag(pFile, tag_string);
		if(r < 0)
			break;
		if(strstr(tag_string, "rotate") != 0)
		{
			r = GetFloatData(pFile, temp_vec, 4);
			if(r < 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}

			//check rotation in degrees if not zero, print error and return -1
			//because the model isn't going to load correctly
			if(temp_vec[3] != 0.0f)
			{
				printf("%s: error Armature base rotate not 0\n", __func__);
				r = -1;
				goto cleanup;
			}
			r = GetNextTag(pFile, tag_string); //get the </rotate> tag
			if(r < 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
	}
	//indicate success
	r = 0;

cleanup:
	if(tag_string != 0)
		free(tag_string);
	fclose(pFile);
	return r;
}

/*
GetNextTag() looks for the next <tag> in the file and if found copies it into tag_string.
Return values:
	0 = It found a tag
	-1 = end of file found. tag_string is invalid.
*/
static int GetNextTag(FILE * pFile, char * tag_string)
{
	char c;
	int i = 0;
	int found_start_of_tag = 0;
	
	while(feof(pFile) == 0)
	{
		c = fgetc(pFile);
		
		//Look for '<' When we find it start storing characters in the tag string
		if(found_start_of_tag == 1 && c == '>')
		{
			tag_string[i] = '\00'; //null terminate the string
			return 0;
		}
		if(found_start_of_tag == 0 && c == '<')
		{
			found_start_of_tag = 1;
			continue;
		}
		if(found_start_of_tag == 1)
		{
			tag_string[i] = c;
			i++;
		}
	}
	return -1;
}

/*
This function assumes that the file stream is right at the first digit
of the first float.
*/
static int GetFloatData(FILE * pFile, float * float_array, int num_floats)
{
	int i;
	int r;
	
	for(i = 0; i < num_floats; i++)
	{
		r = fscanf(pFile, "%f", (float_array+i));
		if(r == 0)
		{
			printf("GetFloatData: error at i=%d. could not load all %d floats.\n", i, num_floats);
			return -1;
		}
		if(feof(pFile) != 0)
		{
			printf("GetFloatData: error premature end of file found at i=%d\n", i);
			return -1;
		}
	}
	
	return 0;
}

/*
FindNextTag() looks for the next tag specified by 'tag_to_find' and if found
places it in 'tag_string' and returns 0. Return values:
	0 = Found a tag containing tag_to_find
	-1 = did not find tag before end of file
*/
static int FindNextTag(FILE * pFile, char * tag_to_find, char * tag_string)
{
	int r;
	char * r_string=0;
	
	while(feof(pFile) == 0)
	{
		r = GetNextTag(pFile, tag_string);
		if(r == 0)
		{
			r_string = strstr(tag_string, tag_to_find);
			if(r_string != 0) //found tag_to_find in tag_string
			{
				return 0;
			}
		}
	}
	
	return -1;
}

/*
FindChildElement() searches for the next tag before reaching a closing tag of the parent
element. So 'parent_tag' is a misnomer, be sure to pass '/polylist' and not 'polylist'.
Returns:
	0 = found tag_to_find
	-1 = hit end of parent element or end-of-file
*/
static int FindChildElement(FILE * pFile, char * tag_to_find, char * tag_string, char * parent_tag)
{
	int r;
	int i_swapped;
	int found_space=0;
	
	while(feof(pFile) == 0)
	{
		r = GetNextTag(pFile, tag_string);
		if(r == 0)
		{
			//first check if we found the parent's closing tag
			if(strcmp(parent_tag, tag_string) == 0)
				return -1;
			
			//Now see if this is the string we are looking for.
			
			//Find the first space in the tag_string
			i_swapped = 0;
			found_space = 0;
			while(tag_string[i_swapped] != '\x00')
			{
				if(tag_string[i_swapped] == ' ')
				{
					found_space=1;
					break;
				}
				i_swapped++;
			}
			tag_string[i_swapped] = '\x00'; //temporarily set the first space to make a smaller string to check
			
			//now check to see if this is the tag we are looking for:
			if(strcmp(tag_string, tag_to_find) == 0)
			{
				//If we found a space originally and not the null-termination
				//set it back to a space.
				if(found_space == 1)
					tag_string[i_swapped] = ' ';
				
				return 0;
			}
		}
	}
}

/*
GetElementAttribute() given a tag_string will look for an attribute and return its
value if it exists in the value string.
*/
static int GetElementAttribute(char * tag_string, char * attribute_name, char * value)
{
	int i = 0;
	int mode=0; /*0=look for space, 1=look for equals, 2=look for first double-quotes 3 = look for 2nd double quote*/
	int i_start_attribute;
	int i_end_attribute;
	int i_start_value=0;
	int i_end_value;
	char c;
	
	while(tag_string[i] != '\x00')
	{
		switch(mode)
		{
		case 0: //looking for a space character.
			if(tag_string[i] == ' ')
			{
				i_start_attribute = i + 1;
				mode=1;
			}
			break;
		case 1:
			if(tag_string[i] == '=')
			{
				i_end_attribute = i;
				
				//Now check if this is the attribute we are looking for:
				c = tag_string[i_end_attribute];
				tag_string[i_end_attribute] = '\x00';
				if(strcmp((tag_string+i_start_attribute),attribute_name) == 0)
				{
					mode = 2;
				}
				else
				{
					//This is kinda weak in here because it needs the attributes to not have spaces around
					//their equal signs which seems unnecessarily inflexible.
					mode = 0;//if this wasn't the right attribute look for the next one.
				}
				tag_string[i_end_attribute] = c; //put the character back
			}
			break;
		case 2:
			if(tag_string[i] == '"')
			{
				i_start_value = i+1; //add 1 so we don't include the double-quote in the value string
				mode = 3; 
			}
			break;
		case 3:
			if(tag_string[i] == '"')
			{
				i_end_value = i;//subtract 1 so we don't include the double-quote in the value string
				
				//copy the string to the value parameter
				c = tag_string[i_end_value];
				tag_string[i_end_value] = '\x00'; //null-terminate the string so we can copy
				strcpy(value, (tag_string+i_start_value));
				tag_string[i_end_value] = c;
				return 0;
			}
			break;
		}
		
		i++;//go to next character in the tag string
	}
	return -1;
}

/*
This function makes sure that the vcount element only contains 3's for
triangles.
Returns:
	0 = vcount contains only triangles
	-1 = vcount does not contain only trianlges
*/
static int CheckVcount(FILE * pFile)
{
	char c;
	
	while(feof(pFile) == 0)
	{
		c = fgetc(pFile);
		if(c == '3' || c == ' ')
		{
			continue;
		}
		if(c == '<')
		{
			break;
		}
		else
		{
			return -1;
		}
	}
	return 0;
}

/*
GetIntArray() - Assumes file stream is at the start of an array of integers in
the collada file.
*/
static int GetIntArray(FILE * pFile, int num_ints, int * int_array)
{
	int i;
	int r;

	for(i = 0; i < num_ints; i++)
	{
		r = fscanf(pFile, "%d", (int_array+i));
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return -1;
		}
		if(feof(pFile) != 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return -1;
		}

	}

	return 0;
}

static int GetPositionArray(FILE * pFile, int * num_pos, float ** pos_array)
{
	char * tag_string=0;
	char * value_string=0;
	char * id_string=0;
	float * positions=0;
	long bookmark_library_geometries;
	int r;

	tag_string = (char*)malloc(768);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256; //give value_string half the allocation.
	id_string = tag_string + (2*256);	

	rewind(pFile); //start at the beginning of the file, incase previous functions moved the file pointer.

	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1) //indicates end of file
	{
		printf("Load_DAE_Model: error. could not find library_geometries tag.\n");
		goto cleanup_1;
	}

	bookmark_library_geometries = ftell(pFile);
	r = FindNextTag(pFile, "polylist", tag_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. polylist tag not found.\n");
		goto cleanup_1;
	}
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pFile, "input", tag_string, "/polylist");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "semantic", value_string);
			if(strcmp("VERTEX", value_string) == 0)
			{
				r = 0; //set this to flag that we found vertex.
				break;
			}
			else
			{
				continue;
			}
		}
	}
	//the while loop will end with r = -1 if it couldn't find VERTEX
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find VERTEX semantic in polylist.\n");
		goto cleanup_1;
	}
	
	//Now that we have the VERTEX <input> element we need the name of the source
	r = GetElementAttribute(tag_string, "source", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find source attribute in VERTEX input.\n");
		goto cleanup_1;
	}
	//Now search for a <vertices> that has the id in value_string
	fseek(pFile, (long)bookmark_library_geometries, SEEK_SET);
	tag_string[0] = '\x00';
	r = FindNextTag(pFile, "vertices", tag_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find vertices element.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "id", id_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error could not find id of vertices element.\n");
		goto cleanup_1;
	}
	if(strcmp(id_string, (value_string+1)) != 0) //check that the id matches the source. source string starts with a #, so need to offset by 1 to not use the # in the comparison.
	{
		printf("Load_DAE_Model: error. could not find <vertices> with id=%s\n", (value_string+1));
		goto cleanup_1;
	}

	//Now find an <input> child element that has a semantic=POSITION
	r = FindChildElement(pFile, "input", tag_string, "/vertices");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find <input> in <vertices>.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "semantic", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find <input> with semantic attribute.\n");
		goto cleanup_1;
	}
	if(strcmp("POSITION", value_string) != 0)
	{
		printf("Load_DAE_Model: error. could not find POSITION in vertices input.\n");
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "source", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find source id in vertices input.\n");
		goto cleanup_1;
	}

	//Now we have an id for positions in value_string. Search the mesh for this.
	fseek(pFile, (long)bookmark_library_geometries, SEEK_SET);
	tag_string[0] = '\x00';
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pFile, "source", tag_string, "/mesh");
		if(r == 0)
		{
			r = GetElementAttribute(tag_string, "id", id_string);
			if(r == 0)
			{
				if(strcmp(id_string, (value_string+1)) == 0)
				{
					r = 0;
					break;
				}
			}
		}
	}
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find source with id=%s\n", (value_string+1));
		goto cleanup_1;
	}
	
	//Now load the float array
	r = FindChildElement(pFile, "float_array", tag_string, "/source");
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find float_array with id=%s\n", id_string);
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r == -1)
	{
		printf("Load_DAE_Model: error. could not find count for id=%s\n", id_string);
		goto cleanup_1;
	}

	sscanf(value_string, "%d", num_pos);
	positions = (float*)malloc(*num_pos*sizeof(float));
	if(positions == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}

	r = GetFloatData(pFile, positions, *num_pos);
	if(r != 0)
	{
		free(positions);
		r = -1;
		goto cleanup_1;
	}
	else
	{
		*pos_array = positions;
		r = 0;
	}

cleanup_1:
	free(tag_string);
	return r;
}

/*
GetPolylist() allocates and fills out the <p> array in the <polylist> element. This array
shows for each vertex which elements of the position and normals array it uses.
-If there is more than one <polylist> then this function will loop through each one.
*/
static int GetPolylist(FILE * pFile, int * num_inputs, int * num_polylist_verts, int ** polylist_out)
{
	int * polylist=0;
	int * polylist_end=0;
	char * tag_string=0;
	char * value_string=0;
	long bookmark_mesh_start;
	int r;
	int i;
	int num_vcount;
	int num_count;
	int sides;

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	value_string = tag_string+256;

	//start at the beginning of the file in case a function before left the
	//file somewhere.
	rewind(pFile);

	r = FindNextTag(pFile, "mesh", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	bookmark_mesh_start = ftell(pFile);

	//find how many inputs there are
	r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
	if(r == -1)
	{
		r = -1;
		printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup;
	}
	*num_inputs = 0;
	while(1)
	{
		r = FindChildElement(pFile, "input", tag_string, "/polylist");
		if(r == -1)
			break;
		else
			*num_inputs += 1;
	}

	//first step is to figure out how large the <p> array is.
	fseek(pFile, bookmark_mesh_start, SEEK_SET);
	*num_polylist_verts = 0;
	while(1)
	{
		r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
		if(r < 0)
		{
			break;
		}
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r < 0)
		{
			printf("%s: error count attribute not in <polylist>\n", __func__);
			goto cleanup;
		}
		sscanf(value_string, "%d", &num_vcount);

		if(num_vcount < 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		r = FindChildElement(pFile, "vcount", tag_string, "/polylist");
		if(r < 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}

		for(i = 0; i < num_vcount; i++)
		{
			fscanf(pFile, "%d", &sides);
			*num_polylist_verts += sides;
		}
	}

	//num_polylist_verts now contains the number of final vertices (so <p> is twice this #, since there is an index for position then for normal)
	//now allocate memory for the polylist array
	polylist = (int*)malloc(*num_polylist_verts*(*num_inputs)*sizeof(int));
	if(polylist == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	polylist_end = polylist;
	
	fseek(pFile, bookmark_mesh_start, SEEK_SET);
	
	//now find the <p> element and fill in the polylist array
	while(1)
	{
		r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
		if(r == -1)
			break;
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r == -1)
		{
			r = -1;
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		sscanf(value_string, "%d", &num_count);

		r = FindChildElement(pFile, "p", tag_string, "/polylist");
		if(r < 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		r = GetIntArray(pFile, (num_count*3*(*num_inputs)), polylist_end); //3 = triangles. Assume we are doing triangles.
		if(r < 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}

		//calculate the new polylist_end
		polylist_end = polylist_end + (num_count*3*(*num_inputs));
	}

	*polylist_out = polylist;

	r = 0;

cleanup:
	free(tag_string);
	return r;
}

/*
GetNumPolylist() returns the # of polylists in the dae file
*/
static int GetNumPolylist(FILE * pFile)
{
	char * tag_string=0;
	char * value_string=0;
	int num_polylists;
	int r;

	//rewind to the beginning
	fseek(pFile, 0, SEEK_SET);

	//setup temporary strings
	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;
	tag_string[0] = '\x00';

	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//count polylists
	num_polylists = 0;
	while(FindChildElement(pFile, "polylist", tag_string, "/mesh") == 0)
	{
		num_polylists += 1;
	}
	r = num_polylists;

cleanup:
	free(tag_string);
	fseek(pFile, 0, SEEK_SET);
	return r;	
}

static int GetPositionArraySize(FILE * pFile)
{
	char * tag_string=0;
	char * value_string=0;
	char * id_string=0;
	long bookmark_library_geometries;
	int r;
	int r2;
	int num_pos=0;

	tag_string = (char*)malloc((256*3));
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;
	id_string = tag_string + (2*256);
	tag_string[0] = '\x00';
	value_string[0] = '\x00';

	//Find <library_geometries>
	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	bookmark_library_geometries = ftell(pFile);

	r = FindNextTag(pFile, "vertices", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}

	r = FindChildElement(pFile, "input", tag_string, "/vertices");
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	
	r = GetElementAttribute(tag_string, "source", value_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	//value_string now contains the id of the positions array, which will be a <source> element.
	strcpy(id_string, (value_string+1)); //+1 because value_string starts with a '#'.

	fseek(pFile, bookmark_library_geometries, SEEK_SET);
	r = 0;
	while(r == 0)
	{
		r = FindChildElement(pFile, "source", tag_string, "/mesh");
		if(r == 0)
		{
			r2 = GetElementAttribute(tag_string, "id", value_string);
			if(r2 == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup_1;
			}
			//if not the id we are looking for, go to the next one.
			if(strcmp(id_string, value_string) != 0)
			{
				continue;
			}
			else
			{
				r = 0;
				break;
			}
		}
	}
	//r will be 0 or -1
	if(r == -1)
	{
		goto cleanup_1;
	}
	
	//so now we are at a <source> with an id that matches the POSITION source.
	//find the float_array
	r = FindChildElement(pFile, "float_array", tag_string, "/source");
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	sscanf(value_string, "%d", &num_pos);

	//check that num_pos is divisible by 3.
	if(num_pos % 3 != 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_1;
	}
	r = num_pos/3;
	//success set r to # of position vectors.

cleanup_1:
	free(tag_string);
	return r;	
}

/*Load_DAE_GetNumAnimationFrames() returns the # of frames in the animation elements
in the library_animations.
*/
int Load_DAE_GetNumAnimationFrames(char * filename)
{
	FILE * pFile = 0;
	char * tag_string=0;
	char * value_string=0;
	long last_animation_bookmark;
	int num_frames = -1;
	int cur_frames = -1;
	int i_anim = 0;
	int r;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error could not find %s\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	r = FindNextTag(pFile, "library_animations", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup_ok;
	}
	
	//go through each animation element making note of the # of frames.
	r = 0;
	while(r == 0)
	{
		//find the next animation element.
		r = FindChildElement(pFile, "animation", tag_string, "/library_animations");
		if(r == 0)
		{
			//now search the animation element's children for the param element that
			//is time.
			last_animation_bookmark = ftell(pFile);
			r = FindChildElement(pFile, "param", tag_string, "/animation");
			if(r == 0)
			{
				r = GetElementAttribute(tag_string, "name", value_string);
				if(r == 0)
				{
					if(strcmp(value_string, "TIME") == 0)
					{
						fseek(pFile, last_animation_bookmark, SEEK_SET);
						r = FindChildElement(pFile, "float_array", tag_string, "/animation");
						if(r == 0)
						{
							//found the float_array now check how many frames it has
							r = GetElementAttribute(tag_string, "count", value_string);
							if(r == 0)
							{
								sscanf(value_string, "%d", &cur_frames);
								if(num_frames == -1)
								{
									num_frames = cur_frames;
								}
								else
								{
									if(num_frames != cur_frames)
									{
										printf("%s: i_anim=%d frames mismatch. has %d frames, expected %d frames.\n", 
											__func__, 
											i_anim, 
											cur_frames, 
											num_frames);
										r = -1;
										goto cleanup_ok;
									}

									//increment our tracking of how many animations we have
									//processed.
									i_anim += 1;
								}
							}
						}
					}
				}
			}
		}
	}

	r = num_frames;

cleanup_ok:
	free(tag_string);
	fclose(pFile);
	return r;
}

/*
Load_DAE_GetAnimationTransforms() loads bone transform keyframes from <animation> elements.
//TODO: Fix code it is stupid
-Assumes all bone transforms have the same # of keyframes
-Assumes the source elements within an animation element come in a specific order: TIME then TRANSFORM.
*/
int Load_DAE_GetAnimationTransforms(char * filename, int num_bones, int num_frames, struct dae_animation_struct * anim_info)
{
	FILE * pFile = 0;
	char * tag_string=0;
	char * value_string=0;
	char * bone_names=0;
	float * float_frame_times=0;
	long bookmark_float_array;
	int i;
	int j;
	int r;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error file %s not found.\n", __func__, filename);
		return -1;
	}

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		fclose(pFile);
		return -1;
	}
	value_string = tag_string + 256;

	memset(anim_info, 0, sizeof(struct dae_animation_struct));
	anim_info->num_frames = num_frames;

	//allocate the time array before filling it in from the file
	float_frame_times = (float*)malloc(num_frames*sizeof(float));
	if(float_frame_times == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	
	anim_info->key_frame_times = (int*)malloc(num_frames*sizeof(int));
	if(anim_info->key_frame_times == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	
	anim_info->bone_frame_transform_mat4_array = (float*)malloc(num_bones*num_frames*16*sizeof(float));
	if(anim_info->bone_frame_transform_mat4_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//Setup a string of bone names so we can skip animations that aren't for
	//bones.
	r = Load_DAE_GetBoneNames(pFile, num_bones, &bone_names); 
	if(r != 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	
	r = FindNextTag(pFile, "library_animations", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	for(i = 0; i < num_bones; i++)
	{
		r = FindChildElement(pFile, "animation", tag_string, "/library_animations");
		if(r == -1)
		{
			//this is unexpected. Should find the same number of <animation> elements as there are bones.
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		//Check that a bone name is found in the id of the animation.
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		if(CheckNamesInString(value_string, num_bones, bone_names) == 0)
			continue; //TODO: since this is in a for loop, continuing just lost us an iteration for a bone that wasn't loaded

		r = FindChildElement(pFile, "float_array", tag_string, "/source");
		if(r == -1)
			continue;
		bookmark_float_array = ftell(pFile);
		r = FindChildElement(pFile, "param", tag_string, "/source");
		if(r == -1)
			continue;
		r = GetElementAttribute(tag_string, "name", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		if(strcmp(value_string, "TIME") == 0)
		{
			fseek(pFile, bookmark_float_array, SEEK_SET);

			r = GetFloatData(pFile, float_frame_times, num_frames);
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}

			//These frame times in the dae file are in seconds and are floats. Convert them
			//to integers.
			for(j = 0; j < num_frames; j++)
			{
				//assume that the dae file was made with blender using a default of 24 frames-per-second.
				anim_info->key_frame_times[j] = (int)(float_frame_times[j]*60.0f);
			}
		}
		else
		{
			//print an error because we expected TIME first.
			printf("%s: error line %d\n", __func__, __LINE__);
		}

		//Now that we have time, we expect that the bone transforms will be next as the TRANSFORM param.
	   	//get the next source element in the animation
		r = FindChildElement(pFile, "source", tag_string, "/animation");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		r = FindChildElement(pFile, "float_array", tag_string, "/source");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		bookmark_float_array = ftell(pFile);
		r = FindChildElement(pFile, "param", tag_string, "/source");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		r = GetElementAttribute(tag_string, "name", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		if(strcmp(value_string, "TRANSFORM") != 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		fseek(pFile, bookmark_float_array, SEEK_SET);

		r = GetFloatData(pFile, (anim_info->bone_frame_transform_mat4_array+(i*num_frames*16)), (num_frames*16));
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//indicate success
	r = 0;

cleanup:
	if(bone_names != 0)
		free(bone_names);
	free(float_frame_times);
	free(tag_string);
	fclose(pFile);
	return r;
}

int Load_DAE_GetBoneNames(FILE * pFile, int num_bones, char ** bone_name_array)
{
	char * tag_string=0;
	char * value_string=0;
	char * name_string=0;
	long bookmark;
	char c=0;
	int r;
	int i;
	int num_names=0;
	int len=0;

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	fseek(pFile, 0, SEEK_SET);

	r = FindNextTag(pFile, "library_controllers", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	
	r = FindNextTag(pFile, "controller", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	while(r == 0)
	{
		r = FindChildElement(pFile, "source", tag_string, "/controller");
		if(r != 0)
			break;
		r = FindChildElement(pFile, "Name_array", tag_string, "/source");
		if(r == 0)
		{
			//exit the loop since we have found the name string
			r = 0;
			break;
		}
	}
	if(r != 0) //exit if we didn't find the Name_array for the joints.
		goto cleanup;

	//get the # of names in the string
	r = GetElementAttribute(tag_string, "count", value_string);
	if(r != 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//save the place in the file stream so we can go back after
	//we count the number of characters.
	bookmark = ftell(pFile);

	//get a count of how big the name string is.
	while(1)
	{
		c = fgetc(pFile);
		if(c == '<')
			break;
		else
			len += 1;
	}

	if(len == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	name_string = (char*)malloc(len);
	if(name_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	fseek(pFile, bookmark, SEEK_SET);
	for(i = 0; i < len; i++)
	{
		c = fgetc(pFile);
		name_string[i] = c;
	}
	*bone_name_array = name_string;

cleanup:
	free(tag_string);
	fseek(pFile, 0, SEEK_SET);
	return r;
}

/*
CheckNamesInString() checks if one of the names in the name_array match
a part of the string parameter.
-Assume name_array is a string that consists of names separated by
spaces.
-Returns:
	0 = no names are found in the string
	1 = a name was found.
*/
static int CheckNamesInString(char * string, int num_bones, char * name_array)
{
	int i;
	int j_start=0;
	int j_end=0;
	char temp_c;

	for(i = 0; i < num_bones; i++)
	{
		//find the next space or null-termination in the name_array.
		while(1)
		{
			if(name_array[j_end] != '\x20' && name_array[j_end] != 0)
			{
				j_end += 1;
			}
			else
			{
				break;
			}
		}

		//save the character at the end and replace it a null-termination so
		//we can use the string.
		temp_c = name_array[j_end];
		name_array[j_end] = 0;

		if(strstr(string, (name_array+j_start)) != 0)
		{
			name_array[j_end] = temp_c;
			return 1;
		}

		name_array[j_end] = temp_c;
		j_start = j_end + 1;
		j_end += 1;
	}

	return 0;
}

/*
GetNumMaterials() returns the # of <material> elements in the dae file.
-saves current file-offset and restores it on return
returns:
	0 = success
	-1 = fail
*/
static int GetNumMaterials(FILE * pFile, int * num_materials)
{
	long save_filepos;
	char * tag_string=0;
	char * value_string=0;
	int r;

	//save the current file position so we can restore it once we are done counting materials.
	save_filepos = ftell(pFile);

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	rewind(pFile);

	r = FindNextTag(pFile, "library_materials", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	*num_materials = 0;
	while(1)
	{
		r = FindChildElement(pFile, "material", tag_string, "/library_materials");
		if(r == -1)
			break;
		*num_materials += 1;
	}
	r = 0;

cleanup:
	free(tag_string);
	fseek(pFile, save_filepos, SEEK_SET);
	return r;
}

/*
GetMaterials() fills in the materials array with diffuse colors.
returns:
	0 = success
	-1 = any failure
*/   
static int GetMaterials(FILE * pFile, int num_materials, struct dae_material_struct * materials)
{
	long save_filepos;
	long bookmark;
	char * tag_string=0;
	char * value_string=0;
	char * id_string=0;
	int i;
	int r;
	int found_effect; //flag used when searching for an effect element of a material.

	save_filepos = ftell(pFile);

	tag_string = (char*)malloc(768);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	value_string = tag_string + 256;
	id_string = tag_string + 512;

	//go back to the start of the file so we can find the <material> elements
	rewind(pFile);
	r = FindNextTag(pFile, "library_materials", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		goto cleanup;
	}
	
	for(i = 0; i < num_materials; i++)
	{
		r = FindChildElement(pFile, "material", tag_string, "/library_materials");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}

		//look for the 'id' attribute and save the name. The polylist will
		//reference this name for the material.
		r = GetElementAttribute(tag_string, "id", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		materials[i].id = (char*)malloc(strlen(value_string)+1);
		if(materials[i].id == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		strcpy(materials[i].id, value_string);

		//now go and find the <effect> element associated with the material.
		bookmark = ftell(pFile);
		r = FindNextTag(pFile, "instance_effect", tag_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		r = GetElementAttribute(tag_string, "url", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		rewind(pFile);
		r = FindNextTag(pFile, "library_effects", tag_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			goto cleanup;
		}
		found_effect = 0;
		while(1)
		{
			r = FindChildElement(pFile, "effect", tag_string, "/library_effects");
			if(r == -1)
				break;
			r = GetElementAttribute(tag_string, "id", id_string); //use a different string here since we need to match it with name_string
			if(r == -1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				goto cleanup;
			}
			if(strcmp((value_string+1), id_string) == 0)
			{
				//found the effect
				r = FindChildElement(pFile, "diffuse", tag_string, "/effect");
				if(r == -1)
				{
					printf("%s: error line %d\n", __func__, __LINE__);
					goto cleanup;
				}
				r = FindChildElement(pFile, "color", tag_string, "/diffuse");
				if(r == -1)
				{
					printf("%s: error line %d\n", __func__, __LINE__);
					goto cleanup;
				}
				r = GetFloatData(pFile, materials[i].diffuse, 3);
				if(r == -1)
				{
					printf("%s: error line %d\n", __func__, __LINE__);
					goto cleanup;
				}
				
				//we found the color information for the material. signal that
				//it is found and break the while loop.
				found_effect = 1;
				break;
			}
			else
			{
				continue; //keep looking for the effect
			}
		}
		
		//color is loaded go back to where we were in the list of materials
		fseek(pFile, bookmark, SEEK_SET);
	}

	//indicate success. materials array is now filled with color information.
	r = 0;

cleanup:
	fseek(pFile, save_filepos, SEEK_SET);
	if(tag_string != 0)
		free(tag_string);
	return r;
}

void Load_DAE_Convert_Matrix_Orientations(struct dae_model_bones_struct * bones, struct dae_animation_struct * anim, int num_anims)
{
	int i;
	int j;

	for(i = 0; i < bones->num_bones; i++)
	{
		Convert_Mat_From_DAE_To_GL(bones->inverse_bind_mat4_array+(i*16));

		//bone_transform_mat4_array isn't used anymore
		//if(bones->bone_transform_mat4_array != 0)
		//{
		//	Convert_Mat_From_DAE_To_GL(bones->bone_transform_mat4_array+(i*16));
		//}
	}

	Convert_Mat_From_DAE_To_GL(bones->bind_shape_mat4);
	Convert_Mat_From_DAE_To_GL(bones->armature_mat4);

	for(j = 0; j < num_anims; j++)
	{
		for(i = 0; i < (bones->num_bones*anim[j].num_frames); i++)
		{
			Convert_Mat_From_DAE_To_GL(anim[j].bone_frame_transform_mat4_array+(i*16));
		}
	}
}

static void Convert_Mat_From_DAE_To_GL(float * mat4)
{
	float rot_dae_to_gl[16];
	float rot_gl_to_dae[16];

	mmRotateAboutX(rot_dae_to_gl, -90.0f);
	mmRotateAboutX(rot_gl_to_dae, 90.0f);

	//The matrices are in DAE coordinates, so need to sandwich them with
	//transforms that go from GL to DAE and DAE back to GL
	mmMultiplyMatrix4x4(mat4, rot_gl_to_dae, mat4);
	mmMultiplyMatrix4x4(rot_dae_to_gl, mat4, mat4);
}

/*
Load_DAE_ExtraAnimInfoFile() assumes that the filename passed is an extra text
file that I made that says what keyframes go to which animation. I made this extra
file because I can only export one animation into a DAE, so all the keyframes
are together.
//TODO: Remove the temporary arrays in dae_extra_anim_info_struct, keep anim_keyframes table
*/
int Load_DAE_ExtraAnimInfoFile(char * filename, struct dae_extra_anim_info_struct * info)
{
	char temp_string[32];
	FILE* pFile;
	struct dae_keyframe_info_struct * keyframes_list=0;	//temp array used when looping. (no malloc for this)
	int * masterkeyframe_list=0;	//list holds master keyframes.
	struct dae_keyframe_info_struct * p_keyframe_info=0;
	int flag_skipline=0;
	int num_lines=0; //
	int num_animations=1;
	int temp_keyframe;
	int temp_anim;
	int cur_anim=0; //current animation
	int i_col;		//what column we are on in the .txt file
	int i;
	int j;
	char c;

	pFile = fopen(filename, "r");
	if(pFile == 0)
	{
		printf("%s: error. file %s not found.\n", __func__, filename);
		return -1;
	}

	//Count # of lines to get a size to use to allocate
	//arrays in the dae_extra_anim_info_struct
	while(1)
	{
		c = fgetc(pFile);
		if(feof(pFile) != 0)
			break;

		if(c == ';')
		{
			flag_skipline = 1;
			continue;
		}

		if(c == '\n' && flag_skipline == 1)
		{
			flag_skipline = 0;
			continue;
		}

		if(c == '\n')
		{
			num_lines += 1;
		}
	}

	//allocate memory in the dae_extra_anim_info_struct
	info->total_keyframes = num_lines;
	info->keyframe_list = (int*)malloc(num_lines*sizeof(int));
	if(info->keyframe_list == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	info->anim_id_list = (int*)malloc(num_lines*sizeof(int));
	if(info->anim_id_list == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	masterkeyframe_list = (int*)malloc(num_lines*sizeof(int));
	if(masterkeyframe_list == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	memset(info->keyframe_list, 0, num_lines*sizeof(int));
	memset(info->anim_id_list, 0, num_lines*sizeof(int));
	memset(masterkeyframe_list, 0, num_lines*sizeof(int));

	//go back and load in the data
	j = 0; //this index is used with the keyframe_list and anim_id_list
	i = 0;
	rewind(pFile);
	while(1)
	{
		c = fgetc(pFile);

		//break if end of file
		if(feof(pFile) != 0)
			break;

		if(flag_skipline == 1)
		{
			if(c == '\n')
			{
				flag_skipline = 0;
				continue;
			}
			else
			{
				continue;
			}
		}

		if(c == ';')
		{
			flag_skipline = 1;
			continue;
		}

		//when we get to a space or newline load in whatever
		//is in the string
		if(c == '\x20' && i != 0)
		{
			temp_string[i] = '\x00';

			switch(i_col)
			{
			case 0:
				sscanf(temp_string, "%d", &temp_anim);
				info->anim_id_list[j] = temp_anim;

				if(temp_anim != cur_anim)
				{
					num_animations += 1;
					cur_anim = temp_anim;
				}
				
				break;
			case 1:
				sscanf(temp_string, "%d", &temp_keyframe);
				masterkeyframe_list[j] = temp_keyframe;
			}

			//reset where we are putting new characters
			i_col += 1;
			i = 0;
			continue;
		}
		if(c == '\x20')
		{
			continue;
		}

		if(c == '\n')
		{
			temp_string[i] = '\x00';
			sscanf(temp_string, "%d", &temp_keyframe);
			
			info->keyframe_list[j] = temp_keyframe;

			//reset where we are putting new characters
			i = 0;
			i_col = 0;
			j += 1; //go to the next index in the keyframe_list and anim_id_list.
			continue;
		}

		//if just a normal char load it up.
		temp_string[i] = c;
		i += 1;
	}

	info->num_animations = num_animations;

	//save how many keyframes each animation has
	info->num_keyframes = (int*)malloc(info->num_animations*sizeof(int));
	if(info->num_keyframes == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	memset(info->num_keyframes, 0, (info->num_animations*sizeof(int)));

	//loop through the anim_id_list sum the different animations
	//found.
	cur_anim = 0;
	j = 0;
	for(i = 0; i < info->total_keyframes; i++)
	{
		if(info->anim_id_list[i] != cur_anim)
		{
			j += 1;
			cur_anim = info->anim_id_list[i];
		}
		
		info->num_keyframes[j] += 1;
		
	}

	//convert keyframe information into anim_keyframes 2d array
	info->anim_keyframes = (struct dae_keyframe_info_struct**)malloc(info->num_animations*sizeof(struct dae_keyframe_info_struct*));
	if(info->anim_keyframes == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	memset(info->anim_keyframes, 0, (info->num_animations*sizeof(struct dae_keyframe_info_struct*)));

	//allocate space for the anim_keyframes 2d array
	for(i = 0; i < info->num_animations; i++)
	{
		info->anim_keyframes[i] = (struct dae_keyframe_info_struct*)malloc(info->num_keyframes[i]*sizeof(struct dae_keyframe_info_struct));
		if(info->anim_keyframes[i] == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			return -1;
		}
		memset(info->anim_keyframes[i], 0, (info->num_keyframes[i]*sizeof(struct dae_keyframe_info_struct)));
	}

	//fill in keyframes index into the 2d anim_keyframes array
	j = 0;
	for(i = 0; i < info->total_keyframes; i++)
	{
		keyframes_list = info->anim_keyframes[(info->anim_id_list[i])];
		p_keyframe_info = (keyframes_list+j);
		p_keyframe_info->i_master_keyframe = masterkeyframe_list[i]; //save the overall keyframe index in this table
		p_keyframe_info->time = info->keyframe_list[i];
		j += 1;

		//If we recorded all the animations keyframes, skip to
		//the next one.
		if(j >= info->num_keyframes[(info->anim_id_list[i])])
		{
			j = 0;
		}
	}

	free(masterkeyframe_list);
	fclose(pFile);
	return 0;
}

int Load_DAE_Polylists(FILE * pFile, struct dae_polylists_struct * polylists)
{
	char * tag_string=0;
	char * value_string=0;
	int * indices;
	long bookmark_polylist;
	int num_polylists;
	int num_triangles;
	int num_indices;
	int index;
	int i;
	int j;
	int r;

	tag_string = (char*)malloc(512);
	if(tag_string == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}
	value_string = tag_string + 256;

	num_polylists = GetNumPolylist(pFile);
	if(num_polylists <= 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
	}

	memset(polylists, 0, sizeof(struct dae_polylists_struct));
	polylists->num_polylists = num_polylists;

	polylists->polylist_len = (int*)malloc(num_polylists*sizeof(int));
	if(polylists->polylist_len == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	memset(polylists->polylist_len, 0, (num_polylists*sizeof(int)));

	polylists->polylist_indices = (int**)malloc(num_polylists*sizeof(int*));
	if(polylists->polylist_indices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	r = FindNextTag(pFile, "library_geometries", tag_string);
	if(r == -1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	for(i = 0; i < num_polylists; i++)
	{
		//look for a polylist
		r = FindChildElement(pFile, "polylist", tag_string, "/mesh");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		bookmark_polylist = ftell(pFile);
		r = GetElementAttribute(tag_string, "count", value_string);
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		sscanf(value_string, "%d", &num_triangles);
		num_indices = num_triangles*3*4;	//3 = # of verts in a triangle, 4= # of properties in a single vert (pos,normal,texcoord,color)
		polylists->polylist_len[i] = num_indices;

		//Use vcount to make sure the polylist contains only triangles
		r = FindChildElement(pFile, "vcount", tag_string, "/polylist");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		r = CheckVcount(pFile);
		if(r != 0) //CheckVcount() returns 0 if vcount has all 3's
		{
			printf("%s: error. <vcount> of polylist[%d] not all triangles.\n", __func__, i);
			r = -1;
			goto cleanup;
		}

		//go back to the start of the polylist and find the <p> element
		fseek(pFile, bookmark_polylist, SEEK_SET);
		r = FindChildElement(pFile, "p", tag_string, "/polylist");
		if(r == -1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		//allocate polylist indices array
		indices = (int*)malloc(num_indices*sizeof(int));
		if(indices == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		for(j = 0; j < num_indices; j++)
		{
			fscanf(pFile, "%d", (indices+j));
		}

		//save the newly created <p> element in the polylists struct
		polylists->polylist_indices[i] = indices;
	}
	r = 0;

cleanup:
	free(tag_string);
	return r;
}

/*
Loads a custom binary format written by the soldier converter utility program in
resources/utilities/soldier_dae_converter.
File consists of:
	Header:
		1. string SOLDIER
		2. num_materials
		3. num_verts
		4. num_indices
		5. floats_per_vert
		6. offset to start of vertex data
		7. offset to start of indices
		8. offset to start of base index offsets & mesh counts
*/
int Load_DAE_CustomBinaryModel(char * filename, struct dae_model_info2 * model_info)
{
	FILE* pFile=0;
	long lbookmarkOffsets;
	unsigned int offset;
	char temp_string[8];
	int i;
	int r;

	memset(model_info, 0, sizeof(struct dae_model_info2));
	
	pFile = fopen(filename, "rb");
	if(pFile == 0)
	{
		printf("%s: error could not open %s\n", __func__, filename);
		return -1;
	}
	r = fread(temp_string, 1, 7, pFile);
	if(r != 7)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	temp_string[7] = '\x00';
	if(strcmp(temp_string, "SOLDIER") != 0)
	{
		printf("%s: error. file doesn't start with SOLDIER string.\n", __func__);
		r = -1;
		goto cleanup;
	}
	r = fread(&(model_info->num_materials), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = fread(&(model_info->num_verts), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = fread(&(model_info->num_indices), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	r = fread(&(model_info->floats_per_vert), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	lbookmarkOffsets = ftell(pFile);

	//allocate space for memory arrays
	model_info->vert_data = (float*)malloc(model_info->num_verts*model_info->floats_per_vert*sizeof(float));
	if(model_info->vert_data == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	model_info->vert_indices = (int*)malloc(model_info->num_verts*sizeof(int));
	if(model_info->vert_indices == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	model_info->base_index_offsets = (int*)malloc(model_info->num_materials*sizeof(int));
	if(model_info->base_index_offsets == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	model_info->mesh_counts = (int*)malloc(model_info->num_materials*sizeof(int));
	if(model_info->mesh_counts == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//Go to offsets table and read offset to vertices
	fseek(pFile, lbookmarkOffsets, SEEK_SET);
	r = fread(&offset, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}	
	fseek(pFile, (long)offset, SEEK_SET);
	for(i = 0; i < (model_info->num_verts*model_info->floats_per_vert); i++)
	{
		r = fread((model_info->vert_data+i), 4, 1, pFile);
	   	if(r != 1)
		{
			printf("%s: error. fread() fail at i=%d vert data\n", __func__, i);
			r = -1;
			goto cleanup;
		}
	}

	//Go back to the offsets table
	//and go to indices array
	fseek(pFile, (lbookmarkOffsets+4), SEEK_SET); //advance by 4 to go to indices offset
	r = fread(&offset, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	fseek(pFile, (long)offset, SEEK_SET);
	for(i = 0; i < model_info->num_indices; i++)
	{
		r = fread((model_info->vert_indices+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error. fread() fail at i%d index data\n", __func__, i);
			r = -1;
			goto cleanup;
		}
	}

	//Go back to offsets table and shift to base vertex array and mesh count
	fseek(pFile, (lbookmarkOffsets+8), SEEK_SET); //advance by 8 to go to base vert offset
	r = fread(&offset, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	fseek(pFile, (long)offset, SEEK_SET);
	for(i = 0; i < model_info->num_materials; i++)
	{
		r = fread((model_info->base_index_offsets+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}
	for(i = 0; i < model_info->num_materials; i++)
	{
		r = fread((model_info->mesh_counts+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//read complete. return OK
	r = 0;

cleanup:
	fclose(pFile);
	return r;
}

/*
LoadTextureNames() loads texture names from soldier_textures.txt
*/
int Load_DAE_CustomTextureNames(struct dae_texture_names_struct * texture_names)
{
	char temp_string[128];
	FILE* pFile=0;
	int flag_skipline=0;
	int num_lines=0;
	int name_len;
	int i=0;
	int i_tex=0;
	int r;
	char c;

	pFile = fopen("soldier_textures.txt", "r");
	if(pFile == 0)
	{
		printf("%s: could not find %s\n", __func__, "soldier_textures.txt");
		return -1;
	}

	//loop to count # of lines
	//TODO: soldier_textures.txt ends with a new-line char even though this isnt shown in vi
	while(1)
	{
		c = fgetc(pFile);
		if(feof(pFile) != 0)
		{
			break;
		}

		if(c == ';')
		{
			flag_skipline = 1;
			continue;
		}

		if(c == '\n' && flag_skipline == 1)
		{
			flag_skipline = 0;
			continue;
		}

		if(flag_skipline == 1)
			continue;

		if(c == '\n')
		{
			num_lines += 1;
		}
	}

	texture_names->num_textures = num_lines;
	texture_names->names = (char**)malloc(num_lines*sizeof(char*));
	if(texture_names->names == 0)
	{
		printf("%s: malloc fail.\n", __func__);
		r = -1;
		goto cleanup;
	}
	memset(texture_names->names, 0, (num_lines*sizeof(char*)));

	//loop and pull in texture names
	i = 0;
	flag_skipline = 0;
	rewind(pFile);
	while(1)
	{
		c = fgetc(pFile);
		if(feof(pFile) != 0)
			break;

		if((flag_skipline == 0 && c == '\x20') || (flag_skipline == 0 && c == '\n'))
		{
			if(c == '\x20' && i != 0)
			{
				temp_string[i] = '\x00';
				sscanf(temp_string, "%d", &i_tex);
				i = 0;
			}
			
			if(c == '\n')
			{
				//assumes i_tex has been parsed already
				temp_string[i] = '\x00';
				name_len = strlen(temp_string);
				name_len += 1;
				texture_names->names[i_tex] = (char*)malloc(name_len);
				strcpy(texture_names->names[i_tex], temp_string);
				i = 0;
			}

			continue;
		}

		if(c == ';')
		{
			flag_skipline = 1;
			continue;
		}

		if(c == '\n' && flag_skipline == 1)
		{
			flag_skipline = 0;
			continue;
		}

		if(flag_skipline == 1)
		{
			continue;
		}

		temp_string[i] = c;
		i += 1;
	}

	r = 0;
cleanup:
	fclose(pFile);
	return r;
}

/*
This function assumes a filename to a binary SOLANIM file and gets
the # of animations the file contains. This is so an proper sized array
can be passed to Load_DAE_CustomBinaryBones().
*/
int Load_DAE_BinaryGetNumAnims(char * filename, int * num_anims)
{
	char expected_start_string[8] = "SOLANIM";
	char temp_string[8];
	FILE * pFile=0;
	int r;

	pFile = fopen(filename, "rb");
	if(pFile == 0)
	{
		printf("%s: error. could not open %s\n", __func__, filename);
		return -1;
	}
	r = fread(temp_string, 1, 7, pFile);
	if(r != 7)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	temp_string[7] = '\x00';
	if(strcmp(temp_string, expected_start_string) != 0)
	{
		printf("%s: error. file %s magic sequence of %s incorrect.\n", __func__, filename, temp_string);
		r = -1;
		goto cleanup;
	}

	//go to the spot in the file where the # of anims is
	fseek(pFile, 15, SEEK_SET);
	r = fread(num_anims, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//[num_anims] will contain # of animation sequences
	r = 0; //success

cleanup:
	fclose(pFile);
	return r;
}

/*
This function reads a binary file who's name is passed via filename and fills in the info into the
dae_model_bones_struct and dae_animation_struct structures.
-returns:
	0 	;okay
	-1	;error
*/
int Load_DAE_CustomBinaryBones(char * filename, struct dae_model_bones_struct * bones, int num_anims, struct dae_animation_struct * anim)
{
	char expected_start_string[8] = "SOLANIM";
	char temp_string[8];
	char c_temp;
	FILE *pFile=0;
	long weightArrayOffset;
	long animStructsOffset;
	int temp;
	int num_floats;
	int i_anim;
	int i;
	int j;
	int r;

	pFile = fopen(filename, "rb");
	if(pFile == 0)
	{
		printf("%s: error. could not open %s\n", __func__, filename);
		return -1;
	}

	//initialize the dae_model_bones_struct and dae_animation_struct
	memset(bones, 0, sizeof(struct dae_model_bones_struct));
	memset(anim, 0, (num_anims*sizeof(struct dae_animation_struct)));

	//check that the first 7 bytes are the magic sequence
	r = fread(temp_string, 1, 7, pFile);
	if(r != 7)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	temp_string[7] = '\x00';
	if(strcmp(temp_string, expected_start_string) != 0)
	{
		printf("%s: error. file %s magic sequence of %s incorrect.\n", __func__, filename, temp_string);
		r = -1;
		goto cleanup;
	}

	//read the header data
	//1. num bones
	//2. num verts
	//3. num anim sequences
	//4. inverse_bind_mat4_array	;one mat4 for each bone
	//5. bind_shape_mat4
	//6. armature_mat4
	//7. bone_tree array
	//8. read offset for weight_array
	//9. read offset for dae_animation_structs
	//10. read weight_array
	//11. (dae_animation_struct) # of keyframes
	//12. (dae_animation_struct) # of floats in the bone_frame_transform_mat4_array

	//read num bones
	r = fread(&(bones->num_bones), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//read num verts
	r = fread(&(bones->num_verts), 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//read num anim sequences
	r = fread(&temp, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	if(temp != num_anims)
	{
		printf("%s: error. unexpected # of animation sequences. but found %d.\n", __func__, temp);
		r = -1;
		goto cleanup;
	}

	//read inverse_bind_mat4_array
	bones->inverse_bind_mat4_array = (float*)malloc(bones->num_bones*16*sizeof(float));
	if(bones->inverse_bind_mat4_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	for(i = 0; i < (bones->num_bones*16); i++)
	{
		r = fread((bones->inverse_bind_mat4_array+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}
	
	//read bind_shape_mat4
	for(i = 0; i < 16; i++)
	{
		r = fread((bones->bind_shape_mat4+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//read armature_mat4
	for(i = 0; i < 16; i++)
	{
		r = fread((bones->armature_mat4+i), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
	}

	//bone tree
	bones->bone_tree = (struct dae_bone_tree_leaf*)malloc(bones->num_bones*sizeof(struct dae_bone_tree_leaf));
	if(bones->bone_tree == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		return -1;
		goto cleanup;
	}
	memset(bones->bone_tree, 0, (bones->num_bones*sizeof(struct dae_bone_tree_leaf)));
	for(i = 0; i < bones->num_bones; i++)
	{
		//read the parent index
		c_temp = 0;
		temp = 0;
		r = fread(&c_temp, 1, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		temp = (int)c_temp;
		if(temp != -1)
		{
			bones->bone_tree[i].parent = (bones->bone_tree+temp);
		}
		else
		{
			bones->bone_tree[i].parent = 0;
		}

		//read the number of children the bone has
		temp = 0;
		r = fread(&temp, 1, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		bones->bone_tree[i].num_children = temp;
		
		//if there are children then their indices follow
		if(bones->bone_tree[i].num_children != 0)
		{
			bones->bone_tree[i].children = (struct dae_bone_tree_leaf**)malloc(bones->bone_tree[i].num_children*sizeof(struct dae_bone_tree_leaf*));
			if(bones->bone_tree[i].children == 0)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
			memset(bones->bone_tree[i].children, 0, (bones->bone_tree[i].num_children*sizeof(struct dae_bone_tree_leaf*)));

			for(j = 0; j < bones->bone_tree[i].num_children; j++)
			{
				temp = 0;
				r = fread(&temp, 1, 1, pFile);
				if(r != 1)
				{
					printf("%s: error line %d\n", __func__, __LINE__);
					r = -1;
					goto cleanup;
				}

				//check if we read in a child index of 0 that means that
				//something super bad happened
				if(temp == 0)
				{
					printf("%s: error. bone_tree[%d] child at index=%d is 0.\n", __func__, i, j);
					r = -1;
					goto cleanup;
				}
				bones->bone_tree[i].children[j] = (bones->bone_tree + temp);
			}
		}

	}

	//read offsets for weight array and dae_anim_structs
	weightArrayOffset = 0; //need to zero the high dword
	r = fread(&weightArrayOffset, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	animStructsOffset = 0;
	r = fread(&animStructsOffset, 4, 1, pFile);
	if(r != 1)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}

	//weight array
	bones->weight_array = (float*)malloc(bones->num_verts*bones->num_bones*sizeof(float));
	if(bones->weight_array == 0)
	{
		printf("%s: error line %d\n", __func__, __LINE__);
		r = -1;
		goto cleanup;
	}
	for(i = 0; i < bones->num_verts; i++)
	{
		for(j = 0; j < bones->num_bones; j++)
		{
			r = fread((bones->weight_array+(i*bones->num_bones)+j), 4, 1, pFile);
			if(r != 1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
	}

	for(i_anim = 0; i_anim < num_anims; i_anim++)
	{
		//number of keyframes in this dae_animation_struct
		r = fread(&(anim[i_anim].num_frames), 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		//number of floats in the bone_frame_transform_mat4_array
		r = fread(&num_floats, 4, 1, pFile);
		if(r != 1)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}

		//read key_frame_times array
		anim[i_anim].key_frame_times = (int*)malloc(anim[i_anim].num_frames*sizeof(int));
		for(i = 0; i < anim[i_anim].num_frames; i++)
		{
			r = fread((anim[i_anim].key_frame_times+i), 4, 1, pFile);
			if(r != 1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}

		//read dae_animation_struct bone_frame_transform_mat4_array
		//check num_floats is what we expect
		if(num_floats != (bones->num_bones*anim[i_anim].num_frames*16))
		{
			printf("%s: error. num_floats mismatch. read %d for num_floats but num_bones=%d num_frames=%d\n", __func__, num_floats, bones->num_bones, anim->num_frames);
			r = -1;
			goto cleanup;
		}
		anim[i_anim].bone_frame_transform_mat4_array = (float*)malloc(num_floats*sizeof(float));
		if(anim[i_anim].bone_frame_transform_mat4_array == 0)
		{
			printf("%s: error line %d\n", __func__, __LINE__);
			r = -1;
			goto cleanup;
		}
		for(i = 0; i < num_floats; i++)
		{
			r = fread((anim[i_anim].bone_frame_transform_mat4_array+i), 4, 1, pFile);
			if(r != 1)
			{
				printf("%s: error line %d\n", __func__, __LINE__);
				r = -1;
				goto cleanup;
			}
		}
	}

	//success
	r = 0;

cleanup:
	fclose(pFile);
	return r;
}

/*
This function checks the set of bone weights for all vertices and makes
sure that none are 0. (this indicates a problem)
returns:
	0 	;everything OK
	-1	;error
*/
int DAE_CheckVertsForZeroWeight(struct dae_model_bones_struct * bones)
{
	int i;
	int j;
	float totalWeight;

	for(i = 0; i < bones->num_verts; i++)
	{
		totalWeight = 0.0f;
		for(j = 0; j < bones->num_bones; j++)
		{
			totalWeight += bones->weight_array[(i*bones->num_bones)+j];
		}
		if(totalWeight < 0.9f) //if is_zero is still 1 then all the bone weights are 0
		{
			printf("%s: error. vertex %d has no weight set for any bones.\n", __func__, i);
			return -1;
		}
	}

	return 0;
}

/*
This function gets a vector that represents the front
face of a triangle
*/
void Load_DAE_GetTriangleFaceVector(float * a, float * b, float * c, float * normal_out)
{
	float ab_vec[3];
	float bc_vec[3];

	vSubtract(ab_vec, b, a);
	vSubtract(bc_vec, c, b);

	vCrossProduct(normal_out, ab_vec, bc_vec);
	vNormalize(normal_out);

}
