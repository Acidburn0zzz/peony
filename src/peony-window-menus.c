/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Peony
 *
 * Copyright (C) 2000, 2001 Eazel, Inc.
 *
 * Peony is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Peony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Author: John Sullivan <sullivan@eazel.com>
 */

/* peony-window-menus.h - implementation of peony window menu operations,
 *                           split into separate file just for convenience.
 */
#include <config.h>

#include <locale.h>

#include "peony-actions.h"
#include "peony-application.h"
#include "peony-connect-server-dialog.h"
#include "peony-file-management-properties.h"
#include "peony-property-browser.h"
#include "peony-window-manage-views.h"
#include "peony-window-bookmarks.h"
#include "peony-window-private.h"
#include "peony-desktop-window.h"
#include "peony-search-bar.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <eel/eel-gtk-extensions.h>
#include <libpeony-extension/peony-menu-provider.h>
#include <libpeony-private/peony-extensions.h>
#include <libpeony-private/peony-file-utilities.h>
#include <libpeony-private/peony-global-preferences.h>
#include <libpeony-private/peony-icon-names.h>
#include <libpeony-private/peony-ui-utilities.h>
#include <libpeony-private/peony-module.h>
#include <libpeony-private/peony-search-directory.h>
#include <libpeony-private/peony-search-engine.h>
#include <libpeony-private/peony-signaller.h>
#include <libpeony-private/peony-trash-monitor.h>
#include <string.h>
#include "file-manager/fm-directory-view.h"
#include <libpeony-private/peony-undostack-manager.h>

#define MENU_PATH_EXTENSION_ACTIONS                     "/MenuBar/File/Extension Actions"
#define POPUP_PATH_EXTENSION_ACTIONS                     "/background/Before Zoom Items/Extension Actions"

#define NETWORK_URI          "network:"
#define COMPUTER_URI         "computer:"


struct FMDirectoryViewDetails
{
	PeonyWindowInfo *window;
	PeonyWindowSlotInfo *slot;
	PeonyDirectory *model;
	PeonyFile *directory_as_file;
	PeonyFile *location_popup_directory_as_file;
	GdkEventButton *location_popup_event;
	GtkActionGroup *dir_action_group;
	guint dir_merge_id;

	GList *scripts_directory_list;
	GtkActionGroup *scripts_action_group;
	guint scripts_merge_id;

	GList *templates_directory_list;
	GtkActionGroup *templates_action_group;
	guint templates_merge_id;

	GtkActionGroup *extensions_menu_action_group;
	guint extensions_menu_merge_id;

	guint display_selection_idle_id;
	guint update_menus_timeout_id;
	guint update_status_idle_id;
	guint reveal_selection_idle_id;

	guint display_pending_source_id;
	guint changes_timeout_id;

	guint update_interval;
 	guint64 last_queued;

	guint files_added_handler_id;
	guint files_changed_handler_id;
	guint load_error_handler_id;
	guint done_loading_handler_id;
	guint file_changed_handler_id;

	guint delayed_rename_file_id;

	GList *new_added_files;
	GList *new_changed_files;

	GHashTable *non_ready_files;

	GList *old_added_files;
	GList *old_changed_files;

	GList *pending_locations_selected;

	/* whether we are in the active slot */
	gboolean active;

	/* loading indicates whether this view has begun loading a directory.
	 * This flag should need not be set inside subclasses. FMDirectoryView automatically
	 * sets 'loading' to TRUE before it begins loading a directory's contents and to FALSE
	 * after it finishes loading the directory and its view.
	 */
	gboolean loading;
	gboolean menu_states_untrustworthy;
	gboolean scripts_invalid;
	gboolean templates_invalid;
	gboolean reported_load_error;

	/* flag to indicate that no file updates should be dispatched to subclasses.
	 * This is a workaround for bug #87701 that prevents the list view from
	 * losing focus when the underlying GtkTreeView is updated.
	 */
	gboolean updates_frozen;
	guint	 updates_queued;
	gboolean needs_reload;

	gboolean sort_directories_first;

	gboolean show_foreign_files;
	gboolean show_hidden_files;
	gboolean ignore_hidden_file_preferences;

	gboolean batching_selection_level;
	gboolean selection_changed_while_batched;

	gboolean selection_was_removed;

	gboolean metadata_for_directory_as_file_pending;
	gboolean metadata_for_files_in_directory_pending;

	gboolean selection_change_is_due_to_shell;
	gboolean send_selection_change_to_shell;

	GtkActionGroup *open_with_action_group;
	guint open_with_merge_id;

	GList *subdirectory_list;

	gboolean allow_moves;

	GdkPoint context_menu_position;

	gboolean undo_active;
	gboolean redo_active;
	gchar* undo_action_description;
	gchar* undo_action_label;
	gchar* redo_action_description;
	gchar* redo_action_label;
};

