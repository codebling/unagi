/* -*-mode:c;coding:utf-8; c-basic-offset:2;fill-column:70;c-file-style:"gnu"-*-
 *
 * Copyright (C) 2009 Arnaud "arnau" Fontaine <arnau@mini-dweeb.org>
 *
 * This  program is  free  software: you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 *  along      with      this      program.      If      not,      see
 *  <http://www.gnu.org/licenses/>.
 */

/** \file
 *  \brief Display management run on startup
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <libdrm/drm.h>

#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/randr.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_aux.h>

#include "structs.h"
#include "display.h"
#include "atoms.h"
#include "window.h"
#include "util.h"

/** Structure   holding   cookies   for   QueryVersion   requests   of
    extensions */
typedef struct {
  /** XFixes QueryVersion request cookie */
  xcb_xfixes_query_version_cookie_t xfixes;
  /** Damage QueryVersion request cookie */
  xcb_damage_query_version_cookie_t damage;
  /** Composite QueryVersion request cookie */
  xcb_composite_query_version_cookie_t composite;
  /** RandR QueryVersion request cookie */
  xcb_randr_query_version_cookie_t randr;
}  init_extensions_cookies_t;

/** NOTICE:  All above  variables are  not thread-safe,  but  well, we
    don't care as they are only used during initialisation */

/** Initialise the  QueryVersion extensions cookies with  a 0 sequence
    number, this  is not thread-safe but  we don't care here  as it is
    only used during initialisation */
static init_extensions_cookies_t _init_extensions_cookies = {{0}, {0}, {0}, {0}};

/** Cookie request used when acquiring ownership on _NET_WM_CM_Sn */
static xcb_get_selection_owner_cookie_t _get_wm_cm_owner_cookie = { 0 };

/** Cookie  when querying  the  windows tree  starting  from the  root
    window */
static xcb_query_tree_cookie_t _query_tree_cookie = { 0 };

/** Check  whether  the  needed   X  extensions  are  present  on  the
 *  server-side (all the data  have been previously pre-fetched in the
 *  extension  cache). Then send  requests to  check their  version by
 *  sending  QueryVersion  requests which  is  compulsory because  the
 *  client  MUST  negotiate  the   version  of  the  extension  before
 *  executing extension requests
 */
void
unagi_display_init_extensions(void)
{
  globalconf.extensions.composite = xcb_get_extension_data(globalconf.connection,
							   &xcb_composite_id);

  globalconf.extensions.xfixes = xcb_get_extension_data(globalconf.connection,
							&xcb_xfixes_id);

  globalconf.extensions.damage = xcb_get_extension_data(globalconf.connection,
							&xcb_damage_id);

  globalconf.extensions.randr = xcb_get_extension_data(globalconf.connection,
                                                       &xcb_randr_id);

  if(!globalconf.extensions.composite ||
     !globalconf.extensions.composite->present)
    unagi_fatal("No Composite extension");

  unagi_debug("Composite: major_opcode=%ju",
              (uintmax_t) globalconf.extensions.composite->major_opcode);

  if(!globalconf.extensions.xfixes ||
     !globalconf.extensions.xfixes->present)
    unagi_fatal("No XFixes extension");

  unagi_debug("XFixes: major_opcode=%ju",
              (uintmax_t) globalconf.extensions.xfixes->major_opcode);

  if(!globalconf.extensions.damage ||
     !globalconf.extensions.damage->present)
    unagi_fatal("No Damage extension");

  unagi_debug("Damage: major_opcode=%ju",
              (uintmax_t) globalconf.extensions.damage->major_opcode);

  _init_extensions_cookies.composite =
    xcb_composite_query_version_unchecked(globalconf.connection,
					  XCB_COMPOSITE_MAJOR_VERSION,
					  XCB_COMPOSITE_MINOR_VERSION);

  _init_extensions_cookies.damage =
    xcb_damage_query_version_unchecked(globalconf.connection,
				       XCB_DAMAGE_MAJOR_VERSION,
				       XCB_DAMAGE_MINOR_VERSION);

  _init_extensions_cookies.xfixes =
    xcb_xfixes_query_version_unchecked(globalconf.connection,
				       XCB_XFIXES_MAJOR_VERSION,
				       XCB_XFIXES_MINOR_VERSION);

  if(globalconf.extensions.randr && globalconf.extensions.randr->present)
    _init_extensions_cookies.randr =
      xcb_randr_query_version_unchecked(globalconf.connection,
                                        XCB_RANDR_MAJOR_VERSION,
                                        XCB_RANDR_MINOR_VERSION);
  else
    globalconf.extensions.randr = NULL;
}

/** Get the  replies of the QueryVersion requests  previously sent and
 * check if their version actually matched the versions needed
 *
 * \see unagi_display_init_extensions
 */
