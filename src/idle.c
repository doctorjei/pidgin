/*
 * gaim
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif
#include <time.h>
#include <stdio.h>
#include <unistd.h>

#ifdef USE_SCREENSAVER
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/scrnsaver.h>
#endif /* USE_SCREENSAVER */

#include "multi.h"
#include "gaim.h"


int auto_is_away = 0;


gint check_idle(struct gaim_connection *gc)
{
	time_t t;
#ifdef USE_SCREENSAVER
	static XScreenSaverInfo *mit_info = NULL;
	static Display *d = NULL;
#endif
	time_t idle_time;

        /* Not idle, really...  :) */
	update_idle_times();

	plugin_event(event_blist_update, 0, 0, 0, 0);
        
	time(&t);


#ifdef USE_SCREENSAVER
	if (report_idle == IDLE_SCREENSAVER) {
		if (!d)
			d = XOpenDisplay((char *)NULL);
		if (mit_info == NULL) {
			mit_info = XScreenSaverAllocInfo ();
		}
		XScreenSaverQueryInfo (d, DefaultRootWindow(d), mit_info);
		idle_time = (mit_info->idle)/1000;
	} else
#endif /* USE_SCREENSAVER */
		idle_time = t - gc->lastsent;

	if ((general_options & OPT_GEN_AUTO_AWAY) && 
	    (idle_time > (60 * auto_away)) &&
	    (awaymessage == NULL) &&
	    (auto_is_away == 0)) {
		struct away_message *a;
		set_default_away((GtkWidget*)NULL, (gpointer)default_away);
		a = g_slist_nth_data(away_messages, default_away);
		do_away_message((GtkWidget*)NULL, a);
		auto_is_away = 1;
	} else if (auto_is_away == 1 && awaymessage != NULL &&
		   idle_time < 60*auto_away) {
		do_im_back((GtkWidget*)NULL, (GtkWidget*)NULL);
		auto_is_away = 0;
	}
	if (auto_is_away == 1 && awaymessage == NULL)
		auto_is_away = 0;


	/* If we're not reporting idle times to the server, still use Gaim
	   usage for auto-away, but quit here so we don't report to the 
	   server */
	if (report_idle == 0) {
		return TRUE;
	}

	if (idle_time > 600 && !gc->is_idle) { /* 10 minutes! */
		debug_printf("setting %s idle %d seconds\n", gc->username, idle_time);
		serv_set_idle(gc, idle_time);
		gc->is_idle = 1;
        } else if (idle_time < 600 && gc->is_idle) {
		debug_printf("setting %s unidle\n", gc->username);
		serv_touch_idle(gc);
	}

	return TRUE;

}
