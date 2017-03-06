replayer: replay.c
	gcc replay.c -o replay -lrt
clean:
	rm -f replay