void
unagi_display_init_extensions_finalise(void)
{
  assert(_init_extensions_cookies.composite.sequence);

  xcb_composite_query_version_reply_t *composite_version_reply =
    xcb_composite_query_version_reply(globalconf.connection,
				      _init_extensions_cookies.composite,
				      NULL);

  /* Need NameWindowPixmap support introduced in version >= 0.2 */
  if(!composite_version_reply || composite_version_reply->minor_version < 2)
    {
      free(composite_version_reply);
      unagi_fatal("Need Composite extension 0.2 at least");
    }

  free(composite_version_reply);

  assert(_init_extensions_cookies.damage.sequence);

  xcb_damage_query_version_reply_t *damage_version_reply = 
    xcb_damage_query_version_reply(globalconf.connection,
				   _init_extensions_cookies.damage,
				   NULL);

  if(!damage_version_reply)
    unagi_fatal("Can't initialise Damage extension");

  free(damage_version_reply);

  assert(_init_extensions_cookies.xfixes.sequence);

  xcb_xfixes_query_version_reply_t *xfixes_version_reply =
    xcb_xfixes_query_version_reply(globalconf.connection,
				  _init_extensions_cookies.xfixes,
				  NULL);

  /* Need Region objects support introduced in version >= 2.0 */
  if(!xfixes_version_reply || xfixes_version_reply->major_version < 2)
    {
      free(xfixes_version_reply);
      unagi_fatal("Need XFixes extension 2.0 at least");
    }

  free(xfixes_version_reply);

  /* Need refresh rates support introduced in version >= 1.1 */
  if(globalconf.extensions.randr)
    {
      assert(_init_extensions_cookies.randr.sequence);

      xcb_randr_query_version_reply_t *randr_version_reply =
        xcb_randr_query_version_reply(globalconf.connection,
                                      _init_extensions_cookies.randr,
                                      NULL);

      if(!randr_version_reply || randr_version_reply->major_version < 1 ||
         randr_version_reply->minor_version < 1)
        globalconf.extensions.randr = NULL;

      free(randr_version_reply);
    }
}

/** Handler for  PropertyNotify event meaningful to  set the timestamp
 *  (given  in  the PropertyNotify  event  field)  when acquiring  the
 *  ownership of _NET_WM_CM_Sn using SetOwner request (as specified in
 *  ICCCM and EWMH)
 *
 * \see unagi_display_register_cm
 * \param event The X PropertyNotify event
 */
void
unagi_display_event_set_owner_property(xcb_property_notify_event_t *event)
{
  unagi_debug("Set _NET_WM_CM_Sn ownership");

  /* Set ownership on _NET_WM_CM_Sn giving the Compositing Manager window */
  xcb_ewmh_set_wm_cm_owner(&globalconf.ewmh, globalconf.screen_nbr,
                           globalconf.cm_window, event->time, 0, 0);

  /* Send request to check whether the ownership succeeded */
  _get_wm_cm_owner_cookie = xcb_ewmh_get_wm_cm_owner_unchecked(&globalconf.ewmh,
                                                               globalconf.screen_nbr);
}

/** Register  Compositing   Manager,  e.g.   set   ownership  on  EMWH
 *  _NET_WM_CM_Sn  atom used  to politely  stating that  a Compositing
 *  Manager is  currently running. Acquiring ownership is  done in the
 *  following  steps  (ICCCM  explains  the  principles  of  selection
 *  ownership):
 *
 *  0/ Check  whether this  selection  is  already  owned by  another
 *     program
 *
 *  1/ Create  a Window whose  identifier is set as  the _NET_WM_CM_Sn
 *     value
 *
 *  2/ Change a Window  property to  generate a  PropertyNotify event
 *     used as the timestamp  to SetOwner request as multiple attempts
 *     may be sent at the same time
 *
 *  3/ Send SetOwner request
 *
 *  4/ Check whether the SetOwner request succeeds
 */
void
unagi_display_register_cm(void)
{
  globalconf.cm_window = xcb_generate_id(globalconf.connection);

  /* Create  a  dummy  window  meaningful  to  set  the  ownership  on
     _NET_WM_CM_Sn atom */
  const uint32_t create_win_val[] = { true, XCB_EVENT_MASK_PROPERTY_CHANGE };

  xcb_create_window(globalconf.connection, XCB_COPY_FROM_PARENT,
		    globalconf.cm_window, globalconf.screen->root, 0, 0, 1, 1, 0,
		    XCB_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
		    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
		    create_win_val);

  xcb_change_property(globalconf.connection, XCB_PROP_MODE_REPLACE,
		      globalconf.cm_window, globalconf.ewmh._NET_WM_NAME,
                      globalconf.ewmh.UTF8_STRING, 8,
		      sizeof(PACKAGE_NAME), PACKAGE_NAME);

  xcb_flush(globalconf.connection);
}