typedef struct {
	PeonyFile *file;
	PeonyDirectory *directory;
} FileAndDirectory;

/* Struct that stores all the info necessary to activate a bookmark. */
typedef struct
{
    PeonyBookmark *bookmark;
    PeonyWindow *window;
    guint changed_handler_id;
    PeonyBookmarkFailedCallback failed_callback;
} BookmarkHolder;

static void
finish_undoredo_callback (gpointer data)
{
}


static BookmarkHolder *
bookmark_holder_new (PeonyBookmark *bookmark,
                     PeonyWindow *window,
                     GCallback refresh_callback,
                     PeonyBookmarkFailedCallback failed_callback)
{
    BookmarkHolder *new_bookmark_holder;

    new_bookmark_holder = g_new (BookmarkHolder, 1);
    new_bookmark_holder->window = window;
    new_bookmark_holder->bookmark = bookmark;
    new_bookmark_holder->failed_callback = failed_callback;
    /* Ref the bookmark because it might be unreffed away while
     * we're holding onto it (not an issue for window).
     */
    g_object_ref (bookmark);
    new_bookmark_holder->changed_handler_id =
        g_signal_connect_object (bookmark, "appearance_changed",
                                 refresh_callback,
                                 window, G_CONNECT_SWAPPED);

    return new_bookmark_holder;
}

static void
bookmark_holder_free (BookmarkHolder *bookmark_holder)
{
    if (g_signal_handler_is_connected(bookmark_holder->bookmark,
                                      bookmark_holder->changed_handler_id)){
    g_signal_handler_disconnect (bookmark_holder->bookmark,
                                      bookmark_holder->changed_handler_id);
    }
    g_object_unref (bookmark_holder->bookmark);
    g_free (bookmark_holder);
}

static void
bookmark_holder_free_cover (gpointer callback_data, GClosure *closure)
{
    bookmark_holder_free (callback_data);
}

static gboolean
should_open_in_new_tab (void)
{
    /* FIXME this is duplicated */
    GdkEvent *event;

    event = gtk_get_current_event ();

    if (event == NULL)
    {
        return FALSE;
    }

    if (event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE)
    {
        return event->button.button == 2;
    }

    gdk_event_free (event);

    return FALSE;
}

static void
activate_bookmark_in_menu_item (GtkAction *action, gpointer user_data)
{
    PeonyWindowSlot *slot;
    BookmarkHolder *holder;
    GFile *location;

    holder = (BookmarkHolder *)user_data;

    if (peony_bookmark_uri_known_not_to_exist (holder->bookmark))
    {
        holder->failed_callback (holder->window, holder->bookmark);
    }
    else
    {
        location = peony_bookmark_get_location (holder->bookmark);
        slot = peony_window_get_active_slot (holder->window);
        peony_window_slot_go_to (slot,
                                location,
                                should_open_in_new_tab ());
        g_object_unref (location);
    }
}

void
peony_menus_append_bookmark_to_menu (PeonyWindow *window,
                                    PeonyBookmark *bookmark,
                                    const char *parent_path,
                                    const char *parent_id,
                                    guint index_in_parent,
                                    GtkActionGroup *action_group,
                                    guint merge_id,
                                    GCallback refresh_callback,
                                    PeonyBookmarkFailedCallback failed_callback)
{
    BookmarkHolder *bookmark_holder;
    char action_name[128];
    char *name;
    char *path;
    GdkPixbuf *pixbuf;
    GtkAction *action;
    GtkWidget *menuitem;

    g_assert (PEONY_IS_WINDOW (window));
    g_assert (PEONY_IS_BOOKMARK (bookmark));

    bookmark_holder = bookmark_holder_new (bookmark, window, refresh_callback, failed_callback);
    name = peony_bookmark_get_name (bookmark);

    /* Create menu item with pixbuf */
    pixbuf = peony_bookmark_get_pixbuf (bookmark, GTK_ICON_SIZE_MENU);

    g_snprintf (action_name, sizeof (action_name), "%s%d", parent_id, index_in_parent);

    action = gtk_action_new (action_name,
                             name,
                             _("Go to the location specified by this bookmark"),
                             NULL);

    g_object_set_data_full (G_OBJECT (action), "menu-icon",
                            g_object_ref (pixbuf),
                            g_object_unref);

    g_signal_connect_data (action, "activate",
                           G_CALLBACK (activate_bookmark_in_menu_item),
                           bookmark_holder,
                           bookmark_holder_free_cover, 0);

    gtk_action_group_add_action (action_group,
                                 GTK_ACTION (action));

    g_object_unref (action);

    gtk_ui_manager_add_ui (window->details->ui_manager,
                           merge_id,
                           parent_path,
                           action_name,
                           action_name,
                           GTK_UI_MANAGER_MENUITEM,
                           FALSE);

    path = g_strdup_printf ("%s/%s", parent_path, action_name);
    menuitem = gtk_ui_manager_get_widget (window->details->ui_manager,
                                          path);
    if(menuitem!=NULL)
        gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menuitem),
                TRUE);

    g_object_unref (pixbuf);
    g_free (path);
    g_free (name);
}

