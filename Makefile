# ============================================================
# Makefile проекта «Многопоточное приложение для обработки
# информации о продажах» (Язык С, POSIX threads)
# Исполнители модулей см. в комментариях к исходным файлам.
# ============================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread -Isrc/common
ANALYZER_INCLUDES := -Isrc/sales_analyzer

BUILD   := build
COMMON_SRC := src/common/logger.c src/common/sales_model.c

SIM_BIN    := $(BUILD)/sales_simulator
ANAL_BIN   := $(BUILD)/sales_analyzer

.PHONY: all clean run-sim run-analyze testdirs

all: $(SIM_BIN) $(ANAL_BIN)

$(BUILD):
	mkdir -p $(BUILD)

# Программа 1: симулятор продаж
$(SIM_BIN): src/sales_simulator/simulator_main.c $(COMMON_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

# Программа 2: анализатор продаж
$(ANAL_BIN): src/sales_analyzer/analyzer_main.c \
             src/sales_analyzer/analyzer.c \
             src/sales_analyzer/report.c \
             $(COMMON_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(ANALYZER_INCLUDES) $^ -o $@

# Запуск симуляции (10 продаж на каждый товар)
run-sim: all
	./$(SIM_BIN) data/sales.csv data/goods.csv data/customers.csv 10

# Интерактивный запуск анализатора
run-analyze: all
	./$(ANAL_BIN) data/sales.csv data/goods.csv data/customers.csv

clean:
	rm -rf $(BUILD)
