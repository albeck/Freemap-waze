/* ssd_dialog.c - small screen devices Widgets (designed for touchscreens)
 * (requires agg)
 *
 * LICENSE:
 *
 *   Copyright 2006 Ehud Shabtai
 *
 *   This file is part of RoadMap.
 *
 *   RoadMap is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   RoadMap is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with RoadMap; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * SYNOPSYS:
 *
 *   See ssd_dialog.h
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "roadmap.h"
#include "roadmap_types.h"
#include "roadmap_time.h"
#include "roadmap_lang.h"
#include "roadmap_main.h"
#include "roadmap_keyboard.h"
#include "roadmap_pointer.h"
#include "roadmap_start.h"
#include "roadmap_res.h"
#include "roadmap_screen.h"
#include "roadmap_bar.h"
#include "roadmap_softkeys.h"
#include "roadmap_bar.h"

#include "ssd_widget.h"
#include "ssd_container.h"
#include "ssd_entry.h"
#include "ssd_button.h"
#include "ssd_tabcontrol.h"
#include "ssd_dialog.h"
#include "ssd_text.h"

struct ssd_dialog_item;
typedef struct ssd_dialog_item *SsdDialog;

struct ssd_dialog_item {

   struct ssd_dialog_item* next;
   struct ssd_dialog_item* activated_prev;
   char*                   name;
   void*                   context;                // References a caller-specific context.
   PFN_ON_DIALOG_CLOSED    on_dialog_closed;       // Called before hide
   SsdWidget               container;
   SsdWidget               scroll_container;
   SsdWidget               in_focus;               // Widget currently in focus
   SsdWidget               in_focus_default;       // Widget currently in focus
   BOOL                    tab_order_sorted;       // Did we sort the tab-order already?
   BOOL                    use_gui_tab_order;
   BOOL                    gui_tab_order_sorted;   // Did we sort the GUI tab-order already?
   RoadMapGuiPoint         drag_start_point;
   RoadMapGuiPoint         drag_last_motion;
   RoadMapGuiPoint         drag_end_motion;
   uint32_t                drag_start_time_ms;
   uint32_t                drag_end_time_ms;
   int                     drag_speed;
   BOOL                    time_active;
   int                     stop_offset;
   int                     scroll_counter;
   BOOL                    scroll;

   RMNativeKBParams		   ntv_kb_params;				// Native keyboard parameters per dialog
   SsdDialogNtvKbAction    ntv_kb_action;			// If the native keyboard has to be shown/hidden/no action
													    // when the dialog become active
};

static SsdDialog RoadMapDialogWindows = NULL;
static SsdDialog RoadMapDialogCurrent = NULL;


BOOL ssd_dialog_is_currently_active(void)
{ return (NULL != RoadMapDialogCurrent);}

char *ssd_dialog_currently_active_name(void){
	if (!ssd_dialog_is_currently_active())
		return NULL;
	else
		return RoadMapDialogCurrent->name;
}

void * ssd_dialog_get_current_data(void){
	return RoadMapDialogCurrent->container->data;
}


BOOL ssd_dialog_is_currently_vertical(void){

	if (NULL == RoadMapDialogCurrent)
		return FALSE;
	if (RoadMapDialogCurrent->container->flags & SSD_DIALOG_VERTICAL)
		return TRUE;
	else
		return FALSE;
}

static RoadMapGuiPoint LastPointerPoint;

static int RoadMapScreenFrozen = 0;
static int RoadMapDialogKeyEnabled = 0;

extern void ssd_tabcontrol_move_tab_left ( SsdWidget dialog);
extern void ssd_tabcontrol_move_tab_right( SsdWidget dialog);

static SsdDialog ssd_dialog_get (const char *name) {

   SsdDialog child;

   child = RoadMapDialogWindows;

   while (child != NULL) {
      if (strcmp (child->name, name) == 0) {
         return child;
      }
      child = child->next;
   }

   return NULL;
}


static void ssd_dialog_enable_callback (void) {
   RoadMapDialogKeyEnabled = 1;
   roadmap_main_remove_periodic (ssd_dialog_enable_callback);
}


static void ssd_dialog_disable_key (void) {
   RoadMapDialogKeyEnabled = 0;
   roadmap_main_set_periodic (350, ssd_dialog_enable_callback);
}

static void ssd_dialog_handle_native_kb( SsdDialog dialog )
{
   if ( roadmap_native_keyboard_enabled() )
   {
	   if ( dialog )
	   {
		   if ( dialog->ntv_kb_action == _ntv_kb_action_show )
		   {
			   roadmap_native_keyboard_show( &dialog->ntv_kb_params );
		   }
		   if ( dialog->ntv_kb_action == _ntv_kb_action_hide )
		   {
			   roadmap_native_keyboard_hide();
		   }
	   }
	   else
	   {
		   roadmap_native_keyboard_hide();
	   }
   }
}
static int ssd_dialog_pressed (RoadMapGuiPoint *point) {
   SsdWidget container = RoadMapDialogCurrent->container;

   if (!ssd_widget_find_by_pos (container, point, TRUE )) {
      LastPointerPoint.x = -1;
      return 0;
   }
   else{
      LastPointerPoint = *point;
   }
   if (!RoadMapDialogKeyEnabled) {
      LastPointerPoint.x = -1;
      return 0;
   }

   if ((RoadMapDialogCurrent->scroll_container != NULL) && (RoadMapDialogCurrent->scroll)){
      ssd_dialog_drag_start(point);
   }


   if ( ssd_widget_pointer_down (RoadMapDialogCurrent->container, point) )
   {
	   roadmap_screen_redraw ();

	   return 1;
   }

   return 1;
}

static int ssd_dialog_released(RoadMapGuiPoint *point)
{
   LastPointerPoint.x = -1;
   if( !ssd_widget_pointer_up(RoadMapDialogCurrent->container, point))
      return 0;
   roadmap_screen_redraw ();
   return 0;
}

static int ssd_dialog_short_click (RoadMapGuiPoint *point) {
   int res;

   if (LastPointerPoint.x < 0) {
      SsdWidget container = RoadMapDialogCurrent->container;
      if (ssd_widget_find_by_pos (container, point, TRUE )) {
         //return 1;
         LastPointerPoint = *point;
      } else {
         return 0;
      }
   }

   res = ssd_widget_short_click (RoadMapDialogCurrent->container, &LastPointerPoint);
   if (!res && RoadMapDialogCurrent->scroll_container)
      res = ssd_widget_short_click (RoadMapDialogCurrent->scroll_container, &LastPointerPoint);


   // TEMPORARY SOLUTION
   // TODO :: Add option to make the container as clicked anyway
   if ( !res &&  ( ssd_widget_find_by_pos ( RoadMapDialogCurrent->container,  &LastPointerPoint, TRUE ) ||
		   ssd_widget_find_by_pos ( RoadMapDialogCurrent->scroll_container,  &LastPointerPoint, TRUE ) ) )
   {
	   res = 1;
   }

   
   
   roadmap_screen_redraw ();
   return  res;
}

static int ssd_dialog_long_click (RoadMapGuiPoint *point) {

   if (!LastPointerPoint.x < 0) {
      SsdWidget container = RoadMapDialogCurrent->container;
      if (ssd_widget_find_by_pos (container, point, TRUE )) {
         return 1;
      } else {
         return 0;
      }
   }
   ssd_widget_long_click (RoadMapDialogCurrent->container, &LastPointerPoint);
   roadmap_screen_redraw ();

   return 1;
}

static void append_child (SsdWidget child) {

   ssd_widget_add (RoadMapDialogWindows->container, child);
}

void ssd_dialog_allign_focus(void){
   SsdSize size;
   int height;
   int min;
   SsdWidget title;
    title = ssd_widget_get (RoadMapDialogCurrent->container, "title_bar");
    if (title)
       min =  title->cached_size.height;
    else
       min = 0;
#ifndef TOUCH_SCREEN
    if (!is_screen_wide())
    	min += roadmap_bar_top_height();
    if( SSD_TAB_CONTROL & RoadMapDialogCurrent->container->flags)
       min += ssd_tabcontrol_get_height();
#endif

#ifdef TOUCH_SCREEN
   height = roadmap_canvas_height();
#else
   height = roadmap_canvas_height() - roadmap_bar_bottom_height();
#endif

   if (RoadMapDialogCurrent->in_focus && RoadMapDialogCurrent->scroll_container){
      if ((RoadMapDialogCurrent->in_focus->position.y == -1) || (RoadMapDialogCurrent->in_focus->position.x == -1 )){
         RoadMapDialogCurrent->in_focus->position.y = 0;
         RoadMapDialogCurrent->scroll_container->offset_y +=20;
      }
      ssd_widget_get_size(RoadMapDialogCurrent->in_focus, &size, NULL);
      if (( RoadMapDialogCurrent->in_focus->position.y == 0) || ((size.height + RoadMapDialogCurrent->in_focus->position.y) > height)){

          while (( RoadMapDialogCurrent->in_focus->position.y == 0) || ((size.height + RoadMapDialogCurrent->in_focus->position.y) > height)) {
            ssd_widget_set_offset(RoadMapDialogCurrent->scroll_container, 0, RoadMapDialogCurrent->scroll_container->offset_y-20);
            ssd_dialog_draw();
         }
         
         while (RoadMapDialogCurrent->in_focus->position.y < min){
            ssd_widget_set_offset(RoadMapDialogCurrent->scroll_container, 0, RoadMapDialogCurrent->scroll_container->offset_y+20);
            ssd_dialog_draw();
         }
      }
      else{
         while (RoadMapDialogCurrent->in_focus->position.y < min){
            ssd_widget_set_offset(RoadMapDialogCurrent->scroll_container, 0, RoadMapDialogCurrent->scroll_container->offset_y+20);
            ssd_dialog_draw();
         }
      }
   }
   
}


BOOL ssd_dialog_set_dialog_focus( SsdDialog dialog, SsdWidget new_focus)
{
   SsdWidget last_focus;
   SsdWidget w;
   if( new_focus && !new_focus->tab_stop)
   {
      //assert( 0 && "ssd_dialog_set_dialog_focus() - Invalid input");
      return FALSE;
   }


   w = ssd_widget_get(new_focus,"focus_image");
   if( w != NULL)
       ssd_widget_show( w->children);

   // Setting the same one twice...
   if( dialog->in_focus == new_focus){
      if (new_focus && !new_focus->in_focus)
         ssd_widget_set_focus(new_focus);
      return TRUE;
   }

   if( new_focus && (dialog != new_focus->parent_dialog))
   {
      /*/[BOOKMARK]:[NOTE]:[PAZ] - If 'new_focus->parent_dialog' is NULL:
         Maybe 'ssd_dialog_sort_tab_order()' was called before 'ssd_dialog_draw'?
         Note that 'ssd_dialog_draw' will call 'ssd_dialog_sort_tab_order()',
         thus no need to call it directly.                                          */

      assert( 0 && "ssd_dialog_set_dialog_focus() - widget does not belong to this dialog");
      return FALSE;
   }

   last_focus = dialog->in_focus;
   if (dialog->in_focus)
   {
      ssd_widget_loose_focus(dialog->in_focus);
      dialog->in_focus = NULL;
   }

   if( !new_focus)
      return TRUE;

   if( !ssd_widget_set_focus(new_focus))
   {
      assert(0);
      if( ssd_widget_set_focus( last_focus))
         dialog->in_focus = last_focus;
      return FALSE;
   }

   dialog->in_focus = new_focus;

   ssd_dialog_allign_focus();
   return TRUE;
}