/** Finish  acquiring  ownership  by  checking  whether  the  SetOwner
 *  request succeeded
 *
 * \see unagi_display_register_cm
 * \return bool true if it succeeded, false otherwise
 */
bool
unagi_display_register_cm_finalise(void)
{
  assert(_get_wm_cm_owner_cookie.sequence);

  xcb_window_t wm_cm_owner_win;

  /* Check whether the ownership of WM_CM_Sn succeeded */
  return (xcb_ewmh_get_wm_cm_owner_reply(&globalconf.ewmh, _get_wm_cm_owner_cookie,
					 &wm_cm_owner_win, NULL) &&
	  wm_cm_owner_win == globalconf.cm_window);
}

/** Redirect all  the windows to  the off-screen buffer  starting from
 *  the  root window  and change  root window  attributes to  make the
 *  server reporting meaningful events
 */
void
unagi_display_init_redirect(void)
{
  /* Manage all children windows from the root window */
  _query_tree_cookie = xcb_query_tree_unchecked(globalconf.connection,
						globalconf.screen->root);

  xcb_composite_redirect_subwindows(globalconf.connection,
				    globalconf.screen->root,
				    XCB_COMPOSITE_REDIRECT_MANUAL);

  /* Declare interest in meaningful events */
  const uint32_t select_input_val =
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
    XCB_EVENT_MASK_PROPERTY_CHANGE;

  xcb_change_window_attributes(globalconf.connection, globalconf.screen->root,
			       XCB_CW_EVENT_MASK, &select_input_val);
}

/** Finish  redirection by  adding  all the  existing  windows in  the
 *  hierarchy
 */
void
unagi_display_init_redirect_finalise(void)
{
  assert(_query_tree_cookie.sequence);

  /* Get all the windows below the root window */
  xcb_query_tree_reply_t *query_tree_reply =
    xcb_query_tree_reply(globalconf.connection,
			 _query_tree_cookie,
			 NULL);

  /* Add all these windows excluding the root window of course */
  const int nwindows = xcb_query_tree_children_length(query_tree_reply);
  if(nwindows)
    unagi_window_manage_existing(nwindows,
                                 xcb_query_tree_children(query_tree_reply));

  free(query_tree_reply);
}

/** Add the given Region to the damaged Region by either copying it if
 *  the global Damaged is currently empty or adding it otherwise
 *
 *  @todo: Perhaps the  copy of  the  region  could be  avoided  if it
 *         really impacts  on performances, but for now  this does not
 *         seem to be an issue
 *
 * \param region Damaged Region to be added to the global one
 */
void
unagi_display_add_damaged_region(xcb_xfixes_region_t *region,
                                 bool do_destroy_region)
{
  if(!*region)
    return;

  if(globalconf.damaged)
    {
      xcb_xfixes_union_region(globalconf.connection, globalconf.damaged,
                              *region, globalconf.damaged);

      unagi_debug("Added %x to damaged region %x", *region, globalconf.damaged);

      if(do_destroy_region)
        xcb_xfixes_destroy_region(globalconf.connection, *region);
    }
  else
    {
      /* If the region should not be destroyed, then copy the given
         Region as it is generally the Window Region and can still be
         used later on whereas the damaged Region is cleared at each
         painting iteration */
      if(!do_destroy_region)
        {
          globalconf.damaged = xcb_generate_id(globalconf.connection);

          xcb_xfixes_create_region(globalconf.connection,
                                   globalconf.damaged,
                                   0, NULL);

          xcb_xfixes_copy_region(globalconf.connection, *region,
                                 globalconf.damaged);
        }
      else
        globalconf.damaged = *region;

      unagi_debug("Initialized damaged region to %x (copied: %d)",
                  globalconf.damaged, !do_destroy_region);
    }

  if(do_destroy_region)
    *region = XCB_NONE;
}

/** Destroy the global  damaged Region and set it  to None, meaningful
 *  at  each  re-painting iteration  to  check  whether  a repaint  is
 *  necessary. This region is filled in event handlers
 */
void
unagi_display_reset_damaged(void)
{
  if(globalconf.damaged)
    {
      xcb_xfixes_destroy_region(globalconf.connection, globalconf.damaged);
      globalconf.damaged = XCB_NONE;
    }
}

/** Update screen information provided by RandR, currently only screen
 *  refresh rate (necessary to calculate the interval between
 *  painting) and screen sizes (useful for expose for example to not
 *  display scaled windows out of screen
 */
