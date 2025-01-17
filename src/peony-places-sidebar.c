/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 *  Peony
 *  Copyright (C) 2017, Tianjin KYLIN Information Technology Co., Ltd.
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Authors : Mr Jamie McCracken (jamiemcc at blueyonder dot co dot uk)
 *            Cosimo Cecchi <cosimoc@gnome.org>
 *
 */

#include <config.h>

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <eel/eel-stock-dialogs.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libpeony-private/peony-debug-log.h>
#include <libpeony-private/peony-dnd.h>
#include <libpeony-private/peony-bookmark.h>
#include <libpeony-private/peony-global-preferences.h>
#include <libpeony-private/peony-sidebar-provider.h>
#include <libpeony-private/peony-module.h>
#include <libpeony-private/peony-file.h>
#include <libpeony-private/peony-file-utilities.h>
#include <libpeony-private/peony-file-operations.h>
#include <libpeony-private/peony-trash-monitor.h>
#include <libpeony-private/peony-icon-names.h>
#include <libpeony-private/peony-autorun.h>
#include <libpeony-private/peony-window-info.h>
#include <libpeony-private/peony-window-slot-info.h>
#include <gio/gio.h>
#include <libnotify/notify.h>

#include "peony-bookmark-list.h"
#include "peony-places-sidebar.h"
#include "peony-window.h"
#include "peony-kdisk-format.h"
#define EJECT_BUTTON_XPAD 6
#define ICON_CELL_XPAD 6

gboolean can_receive=FALSE;

typedef struct
{
    GtkScrolledWindow  parent;
    GtkTreeView        *tree_view;
    GtkCellRenderer    *eject_text_cell_renderer;
    GtkCellRenderer    *icon_cell_renderer;
    GtkCellRenderer    *icon_padding_cell_renderer;
    GtkCellRenderer    *padding_cell_renderer;
    char               *uri;
    GtkListStore       *store;
    GtkTreeModel       *filter_model;
    PeonyWindowInfo *window;
    PeonyBookmarkList *bookmarks;
    GVolumeMonitor *volume_monitor;

    gboolean devices_header_added;
    gboolean bookmarks_header_added;

    /* DnD */
    GList     *drag_list;
    gboolean  drag_data_received;
    int       drag_data_info;
    gboolean  drop_occured;

    GtkWidget *popup_menu;
    GtkWidget *popup_menu_open_in_new_tab_item;
    GtkWidget *popup_menu_remove_item;
    GtkWidget *popup_menu_rename_item;
    GtkWidget *popup_menu_separator_item;
    GtkWidget *popup_menu_mount_item;
    GtkWidget *popup_menu_unmount_item;
    GtkWidget *popup_menu_eject_item;
    GtkWidget *popup_menu_rescan_item;
    GtkWidget *popup_menu_format_item;
    GtkWidget *popup_menu_empty_trash_item;
    GtkWidget *popup_menu_start_item;
    GtkWidget *popup_menu_stop_item;

    /* volume mounting - delayed open process */
    gboolean mounting;
    PeonyWindowSlotInfo *go_to_after_mount_slot;
    PeonyWindowOpenFlags go_to_after_mount_flags;

    GtkTreePath *eject_highlight_path;
} PeonyPlacesSidebar;

typedef struct
{
    GtkScrolledWindowClass parent;
} PeonyPlacesSidebarClass;

typedef struct
{
    GObject parent;
} PeonyPlacesSidebarProvider;

typedef struct
{
    GObjectClass parent;
} PeonyPlacesSidebarProviderClass;

enum
{
    PLACES_SIDEBAR_COLUMN_ROW_TYPE,
    PLACES_SIDEBAR_COLUMN_URI,
    PLACES_SIDEBAR_COLUMN_DRIVE,
    PLACES_SIDEBAR_COLUMN_VOLUME,
    PLACES_SIDEBAR_COLUMN_MOUNT,
    PLACES_SIDEBAR_COLUMN_NAME,
    PLACES_SIDEBAR_COLUMN_ICON,
    PLACES_SIDEBAR_COLUMN_INDEX,
    PLACES_SIDEBAR_COLUMN_EJECT,
    PLACES_SIDEBAR_COLUMN_NO_EJECT,
    PLACES_SIDEBAR_COLUMN_BOOKMARK,
    PLACES_SIDEBAR_COLUMN_TOOLTIP,
    PLACES_SIDEBAR_COLUMN_EJECT_ICON,
    PLACES_SIDEBAR_COLUMN_SECTION_TYPE,
    PLACES_SIDEBAR_COLUMN_HEADING_TEXT,

    PLACES_SIDEBAR_COLUMN_COUNT
};

typedef enum
{
    PLACES_BUILT_IN,
    PLACES_MOUNTED_VOLUME,
    PLACES_BOOKMARK,
    PLACES_HEADING,
} PlaceType;

typedef enum {
    SECTION_COMPUTER,
    SECTION_DEVICES,
    SECTION_BOOKMARKS,
    SECTION_NETWORK,
    SECTION_PERSONAL,
    SECTION_FAVORITE,
} SectionType;

static void  peony_places_sidebar_iface_init        (PeonySidebarIface         *iface);
static void  sidebar_provider_iface_init               (PeonySidebarProviderIface *iface);
static GType peony_places_sidebar_provider_get_type (void);
static void  open_selected_bookmark                    (PeonyPlacesSidebar        *sidebar,
        GtkTreeModel                 *model,
        GtkTreePath                  *path,
        PeonyWindowOpenFlags flags);

static void  peony_places_sidebar_style_updated         (GtkWidget                    *widget);

static gboolean eject_or_unmount_bookmark              (PeonyPlacesSidebar *sidebar,
        GtkTreePath *path);
static gboolean eject_or_unmount_selection             (PeonyPlacesSidebar *sidebar);
static void  check_unmount_and_eject                   (GMount *mount,
        GVolume *volume,
        GDrive *drive,
        gboolean *show_unmount,
        gboolean *show_eject);

static void bookmarks_check_popup_sensitivity          (PeonyPlacesSidebar *sidebar);

/* Identifiers for target types */
enum
{
    GTK_TREE_MODEL_ROW,
    TEXT_URI_LIST
};

/* Target types for dragging from the shortcuts list */
static const GtkTargetEntry peony_shortcuts_source_targets[] =
{
    { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW }
};

/* Target types for dropping into the shortcuts list */
static const GtkTargetEntry peony_shortcuts_drop_targets [] =
{
    { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW },
    { "text/uri-list", 0, TEXT_URI_LIST }
};

/* Drag and drop interface declarations */
typedef struct
{
    GtkTreeModelFilter parent;

    PeonyPlacesSidebar *sidebar;
} PeonyShortcutsModelFilter;

typedef struct
{
    GtkTreeModelFilterClass parent_class;
} PeonyShortcutsModelFilterClass;

#define PEONY_SHORTCUTS_MODEL_FILTER_TYPE (_peony_shortcuts_model_filter_get_type ())
#define PEONY_SHORTCUTS_MODEL_FILTER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PEONY_SHORTCUTS_MODEL_FILTER_TYPE, PeonyShortcutsModelFilter))

GType _peony_shortcuts_model_filter_get_type (void);
static void peony_shortcuts_model_filter_drag_source_iface_init (GtkTreeDragSourceIface *iface);

G_DEFINE_TYPE_WITH_CODE (PeonyShortcutsModelFilter,
                         _peony_shortcuts_model_filter,
                         GTK_TYPE_TREE_MODEL_FILTER,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE,
                                 peony_shortcuts_model_filter_drag_source_iface_init));

static GtkTreeModel *peony_shortcuts_model_filter_new (PeonyPlacesSidebar *sidebar,
        GtkTreeModel          *child_model,
        GtkTreePath           *root);

G_DEFINE_TYPE_WITH_CODE (PeonyPlacesSidebar, peony_places_sidebar, GTK_TYPE_SCROLLED_WINDOW,
                         G_IMPLEMENT_INTERFACE (PEONY_TYPE_SIDEBAR,
                                 peony_places_sidebar_iface_init));

G_DEFINE_TYPE_WITH_CODE (PeonyPlacesSidebarProvider, peony_places_sidebar_provider, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (PEONY_TYPE_SIDEBAR_PROVIDER,
                                 sidebar_provider_iface_init));

static GdkPixbuf *
get_eject_icon (gboolean highlighted)
{
    GdkPixbuf *eject;
    PeonyIconInfo *eject_icon_info;
    int icon_size;

    icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);

    eject_icon_info = peony_icon_info_lookup_from_name ("media-eject", icon_size);
    eject = peony_icon_info_get_pixbuf_at_size (eject_icon_info, icon_size);

    if (highlighted) {
        GdkPixbuf *high;
        high = eel_create_spotlight_pixbuf (eject);
        g_object_unref (eject);
        eject = high;
    }

    g_object_unref (eject_icon_info);

    return eject;
}

static gboolean
is_built_in_bookmark (PeonyFile *file)
{
    gboolean built_in;
    gint idx;

    built_in = FALSE;

    for (idx = 0; idx < G_USER_N_DIRECTORIES; idx++) {
        /* PUBLIC_SHARE and TEMPLATES are not in our built-in list */
        if (peony_file_is_user_special_directory (file, idx)) {
            if (idx != G_USER_DIRECTORY_PUBLIC_SHARE &&  idx != G_USER_DIRECTORY_TEMPLATES) {
                built_in = TRUE;
            }

            break;
        }
    }

    return built_in;
}

static GtkTreeIter
add_heading (PeonyPlacesSidebar *sidebar,
         SectionType section_type,
         const gchar *title,
         const gchar *uri,
         GIcon *icon)
{
    GtkTreeIter iter, child_iter;
    GdkPixbuf      *pixbuf = NULL;
    PeonyIconInfo   *icon_info = NULL;
    int             icon_size;

    
    // check_heading_for_section (sidebar, section_type);
    
     icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
     icon_info = peony_icon_info_lookup (icon, icon_size);
    
     pixbuf = peony_icon_info_get_pixbuf_at_size (icon_info, icon_size);
     g_object_unref (icon_info);

    gtk_list_store_append (sidebar->store, &iter);
	if(NULL == uri || NULL == icon)
	{
		gtk_list_store_set (sidebar->store, &iter,
                PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_HEADING,
                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,    
                PLACES_SIDEBAR_COLUMN_HEADING_TEXT, title,
                PLACES_SIDEBAR_COLUMN_EJECT, FALSE,
                PLACES_SIDEBAR_COLUMN_NO_EJECT, TRUE,
                -1);
	}
	else
	{
	    gtk_list_store_set (sidebar->store, &iter,
	                PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_HEADING,
	                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,    
	                PLACES_SIDEBAR_COLUMN_URI,uri,
	                PLACES_SIDEBAR_COLUMN_ICON,pixbuf,
	                PLACES_SIDEBAR_COLUMN_HEADING_TEXT, title,
	                PLACES_SIDEBAR_COLUMN_EJECT, FALSE,
	                PLACES_SIDEBAR_COLUMN_NO_EJECT, TRUE,
	                -1);
	}
    
    if (pixbuf != NULL)
    {
        g_object_unref (pixbuf);
    }

    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));
    gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (sidebar->filter_model),
                              &child_iter,
                              &iter);

    return child_iter;
}

static void
check_heading_for_section (PeonyPlacesSidebar *sidebar,
               SectionType section_type)
{
    switch (section_type) {
    case SECTION_DEVICES:
       /* if (!sidebar->devices_header_added) {
            add_heading (sidebar, SECTION_DEVICES,
                     _("Devices"),NULL);
            sidebar->devices_header_added = TRUE;
        }*/

        break;
    case SECTION_BOOKMARKS:
       /* if (!sidebar->bookmarks_header_added) {
            add_heading (sidebar, SECTION_BOOKMARKS,
                     _("Bookmarks"),NULL);
            sidebar->bookmarks_header_added = TRUE;
        }*/

        break;
    default:
        break;
    }
}

