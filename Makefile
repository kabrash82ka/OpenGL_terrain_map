VPATH = src obj
DEPS = load_bush_3.h my_mat_math_6.h my_mouse_2.h \
my_tga_2.h my_character2.h my_vehicle.h \
load_collada_4.h my_keyboard.h my_item.h \
my_collision.h my_gui.h load_character.h \
my_milbase.h my_camera.h
OBJ = terrain_16.o load_bush_3.o my_mouse_2.o \
my_tga_2.o my_mat_math_6.o load_character.o \
load_collada_4.o
LIBS = -lX11 -lGL -lm -lrt
CFLAGS = -g

a.out: $(OBJ)
	gcc $(addprefix obj/, $(^F)) $(LIBS) -o $@

$(OBJ): %.o: %.c $(DEPS)
	gcc $(CFLAGS) -I./src -c -o obj/$(@F) src/$(<F)