SsdWidget ssd_dialog_get_focus(void)
{
   if( RoadMapDialogCurrent && RoadMapDialogCurrent->in_focus)
      return RoadMapDialogCurrent->in_focus;
   return NULL;
}

BOOL ssd_dialog_set_focus( SsdWidget new_focus)
{
   if( !RoadMapDialogCurrent)
   {
      assert( 0 && "ssd_dialog_set_focus() - Invalid state");
      return FALSE;
   }

   return ssd_dialog_set_dialog_focus( RoadMapDialogCurrent, new_focus);
}


void ssd_dialog_move_focus (int direction) {

   assert(RoadMapDialogCurrent);

   if (!RoadMapDialogCurrent->in_focus) {
      ssd_dialog_set_dialog_focus(RoadMapDialogCurrent,
                                  RoadMapDialogCurrent->in_focus_default);
      return;
   }

   RoadMapDialogCurrent->in_focus =
      ssd_widget_move_focus(RoadMapDialogCurrent->in_focus, direction);
   ssd_dialog_allign_focus();
   ssd_dialog_resort_tab_order();
}


SsdWidget ssd_dialog_new (const char *name, const char *title,
                          PFN_ON_DIALOG_CLOSED on_dialog_closed, int flags)
{
   SsdDialog   dialog;

   int width   = SSD_MAX_SIZE;
   int height  = SSD_MAX_SIZE;

   dialog = (SsdDialog)calloc( sizeof( struct ssd_dialog_item), 1);
   roadmap_check_allocated(dialog);

   dialog->name                  = strdup(name);
   dialog->on_dialog_closed      = on_dialog_closed;
   dialog->in_focus              = NULL;
   dialog->tab_order_sorted      = FALSE;
   dialog->gui_tab_order_sorted  = FALSE;
   dialog->use_gui_tab_order     = (SSD_DIALOG_GUI_TAB_ORDER & flags)? TRUE: FALSE;
   dialog->ntv_kb_action = _ntv_kb_action_hide;
   memset( &dialog->ntv_kb_params, 0, sizeof( RMNativeKBParams ) );

   if (flags & SSD_DIALOG_FLOAT) {
      if (flags & SSD_DIALOG_VERTICAL)
         width = SSD_MIN_SIZE;
      else
         width = SSD_MAX_SIZE;
      height = SSD_MIN_SIZE;
   }

   if (flags & SSD_DIALOG_NO_SCROLL) {
      flags &= ~SSD_DIALOG_NO_SCROLL;
      dialog->scroll = FALSE;
   }
   else{
      dialog->scroll = TRUE;
   }

   dialog->container = ssd_container_new (name, title, width, height, flags);
   dialog->next      = RoadMapDialogWindows;

   if (!(flags & SSD_DIALOG_FLOAT)){
      dialog->scroll_container = ssd_container_new (name, title, width, SSD_MIN_SIZE, 0);
      ssd_widget_set_color(dialog->scroll_container, NULL, NULL);
      ssd_widget_add(dialog->container, dialog->scroll_container);
      dialog->stop_offset = 0;
   }

   RoadMapDialogWindows = dialog;

   ssd_widget_set_right_softkey_text(dialog->container, roadmap_lang_get("Back_key"));
   ssd_widget_set_left_softkey_text (dialog->container, roadmap_lang_get("Exit_key"));

   if (!(flags & SSD_DIALOG_FLOAT))
      return dialog->scroll_container;
   else
      return dialog->container;
}