static GtkTreeIter
insert_place_no_write_file (PeonyPlacesSidebar *sidebar,
           PlaceType place_type,
           SectionType section_type,
           const char *name,
           GIcon *icon,
           const char *uri,
           GDrive *drive,
           GVolume *volume,
           GMount *mount,
           const int index,
           const char *tooltip)
{
    GdkPixbuf      *pixbuf;
    GtkTreeIter     iter, child_iter;
    GdkPixbuf      *eject;
    PeonyIconInfo   *icon_info;
    int             icon_size;
    gboolean        show_eject;
    gboolean        show_unmount;
    gboolean        show_eject_button;

    check_heading_for_section (sidebar, section_type);

    icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
    icon_info = peony_icon_info_lookup (icon, icon_size);

    pixbuf = peony_icon_info_get_pixbuf_at_size (icon_info, icon_size);
    g_object_unref (icon_info);

    check_unmount_and_eject (mount, volume, drive,
                             &show_unmount, &show_eject);

    if (show_unmount || show_eject)
    {
        g_assert (place_type != PLACES_BOOKMARK);
    }

    if (mount == NULL)
    {
        show_eject_button = FALSE;
    }
    else
    {
        show_eject_button = (show_unmount || show_eject);
    }

    if (show_eject_button) {
        eject = get_eject_icon (FALSE);
    } else {
        eject = NULL;
    }

//    gtk_list_store_append (sidebar->store, &iter);
//    gtk_list_store_insert (sidebar->store, &iter, 5);


    char            *path1;
    GSettings       *settings1;
    settings1                       = g_settings_new("org.ukui.peony.preferences");
    int position=g_settings_get_int(settings1,"favorite-iter-position");
    gtk_list_store_insert (sidebar->store, &iter, position);

    gtk_list_store_set (sidebar->store, &iter,
                        PLACES_SIDEBAR_COLUMN_ICON, pixbuf,
                        PLACES_SIDEBAR_COLUMN_NAME, name,
                        PLACES_SIDEBAR_COLUMN_URI, uri,
                        PLACES_SIDEBAR_COLUMN_DRIVE, drive,
                        PLACES_SIDEBAR_COLUMN_VOLUME, volume,
                        PLACES_SIDEBAR_COLUMN_MOUNT, mount,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
                        PLACES_SIDEBAR_COLUMN_INDEX, index,
                        PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
                        PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
                        PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                        PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON, eject,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                        -1);

    if (pixbuf != NULL)
    {
        g_object_unref (pixbuf);
    }
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));
    gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (sidebar->filter_model),
            &child_iter,
            &iter);
    position = position +1;
    g_settings_set_int(settings1,"favorite-iter-position",position);

    return child_iter;
}
static GtkTreeIter
insert_place (PeonyPlacesSidebar *sidebar,
           PlaceType place_type,
           SectionType section_type,
           const char *name,
           GIcon *icon,
           const char *uri,
           GDrive *drive,
           GVolume *volume,
           GMount *mount,
           const int index,
           const char *tooltip)
{
    GdkPixbuf      *pixbuf;
    GtkTreeIter     iter, child_iter;
    GdkPixbuf      *eject;
    PeonyIconInfo   *icon_info;
    int             icon_size;
    gboolean        show_eject;
    gboolean        show_unmount;
    gboolean        show_eject_button;

    check_heading_for_section (sidebar, section_type);

    icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
    icon_info = peony_icon_info_lookup (icon, icon_size);

    pixbuf = peony_icon_info_get_pixbuf_at_size (icon_info, icon_size);
    g_object_unref (icon_info);

    check_unmount_and_eject (mount, volume, drive,
                             &show_unmount, &show_eject);

    if (show_unmount || show_eject)
    {
        g_assert (place_type != PLACES_BOOKMARK);
    }

    if (mount == NULL)
    {
        show_eject_button = FALSE;
    }
    else
    {
        show_eject_button = (show_unmount || show_eject);
    }

    if (show_eject_button) {
        eject = get_eject_icon (FALSE);
    } else {
        eject = NULL;
    }

//    gtk_list_store_append (sidebar->store, &iter);
//    gtk_list_store_insert (sidebar->store, &iter, 5);

    char            *path1;
    GSettings       *settings1;
    settings1                       = g_settings_new("org.ukui.peony.preferences");
    int position=g_settings_get_int(settings1,"favorite-iter-position");
    gtk_list_store_insert (sidebar->store, &iter, position);

    gtk_list_store_set (sidebar->store, &iter,
                        PLACES_SIDEBAR_COLUMN_ICON, pixbuf,
                        PLACES_SIDEBAR_COLUMN_NAME, name,
                        PLACES_SIDEBAR_COLUMN_URI, uri,
                        PLACES_SIDEBAR_COLUMN_DRIVE, drive,
                        PLACES_SIDEBAR_COLUMN_VOLUME, volume,
                        PLACES_SIDEBAR_COLUMN_MOUNT, mount,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
                        PLACES_SIDEBAR_COLUMN_INDEX, index,
                        PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
                        PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
                        PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                        PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON, eject,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                        -1);

    if (pixbuf != NULL)
    {
        g_object_unref (pixbuf);
    }
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));
    gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (sidebar->filter_model),
            &child_iter,
            &iter);
    position = position +1;
    g_settings_set_int(settings1,"favorite-iter-position",position);

    FILE *fp1;
    char favorite_files_path[100];
    sprintf(favorite_files_path,"%s/.config/peony/favorite-files",g_get_home_dir ());
    fp1= fopen (favorite_files_path, "a");
    fprintf(fp1,"%s\n", uri);
    fclose(fp1);

    return child_iter;
}
static GtkTreeIter
add_place (PeonyPlacesSidebar *sidebar,
           PlaceType place_type,
           SectionType section_type,
           const char *name,
           GIcon *icon,
           const char *uri,
           GDrive *drive,
           GVolume *volume,
           GMount *mount,
           const int index,
           const char *tooltip)
{
    GdkPixbuf      *pixbuf;
    GtkTreeIter     iter, child_iter;
    GdkPixbuf      *eject;
    PeonyIconInfo   *icon_info;
    int             icon_size;
    gboolean        show_eject;
    gboolean        show_unmount;
    gboolean        show_eject_button;

    check_heading_for_section (sidebar, section_type);

    icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
    icon_info = peony_icon_info_lookup (icon, icon_size);

    pixbuf = peony_icon_info_get_pixbuf_at_size (icon_info, icon_size);
    g_object_unref (icon_info);

    check_unmount_and_eject (mount, volume, drive,
                             &show_unmount, &show_eject);

    if (show_unmount || show_eject)
    {
        g_assert (place_type != PLACES_BOOKMARK);
    }

    if (mount == NULL)
    {
        show_eject_button = FALSE;
    }
    else
    {
        show_eject_button = (show_unmount || show_eject);
    }

    if (show_eject_button) {
        eject = get_eject_icon (FALSE);
    } else {
        eject = NULL;
    }

    gtk_list_store_append (sidebar->store, &iter);
    gtk_list_store_set (sidebar->store, &iter,
                        PLACES_SIDEBAR_COLUMN_ICON, pixbuf,
                        PLACES_SIDEBAR_COLUMN_NAME, name,
                        PLACES_SIDEBAR_COLUMN_URI, uri,
                        PLACES_SIDEBAR_COLUMN_DRIVE, drive,
                        PLACES_SIDEBAR_COLUMN_VOLUME, volume,
                        PLACES_SIDEBAR_COLUMN_MOUNT, mount,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
                        PLACES_SIDEBAR_COLUMN_INDEX, index,
                        PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
                        PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
                        PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                        PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON, eject,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                        -1);

    if (pixbuf != NULL)
    {
        g_object_unref (pixbuf);
    }
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));
    gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (sidebar->filter_model),
            &child_iter,
            &iter);
    return child_iter;
}

static void
compare_for_selection (PeonyPlacesSidebar *sidebar,
                       const gchar *location,
                       const gchar *added_uri,
                       const gchar *last_uri,
                       GtkTreeIter *iter,
                       GtkTreePath **path)
{
    int res;

    res = g_strcmp0 (added_uri, last_uri);

    if (res == 0)
    {
        /* last_uri always comes first */
        if (*path != NULL)
        {
            gtk_tree_path_free (*path);
        }
        *path = gtk_tree_model_get_path (sidebar->filter_model,
                                         iter);
    }
    else if (g_strcmp0 (location, added_uri) == 0)
    {
        if (*path == NULL)
        {
            *path = gtk_tree_model_get_path (sidebar->filter_model,
                                             iter);
        }
    }
}

