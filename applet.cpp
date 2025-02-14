// SPDX-License-Identifier: MIT

#include <libxfce4panel/xfce-panel-plugin-provider.h>
#include <libxfce4util/libxfce4util.h>
#include <gtk/gtkx.h>

#include <string>
#include <vector>

using namespace std;

constexpr auto g_mate_panel_applet_iface = "org.mate.panel.applet.Applet";
constexpr auto g_rc_key_file_path = "mate-panel-applet";
constexpr auto g_rc_key_applet_name = "name";
constexpr auto g_mate_config_prefix = "/org/mate/panel/objects-xfce4/prefs-";
static string g_mate_applet_config_path;
static string g_mate_applet_dbus_bus_name;
static string g_mate_dbus_applet_path;
static string g_mate_applet_name;
static GDBusConnection *g_conn = nullptr;
static GDBusProxy *g_proxy = nullptr;
static GtkWidget *g_widget = nullptr;
static bool g_widget_expand_only = false;

enum MatePanelAppletFlags
{
    MATE_PANEL_APPLET_FLAGS_NONE   = 0,
    MATE_PANEL_APPLET_EXPAND_MAJOR = 1 << 0,
    MATE_PANEL_APPLET_EXPAND_MINOR = 1 << 1,
    MATE_PANEL_APPLET_HAS_HANDLE   = 1 << 2,
};

static void handle_mate_applet_properties_changed(
        GDBusConnection *connection,
        const gchar     *sender_name,
        const gchar     *object_path,
        const gchar     *interface_name,
        const gchar     *signal_name,
        GVariant        *parameters,
        XfcePanelPlugin *plugin)
{
    if (g_strcmp0(signal_name, "PropertiesChanged") != 0)
        return;

    GVariant *props = nullptr;
    g_variant_get(parameters, "(s@a{sv}*)", nullptr, &props, nullptr);

    GVariantIter iter_props;
    gchar *key = nullptr;
    GVariant *value = nullptr;
    g_variant_iter_init(&iter_props, props);
    while (g_variant_iter_loop(&iter_props, "{sv}", &key, &value))
    {
        if (g_strcmp0(key, "SizeHints") == 0)
        {
#if 0
            gint size_hint[2] = {0, 0};
            GVariantIter iter_size;
            g_variant_iter_init(&iter_size, value);
            for (int i = 0; i < 2; ++i)
            {
                if (!g_variant_iter_loop(&iter_size, "i", &size_hint[i]))
                    break;
            }
#endif
        }
        else if (g_strcmp0(key, "Flags") == 0)
        {
            guint flags = 0;
            g_variant_get(value, "u", &flags);
            xfce_panel_plugin_set_expand(plugin, (flags & MATE_PANEL_APPLET_EXPAND_MAJOR));
        }
    }

    g_variant_unref(props);
}

static void handle_mate_applet_signal(
        GDBusProxy      *proxy,
        gchar           *sender_name,
        gchar           *signal_name,
        GVariant        *parameters,
        XfcePanelPlugin *plugin)
{
    auto plugin_provider = XFCE_PANEL_PLUGIN_PROVIDER(plugin);
    if (g_strcmp0(signal_name, "Move") == 0)
    {
        xfce_panel_plugin_provider_emit_signal(plugin_provider, PROVIDER_SIGNAL_MOVE_PLUGIN);
    }
    else if (g_strcmp0(signal_name, "RemoveFromPanel") == 0)
    {
        xfce_panel_plugin_provider_ask_remove(plugin_provider);
    }
    else if (g_strcmp0(signal_name, "Lock") == 0)
    {
        xfce_panel_plugin_provider_set_locked(plugin_provider, true);
    }
    else if (g_strcmp0(signal_name, "Unlock") == 0)
    {
        xfce_panel_plugin_provider_set_locked(plugin_provider, false);
    }
}

static bool set_mate_applet_property(const gchar *prop_name, GVariant *prop_value)
{
    auto set_prefs_path_result = g_dbus_connection_call_sync(
        g_conn,
        g_mate_applet_dbus_bus_name.c_str(),
        g_mate_dbus_applet_path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Set",
        g_variant_new(
            "(ssv)",
            g_mate_panel_applet_iface,
            prop_name,
            prop_value
        ),
        nullptr,
        G_DBUS_CALL_FLAGS_NO_AUTO_START,
        -1,
        nullptr,
        nullptr
    );

    if (!set_prefs_path_result)
        return false;

    g_variant_unref(set_prefs_path_result);
    return true;
}

static gboolean plug_removed(XfcePanelPlugin *plugin)
{
    // FIXME: Find better way to notify the xfce4-panel to restart the plugin
    exit(-1);
    return false;
}

static void widget_resized(GtkWidget *self, GtkAllocation *allocation, gpointer)
{
    if (g_widget_expand_only)
    {
        gint w = 0;
        gint h = 0;
        gtk_widget_get_size_request(self, &w, &h);
        gtk_widget_set_size_request(self, max(w, allocation->width), h);
    }
}

