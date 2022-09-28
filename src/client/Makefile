.PHONY:clean all

CFLAGS+=-I../include/ -Wall
bin = ./bin/
src = $(wildcard *.c)
obj = $(patsubst %.c,%.o,$(src))

all:$(obj) # 生成目标

# $@  表示目标文件
# $^  表示所有的依赖文件
# $<  表示第一个依赖文件
# $?  表示比目标还要新的依赖文件列表
%.o:%.c # 依赖于client.o
	gcc $^ -o $(bin)$@ $(CFLAGS)

clean:
	rm -rf $(bin)