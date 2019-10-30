.SILENT:
.DELETE_ON_ERROR:

#
#  What we want the target named, and our list of source files. 
#
BINARY = pi-timer
SRCS   = pi-timer.c
OBJS   = $(SRCS:.c=.o)
LIBS   = -lm

#
#  The compiler we'll use and any flags
#
CC      = gcc
CFLAGS  = -Wall
CFLAGS += -W
CFLAGS += -Wextra
CFLAGS += -g3
CFLAGS += -Wunused
CFLAGS += -Wuninitialized
CFLAGS += -Wmissing-declarations
CFLAGS += -Wpointer-arith
CFLAGS += -Wfloat-equal
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wbad-function-cast
CFLAGS += -Wformat
CFLAGS += -std=gnu11
CFLAGS += -fsigned-char
CFLAGS += -ffunction-sections
CFLAGS += -fdata-sections
CFLAGS += -Wlogical-op
CFLAGS += -fsingle-precision-constant
CFLAGS += -fno-move-loop-invariants

.c.o:
	echo "  CC    $<"
	$(CC) $(CFLAGS) -c $< -o $@

#
#  Dependency for the binary is .depend and all the .o files.
#
$(BINARY): .depend $(OBJS)
	echo "  LD    $@"
	$(CC) $(CFLAGS) -o $(BINARY) $(OBJS) $(LIBS)
	echo "  STRIP $@"
	strip $(BINARY)
	sudo chown root:root $(BINARY)

#
#  The .depend files contains the list of header files that the
#  various source files depend on.  By doing this, we'll only
#  rebuild the .o's that are affected by header files changing.
#
.depend:
	$(CC) -M $(CFLAGS) $(SRCS) > .depend

#
#  Utility targets
#
.PHONY: tags
tags:
	ctags *

.PHONY: dep
dep:
	$(CC) -M $(SRCS) > .depend

.PHONY: clean
clean:
	rm -f $(BINARY) .depend *.o core

#
#  Include the .depend file so we have the list of dependencies
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
