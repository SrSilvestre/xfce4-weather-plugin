#include <config.h>

#include <sys/stat.h>
#include <libxfce4util/libxfce4util.h>
#include <time.h>

#include "plugin.h"
#include "debug_print.h"
#include "translate.h"
#include "http_client.h"
#include "summary_window.h"
#include "config_dialog.h"
#include "icon.h"
#include "scrollbox.h"

gboolean check_envproxy(gchar **proxy_host, gint *proxy_port)
{
        char *env_proxy = getenv("HTTP_PROXY"), *tmp, **split;

        if (!env_proxy)
                return FALSE;

        tmp = strstr(env_proxy, "://");

        if (!tmp || strlen(tmp) < 3)
                return FALSE; 

        env_proxy = tmp + 3;

        /* we don't support username:password so return */
        tmp = strchr(env_proxy, '@');
        if (tmp)
                return FALSE;

        split = g_strsplit(env_proxy, ":", 2);

        if (!split[0])
                return FALSE;
        else if (!split[1])
        {
                g_strfreev(split);
                return FALSE;
        }

        *proxy_host = g_strdup(split[0]);
        *proxy_port = (int)strtol(split[1], NULL, 0);

        g_strfreev(split);

        return TRUE;
}

gint IconSizeSmall = 0;

gchar *make_label(struct xml_weather *weatherdata, enum datas opt, enum units unit, gint size)
{
        gchar *str, *lbl, *txtsize, *value;
        const gchar *rawvalue;

        switch (opt)
        {
                case VIS:       lbl = _("V"); break;
                case UV_INDEX:  lbl = _("U"); break;
                case WIND_DIRECTION: lbl = _("WD"); break;
                case BAR_D:     lbl = _("P"); break;
                case BAR_R:     lbl = _("P"); break;
                case FLIK:      lbl = _("F"); break;
                case TEMP:      lbl = _("T"); break;
                case DEWP:      lbl = _("D"); break;
                case HMID:      lbl = _("H"); break;
                case WIND_SPEED:lbl = _("WS"); break;
                case WIND_GUST: lbl = _("WG"); break;
                default: lbl = "?"; break;
        }

        /* arbitrary, choose something that works */
        if (size > 36)
                txtsize = "medium";
        else if (size > 30)
                txtsize = "small";
        else if (size > 24)     
                txtsize = "x-small";
        else
                txtsize="xx-small";

        rawvalue = get_data(weatherdata, opt);

        switch (opt)
        {
                case VIS:       value = translate_visibility(rawvalue, unit);
                                break;
                case WIND_DIRECTION: value = translate_wind_direction(rawvalue);
                                     break;
                case WIND_SPEED:
                case WIND_GUST: value = translate_wind_speed(rawvalue, unit);
                                break;
                default:
                                value = NULL;
                                break;
        }

       

        if (value != NULL)
        {
                str = g_strdup_printf("<span size=\"%s\">%s: %s</span>",
                                txtsize, lbl, value);
                g_free(value);
        }
        else
                str = g_strdup_printf("<span size=\"%s\">%s: %s %s</span>",
                                txtsize, lbl, rawvalue, get_unit(unit, opt));

        return str;
}

gchar *get_filename(const struct xfceweather_data *data)
{
        gchar *filename = 
                g_strdup_printf("xfce4/weather-plugin/weather_%s_%c.xml", 
                        data->location_code, data->unit == METRIC ? 'm' : 'i');
        gchar *fullfilename = 
                xfce_resource_save_location(XFCE_RESOURCE_CACHE, filename, 
                                            TRUE);
        g_free(filename);

        return fullfilename;
}

void set_icon_error(struct xfceweather_data *data)
{
        GdkPixbuf *icon = get_icon(data->iconimage, "25", data->iconsize);
        gtk_image_set_from_pixbuf(GTK_IMAGE(data->iconimage), icon);
        g_object_unref (icon);

        if (data->weatherdata)
        {
                xml_weather_free(data->weatherdata);
                data->weatherdata = NULL;
        }

        gtk_tooltips_set_tip(data->tooltips, data->tooltipbox, 
                             _("Cannot update weather data"), NULL);

        return;
}

void set_icon_current(struct xfceweather_data *data)
{
        int i;
        GdkPixbuf *icon;
        for (i = 0; i < data->labels->len; i++)
        {
                enum datas opt;
                gchar *str;

                opt = g_array_index(data->labels, enum datas, i);
               
                str = make_label(data->weatherdata, opt, data->unit,
                                data->size);

                gtk_scrollbox_set_label(GTK_SCROLLBOX(data->scrollbox), -1, 
                                str);
                g_free(str);

               
        }

        gtk_scrollbox_enablecb(GTK_SCROLLBOX(data->scrollbox), TRUE);       
                
        icon = get_icon(data->iconimage, get_data(data->weatherdata, WICON), data->iconsize);
        gtk_image_set_from_pixbuf(GTK_IMAGE(data->iconimage), icon);
        g_object_unref (icon);
       
        gtk_tooltips_set_tip (data->tooltips, data->tooltipbox, 
                translate_desc(get_data(data->weatherdata, TRANS)), NULL);
}

