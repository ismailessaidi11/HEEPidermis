# Native host build for ground-truth feature extraction.
# Run this on the PC — no chip simulator needed.
#
# Usage:
#   1. Generate input_signal.h from conductance.txt:
#        python3 txt_to_c_array_easy.py
#   2. Build:
#        make
#   3. Run and save GT output:
#        make run          # writes gt_fe_output.csv

CC      = gcc
CFLAGS  = -O2 -Wall -Wno-unused-result -Wno-unused-variable
LDFLAGS = -lm

TARGET  = gsr_eval_native
SRCS    = main.c descompv5.c
OUT_CSV = fe_output_xHz.csv

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS) input_signal.h descompv5.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) > $(OUT_CSV)
	@echo "Ground-truth FE output saved to $(OUT_CSV)"
	@wc -l $(OUT_CSV)

clean:
	rm -f $(TARGET) $(OUT_CSV)