static void
update_places (PeonyPlacesSidebar *sidebar)
{
    PeonyBookmark *bookmark;
    GtkTreeSelection *selection;
    GtkTreeIter last_iter;
    GtkTreePath *select_path;
    GtkTreeModel *model;
    GVolumeMonitor *volume_monitor;
    GList *mounts, *l, *ll;
    GMount *mount;
    GList *drives;
    GDrive *drive;
    GList *volumes;
    GVolume *volume;
    int bookmark_count, index;
    char *location, *mount_uri, *name, *desktop_path, *last_uri;
    const gchar *path;
    GIcon *icon;
    GFile *root;
    PeonyWindowSlotInfo *slot;
    char *tooltip;
    GList *network_mounts;
    GList *xdg_dirs;
    PeonyFile *file;

    model = NULL;
    last_uri = NULL;
    select_path = NULL;

    selection = gtk_tree_view_get_selection (sidebar->tree_view);
    if (gtk_tree_selection_get_selected (selection, &model, &last_iter))
    {
        gtk_tree_model_get (model,
                            &last_iter,
                            PLACES_SIDEBAR_COLUMN_URI, &last_uri, -1);
    }
    gtk_list_store_clear (sidebar->store);

    sidebar->devices_header_added = FALSE;
    sidebar->bookmarks_header_added = FALSE;

    slot = peony_window_info_get_active_slot (sidebar->window);
    location = peony_window_slot_info_get_current_location (slot);

    volume_monitor = sidebar->volume_monitor;

    last_iter = add_heading (sidebar, SECTION_FAVORITE,
                             NULL,NULL,NULL);

    /* FAVORITE */
    //icon = g_themed_icon_new (PEONY_ICON_FAVORITE);
    last_iter = add_heading (sidebar, SECTION_FAVORITE,
                             _("Favorite"),NULL,NULL);//"favorite:///",icon);
    //g_object_unref (icon);

    desktop_path = peony_get_desktop_directory ();

    /* desktop */
    mount_uri = g_filename_to_uri (desktop_path, NULL, NULL);
    icon = g_themed_icon_new (PEONY_ICON_DESKTOP);
    last_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_FAVORITE,
                           _("Desktop"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           _("Open the contents of your desktop in a folder"));
    g_object_unref (icon);
    compare_for_selection (sidebar,
                           location, mount_uri, last_uri,
                           &last_iter, &select_path);
    g_free (mount_uri);
    g_free (desktop_path);

    /*trash:*/
    mount_uri = "trash:///"; /* No need to strdup */
    icon = peony_trash_monitor_get_icon ();
    last_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_FAVORITE,
                           _("Trash"), icon, mount_uri,
                           NULL, NULL, NULL, 0,
                           _("Open the trash"));
    compare_for_selection (sidebar,
                           location, mount_uri, last_uri,
                           &last_iter, &select_path);
    g_object_unref (icon);

    /*recent:*/
    mount_uri = "recent:///";/*No need to strdup*/
    icon = g_themed_icon_new_with_default_fallbacks("folder-recent");
    last_iter = add_place (sidebar,PLACES_BUILT_IN,
                           SECTION_FAVORITE,
                           _("Recent"),icon,mount_uri,
                           NULL,NULL,NULL,0,
                           _("Open the recent"));
    compare_for_selection (sidebar,
                           location, mount_uri, last_uri,
                           &last_iter, &select_path);
    g_object_unref (icon);


    char            *path1;
    GSettings       *settings1;
    settings1                       = g_settings_new("org.ukui.peony.preferences");    

    char line[1000];
    char favorite_files_path[100];
    sprintf(favorite_files_path,"%s/.config/peony/favorite-files",g_get_home_dir ());
    FILE *fp;
    fp=fopen(favorite_files_path,"r");
    if(fp==NULL)
    {
	g_settings_set_int(settings1,"favorite-iter-position",5);
    }else {
	g_settings_set_int(settings1,"favorite-iter-position",5);
	char buf[1000];
        char c;
        c = fgetc(fp);
        while(!feof(fp))
        {
            fgets(line,1000,fp);
	    sprintf(buf,"%c%s",c,line);
	    buf[strlen(buf)-1]=0;
    	    mount_uri = buf;
    	    icon = g_themed_icon_new (PEONY_ICON_FOLDER);
	    GFile *file1;
	    char *filename;
	    file1=g_file_new_for_uri (mount_uri);
	    filename=g_file_get_basename (file1);
//	    last_iter = insert_place (sidebar, PLACES_BUILT_IN,
	    last_iter = insert_place_no_write_file (sidebar, PLACES_BUILT_IN,
                           			SECTION_FAVORITE,
                           			filename, icon, buf,
                           			NULL, NULL, NULL, 0,
                           			_("Open the folder"));
    	    compare_for_selection (sidebar,
                          	   location, mount_uri, last_uri,
                           	   &last_iter, &select_path);
    	    g_object_unref (icon);
            c = fgetc(fp);
        }
        fclose(fp);
    }


    last_iter = add_heading (sidebar, SECTION_PERSONAL,
                             NULL,NULL,NULL);

   /*personal*/
    icon = g_themed_icon_new (PEONY_ICON_HOME);
    mount_uri = peony_get_home_directory_uri ();
    last_iter = add_heading(sidebar,SECTION_PERSONAL,_("Personal"),mount_uri,icon);
    g_object_unref(icon);
	g_free (mount_uri);

    /* home folder */
    /*if (strcmp (g_get_home_dir(), desktop_path) != 0) {
        char *display_name;

        mount_uri = peony_get_home_directory_uri ();
        display_name = g_filename_display_basename (g_get_home_dir ());
        icon = g_themed_icon_new (PEONY_ICON_HOME);
        last_iter = add_place (sidebar, PLACES_BUILT_IN,
                               SECTION_PERSONAL,
                               display_name, icon,
                               mount_uri, NULL, NULL, NULL, 0,
                               _("Open your personal folder"));
        g_object_unref (icon);
        g_free (display_name);
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
        g_free (mount_uri);
    }*/

    /* XDG directories */
    xdg_dirs = NULL;
    for (index = 0; index < G_USER_N_DIRECTORIES; index++) {

        if (index == G_USER_DIRECTORY_DESKTOP ||
            index == G_USER_DIRECTORY_TEMPLATES ||
            index == G_USER_DIRECTORY_PUBLIC_SHARE) {
            continue;
        }

        path = g_get_user_special_dir (index);

        /* xdg resets special dirs to the home directory in case
         * it's not finiding what it expects. We don't want the home
         * to be added multiple times in that weird configuration.
         */
        if (path == NULL
            || g_strcmp0 (path, g_get_home_dir ()) == 0
            || g_list_find_custom (xdg_dirs, path, (GCompareFunc) g_strcmp0) != NULL) {
            continue;
        }

        root = g_file_new_for_path (path);
        name = g_file_get_basename (root);
        icon = peony_user_special_directory_get_gicon (index);
        mount_uri = g_file_get_uri (root);
        tooltip = g_file_get_parse_name (root);

        last_iter = add_place (sidebar, PLACES_BUILT_IN,
                               SECTION_PERSONAL,
                               name, icon, mount_uri,
                               NULL, NULL, NULL, 0,
                               tooltip);
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
        g_free (name);
        g_object_unref (root);
        g_object_unref (icon);
        g_free (mount_uri);
        g_free (tooltip);

        xdg_dirs = g_list_prepend (xdg_dirs, (char *)path);
    }
    g_list_free (xdg_dirs);

    last_iter = add_heading (sidebar, SECTION_FAVORITE,
                             NULL,NULL,NULL);

    /*Computer*/
    icon = g_themed_icon_new ("uk-computer");
    last_iter = add_heading(sidebar,SECTION_COMPUTER,_("My Computer"),"computer:///",icon);
    g_object_unref (icon);

    /* file system root */
    mount_uri = "file:///"; /* No need to strdup */
    icon = g_themed_icon_new (PEONY_ICON_FILESYSTEM);
    last_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_COMPUTER,
                           _("File System"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           _("Open the contents of the File System"));
    g_object_unref (icon);
    compare_for_selection (sidebar,
                           location, mount_uri, last_uri,
                           &last_iter, &select_path);

    /* first go through all connected drives */
    drives = g_volume_monitor_get_connected_drives (volume_monitor);

    for (l = drives; l != NULL; l = l->next)
    {
        drive = l->data;

        volumes = g_drive_get_volumes (drive);
        if (volumes != NULL)
        {
            for (ll = volumes; ll != NULL; ll = ll->next)
            {
                volume = ll->data;
                mount = g_volume_get_mount (volume);
                if (mount != NULL)
                {
                    /* Show mounted volume in the sidebar */
                    icon = g_mount_get_icon (mount);
                    root = g_mount_get_default_location (mount);
                    mount_uri = g_file_get_uri (root);
                    name = g_mount_get_name (mount);
                    tooltip = g_file_get_parse_name (root);

                    last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                                           SECTION_COMPUTER,
                                           name, icon, mount_uri,
                                           drive, volume, mount, 0, tooltip);
                    compare_for_selection (sidebar,
                                           location, mount_uri, last_uri,
                                           &last_iter, &select_path);
                    g_object_unref (root);
                    g_object_unref (mount);
                    g_object_unref (icon);
                    g_free (tooltip);
                    g_free (name);
                    g_free (mount_uri);
                }
                else
                {
                    /* Do show the unmounted volumes in the sidebar;
                     * this is so the user can mount it (in case automounting
                     * is off).
                     *
                     * Also, even if automounting is enabled, this gives a visual
                     * cue that the user should remember to yank out the media if
                     * he just unmounted it.
                     */
                    icon = g_volume_get_icon (volume);
                    name = g_volume_get_name (volume);
                    tooltip = g_strdup_printf (_("Mount and open %s"), name);

                    last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                                           SECTION_COMPUTER,
                                           name, icon, NULL,
                                           drive, volume, NULL, 0, tooltip);
                    g_object_unref (icon);
                    g_free (name);
                    g_free (tooltip);
                }
                g_object_unref (volume);
            }
            g_list_free (volumes);
        }
        else
        {
            if (g_drive_is_media_removable (drive) && !g_drive_is_media_check_automatic (drive))
            {
                /* If the drive has no mountable volumes and we cannot detect media change.. we
                 * display the drive in the sidebar so the user can manually poll the drive by
                 * right clicking and selecting "Rescan..."
                 *
                 * This is mainly for drives like floppies where media detection doesn't
                 * work.. but it's also for human beings who like to turn off media detection
                 * in the OS to save battery juice.
                 */
                icon = g_drive_get_icon (drive);
                name = g_drive_get_name (drive);
                tooltip = g_strdup_printf (_("Mount and open %s"), name);

                last_iter = add_place (sidebar, PLACES_BUILT_IN,
                                       SECTION_COMPUTER,
                                       name, icon, NULL,
                                       drive, NULL, NULL, 0, tooltip);
                g_object_unref (icon);
                g_free (tooltip);
                g_free (name);
            }
        }
        g_object_unref (drive);
    }
    g_list_free (drives);

    /* add all volumes that is not associated with a drive */
    volumes = g_volume_monitor_get_volumes (volume_monitor);
    for (l = volumes; l != NULL; l = l->next)
    {
        volume = l->data;
        drive = g_volume_get_drive (volume);
        if (drive != NULL)
        {
            g_object_unref (volume);
            g_object_unref (drive);
            continue;
        }
        mount = g_volume_get_mount (volume);
        if (mount != NULL)
        {
            icon = g_mount_get_icon (mount);
            root = g_mount_get_default_location (mount);
            mount_uri = g_file_get_uri (root);
            tooltip = g_file_get_parse_name (root);
            g_object_unref (root);
            name = g_mount_get_name (mount);
            last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                                   SECTION_COMPUTER,
                                   name, icon, mount_uri,
                                   NULL, volume, mount, 0, tooltip);
            compare_for_selection (sidebar,
                                   location, mount_uri, last_uri,
                                   &last_iter, &select_path);
            g_object_unref (mount);
            g_object_unref (icon);
            g_free (name);
            g_free (tooltip);
            g_free (mount_uri);
        }
        else
        {
            /* see comment above in why we add an icon for an unmounted mountable volume */
            icon = g_volume_get_icon (volume);
            name = g_volume_get_name (volume);
            last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                                   SECTION_COMPUTER,
                                   name, icon, NULL,
                                   NULL, volume, NULL, 0, name);
            g_object_unref (icon);
            g_free (name);
        }
        g_object_unref (volume);
    }
    g_list_free (volumes);

    /* add mounts that has no volume (/etc/mtab mounts, ftp, sftp,...) */
    network_mounts = NULL;
    mounts = g_volume_monitor_get_mounts (volume_monitor);

    for (l = mounts; l != NULL; l = l->next)
    {
        mount = l->data;
        if (g_mount_is_shadowed (mount))
        {
            g_object_unref (mount);
            continue;
        }
        volume = g_mount_get_volume (mount);
        if (volume != NULL)
        {
            g_object_unref (volume);
            g_object_unref (mount);
            continue;
        }
        root = g_mount_get_default_location (mount);

        if (!g_file_is_native (root)) {
            network_mounts = g_list_prepend (network_mounts, g_object_ref (mount));
            continue;
        }

        icon = g_mount_get_icon (mount);
        mount_uri = g_file_get_uri (root);
        name = g_mount_get_name (mount);
        tooltip = g_file_get_parse_name (root);
        last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                               SECTION_COMPUTER,
                               name, icon, mount_uri,
                               NULL, NULL, mount, 0, tooltip);
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
        g_object_unref (root);
        g_object_unref (mount);
        g_object_unref (icon);
        g_free (name);
        g_free (mount_uri);
        g_free (tooltip);
    }
    g_list_free (mounts);
    /* network */
    //last_iter = add_heading (sidebar, SECTION_NETWORK,
    //                         _("Network"));

    network_mounts = g_list_reverse (network_mounts);
    for (l = network_mounts; l != NULL; l = l->next) {
        mount = l->data;
        root = g_mount_get_default_location (mount);
        icon = g_mount_get_icon (mount);
        mount_uri = g_file_get_uri (root);
        name = g_mount_get_name (mount);
        tooltip = g_file_get_parse_name (root);
        last_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                               SECTION_NETWORK,
                               name, icon, mount_uri,
                               NULL, NULL, mount, 0, tooltip);
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
        g_object_unref (root);
        g_object_unref (mount);
        g_object_unref (icon);
        g_free (name);
        g_free (mount_uri);
        g_free (tooltip);
    }

    g_list_free_full (network_mounts, g_object_unref);

    /* network:// */
	#if 0
    mount_uri = "network:///"; /* No need to strdup */
    icon = g_themed_icon_new (PEONY_ICON_NETWORK);
    last_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_NETWORK,
                           _("Browse Network"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           _("Browse the contents of the network"));
    g_object_unref (icon);
    compare_for_selection (sidebar,
                           location, mount_uri, last_uri,
                           &last_iter, &select_path);
    #endif
    /*/new layout*/

    /* add bookmarks */
    bookmark_count = peony_bookmark_list_length (sidebar->bookmarks);

    for (index = 0; index < bookmark_count; ++index) {
        bookmark = peony_bookmark_list_item_at (sidebar->bookmarks, index);

        if (peony_bookmark_uri_known_not_to_exist (bookmark)) {
            continue;
        }

        root = peony_bookmark_get_location (bookmark);
        file = peony_file_get (root);

        if (is_built_in_bookmark (file)) {
            g_object_unref (root);
            peony_file_unref (file);
            continue;
        }

        name = peony_bookmark_get_name (bookmark);
        icon = peony_bookmark_get_icon (bookmark);
        mount_uri = peony_bookmark_get_uri (bookmark);
        tooltip = g_file_get_parse_name (root);

        last_iter = add_place (sidebar, PLACES_BOOKMARK,
                               SECTION_BOOKMARKS,
                               name, icon, mount_uri,
                               NULL, NULL, NULL, index,
                               tooltip);
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
        g_free (name);
        g_object_unref (root);
        g_object_unref (icon);
        g_free (mount_uri);
        g_free (tooltip);
    }

    g_free (location);

    if (select_path != NULL) {
        gtk_tree_selection_select_path (selection, select_path);
    }

    if (select_path != NULL) {
        gtk_tree_path_free (select_path);
    }

    g_free (last_uri);
}

static void
mount_added_callback (GVolumeMonitor *volume_monitor,
                      GMount *mount,
                      PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
mount_removed_callback (GVolumeMonitor *volume_monitor,
                        GMount *mount,
                        PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
mount_changed_callback (GVolumeMonitor *volume_monitor,
                        GMount *mount,
                        PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
volume_added_callback (GVolumeMonitor *volume_monitor,
                       GVolume *volume,
                       PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
volume_removed_callback (GVolumeMonitor *volume_monitor,
                         GVolume *volume,
                         PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
volume_changed_callback (GVolumeMonitor *volume_monitor,
                         GVolume *volume,
                         PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
drive_disconnected_callback (GVolumeMonitor *volume_monitor,
                             GDrive         *drive,
                             PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
drive_connected_callback (GVolumeMonitor *volume_monitor,
                          GDrive         *drive,
                          PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static void
drive_changed_callback (GVolumeMonitor *volume_monitor,
                        GDrive         *drive,
                        PeonyPlacesSidebar *sidebar)
{
    update_places (sidebar);
}

static gboolean
over_eject_button (PeonyPlacesSidebar *sidebar,
                   gint x,
                   gint y,
                   GtkTreePath **path)
{
    GtkTreeViewColumn *column;
    GtkTextDirection direction;
    int width, total_width;
    int eject_button_size;
    gboolean show_eject;
    GtkTreeIter iter;
    GtkTreeModel *model;

    *path = NULL;
    model = gtk_tree_view_get_model (sidebar->tree_view);

   if (gtk_tree_view_get_path_at_pos (sidebar->tree_view,
                                      x, y,
                                      path, &column, NULL, NULL)) {

        gtk_tree_model_get_iter (model, &iter, *path);
        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_EJECT, &show_eject,
                            -1);

        if (!show_eject) {
            goto out;
        }

        total_width = 0;

        gtk_widget_style_get (GTK_WIDGET (sidebar->tree_view),
                              "horizontal-separator", &width,
                              NULL);
        total_width += width;

        direction = gtk_widget_get_direction (GTK_WIDGET (sidebar->tree_view));
        if (direction != GTK_TEXT_DIR_RTL) {
            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->padding_cell_renderer,
                                                    NULL, &width);
            total_width += width;

            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->icon_padding_cell_renderer,
                                                    NULL, &width);
            total_width += width;
            
            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->icon_cell_renderer,
                                                    NULL, &width);
            total_width += width;

            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->eject_text_cell_renderer,
                                                    NULL, &width);
            total_width += width;
        }

        total_width += EJECT_BUTTON_XPAD;

        eject_button_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);

        if (x - total_width >= 0 &&
            /* fix unwanted unmount requests if clicking on the label */
            x >= total_width - eject_button_size &&
            x >= 80 &&
            x - total_width <= eject_button_size) {
            return TRUE;
        }
    }

out:
    if (*path != NULL) {
        gtk_tree_path_free (*path);
        *path = NULL;
    }

    return FALSE;
}

static gboolean
clicked_eject_button (PeonyPlacesSidebar *sidebar,
                      GtkTreePath **path)
{
    GdkEvent *event = gtk_get_current_event ();
    GdkEventButton *button_event = (GdkEventButton *) event;

    if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
         over_eject_button (sidebar, button_event->x, button_event->y, path)) {
        return TRUE;
    }

    return FALSE;
}

static void
desktop_location_changed_callback (gpointer user_data)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = PEONY_PLACES_SIDEBAR (user_data);

    update_places (sidebar);
}

static void
loading_uri_callback (PeonyWindowInfo *window,
                      char *location,
                      PeonyPlacesSidebar *sidebar)
{
    GtkTreeSelection *selection;
    GtkTreeIter       iter;
    gboolean          valid;
    char             *uri;

    if (strcmp (sidebar->uri, location) != 0)
    {
        g_free (sidebar->uri);
        sidebar->uri = g_strdup (location);

        /* set selection if any place matches location */
        selection = gtk_tree_view_get_selection (sidebar->tree_view);
        gtk_tree_selection_unselect_all (selection);
        valid = gtk_tree_model_get_iter_first (sidebar->filter_model, &iter);

        while (valid)
        {
            gtk_tree_model_get (sidebar->filter_model, &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &uri,
                                -1);
            if (uri != NULL)
            {
                if (strcmp (uri, location) == 0)
                {
                    g_free (uri);
                    gtk_tree_selection_select_iter (selection, &iter);
                    break;
                }
                g_free (uri);
            }
            valid = gtk_tree_model_iter_next (sidebar->filter_model, &iter);
        }
    }
}

/* Computes the appropriate row and position for dropping */
static gboolean
compute_drop_position (GtkTreeView *tree_view,
                       int                      x,
                       int                      y,
                       GtkTreePath            **path,
                       GtkTreeViewDropPosition *pos,
                       PeonyPlacesSidebar *sidebar)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    PlaceType place_type;
    SectionType section_type;

    if (!gtk_tree_view_get_dest_row_at_pos (tree_view,
                                            x,
                                            y,
                                            path,
                                            pos)) {
        return FALSE;
    }

    model = gtk_tree_view_get_model (tree_view);

    gtk_tree_model_get_iter (model, &iter, *path);
    gtk_tree_model_get (model, &iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                        -1);

    if (place_type == PLACES_HEADING &&
        section_type != SECTION_BOOKMARKS &&
        section_type != SECTION_NETWORK) {
        /* never drop on headings, but the bookmarks or network heading
         * is a special case, so we can create new bookmarks by dragging
         * at the beginning or end of the bookmark list.
         */
        gtk_tree_path_free (*path);
        *path = NULL;

        return FALSE;
    }

    if (section_type != SECTION_BOOKMARKS &&
        sidebar->drag_data_received &&
        sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
        /* don't allow dropping bookmarks into non-bookmark areas */
        gtk_tree_path_free (*path);
        *path = NULL;

        return FALSE;
    }

    /* drag to top or bottom of bookmark list to add a bookmark */
    if (place_type == PLACES_HEADING && section_type == SECTION_BOOKMARKS) {
        *pos = GTK_TREE_VIEW_DROP_AFTER;
    } else if (place_type == PLACES_HEADING && section_type == SECTION_NETWORK) {
        *pos = GTK_TREE_VIEW_DROP_BEFORE;
    } else {
        /* or else you want to drag items INTO the existing bookmarks */
        *pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
    }

    if (*pos != GTK_TREE_VIEW_DROP_BEFORE &&
        sidebar->drag_data_received &&
        sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
        /* bookmark rows are never dragged into other bookmark rows */
        *pos = GTK_TREE_VIEW_DROP_AFTER;
    }

    return TRUE;
}

