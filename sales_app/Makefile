# ============================================================
# Makefile — многопоточное приложение обработки информации
# о продажах (язык C, POSIX threads).
# Компиляция:  make        Запуск:  ./sales_app   Очистка: make clean
# ============================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=gnu11 -pthread
TARGET  := sales_app
SRCS    := main.c logger.c data.c sim_thread.c analytics_threads.c cli_thread.c
OBJS    := $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

%.o: %.c app.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
