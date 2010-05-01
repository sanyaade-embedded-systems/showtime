/*
 *  Showtime GTK frontend
 *  Copyright (C) 2009 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GU_H__
#define GU_H__

#include <gtk/gtk.h>

#include <prop.h>
#include <ui/ui.h>

LIST_HEAD(gu_nav_page_list, gu_nav_page);
LIST_HEAD(gu_window_list, gu_window);

typedef struct gtk_ui {
  uii_t gu_uii;
  
  hts_thread_t gu_thread;

  prop_courier_t *gu_pc;

  struct gu_window_list gu_windows;

  LIST_HEAD(, popup) popups;

} gtk_ui_t;


/**
 *
 */
typedef struct gu_window {

  gtk_ui_t *gw_gu;

  LIST_ENTRY(gu_window) gw_link;

  GtkWidget *gw_page_container;

  struct gu_nav_page_list gw_pages;

  struct gu_nav_page *gw_page_current;
  
  GtkWidget *gw_url;

  GtkWidget *gw_window;

  int gw_fullwindow;
  int gw_fullscreen;

  GtkWidget *gw_vbox;
  GtkWidget *gw_menubar;
  GtkWidget *gw_toolbar;
  GtkWidget *gw_playdeck;
  GtkWidget *gw_statusbar;

  prop_t *gw_nav;

  int gw_view_toolbar;
  int gw_view_playdeck;
  int gw_view_statusbar;


} gu_window_t;



void gu_fullwindow_update(gu_window_t *gw);

void gu_nav_open(gu_window_t *gw, 
		 const char *url, const char *type, prop_t *psource);

void gu_nav_open_newwin(gtk_ui_t *gu, 
			const char *url, const char *type, prop_t *psource);

void gu_nav_send_event(gu_window_t *gw, event_t *e);

gu_window_t *gu_win_create(gtk_ui_t *gu, prop_t *nav, int all);

void gu_win_destroy(gu_window_t *gw);

/**
 *
 */
typedef struct gu_nav_page {
  gu_window_t *gnp_gw;

  LIST_ENTRY(gu_nav_page) gnp_link;

  prop_t *gnp_prop;         // Root property for page

  GtkWidget *gnp_pagebin;   /* Bin for the page to put its widget in.
			     * This widget is hidden/shown depending
			     * on if this page is currently shown
			     * in the navigator 
			     */

  GtkWidget *gnp_pageroot;   /* Root widget for current page.
				Must always be the only child of
				gnp_pagebin */

  char *gnp_url;

  prop_sub_t *gnp_sub_type;
  prop_sub_t *gnp_sub_url;

  int gnp_fullwindow;

} gu_nav_page_t;

void gu_nav_pages(void *opaque, prop_event_t event, ...);

void gu_directory_create(gu_nav_page_t *gnp);

GtkWidget *gu_menubar_add(gu_window_t *gw, GtkWidget *parent);

GtkWidget *gu_toolbar_add(gu_window_t *gw, GtkWidget *parent);

GtkWidget *gu_playdeck_add(gu_window_t *gw, GtkWidget *parent);

GtkWidget *gu_statusbar_add(gu_window_t *gw, GtkWidget *parent);

void gu_popup_init(gtk_ui_t *gu);

void gu_home_create(gu_nav_page_t *gnp);

void gu_video_create(gu_nav_page_t *gnp);

void gu_page_set_fullwindow(gu_nav_page_t *gnp, int enable);

/**
 * gu_helpers.c
 */
void gu_subscription_set_label(void *opaque, const char *str);

void gu_subscription_set_label_xl(void *opaque, const char *str);

void gu_subscription_set_sensitivity(void *opaque, int on);

void gu_unsubscribe_on_destroy(GtkObject *o, prop_sub_t *s);

typedef void (gu_cloner_add_func_t)(gtk_ui_t *gu, void *opaque,
				    prop_t *p, void *node, void *before,
				    int position);

typedef void (gu_cloner_del_func_t)(gtk_ui_t *gu, void *opaque, void *node);

TAILQ_HEAD(gu_cloner_node_queue, gu_cloner_node);

typedef struct gu_cloner {
  void *gc_opaque;
  gu_cloner_add_func_t *gc_add;
  gu_cloner_del_func_t *gc_del;
  size_t gc_nodesize;
  gtk_ui_t *gc_gu;
  struct gu_cloner_node_queue gc_nodes;
  int gc_flags;
} gu_cloner_t;


typedef struct gu_cloner_node {
  TAILQ_ENTRY(gu_cloner_node) gcn_link;
  prop_t *gcn_prop;
  int gcn_position;
} gu_cloner_node_t;

void gu_cloner_init(gu_cloner_t *gc, void *opaque, void *addfunc,
		    void *delfunc, size_t nodesize, gtk_ui_t *gu,
		    int flags);

#define GU_CLONER_TRACK_POSITION 0x1

void gu_cloner_subscription(void *opaque, prop_event_t event, ...);

void gu_cloner_destroy(gu_cloner_t *gc);


/**
 * gu_pixbuf.c
 */
void gu_pixbuf_init(void);

GdkPixbuf *gu_pixbuf_get_sync(const char *url, int width, int height);

void gu_pixbuf_async_set(const char *url, int width, int height, 
			 GtkObject *target);

#endif /* GU_H__ */

