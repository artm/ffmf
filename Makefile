SUBDIRS = libavcodec libavformat src

include make.rules

all: all-recursive

clean: clean-recursive

remake:
	$(MAKE) -C src clean all