static gboolean
get_drag_data (GtkTreeView *tree_view,
               GdkDragContext *context,
               unsigned int time)
{
    GdkAtom target;

    target = gtk_drag_dest_find_target (GTK_WIDGET (tree_view),
                                        context,
                                        NULL);

    if (target == GDK_NONE)
    {
        return FALSE;
    }

    gtk_drag_get_data (GTK_WIDGET (tree_view),
                       context, target, time);

    return TRUE;
}

static void
free_drag_data (PeonyPlacesSidebar *sidebar)
{
    sidebar->drag_data_received = FALSE;

    if (sidebar->drag_list)
    {
        peony_drag_destroy_selection_list (sidebar->drag_list);
        sidebar->drag_list = NULL;
    }
}

static gboolean
can_accept_file_as_bookmark (PeonyFile *file)
{
    return (peony_file_is_directory (file) &&
            !is_built_in_bookmark (file));
}

static gboolean
can_accept_items_as_bookmarks (const GList *items)
{
    int max;
    char *uri;
    PeonyFile *file;

    /* Iterate through selection checking if item will get accepted as a bookmark.
     * If more than 100 items selected, return an over-optimistic result.
     */
    for (max = 100; items != NULL && max >= 0; items = items->next, max--)
    {
        uri = ((PeonyDragSelectionItem *)items->data)->uri;
        file = peony_file_get_by_uri (uri);
        if (!can_accept_file_as_bookmark (file))
        {
            peony_file_unref (file);
            return FALSE;
        }
        peony_file_unref (file);
    }

    return TRUE;
}

static gboolean
drag_motion_callback (GtkTreeView *tree_view,
                      GdkDragContext *context,
                      int x,
                      int y,
                      unsigned int time,
                      PeonyPlacesSidebar *sidebar)
{
    //printf ("drag_motion_callback\n");
    GtkTreePath *path;
    GtkTreeViewDropPosition pos;
    int action = 0;
    GtkTreeIter iter;
    char *uri;
    gboolean res;

    if (!sidebar->drag_data_received)
    {
        if (!get_drag_data (tree_view, context, time))
        {
            return FALSE;
        }
    }

    path = NULL;
    res = compute_drop_position (tree_view, x, y, &path, &pos, sidebar);

    if (!res) {
        action = GDK_ACTION_COPY;
        goto out;
    }

    if (pos == GTK_TREE_VIEW_DROP_BEFORE ||
        pos == GTK_TREE_VIEW_DROP_AFTER )
    {
        if (sidebar->drag_data_received &&
            sidebar->drag_data_info == GTK_TREE_MODEL_ROW)
        {
            action = GDK_ACTION_MOVE;
        }
        else if (can_accept_items_as_bookmarks (sidebar->drag_list))
        {
            action = GDK_ACTION_COPY;
        }
        else
        {
            action = 0;
        }
    }
    else
    {
        if (sidebar->drag_list == NULL)
        {
            action = 0;
        }
        else
        {
            gtk_tree_model_get_iter (sidebar->filter_model,
                                     &iter, path);
            gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model),
                                &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &uri,
                                -1);
            peony_drag_default_drop_action_for_icons (context, uri,
                    sidebar->drag_list,
                    &action);
            g_free (uri);
        }
    }

    if (action != 0) {
        gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
    }

    if (path != NULL) {
        gtk_tree_path_free (path);
    }

 out:
    g_signal_stop_emission_by_name (tree_view, "drag-motion");

    if (action != 0)
    {
        gdk_drag_status (context, action, time);
    }
    else
    {
        gdk_drag_status (context, 0, time);
    }

    return TRUE;
}

static void
drag_leave_callback (GtkTreeView *tree_view,
                     GdkDragContext *context,
                     unsigned int time,
                     PeonyPlacesSidebar *sidebar)
{
    printf ("drag leave callback\n");
    sidebar->drag_data_received = FALSE;
    gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_BEFORE);
    g_signal_stop_emission_by_name (tree_view, "drag-leave");
}

/* Parses a "text/uri-list" string and inserts its URIs as bookmarks */
static void
bookmarks_drop_uris (PeonyPlacesSidebar *sidebar,
                     GtkSelectionData      *selection_data,
                     int                    position)
{
    PeonyBookmark *bookmark;
    PeonyFile *file;
    char *uri, *name;
    char **uris;
    int i;
    GFile *location;
    GIcon *icon;

    uris = gtk_selection_data_get_uris (selection_data);
    if (!uris)
        return;

    for (i = 0; uris[i]; i++)
    {
        uri = uris[i];
        file = peony_file_get_by_uri (uri);

        if (!can_accept_file_as_bookmark (file))
        {
            peony_file_unref (file);
            continue;
        }

        uri = peony_file_get_drop_target_uri (file);
        location = g_file_new_for_uri (uri);
        peony_file_unref (file);

        name = peony_compute_title_for_location (location);
        icon = g_themed_icon_new (PEONY_ICON_FOLDER);
        bookmark = peony_bookmark_new (location, name, TRUE, icon);

        if (!peony_bookmark_list_contains (sidebar->bookmarks, bookmark))
        {
            peony_bookmark_list_insert_item (sidebar->bookmarks, bookmark, position++);
        }

        g_object_unref (location);
        g_object_unref (bookmark);
        g_object_unref (icon);
        g_free (name);
        g_free (uri);
    }

    g_strfreev (uris);
}

static GList *
uri_list_from_selection (GList *selection)
{
    PeonyDragSelectionItem *item;
    GList *ret;
    GList *l;

    ret = NULL;
    for (l = selection; l != NULL; l = l->next)
    {
        item = l->data;
        ret = g_list_prepend (ret, item->uri);
    }

    return g_list_reverse (ret);
}

static GList*
build_selection_list (const char *data)
{
    PeonyDragSelectionItem *item;
    GList *result;
    char **uris;
    char *uri;
    int i;

    uris = g_uri_list_extract_uris (data);

    result = NULL;
    for (i = 0; uris[i]; i++)
    {
        uri = uris[i];
        item = peony_drag_selection_item_new ();
        item->uri = g_strdup (uri);
        item->got_icon_position = FALSE;
        result = g_list_prepend (result, item);
    }

    g_strfreev (uris);

    return g_list_reverse (result);
}

static gboolean
get_selected_iter (PeonyPlacesSidebar *sidebar,
                   GtkTreeIter *iter)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection (sidebar->tree_view);

    return gtk_tree_selection_get_selected (selection, NULL, iter);
}

/* Reorders the selected bookmark to the specified position */
static void
reorder_bookmarks (PeonyPlacesSidebar *sidebar,
                   int                new_position)
{
    GtkTreeIter iter;
    PlaceType type;
    int old_position;

    /* Get the selected path */

    if (!get_selected_iter (sidebar, &iter))
        return;

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        PLACES_SIDEBAR_COLUMN_INDEX, &old_position,
                        -1);

    if (type != PLACES_BOOKMARK ||
            old_position < 0 ||
            old_position >= peony_bookmark_list_length (sidebar->bookmarks))
    {
        return;
    }

    peony_bookmark_list_move_item (sidebar->bookmarks, old_position,
                                  new_position);
}


static void
drag_data_received_callback (GtkWidget *widget,
                             GdkDragContext *context,
                             int x,
                             int y,
                             GtkSelectionData *selection_data,
                             unsigned int info,
                             unsigned int time,
                             PeonyPlacesSidebar *sidebar)
{
    printf ("drag_data_received_callback\n");
    GtkTreeView *tree_view;
    GtkTreePath *tree_path;
    GtkTreeViewDropPosition tree_pos;
    GtkTreeIter iter;
    int position;
    GtkTreeModel *model;
    char *drop_uri;
    GList *selection_list, *uris;
    PlaceType place_type;
    SectionType section_type;
    gboolean success;

    tree_view = GTK_TREE_VIEW (widget);

    if (!sidebar->drag_data_received)
    {
        if (gtk_selection_data_get_target (selection_data) != GDK_NONE &&
                info == TEXT_URI_LIST)
        {
            sidebar->drag_list = build_selection_list (gtk_selection_data_get_data (selection_data));
        }
        else
        {
            sidebar->drag_list = NULL;
        }
        sidebar->drag_data_received = TRUE;
        sidebar->drag_data_info = info;
    }

    g_signal_stop_emission_by_name (widget, "drag-data-received");

    if (!sidebar->drop_occured)
    {
        return;
    }

    GList *uris1 = uri_list_from_selection (sidebar->drag_list);
    if (uris1) {
        printf ("%s\n", uris1->data);
    }

    /* Compute position */
    success = compute_drop_position (tree_view, x, y, &tree_path, &tree_pos, sidebar);
    if (!success) {
        printf ("!success\n");
        goto out;
    }
        

    success = FALSE;

    if (tree_pos == GTK_TREE_VIEW_DROP_BEFORE ||
        tree_pos == GTK_TREE_VIEW_DROP_AFTER)
    {
        model = gtk_tree_view_get_model (tree_view);

        if (!gtk_tree_model_get_iter (model, &iter, tree_path))
        {
            goto out;
        }

        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                            PLACES_SIDEBAR_COLUMN_INDEX, &position,
                            -1);

        if (section_type != SECTION_BOOKMARKS &&
            !(section_type == SECTION_NETWORK && place_type == PLACES_HEADING)) {
            goto out;
        }

        if (section_type == SECTION_NETWORK && place_type == PLACES_HEADING &&
            tree_pos == GTK_TREE_VIEW_DROP_BEFORE) {
            position = peony_bookmark_list_length (sidebar->bookmarks);
        }

        if (tree_pos == GTK_TREE_VIEW_DROP_AFTER && place_type != PLACES_HEADING) {
            /* heading already has position 0 */
            position++;
        }

        switch (info)
        {
        case TEXT_URI_LIST:
            bookmarks_drop_uris (sidebar, selection_data, position);
            success = TRUE;
            break;
        case GTK_TREE_MODEL_ROW:
            reorder_bookmarks (sidebar, position);
            success = TRUE;
            break;
        default:
            g_assert_not_reached ();
            break;
        }
    }
    else
    {
        GdkDragAction real_action;

        /* file transfer requested */
        real_action = gdk_drag_context_get_selected_action (context);

        if (real_action == GDK_ACTION_ASK)
        {
            real_action =
                peony_drag_drop_action_ask (GTK_WIDGET (tree_view),
                                           gdk_drag_context_get_actions (context));
        }

        if (real_action > 0)
        {
            model = gtk_tree_view_get_model (tree_view);

            gtk_tree_model_get_iter (model, &iter, tree_path);
            gtk_tree_model_get (model, &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &drop_uri,
                                -1);

            switch (info)
            {
            case TEXT_URI_LIST:
                selection_list = build_selection_list (gtk_selection_data_get_data (selection_data));
                uris = uri_list_from_selection (selection_list);
                peony_file_operations_copy_move (uris, NULL, drop_uri,
                                                real_action, GTK_WIDGET (tree_view),FALSE,
                                                NULL, NULL);
                peony_drag_destroy_selection_list (selection_list);
                g_list_free (uris);
                success = TRUE;
                break;
            case GTK_TREE_MODEL_ROW:
                success = FALSE;
                break;
            default:
                g_assert_not_reached ();
                break;
            }

            g_free (drop_uri);
        }
    }

out:
    sidebar->drop_occured = FALSE;
    free_drag_data (sidebar);
    gtk_drag_finish (context, success, FALSE, time);

    gtk_tree_path_free (tree_path);
}

