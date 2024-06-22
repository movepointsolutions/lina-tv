lina-tv: lina-tv.c Makefile
	gcc -O0 -g -o lina-tv `pkg-config --cflags --libs gstreamer-1.0` lina-tv.c
