TARGET = flash_update

CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

SRCS = $(TARGET).c bm_spi.c
OBJS = $(SRCS:.c=.o)

CFLAGS = --static -g

all : $(OBJS)
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)