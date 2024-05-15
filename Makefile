PROJ = batnotify
CC = gcc
CFLAGSLIBS = libnotify

${PROJ}: batnotify.c
	${CC} -o ${PROJ} `pkg-config --cflags --libs ${CFLAGSLIBS}` ${PROJ}.c

clean:
	rm ${PROJ}
