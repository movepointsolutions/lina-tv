lina-tv: lina-tv.c
	gcc -o lina-tv `pkg-config --cflags --libs gstreamer-1.0` lina-tv.c