void
unagi_display_update_screen_information(xcb_randr_get_screen_info_cookie_t screen_info_cookie,
                                        xcb_randr_get_screen_resources_cookie_t screen_resources_cookie)
{
  globalconf.crtc_len = 0;
  int crtcs_len = 0;
  if(!screen_info_cookie.sequence || !screen_resources_cookie.sequence)
    goto randr_not_available;

  xcb_randr_get_screen_info_reply_t *screen_info_reply =
    xcb_randr_get_screen_info_reply(globalconf.connection, screen_info_cookie, NULL);

  if(screen_info_reply)
    {
      if(screen_info_reply->rate)
        {
          float rate = 1 / (float) screen_info_reply->rate;

          if(rate < UNAGI_MINIMUM_REPAINT_INTERVAL)
            {
              unagi_warn("Got refresh rate > 200Hz, set it to 200Hz");
              rate = (float) UNAGI_MINIMUM_REPAINT_INTERVAL;
            }

          unagi_debug("Set refresh rate interval to %.3fs", rate);
          globalconf.refresh_rate_interval = rate;
        }

      free(screen_info_reply);
    }

  xcb_randr_get_screen_resources_reply_t *screen_resources_reply;
  if((screen_resources_reply = xcb_randr_get_screen_resources_reply(globalconf.connection,
                                                                    screen_resources_cookie,
                                                                    NULL)) &&
     (crtcs_len = xcb_randr_get_screen_resources_crtcs_length(screen_resources_reply)))
    {
      globalconf.crtc = calloc((size_t) crtcs_len,
                               sizeof(xcb_randr_get_crtc_info_reply_t *));

      /* TODO: Asynchronous? */
      xcb_randr_crtc_t *crtcs = xcb_randr_get_screen_resources_crtcs(screen_resources_reply);
      for(int i = 0; i < crtcs_len; i++)
        {
          xcb_randr_get_crtc_info_cookie_t crtc_info_cookie;
          crtc_info_cookie = xcb_randr_get_crtc_info_unchecked(globalconf.connection,
                                                               crtcs[i],
                                                               screen_resources_reply->config_timestamp);

          xcb_randr_get_crtc_info_reply_t *crtc_info_reply;
          crtc_info_reply = xcb_randr_get_crtc_info_reply(globalconf.connection,
                                                          crtc_info_cookie,
                                                          NULL);

          if(crtc_info_reply && crtc_info_reply->mode != XCB_NONE)
            {
              globalconf.crtc[i] = crtc_info_reply;
              globalconf.crtc_len++;
              unagi_debug("%jux%ju +%jd +%jd",
                          (uintmax_t) crtc_info_reply->width,
                          (uintmax_t) crtc_info_reply->height,
                          (intmax_t) crtc_info_reply->x,
                          (intmax_t) crtc_info_reply->y);
            }
          else
            {
              if(crtc_info_reply)
                free(crtc_info_reply);
              else
                unagi_warn("Could not get CRTC %d information with RandR", i);
            }
        }

      free(screen_resources_reply);
    }

 randr_not_available:
  if(!globalconf.refresh_rate_interval)
    {
      unagi_warn("Could not get screen refresh rate with RandR, set it to 50Hz");
      globalconf.refresh_rate_interval = (float) UNAGI_DEFAULT_REPAINT_INTERVAL;
    }

  if(!globalconf.crtc_len)
    {
      unagi_warn("Could not get CRTC sizes with RandR, assuming root Window size");

      if(!crtcs_len)
        globalconf.crtc = calloc(1, sizeof(xcb_randr_get_crtc_info_reply_t *));

      globalconf.crtc_len = 1;

      globalconf.crtc[0] = calloc(1, sizeof(xcb_randr_get_crtc_info_reply_t));
      globalconf.crtc[0]->width = globalconf.screen->width_in_pixels;
      globalconf.crtc[0]->height = globalconf.screen->height_in_pixels;
    }
}

bool
display_vsync_drm_init(void)
{
  if((globalconf.vsync_drm_fd = open("/dev/dri/card0", O_RDWR)) < 0)
    {
      unagi_warn("Failed to open DRM device: %s, disabling VSync with DRM",
                 strerror(errno));

      return false;
    }

  return true;
}

/**
 * Block until the next VSync
 *
 * Taken from compton
 */
int
display_vsync_drm_wait(void)
{
  if(globalconf.vsync_drm_fd < 0)
    return 0;

  int ret = -1;
  drm_wait_vblank_t vbl;
  vbl.request.type = _DRM_VBLANK_RELATIVE;
  vbl.request.sequence = 1;

  do
    {
      ret = ioctl(globalconf.vsync_drm_fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
      vbl.request.type &= ~_DRM_VBLANK_RELATIVE;
    }
  while(ret && errno == EINTR);

  if(ret)
    unagi_warn("VBlank ioctl failed, not implemented in this driver?");

  return ret;
}

void
display_vsync_drm_cleanup(void)
{
  if(globalconf.vsync_drm_fd >= 0)
    close(globalconf.vsync_drm_fd);
}