static void
action_close_window_slot_callback (GtkAction *action,
                                   gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    peony_window_slot_close (slot);
}

static void
action_connect_to_server_callback (GtkAction *action,
                                   gpointer user_data)
{
    PeonyWindow *window = PEONY_WINDOW (user_data);
    GtkWidget *dialog;

    dialog = peony_connect_server_dialog_new (window);

    gtk_widget_show (dialog);
}

static void
action_stop_callback (GtkAction *action,
                      gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    peony_window_slot_stop_loading (slot);
}

static void
action_home_callback (GtkAction *action,
                      gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    peony_window_slot_go_home (slot,
                              should_open_in_new_tab ());
}

static void
action_go_to_computer_callback (GtkAction *action,
                                gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;
    GFile *computer;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    computer = g_file_new_for_uri (COMPUTER_URI);
    peony_window_slot_go_to (slot,
                            computer,
                            should_open_in_new_tab ());
    g_object_unref (computer);
}

static void
action_go_to_network_callback (GtkAction *action,
                               gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;
    GFile *network;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    network = g_file_new_for_uri (NETWORK_URI);
    peony_window_slot_go_to (slot,
                            network,
                            should_open_in_new_tab ());
    g_object_unref (network);
}

static void
action_go_to_templates_callback (GtkAction *action,
                                 gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;
    char *path;
    GFile *location;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    path = peony_get_templates_directory ();
    location = g_file_new_for_path (path);
    g_free (path);
    peony_window_slot_go_to (slot,
                            location,
                            should_open_in_new_tab ());
    g_object_unref (location);
}

static void
action_go_to_trash_callback (GtkAction *action,
                             gpointer user_data)
{
    PeonyWindow *window;
    PeonyWindowSlot *slot;
    GFile *trash;

    window = PEONY_WINDOW (user_data);
    slot = peony_window_get_active_slot (window);

    trash = g_file_new_for_uri ("trash:///");
    peony_window_slot_go_to (slot,
                            trash,
                            should_open_in_new_tab ());
    g_object_unref (trash);
}

static void
action_reload_callback (GtkAction *action,
                        gpointer user_data)
{
    peony_window_reload (PEONY_WINDOW (user_data));
}

static void
action_zoom_in_callback (GtkAction *action,
                         gpointer user_data)
{
    peony_window_zoom_in (PEONY_WINDOW (user_data));
}

static void
real_action_undo (FMDirectoryView *view)
{
        PeonyUndoStackManager *manager = peony_undostack_manager_instance ();

        /* Disable menus because they are in an untrustworthy status */
        view->details->undo_active = FALSE;
        view->details->redo_active = FALSE;
        fm_directory_view_update_menus (view);

        peony_undostack_manager_undo (manager, GTK_WIDGET (view), finish_undoredo_callback);
}


static void
action_undo_callback (GtkAction *action,
                        gpointer callback_data)
{
        real_action_undo (FM_DIRECTORY_VIEW (callback_data));
}


static void
action_zoom_out_callback (GtkAction *action,
                          gpointer user_data)
{
    peony_window_zoom_out (PEONY_WINDOW (user_data));
}

static void
action_zoom_normal_callback (GtkAction *action,
                             gpointer user_data)
{
    peony_window_zoom_to_default (PEONY_WINDOW (user_data));
}

static void
action_show_hidden_files_callback (GtkAction *action,
                                   gpointer callback_data)
{
    PeonyWindow *window;
    PeonyWindowShowHiddenFilesMode mode;

    window = PEONY_WINDOW (callback_data);

    if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
    {
        mode = PEONY_WINDOW_SHOW_HIDDEN_FILES_ENABLE;
    }
    else
    {
        mode = PEONY_WINDOW_SHOW_HIDDEN_FILES_DISABLE;
    }

    peony_window_info_set_hidden_files_mode (window, mode);
}

static void
show_hidden_files_preference_callback (gpointer callback_data)
{
    PeonyWindow *window;
    GtkAction *action;

    window = PEONY_WINDOW (callback_data);

    if (window->details->show_hidden_files_mode == PEONY_WINDOW_SHOW_HIDDEN_FILES_DEFAULT)
    {
        action = gtk_action_group_get_action (window->details->main_action_group, PEONY_ACTION_SHOW_HIDDEN_FILES);
        g_assert (GTK_IS_ACTION (action));

        /* update button */
        g_signal_handlers_block_by_func (action, action_show_hidden_files_callback, window);
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
                                      g_settings_get_boolean (peony_preferences, PEONY_PREFERENCES_SHOW_HIDDEN_FILES));
        g_signal_handlers_unblock_by_func (action, action_show_hidden_files_callback, window);

        /* inform views */
        peony_window_info_set_hidden_files_mode (window, PEONY_WINDOW_SHOW_HIDDEN_FILES_DEFAULT);

    }
}