static void configure_plugin(XfcePanelPlugin *plugin, gpointer)
{
    g_dbus_proxy_call_sync(
        g_proxy,
        "PopupMenu",
        g_variant_new("(uu)", 0, 0),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        nullptr
    );
}
static void size_changed(XfcePanelPlugin *plugin, gpointer)
{
    if (g_widget_expand_only)
    {
        gtk_widget_set_size_request(g_widget, -1, -1);
    }

    set_mate_applet_property(
        "Size",
        g_variant_new_uint32(xfce_panel_plugin_get_size(plugin))
    );
}
static void mode_changed(XfcePanelPlugin *plugin, gpointer)
{
    if (g_widget_expand_only)
    {
        gtk_widget_set_size_request(g_widget, -1, -1);
    }

    const guint orient = (xfce_panel_plugin_get_mode(plugin) == XFCE_PANEL_PLUGIN_MODE_VERTICAL)
        ? 2
        : 0
    ;
    set_mate_applet_property("Orient", g_variant_new_uint32(orient));
}
static void removed(XfcePanelPlugin *plugin, gpointer)
{
    if (g_mate_applet_config_path.rfind(g_mate_config_prefix, 0) != string::npos)
    {
        // FIXME: Use API
        g_spawn_command_line_async(("dconf reset -f " + g_mate_applet_config_path).c_str(), nullptr);
    }
}
static void free_data(XfcePanelPlugin *plugin, gpointer)
{
    if (g_widget)
    {
        gtk_widget_destroy(g_widget);
    }

    g_dbus_connection_close_sync(g_conn, nullptr, nullptr);
}

static bool load_mate_applet(
        XfcePanelPlugin *plugin,
        const gchar *applet_config_path,
        const gchar *stored_applet_name = nullptr)
{
    if (!applet_config_path)
        return false;

    // Load MATE applet facory Id from file
    auto key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, applet_config_path, G_KEY_FILE_NONE, nullptr))
    {
        g_key_file_free(key_file);
        return false;
    }

    vector<string> applet_names;

    gsize n_groups = 0;
    if (auto groups = g_key_file_get_groups(key_file, &n_groups))
    {
        if (n_groups > 1)
        {
            applet_names.reserve(n_groups - 1);
            for (gsize i = 0; i < n_groups; ++i)
            {
                if (g_strcmp0(groups[i], "Applet Factory") != 0)
                    applet_names.push_back(groups[i]);
            }
        }
        g_strfreev(groups);
    }

    auto applet_factory_id_raw = g_key_file_get_string(key_file, "Applet Factory", "Id", nullptr);

    g_key_file_free(key_file);

    if (!applet_factory_id_raw)
        return false;

    const string applet_factory_id(applet_factory_id_raw);
    g_free(applet_factory_id_raw);

    if (stored_applet_name)
    {
        for (auto &&applet_name : applet_names)
        {
            if (applet_name == stored_applet_name)
            {
                g_mate_applet_name = applet_name;
                break;
            }
        }
    }
    else if (applet_names.size() == 1)
    {
        g_mate_applet_name = applet_names[0];
    }
    else for (auto &&applet_name : applet_names)
    {
        auto dlg = gtk_message_dialog_new(
            nullptr,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_YES_NO,
            "Do you want to load: %s?",
            applet_name.c_str()
        );
        auto res = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);

        if (res == GTK_RESPONSE_YES)
        {
            g_mate_applet_name = applet_name;
            break;
        }
    }

    if (g_mate_applet_name.empty())
        return false;

    // Obtain D-Bus values from MATE applet facory Id
    g_mate_applet_dbus_bus_name = "org.mate.panel.applet." + applet_factory_id;
    const auto obj_path = "/org/mate/panel/applet/" + applet_factory_id;

    // Create applet instance
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    auto get_applet_result = g_dbus_connection_call_sync(
        g_conn,
        g_mate_applet_dbus_bus_name.c_str(),
        obj_path.c_str(),
        "org.mate.panel.applet.AppletFactory",
        "GetApplet",
        g_variant_new("(sia{sv})", g_mate_applet_name.c_str(), 0, &builder),
        G_VARIANT_TYPE("(obuu)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        nullptr
    );
    g_variant_builder_clear(&builder);
    if (!get_applet_result)
        return false;

    // Get data from D-Bus call result
    const gchar *applet_path = nullptr;
    gboolean out_of_process = false;
    guint xid = 0;
    guint uid = 0;
    g_variant_get(get_applet_result, "(&obuu)", &applet_path, &out_of_process, &xid, &uid);
    g_mate_dbus_applet_path = applet_path;
    g_variant_unref(get_applet_result);

    // Set MATE applet config path
    g_mate_applet_config_path = g_mate_config_prefix + to_string(xfce_panel_plugin_get_unique_id(plugin)) + "/";
    if (!set_mate_applet_property("PrefsPath", g_variant_new_string(g_mate_applet_config_path.c_str())))
        return false;

    // Connect signals from MATE applet
    g_proxy = g_dbus_proxy_new_sync(
        g_conn,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        nullptr,
        g_mate_applet_dbus_bus_name.c_str(),
        g_mate_dbus_applet_path.c_str(),
        g_mate_panel_applet_iface,
        nullptr,
        nullptr
    );
    if (!g_proxy)
        return false;

    g_dbus_connection_signal_subscribe(
        g_conn,
        g_dbus_proxy_get_name(g_proxy),
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_mate_dbus_applet_path.c_str(),
        g_mate_panel_applet_iface,
        G_DBUS_SIGNAL_FLAGS_NONE,
        reinterpret_cast<GDBusSignalCallback>(handle_mate_applet_properties_changed),
        plugin,
        nullptr
    );

    g_signal_connect(
        g_proxy,
        "g-signal",
        G_CALLBACK(handle_mate_applet_signal),
        plugin
    );

    // Embed MATE applet widget
    g_widget = gtk_socket_new();
    g_signal_connect_swapped(
        g_widget,
        "plug-removed",
        G_CALLBACK(plug_removed),
        plugin
    );
    gtk_container_add(GTK_CONTAINER(plugin), g_widget);
    gtk_widget_show(g_widget);
    gtk_socket_add_id(GTK_SOCKET(g_widget), xid);

    if (g_mate_applet_name == "NetspeedApplet")
    {
        // Prevent size changes
        g_widget_expand_only = true;
        g_signal_connect(
            g_widget,
            "size-allocate",
            G_CALLBACK(widget_resized),
            nullptr
        );
    }

    return true;
}