static void
drag_data_received_callback0 (GtkWidget *widget,
                             GdkDragContext *context,
                             int x,
                             int y,
                             GtkSelectionData *selection_data,
                             unsigned int info,
                             unsigned int time,
                             PeonyPlacesSidebar *sidebar)
{
    GList *selection_list, *uris,*l;
    char *path;
    selection_list = build_selection_list (gtk_selection_data_get_data (selection_data));
    uris = uri_list_from_selection (selection_list);

//#if 0
    GIcon *icon;
    GtkTreeIter last_iter;
    char *location;
    char *last_uri;
    GtkTreePath *select_path;   	
  
    char *mount_uri;
    GFile *file1;
    char *filename;
    for (l = uris; l != NULL; l = l->next)
    {
        mount_uri = uris->data;
        file1=g_file_new_for_uri (mount_uri);
        filename=g_file_get_basename (file1);
        icon = g_themed_icon_new (PEONY_ICON_FOLDER);
        last_iter = insert_place (sidebar, PLACES_BUILT_IN,
                                  SECTION_FAVORITE,
                                  filename, icon, mount_uri,
                                  NULL, NULL, NULL, 0,
                                  _("Open the folder"));
        compare_for_selection (sidebar,
                               location, mount_uri, last_uri,
                               &last_iter, &select_path);
/*    
    PlaceType place_type=PLACES_BUILT_IN;
    SectionType section_type=SECTION_FAVORITE;

    GdkPixbuf      *pixbuf;
    GtkTreeIter     iter, child_iter;
    GdkPixbuf      *eject;
    PeonyIconInfo   *icon_info;
    int             icon_size;
    gboolean        show_eject;
    gboolean        show_unmount;
    gboolean        show_eject_button;

    
    check_heading_for_section (sidebar, SECTION_FAVORITE);

    icon_size = peony_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
    icon_info = peony_icon_info_lookup (icon, icon_size);

    pixbuf = peony_icon_info_get_pixbuf_at_size (icon_info, icon_size);
    g_object_unref (icon_info);


//    gtk_list_store_append (sidebar->store, &iter);
    gtk_list_store_insert (sidebar->store, &iter, 5);
    gtk_list_store_set (sidebar->store, &iter,
                        PLACES_SIDEBAR_COLUMN_ICON, pixbuf,
                        PLACES_SIDEBAR_COLUMN_NAME, filename,
                        PLACES_SIDEBAR_COLUMN_URI, mount_uri,
                        PLACES_SIDEBAR_COLUMN_DRIVE, NULL,
                        PLACES_SIDEBAR_COLUMN_VOLUME, NULL,
                        PLACES_SIDEBAR_COLUMN_MOUNT, NULL,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, SECTION_FAVORITE,
                        PLACES_SIDEBAR_COLUMN_INDEX, index,
                        PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
                        PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
                        PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                        PLACES_SIDEBAR_COLUMN_TOOLTIP, _("Open the folder"),
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON, eject,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                        -1);

*/			   
    g_object_unref (icon);
  }

//#endif
/*

  GtkTreeModel *model;
  GtkTreeIter   iter;

  model = GTK_TREE_MODEL(sidebar);

  gtk_list_store_append(GTK_LIST_STORE(model), &iter);

//  gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_URI, (gchar*)seldata->data, -1);
  gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_URI, gtk_selection_data_get_text (selection_data), -1);
*/

/*
    GtkTreeView *tree_view;
    GtkTreePath *tree_path;
    GtkTreeViewDropPosition tree_pos;
    GtkTreeIter iter;
    int position;
    GtkTreeModel *model;
    char *drop_uri;
    GList *selection_list, *uris;
    PlaceType place_type;
    SectionType section_type;
    gboolean success;

    tree_view = GTK_TREE_VIEW (widget);

    if (!sidebar->drag_data_received)
    {
        if (gtk_selection_data_get_target (selection_data) != GDK_NONE &&
                info == TEXT_URI_LIST)
        {
            sidebar->drag_list = build_selection_list (gtk_selection_data_get_data (selection_data));
        }
        else
        {
            sidebar->drag_list = NULL;
        }
        sidebar->drag_data_received = TRUE;
        sidebar->drag_data_info = info;
    }

    g_signal_stop_emission_by_name (widget, "drag-data-received");

    if (!sidebar->drop_occured)
    {
        return;
    }

    success = compute_drop_position (tree_view, x, y, &tree_path, &tree_pos, sidebar);
    if (!success)
        goto out;

    success = FALSE;

    if (tree_pos == GTK_TREE_VIEW_DROP_BEFORE ||
        tree_pos == GTK_TREE_VIEW_DROP_AFTER)
    {
        model = gtk_tree_view_get_model (tree_view);

        if (!gtk_tree_model_get_iter (model, &iter, tree_path))
        {
            goto out;
        }

        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                            PLACES_SIDEBAR_COLUMN_INDEX, &position,
                            -1);

        if (section_type != SECTION_BOOKMARKS &&
            !(section_type == SECTION_NETWORK && place_type == PLACES_HEADING)) {
            goto out;
        }

        if (section_type == SECTION_NETWORK && place_type == PLACES_HEADING &&
            tree_pos == GTK_TREE_VIEW_DROP_BEFORE) {
            position = peony_bookmark_list_length (sidebar->bookmarks);
        }

        if (tree_pos == GTK_TREE_VIEW_DROP_AFTER && place_type != PLACES_HEADING) {
            position++;
        }

        switch (info)
        {
        case TEXT_URI_LIST:
            bookmarks_drop_uris (sidebar, selection_data, position);
            success = TRUE;
            break;
        case GTK_TREE_MODEL_ROW:
            reorder_bookmarks (sidebar, position);
            success = TRUE;
            break;
        default:
            g_assert_not_reached ();
            break;
        }
    }
    else
    {
        GdkDragAction real_action;

        real_action = gdk_drag_context_get_selected_action (context);

        if (real_action == GDK_ACTION_ASK)
        {
            real_action =
                peony_drag_drop_action_ask (GTK_WIDGET (tree_view),
                                           gdk_drag_context_get_actions (context));
        }

        if (real_action > 0)
        {
            model = gtk_tree_view_get_model (tree_view);

            gtk_tree_model_get_iter (model, &iter, tree_path);
            gtk_tree_model_get (model, &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &drop_uri,
                                -1);

            switch (info)
            {
            case TEXT_URI_LIST:
                selection_list = build_selection_list (gtk_selection_data_get_data (selection_data));
                uris = uri_list_from_selection (selection_list);
                peony_file_operations_copy_move (uris, NULL, drop_uri,
                                                real_action, GTK_WIDGET (tree_view),FALSE,
                                                NULL, NULL);
                peony_drag_destroy_selection_list (selection_list);
                g_list_free (uris);
                success = TRUE;
                break;
            case GTK_TREE_MODEL_ROW:
                success = FALSE;
                break;
            default:
                g_assert_not_reached ();
                break;
            }

            g_free (drop_uri);
        }
    }

out:
    sidebar->drop_occured = FALSE;
    free_drag_data (sidebar);
    gtk_drag_finish (context, success, FALSE, time);

    gtk_tree_path_free (tree_path);
    */
}

static gboolean
drag_drop_callback (GtkTreeView *tree_view,
                    GdkDragContext *context,
                    int x,
                    int y,
                    unsigned int time,
                    PeonyPlacesSidebar *sidebar)
{
    GtkTreePath *tree_path;
    GtkTreeViewDropPosition tree_pos;
    GtkTreeIter iter;
    int position;
    GtkTreeModel *model;
    char *drop_uri;
    GList *selection_list, *uris;
    PlaceType place_type;
    SectionType section_type;
    gboolean success;

    //printf ("drag_drop_callback\n");

    success = compute_drop_position (tree_view, x, y, &tree_path, &tree_pos, sidebar);
    if (!success) {
        uris = uri_list_from_selection (sidebar->drag_list); 
        GIcon *icon = g_themed_icon_new (PEONY_ICON_FOLDER);
        GtkTreeIter last_iter;
        char *location;
        char *last_uri;
        GtkTreePath *select_path;   	
  
        char *mount_uri;
        GFile *file1;
        char *filename;
        GList *l;
        for (l = uris; l != NULL; l = l->next)
        {
            mount_uri = l->data;
            file1=g_file_new_for_uri (mount_uri);
            filename=g_file_get_basename (file1);
            PeonyFile *file = peony_file_get_existing_by_uri (l->data);
            if (!peony_file_is_directory (file)) {
                printf ("not a dir!\n");
                g_object_unref (file1);
                g_free (filename);
                peony_file_unref (file);
                continue;
            }
            last_iter = insert_place (sidebar, PLACES_BUILT_IN,
                                      SECTION_FAVORITE,
                                      filename, icon, mount_uri,
                                      NULL, NULL, NULL, 0,
                                      _("Open the folder"));
            compare_for_selection (sidebar,
                                   location, mount_uri, last_uri,
                                   &last_iter, &select_path);
        
            g_object_unref (file1);
            g_free (filename);
        }
        g_object_unref (icon);
        g_list_free (uris);
        //printf ("drop the file to favorite\n");
    }
    gboolean retval = FALSE;
    sidebar->drop_occured = TRUE;
    retval = get_drag_data (tree_view, context, time);
    g_signal_stop_emission_by_name (tree_view, "drag-drop");
    return retval;
}

static gboolean
drag_drop_callback0 (GtkTreeView *tree_view,
                    GdkDragContext *context,
                    int x,
                    int y,
                    unsigned int time,
                    PeonyPlacesSidebar *sidebar)
{
    gboolean retval = FALSE;
    sidebar->drop_occured = TRUE;
    retval = get_drag_data (tree_view, context, time);
    g_signal_stop_emission_by_name (tree_view, "drag-drop");
    return retval;
}

/* Callback used when the file list's popup menu is detached */
static void
bookmarks_popup_menu_detach_cb (GtkWidget *attach_widget,
                                GtkMenu   *menu)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = PEONY_PLACES_SIDEBAR (attach_widget);
    g_assert (PEONY_IS_PLACES_SIDEBAR (sidebar));

    sidebar->popup_menu = NULL;
    sidebar->popup_menu_remove_item = NULL;
    sidebar->popup_menu_rename_item = NULL;
    sidebar->popup_menu_separator_item = NULL;
    sidebar->popup_menu_mount_item = NULL;
    sidebar->popup_menu_unmount_item = NULL;
    sidebar->popup_menu_eject_item = NULL;
    sidebar->popup_menu_rescan_item = NULL;
    sidebar->popup_menu_format_item = NULL;
    sidebar->popup_menu_start_item = NULL;
    sidebar->popup_menu_stop_item = NULL;
    sidebar->popup_menu_empty_trash_item = NULL;
}

static void
check_unmount_and_eject (GMount *mount,
                         GVolume *volume,
                         GDrive *drive,
                         gboolean *show_unmount,
                         gboolean *show_eject)
{
    *show_unmount = FALSE;
    *show_eject = FALSE;

    if (drive != NULL)
    {
        *show_eject = g_drive_can_eject (drive);
    }

    if (volume != NULL)
    {
        *show_eject |= g_volume_can_eject (volume);
    }
    if (mount != NULL)
    {
        *show_eject |= g_mount_can_eject (mount);
        *show_unmount = g_mount_can_unmount (mount) && !*show_eject;
    }
}

static void
check_visibility (GMount           *mount,
                  GVolume          *volume,
                  GDrive           *drive,
                  gboolean         *show_mount,
                  gboolean         *show_unmount,
                  gboolean         *show_eject,
                  gboolean         *show_rescan,
                  gboolean         *show_format,
                  gboolean         *show_start,
                  gboolean         *show_stop)
{
    *show_mount = FALSE;
    *show_format = FALSE;
    *show_rescan = FALSE;
    *show_start = FALSE;
    *show_stop = FALSE;

    check_unmount_and_eject (mount, volume, drive, show_unmount, show_eject);

    if (drive != NULL)
    {
        if (g_drive_is_media_removable (drive) &&
                !g_drive_is_media_check_automatic (drive) &&
                g_drive_can_poll_for_media (drive))
            *show_rescan = TRUE;

        *show_start = g_drive_can_start (drive) || g_drive_can_start_degraded (drive);
        *show_stop  = g_drive_can_stop (drive);

        if (*show_stop)
            *show_unmount = FALSE;
    }

    if (volume != NULL)
    {
        if (mount == NULL)
            *show_mount = g_volume_can_mount (volume);
    }
}

static void
bookmarks_check_popup_sensitivity (PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter1;
    GtkTreeView *treeview1 = sidebar->tree_view;
    GtkTreeModel *model1 = gtk_tree_view_get_model (treeview1);
    GtkTreeSelection *selection1 = gtk_tree_view_get_selection (treeview1);
    int selected_iter_number=0;
    if (gtk_tree_selection_get_selected (selection1, NULL, &iter1))
    {
        gint i;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model1, &iter1);
        i = gtk_tree_path_get_indices (path)[0];
        selected_iter_number = i;
        gtk_tree_path_free (path);
    }

    char            *path1;
    GSettings       *settings1;
    settings1                       = g_settings_new("org.ukui.peony.preferences");
    int position=g_settings_get_int(settings1,"favorite-iter-position");

    GtkTreeIter iter;
    PlaceType type;
    GDrive *drive = NULL;
    GVolume *volume = NULL;
    GMount *mount = NULL;
    gboolean show_mount;
    gboolean show_unmount;
    gboolean show_eject;
    gboolean show_rescan;
    gboolean show_format;
    gboolean show_start;
    gboolean show_stop;
    gboolean show_empty_trash;
    char *uri = NULL;

    type = PLACES_BUILT_IN;

    if (sidebar->popup_menu == NULL)
    {
        return;
    }

    if (get_selected_iter (sidebar, &iter))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                            PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                            PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                            PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                            PLACES_SIDEBAR_COLUMN_URI, &uri,
                            -1);
    }

    gtk_widget_show (sidebar->popup_menu_open_in_new_tab_item);

//    gtk_widget_set_sensitive (sidebar->popup_menu_remove_item, (type == PLACES_BOOKMARK));
    if (selected_iter_number<position && selected_iter_number > 4){
    	gtk_widget_set_sensitive (sidebar->popup_menu_remove_item, TRUE);
    } else {
    	gtk_widget_set_sensitive (sidebar->popup_menu_remove_item, FALSE);
    }
    gtk_widget_set_sensitive (sidebar->popup_menu_rename_item, (type == PLACES_BOOKMARK));
    gtk_widget_set_sensitive (sidebar->popup_menu_empty_trash_item, !peony_trash_monitor_is_empty ());

    check_visibility (mount, volume, drive,
                      &show_mount, &show_unmount, &show_eject, &show_rescan, &show_format, &show_start, &show_stop);

    /* We actually want both eject and unmount since eject will unmount all volumes.
     * TODO: hide unmount if the drive only has a single mountable volume
     */

    if (show_unmount == TRUE || show_eject ==TRUE)
            show_format = TRUE;

    show_empty_trash = (uri != NULL) &&
                       (!strcmp (uri, "trash:///"));

    gtk_widget_set_visible (sidebar->popup_menu_separator_item,
                              show_mount || show_unmount || show_eject || show_format || show_empty_trash);
    gtk_widget_set_visible (sidebar->popup_menu_mount_item, show_mount);
    gtk_widget_set_visible (sidebar->popup_menu_unmount_item, show_unmount);
    gtk_widget_set_visible (sidebar->popup_menu_eject_item, show_eject);
    gtk_widget_set_visible (sidebar->popup_menu_rescan_item, show_rescan);
    gtk_widget_set_visible (sidebar->popup_menu_format_item, show_format);
    gtk_widget_set_visible (sidebar->popup_menu_start_item, show_start);
    gtk_widget_set_visible (sidebar->popup_menu_stop_item, show_stop);
    gtk_widget_set_visible (sidebar->popup_menu_empty_trash_item, show_empty_trash);

    /* Adjust start/stop items to reflect the type of the drive */
    gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Start"));
    gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Stop"));
    if ((show_start || show_stop) && drive != NULL)
    {
        switch (g_drive_get_start_stop_type (drive))
        {
        case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
            /* start() for type G_DRIVE_START_STOP_TYPE_SHUTDOWN is normally not used */
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Power On"));
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Safely Remove Drive"));
            break;
        case G_DRIVE_START_STOP_TYPE_NETWORK:
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Connect Drive"));
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Disconnect Drive"));
            break;
        case G_DRIVE_START_STOP_TYPE_MULTIDISK:
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Start Multi-disk Device"));
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Stop Multi-disk Device"));
            break;
        case G_DRIVE_START_STOP_TYPE_PASSWORD:
            /* stop() for type G_DRIVE_START_STOP_TYPE_PASSWORD is normally not used */
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Unlock Drive"));
            gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Lock Drive"));
            break;

        default:
        case G_DRIVE_START_STOP_TYPE_UNKNOWN:
            /* uses defaults set above */
            break;
        }
    }


    g_free (uri);
}