void cb_update(gboolean status, gpointer user_data)
{
        struct xfceweather_data *data = (struct xfceweather_data *)user_data;
        gchar *fullfilename = get_filename(data);
        xmlDoc *doc;
        xmlNode *cur_node;
        struct xml_weather *weather;

        if (!fullfilename)
                return;
        
        doc = xmlParseFile(fullfilename);
        g_free(fullfilename);

        if (!doc)
                return;

        cur_node = xmlDocGetRootElement(doc);

        if (cur_node)
                weather = parse_weather(cur_node);

        xmlFreeDoc(doc);

        gtk_scrollbox_clear(GTK_SCROLLBOX(data->scrollbox));

        if (weather)
        {
               
                if (data->weatherdata)
                        xml_weather_free(data->weatherdata);
               
                data->weatherdata = weather;
                set_icon_current(data);
        }
        else
        {
                set_icon_error(data);
                return;
        }
}

/* -1 error 0 no update needed 1 updating */
gint update_weatherdata(struct xfceweather_data *data, gboolean force)
{
        struct stat attrs; 
        /*gchar *fullfilename = xfce_resource_save_location(XFCE_RESOURCE_CACHE, 
                        filename, TRUE);*/
        gchar *fullfilename;

        if (!data->location_code)
                return -1;

        fullfilename = get_filename(data);

        if (!fullfilename)
        {
                DEBUG_PUTS("can't get savedir?\n");
                return -1;
        } 

        if (force || (stat(fullfilename, &attrs) == -1) || 
                        ((time(NULL) - attrs.st_mtime) > (UPDATE_TIME)))
        {
                gchar *url = g_strdup_printf("/weather/local/%s?cc=*&dayf=%d&unit=%c",
                                data->location_code, XML_WEATHER_DAYF_N,
                                data->unit == METRIC ? 'm' : 'i');
               

                gboolean status = http_get_file(url, "xoap.weather.com", fullfilename, 
                                data->proxy_host, data->proxy_port, cb_update,
                                (gpointer) data);
                g_free(url);
                g_free(fullfilename);

                return status ? 1 : -1;
        }
        else if (data->weatherdata)
                return 0;
        else
        {
                cb_update(TRUE, data);
                return 1;
        }
}

void update_plugin (struct xfceweather_data *data, gboolean force)
{
        if (update_weatherdata(data, force) == -1)
        {
                gtk_scrollbox_clear(GTK_SCROLLBOX(data->scrollbox));
                set_icon_error(data);
        }
        /* else update will be called through the callback in http_get_file() */
}

GArray *labels_clear (GArray *array)
{       
        if (!array || array->len > 0)
        {
                if (array)
                        g_array_free(array, TRUE);
                
                array = g_array_new(FALSE, TRUE, sizeof(enum datas));
        }

        return array;
}

void xfceweather_read_config (XfcePanelPlugin *plugin, struct xfceweather_data *data)
{
        char label[10];
        int i;
        const char *value;
        char *file;
        XfceRc *rc;
        
        if (!(file = xfce_panel_plugin_lookup_rc_file (plugin)))
                return;
        
        rc = xfce_rc_simple_open (file, TRUE);
        g_free (file);
        
        if (!rc)
                return;
    
        value = xfce_rc_read_entry (rc, "loc_code", NULL);

        if (value) 
        {
                if (data->location_code)
                        g_free(data->location_code);
                
                data->location_code = g_strdup(value);
        }

        if (xfce_rc_read_bool_entry (rc, "celsius", TRUE))
                data->unit = METRIC;
        else
                data->unit = IMPERIAL;

        if (data->proxy_host)
        {
                g_free(data->proxy_host);
                data->proxy_host = NULL;
        }

        if (data->saved_proxy_host)
        {
                g_free(data->saved_proxy_host);
                data->saved_proxy_host = NULL;
        }

        value = xfce_rc_read_entry (rc, "proxy_host", NULL);

        if (value && *value)
        {
                data->saved_proxy_host = g_strdup(value);
        }

        data->saved_proxy_port = xfce_rc_read_int_entry (rc, "proxy_port", 0);

        data->proxy_fromenv = xfce_rc_read_bool_entry (rc, "proxy_fromenv", FALSE);
        
        if (data->proxy_fromenv)
        {
                check_envproxy(&data->proxy_host, &data->proxy_port); 
        }
        else
        {
                data->proxy_host = g_strdup(data->saved_proxy_host);
                data->proxy_port = data->saved_proxy_port;
        }

        data->labels = labels_clear(data->labels); 
        
        for (i = 0; i < 100 /* arbitrary */; ++i) {
                int val;
                
                g_snprintf (label, 10, "label%d", i);

                val = xfce_rc_read_int_entry (rc, label, -1);

                if (val >= 0)
                        g_array_append_val(data->labels, val);
                else
                        break;
        }

        xfce_rc_close (rc);
}