static void
preferences_respond_callback (GtkDialog *dialog,
                              gint response_id)
{
    if (response_id == GTK_RESPONSE_CLOSE)
    {
        gtk_widget_destroy (GTK_WIDGET (dialog));
    }
}

static void
action_preferences_callback (GtkAction *action,
                             gpointer user_data)
{
    GtkWindow *window;

    window = GTK_WINDOW (user_data);

    peony_file_management_properties_dialog_show (G_CALLBACK (preferences_respond_callback), window);
}

static void
action_backgrounds_and_emblems_callback (GtkAction *action,
        gpointer user_data)
{
    GtkWindow *window;

    window = GTK_WINDOW (user_data);

    peony_property_browser_show (gtk_window_get_screen (window));
}

static void
action_about_peony_callback (GtkAction *action,
                            gpointer user_data)
{
    const gchar *authors[] =
    {
        "Alexander Larsson",
        "Ali Abdin",
        "Anders Carlsson",
        "Andy Hertzfeld",
        "Arlo Rose",
        "Darin Adler",
        "David Camp",
        "Eli Goldberg",
        "Elliot Lee",
        "Eskil Heyn Olsen",
        "Ettore Perazzoli",
        "Gene Z. Ragan",
        "George Lebl",
        "Ian McKellar",
        "J Shane Culpepper",
        "James Willcox",
        "Jan Arne Petersen",
        "John Harper",
        "John Sullivan",
        "Josh Barrow",
        "Maciej Stachowiak",
        "Mark McLoughlin",
        "Mathieu Lacage",
        "Mike Engber",
        "Mike Fleming",
        "Pavel Cisler",
        "Ramiro Estrugo",
        "Raph Levien",
        "Rebecca Schulman",
        "Robey Pointer",
        "Robin * Slomkowski",
        "Seth Nickell",
        "Susan Kare",
        "Perberos",
        "Steve Zesch",
        "Stefano Karapetsas",
        "Jasmine Hassan",
        NULL
    };
    const gchar *documenters[] =
    {
        "GNOME Documentation Team",
        "Sun Microsystem",
        NULL
    };
    const gchar *license[] =
    {
        N_("Peony is free software; you can redistribute it and/or modify "
        "it under the terms of the GNU General Public License as published by "
        "the Free Software Foundation; either version 2 of the License, or "
        "(at your option) any later version."),
        N_("Peony is distributed in the hope that it will be useful, "
        "but WITHOUT ANY WARRANTY; without even the implied warranty of "
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
        "GNU General Public License for more details."),
        N_("You should have received a copy of the GNU General Public License "
        "along with Peony; if not, write to the Free Software Foundation, Inc., "
        "51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA")
    };
    gchar *license_trans;

    license_trans = g_strjoin ("\n\n", _(license[0]), _(license[1]),
                               _(license[2]), NULL);

    gtk_show_about_dialog (GTK_WINDOW (user_data),
                           "program-name", _("Peony"),
                           "version", VERSION,
                           "comments", _("Peony lets you organize "
                                         "files and folders, both on "
                                         "your computer and online."),
                           "copyright", _("Copyright \xC2\xA9 1999-2009 The Nautilus authors\n"
                                          "Copyright \xC2\xA9 2011-2016 The Caja authors\n"
					  "Copyright \xC2\xA9 2016-2017 The Peony authors"),
                           "license", license_trans,
                           "wrap-license", TRUE,
                           "authors", authors,
                           "documenters", documenters,
                           /* Translators should localize the following string
                            * which will be displayed at the bottom of the about
                            * box to give credit to the translator(s).
                            */
                           "translator-credits", _("translator-credits"),
                           "logo-icon-name", "system-file-manager",
                           "website", "http://www.ukui.org",
                           "website-label", _("UKUI Web Site"),
                           NULL);

    g_free (license_trans);

}

static void
action_up_callback (GtkAction *action,
                    gpointer user_data)
{
    peony_window_go_up (PEONY_WINDOW (user_data), FALSE, should_open_in_new_tab ());
}