/* Callback used when the selection in the shortcuts tree changes */
static void
bookmarks_selection_changed_cb (GtkTreeSelection      *selection,
                                PeonyPlacesSidebar *sidebar)
{
    bookmarks_check_popup_sensitivity (sidebar);
}

static void
volume_mounted_cb (GVolume *volume,
                   GObject *user_data)
{
    GMount *mount;
    PeonyPlacesSidebar *sidebar;
    GFile *location;

    sidebar = PEONY_PLACES_SIDEBAR (user_data);

    sidebar->mounting = FALSE;

    mount = g_volume_get_mount (volume);
    if (mount != NULL)
    {
        location = g_mount_get_default_location (mount);

        if (sidebar->go_to_after_mount_slot != NULL)
        {
            if ((sidebar->go_to_after_mount_flags & PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0)
            {
                peony_window_slot_info_open_location (sidebar->go_to_after_mount_slot, location,
                                                     PEONY_WINDOW_OPEN_ACCORDING_TO_MODE,
                                                     sidebar->go_to_after_mount_flags, NULL);
            }
            else
            {
                PeonyWindow *cur, *new;

                cur = PEONY_WINDOW (sidebar->window);
                new = peony_application_create_navigation_window (cur->application,
                        gtk_window_get_screen (GTK_WINDOW (cur)));
                peony_window_go_to (new, location);
            }
        }

        g_object_unref (G_OBJECT (location));
        g_object_unref (G_OBJECT (mount));
    }


    eel_remove_weak_pointer (&(sidebar->go_to_after_mount_slot));
}

static void
drive_start_from_bookmark_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
    GError *error;
    char *primary;
    char *name;

    error = NULL;
    if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to start %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
}

static void
open_selected_bookmark (PeonyPlacesSidebar   *sidebar,
                        GtkTreeModel        *model,
                        GtkTreePath         *path,
                        PeonyWindowOpenFlags  flags)
{
    PeonyWindowSlotInfo *slot;
    GtkTreeIter iter;
    GFile *location;
    char *uri;

    if (!path)
    {
        return;
    }

    if (!gtk_tree_model_get_iter (model, &iter, path))
    {
        return;
    }

    gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

    if (uri != NULL)
    {
        peony_debug_log (FALSE, PEONY_DEBUG_LOG_DOMAIN_USER,
                        "activate from places sidebar window=%p: %s",
                        sidebar->window, uri);
        location = g_file_new_for_uri (uri);
        /* Navigate to the clicked location */
        if ((flags & PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0)
        {
            slot = peony_window_info_get_active_slot (sidebar->window);
            peony_window_slot_info_open_location (slot, location,
                                                 PEONY_WINDOW_OPEN_ACCORDING_TO_MODE,
                                                 flags, NULL);
        }
        else
        {
            PeonyWindow *cur, *new;

            cur = PEONY_WINDOW (sidebar->window);
            new = peony_application_create_navigation_window (cur->application,
                    gtk_window_get_screen (GTK_WINDOW (cur)));
            peony_window_go_to (new, location);
        }
        g_object_unref (location);
        g_free (uri);

    }
    else
    {
        GDrive *drive;
        GVolume *volume;
        PeonyWindowSlot *slot;

        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                            PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                            -1);

        if (volume != NULL && !sidebar->mounting)
        {
            sidebar->mounting = TRUE;

            g_assert (sidebar->go_to_after_mount_slot == NULL);

            slot = peony_window_info_get_active_slot (sidebar->window);
            sidebar->go_to_after_mount_slot = slot;
            eel_add_weak_pointer (&(sidebar->go_to_after_mount_slot));

            sidebar->go_to_after_mount_flags = flags;

            peony_file_operations_mount_volume_full (NULL, volume, FALSE,
                                                    volume_mounted_cb,
                                                    G_OBJECT (sidebar));
        }
        else if (volume == NULL && drive != NULL &&
                 (g_drive_can_start (drive) || g_drive_can_start_degraded (drive)))
        {
            GMountOperation *mount_op;

            mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
            g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_from_bookmark_cb, NULL);
            g_object_unref (mount_op);
        }

        if (drive != NULL)
            g_object_unref (drive);
        if (volume != NULL)
            g_object_unref (volume);
    }
}

static void
open_shortcut_from_menu (PeonyPlacesSidebar   *sidebar,
                         PeonyWindowOpenFlags  flags)
{
    GtkTreeModel *model;
    GtkTreePath *path;

    model = gtk_tree_view_get_model (sidebar->tree_view);
    gtk_tree_view_get_cursor (sidebar->tree_view, &path, NULL);

    open_selected_bookmark (sidebar, model, path, flags);

    gtk_tree_path_free (path);
}

static void
open_shortcut_cb (GtkMenuItem       *item,
                  PeonyPlacesSidebar *sidebar)
{
    open_shortcut_from_menu (sidebar, 0);
}

static void
open_shortcut_in_new_window_cb (GtkMenuItem       *item,
                                PeonyPlacesSidebar *sidebar)
{
    open_shortcut_from_menu (sidebar, PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

static void
open_shortcut_in_new_tab_cb (GtkMenuItem       *item,
                             PeonyPlacesSidebar *sidebar)
{
    open_shortcut_from_menu (sidebar, PEONY_WINDOW_OPEN_FLAG_NEW_TAB);
}

/* Rename the selected bookmark */
static void
rename_selected_bookmark (PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    GtkCellRenderer *cell;
    GList *renderers;

    if (get_selected_iter (sidebar, &iter))
    {
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->filter_model), &iter);
        column = gtk_tree_view_get_column (GTK_TREE_VIEW (sidebar->tree_view), 0);
        renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
        cell = g_list_nth_data (renderers, 6);
        g_list_free (renderers);
        g_object_set (cell, "editable", TRUE, NULL);
        gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sidebar->tree_view),
                                          path, column, cell, TRUE);
        gtk_tree_path_free (path);
    }
}

static void
rename_shortcut_cb (GtkMenuItem           *item,
                    PeonyPlacesSidebar *sidebar)
{
    rename_selected_bookmark (sidebar);
}

/* Removes the selected bookmarks */
static void
remove_selected_bookmarks (PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    PlaceType type;
    int index;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        -1);

    if (type != PLACES_BOOKMARK)
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_INDEX, &index,
                        -1);

    peony_bookmark_list_delete_item_at (sidebar->bookmarks, index);
}

static void
remove_shortcut_cb (GtkMenuItem           *item,
                    PeonyPlacesSidebar *sidebar)
{
//  remove_selected_bookmarks (sidebar);
    GtkTreeIter iter;
    GtkTreeView *treeview = sidebar->tree_view;
    GtkTreeModel *model = gtk_tree_view_get_model (treeview);
    GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

    if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
        gint i;
        GtkTreePath *path;

        path = gtk_tree_model_get_path (model, &iter);
        i = gtk_tree_path_get_indices (path)[0];

        i = i-4;
        char buf[100];
        char favorite_files_path[100];
        sprintf(favorite_files_path,"%s/.config/peony/favorite-files",g_get_home_dir ());
        sprintf(buf,"sed -i '%d d' %s",i,favorite_files_path);
        system(buf);

        gtk_tree_path_free (path);
    }      

    char            *path1;
    GSettings       *settings1;
    settings1                       = g_settings_new("org.ukui.peony.preferences");
    int position=g_settings_get_int(settings1,"favorite-iter-position");
    position = position -1;
    g_settings_set_int(settings1,"favorite-iter-position",position);

    update_places (sidebar);
}

static void
mount_shortcut_cb (GtkMenuItem           *item,
                   PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GVolume *volume;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                        -1);

    if (volume != NULL)
    {
        peony_file_operations_mount_volume (NULL, volume, FALSE);
        g_object_unref (volume);
    }
}

static void
unmount_done (gpointer data)
{
    PeonyWindow *window;

    window = data;
    peony_window_info_set_initiated_unmount (window, FALSE);
    g_object_unref (window);
}

static void
do_unmount (GMount *mount,
            PeonyPlacesSidebar *sidebar)
{
    if (mount != NULL)
    {
        peony_window_info_set_initiated_unmount (sidebar->window, TRUE);
        peony_file_operations_unmount_mount_full (NULL, mount, FALSE, TRUE,
                unmount_done,
                g_object_ref (sidebar->window));
    }
}

static void
do_unmount_selection (PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GMount *mount;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                        -1);

    if (mount != NULL)
    {
        do_unmount (mount, sidebar);
        g_object_unref (mount);
    }
}

static void
unmount_shortcut_cb (GtkMenuItem           *item,
                     PeonyPlacesSidebar *sidebar)
{
    do_unmount_selection (sidebar);
}

static void
drive_eject_cb (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
    PeonyWindow *window;
    GError *error;
    char *primary;
    char *name;

    window = user_data;
    peony_window_info_set_initiated_unmount (window, FALSE);
    g_object_unref (window);

    error = NULL;
    if (!g_drive_eject_with_operation_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to eject %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
    else {
        peony_application_notify_unmount_show ("It is now safe to remove the drive");
    }
}

static void
volume_eject_cb (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
    PeonyWindow *window;
    GError *error;
    char *primary;
    char *name;

    window = user_data;
    peony_window_info_set_initiated_unmount (window, FALSE);
    g_object_unref (window);

    error = NULL;
    if (!g_volume_eject_with_operation_finish (G_VOLUME (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_volume_get_name (G_VOLUME (source_object));
            primary = g_strdup_printf (_("Unable to eject %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error); 
    }

    else {
        peony_application_notify_unmount_show ("It is now safe to remove the drive");
    }
}

static void
mount_eject_cb (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
    PeonyWindow *window;
    GError *error;
    char *primary;
    char *name;

    window = user_data;
    peony_window_info_set_initiated_unmount (window, FALSE);
    g_object_unref (window);

    error = NULL;
    if (!g_mount_eject_with_operation_finish (G_MOUNT (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_mount_get_name (G_MOUNT (source_object));
            primary = g_strdup_printf (_("Unable to eject %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }

    else {
        peony_application_notify_unmount_show ("It is now safe to remove the drive");
    }
}

static void
do_eject (GMount *mount,
          GVolume *volume,
          GDrive *drive,
          PeonyPlacesSidebar *sidebar)
{

    GMountOperation *mount_op;

    mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));

    if (mount != NULL)
    {
        peony_window_info_set_initiated_unmount (sidebar->window, TRUE);
        g_mount_eject_with_operation (mount, 0, mount_op, NULL, mount_eject_cb,
                                      g_object_ref (sidebar->window));
    }
    else if (volume != NULL)
    {
        peony_window_info_set_initiated_unmount (sidebar->window, TRUE);
        g_volume_eject_with_operation (volume, 0, mount_op, NULL, volume_eject_cb,
                                       g_object_ref (sidebar->window));
    }
    else if (drive != NULL)
    {
        peony_window_info_set_initiated_unmount (sidebar->window, TRUE);
        g_drive_eject_with_operation (drive, 0, mount_op, NULL, drive_eject_cb,
                                      g_object_ref (sidebar->window));
    }

    peony_application_notify_unmount_show ("writing data to the drive-do not unplug");
    g_object_unref (mount_op);
}

static void
eject_shortcut_cb (GtkMenuItem           *item,
                   PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GMount *mount;
    GVolume *volume;
    GDrive *drive;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                        PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        -1);

    do_eject (mount, volume, drive, sidebar);
}

static gboolean
eject_or_unmount_bookmark (PeonyPlacesSidebar *sidebar,
                           GtkTreePath *path)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean can_unmount, can_eject;
    GMount *mount;
    GVolume *volume;
    GDrive *drive;
    gboolean ret;

    model = GTK_TREE_MODEL (sidebar->filter_model);

    if (!path)
    {
        return FALSE;
    }
    if (!gtk_tree_model_get_iter (model, &iter, path))
    {
        return FALSE;
    }

    gtk_tree_model_get (model, &iter,
                        PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                        PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        -1);

    ret = FALSE;

    check_unmount_and_eject (mount, volume, drive, &can_unmount, &can_eject);
    /* if we can eject, it has priority over unmount */
    if (can_eject)
    {
        do_eject (mount, volume, drive, sidebar);
        ret = TRUE;
    }
    else if (can_unmount)
    {
        do_unmount (mount, sidebar);
        ret = TRUE;
    }

    if (mount != NULL)
        g_object_unref (mount);
    if (volume != NULL)
        g_object_unref (volume);
    if (drive != NULL)
        g_object_unref (drive);

    return ret;
}

static gboolean
eject_or_unmount_selection (PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean ret;

    if (!get_selected_iter (sidebar, &iter)) {
        return FALSE;
    }

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->filter_model), &iter);
    if (path == NULL) {
        return FALSE;
    }

    ret = eject_or_unmount_bookmark (sidebar, path);

    gtk_tree_path_free (path);

    return ret;
}

static void
drive_poll_for_media_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
    GError *error;
    char *primary;
    char *name;

    error = NULL;
    if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to poll %s for media changes"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
}

static void
rescan_shortcut_cb (GtkMenuItem           *item,
                    PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GDrive  *drive;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        -1);

    if (drive != NULL)
    {
        g_drive_poll_for_media (drive, NULL, drive_poll_for_media_cb, NULL);
    }
    g_object_unref (drive);
}

static void
format_shortcut_cb (GtkMenuItem           *item,
                    PeonyPlacesSidebar *sidebar)
{
    char *res, *volume_path;
    GtkTreeIter iter;
    GVolume *volume;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                        -1);

    volume_path = g_volume_get_identifier(volume,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	
      kdiskformat(volume_path);

}

static void
drive_start_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
    GError *error;
    char *primary;
    char *name;

    error = NULL;
    if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to start %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
}

static void
start_shortcut_cb (GtkMenuItem           *item,
                   PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GDrive  *drive;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        -1);

    if (drive != NULL)
    {
        GMountOperation *mount_op;

        mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));

        g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_cb, NULL);

        g_object_unref (mount_op);
    }
    g_object_unref (drive);
}

