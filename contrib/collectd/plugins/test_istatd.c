#include <stdlib.h>
#include "istatd.c"

// mocked collectd function calls that we can't link to for testing
gauge_t *uc_get_rate(const data_set_t *dset, const value_list_t *vl) {
    printf("uc_get_rate: calloc for %d of size %lu\n", dset->ds_num, sizeof(gauge_t));
    gauge_t *rates = (gauge_t *)calloc(dset->ds_num, sizeof(gauge_t));
    printf("rates addr %lx\n", (size_t)rates);

    INFO ("uc_get_rate: here 1");
    int i;
    for (i=0; i < dset->ds_num; i++) {
        gauge_t val = -1.0;
        if (dset->ds[i].type == DS_TYPE_COUNTER) {
            INFO ("uc_get_rate: %d, counter: %llu", i, vl->values[i].counter);
            val = (gauge_t) vl->values[i].counter;
        }
        else if (dset->ds[i].type == DS_TYPE_DERIVE) {
            INFO ("uc_get_rate: here 3");
            val = (gauge_t) vl->values[i].derive;
        }
        else if (dset->ds[i].type == DS_TYPE_ABSOLUTE) {
            INFO ("uc_get_rate: here 4");
            val = (gauge_t) vl->values[i].absolute;
        }
        else {
            INFO ("uc_get_rate: here 5");
            val = (gauge_t) vl->values[i].gauge;
        }
        if (dset->ds[i].type != DS_TYPE_GAUGE) {
            INFO ("uc_get_rate: %d, calc fake rate", i);
            rates[i] = (gauge_t)(val/10.0);
            INFO ("uc_get_rate: %f, calc fake rate was ", rates[i]);
        }
    }

    INFO ("uc_get_rate: here 7");
    return rates;
}

void plugin_log (int level, const char *format, ...) {
    char my_format[1024];
    sprintf(my_format, "FAKE_LOG: (%d) - %s\n", level, format);

    va_list argptr;
    va_start(argptr,format);
    vprintf(my_format, argptr);
    va_end(argptr);
}

int plugin_register_config (const char *name,
        int (*callback) (const char *key, const char *val),
        const char **keys, int keys_num) { return 0;}

int plugin_register_init (const char *name,
        int (*callback) (void)) { return 0;}

int plugin_register_write (const char *name,
        plugin_write_cb callback, user_data_t *user_data) { return 0;}

static int error_count = 0;

static void c_assert_str_equal(const char *fname, int line, const char *s1, const char *s2) {
    if (strcmp(s1,s2)) {
        fprintf(stderr, "ERROR %s: %d - \"%s\" does not equal \"%s\"\n", fname, line, s1, s2);
        error_count++;
    }
}

static void c_assert_equal(const char *fname, int line, const int s1, const int s2) {
    if (s1 != s2) {
        fprintf(stderr, "ERROR %s: %d - \"%d\" does not equal \"%d\"\n", fname, line, s1, s2);
        error_count++;
    }
}


#define assert_str_equal(s1,s2) c_assert_str_equal(__FILE__, __LINE__, (s1), (s2))
#define assert_equal(v1,v2) c_assert_equal(__FILE__, __LINE__, (v1), (v2))
#define assert_true(b1) c_assert_equal(__FILE__, __LINE__, (b1), (1))
#define assert_false(b1) c_assert_equal(__FILE__, __LINE__, (b1), (0))


void test_make_istatd_metric_name() {
    char buffer[1024];
    data_set_t ds;
    value_list_t vl;

    // test uninitialized value_list returns empty string
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(buffer,"bogus");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("", buffer);

    // test plugin appears in name
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(vl.plugin, "plug");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("plug", buffer);

    // test plugin_instance appears in name
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(vl.plugin_instance, "plug_inst");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("plug_inst", buffer);

    // test type appears in name
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(ds.type, "type");
    strcpy(vl.type, "type");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("type", buffer);
    
    // test type_instance appears in name
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type_instance, "type_inst");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("type_inst", buffer);

    // test type does not appear when equal to plugin
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(ds.type, "foo");
    strcpy(vl.type, "foo");
    strcpy(vl.plugin, "foo");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("foo", buffer); // is foo, not foo.foo

    // test name ordering
    memset(&ds, 0, sizeof(ds));
    memset(&vl, 0, sizeof(vl));
    strcpy(ds.type, "type");
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "plug");
    strcpy(vl.plugin_instance, "plug_inst");
    strcpy(vl.type_instance, "type_inst");
    make_istatd_metric_name(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("plug.plug_inst.type.type_inst", buffer);

}


void make_fake_data(data_set_t *dset, value_list_t *vl,
          const int data_source_type,
          const char *plugin, const char *plugin_instance,
          const char *type, const char *type_instance,
          value_t *values, int num_values)
{
    // initialize data_set_t 
    strcpy(dset->type, type);
    dset->ds_num = num_values;
    dset->ds = (data_source_t *)calloc(num_values, sizeof(data_source_t));

    // initialize data_source_t in data_set_t
    int i;
    for (i = 0; i < num_values; i++) {
        strcpy(dset->ds[i].name, "fake_data_source");
        dset->ds[i].type = data_source_type;
        dset->ds[i].min = 0.0;
        dset->ds[i].max = 65535.0;
    }

    vl->values = values;
    vl->values_len = num_values;
    vl->time = TIME_T_TO_CDTIME_T(0);
    vl->interval = TIME_T_TO_CDTIME_T(10);
    strcpy(vl->host, "localhost");
    strcpy(vl->plugin, plugin);
    strcpy(vl->plugin_instance, plugin_instance);
    strcpy(vl->type, type);
    strcpy(vl->type_instance, type_instance);
}