static void
action_peony_manual_callback (GtkAction *action,
                             gpointer user_data)
{
    PeonyWindow *window;
    GError *error;
    GtkWidget *dialog;

    error = NULL;
    window = PEONY_WINDOW (user_data);

    gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (window)),
                   PEONY_IS_DESKTOP_WINDOW (window)
                      ? "help:ubuntu-kylin-help"
                      : "help:ubuntu-kylin-help/files",
                  gtk_get_current_event_time (), &error);

    if (error)
    {
        dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                         GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK,
                                         _("There was an error displaying help: \n%s"),
                                         error->message);
        g_signal_connect (G_OBJECT (dialog), "response",
                          G_CALLBACK (gtk_widget_destroy),
                          NULL);

        gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
        gtk_widget_show (dialog);
        g_error_free (error);
    }
}

static void
menu_item_select_cb (GtkMenuItem *proxy,
                     PeonyWindow *window)
{
    GtkAction *action;
    char *message;

    action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (proxy));
    g_return_if_fail (action != NULL);

    g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
    if (message)
    {
        gtk_statusbar_push (GTK_STATUSBAR (window->details->statusbar),
                            window->details->help_message_cid, message);
        g_free (message);
    }
}

static void
menu_item_deselect_cb (GtkMenuItem *proxy,
                       PeonyWindow *window)
{
    gtk_statusbar_pop (GTK_STATUSBAR (window->details->statusbar),
                       window->details->help_message_cid);
}

static GtkWidget *
get_event_widget (GtkWidget *proxy)
{
    GtkWidget *widget;

    /**
     * Finding the interesting widget requires internal knowledge of
     * the widgets in question. This can't be helped, but by keeping
     * the sneaky code in one place, it can easily be updated.
     */
    if (GTK_IS_MENU_ITEM (proxy))
    {
        /* Menu items already forward middle clicks */
        widget = NULL;
    }
    else if (GTK_IS_MENU_TOOL_BUTTON (proxy))
    {
        widget = eel_gtk_menu_tool_button_get_button (GTK_MENU_TOOL_BUTTON (proxy));
    }
    else if (GTK_IS_TOOL_BUTTON (proxy))
    {
        /* The tool button's button is the direct child */
        widget = gtk_bin_get_child (GTK_BIN (proxy));
    }
    else if (GTK_IS_BUTTON (proxy))
    {
        widget = proxy;
    }
    else
    {
        /* Don't touch anything we don't know about */
        widget = NULL;
    }

    return widget;
}

static gboolean
proxy_button_press_event_cb (GtkButton *button,
                             GdkEventButton *event,
                             gpointer user_data)
{
    if (event->button == 2)
    {
        g_signal_emit_by_name (button, "pressed", 0);
    }

    return FALSE;
}

static gboolean
proxy_button_release_event_cb (GtkButton *button,
                               GdkEventButton *event,
                               gpointer user_data)
{
    if (event->button == 2)
    {
        g_signal_emit_by_name (button, "released", 0);
    }

    return FALSE;
}

static void
disconnect_proxy_cb (GtkUIManager *manager,
                     GtkAction *action,
                     GtkWidget *proxy,
                     PeonyWindow *window)
{
    GtkWidget *widget;

    if (GTK_IS_MENU_ITEM (proxy))
    {
        g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK (menu_item_select_cb), window);
        g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK (menu_item_deselect_cb), window);
    }

    widget = get_event_widget (proxy);
    if (widget)
    {
        g_signal_handlers_disconnect_by_func (widget,
                                              G_CALLBACK (proxy_button_press_event_cb),
                                              action);
        g_signal_handlers_disconnect_by_func (widget,
                                              G_CALLBACK (proxy_button_release_event_cb),
                                              action);
    }

}

static void
connect_proxy_cb (GtkUIManager *manager,
                  GtkAction *action,
                  GtkWidget *proxy,
                  PeonyWindow *window)
{
    GdkPixbuf *icon;
    GtkWidget *widget;

    if (GTK_IS_MENU_ITEM (proxy))
    {
        g_signal_connect (proxy, "select",
                          G_CALLBACK (menu_item_select_cb), window);
        g_signal_connect (proxy, "deselect",
                          G_CALLBACK (menu_item_deselect_cb), window);


        /* This is a way to easily get pixbufs into the menu items */
        icon = g_object_get_data (G_OBJECT (action), "menu-icon");
        if (icon != NULL)
        {
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (proxy),
                                           gtk_image_new_from_pixbuf (icon));
        }
    }
    if (GTK_IS_TOOL_BUTTON (proxy))
    {
        icon = g_object_get_data (G_OBJECT (action), "toolbar-icon");
        if (icon != NULL)
        {
            widget = gtk_image_new_from_pixbuf (icon);
            gtk_widget_show (widget);
            gtk_tool_button_set_icon_widget (GTK_TOOL_BUTTON (proxy),
                                             widget);
        }
    }

    widget = get_event_widget (proxy);
    if (widget)
    {
        g_signal_connect (widget, "button-press-event",
                          G_CALLBACK (proxy_button_press_event_cb),
                          action);
        g_signal_connect (widget, "button-release-event",
                          G_CALLBACK (proxy_button_release_event_cb),
                          action);
    }
}

