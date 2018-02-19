#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

#include "my_tga_2.h"

int LoadTga(char* filename, image_t *p)
{
	FILE* pFile;
	unsigned char temp;
	int i;
	
	pFile = fopen(filename, "rb");
	if(pFile == 0)
	{
		printf("loadTga: could not open file.\n");
		return 0;
	}
	
	//Get the image header
	fread(&(p->info.id_length), sizeof(unsigned char), 1, pFile);
	fread(&(p->info.color_map_type), sizeof(unsigned char), 1, pFile);
	fread(&(p->info.image_type), sizeof(unsigned char), 1, pFile);
	fread(&(p->info.color_map_first_entry), sizeof(short int), 1, pFile);
	fread(&(p->info.color_map_length), sizeof(short int), 1, pFile);
	fread(&(p->info.color_map_entry_size), sizeof(unsigned char), 1, pFile);
	fread(&(p->info.x_origin), sizeof(short int), 1, pFile);
	fread(&(p->info.y_origin), sizeof(short int), 1, pFile);
	fread(&(p->info.width), sizeof(short int), 1, pFile);
	fread(&(p->info.height), sizeof(short int), 1, pFile);
	fread(&(p->info.pixel_depth), sizeof(unsigned char), 1, pFile);
	fread(&(p->info.image_descriptor), sizeof(unsigned char), 1, pFile);
	
	p->info.components = p->info.pixel_depth / 8;
	p->info.bytes = p->info.width * p->info.height * p->info.components;

	//the callers of this func usually set this themselves
	if(p->info.image_type == 2) //image is RGB type
	{
		if(p->info.pixel_depth == 24)
			p->info.tgaColorType = GL_RGB;
		else if(p->info.pixel_depth == 32)
			p->info.tgaColorType = GL_RGBA;
		else
			return 0;
	}
	else if(p->info.image_type == 3) //image is black and white
	{
		if(p->info.pixel_depth == 8)
			p->info.tgaColorType = GL_RED;
		else
			return 0;
	}
	else
	{
		printf("loadTga: unsupported RGBA format. pixel depth is %d\n", p->info.tgaColorType);
		return 0;
	}
	
	//Get the image data assuming image_type is 2
	p->data = (unsigned char*)malloc(p->info.bytes);
	if(p->data == 0)
	{
		printf("loadTga: malloc failed to allocate for p->data.\n");
		return 0;
	}
	memset(p->data, 0, p->info.bytes); //calloc()? whatever! that's how i roll
	
	fread(p->data, sizeof(unsigned char), p->info.bytes, pFile);

	//tga file stores as BGR(A), switch it to RGB(A) ... which is dumb because I could just tell OpenGL that its GL_BGRA
	if(p->info.image_type == 2)
	{
		for(i = 0; i < p->info.bytes; i += p->info.components)
		{
			temp = p->data[i];
			p->data[i] = p->data[i+2];
			p->data[i+2] = temp;
		}
	}

	fclose(pFile);
	
	//free? no no! don't free() yet because we haven't called glTexImage2D to load it into OpenGL
	return 1;
}

/*
TGA images:
Uncompressed 24-bit TGA images are relatively simple compared to
several other prominent 24-bit storage formats: A 24-bit TGA 
contains only an 18-byte header followed by the image data as 
packed RGB data. In contrast, BMP requires padding rows to 4-byte
boundaries, TIFF and PNG are metadata containers that do not place
the image data or attributes at a fixed location within the file.
*/
/*
Targa format Data Type field (one byte binary):
0 - No image data included.
1 - uncompressed, color-mapped images.
2 - uncompressed, RGB images.
3 - uncompressed, black and white images.
9 - Runlength encoded color-mapped images
10 - Runlength encoded RGB images.
11 - Compressed, black and white images.
32 - Compressed color-mapped data, using Huffman, Delta, and runlength encoding
33 - Compressed color-mapped data, using Huffman, Delta, and runlength encoding. 4-pass quadtree-type process
*/
/*
Header information of kikko.tga:
TGA Header:
	idlength = 0
	datatypecode = 2 //uncompressed RGB image
	width = 64
	height= 64
	bitsperpixel = 24 //24 bits per pixel
*/

int DisplayTGAHeader(image_t *p)
{
	if(p == 0)
	{
		printf("DisplayTGAHeader: image_t* p is NULL\n");
		return 1;
	}
	printf("TGA Header:\n");
	printf("\tidlength = %d\n", p->info.id_length);
	printf("\tdatatypecode = %d\n", p->info.image_type);
	printf("\twidth = %d\n", p->info.width);
	printf("\theight= %d\n", p->info.height);
	printf("\tbitsperpixel = %d\n", p->info.pixel_depth);
	return 0;
}

int IsTgaAllowedSize(TgaHeader * p)
{
	int num_allowed_sizes = 2;
	int allowed_size[2] = {512,1024};
	int i;
	
	for(i = 0; i < num_allowed_sizes; i++)
	{
		if(p->width == allowed_size[i] && p->height == allowed_size[i])
			return 1;
	}
	return 0;
}
/*
Mipmap Sizes:
Each mipmap is half as small as the previous one.
0: 1024x1024
1: 512x512
2: 256x256
3: 128x128
4: 64x64
5: 32x32
6: 16x16
7: 8x8
8: 4x4
9: 2x2
10: 1x1
*/