void ssd_dialog_redraw_screen_recursive( SsdDialog Dialog)
{
   RoadMapGuiRect rect;
   SsdWidget      container = Dialog->container;

   if( (SSD_DIALOG_FLOAT & container->flags) && Dialog->next)
      ssd_dialog_redraw_screen_recursive( Dialog->next);

   rect.minx = 0;
   rect.miny = 0;
   rect.maxx = roadmap_canvas_width() - 1;
   rect.maxy = roadmap_canvas_height() - 1;

   ssd_widget_reset_cache  ( container);
   ssd_widget_draw         ( container, &rect, 0);
}

void ssd_dialog_redraw_screen()
{
   if( !RoadMapDialogCurrent)
      return;

   ssd_dialog_redraw_screen_recursive( RoadMapDialogCurrent);
}

void ssd_dialog_new_entry (const char *name, const char *value,
                           int flags, SsdCallback callback) {

   SsdWidget child = ssd_entry_new (name, value, flags, 0,SSD_MIN_SIZE, SSD_MIN_SIZE,"");
   append_child (child);

   ssd_widget_set_callback (child, callback);
}


SsdWidget ssd_dialog_new_button (const char *name, const char *value,
                                 const char **bitmaps, int num_bitmaps,
                                 int flags, SsdCallback callback) {

   SsdWidget child =
      ssd_button_new (name, value, bitmaps, num_bitmaps, flags, callback);
   append_child (child);

   return child;
}

void ssd_dialog_change_button(const char *name, const char **bitmaps, int num_bitmaps){
   SsdWidget button = ssd_widget_get(RoadMapDialogCurrent->container, name);
   if (button)
      ssd_button_change_icon(button, bitmaps, num_bitmaps);
}

