CC = gcc
CFLAGS = -Wall -o
router: router.c
        $(CC) $(CFLAGS) router router.c
