/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fm-directory-view.h
 *
 * Copyright (C) 1999, 2000  Free Software Foundaton
 * Copyright (C) 2000  Eazel, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Rebecca Schulman <rebecka@eazel.com>
 */

#ifndef PEONY_CLIPBOARD_H
#define PEONY_CLIPBOARD_H

#include <gtk/gtk.h>

/* This makes this editable or text view put clipboard commands into
 * the passed UI manager when the editable/text view is in focus.
 * Callers in Peony normally get the UI manager from
 * peony_window_get_ui_manager. */
/* The shares selection changes argument should be set to true if the
 * widget uses the signal "selection_changed" to tell others about
 * text selection changes.  The PeonyEntry widget
 * is currently the only editable in peony that shares selection
 * changes. */
void peony_clipboard_set_up_editable            (GtkEditable        *target,
        GtkUIManager       *ui_manager,
        gboolean            shares_selection_changes);
void peony_clipboard_set_up_text_view           (GtkTextView        *target,
        GtkUIManager       *ui_manager);
void peony_clipboard_clear_if_colliding_uris    (GtkWidget          *widget,
        const GList        *item_uris,
        GdkAtom             copied_files_atom);
GtkClipboard* peony_clipboard_get                (GtkWidget          *widget);
GList* peony_clipboard_get_uri_list_from_selection_data
(GtkSelectionData   *selection_data,
 gboolean           *cut,
 GdkAtom             copied_files_atom);

#endif /* PEONY_CLIPBOARD_H */