static void
drive_stop_cb (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
    PeonyWindow *window;
    GError *error;
    char *primary;
    char *name;

    window = user_data;
    peony_window_info_set_initiated_unmount (window, FALSE);
    g_object_unref (window);

    error = NULL;
    if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to stop %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
}

static void
stop_shortcut_cb (GtkMenuItem           *item,
                  PeonyPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    GDrive  *drive;

    if (!get_selected_iter (sidebar, &iter))
    {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        -1);

    if (drive != NULL)
    {
        GMountOperation *mount_op;

        mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
        peony_window_info_set_initiated_unmount (sidebar->window, TRUE);
        g_drive_stop (drive, G_MOUNT_UNMOUNT_NONE, mount_op, NULL, drive_stop_cb,
                      g_object_ref (sidebar->window));
        g_object_unref (mount_op);
    }
    g_object_unref (drive);
}

static void
empty_trash_cb (GtkMenuItem           *item,
                PeonyPlacesSidebar *sidebar)
{
    peony_file_operations_empty_trash (GTK_WIDGET (sidebar->window));
}

/* Handler for GtkWidget::key-press-event on the shortcuts list */
static gboolean
bookmarks_key_press_event_cb (GtkWidget             *widget,
                              GdkEventKey           *event,
                              PeonyPlacesSidebar *sidebar)
{
    guint modifiers;
    GtkTreeModel *model;
    GtkTreePath *path;
    PeonyWindowOpenFlags flags = 0;

    modifiers = gtk_accelerator_get_default_mod_mask ();

    if (event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter ||
        event->keyval == GDK_KEY_ISO_Enter ||
        event->keyval == GDK_KEY_space)
    {
        if ((event->state & modifiers) == GDK_SHIFT_MASK)
            flags = PEONY_WINDOW_OPEN_FLAG_NEW_TAB;
        else if ((event->state & modifiers) == GDK_CONTROL_MASK)
            flags = PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW;

        model = gtk_tree_view_get_model(sidebar->tree_view);
        gtk_tree_view_get_cursor(sidebar->tree_view, &path, NULL);

        open_selected_bookmark(sidebar, model, path, flags);

        gtk_tree_path_free(path);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Down &&
            (event->state & modifiers) == GDK_MOD1_MASK)
    {
        return eject_or_unmount_selection (sidebar);
    }

    if ((event->keyval == GDK_KEY_Delete
            || event->keyval == GDK_KEY_KP_Delete)
            && (event->state & modifiers) == 0)
    {
        remove_selected_bookmarks (sidebar);
        return TRUE;
    }

    if ((event->keyval == GDK_KEY_F2)
            && (event->state & modifiers) == 0)
    {
        rename_selected_bookmark (sidebar);
        return TRUE;
    }

    return FALSE;
}

/* Constructs the popup menu for the file list if needed */
static void
bookmarks_build_popup_menu (PeonyPlacesSidebar *sidebar)
{
    GtkWidget *item;

    if (sidebar->popup_menu)
    {
        return;
    }

    sidebar->popup_menu = gtk_menu_new ();
    gtk_menu_attach_to_widget (GTK_MENU (sidebar->popup_menu),
                               GTK_WIDGET (sidebar),
                               bookmarks_popup_menu_detach_cb);

    item = gtk_image_menu_item_new_with_mnemonic (_("_Open"));
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                   gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU));
    g_signal_connect (item, "activate",
                      G_CALLBACK (open_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Open in New _Tab"));
    sidebar->popup_menu_open_in_new_tab_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (open_shortcut_in_new_tab_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Open in New _Window"));
    g_signal_connect (item, "activate",
                      G_CALLBACK (open_shortcut_in_new_window_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    eel_gtk_menu_append_separator (GTK_MENU (sidebar->popup_menu));

    item = gtk_image_menu_item_new_with_label (_("Remove"));
    sidebar->popup_menu_remove_item = item;
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                   gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
    g_signal_connect (item, "activate",
                      G_CALLBACK (remove_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_label (_("Rename..."));
    sidebar->popup_menu_rename_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (rename_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    /* Mount/Unmount/Eject menu items */

    sidebar->popup_menu_separator_item =
        GTK_WIDGET (eel_gtk_menu_append_separator (GTK_MENU (sidebar->popup_menu)));

    item = gtk_menu_item_new_with_mnemonic (_("_Mount"));
    sidebar->popup_menu_mount_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (mount_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Unmount"));
    sidebar->popup_menu_unmount_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (unmount_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Eject"));
    sidebar->popup_menu_eject_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (eject_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Detect Media"));
    sidebar->popup_menu_rescan_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (rescan_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Format"));
    sidebar->popup_menu_format_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (format_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Start"));
    sidebar->popup_menu_start_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (start_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("_Stop"));
    sidebar->popup_menu_stop_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (stop_shortcut_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    /* Empty Trash menu item */

    item = gtk_menu_item_new_with_mnemonic (_("Empty _Trash"));
    sidebar->popup_menu_empty_trash_item = item;
    g_signal_connect (item, "activate",
                      G_CALLBACK (empty_trash_cb), sidebar);
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

    bookmarks_check_popup_sensitivity (sidebar);
}

static void
bookmarks_update_popup_menu (PeonyPlacesSidebar *sidebar)
{
    bookmarks_build_popup_menu (sidebar);
}

static void
bookmarks_popup_menu (PeonyPlacesSidebar *sidebar,
                      GdkEventButton        *event)
{
    bookmarks_update_popup_menu (sidebar);
    eel_pop_up_context_menu (GTK_MENU(sidebar->popup_menu),
                             EEL_DEFAULT_POPUP_MENU_DISPLACEMENT,
                             EEL_DEFAULT_POPUP_MENU_DISPLACEMENT,
                             event);
}

/* Callback used for the GtkWidget::popup-menu signal of the shortcuts list */
static gboolean
bookmarks_popup_menu_cb (GtkWidget *widget,
                         PeonyPlacesSidebar *sidebar)
{
    bookmarks_popup_menu (sidebar, NULL);
    return TRUE;
}

static gboolean
bookmarks_button_release_event_cb (GtkWidget *widget,
                                   GdkEventButton *event,
                                   PeonyPlacesSidebar *sidebar)
{
printf("bookmarks_button_release_event_cb===\n");
    GtkTreePath *path;
    GtkTreeModel *model;
    GtkTreeView *tree_view;

    path = NULL;

    if (event->type != GDK_BUTTON_RELEASE)
    {
        return TRUE;
    }

    if (clicked_eject_button (sidebar, &path))
    {
        eject_or_unmount_bookmark (sidebar, path);
        gtk_tree_path_free (path);
        return FALSE;
    }

    tree_view = GTK_TREE_VIEW (widget);
    model = gtk_tree_view_get_model (tree_view);

    if (event->button == 1)
    {

        if (event->window != gtk_tree_view_get_bin_window (tree_view))
        {
            return FALSE;
        }

        gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
                                       &path, NULL, NULL, NULL);

        open_selected_bookmark (sidebar, model, path, 0);

        gtk_tree_path_free (path);
    }

    return FALSE;
}

static void
update_eject_buttons (PeonyPlacesSidebar *sidebar,
                      GtkTreePath         *path)
{
    GtkTreeIter iter;
    gboolean icon_visible, path_same;

    icon_visible = TRUE;

    if (path == NULL && sidebar->eject_highlight_path == NULL) {
        /* Both are null - highlight up to date */
        return;
    }

    path_same = (path != NULL) &&
        (sidebar->eject_highlight_path != NULL) &&
        (gtk_tree_path_compare (sidebar->eject_highlight_path, path) == 0);

    if (path_same) {
        /* Same path - highlight up to date */
        return;
    }

    if (path) {
        gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->filter_model),
                     &iter,
                     path);

        gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model),
                    &iter,
                    PLACES_SIDEBAR_COLUMN_EJECT, &icon_visible,
                    -1);
    }

    if (!icon_visible || path == NULL || !path_same) {
        /* remove highlighting and reset the saved path, as we are leaving
         * an eject button area.
         */
        if (sidebar->eject_highlight_path) {
            gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store),
                         &iter,
                         sidebar->eject_highlight_path);

            gtk_list_store_set (sidebar->store,
                        &iter,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON, get_eject_icon (FALSE),
                        -1);
            gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));

            gtk_tree_path_free (sidebar->eject_highlight_path);
            sidebar->eject_highlight_path = NULL;
        }

        if (!icon_visible) {
            return;
        }
    }

    if (path != NULL) {
        /* add highlighting to the selected path, as the icon is visible and
         * we're hovering it.
         */
        gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store),
                     &iter,
                     path);
        gtk_list_store_set (sidebar->store,
                    &iter,
                    PLACES_SIDEBAR_COLUMN_EJECT_ICON, get_eject_icon (TRUE),
                    -1);
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->filter_model));

        sidebar->eject_highlight_path = gtk_tree_path_copy (path);
    }
}

static gboolean
bookmarks_motion_event_cb (GtkWidget             *widget,
                           GdkEventMotion        *event,
                           PeonyPlacesSidebar *sidebar)
{
    GtkTreePath *path;
    GtkTreeModel *model;

    model = GTK_TREE_MODEL (sidebar->filter_model);
    path = NULL;

    if (over_eject_button (sidebar, event->x, event->y, &path)) {
        update_eject_buttons (sidebar, path);
        gtk_tree_path_free (path);

        return TRUE;
    }

    update_eject_buttons (sidebar, NULL);

    return FALSE;
}

/* Callback used when a button is pressed on the shortcuts list.
 * We trap button 3 to bring up a popup menu, and button 2 to
 * open in a new tab.
 */
static gboolean
bookmarks_button_press_event_cb (GtkWidget             *widget,
                                 GdkEventButton        *event,
                                 PeonyPlacesSidebar *sidebar)
{
    if (event->type != GDK_BUTTON_PRESS)
    {
        /* ignore multiple clicks */
        return TRUE;
    }

    if (event->button == 3)
    {
        bookmarks_popup_menu (sidebar, event);
    }
    else if (event->button == 2)
    {
        GtkTreeModel *model;
        GtkTreePath *path;
        GtkTreeView *tree_view;

        tree_view = GTK_TREE_VIEW (widget);
        g_assert (tree_view == sidebar->tree_view);

        model = gtk_tree_view_get_model (tree_view);

        gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
                                       &path, NULL, NULL, NULL);

        open_selected_bookmark (sidebar, model, path,
                                event->state & GDK_CONTROL_MASK ?
                                PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW :
                                PEONY_WINDOW_OPEN_FLAG_NEW_TAB);

        if (path != NULL)
        {
            gtk_tree_path_free (path);
            return TRUE;
        }
    }

    return FALSE;
}


static void
bookmarks_edited (GtkCellRenderer       *cell,
                  gchar                 *path_string,
                  gchar                 *new_text,
                  PeonyPlacesSidebar *sidebar)
{
    GtkTreePath *path;
    GtkTreeIter iter;
    PeonyBookmark *bookmark;
    int index;

    g_object_set (cell, "editable", FALSE, NULL);

    path = gtk_tree_path_new_from_string (path_string);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->filter_model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->filter_model), &iter,
                        PLACES_SIDEBAR_COLUMN_INDEX, &index,
                        -1);
    gtk_tree_path_free (path);
    bookmark = peony_bookmark_list_item_at (sidebar->bookmarks, index);

    if (bookmark != NULL)
    {
        peony_bookmark_set_name (bookmark, new_text);
    }
}

static void
bookmarks_editing_canceled (GtkCellRenderer       *cell,
                            PeonyPlacesSidebar *sidebar)
{
    g_object_set (cell, "editable", FALSE, NULL);
}

static void
trash_state_changed_cb (PeonyTrashMonitor    *trash_monitor,
                        gboolean             state,
                        gpointer             data)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = PEONY_PLACES_SIDEBAR (data);

    /* The trash icon changed, update the sidebar */
    update_places (sidebar);

    bookmarks_check_popup_sensitivity (sidebar);
}

static gboolean
tree_selection_func (GtkTreeSelection *selection,
                     GtkTreeModel *model,
                     GtkTreePath *path,
                     gboolean path_currently_selected,
                     gpointer user_data)
{
    GtkTreeIter iter;
    PlaceType row_type;

    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model, &iter,
                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
                -1);

    if (row_type == PLACES_HEADING) {
        return FALSE;
    }

    return TRUE;
}

static void
icon_cell_renderer_func (GtkTreeViewColumn *column,
                         GtkCellRenderer *cell,
                         GtkTreeModel *model,
                         GtkTreeIter *iter,
                         gpointer user_data)
{
    PeonyPlacesSidebar *sidebar;
    PlaceType type;

    sidebar = user_data;

    gtk_tree_model_get (model, iter,
                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                -1);

    if (type == PLACES_HEADING) {
        g_object_set (cell,
                  "visible", FALSE,
                  NULL);
    } else {
        g_object_set (cell,
                  "visible", TRUE,
                  NULL);
    }
}

static void
padding_cell_renderer_func (GtkTreeViewColumn *column,
                            GtkCellRenderer *cell,
                            GtkTreeModel *model,
                            GtkTreeIter *iter,
                            gpointer user_data)
{
    PlaceType type;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        -1);

    if (type == PLACES_HEADING) {
        g_object_set (cell,
                      "visible", FALSE,
                      "xpad", 0,
                      "ypad", 0,
                      NULL);
    } else {
        g_object_set (cell,
                      "visible", TRUE,
                      "xpad", 3,
                      "ypad", 0,
                      NULL);
    }
}

static void
heading_cell_renderer_func (GtkTreeViewColumn *column,
                        GtkCellRenderer *cell,
                        GtkTreeModel *model,
                        GtkTreeIter *iter,
                        gpointer user_data)
{
    PlaceType type;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        -1);

    if (type == PLACES_HEADING) {
        g_object_set (cell,
                      "visible", TRUE,
                      NULL);
    } else {
        g_object_set (cell,
                      "visible", FALSE,
                      NULL);
    }
}

