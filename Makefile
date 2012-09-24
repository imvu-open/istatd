-include makevars.config
TEST_SRCS:=$(wildcard test/*.cpp)
TEST_OBJS:=$(patsubst %.cpp,obj/%.o,$(TEST_SRCS))
TESTS:=$(patsubst test/%.cpp,%,$(TEST_SRCS))
TOOL_SRCS:=$(wildcard tool/*.cpp)
TOOL_OBJS:=$(patsubst %.cpp,obj/%.o,$(TOOL_SRCS))
TOOLS:=$(patsubst tool/%.cpp,%,$(TOOL_SRCS))
LIB_SRCS:=$(wildcard lib/*.cpp)
LIB_OBJS:=$(patsubst %.cpp,obj/%.o,$(LIB_SRCS))
LIBS:=istatdaemon istat
DIR_DEPS:=obj obj/lib obj/daemon obj/test obj/tool obj/splitter bin
FTEST_FILES:=$(wildcard ftest/test_*.sh)
DAEMON_MAIN_SRC:=daemon/main.cpp
DAEMON_MAIN_OBJ:=$(patsubst %.cpp,obj/%.o,$(DAEMON_MAIN_SRC))
DAEMON_SRCS:=$(filter-out $(DAEMON_MAIN_SRC),$(wildcard daemon/*.cpp))
DAEMON_OBJS:=$(patsubst %.cpp,obj/%.o,$(DAEMON_SRCS))
SPLITTER_MAIN_SRC:=splitter/main.cpp
SPLITTER_MAIN_OBJ:=$(patsubst %.cpp,obj/%.o,$(SPLITTER_MAIN_SRC))
DAEMONS:=istatd splitd
DEPS:=$(sort $(patsubst %.o,%.d,$(TEST_OBJS) $(TOOL_OBJS) $(LIB_OBJS) $(DAEMON_OBJS) $(DAEMON_MAIN_OBJ) $(SPLITTER_MAIN_OBJ)))
BINS:=$(patsubst %,bin/%,$(TESTS) $(TOOLS) $(DAEMONS))
LIB_DEPS:=$(foreach lib,$(LIBS),obj/lib$(lib).a)
HDRS:=$(wildcard include/istat/*.h) $(wildcard include/json/*.h)
TESTS_TO_RUN:=
FILES_SRCS:=$(wildcard files/*)
SETTINGS_SRCS:=$(wildcard settings/*)

DESTDIR?=/
USR_PREFIX?=$(DESTDIR)/usr
VAR_PREFIX?=$(DESTDIR)/var
ETC_PREFIX?=$(DESTDIR)/etc
INSTALL?=install -C -D
TOUCH=touch

INSTALL_DIRS:=
INSTALL_DSTS:=
CXX:=g++#./gstlfilt/gfilt
LXXFLAGS:=-Lobj/ $(patsubst %,-l%,$(LIBS))
ifeq ($(OPT),)
OPT := -O2
endif
CXXFLAGS:=-pipe $(OPT) -g -Iinclude -MMD -D_LARGEFILE64_SOURCE -Wall -Werror
SYS_LIBS:=$(BOOST_SYSTEM) -lboost_thread -lboost_signals -lpthread $(STATGRAB) $(BOOST_FILESYSTEM) -lboost_date_time

all:	$(DIR_DEPS) $(LIB_DEPS) $(BINS) tests ftests

dpkg:
	env DEB_BUILD_OPTIONS="nostrip" debuild -us -uc   
	@echo done

build:	$(DIR_DEPS) $(BINS)
	@echo "build done"

clean:
	rm -fr obj bin testdata/* /tmp/test /var/tmp/test /tmp/ss.test

distclean:	clean
	rm -f makevars.config

killall:
	killall -q istatd || true

-include make.def

obj/libistat.a:	$(LIB_OBJS)
	ar cr $@ $^
$(eval $(call add_install,obj/libistat.a,$(USR_PREFIX)/lib/libistat.a,664))
HEADERS:=$(wildcard include/istat/*)
$(foreach hfile,$(HEADERS),$(eval $(call add_install,$(hfile),$(USR_PREFIX)/$(hfile),664)))

obj/libistatdaemon.a:	$(DAEMON_OBJS)
	ar cr $@ $^

bin/istatd:	$(DAEMON_MAIN_OBJ) $(LIB_DEPS)
	$(CXX) -g $(DAEMON_MAIN_OBJ) -o $@ $(LXXFLAGS) $(SYS_LIBS)
$(eval $(call add_install,bin/istatd,$(USR_PREFIX)/bin/istatd,775))
bin/splitd:	$(SPLITTER_MAIN_OBJ) $(LIB_DEPS)
	$(CXX) -g $(SPLITTER_MAIN_OBJ) -o $@ $(LXXFLAGS) $(SYS_LIBS)
$(eval $(call add_install,bin/splitd,$(USR_PREFIX)/bin/splitd,775))

$(foreach test,$(TESTS),$(eval $(call build_test,$(test))))
$(foreach tool,$(TOOLS),$(eval $(call build_tool,$(tool))))
$(foreach dir,obj bin obj/test obj/tool obj/daemon obj/lib obj/splitter,$(eval $(call build_dir,$(dir))))

obj/%.o:	%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# tests only require libs to be built
tests:	$(DIR_DEPS) $(patsubst %,run_%,$(TESTS_TO_RUN))
	@echo "tests complete"

# ftests require istatd to be built
ftests: $(DIR_DEPS) $(BINS) tests $(FTEST_FILES)
	@for ft in $(FTEST_FILES); do echo "\n============================================\nftest $$ft"; $$ft || exit 1; done
	bin/istatd --test --config test.cfg
	@echo "ftests complete"

-include $(DEPS)

$(eval $(call add_install,istatd.default,$(ETC_PREFIX)/default/istatd,755))
$(eval $(call add_install,istatd-init.sh,$(ETC_PREFIX)/init.d/istatd,755))
$(foreach set,$(SETTINGS_SRCS),$(eval $(call add_precious_install,$(set),$(VAR_PREFIX)/db/istatd/$(set),664)))
$(eval $(call add_precious_install,istatd.settings,$(ETC_PREFIX)/istatd.cfg,644))
$(foreach file,$(FILES_SRCS),$(eval $(call add_install,$(file),$(USR_PREFIX)/share/istatd/files/$(notdir $(file)),664)))

# add install must go before make directories
$(foreach dir,$(INSTALL_DIRS),$(eval $(call mk_install_dir,$(patsubst %/,%,$(dir)))))
$(call mk_install_dir,$(VAR_PREFIX)/db/istatd)

install:	$(INSTALL_DIRS) $(INSTALL_DSTS)
	#update-rc.d istatd defaults
	@echo done

uninstall:
	rm -f $(INSTALL_DSTS)
	rm -f $(ETC_PREFIX)/rc*.d/*istatd
