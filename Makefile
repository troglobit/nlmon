EXEC   := nlmon
SRCS   := nlmon.c
OBJS   := nlmon.o
LDLIBS := $(shell pkg-config --libs   libnl-route-3.0 libnl-3.0)
LDLIBS += -lev
CFLAGS := $(shell pkg-config --cflags libnl-route-3.0 libnl-3.0)
CFLAGS += -g -Og -W -Wall -Wextra -Wno-unused-parameter

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

clean:
	$(RM) $(EXEC) $(OBJS)

distclean: clean
	$(RM) *.o *~ *.bak