static void
trash_state_changed_cb (PeonyTrashMonitor *monitor,
                        gboolean state,
                        PeonyWindow *window)
{
    GtkActionGroup *action_group;
    GtkAction *action;
    GIcon *gicon;

    action_group = window->details->main_action_group;
    action = gtk_action_group_get_action (action_group, "Go to Trash");

    gicon = peony_trash_monitor_get_icon ();

    if (gicon)
    {
        g_object_set (action, "gicon", gicon, NULL);
        g_object_unref (gicon);
    }
}

static void
peony_window_initialize_trash_icon_monitor (PeonyWindow *window)
{
    PeonyTrashMonitor *monitor;

    monitor = peony_trash_monitor_get ();

    trash_state_changed_cb (monitor, TRUE, window);

    g_signal_connect (monitor, "trash_state_changed",
                      G_CALLBACK (trash_state_changed_cb), window);
}

static const GtkActionEntry main_entries[] =
{
    /* name, stock id, label */  { "File", NULL, N_("_File") },
    /* name, stock id, label */  { "Edit", NULL, N_("_Edit") },
    /* name, stock id, label */  { "View", NULL, N_("_View") },
    /* name, stock id, label */  { "Help", NULL, N_("_Help") },
    /* name, stock id */         { "Close", GTK_STOCK_CLOSE,
        /* label, accelerator */       N_("_Close"), "<control>W",
        /* tooltip */                  N_("Close this folder"),
        G_CALLBACK (action_close_window_slot_callback)
    },
    {
        "Backgrounds and Emblems", NULL,
        N_("_Backgrounds and Emblems..."),
        NULL, N_("Display patterns, colors, and emblems that can be used to customize appearance"),
        G_CALLBACK (action_backgrounds_and_emblems_callback)
    },
    {
        "Preferences", GTK_STOCK_PREFERENCES,
        N_("Prefere_nces"),
        NULL, N_("Edit Peony preferences"),
        G_CALLBACK (action_preferences_callback)
    },
    /* name, stock id, label */  { "Up", GTK_STOCK_GO_UP, N_("Open _Parent"),
        "<alt>Up", N_("Open the parent folder"),
        G_CALLBACK (action_up_callback)
    },
    /* name, stock id, label */  { "UpAccel", NULL, "UpAccel",
        "", NULL,
        G_CALLBACK (action_up_callback)
    },
    /* name, stock id */         { "Stop", GTK_STOCK_STOP,
        /* label, accelerator */       N_("_Stop"), NULL,
        /* tooltip */                  N_("Stop loading the current location"),
        G_CALLBACK (action_stop_callback)
    },
    /* name, stock id */         { "Reload", GTK_STOCK_REFRESH,
        /* label, accelerator */       N_("_Reload"), "<control>R",
        /* tooltip */                  N_("Reload the current location"),
        G_CALLBACK (action_reload_callback)
    },
    /* name, stock id */         { "Peony Manual", GTK_STOCK_HELP,
        /* label, accelerator */       N_("_Contents"), "F1",
        /* tooltip */                  N_("Display Peony help"),
        G_CALLBACK (action_peony_manual_callback)
    },
    /* name, stock id */         { "About Peony", GTK_STOCK_ABOUT,
        /* label, accelerator */       N_("_About"), NULL,
        /* tooltip */                  N_("Display credits for the creators of Peony"),
        G_CALLBACK (action_about_peony_callback)
    },
    /* name, stock id */         { "Zoom In", GTK_STOCK_ZOOM_IN,
        /* label, accelerator */       N_("Zoom _In"), "<control>plus",
        /* tooltip */                  N_("Increase the view size"),
        G_CALLBACK (action_zoom_in_callback)
    },
    /* name, stock id */         { "UndoFile", GTK_STOCK_UNDO,
        /* label, accelerator */       N_("_Undo"), "<control>Z",
        /* tooltip */                  N_("Undo the last action"),
        G_CALLBACK (action_undo_callback)
    },
    /* name, stock id */         { "ZoomInAccel", NULL,
        /* label, accelerator */       "ZoomInAccel", "<control>equal",
        /* tooltip */                  NULL,
        G_CALLBACK (action_zoom_in_callback)
    },
    /* name, stock id */         { "ZoomInAccel2", NULL,
        /* label, accelerator */       "ZoomInAccel2", "<control>KP_Add",
        /* tooltip */                  NULL,
        G_CALLBACK (action_zoom_in_callback)
    },
    /* name, stock id */         { "Zoom Out", GTK_STOCK_ZOOM_OUT,
        /* label, accelerator */       N_("Zoom _Out"), "<control>minus",
        /* tooltip */                  N_("Decrease the view size"),
        G_CALLBACK (action_zoom_out_callback)
    },
    /* name, stock id */         { "ZoomOutAccel", NULL,
        /* label, accelerator */       "ZoomOutAccel", "<control>KP_Subtract",
        /* tooltip */                  NULL,
        G_CALLBACK (action_zoom_out_callback)
    },
    /* name, stock id */         { "Zoom Normal", GTK_STOCK_ZOOM_100,
        /* label, accelerator */       N_("Normal Si_ze"), "<control>0",
        /* tooltip */                  N_("Use the normal view size"),
        G_CALLBACK (action_zoom_normal_callback)
    },
    /* name, stock id */         { "Connect to Server", NULL,
        /* label, accelerator */       N_("Connect to _Server..."), NULL,
        /* tooltip */                  N_("Connect to a remote computer or shared disk"),
        G_CALLBACK (action_connect_to_server_callback)
    },
    /* name, stock id */         { "Home", PEONY_ICON_HOME,
        /* label, accelerator */       N_("_Home Folder"), "<alt>Home",
        /* tooltip */                  N_("Open your personal folder"),
        G_CALLBACK (action_home_callback)
    },
    /* name, stock id */         { "Go to Computer", PEONY_ICON_COMPUTER,
        /* label, accelerator */       N_("_Computer"), NULL,
        /* tooltip */                  N_("Browse all local and remote disks and folders accessible from this computer"),
        G_CALLBACK (action_go_to_computer_callback)
    },
    /* name, stock id */         { "Go to Network", PEONY_ICON_NETWORK,
        /* label, accelerator */       N_("_Network"), NULL,
        /* tooltip */                  N_("Browse bookmarked and local network locations"),
        G_CALLBACK (action_go_to_network_callback)
    },
    /* name, stock id */         { "Go to Templates", PEONY_ICON_TEMPLATE,
        /* label, accelerator */       N_("T_emplates"), NULL,
        /* tooltip */                  N_("Open your personal templates folder"),
        G_CALLBACK (action_go_to_templates_callback)
    },
    /* name, stock id */         { "Go to Trash", PEONY_ICON_TRASH,
        /* label, accelerator */       N_("_Trash"), NULL,
        /* tooltip */                  N_("Open your personal trash folder"),
        G_CALLBACK (action_go_to_trash_callback)
    },
};

