CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?=

BUILD := build
ENGINE_OBJS := $(BUILD)/tonetrace_engine.o $(BUILD)/tonetrace_realtime.o $(BUILD)/tonetrace_describe.o

.PHONY: all test clean

all: $(BUILD)/tonetrace-match $(BUILD)/tonetrace-tests $(BUILD)/tonetrace-realtime-tests $(BUILD)/tonetrace-ui-layout-tests $(BUILD)/tonetrace-band-value-tests $(BUILD)/tonetrace-fixture-eval $(BUILD)/tonetrace-pair-eval $(BUILD)/tonetrace-stability-eval $(BUILD)/tonetrace-realtime-bench $(BUILD)/ToneTrace_EQ.clap $(BUILD)/tonetrace-clap-tests

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tonetrace_engine.o: src/tonetrace_engine.cpp include/tonetrace/tonetrace_engine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tonetrace_realtime.o: src/tonetrace_realtime.cpp include/tonetrace/tonetrace_realtime.h include/tonetrace/tonetrace_engine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tonetrace_describe.o: src/tonetrace_describe.cpp include/tonetrace/tonetrace_describe.h include/tonetrace/tonetrace_realtime.h include/tonetrace/tonetrace_engine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tonetrace-match: src/main.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-tests: tests/test_engine.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-realtime-tests: tests/test_realtime.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-ui-layout-tests: tests/test_ui_layout.cpp include/tonetrace/tonetrace_ui_layout.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

$(BUILD)/tonetrace-band-value-tests: tests/test_band_value.cpp plugins/clap/tonetrace_band_value.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iplugins/clap $< $(LDFLAGS) -o $@

$(BUILD)/tonetrace-fixture-eval: tools/fixture_eval.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-pair-eval: tools/pair_eval.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-stability-eval: tools/stability_eval.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-realtime-bench: tools/realtime_bench.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/ToneTrace_EQ.clap: plugins/clap/tonetrace_clap.cpp src/tonetrace_engine.cpp src/tonetrace_realtime.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -fPIC -Ithird_party/clap/include -shared $^ $(LDFLAGS) -o $@

$(BUILD)/tonetrace-clap-tests: tests/test_clap.cpp $(ENGINE_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Ithird_party/clap/include $^ $(LDFLAGS) -ldl -o $@

test: $(BUILD)/tonetrace-tests $(BUILD)/tonetrace-realtime-tests $(BUILD)/tonetrace-ui-layout-tests $(BUILD)/tonetrace-band-value-tests $(BUILD)/ToneTrace_EQ.clap $(BUILD)/tonetrace-clap-tests
	$(BUILD)/tonetrace-tests
	$(BUILD)/tonetrace-realtime-tests
	$(BUILD)/tonetrace-ui-layout-tests
	$(BUILD)/tonetrace-band-value-tests
	$(BUILD)/tonetrace-clap-tests $(BUILD)/ToneTrace_EQ.clap

clean:
	rm -f $(ENGINE_OBJS) $(BUILD)/tonetrace-match $(BUILD)/tonetrace-tests $(BUILD)/tonetrace-realtime-tests $(BUILD)/tonetrace-ui-layout-tests $(BUILD)/tonetrace-band-value-tests $(BUILD)/tonetrace-fixture-eval $(BUILD)/tonetrace-pair-eval $(BUILD)/tonetrace-stability-eval $(BUILD)/tonetrace-realtime-bench $(BUILD)/ToneTrace_EQ.clap $(BUILD)/tonetrace-clap-tests
