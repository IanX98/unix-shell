CFLAGS = -g -Wall
SOURCES = src/shell.c \
	  src/cmd.c \
	  src/str.c

all:
	gcc ${CFLAGS} ${SOURCES} -o shell

clean:
	$(RM) shell