static const GtkToggleActionEntry main_toggle_entries[] =
{
    /* name, stock id */         { "Show Hidden Files", NULL,
        /* label, accelerator */       N_("Show _Hidden Files"), "<control>H",
        /* tooltip */                  N_("Toggle the display of hidden files in the current window"),
        G_CALLBACK (action_show_hidden_files_callback),
        TRUE
    },
};

/**
 * peony_window_initialize_menus
 *
 * Create and install the set of menus for this window.
 * @window: A recently-created PeonyWindow.
 */
void
peony_window_initialize_menus (PeonyWindow *window)
{
    GtkActionGroup *action_group;
    GtkUIManager *ui_manager;
    GtkAction *action;
    const char *ui;

    action_group = gtk_action_group_new ("ShellActions");
    gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
    window->details->main_action_group = action_group;
    gtk_action_group_add_actions (action_group,
                                  main_entries, G_N_ELEMENTS (main_entries),
                                  window);
    gtk_action_group_add_toggle_actions (action_group,
                                         main_toggle_entries, G_N_ELEMENTS (main_toggle_entries),
                                         window);

    action = gtk_action_group_get_action (action_group, PEONY_ACTION_UP);
    g_object_set (action, "short_label", _("_Up"), NULL);

    action = gtk_action_group_get_action (action_group, PEONY_ACTION_HOME);
    g_object_set (action, "short_label", _("_Home"), NULL);

    action = gtk_action_group_get_action (action_group, PEONY_ACTION_SHOW_HIDDEN_FILES);
    g_signal_handlers_block_by_func (action, action_show_hidden_files_callback, window);
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
                                  g_settings_get_boolean (peony_preferences, PEONY_PREFERENCES_SHOW_HIDDEN_FILES));
    g_signal_handlers_unblock_by_func (action, action_show_hidden_files_callback, window);


    g_signal_connect_swapped (peony_preferences, "changed::" PEONY_PREFERENCES_SHOW_HIDDEN_FILES,
                              G_CALLBACK(show_hidden_files_preference_callback),
                              window);

    window->details->ui_manager = gtk_ui_manager_new ();
    ui_manager = window->details->ui_manager;
    gtk_window_add_accel_group (GTK_WINDOW (window),
                                gtk_ui_manager_get_accel_group (ui_manager));

    g_signal_connect (ui_manager, "connect_proxy",
                      G_CALLBACK (connect_proxy_cb), window);
    g_signal_connect (ui_manager, "disconnect_proxy",
                      G_CALLBACK (disconnect_proxy_cb), window);

    gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
    g_object_unref (action_group); /* owned by ui manager */

    ui = peony_ui_string_get ("peony-shell-ui.xml");
    gtk_ui_manager_add_ui_from_string (ui_manager, ui, -1, NULL);

    peony_window_initialize_trash_icon_monitor (window);
}