static gchar *get_applet_file_path()
{
    gchar *file_path = nullptr;

    auto file_dialog = gtk_file_chooser_dialog_new(
        "Choose the MATE panel applet",
        nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel",
        GTK_RESPONSE_CANCEL,
        "_Open",
        GTK_RESPONSE_ACCEPT,
        nullptr
    );

    gtk_file_chooser_set_current_folder(
        GTK_FILE_CHOOSER(file_dialog),
        DATAROOTDIR_PATH "/mate-panel/applets"
    );

    auto filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.mate-panel-applet");
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(file_dialog)) == GTK_RESPONSE_ACCEPT)
        file_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_dialog));

    gtk_widget_destroy(file_dialog);

    return file_path;
}

extern "C" void mateappletloader_construct(XfcePanelPlugin *plugin)
{
    g_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (G_UNLIKELY(!g_conn))
        return;

    g_signal_connect(
        plugin,
        "free-data",
        G_CALLBACK(free_data),
        nullptr
    );

    bool has_config = false;
    bool loaded = false;

    if (auto file = xfce_panel_plugin_lookup_rc_file(plugin))
    {
        if (auto rc = xfce_rc_simple_open(file, true))
        {
            auto applet_config_path = xfce_rc_read_entry(rc, g_rc_key_file_path, nullptr);
            auto applet_name = xfce_rc_read_entry(rc, g_rc_key_applet_name, nullptr);
            loaded = load_mate_applet(plugin, applet_config_path, applet_name);
            if (applet_config_path)
                has_config = true;
            xfce_rc_close(rc);
        }
        g_free(file);
    }

    if (!has_config)
    {
        g_assert(!loaded);
        auto applet_file_path = get_applet_file_path();
        loaded = load_mate_applet(plugin, applet_file_path);
        if (loaded)
        {
            if (auto file = xfce_panel_plugin_save_location(plugin, true))
            {
                if (auto rc = xfce_rc_simple_open(file, false))
                {
                    xfce_rc_write_entry(rc, g_rc_key_file_path, applet_file_path);
                    xfce_rc_write_entry(rc, g_rc_key_applet_name, g_mate_applet_name.c_str());
                    xfce_rc_close(rc);
                }
                g_free(file);
            }
        }
        else if (applet_file_path)
        {
            if (auto applet_file_name = g_strrstr(applet_file_path, "/"))
            {
                applet_file_name += 1;
                auto dlg = gtk_message_dialog_new(
                    nullptr,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "Unable to load: %s",
                    applet_file_name
                );
                gtk_dialog_run(GTK_DIALOG(dlg));
                gtk_widget_destroy(dlg);
            }
        }
        g_free(applet_file_path);
    }

    if (loaded)
    {
        xfce_panel_plugin_menu_show_configure(plugin);

        g_signal_connect(
            plugin,
            "configure-plugin",
            G_CALLBACK(configure_plugin),
            nullptr
        );
        g_signal_connect(
            plugin,
            "size-changed",
            G_CALLBACK(size_changed),
            nullptr
        );
        g_signal_connect(
            plugin,
            "mode-changed",
            G_CALLBACK(mode_changed),
            nullptr
        );
        g_signal_connect(
            plugin,
            "removed",
            G_CALLBACK(removed),
            nullptr
        );
    }
    else if (!has_config)
    {
        xfce_panel_plugin_remove(plugin);
    }
}
