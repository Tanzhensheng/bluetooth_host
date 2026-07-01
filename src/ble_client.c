#include "ble_client.h"

#include "mod_ble_log.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__) && defined(MOD_BLE_HAVE_GIO)

#include <gio/gio.h>
#include <glib.h>

typedef struct {
    GDBusConnection *connection;
    guint signal_subscription_id;
    char adapter_path[128];
    char device_path[256];
    char write_char_path[256];
    char notify_char_path[256];
    uint8_t notify_buf[MOD_BLE_MAX_HEX_DUMP_LEN];
    size_t notify_len;
    int notify_ready;
    int notify_started;
} ble_linux_state_t;

static ble_linux_state_t g_ble_linux;

static int ble_linux_target_is_mac(const char *target_id)
{
    size_t i;

    if (target_id == NULL || strlen(target_id) != 17U) {
        return 0;
    }

    for (i = 0; i < 17U; ++i) {
        if ((i % 3U) == 2U) {
            if (target_id[i] != ':') {
                return 0;
            }
        } else if (!g_ascii_isxdigit(target_id[i])) {
            return 0;
        }
    }

    return 1;
}

static void ble_linux_state_reset(void)
{
    (void)memset(&g_ble_linux, 0, sizeof(g_ble_linux));
}

static int ble_linux_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (strlen(src) >= dst_size) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    (void)snprintf(dst, dst_size, "%s", src);
    return MOD_BLE_STATUS_OK;
}

static GVariant *ble_linux_get_managed_objects(GError **error)
{
    return g_dbus_connection_call_sync(
        g_ble_linux.connection,
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        error);
}

