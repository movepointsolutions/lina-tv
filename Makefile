lina-tv: lina-tv.c Makefile
	gcc -O2 -o lina-tv `pkg-config --cflags --libs gstreamer-1.0` `pkg-config --cflags --libs gio-2.0` lina-tv.c