void xfceweather_write_config (XfcePanelPlugin *plugin, 
                               struct xfceweather_data *data)
{
        char label[10];
        int i = 0;
        XfceRc *rc;
        char *file;
        
        if (!(file = xfce_panel_plugin_save_location (plugin, TRUE)))
                return;
        
        /* get rid of old values */
        unlink (file);
        
        rc = xfce_rc_simple_open (file, FALSE);
        g_free (file);
        
        if (!rc)
                return;    

        xfce_rc_write_bool_entry (rc, "celcius", (data->unit == METRIC));

        if (data->location_code)
                xfce_rc_write_entry (rc, "loc_code", data->location_code);

        xfce_rc_write_bool_entry (rc, "proxy_fromenv", data->proxy_fromenv);
        
        if (data->proxy_host)
        {
                xfce_rc_write_entry (rc, "proxy_host", data->proxy_host);

                xfce_rc_write_int_entry (rc, "proxy_port", data->proxy_port);
        }

        for (i = 0; i < data->labels->len; i++) 
        {
                g_snprintf (label, 10, "label%d", i);

                xfce_rc_write_int_entry (rc, label, (int)g_array_index (data->labels, enum datas, i));
        }

        xfce_rc_close (rc);
}

gboolean update_cb(struct xfceweather_data *data)
{
        DEBUG_PUTS("update_cb(): callback called\n");
        
        update_plugin(data, FALSE);

        DEBUG_PUTS("update_cb(): request added, returning\n");
        
        return TRUE;
}

void update_plugin_with_reset(struct xfceweather_data *data, gboolean force)
{
        if (data->updatetimeout)
                g_source_remove(data->updatetimeout);

        update_plugin(data, force);

        data->updatetimeout = gtk_timeout_add(UPDATE_TIME * 1000, (GSourceFunc)update_cb, data);
}


void update_config(struct xfceweather_data *data)
{
        update_plugin_with_reset(data, TRUE); /* force because units could have changed */
}

void close_summary(GtkWidget *widget, gpointer *user_data)
{
        struct xfceweather_data *data = (struct xfceweather_data *)user_data;

        data->summary_window = NULL;
}

gboolean cb_click(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
        struct xfceweather_data *data = (struct xfceweather_data *)user_data;

        if (event->button == 1)
        {
               
                if (data->summary_window != NULL)
                {
                       
                        gtk_window_present(GTK_WINDOW(data->summary_window));
                }
                else
                {
                        data->summary_window = create_summary_window(data->weatherdata,
                                        data->unit);
                        g_signal_connect(data->summary_window, "destroy",
                                        G_CALLBACK(close_summary), (gpointer)data);
                                        
                        gtk_widget_show_all(data->summary_window);
                }
        }
        else if (event->button == 2)
                update_plugin_with_reset(data, TRUE);

        return FALSE;
}


static void xfceweather_dialog_response (GtkWidget *dlg, int response, 
                struct xfceweather_dialog *dialog)
{
        struct xfceweather_data *data = dialog->wd;
        
        apply_options (dialog);
            
        gtk_widget_destroy (dlg);
        xfce_panel_plugin_unblock_menu (data->plugin);
        xfceweather_write_config (data->plugin, data);
}

void xfceweather_create_options(XfcePanelPlugin *plugin, struct xfceweather_data *data)
{
        GtkWidget *dlg, *header, *vbox;
        struct xfceweather_dialog *dialog;
        
        xfce_panel_plugin_block_menu (plugin);

        dlg = gtk_dialog_new_with_buttons (_("Properties"), 
                GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
                GTK_DIALOG_DESTROY_WITH_PARENT |
                GTK_DIALOG_NO_SEPARATOR,
                GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
                NULL);

        gtk_container_set_border_width (GTK_CONTAINER (dlg), 2);

        header = xfce_create_header (NULL, _("Weather Update"));
        gtk_widget_set_size_request (GTK_BIN (header)->child, -1, 32);
        gtk_container_set_border_width (GTK_CONTAINER (header), BORDER - 2);
        gtk_widget_show (header);
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), header,
                            FALSE, TRUE, 0);

        vbox = gtk_vbox_new(FALSE, BORDER);
        gtk_container_set_border_width (GTK_CONTAINER (vbox), BORDER - 2);
        gtk_widget_show(vbox);
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), vbox,
                            TRUE, TRUE, 0);
    
        dialog = create_config_dialog(data, vbox);

        g_signal_connect (dlg, "response", G_CALLBACK (xfceweather_dialog_response),
                          dialog);

        set_callback_config_dialog(dialog, update_config);

        gtk_widget_show (dlg);
}
       