void ssd_dialog_change_text(const char *name, const char *value){
   SsdWidget text = ssd_widget_get(RoadMapDialogCurrent->container, name);
   if (text)
      ssd_text_set_text(text, value);
}

void ssd_dialog_new_line (void) {

   SsdWidget last = RoadMapDialogWindows->container->children;

   while (last->next) last=last->next;

   last->flags |= SSD_END_ROW;
}

static BOOL OnKeyPressed( const char* utf8char, uint32_t flags) {
   BOOL        key_handled = TRUE;
   SsdWidget   in_focus    = NULL;

   if( !RoadMapDialogCurrent)
      return FALSE;

   // Let the control handle the key:
   in_focus = RoadMapDialogCurrent->in_focus;
   if( in_focus && ssd_widget_on_key_pressed (in_focus, utf8char, flags)) {
      roadmap_screen_redraw();
      return TRUE;
   }

   // The control did not handle the key...
   //    Supply general handling for virtual keys:
   if( KEYBOARD_VIRTUAL_KEY & flags)
   {
      SsdWidget container = RoadMapDialogCurrent->container;
      switch( *utf8char) {
         case VK_Back:
            ssd_dialog_hide_current(dec_cancel);
            break;

         case VK_Arrow_left:
            if( SSD_TAB_CONTROL & RoadMapDialogCurrent->container->flags)
               if (RoadMapDialogCurrent->scroll_container)
                  ssd_tabcontrol_move_tab_left( RoadMapDialogCurrent->scroll_container);
               else
                  ssd_tabcontrol_move_tab_left( RoadMapDialogCurrent->container);
            else
               ssd_dialog_move_focus(FOCUS_LEFT);
            break;

         case VK_Arrow_up:
            ssd_dialog_move_focus(FOCUS_UP);
            break;

         case VK_Arrow_right:
            if( SSD_TAB_CONTROL & RoadMapDialogCurrent->container->flags)
               if (RoadMapDialogCurrent->scroll_container)
                  ssd_tabcontrol_move_tab_right( RoadMapDialogCurrent->scroll_container);
               else
                  ssd_tabcontrol_move_tab_right( RoadMapDialogCurrent->container);
            else
               ssd_dialog_move_focus( FOCUS_RIGHT);
            break;

         case VK_Arrow_down:
            ssd_dialog_move_focus(FOCUS_DOWN);
            break;

         case VK_Softkey_right:
            if (RoadMapDialogCurrent->scroll_container && (RoadMapDialogCurrent->scroll_container->right_softkey_callback != NULL))
                     RoadMapDialogCurrent->scroll_container->right_softkey_callback(RoadMapDialogCurrent->scroll_container, RoadMapDialogCurrent->scroll_container->name, RoadMapDialogCurrent->scroll_container->context);
            else if (container->right_softkey_callback != NULL)
               container->right_softkey_callback(container, container->name, container->context);
            else
#ifdef TOUCH_SCREEN
               ssd_dialog_hide_current( dec_ok );
#else
               ssd_dialog_hide_current( dec_cancel );
#endif
            break;

         case VK_Softkey_left:
            if (RoadMapDialogCurrent->scroll_container && (RoadMapDialogCurrent->scroll_container->left_softkey_callback != NULL))
                     RoadMapDialogCurrent->scroll_container->left_softkey_callback(RoadMapDialogCurrent->scroll_container, RoadMapDialogCurrent->scroll_container->name, RoadMapDialogCurrent->scroll_container->context);
            else if (container->left_softkey_callback != NULL)
               container->left_softkey_callback(container, container->name, container->context);
            else
               ssd_dialog_hide_all(dec_cancel);
            break;

         default:
            key_handled = FALSE;
      }

   }
   else
   {
      assert(utf8char);
      assert(*utf8char);

      // Other special keys:
      if( KEYBOARD_ASCII & flags)
      {
         switch(*utf8char)
         {
            case ESCAPE_KEY:
               ssd_dialog_hide_current(dec_cancel);
               break;

            case TAB_KEY:
               ssd_dialog_move_focus(FOCUS_FORWARD);
               break;

            default:
               key_handled = FALSE;
         }
      }
   }

   if( key_handled)
      roadmap_screen_redraw ();

   return key_handled;
}

void ssd_dialog_send_keyboard_event( const char* utf8char, uint32_t flags)
{ OnKeyPressed( utf8char, flags);}

void ssd_dialog_sort_tab_order( SsdDialog dialog)
{
   if( !dialog || dialog->tab_order_sorted)
      return;

   dialog->in_focus_default = ssd_widget_sort_tab_order(dialog, dialog->container);
   dialog->tab_order_sorted = TRUE;

#ifndef TOUCH_SCREEN
   dialog->in_focus         = dialog->in_focus_default;
   if( dialog->in_focus)
      ssd_widget_set_focus( dialog->in_focus);
#endif   // !TOUCH_SCREEN
}

void ssd_dialog_sort_tab_order_by_gui_position()
{
   SsdWidget old_focus;
   SsdWidget new_focus;

   if( !RoadMapDialogCurrent || !RoadMapDialogCurrent->use_gui_tab_order || RoadMapDialogCurrent->gui_tab_order_sorted)
      return;

   // Always do this first:
   ssd_dialog_sort_tab_order( RoadMapDialogCurrent);

   old_focus = RoadMapDialogCurrent->in_focus;
   new_focus = ssd_widget_sort_gui_tab_order( RoadMapDialogCurrent->in_focus_default);


   RoadMapDialogCurrent->gui_tab_order_sorted= TRUE;


#ifndef TOUCH_SCREEN
   RoadMapDialogCurrent->in_focus            = new_focus;
   if( new_focus != old_focus)
      ssd_widget_set_focus( new_focus);
#endif   // !TOUCH_SCREEN
}