void
peony_window_finalize_menus (PeonyWindow *window)
{
    PeonyTrashMonitor *monitor;

    monitor = peony_trash_monitor_get ();

    g_signal_handlers_disconnect_by_func (monitor,
                                          trash_state_changed_cb, window);

    g_signal_handlers_disconnect_by_func (peony_preferences,
                                          show_hidden_files_preference_callback, window);
}

static GList *
get_extension_menus (PeonyWindow *window)
{
    PeonyWindowSlot *slot;
    GList *providers;
    GList *items;
    GList *l;

    providers = peony_extensions_get_for_type (PEONY_TYPE_MENU_PROVIDER);
    items = NULL;

    slot = peony_window_get_active_slot (window);

    for (l = providers; l != NULL; l = l->next)
    {
        PeonyMenuProvider *provider;
        GList *file_items;

        provider = PEONY_MENU_PROVIDER (l->data);
        file_items = peony_menu_provider_get_background_items (provider,
                     GTK_WIDGET (window),
                     slot->viewed_file);
        items = g_list_concat (items, file_items);
    }

    peony_module_extension_list_free (providers);

    return items;
}

static void
add_extension_menu_items (PeonyWindow *window,
                          guint merge_id,
                          GtkActionGroup *action_group,
                          GList *menu_items,
                          const char *subdirectory)
{
    GtkUIManager *ui_manager;
    GList *l;

    ui_manager = window->details->ui_manager;

    for (l = menu_items; l; l = l->next)
    {
        PeonyMenuItem *item;
        PeonyMenu *menu;
        GtkAction *action;
        char *path;

        item = PEONY_MENU_ITEM (l->data);

        g_object_get (item, "menu", &menu, NULL);

        action = peony_action_from_menu_item (item);
        gtk_action_group_add_action_with_accel (action_group, action, NULL);

        path = g_build_path ("/", POPUP_PATH_EXTENSION_ACTIONS, subdirectory, NULL);
        gtk_ui_manager_add_ui (ui_manager,
                               merge_id,
                               path,
                               gtk_action_get_name (action),
                               gtk_action_get_name (action),
                               (menu != NULL) ? GTK_UI_MANAGER_MENU : GTK_UI_MANAGER_MENUITEM,
                               FALSE);
        g_free (path);

        path = g_build_path ("/", MENU_PATH_EXTENSION_ACTIONS, subdirectory, NULL);
        gtk_ui_manager_add_ui (ui_manager,
                               merge_id,
                               path,
                               gtk_action_get_name (action),
                               gtk_action_get_name (action),
                               (menu != NULL) ? GTK_UI_MANAGER_MENU : GTK_UI_MANAGER_MENUITEM,
                               FALSE);
        g_free (path);

        /* recursively fill the menu */
        if (menu != NULL)
        {
            char *subdir;
            GList *children;

            children = peony_menu_get_items (menu);

            subdir = g_build_path ("/", subdirectory, "/", gtk_action_get_name (action), NULL);
            add_extension_menu_items (window,
                                      merge_id,
                                      action_group,
                                      children,
                                      subdir);

            peony_menu_item_list_free (children);
            g_free (subdir);
        }
    }
}

void
peony_window_load_extension_menus (PeonyWindow *window)
{
    GtkActionGroup *action_group;
    GList *items;
    guint merge_id;

    if (window->details->extensions_menu_merge_id != 0)
    {
        gtk_ui_manager_remove_ui (window->details->ui_manager,
                                  window->details->extensions_menu_merge_id);
        window->details->extensions_menu_merge_id = 0;
    }

    if (window->details->extensions_menu_action_group != NULL)
    {
        gtk_ui_manager_remove_action_group (window->details->ui_manager,
                                            window->details->extensions_menu_action_group);
        window->details->extensions_menu_action_group = NULL;
    }

    merge_id = gtk_ui_manager_new_merge_id (window->details->ui_manager);
    window->details->extensions_menu_merge_id = merge_id;
    action_group = gtk_action_group_new ("ExtensionsMenuGroup");
    window->details->extensions_menu_action_group = action_group;
    gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
    gtk_ui_manager_insert_action_group (window->details->ui_manager, action_group, 0);
    g_object_unref (action_group); /* owned by ui manager */

    items = get_extension_menus (window);

    if (items != NULL)
    {
        add_extension_menu_items (window, merge_id, action_group, items, "");

        g_list_foreach (items, (GFunc) g_object_unref, NULL);
        g_list_free (items);
    }
}