struct xfceweather_data *xfceweather_create_control(XfcePanelPlugin *plugin)
{
        struct xfceweather_data *data = g_new0(struct xfceweather_data, 1);
        GtkWidget *vbox;
        enum datas lbl;
        GdkPixbuf *icon;

        if (!IconSizeSmall)
                IconSizeSmall = gtk_icon_size_register("iconsize_small", 16, 16);

        data->plugin = plugin;

        data->tooltips = gtk_tooltips_new ();
        g_object_ref (data->tooltips);
        gtk_object_sink (GTK_OBJECT (data->tooltips));

        data->scrollbox = gtk_scrollbox_new();
       
        icon = get_icon(GTK_WIDGET (plugin), "-", IconSizeSmall);
        data->iconimage = gtk_image_new_from_pixbuf(icon);
        gtk_misc_set_alignment(GTK_MISC(data->iconimage), 0.5, 1);
        g_object_unref (icon);

        data->labels = g_array_new(FALSE, TRUE, sizeof(enum datas));
       
        vbox = gtk_vbox_new(FALSE, 0);

        gtk_box_pack_start(GTK_BOX(vbox), data->iconimage, TRUE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), data->scrollbox, TRUE, TRUE, 0); 

        data->tooltipbox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(data->tooltipbox), vbox);
        gtk_widget_show_all(data->tooltipbox);
        
        xfce_panel_plugin_add_action_widget (plugin, data->tooltipbox);

        g_signal_connect(data->tooltipbox, "button-press-event", 
                        G_CALLBACK(cb_click), (gpointer)data);

        /* assign to tempval because g_array_append_val() is using & operator */
        lbl = FLIK;
        g_array_append_val(data->labels, lbl);
        lbl = TEMP;
        g_array_append_val(data->labels, lbl);

        /* FIXME Without this the first label looks odd, because 
         * the gc isn't created yet */
        gtk_scrollbox_set_label(GTK_SCROLLBOX(data->scrollbox), -1, "1");
        gtk_scrollbox_clear(GTK_SCROLLBOX(data->scrollbox));

        data->updatetimeout = 
                gtk_timeout_add(UPDATE_TIME * 1000, (GSourceFunc)update_cb, data);    

        return data;
}

void xfceweather_free(XfcePanelPlugin *plugin, struct xfceweather_data *data)
{
        if (data->weatherdata)
                xml_weather_free(data->weatherdata);

        if (data->updatetimeout)
        {
                g_source_remove (data->updatetimeout);
                data->updatetimeout = 0;
        }
        
        g_free(data->location_code);
        free_get_data_buffer();  
        
        g_array_free(data->labels, TRUE);

        xmlCleanupParser();

        g_free (data);
}

gboolean xfceweather_set_size(XfcePanelPlugin *panel, int size, 
                              struct xfceweather_data *data)
{
        data->size = size;

        /* arbitrary, choose something that works */
        if (size <= 30)
                data->iconsize = IconSizeSmall;
        else if (size <= 36)
                data->iconsize = GTK_ICON_SIZE_BUTTON;
        else if (size <= 48)
                data->iconsize = GTK_ICON_SIZE_LARGE_TOOLBAR;
        else
                data->iconsize = GTK_ICON_SIZE_DND;

        update_plugin(data, FALSE);

        return TRUE;
}

static void
weather_construct (XfcePanelPlugin *plugin)
{
        char *path;
        struct xfceweather_data *data;

        xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");
        
        path = g_strdup_printf("%s%s%s%s", THEMESDIR, G_DIR_SEPARATOR_S,
                        DEFAULT_W_THEME, G_DIR_SEPARATOR_S);
        register_icons(path);
        g_free(path);

        data = xfceweather_create_control (plugin);
        
        xfceweather_read_config (plugin, data);

        xfceweather_set_size (plugin, xfce_panel_plugin_get_size (plugin), 
                              data);
        
        gtk_container_add (GTK_CONTAINER (plugin), data->tooltipbox);
        
        g_signal_connect (plugin, "free-data", G_CALLBACK (xfceweather_free), 
                          data);
        
        g_signal_connect (plugin, "save", 
                          G_CALLBACK (xfceweather_write_config), data);
        
        g_signal_connect (plugin, "size-changed", 
                          G_CALLBACK (xfceweather_set_size), data);

        xfce_panel_plugin_menu_show_configure (plugin);
        g_signal_connect (plugin, "configure-plugin", 
                          G_CALLBACK (xfceweather_create_options), data);
}

XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL (weather_construct);