static int ble_linux_find_adapter_path(void)
{
    GError *error = NULL;
    GVariant *reply = NULL;
    GVariantIter *objects = NULL;
    gchar *object_path = NULL;
    GVariant *interfaces = NULL;
    int status = MOD_BLE_STATUS_IO;

    reply = ble_linux_get_managed_objects(&error);
    if (reply == NULL) {
        mod_ble_log_error("GetManagedObjects failed while locating adapter: %s", error->message);
        g_clear_error(&error);
        return MOD_BLE_STATUS_IO;
    }

    g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
    while (g_variant_iter_loop(objects, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariant *adapter_props = g_variant_lookup_value(interfaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
        if (adapter_props != NULL) {
            status = ble_linux_copy_string(g_ble_linux.adapter_path, sizeof(g_ble_linux.adapter_path), object_path);
            g_variant_unref(adapter_props);
            break;
        }
    }

    g_variant_iter_free(objects);
    g_variant_unref(reply);
    return status;
}

static int ble_linux_call_void(const char *object_path, const char *interface_name, const char *method_name, GVariant *parameters)
{
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        g_ble_linux.connection,
        "org.bluez",
        object_path,
        interface_name,
        method_name,
        parameters,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    if (reply == NULL) {
        mod_ble_log_error("%s.%s failed on %s: %s", interface_name, method_name, object_path, error->message);
        g_clear_error(&error);
        return MOD_BLE_STATUS_IO;
    }

    g_variant_unref(reply);
    return MOD_BLE_STATUS_OK;
}

static int ble_linux_set_discovery_filter(void)
{
    GVariantBuilder dict_builder;

    g_variant_builder_init(&dict_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&dict_builder, "{sv}", "Transport", g_variant_new_string("le"));
    g_variant_builder_add(&dict_builder, "{sv}", "DuplicateData", g_variant_new_boolean(FALSE));
    return ble_linux_call_void(
        g_ble_linux.adapter_path,
        "org.bluez.Adapter1",
        "SetDiscoveryFilter",
        g_variant_new("(a{sv})", &dict_builder));
}

static int ble_linux_build_device_path_from_mac(const char *target_id)
{
    char suffix[18];
    size_t i;

    if (!ble_linux_target_is_mac(target_id)) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    for (i = 0; i < 17U; ++i) {
        suffix[i] = (target_id[i] == ':') ? '_' : g_ascii_toupper(target_id[i]);
    }
    suffix[17] = '\0';

    if (snprintf(g_ble_linux.device_path, sizeof(g_ble_linux.device_path), "%s/dev_%s", g_ble_linux.adapter_path, suffix)
        >= (int)sizeof(g_ble_linux.device_path)) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    return MOD_BLE_STATUS_OK;
}

static int ble_linux_device_exists_on_bus(const char *device_path)
{
    GError *error = NULL;
    GVariant *reply = NULL;

    reply = g_dbus_connection_call_sync(
        g_ble_linux.connection,
        "org.bluez",
        device_path,
        "org.freedesktop.DBus.Properties",
        "GetAll",
        g_variant_new("(s)", "org.bluez.Device1"),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    if (reply == NULL) {
        g_clear_error(&error);
        return 0;
    }

    g_variant_unref(reply);
    return 1;
}

static int ble_linux_find_device_path(const char *target_id)
{
    GError *error = NULL;
    GVariant *reply = NULL;
    GVariantIter *objects = NULL;
    gchar *object_path = NULL;
    GVariant *interfaces = NULL;
    int status = MOD_BLE_STATUS_IO;

    reply = ble_linux_get_managed_objects(&error);
    if (reply == NULL) {
        mod_ble_log_error("GetManagedObjects failed while locating device: %s", error->message);
        g_clear_error(&error);
        return MOD_BLE_STATUS_IO;
    }

    g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
    while (g_variant_iter_loop(objects, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariant *device_props = g_variant_lookup_value(interfaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
        if (device_props != NULL) {
            const char *address = NULL;
            const char *name = NULL;
            const char *alias = NULL;

            (void)g_variant_lookup(device_props, "Address", "&s", &address);
            (void)g_variant_lookup(device_props, "Name", "&s", &name);
            (void)g_variant_lookup(device_props, "Alias", "&s", &alias);
            if ((address != NULL && g_ascii_strcasecmp(address, target_id) == 0) ||
                (name != NULL && g_ascii_strcasecmp(name, target_id) == 0) ||
                (alias != NULL && g_ascii_strcasecmp(alias, target_id) == 0)) {
                status = ble_linux_copy_string(g_ble_linux.device_path, sizeof(g_ble_linux.device_path), object_path);
                mod_ble_log_info("matched device path=%s address=%s name=%s alias=%s",
                    object_path,
                    address != NULL ? address : "-",
                    name != NULL ? name : "-",
                    alias != NULL ? alias : "-");
                g_variant_unref(device_props);
                break;
            }

            g_variant_unref(device_props);
        }
    }

    g_variant_iter_free(objects);
    g_variant_unref(reply);
    return status;
}

static void ble_linux_log_flags(GVariant *flags)
{
    GVariantIter iter;
    const gchar *flag = NULL;
    GString *joined = g_string_new("");

    g_variant_iter_init(&iter, flags);
    while (g_variant_iter_loop(&iter, "&s", &flag)) {
        if (joined->len > 0U) {
            g_string_append(joined, ",");
        }
        g_string_append(joined, flag);
    }

    mod_ble_log_info("    flags=%s", joined->str);
    g_string_free(joined, TRUE);
}

static int ble_linux_discover_gatt(const ble_client_context_t *ctx)
{
    GError *error = NULL;
    GVariant *reply = NULL;
    GVariantIter *objects = NULL;
    gchar *object_path = NULL;
    GVariant *interfaces = NULL;
    int status = MOD_BLE_STATUS_OK;

    reply = ble_linux_get_managed_objects(&error);
    if (reply == NULL) {
        mod_ble_log_error("GetManagedObjects failed during GATT discovery: %s", error->message);
        g_clear_error(&error);
        return MOD_BLE_STATUS_IO;
    }

    g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
    while (g_variant_iter_loop(objects, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        if (!g_str_has_prefix(object_path, g_ble_linux.device_path)) {
            continue;
        }

        {
            GVariant *service_props = g_variant_lookup_value(interfaces, "org.bluez.GattService1", G_VARIANT_TYPE("a{sv}"));
            if (service_props != NULL) {
                const char *uuid = NULL;
                gboolean primary = FALSE;

                (void)g_variant_lookup(service_props, "UUID", "&s", &uuid);
                (void)g_variant_lookup(service_props, "Primary", "b", &primary);
                mod_ble_log_info("service path=%s uuid=%s primary=%s", object_path, uuid != NULL ? uuid : "-", primary ? "true" : "false");
                g_variant_unref(service_props);
            }
        }

        {
            GVariant *char_props = g_variant_lookup_value(interfaces, "org.bluez.GattCharacteristic1", G_VARIANT_TYPE("a{sv}"));
            if (char_props != NULL) {
                const char *uuid = NULL;
                GVariant *flags = NULL;

                (void)g_variant_lookup(char_props, "UUID", "&s", &uuid);
                mod_ble_log_info("characteristic path=%s uuid=%s", object_path, uuid != NULL ? uuid : "-");
                flags = g_variant_lookup_value(char_props, "Flags", G_VARIANT_TYPE("as"));
                if (flags != NULL) {
                    ble_linux_log_flags(flags);
                    g_variant_unref(flags);
                }

                if (uuid != NULL) {
                    if (ctx->config.gatt.write_char_uuid[0] != '\0' &&
                        g_ascii_strcasecmp(uuid, ctx->config.gatt.write_char_uuid) == 0) {
                        status = ble_linux_copy_string(g_ble_linux.write_char_path, sizeof(g_ble_linux.write_char_path), object_path);
                    }
                    if (ctx->config.gatt.notify_char_uuid[0] != '\0' &&
                        g_ascii_strcasecmp(uuid, ctx->config.gatt.notify_char_uuid) == 0) {
                        status = ble_linux_copy_string(g_ble_linux.notify_char_path, sizeof(g_ble_linux.notify_char_path), object_path);
                    }
                }

                g_variant_unref(char_props);
            }
        }
        if (status != MOD_BLE_STATUS_OK) {
            break;
        }
    }

    g_variant_iter_free(objects);
    g_variant_unref(reply);

    if (status == MOD_BLE_STATUS_OK) {
        if (g_ble_linux.write_char_path[0] != '\0') {
            mod_ble_log_info("selected write characteristic=%s", g_ble_linux.write_char_path);
        }
        if (g_ble_linux.notify_char_path[0] != '\0') {
            mod_ble_log_info("selected notify characteristic=%s", g_ble_linux.notify_char_path);
        }
    }

    return status;
}

static void ble_linux_on_properties_changed(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    const gchar *changed_interface = NULL;
    GVariant *changed_props = NULL;
    GVariant *invalidated = NULL;
    GVariant *value = NULL;
    GVariantIter iter;
    guint8 byte = 0U;
    size_t len = 0U;

    (void)connection;
    (void)sender_name;
    (void)interface_name;
    (void)signal_name;
    (void)user_data;

    if (strcmp(object_path, g_ble_linux.notify_char_path) != 0) {
        return;
    }

    g_variant_get(parameters, "(&s@a{sv}@as)", &changed_interface, &changed_props, &invalidated);
    if (strcmp(changed_interface, "org.bluez.GattCharacteristic1") != 0) {
        g_variant_unref(changed_props);
        g_variant_unref(invalidated);
        return;
    }

    value = g_variant_lookup_value(changed_props, "Value", G_VARIANT_TYPE("ay"));
    if (value != NULL) {
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_loop(&iter, "y", &byte)) {
            if (len < sizeof(g_ble_linux.notify_buf)) {
                g_ble_linux.notify_buf[len++] = byte;
            }
        }
        g_ble_linux.notify_len = len;
        g_ble_linux.notify_ready = 1;
        mod_ble_log_info("notify received len=%zu", len);
        g_variant_unref(value);
    }

    g_variant_unref(changed_props);
    g_variant_unref(invalidated);
}

static int ble_linux_wait_for_device(const ble_client_context_t *ctx)
{
    gint64 deadline_us = g_get_monotonic_time() + ((gint64)ctx->config.scan_timeout_ms * 1000);

    if (ble_linux_target_is_mac(ctx->config.target_id) &&
        ble_linux_build_device_path_from_mac(ctx->config.target_id) == MOD_BLE_STATUS_OK &&
        ble_linux_device_exists_on_bus(g_ble_linux.device_path)) {
        mod_ble_log_info("matched cached device path=%s from MAC", g_ble_linux.device_path);
        return MOD_BLE_STATUS_OK;
    }

    while (g_get_monotonic_time() < deadline_us) {
        if (ble_linux_find_device_path(ctx->config.target_id) == MOD_BLE_STATUS_OK) {
            return MOD_BLE_STATUS_OK;
        }
        g_usleep(200000);
    }

    return MOD_BLE_STATUS_TIMEOUT;
}

int ble_client_open(ble_client_context_t *ctx)
{
    GError *error = NULL;
    int status;

    if (ctx == NULL || ctx->config.target_id[0] == '\0') {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    ble_linux_state_reset();
    g_ble_linux.connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (g_ble_linux.connection == NULL) {
        mod_ble_log_error("failed to connect to system bus: %s", error->message);
        g_clear_error(&error);
        return MOD_BLE_STATUS_IO;
    }

    status = ble_linux_find_adapter_path();
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }
    mod_ble_log_info("using adapter path=%s", g_ble_linux.adapter_path);

    (void)ble_linux_set_discovery_filter();

    status = ble_linux_call_void(g_ble_linux.adapter_path, "org.bluez.Adapter1", "StartDiscovery", NULL);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    status = ble_linux_wait_for_device(ctx);
    (void)ble_linux_call_void(g_ble_linux.adapter_path, "org.bluez.Adapter1", "StopDiscovery", NULL);
    if (status != MOD_BLE_STATUS_OK) {
        mod_ble_log_error("target %s not found within %d ms", ctx->config.target_id, ctx->config.scan_timeout_ms);
        return status;
    }

    ctx->is_connected = 1;
    if (ctx->config.mode == MOD_BLE_MODE_SCAN_ONLY) {
        mod_ble_log_info("scan-only mode complete");
        return MOD_BLE_STATUS_OK;
    }

    status = ble_linux_call_void(g_ble_linux.device_path, "org.bluez.Device1", "Connect", NULL);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    status = ble_linux_discover_gatt(ctx);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    if (g_ble_linux.notify_char_path[0] != '\0') {
        g_ble_linux.signal_subscription_id = g_dbus_connection_signal_subscribe(
            g_ble_linux.connection,
            "org.bluez",
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            g_ble_linux.notify_char_path,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            ble_linux_on_properties_changed,
            NULL,
            NULL);
    }

    return MOD_BLE_STATUS_OK;
}

int ble_client_send(ble_client_context_t *ctx, const uint8_t *data, size_t len)
{
    GVariantBuilder bytes_builder;
    GVariantBuilder options_builder;

    if (ctx == NULL || data == NULL || len == 0U) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (!ctx->is_connected) {
        return MOD_BLE_STATUS_STATE;
    }
    if (ctx->config.mode != MOD_BLE_MODE_FULL) {
        return MOD_BLE_STATUS_UNSUPPORTED;
    }
    if (g_ble_linux.write_char_path[0] == '\0') {
        mod_ble_log_error("write characteristic UUID not configured or not found");
        return MOD_BLE_STATUS_UNSUPPORTED;
    }

    g_variant_builder_init(&bytes_builder, G_VARIANT_TYPE("ay"));
    for (size_t i = 0; i < len; ++i) {
        g_variant_builder_add(&bytes_builder, "y", data[i]);
    }

    g_variant_builder_init(&options_builder, G_VARIANT_TYPE("a{sv}"));
    return ble_linux_call_void(
        g_ble_linux.write_char_path,
        "org.bluez.GattCharacteristic1",
        "WriteValue",
        g_variant_new("(aya{sv})", &bytes_builder, &options_builder));
}

int ble_client_receive(ble_client_context_t *ctx, uint8_t *buf, size_t buf_size, int timeout_ms)
{
    gint64 deadline_us;

    if (ctx == NULL || buf == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (!ctx->is_connected) {
        return MOD_BLE_STATUS_STATE;
    }
    if (ctx->config.mode != MOD_BLE_MODE_FULL) {
        return MOD_BLE_STATUS_UNSUPPORTED;
    }
    if (g_ble_linux.notify_char_path[0] == '\0') {
        mod_ble_log_error("notify characteristic UUID not configured or not found");
        return MOD_BLE_STATUS_UNSUPPORTED;
    }

    if (!g_ble_linux.notify_started) {
        int status = ble_linux_call_void(g_ble_linux.notify_char_path, "org.bluez.GattCharacteristic1", "StartNotify", NULL);
        if (status != MOD_BLE_STATUS_OK) {
            return status;
        }
        g_ble_linux.notify_started = 1;
    }

    g_ble_linux.notify_ready = 0;
    deadline_us = g_get_monotonic_time() + ((gint64)timeout_ms * 1000);
    while (g_get_monotonic_time() < deadline_us) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        if (g_ble_linux.notify_ready) {
            if (buf_size < g_ble_linux.notify_len) {
                return MOD_BLE_STATUS_INVALID_ARG;
            }
            (void)memcpy(buf, g_ble_linux.notify_buf, g_ble_linux.notify_len);
            return (int)g_ble_linux.notify_len;
        }
        g_usleep(10000);
    }

    return MOD_BLE_STATUS_TIMEOUT;
}

void ble_client_close(ble_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (g_ble_linux.connection != NULL) {
        if (g_ble_linux.notify_started && g_ble_linux.notify_char_path[0] != '\0') {
            (void)ble_linux_call_void(g_ble_linux.notify_char_path, "org.bluez.GattCharacteristic1", "StopNotify", NULL);
        }
        if (g_ble_linux.signal_subscription_id != 0U) {
            g_dbus_connection_signal_unsubscribe(g_ble_linux.connection, g_ble_linux.signal_subscription_id);
        }
        if (g_ble_linux.device_path[0] != '\0' && ctx->config.mode != MOD_BLE_MODE_SCAN_ONLY) {
            (void)ble_linux_call_void(g_ble_linux.device_path, "org.bluez.Device1", "Disconnect", NULL);
        }
        g_object_unref(g_ble_linux.connection);
    }

    mod_ble_log_info("ble_client_close target=%s", ctx->config.target_id);
    ble_linux_state_reset();
    ctx->is_connected = 0;
}

#else

int ble_client_open(ble_client_context_t *ctx)
{
    if (ctx == NULL || ctx->config.target_id[0] == '\0') {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    ctx->is_connected = 1;
    mod_ble_log_info("ble_client_open target=%s", ctx->config.target_id);
    mod_ble_log_info("non-Linux or no gio found; using stub transport");
    return MOD_BLE_STATUS_OK;
}

int ble_client_send(ble_client_context_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    if (ctx == NULL || data == NULL || len == 0U) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (!ctx->is_connected) {
        return MOD_BLE_STATUS_STATE;
    }

    mod_ble_log_info("ble_client_send len=%zu", len);
    (void)fprintf(stdout, "[TX] ");
    for (i = 0; i < len; ++i) {
        (void)fprintf(stdout, "%02X ", data[i]);
    }
    (void)fprintf(stdout, "\n");
    return MOD_BLE_STATUS_OK;
}

int ble_client_receive(ble_client_context_t *ctx, uint8_t *buf, size_t buf_size, int timeout_ms)
{
    static const uint8_t stub_response[] = {0xA5, 0x03, 0x00, 0x80, 0x00, 0x80, 0x10, 0x00, 0x96};

    if (ctx == NULL || buf == NULL || buf_size < sizeof(stub_response)) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (!ctx->is_connected) {
        return MOD_BLE_STATUS_STATE;
    }

    (void)memcpy(buf, stub_response, sizeof(stub_response));
    mod_ble_log_info("ble_client_receive timeout_ms=%d stub_len=%zu", timeout_ms, sizeof(stub_response));
    return (int)sizeof(stub_response);
}

void ble_client_close(ble_client_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    mod_ble_log_info("ble_client_close target=%s", ctx->config.target_id);
    ctx->is_connected = 0;
}

#endif
