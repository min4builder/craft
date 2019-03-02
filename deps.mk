build/client.o: src/client.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/client.c
build/cube.o: src/cube.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/cube.c
build/item.o: src/item.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/item.c
build/lodepng.o: src/lodepng.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/lodepng.c
build/main.o: src/main.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/main.c
build/map.o: src/map.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/map.c
build/matrix.o: src/matrix.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/matrix.c
build/miniz.o: src/miniz.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/miniz.c
build/tinycthread.o: src/tinycthread.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/tinycthread.c
build/util.o: src/util.c src/*.h
	@$(MK_BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c src/util.c
