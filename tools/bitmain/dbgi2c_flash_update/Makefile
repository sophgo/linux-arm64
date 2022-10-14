TARGET = dbgi2c_flash_update

CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

SRCS = $(TARGET).c bm_spi.c dbg_i2c.c
OBJS = $(SRCS:.c=.o)

CFLAGS = --static -g

all : $(OBJS)
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
