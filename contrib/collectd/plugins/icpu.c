/* mostly lifted from collectd's cpu.c */
#include <collectd.h>
#include <common.h>
#include <plugin.h>

#include <statgrab.h>
#include "config.h"

#define ICPU_PLUGIN_NAME "icpu"

static data_source_t icpu_dsrc[] = {{ICPU_PLUGIN_NAME, DS_TYPE_GAUGE, 0, NAN }};
static data_set_t icpu_ds = {ICPU_PLUGIN_NAME, STATIC_ARRAY_SIZE(icpu_dsrc), icpu_dsrc};

static void icpu_submit(int cpu_num, const char *type_instance, gauge_t value) {
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, ICPU_PLUGIN_NAME, sizeof(vl.plugin));
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", cpu_num);
    sstrncpy(vl.type, ICPU_PLUGIN_NAME, sizeof(vl.type));
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
}

static int icpu_init() {
    return 0;
}

static int icpu_read() {
    sg_cpu_percents *cp;
    #if defined(LIBSTATGRAB_API_0_90)
    cp = sg_get_cpu_percents(NULL);
    #else
    cp = sg_get_cpu_percents();
    #endif
    if (cp == NULL) {
        ERROR("icpu plugin: sg_get_cpu_percents failed.");
        return -1;
    }

    icpu_submit(0, "idle",   (gauge_t) cp->idle);
    icpu_submit(0, "nice",   (gauge_t) cp->nice);
    icpu_submit(0, "swap",   (gauge_t) cp->swap);
    icpu_submit(0, "system", (gauge_t) cp->kernel);
    icpu_submit(0, "user",   (gauge_t) cp->user);
    icpu_submit(0, "wait",   (gauge_t) cp->iowait);

    return 0;
}


void module_register() {
    plugin_register_init(ICPU_PLUGIN_NAME, icpu_init);
    plugin_register_data_set(&icpu_ds);
    plugin_register_read(ICPU_PLUGIN_NAME, icpu_read);
}
