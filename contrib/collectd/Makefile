CC=gcc
CFLAGS+= -I/usr/include -Wall -DHAVE_CONFIG_H -I. -I/usr/include/collectd -I/usr/include/collectd/core
LDFLAGS+= -L/usr/lib -lstatgrab -shared -fPIC
PLUGIN_INSTALL_DIR = /usr/lib/collectd

build: istatd ipcu

istatd:
	./statgrab_detect.sh
	$(CC) plugins/istatd.c $(CFLAGS) $(LDFLAGS) -o plugins/istatd.so 

ipcu:
	$(CC) plugins/icpu.c $(CFLAGS) $(LDFLAGS) -o plugins/icpu.so 

clean: distclean

test: istatd
	$(CC) -g $(CFLAGS) -L/usr/lib/collectd -o plugins/test_istatd plugins/test_istatd.c
	rm -rf test_data/does_not_exist
	plugins/test_istatd

distclean:
	rm -rf plugins/*.so plugins/*.o plugins/test_istatd

install:
	install -d $(DESTDIR)$(PLUGIN_INSTALL_DIR)
	install -t $(DESTDIR)$(PLUGIN_INSTALL_DIR) plugins/istatd.so
	install -t $(DESTDIR)$(PLUGIN_INSTALL_DIR) plugins/icpu.so
