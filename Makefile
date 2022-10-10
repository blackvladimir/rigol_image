all: rigol_pulse rigol_image

rigol_pulse: main.c
	gcc main.c -lzip -o rigol_pulse

rigol_image: picture.c
	gcc picture.c -o rigol_image
