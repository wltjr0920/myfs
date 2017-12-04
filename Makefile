all:
	gcc -Wall fusexmp.c `pkg-config fuse --cflags --libs` -o myfs

clean:
	rm ./myfs