void destroy_fake_data(data_set_t *dset, value_list_t *vl) {
    sfree(dset->ds);
}

void test_map_to_istatd() {
    counter_suffix = "^buildbot.linu-15-14^host.linu-15-14";

    char buffer[1024];
    data_set_t ds;
    value_list_t vl;
    value_t values[3];

    // test default case
    values[0].counter = 9;
    make_fake_data(&ds, &vl, DS_TYPE_COUNTER, "cpu", "", "idle", "", values, 1);

    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("*cpu.idle^buildbot.linu-15-14^host.linu-15-14 9\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test memcache_ps_count
    values[0].gauge = 1.0;
    values[1].gauge = 9.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "memcached", "", "ps_count", "", values, 2);

    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("memcached.ps_count^buildbot.linu-15-14^host.linu-15-14 9.000000\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test load
    values[0].gauge = 1.0;
    values[1].gauge = 5.0;
    values[2].gauge = 15.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "load", "", "load", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("load.1min^buildbot.linu-15-14^host.linu-15-14 1.000000\n"
                     "load.5min^buildbot.linu-15-14^host.linu-15-14 5.000000\n"
                     "load.15min^buildbot.linu-15-14^host.linu-15-14 15.000000\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test load
    values[0].gauge = 1.0;
    values[1].gauge = 5.0;
    values[2].gauge = 15.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "load", "", "load", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("load.1min^buildbot.linu-15-14^host.linu-15-14 1.000000\n"
                     "load.5min^buildbot.linu-15-14^host.linu-15-14 5.000000\n"
                     "load.15min^buildbot.linu-15-14^host.linu-15-14 15.000000\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test network usage
    values[0].derive = 1.0;
    values[1].derive = 5.0;
    make_fake_data(&ds, &vl, DS_TYPE_DERIVE, "interface", "eth0", "if_packets", "", values, 2);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("*interface.eth0.if_packets.fake_data_source^buildbot.linu-15-14^host.linu-15-14 1\n"
                     "*interface.eth0.if_packets.fake_data_source^buildbot.linu-15-14^host.linu-15-14 5\n"
                     , buffer);
    destroy_fake_data(&ds, &vl);

    // test collectd plugins time
    values[0].gauge = 1.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "collectd-plugins_time", "2011-11-11T11:11:11Z", "", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("#collectd-plugins_time 2011-11-11T11:11:11Z\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test collectd plugins dpkg
    values[0].gauge = 1.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "collectd-plugins_dpkg", "0.11-1", "", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("#collectd-plugins_dpkg 0.11-1\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test collectd dpkg
    values[0].gauge = 1.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "collectd_dpkg", "5.0.1-0imvu1", "", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("#collectd_dpkg 5.0.1-0imvu1\n", buffer);
    destroy_fake_data(&ds, &vl);

    // test istatd dpkg
    values[0].gauge = 1.0;
    make_fake_data(&ds, &vl, DS_TYPE_GAUGE, "istatd_dpkg", "0.11-1", "", "", values, 3);
    map_to_istatd(buffer, sizeof(buffer), &ds, &vl);
    assert_str_equal("#istatd_dpkg 0.11-1\n", buffer);
    destroy_fake_data(&ds, &vl);

}

void test_get_counter_suffix() {
    char buffer[256];
    char *suffix;

    // test non-existant file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/does_not_exist", "fakehost", true);
    assert_str_equal("^host.fakehost", suffix);

    // test empty file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/empty_istatd.categories", "fakehost", true);
    assert_str_equal("^host.fakehost", suffix);

    // test sample file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/istatd.categories", "fakehost", true);
    assert_str_equal("^class^trailing_ws^leading_ws^surrounded_ws^clean_me_up__por_-favor5^host.fakehost", suffix);

    //Test no suffix addition

    // test non-existant file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/does_not_exist", "fakehost", false);
    assert_str_equal("", suffix);

    // test empty file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/empty_istatd.categories", "fakehost", false);
    assert_str_equal("", suffix);

    // test sample file
    suffix = get_counter_suffix(buffer, sizeof(buffer), "test_data/istatd.categories", "fakehost", false);
    assert_str_equal("", suffix);
}

void test_should_skip_recording() {
    value_list_t vl;
    int res = 0;

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "disk");
    strcpy(vl.plugin_instance, "sda1");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_true(res);

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "disk");
    strcpy(vl.plugin_instance, "sdb");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_false(res);

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "disk");
    strcpy(vl.plugin_instance, "md0");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_false(res);

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "disk");
    strcpy(vl.plugin_instance, "sdmd0");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_true(res);

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "cpu");
    strcpy(vl.plugin_instance, "idle");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_false(res);

    memset(&vl, 0, sizeof(vl));
    strcpy(vl.type, "type");
    strcpy(vl.plugin, "others");
    strcpy(vl.plugin_instance, "idle");
    strcpy(vl.type_instance, "more");
    res = should_skip_recording(&vl);
    assert_false(res);
}


int main(int argc, char** argv) {
    test_make_istatd_metric_name();
    test_map_to_istatd();
    test_get_counter_suffix();
    test_should_skip_recording();
    return(0);
}