static void draw_dialog (SsdDialog dialog) {

   if (!dialog) {
      return;

   } else {
      RoadMapGuiRect rect;
      rect.minx = 0;
#ifndef TOUCH_SCREEN
      if (is_screen_wide())
         rect.miny = 1;
      else
         rect.miny = roadmap_bar_top_height()+1;
      rect.maxx = roadmap_canvas_width() -1;
#else
      rect.miny = 1;
      rect.maxx = roadmap_canvas_width() -1;
#endif

#ifdef TOUCH_SCREEN
      rect.maxy = roadmap_canvas_height() - 1;
#else
      rect.maxy = roadmap_canvas_height() - 1 - roadmap_bar_bottom_height() ;
#endif

      ssd_widget_reset_cache (dialog->container);
      ssd_widget_draw (dialog->container, &rect, 0);

      if ((dialog->container->flags & SSD_CONTAINER_TITLE) && (dialog->scroll_container != NULL) && (dialog->scroll_container->offset_y < 0)){
         SsdWidget title;
         SsdSize size;
         title = ssd_widget_get (dialog->container, "title_bar");
         ssd_widget_get_size(title, &size, NULL);
#ifndef TOUCH_SCREEN
        if (!is_screen_wide()){
           rect.miny = roadmap_bar_top_height();
        }
        else{
           rect.miny = 0;
        }
#else
        rect.miny = 1;
#endif
        rect.maxy = rect.miny + size.height-1 ;
        title->draw(title, &rect, 0);
        rect.miny +=1;
        ssd_widget_draw(title->children, &rect, 0);
      }


#ifndef TOUCH_SCREEN
      roadmap_bar_draw_bottom_bar(TRUE);
      if ((dialog->container->flags & SSD_CONTAINER_TITLE) && (dialog->scroll_container != NULL) && (dialog->scroll_container->offset_y < 0))
        if (!is_screen_wide())
           roadmap_bar_draw_top_bar(TRUE);
#endif

      ssd_dialog_sort_tab_order( dialog);
      ssd_dialog_sort_tab_order_by_gui_position();
   }
}

void ssd_dialog_draw (void) {

   draw_dialog(RoadMapDialogCurrent);
   roadmap_canvas_refresh ();
}

void ssd_dialog_draw_prev (void) {

	if ( RoadMapDialogCurrent && ( RoadMapDialogCurrent->container->flags & SSD_DIALOG_FLOAT ) )
		draw_dialog(RoadMapDialogCurrent->activated_prev);

}


static void left_softkey_callback(void){
   SsdDialog   current  = RoadMapDialogCurrent;

   if (current == NULL)
      return;

     if (current->scroll_container && current->scroll_container->left_softkey_callback)
       (*current->scroll_container->left_softkey_callback)(current->scroll_container, "", current->context);
     else if (current->container->left_softkey_callback)
      (*current->container->left_softkey_callback)(current->container, "", current->context);
    else
         ssd_dialog_hide_all(dec_cancel);

}

static void right_softkey_callback(void){
   SsdDialog   current  = RoadMapDialogCurrent;

   if (current == NULL)
      return;

   if (current->scroll_container && current->scroll_container->right_softkey_callback)
      (*current->scroll_container->right_softkey_callback)(current->scroll_container, "", current->context);
   else if (current->container->right_softkey_callback)
      (*current->container->right_softkey_callback)(current->container, "", current->context);
   else
         ssd_dialog_hide_current(dec_cancel);
}


static void set_softkeys(SsdDialog d){
   static Softkey right,left;
   SsdWidget container;

   if (d->scroll_container)
      container = d->scroll_container;
   else
      container = d->container;

   if (container->right_softkey)
      strcpy(right.text, container->right_softkey);
   else
      strcpy(right.text, "Back_key");

   right.callback = right_softkey_callback;
   roadmap_softkeys_set_right_soft_key(d->name, &right);

   if (container->left_softkey)
      strcpy(left.text, container->left_softkey);
   else
      strcpy(left.text, "Exit_key");
   left.callback = left_softkey_callback;
   roadmap_softkeys_set_left_soft_key(d->name, &left);
}

static void hide_softkeys(SsdDialog d){
   roadmap_softkeys_remove_right_soft_key(d->name);
   roadmap_softkeys_remove_left_soft_key(d->name);
}

void ssd_dialog_refresh_current_softkeys(){
   if( !RoadMapDialogCurrent)
   {
      return;
   }
   hide_softkeys(RoadMapDialogCurrent);
   set_softkeys(RoadMapDialogCurrent);
}

#define SCROLL_AFTER_END_COUNTER 4


static void keep_dragging (void) {
   SsdDialog dialog = RoadMapDialogCurrent;
   RoadMapGuiPoint point;
   int diff;

   if (dialog == NULL)
      return;

   dialog->scroll_counter++;
   point.x = dialog->drag_start_point.x;
   diff = dialog->drag_end_motion.y - dialog->drag_start_point.y;
   if (diff > 0)
        point.y= dialog->drag_last_motion.y + dialog->scroll_counter *   (dialog->drag_speed/3);
   else
        point.y = dialog->drag_last_motion.y - dialog->scroll_counter  * (dialog->drag_speed/3);

   if (dialog->scroll_counter < SCROLL_AFTER_END_COUNTER)
      ssd_dialog_drag_motion(&point);
   else{
      dialog->time_active = FALSE;
      ssd_dialog_drag_end(&point);
      roadmap_main_remove_periodic (keep_dragging);
   }
}

