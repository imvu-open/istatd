/**
 * Copyright (C) 2011       IMVU, Inc
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Doug MacEachern
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ISTATD Authers:
 *   IMVU, Inc.
 * CSV Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Doug MacEachern <dougm@hyperic.com>
 **/

/**
 * This plugin writes collectd stats to istatd.  It started life as the collectd
 * CSV plugin.
 */
#include <collectd.h>
#include <plugin.h>
#include <common.h>
#include <utils_cache.h>
#include <utils_parse_option.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define ISTATD_PLUGIN_DEBUG 0

/*
 * Private variables
 */
static const char *config_keys[] =
{
    "Port",
    "Suffix",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int istatd_agent_port = 0;
static bool add_suffixes = true;
static char *counter_suffix = "";
static char *istatd_categories = "/etc/istatd.categories";

static int strcatval(char *buffer, int buffer_len, char *fmt, ...)
{
    va_list argptr;
    int n;
    int offset = strlen(buffer);

    va_start(argptr,fmt);
    n = vsnprintf(buffer+offset, buffer_len-offset, fmt, argptr);
    va_end(argptr);

    DEBUG ("strcatval: fmt: \"%s\", offset: %d, buffer: \"%s\"", fmt, offset, buffer);
    return n;
}

counter_t rate_to_count(gauge_t rate, double interval) {
    counter_t delta_count = 0;
    if (rate >= 0 && interval >= 0) {
        delta_count = (counter_t)((rate * interval) + 0.5);
    }
    return delta_count;
}

static char* get_value(char *buffer, int buffer_len, int index,
        const data_set_t *ds, const value_list_t *vl)
{
    int status;
    int i;
    gauge_t *rates = NULL;

    assert (0 == strcmp (ds->type, vl->type));

    memset (buffer, '\0', buffer_len);

    DEBUG ("istatd plugin: in get_value index: %d",index);
#if ISTATD_PLUGIN_DEBUG
    status = ssnprintf (buffer, buffer_len, "%.3f",
             CDTIME_T_TO_DOUBLE (vl->time));
    if ((status < 1) || (status >= buffer_len))
        return (-1);
    offset = status;
#endif

    if ((index >= 0) && (index < ds->ds_num)) {
        i = index;

        if ((ds->ds[i].type != DS_TYPE_COUNTER)
                && (ds->ds[i].type != DS_TYPE_GAUGE)
                && (ds->ds[i].type != DS_TYPE_DERIVE)
                && (ds->ds[i].type != DS_TYPE_ABSOLUTE)) {
            DEBUG ("get_value here1");
            return "";
        }

        if (ds->ds[i].type == DS_TYPE_GAUGE)
        {
            DEBUG ("get_value here2");
            status = strcatval(buffer, buffer_len, "%lf", vl->values[i].gauge);
        }
        else if (ds->ds[i].type == DS_TYPE_DERIVE || ds->ds[i].type == DS_TYPE_COUNTER)
        {
            rates = uc_get_rate (ds, vl);
            if (rates == NULL)
            {
                WARNING ("istatd plugin: uc_get_rate failed.");
                return "";
            }
            counter_t delta_count = rate_to_count(rates[i], CDTIME_T_TO_DOUBLE(vl->interval));
            status = strcatval(buffer, buffer_len, "%llu", delta_count);
        }
        else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
        {
            status = strcatval(buffer, buffer_len, "%"PRIu64, vl->values[i].absolute);
        }

        if (status < 1)
        {
            sfree (rates);
            DEBUG ("get_value here4");
            return "";
        }
    }

    sfree (rates);
    DEBUG ("get_value here5 buffer: \"%s\"", buffer);
    return buffer;

} /* int get_value */

static int make_istatd_metric_name (char *buffer, int buffer_len,
        const data_set_t *ds, const value_list_t *vl)
{
    assert (0 == strcmp (ds->type, vl->type));

    memset(buffer, 0, buffer_len);

    int needs_dot = 0;
    if (strlen(vl->plugin) > 0)
    {
        const char *plugin = vl->plugin;

        if (strcmp("icpu", vl->plugin) == 0)
            plugin = "cpu";

        if (needs_dot)
            strcat(buffer, ".");

        strcat(buffer, plugin);
        needs_dot = 1;
    }

    if (strlen(vl->plugin_instance) > 0)
    {
        if (needs_dot)
            strcat(buffer, ".");

        strcat(buffer, vl->plugin_instance);
        needs_dot = 1;
    }

    if ((strlen(vl->type) > 0) && (strcmp(vl->plugin, vl->type) != 0))
    {
        if (needs_dot)
            strcat(buffer, ".");

        strcat(buffer, vl->type);
        needs_dot = 1;
    }

    if (strlen(vl->type_instance) > 0)
    {
        if (needs_dot)
            strcat(buffer, ".");

        strcat(buffer, vl->type_instance);
        needs_dot = 1;
    }

    return (0);
} /* int make_istatd_metric_name */

static void map_to_istatd (char *buffer, int buffer_len,
        const data_set_t *ds, const value_list_t *vl)
{
    char metric_name[2048];
    char tmpbuf[3][2048];
    make_istatd_metric_name(metric_name, sizeof(metric_name), ds, vl);

    memset(buffer, 0, buffer_len);
    if (strcmp("memcached.ps_count", metric_name) == 0) {
        sprintf(buffer, "%s%s %s\n",
            metric_name, counter_suffix, get_value(tmpbuf[0],sizeof(tmpbuf[0]),1,ds,vl));
    }
    else if (strcmp("load", metric_name) == 0) {
        sprintf(buffer, "%s.1min%s %s\n%s.5min%s %s\n%s.15min%s %s\n",
            metric_name, counter_suffix, get_value(tmpbuf[0],sizeof(tmpbuf[0]),0,ds,vl),
            metric_name, counter_suffix, get_value(tmpbuf[1],sizeof(tmpbuf[1]),1,ds,vl),
            metric_name, counter_suffix, get_value(tmpbuf[2],sizeof(tmpbuf[2]),2,ds,vl)
        );
    }
    else if (strcmp("collectd-plugins_time", vl->plugin) == 0) {
        sprintf(buffer, "#%s %s\n", vl->plugin, vl->plugin_instance);
    }
    else if (strcmp("collectd-plugins_dpkg", vl->plugin) == 0) {
        sprintf(buffer, "#%s %s\n", vl->plugin, vl->plugin_instance);
    }
    else if (strcmp("collectd_dpkg", vl->plugin) == 0) {
        sprintf(buffer, "#%s %s\n", vl->plugin, vl->plugin_instance);
    }
    else if (strcmp("istatd_dpkg", vl->plugin) == 0) {
        sprintf(buffer, "#%s %s\n", vl->plugin, vl->plugin_instance);
    }
    else {
        int i=0, len=0;
        for (i = 0; i < ds->ds_num; ++i) {
            if (ds->ds[i].type == DS_TYPE_DERIVE || ds->ds[i].type == DS_TYPE_COUNTER) {
                sprintf(buffer, "*");
                ++buffer;
            }
            len = sprintf(buffer, "%s", metric_name);
            buffer += len;
            if (ds->ds_num > 1) {
                len = sprintf(buffer, ".%s", ds->ds[i].name);
                buffer += len;
            }
            len = sprintf(buffer, "%s %s\n",
                counter_suffix, get_value(tmpbuf[i],sizeof(tmpbuf[i]),i,ds,vl));
            buffer += len;
        }
    }

} /* int map_to_istatd */


static void send_counter(char *packet, int packet_size)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd=socket(AF_INET,SOCK_DGRAM,0);

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");
    servaddr.sin_port=htons(istatd_agent_port);

    DEBUG ("istatd plugin: send_counter to port %d: %s", istatd_agent_port, packet);
    if (sendto(sockfd, packet, packet_size,
               0, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        INFO("istatd plugin: sendto failed %s", strerror(errno));
    }
    close(sockfd);
}

static char *get_my_hostname(char *buffer, int buffer_len) {
    memset(buffer, '\0', buffer_len);
    int n = gethostname(buffer, buffer_len);
    if (n == -1) {
        ERROR("istatd plugin: gethostname failed: %s", strerror(errno));
    }
    char *dot = strchr(buffer, '.');
    if (dot) {
        *dot = '\0';
    }
    return buffer;
}

static char *get_counter_suffix(char *buffer, int buffer_len, const char* fname, const char* hostname, const bool should_add_suffixes) {
    const char *validchars = "ABCDEFGHIJKLMNOPQRSTUVWXZY"
                             "abcdefghijklmnopqrstuvwxzy"
                             "0123456789"
                             "-_.";
    memset(buffer,'\0',buffer_len);
    if (!should_add_suffixes)
    {
        return buffer;
    }

    FILE *f = fopen(fname, "rt");
    if (f != NULL) {
        char line[1024];
        while (fgets(line,sizeof(line),f)) {
            char *cp;

            // chop off comments
            if ((cp = strchr(line, '#')) != NULL) *cp = '\0';

            // replace invalid chars with underscores
            cp = line;
            while (*cp != '\0') {

                if (!strchr(validchars, *cp)) {
                    *cp = '_';
                }
                cp++;
            }

            // skip leading ".", "-", "_"
            char *fp = line + strspn(line, ".-_");

            // eat trailing ".", "-", "_"
            cp = fp + strlen(fp) - 1;
            while ((*cp == '_') || (*cp == '.') || (*cp == '-')) {
                *cp = '\0';
                cp--;
            }

            if (strlen(fp)) {
                strcat(buffer, "^");
                strcat(buffer, fp);
            }
        }
    }
    else {
        ERROR("istatd plugin: failed to open %s. %s", fname, strerror(errno));
    }

    if ((f == NULL) || strlen(buffer) == 0) {
        if (strlen(hostname)) {
            sprintf(buffer, "^host.%s", hostname);
        }
    }

    return buffer;
}

static int istatd_config (const char *key, const char *value)
{
    if (strcasecmp ("port", key) == 0)
    {
        INFO("processing port value %s", value);
        istatd_agent_port = atoi(value);
        return (0);
    }
    else if (strcasecmp ("suffix", key) == 0)
    {
        INFO("processing suffix value %s", value);
        if ( IS_TRUE ( value ) )
        {
          add_suffixes = true;
        }
        else
        {
          add_suffixes = false;
        }
        return (0);
    }
    return (-1);
} /* int istatd_config */

static int should_skip_recording(const value_list_t *vl)
{
    if ((strcmp(vl->plugin, "disk") == 0) && strncmp("md", vl->plugin_instance, 2) != 0 && isdigit(vl->plugin_instance[strlen(vl->plugin_instance)-1])) {
        // we don't needs disk.sda1 or disk.sdb2, etc.
        return (1);
    }
    else
    {
        return (0);
    }
}


static int istatd_init (void)
{
    char *hostname = get_my_hostname((char *)malloc(128), 128);
    counter_suffix = get_counter_suffix((char*)malloc(2048), 2048, istatd_categories, hostname, add_suffixes);
    return (0);
} /* int istatd_init */

#if ISTATD_PLUGIN_DEBUG
static void log_incoming_data(const data_set_t *ds, const value_list_t *vl)
{
    char info_buffer[2048];
    memset(info_buffer, '\0', 2048);
    sprintf(info_buffer, "istatd plugin: pl: %s, pi: %s, tp: %s, ti: %s",
        vl->plugin, vl->plugin_instance, vl->type, vl->type_instance);
    int i;
    for (i = 0; i < ds->ds_num; i++) {
        strcat(info_buffer, ", ");
        strcat(info_buffer, DS_TYPE_TO_STRING(ds->ds[i].type));
    }
    DEBUG("%s", info_buffer);
}
#endif

static int istatd_write (const data_set_t *ds, const value_list_t *vl,
        user_data_t __attribute__((unused)) *user_data)
{
    if (0 != strcmp (ds->type, vl->type)) {
        ERROR ("istatd plugin: DS type does not match value list type");
        return -1;
    }

#if ISTATD_PLUGIN_DEBUG
    log_incoming_data(ds, vl);
#endif

    if (should_skip_recording(vl)) {
        // we don't needs disk.sda1 or disk.sdb2, etc.
        return (0);
    }
    char packet[4096];
    map_to_istatd(packet, sizeof(packet), ds, vl);
    send_counter(packet, strlen(packet));
    return (0);

} /* int istatd_write */

void module_register (void)
{
    INFO ("istatd plugin: register");
    plugin_register_config ("istatd", istatd_config,
            config_keys, config_keys_num);
    plugin_register_init ("istatd", istatd_init);
    plugin_register_write ("istatd", istatd_write, /* user_data = */ NULL);
} /* void module_register */