static void
peony_places_sidebar_init (PeonyPlacesSidebar *sidebar)
{
    GtkTreeView       *tree_view;
    GtkTreeViewColumn *col;
    GtkCellRenderer   *cell;
    GtkTreeSelection  *selection;

    sidebar->volume_monitor = g_volume_monitor_get ();

    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sidebar),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
    gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sidebar), GTK_SHADOW_IN);

    /* tree view */
    tree_view = GTK_TREE_VIEW (gtk_tree_view_new ());
    gtk_tree_view_set_headers_visible (tree_view, FALSE);

    col = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());

    /* initial padding */
    cell = gtk_cell_renderer_text_new ();
    sidebar->padding_cell_renderer = cell;
    gtk_tree_view_column_pack_start (col, cell, FALSE);
    g_object_set (cell,
                  "xpad", 6,
                  NULL);

    /* headings */
    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, cell, FALSE);
    gtk_tree_view_column_set_attributes (col, cell,
                                         "text", PLACES_SIDEBAR_COLUMN_HEADING_TEXT,
                                         NULL);
    g_object_set (cell,
                  "weight", PANGO_WEIGHT_BOLD,
                  "weight-set", TRUE,
                  "ypad", 1,
                  "xpad", 0,
                  NULL);
    gtk_tree_view_column_set_cell_data_func (col, cell,
                         heading_cell_renderer_func,
                         sidebar, NULL);

    /* icon padding */
    cell = gtk_cell_renderer_text_new ();
    sidebar->icon_padding_cell_renderer = cell;
    gtk_tree_view_column_pack_start (col, cell, FALSE);
    gtk_tree_view_column_set_cell_data_func (col, cell,
                                             padding_cell_renderer_func,
                                             sidebar, NULL);

    /* icon renderer */
    cell = gtk_cell_renderer_pixbuf_new ();
    sidebar->icon_cell_renderer = cell;
    gtk_tree_view_column_pack_start (col, cell, FALSE);
    gtk_tree_view_column_set_attributes (col, cell,
                                         "pixbuf", PLACES_SIDEBAR_COLUMN_ICON,
                                         NULL);
    gtk_tree_view_column_set_cell_data_func (col, cell,
                                             icon_cell_renderer_func,
                                             sidebar, NULL);

    /* eject text renderer */
    cell = gtk_cell_renderer_text_new ();
    sidebar->eject_text_cell_renderer = cell;
    gtk_tree_view_column_pack_start (col, cell, TRUE);
    gtk_tree_view_column_set_attributes (col, cell,
                                         "text", PLACES_SIDEBAR_COLUMN_NAME,
                                         "visible", PLACES_SIDEBAR_COLUMN_EJECT,
                                         NULL);
    g_object_set (cell,
                  "ellipsize", PANGO_ELLIPSIZE_END,
                  "ellipsize-set", TRUE,
                  NULL);

    /* eject icon renderer */
    cell = gtk_cell_renderer_pixbuf_new ();
    g_object_set (cell,
                  "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                  "stock-size", GTK_ICON_SIZE_MENU,
                  "xpad", EJECT_BUTTON_XPAD,
                  NULL);
    gtk_tree_view_column_pack_start (col, cell, FALSE);
    gtk_tree_view_column_set_attributes (col, cell,
                                         "visible", PLACES_SIDEBAR_COLUMN_EJECT,
                                         "pixbuf", PLACES_SIDEBAR_COLUMN_EJECT_ICON,
                                         NULL);

    /* normal text renderer */
    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, cell, TRUE);
    g_object_set (G_OBJECT (cell), "editable", FALSE, NULL);
    gtk_tree_view_column_set_attributes (col, cell,
                                         "text", PLACES_SIDEBAR_COLUMN_NAME,
                                         "visible", PLACES_SIDEBAR_COLUMN_NO_EJECT,
                                         "editable-set", PLACES_SIDEBAR_COLUMN_BOOKMARK,
                                         NULL);
    g_object_set (cell,
                  "ellipsize", PANGO_ELLIPSIZE_END,
                  "ellipsize-set", TRUE,
                  NULL);

    g_signal_connect (cell, "edited",
                      G_CALLBACK (bookmarks_edited), sidebar);
    g_signal_connect (cell, "editing-canceled",
                      G_CALLBACK (bookmarks_editing_canceled), sidebar);

    /* this is required to align the eject buttons to the right */
    gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (col), PEONY_ICON_SIZE_SMALLER);
    gtk_tree_view_append_column (tree_view, col);

    sidebar->store = gtk_list_store_new (PLACES_SIDEBAR_COLUMN_COUNT,
                                         G_TYPE_INT,
                                         G_TYPE_STRING,
                                         G_TYPE_DRIVE,
                                         G_TYPE_VOLUME,
                                         G_TYPE_MOUNT,
                                         G_TYPE_STRING,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_INT,
                                         G_TYPE_BOOLEAN,
                                         G_TYPE_BOOLEAN,
                                         G_TYPE_BOOLEAN,
                                         G_TYPE_STRING,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_INT,
                                         G_TYPE_STRING);

    gtk_tree_view_set_tooltip_column (tree_view, PLACES_SIDEBAR_COLUMN_TOOLTIP);

    sidebar->filter_model = peony_shortcuts_model_filter_new (sidebar,
                            GTK_TREE_MODEL (sidebar->store),
                            NULL);

    gtk_tree_view_set_model (tree_view, sidebar->filter_model);
    gtk_container_add (GTK_CONTAINER (sidebar), GTK_WIDGET (tree_view));
    gtk_widget_show (GTK_WIDGET (tree_view));

    gtk_widget_show (GTK_WIDGET (sidebar));
    sidebar->tree_view = tree_view;

    gtk_tree_view_set_search_column (tree_view, PLACES_SIDEBAR_COLUMN_NAME);
    selection = gtk_tree_view_get_selection (tree_view);
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

    gtk_tree_selection_set_select_function (selection,
                                            tree_selection_func,
                                            sidebar,
                                            NULL);

    gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
                                            GDK_BUTTON1_MASK,
                                            peony_shortcuts_source_targets,
                                            G_N_ELEMENTS (peony_shortcuts_source_targets),
                                            GDK_ACTION_MOVE);
    gtk_drag_dest_set (GTK_WIDGET (tree_view),
                       0,
                       peony_shortcuts_drop_targets, G_N_ELEMENTS (peony_shortcuts_drop_targets),
                       GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);

    g_signal_connect (tree_view, "key-press-event",
                      G_CALLBACK (bookmarks_key_press_event_cb), sidebar);

    g_signal_connect (tree_view, "drag-motion",
                      G_CALLBACK (drag_motion_callback), sidebar);
    g_signal_connect (tree_view, "drag-leave",
                      G_CALLBACK (drag_leave_callback), sidebar);
    g_signal_connect (tree_view, "drag_data_received",
                      G_CALLBACK (drag_data_received_callback), sidebar);
    g_signal_connect (tree_view, "drag-drop",
                      G_CALLBACK (drag_drop_callback), sidebar);

    g_signal_connect (selection, "changed",
                      G_CALLBACK (bookmarks_selection_changed_cb), sidebar);
    g_signal_connect (tree_view, "popup-menu",
                      G_CALLBACK (bookmarks_popup_menu_cb), sidebar);
    g_signal_connect (tree_view, "button-press-event",
                      G_CALLBACK (bookmarks_button_press_event_cb), sidebar);
    g_signal_connect (tree_view, "motion-notify-event",
                      G_CALLBACK (bookmarks_motion_event_cb), sidebar);
    g_signal_connect (tree_view, "button-release-event",
                      G_CALLBACK (bookmarks_button_release_event_cb), sidebar);

    eel_gtk_tree_view_set_activate_on_single_click (sidebar->tree_view,
            TRUE);

    g_signal_connect_swapped (peony_preferences, "changed::" PEONY_PREFERENCES_DESKTOP_IS_HOME_DIR,
                              G_CALLBACK(desktop_location_changed_callback),
                              sidebar);

    g_signal_connect_object (peony_trash_monitor_get (),
                             "trash_state_changed",
                             G_CALLBACK (trash_state_changed_cb),
                             sidebar, 0);
}

static void
peony_places_sidebar_dispose (GObject *object)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = PEONY_PLACES_SIDEBAR (object);

    sidebar->window = NULL;
    sidebar->tree_view = NULL;

    g_free (sidebar->uri);
    sidebar->uri = NULL;

    free_drag_data (sidebar);

    if (sidebar->eject_highlight_path != NULL) {
        gtk_tree_path_free (sidebar->eject_highlight_path);
        sidebar->eject_highlight_path = NULL;
    }

    g_clear_object (&sidebar->store);
    g_clear_object (&sidebar->volume_monitor);
    g_clear_object (&sidebar->bookmarks);
    g_clear_object (&sidebar->filter_model);

    eel_remove_weak_pointer (&(sidebar->go_to_after_mount_slot));

    g_signal_handlers_disconnect_by_func (peony_preferences,
                                          desktop_location_changed_callback,
                                          sidebar);

    G_OBJECT_CLASS (peony_places_sidebar_parent_class)->dispose (object);
}

static void
peony_places_sidebar_class_init (PeonyPlacesSidebarClass *class)
{
    G_OBJECT_CLASS (class)->dispose = peony_places_sidebar_dispose;

    GTK_WIDGET_CLASS (class)->style_updated = peony_places_sidebar_style_updated;
}

static const char *
peony_places_sidebar_get_sidebar_id (PeonySidebar *sidebar)
{
    return PEONY_PLACES_SIDEBAR_ID;
}

static char *
peony_places_sidebar_get_tab_label (PeonySidebar *sidebar)
{
    return g_strdup (_("Places"));
}

static char *
peony_places_sidebar_get_tab_tooltip (PeonySidebar *sidebar)
{
    return g_strdup (_("Show Places"));
}

static GdkPixbuf *
peony_places_sidebar_get_tab_icon (PeonySidebar *sidebar)
{
    return NULL;
}

static void
peony_places_sidebar_is_visible_changed (PeonySidebar *sidebar,
                                        gboolean         is_visible)
{
    /* Do nothing */
}

static void
peony_places_sidebar_iface_init (PeonySidebarIface *iface)
{
    iface->get_sidebar_id = peony_places_sidebar_get_sidebar_id;
    iface->get_tab_label = peony_places_sidebar_get_tab_label;
    iface->get_tab_tooltip = peony_places_sidebar_get_tab_tooltip;
    iface->get_tab_icon = peony_places_sidebar_get_tab_icon;
    iface->is_visible_changed = peony_places_sidebar_is_visible_changed;
}

static void
peony_places_sidebar_set_parent_window (PeonyPlacesSidebar *sidebar,
                                       PeonyWindowInfo *window)
{
    PeonyWindowSlotInfo *slot;

    sidebar->window = window;

    slot = peony_window_info_get_active_slot (window);

    sidebar->bookmarks = peony_bookmark_list_new ();
    sidebar->uri = peony_window_slot_info_get_current_location (slot);

    g_signal_connect_object (sidebar->bookmarks, "contents_changed",
                             G_CALLBACK (update_places),
                             sidebar, G_CONNECT_SWAPPED);

    g_signal_connect_object (window, "loading_uri",
                             G_CALLBACK (loading_uri_callback),
                             sidebar, 0);

    g_signal_connect_object (sidebar->volume_monitor, "volume_added",
                             G_CALLBACK (volume_added_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "volume_removed",
                             G_CALLBACK (volume_removed_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "volume_changed",
                             G_CALLBACK (volume_changed_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "mount_added",
                             G_CALLBACK (mount_added_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "mount_removed",
                             G_CALLBACK (mount_removed_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "mount_changed",
                             G_CALLBACK (mount_changed_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "drive_disconnected",
                             G_CALLBACK (drive_disconnected_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "drive_connected",
                             G_CALLBACK (drive_connected_callback), sidebar, 0);
    g_signal_connect_object (sidebar->volume_monitor, "drive_changed",
                             G_CALLBACK (drive_changed_callback), sidebar, 0);

    update_places (sidebar);
}

static void
peony_places_sidebar_style_updated (GtkWidget *widget)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = PEONY_PLACES_SIDEBAR (widget);

    update_places (sidebar);
}

static PeonySidebar *
peony_places_sidebar_create (PeonySidebarProvider *provider,
                            PeonyWindowInfo *window)
{
    PeonyPlacesSidebar *sidebar;

    sidebar = g_object_new (peony_places_sidebar_get_type (), NULL);
    peony_places_sidebar_set_parent_window (sidebar, window);
    g_object_ref_sink (sidebar);

    return PEONY_SIDEBAR (sidebar);
}

static void
sidebar_provider_iface_init (PeonySidebarProviderIface *iface)
{
    iface->create = peony_places_sidebar_create;
}

static void
peony_places_sidebar_provider_init (PeonyPlacesSidebarProvider *sidebar)
{
}

static void
peony_places_sidebar_provider_class_init (PeonyPlacesSidebarProviderClass *class)
{
}

void
peony_places_sidebar_register (void)
{
    peony_module_add_type (peony_places_sidebar_provider_get_type ());
}

/* Drag and drop interfaces */

static void
_peony_shortcuts_model_filter_class_init (PeonyShortcutsModelFilterClass *class)
{
}

static void
_peony_shortcuts_model_filter_init (PeonyShortcutsModelFilter *model)
{
    model->sidebar = NULL;
}

/* GtkTreeDragSource::row_draggable implementation for the shortcuts filter model */
static gboolean
peony_shortcuts_model_filter_row_draggable (GtkTreeDragSource *drag_source,
                                           GtkTreePath       *path)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    PlaceType place_type;
    SectionType section_type;

    model = GTK_TREE_MODEL (drag_source);

    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model, &iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                        -1);

    if (place_type != PLACES_HEADING && section_type == SECTION_BOOKMARKS)
        return TRUE;

    return FALSE;
}

/* Fill the GtkTreeDragSourceIface vtable */
static void
peony_shortcuts_model_filter_drag_source_iface_init (GtkTreeDragSourceIface *iface)
{
    iface->row_draggable = peony_shortcuts_model_filter_row_draggable;
}

static GtkTreeModel *
peony_shortcuts_model_filter_new (PeonyPlacesSidebar *sidebar,
                                 GtkTreeModel          *child_model,
                                 GtkTreePath           *root)
{
    PeonyShortcutsModelFilter *model;

    model = g_object_new (PEONY_SHORTCUTS_MODEL_FILTER_TYPE,
                          "child-model", child_model,
                          "virtual-root", root,
                          NULL);

    model->sidebar = sidebar;

    return GTK_TREE_MODEL (model);
}