int ssd_dialog_drag_start (RoadMapGuiPoint *point) {
   SsdDialog dialog = RoadMapDialogCurrent;
   if (dialog == NULL){
      return 0;
   }

   if (dialog->container->flags & SSD_DIALOG_FLOAT){
      if (LastPointerPoint.x == -1)
         return 0;
      else
         return 1;
   }

   if (dialog->scroll_container && dialog->scroll_container->drag_start)
         return (*dialog->scroll_container->drag_start)(dialog->container, point);
   else{
      dialog->drag_start_point.x = point->x;
      dialog->drag_start_point.y = point->y;
      dialog->drag_last_motion.x = point->x;
      dialog->drag_last_motion.y = point->y;
      dialog->scroll_counter = 0;
      dialog->drag_start_time_ms = roadmap_time_get_millis();
      if (dialog->time_active){
         roadmap_main_remove_periodic (keep_dragging);
         dialog->time_active = FALSE;
      }
   }
   return 1;
}


int ssd_dialog_drag_end (RoadMapGuiPoint *point) {
   uint32_t time_diff, drag_diff, speed;
   SsdDialog dialog = RoadMapDialogCurrent;

   if (dialog == NULL)
      return 0;

   if (dialog->container->flags & SSD_DIALOG_FLOAT){
      if (LastPointerPoint.x == -1)
         return 0;
      else
         return 1;
   }

   dialog->drag_end_time_ms = roadmap_time_get_millis();

   dialog->drag_end_motion.y = point->y;
   dialog->drag_end_motion.x = point->x;

   time_diff = dialog->drag_end_time_ms - dialog->drag_start_time_ms;
   drag_diff = abs(dialog->drag_end_motion.y - dialog->drag_start_point.y);
   if (time_diff > 0)
      speed = (int)(drag_diff*10)/time_diff;

#if 0
   if ((dialog->scroll_counter < SCROLL_AFTER_END_COUNTER) &&  (drag_diff > 40)){
      dialog->drag_speed = speed;
      roadmap_main_set_periodic (30, keep_dragging);
      dialog->time_active = TRUE;
      return 1;
   }
#endif

   if (dialog->scroll_container && dialog->scroll_container->drag_end)
      return (*dialog->scroll_container->drag_end)(dialog->container, point);
   else if ((dialog->scroll_container) && (dialog->scroll)){
      SsdWidget title;
      SsdSize size, size2;
      int height;
      int goffsef = (int)(1 * (point->y - dialog->drag_start_point.y ) + dialog->stop_offset);
      title = ssd_widget_get (RoadMapDialogCurrent->container, "title_bar");

      height = roadmap_canvas_height() - title->cached_size.height - 4;

      ssd_widget_reset_cache(dialog->scroll_container);
      ssd_widget_get_size(dialog->scroll_container, &size, NULL);
      if (size.height == roadmap_canvas_height() +1){
         ssd_widget_reset_cache(dialog->scroll_container);
         size2.width = SSD_MIN_SIZE;
         size2.height = SSD_MIN_SIZE;
         ssd_widget_get_size(dialog->scroll_container, &size, &size2);
      }

      if (size.height < height)
          goffsef = 0;
      else if ((goffsef + size.height) > height) {
        if (goffsef + dialog->scroll_container->position.y >  dialog->container->children->cached_size.height)
           goffsef = 0 ;
      }

      else if ((goffsef + dialog->scroll_container->position.y + size.height ) < roadmap_canvas_height()){
            goffsef = height - size.height -2 ;
      }

      ssd_widget_set_offset(dialog->scroll_container,0,goffsef);
      dialog->stop_offset = goffsef;
      ssd_dialog_draw();
   }

   return 1;
}

int ssd_dialog_drag_motion (RoadMapGuiPoint *point) {
   SsdDialog dialog = RoadMapDialogCurrent;

   if (dialog == NULL)
      return 0;

   if (dialog->container->flags & SSD_DIALOG_FLOAT){
      if (LastPointerPoint.x == -1)
         return 0;
      else
         return 1;
   }

   if (dialog->scroll_container && dialog->scroll_container->drag_motion)
      return (*dialog->scroll_container->drag_motion)(dialog->container, point);
   else{
      if ((dialog->scroll_container) && (dialog->scroll)){
         int diff = abs(dialog->drag_last_motion.y - point->y);
         if (diff > 5){
            int goffsef;
            LastPointerPoint.x = -1;
            goffsef = (int)(1 * (point->y - dialog->drag_start_point.y ) + dialog->stop_offset);
            if ((goffsef) > roadmap_canvas_height()/3){
               dialog->scroll_counter = SCROLL_AFTER_END_COUNTER;
               return 1;
            }
            else{
               SsdSize size;
               ssd_widget_get_size(dialog->scroll_container, &size, NULL);
               if (size.height > roadmap_canvas_height()){
                  if ((dialog->scroll_container->position.y + size.height) < roadmap_canvas_height()*2/3){
                     dialog->scroll_counter = SCROLL_AFTER_END_COUNTER;
                     return 1;
                  }
               }
            }
            dialog->drag_last_motion.y = point->y;
            dialog->drag_last_motion.x = point->x;
            ssd_widget_set_offset(dialog->scroll_container,0,goffsef);
            ssd_dialog_draw();
         }
         return 1;
      }
   }
   return 1;
}

