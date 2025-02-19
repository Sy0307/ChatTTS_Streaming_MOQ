# Makefile for c_proxy_server

CC = gcc
CFLAGS = -Wall -g -O2  # 编译选项
INCLUDE_PATH = -I/opt/homebrew/include # 头文件路径
LIB_PATH = -L/opt/homebrew/lib # 库文件路径，添加 -L/opt/homebrew/lib
LIBS = -levent -lcurl # 需要链接的库

SRC = c_proxy_server.c
OBJ = c_proxy_server.o
TARGET = c_proxy_server

# 编译规则

$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIB_PATH) $(LIBS) # 在链接命令中添加 LIB_PATH

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $(SRC) -o $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -DDEBUG -g -O0 # 添加 DEBUG 宏，关闭优化
debug: $(OBJ)
	$(CC) -o $(TARGET)_debug $(OBJ) $(LIB_PATH) $(LIBS) # 在 debug 链接命令中也添加 LIB_PATH
	./$(TARGET)_debug

.PHONY: clean run debug