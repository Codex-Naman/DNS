CC      ?= gcc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
           -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
           -Wpointer-arith -Wundef -Wswitch-enum -Wswitch-default -Werror
CFLAGS  ?= -O2 -g
INC     := -Iinclude -Itest
ALL     := $(CSTD) $(WARN) $(CFLAGS) $(INC)

BUILD   := build
COVERAGE := coverage
LIB_SRC := src/dns_table.c
APP     := $(BUILD)/dns_app
TESTS   := $(BUILD)/test_dns_unit $(BUILD)/test_dns_integration
GTEST   := $(BUILD)/test_dns_gtest
GXX     ?= g++
GTEST_LIBS ?= -lgtest -lgtest_main -pthread

.PHONY: all test gtest c-test coverage sanitize valgrind helgrind cppcheck clean

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

coverage:
	@rm -rf $(COVERAGE)
	@mkdir -p $(COVERAGE)/run $(COVERAGE)/run-errors
	@$(CC) $(CSTD) $(WARN) -O0 -g --coverage $(INC) -c \
		-o $(COVERAGE)/dns_table.o $(LIB_SRC)
	@$(CC) $(CSTD) $(WARN) -O0 -g --coverage $(INC) -c \
		-o $(COVERAGE)/main.o src/main.c
	@$(CC) $(CSTD) $(WARN) -O0 -g --coverage $(INC) -c \
		-o $(COVERAGE)/test_util.o test/test_util.c
	@$(CC) --coverage -Iinclude -Itest -o $(COVERAGE)/test_unit \
		test/test_dns_unit.c $(COVERAGE)/test_util.o $(COVERAGE)/dns_table.o
	@$(CC) --coverage -Iinclude -Itest -o $(COVERAGE)/test_integration \
		test/test_dns_integration.c $(COVERAGE)/test_util.o $(COVERAGE)/dns_table.o
	@$(CC) --coverage -o $(COVERAGE)/dns_app \
		$(COVERAGE)/main.o $(COVERAGE)/dns_table.o
	@$(COVERAGE)/test_unit >/dev/null
	@$(COVERAGE)/test_integration >/dev/null
	@printf '1\ncoverage.example.com\n192.0.2.1\n2\ncoverage.example.com\n3\ncoverage.example.com\n192.0.2.2\n5\n6\n7\n4\ncoverage.example.com\n0\n' | \
		(cd $(COVERAGE)/run && ../dns_app >/dev/null)
	@printf '# comment\nbad..domain 192.0.2.1\n' > $(COVERAGE)/run-errors/dns_records.txt
	@printf 'x\n\n8\n2\nmissing.example.com\n3\nmissing.example.com\n192.0.2.3\n4\nbad..domain\n5\n6\n7\n' | \
		(cd $(COVERAGE)/run-errors && ../dns_app >/dev/null)
	@gcov -b -c -o $(COVERAGE) src/dns_table.c src/main.c
	@mv -f dns_table.c.gcov main.c.gcov $(COVERAGE)/

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