SsdWidget ssd_dialog_activate (const char *name, void *context) {

   SsdDialog   prev     = NULL;
   SsdDialog   current  = RoadMapDialogCurrent;
   SsdDialog   dialog   = ssd_dialog_get (name);

   if (!dialog) {
      return NULL; /* Tell the caller this is a new, undefined, dialog. */
   }

   while (current && strcmp(current->name, name)) {
      prev = current;
      current = current->activated_prev;
   }

   if (current) {
      if (prev) {
         prev->activated_prev = current->activated_prev;
         current->activated_prev = RoadMapDialogCurrent;
         RoadMapDialogCurrent = current;
      }
      return current->container;
   }

   dialog->context = context;

   dialog->activated_prev = RoadMapDialogCurrent;

   if (!RoadMapDialogCurrent) {
      roadmap_keyboard_register_to_event__key_pressed( OnKeyPressed);
      /* Grab pointer hooks */
      roadmap_pointer_register_pressed
         (ssd_dialog_pressed, POINTER_HIGHEST);
      roadmap_pointer_register_released
         (ssd_dialog_released, POINTER_HIGHEST);
      roadmap_pointer_register_short_click
         (ssd_dialog_short_click, POINTER_HIGHEST);
      roadmap_pointer_register_long_click
         (ssd_dialog_long_click, POINTER_HIGHEST);

      roadmap_pointer_register_drag_start
                (&ssd_dialog_drag_start,POINTER_HIGHEST );

       roadmap_pointer_register_drag_end
                (&ssd_dialog_drag_end,POINTER_HIGHEST);

       roadmap_pointer_register_drag_motion
                (&ssd_dialog_drag_motion,POINTER_HIGHEST);
   }

   RoadMapDialogCurrent = dialog;
   set_softkeys(dialog);


   if( SSD_TAB_CONTROL & RoadMapDialogCurrent->container->flags)
      ssd_dialog_invalidate_tab_order();

   // Sort tab-order:
   ssd_dialog_sort_tab_order( dialog);
   dialog->stop_offset = 0;
   if (dialog->scroll_container != NULL)
      ssd_widget_set_offset(dialog->scroll_container,0,0);
   // Reset focus
#ifdef TOUCH_SCREEN
   ssd_dialog_set_dialog_focus(dialog, NULL);
#else
   ssd_dialog_set_dialog_focus(dialog, dialog->in_focus_default);
#endif

   if (!RoadMapScreenFrozen &&
   !(dialog->container->flags & (SSD_DIALOG_FLOAT|  SSD_DIALOG_TRANSPARENT))) {
      roadmap_start_screen_refresh (0);
      RoadMapScreenFrozen = 1;
   }

   ssd_dialog_disable_key ();

   ssd_dialog_handle_native_kb( dialog );

   return dialog->container; /* Tell the caller the dialog already exists. */
}


void ssd_dialog_hide (const char *name, int exit_code) {

   SsdDialog prev = NULL;
   SsdDialog dialog = RoadMapDialogCurrent;

   if (dialog == NULL)
   	return;

   dialog = RoadMapDialogCurrent; //might have changes

   while (dialog && strcmp(dialog->name, name)) {
      prev = dialog;
      dialog = dialog->activated_prev;
   }

   if (!dialog) {
      return;
   }

   if (dialog->on_dialog_closed) {
      dialog->on_dialog_closed( exit_code, dialog->context);
   }

   if (prev == NULL) {
      if (RoadMapDialogCurrent)
         RoadMapDialogCurrent = RoadMapDialogCurrent->activated_prev;
   } else {
      prev->activated_prev = dialog->activated_prev;
   }

   if (RoadMapDialogCurrent) {
      ssd_dialog_disable_key ();
//      if (RoadMapDialogCurrent->in_focus)
//	      ssd_widget_loose_focus(RoadMapDialogCurrent->in_focus);
   } else {
      roadmap_pointer_unregister_pressed     (ssd_dialog_pressed);
      roadmap_pointer_unregister_released    (ssd_dialog_released);
      roadmap_pointer_unregister_short_click (ssd_dialog_short_click);
      roadmap_pointer_unregister_long_click  (ssd_dialog_long_click);
      roadmap_keyboard_unregister_from_event__key_pressed(OnKeyPressed);

      roadmap_pointer_unregister_drag_start
                  (&ssd_dialog_drag_start);
      roadmap_pointer_unregister_drag_end
                  (&ssd_dialog_drag_end);
      roadmap_pointer_unregister_drag_motion
                  (&ssd_dialog_drag_motion);

   }


   hide_softkeys(dialog);

   ssd_dialog_handle_native_kb( RoadMapDialogCurrent );

   if (RoadMapScreenFrozen) {
      dialog = RoadMapDialogCurrent;
      while (dialog) {
         if ( !(dialog->container->flags &
             (SSD_DIALOG_FLOAT|SSD_DIALOG_TRANSPARENT))) {
            ssd_dialog_draw ();
            return;
         }
         dialog = dialog->activated_prev;
      }
   }

   roadmap_input_type_set_mode( inputtype_numeric );
    RoadMapScreenFrozen = 0;

   roadmap_start_screen_refresh (1);
}


void ssd_dialog_hide_current( int exit_code) {

   if( RoadMapDialogCurrent)
      ssd_dialog_hide (RoadMapDialogCurrent->name, exit_code);
}

void ssd_dialog_hide_all(int exit_code)
{
   while( RoadMapDialogCurrent)
      ssd_dialog_hide( RoadMapDialogCurrent->name, exit_code);
}

const char *ssd_dialog_get_value (const char *name) {

   return ssd_widget_get_value (RoadMapDialogCurrent->container, name);
}


const void *ssd_dialog_get_data (const char *name) {

   return ssd_widget_get_data (RoadMapDialogCurrent->container, name);
}


int ssd_dialog_set_value (const char *name, const char *value) {

   return ssd_widget_set_value (RoadMapDialogCurrent->container, name, value);
}


int ssd_dialog_set_data  (const char *name, const void *value) {
   return ssd_widget_set_data (RoadMapDialogCurrent->container, name, value);
}

