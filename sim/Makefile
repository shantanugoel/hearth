# Host build of the firmware canvas + bring-up screens.
GFX  := ../firmware/components/hearth_gfx
MAIN := ../firmware/main
SRCS := sim_main.cc \
        $(GFX)/hearth_canvas.cc \
        $(MAIN)/hearth_screens.cc
INC  := -I$(GFX)/include -I$(GFX)/font -I$(MAIN)
# The user profile puts ESP ULP binutils ahead of /usr/bin; -B pins the host
# assembler so this builds in an interactive shell too.
FLAGS := -B/usr/bin -O2 -Wall -Wextra -Wno-unused-parameter $(INC)

out/hearth-sim: $(SRCS)
	@mkdir -p out
	g++ -std=c++17 $(FLAGS) -x c++ $(SRCS) -o $@

.PHONY: run clean
run: out/hearth-sim
	@mkdir -p out
	@./out/hearth-sim out 2

clean:
	rm -rf out/hearth-sim out/*.png
