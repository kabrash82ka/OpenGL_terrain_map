#ifndef MY_TGA_H
#define MY_TGA_H

/*TGA header*/
typedef struct
{
	unsigned char 	id_length;	//length of the string located after the header.
	unsigned char 	color_map_type;
	unsigned char 	image_type;	//data type code. Specifies type of data after the header.
	
	short int 	color_map_first_entry;
	short int 	color_map_length;
	unsigned char 	color_map_entry_size;
	
	short int 	x_origin;
	short int	y_origin;
	short int	width;
	short int	height;
	unsigned char	pixel_depth;	//specifies size of each color value in bits.
	unsigned char 	image_descriptor;
	
	int 		components;
	int 		bytes;
	
	GLenum		tgaColorType;
} TgaHeader;

typedef struct
{
	TgaHeader 	info;
	unsigned char* 	data;//image data
} image_t;

int LoadTga(char* filename, image_t *p);
int DisplayTGAHeader(image_t *p);
int IsTgaAllowedSize(TgaHeader * p);

#endif