void ssd_dialog_set_current_scroll_flag(BOOL scroll){
   if (!RoadMapDialogCurrent)
      return;
   RoadMapDialogCurrent->scroll = scroll;
}

void ssd_dialog_set_callback( PFN_ON_DIALOG_CLOSED on_dialog_closed) {

   if (!RoadMapDialogCurrent) {
      roadmap_log (ROADMAP_FATAL,
         "Trying to set a dialog callback, but no active dialogs exist");
   }

   RoadMapDialogCurrent->on_dialog_closed = on_dialog_closed;
}


void *ssd_dialog_context (void) {
   if (!RoadMapDialogCurrent) {
      roadmap_log (ROADMAP_FATAL,
         "Trying to get dialog context, but no active dialogs exist");
   }

   return RoadMapDialogCurrent->container->context;
}

void ssd_dialog_invalidate_tab_order()
{
   SsdDialog dialog = RoadMapDialogCurrent;
   if( !dialog)
      return;

   // Un-sort:
   ssd_widget_reset_tab_order( dialog->container);

   // Undo the 'was already sorted' flags:
   dialog->tab_order_sorted      = FALSE;
   dialog->gui_tab_order_sorted  = FALSE;
   dialog->in_focus              = NULL;
   dialog->in_focus_default      = NULL;
}


void ssd_dialog_invalidate_tab_order_by_name(const char *name)
{
   SsdDialog dialog = ssd_dialog_get (name);
   if( !dialog)
      return;

   // Un-sort:
   ssd_widget_reset_tab_order( dialog->container);

   // Undo the 'was already sorted' flags:
   dialog->tab_order_sorted      = FALSE;
   dialog->gui_tab_order_sorted  = FALSE;
   dialog->in_focus              = NULL;
   dialog->in_focus_default      = NULL;
}

// Tab-Order:  Force a re-sort, even if was already sorted
void ssd_dialog_resort_tab_order()
{
   SsdDialog dialog = RoadMapDialogCurrent;
   SsdWidget in_focus;
   SsdWidget in_focus_def;
   BOOL      focus_set = FALSE;

   if( !dialog)
      return;

   in_focus    = dialog->in_focus;
   in_focus_def= dialog->in_focus_default;

   ssd_dialog_invalidate_tab_order();
   draw_dialog(RoadMapDialogCurrent);

   if( in_focus_def && ssd_dialog_set_focus( in_focus_def))
   {
      dialog->in_focus_default= in_focus_def;
      dialog->in_focus        = in_focus_def;
      focus_set               = TRUE;
   }

   if( in_focus && ssd_dialog_set_focus( in_focus))
      focus_set = TRUE;

   if( focus_set)
      draw_dialog(RoadMapDialogCurrent);

   roadmap_canvas_refresh ();
}

void ssd_dialog_reset_offset(){
   SsdDialog dialog = RoadMapDialogCurrent;
   if( !dialog)
      return;

   if (!dialog->scroll_container)
      return;

   ssd_widget_set_offset(dialog->scroll_container, 0,0);
   ssd_widget_reset_cache(dialog->scroll_container);
   ssd_widget_reset_position(dialog->scroll_container);
}

void ssd_dialog_set_offset(int offset){
   SsdDialog dialog = RoadMapDialogCurrent;
   if( !dialog)
      return;

   if (!dialog->scroll_container)
      return;

   ssd_widget_set_offset(dialog->scroll_container, 0,offset);
   if (offset == 0)
      ssd_widget_reset_cache(dialog->scroll_container);
   ssd_widget_reset_position(dialog->scroll_container);
}

void ssd_dialog_wait (void)
{
   while (RoadMapDialogCurrent)
   {
      roadmap_main_flush ();
   }
}

void ssd_dialog_set_ntv_keyboard_action( const char* name, SsdDialogNtvKbAction action )
{
   SsdDialog dialog = ssd_dialog_get( name );
   dialog->ntv_kb_action = action;
}

void ssd_dialog_set_ntv_keyboard_params( const char* name, const RMNativeKBParams* params )
{
   SsdDialog dialog = ssd_dialog_get( name );
   memcpy( &dialog->ntv_kb_params, params, sizeof( RMNativeKBParams ) );
}

/***********************************************************
 *  Name        : ssd_dialog_add_hspace
 *  Purpose     : Adds horizontal space to the container
 *  Params      : [in]	widget - container
 *  			: [in]	size - vertical space size
 *  			: [in]	add_flags - additional flags
 *              : [out] - none
 *  Returns		:
 *  Notes       :
 */
void ssd_dialog_add_hspace( SsdWidget widget, int hspace, int add_flags )
{
	SsdWidget wgt_hspace = ssd_container_new( "hspacer", "", hspace, 20, add_flags );
	ssd_widget_set_color( wgt_hspace, NULL, NULL );
	ssd_widget_add( widget, wgt_hspace );
}

/***********************************************************
 *  Name        : ssd_dialog_add_vspace
 *  Purpose     : Adds vertical space to the container
 *  Params      : [in]	widget - container
 *  			: [in]	size - vertical space size
 *  			: [in]	add_flags - additional flags
 *              : [out] - none
 *  Returns		:
 *  Notes       :
 */
void ssd_dialog_add_vspace( SsdWidget widget, int vspace, int add_flags )
{
	SsdWidget space = ssd_container_new ( "spacer", NULL, SSD_MAX_SIZE, vspace,
			 SSD_END_ROW | SSD_WIDGET_SPACE | add_flags );
	ssd_widget_set_color (space, NULL, NULL);
	ssd_widget_add ( widget, space );
}
