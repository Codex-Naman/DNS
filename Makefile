CC      ?= gcc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
           -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
           -Wpointer-arith -Wundef -Wswitch-enum -Wswitch-default -Werror
CFLAGS  ?= -O2 -g
INC     := -Iinclude -Itest
ALL     := $(CSTD) $(WARN) $(CFLAGS) $(INC)

BUILD   := build
LIB_SRC := src/dns_table.c
APP     := $(BUILD)/dns_app
TESTS   := $(BUILD)/test_dns_unit $(BUILD)/test_dns_integration
GTEST   := $(BUILD)/test_dns_gtest
GXX     ?= g++
GTEST_LIBS ?= -lgtest -lgtest_main -pthread

.PHONY: all test gtest c-test sanitize valgrind helgrind cppcheck clean

all: $(APP) $(TESTS)

$(BUILD):
	mkdir -p $(BUILD)

$(APP): src/main.c $(LIB_SRC) include/dns_table.h | $(BUILD)
	$(CC) $(ALL) -o $@ src/main.c $(LIB_SRC)

$(BUILD)/test_dns_unit: test/test_dns_unit.c test/test_util.c test/test_util.h $(LIB_SRC) include/dns_table.h | $(BUILD)
	$(CC) $(ALL) -o $@ test/test_dns_unit.c test/test_util.c $(LIB_SRC)

$(BUILD)/test_dns_integration: test/test_dns_integration.c test/test_util.c test/test_util.h $(LIB_SRC) include/dns_table.h | $(BUILD)
	$(CC) $(ALL) -o $@ test/test_dns_integration.c test/test_util.c $(LIB_SRC)

test: $(TESTS)
	$(BUILD)/test_dns_unit
	$(BUILD)/test_dns_integration

c-test: test

$(BUILD)/dns_table_gtest.o: $(LIB_SRC) include/dns_table.h | $(BUILD)
	$(CC) $(ALL) -c -o $@ $(LIB_SRC)

$(GTEST): test/test_dns_gtest.cpp $(BUILD)/dns_table_gtest.o include/dns_table.h | $(BUILD)
	$(GXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2 -g -Iinclude \
		-o $@ test/test_dns_gtest.cpp $(BUILD)/dns_table_gtest.o $(GTEST_LIBS)

gtest: $(GTEST)
	$(GTEST)

# Run both test programs under AddressSanitizer + UndefinedBehaviorSanitizer
sanitize: | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(INC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o $(BUILD)/test_unit_san test/test_dns_unit.c test/test_util.c $(LIB_SRC)
	$(CC) $(CSTD) $(WARN) $(INC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o $(BUILD)/test_int_san test/test_dns_integration.c test/test_util.c $(LIB_SRC)
	$(BUILD)/test_unit_san
	$(BUILD)/test_int_san

valgrind: $(TESTS)
	valgrind --leak-check=full --error-exitcode=1 $(BUILD)/test_dns_unit
	valgrind --leak-check=full --error-exitcode=1 $(BUILD)/test_dns_integration

helgrind: $(TESTS) $(GTEST)
	valgrind --tool=helgrind --error-exitcode=1 $(BUILD)/test_dns_unit
	valgrind --tool=helgrind --error-exitcode=1 $(BUILD)/test_dns_integration
	valgrind --tool=helgrind --error-exitcode=1 $(GTEST)

# MISRA addon needs cppcheck with the misra addon + rule texts installed
cppcheck:
	cppcheck --enable=all --inconclusive --std=c99 --suppress=missingIncludeSystem \
	         -Iinclude src/dns_table.c src/main.c
	-cppcheck --addon=misra --std=c99 -Iinclude src/dns_table.c src/main.c

clean:
	rm -rf $(BUILD)/*
