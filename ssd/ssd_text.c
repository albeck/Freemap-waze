/* ssd_text.c - Static text widget
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
 *   See ssd_text.h
 */

#include <stdlib.h>
#include <string.h>

#include "roadmap.h"
#include "roadmap_canvas.h"
#include "../roadmap_phone_keyboard.h"
#include "../roadmap_strings.h"
#include "../roadmap_keyboard_text.h"

#include "ssd_widget.h"
#include "ssd_text.h"

#define  AUTO_HEIGHT_FACTOR      (0.50F)

static int           initialized;
static RoadMapPen    pen;
static const char*   def_color = "#000000";

typedef struct tag_text_ctx
{
   int                  size;
   roadmap_input_type   input_type;
   BOOL                 used_last_char;
   BOOL                 auto_size;
   BOOL                 auto_size_set;
   BOOL                 auto_trim;
   
}  text_ctx, *text_ctx_ptr;

void text_ctx_init( text_ctx_ptr this)
{
   memset( this, 0, sizeof(text_ctx));
   this->input_type = inputtype_binary;
}

void ssd_text_set_auto_size( SsdWidget this)
{
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   ctx->auto_size = TRUE;
}   

void ssd_text_set_auto_trim( SsdWidget this)
{
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   ctx->auto_trim = TRUE;
}   

void ssd_text_set_color( SsdWidget this, const char* color)
{ this->fg_color = color;}   

static void init_containers (void) {
   pen = roadmap_canvas_create_pen ("ssd_text_pen");
   roadmap_canvas_set_foreground (def_color);

   initialized = 1;
}

void ssd_text_set_input_type( SsdWidget this, roadmap_input_type input_type)
{
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   
   ctx->input_type   = input_type;
   this->flags       &= ~SSD_TEXT_READONLY;
}

void ssd_text_set_text_size( SsdWidget this, int size)
{
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   ctx->size = size;
}

///[BOOKMARK]:[NOTE]:[PAZ] - This will also position widget at offset (n,m)
static void select_text_size( SsdWidget this)
{
   text_ctx_ptr   ctx = (text_ctx_ptr)this->data;
   int            parent_height;
   int            text_size;
   int            text_offset;

   if(!this->value      || !this->parent  || 
      !ctx->auto_size   || ctx->auto_size_set)
      return;
   
   parent_height = this->parent->size.height;
   if( parent_height < 1)
      parent_height = this->parent->cached_size.height;
   if( parent_height < 1)
      return;
      
   text_size   = (int)(((double) parent_height * AUTO_HEIGHT_FACTOR)+0.5F);
   text_offset = (int)(((double)(parent_height - text_size)/2.F)+0.5);
   
   ctx->size      = text_size;
   this->offset_x = text_offset;
   this->offset_y = text_offset;
   
   ///[BOOKMARK]:[VERIFY]:[PAZ] - Do we need to modify widget size?   
   
   ctx->auto_size_set = TRUE;
}

static int format_text (SsdWidget widget, int draw,
                        RoadMapGuiRect *rect) {

   text_ctx_ptr ctx = (text_ctx_ptr) widget->data;
   char line[255] = {0};
   const char *text;
   int text_width;
   int text_ascent;
   int text_descent;
   int text_height = 0;
   int width = rect->maxx - rect->minx + 1;
   int max_width = 0;
   RoadMapGuiPoint p;
   
   if (draw) {
      p.x = rect->minx;
      p.y = rect->miny;
   }

   if( widget->flags & SSD_TEXT_PASSWORD)
      text = "*****";
   else 
      text = widget->value;

   while (*text || *line) {
      const char *space = strchr(text, ' ');
      const char *new_line = strchr(text, '\n');
      size_t len;
      size_t new_len;

      if (!*text) {
         new_len = strlen(line);
         len = 0;

      } else {

         if (new_line && (!space || (new_line < space))) {
            space = new_line;
         } else {
            new_line = NULL;
         }

         if (space) {
            len = space - text;
         } else {
            len = strlen(text);
         }

         if (len > (sizeof(line) - strlen(line) - 1)) {
            len = sizeof(line) - strlen(line) - 1;
         }

         if (*line) {
            strcat (line ," ");
         }

         new_len = strlen(line) + len;
         strncpy (line + strlen(line), text, len);
         line[new_len] = '\0';

         text += len;

         if (space) text++;
      }

      roadmap_canvas_get_text_extents 
         (line, ctx->size, &text_width, &text_ascent,
          &text_descent, NULL);

      if (!*text || new_line || (text_width > width)) {
         int h;

         if ((text_width > width) && (new_len > len)) {
            line[new_len - len - 1] = '\0';
            text -= len;
            if (space) text--;

            roadmap_canvas_get_text_extents 
               (line, ctx->size, &text_width, &text_ascent,
                &text_descent, NULL);
         }

         h = text_ascent + text_descent;

         if (draw) {
            p.y += h;
            p.x = rect->minx;

            if (widget->flags & SSD_ALIGN_CENTER) {
               if (ssd_widget_rtl (NULL)) {
                  p.x += (width - text_width) / 2;
               } else {
                  p.x -= (width - text_width) / 2;
               }
            } else if (ssd_widget_rtl (NULL)) {
               p.x += width - text_width;
            }

            roadmap_canvas_draw_string_angle (&p, NULL, 0, ctx->size, line);
         }

         line[0] = '\0';

         if (text_width > max_width) max_width = text_width;

         text_height += h;
      }
   }

   rect->maxx = rect->minx + max_width - 1;
   rect->maxy = rect->miny + text_height - 1;

   return 0;
}


static void draw (SsdWidget widget, RoadMapGuiRect *rect, int flags) {

   const char *color = def_color;
   
   select_text_size( widget);

   if( flags & SSD_GET_SIZE)
   {
      format_text( widget, 0, rect);
      return;
   }

   // compensate for descending characters
   // Doing it dynamically is not good as the text will move vertically.
   rect->miny -= 2;

   roadmap_canvas_select_pen (pen);
   if( widget->fg_color)
      color = widget->fg_color;
   
   if (widget->parent && widget->parent->in_focus)
      	   color = "#ffffff";
   
   roadmap_canvas_set_foreground( color);

   format_text (widget, 1, rect);

   

   rect->miny += 2;
}


static int set_value (SsdWidget widget, const char *value) 
{
   if( widget->callback && !widget->callback (widget, value))
      return -1;

   if( *value)
   {
      sttstr_copy( widget->value, value, SSD_TEXT_MAXIMUM_TEXT_LENGTH);

      if( widget->flags & SSD_TEXT_LABEL)
         sttstr_append_char( widget->value, ':',   SSD_TEXT_MAXIMUM_TEXT_LENGTH);
   }
   else
      sttstr_reset( widget->value);

   ssd_widget_reset_cache (widget);

   return 0;
}

void ssd_text_reset_text( SsdWidget this)
{
   if( this)
      sttstr_reset( this->value);
}

const char* ssd_text_get_text( SsdWidget this)
{
   if( !this)
      return NULL;
   
   return (const char*)this->value;
}

void ssd_text_set_text( SsdWidget this, const char* new_value)
{
   if( this)
      sttstr_copy( (char*)this->value, new_value, SSD_TEXT_MAXIMUM_TEXT_LENGTH);
}

static BOOL ssd_text_on_key_pressed( SsdWidget this, const char* utf8char, uint32_t flags)
{ 
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   
   if( this->flags & SSD_TEXT_READONLY)
      return FALSE;

   if( KEYBOARD_VIRTUAL_KEY & flags)
      return FALSE;

   if( KEY_IS_BACKSPACE || KEY_IS_ESCAPE)
   {
      if( this->value && this->value[0])
      {
         sttstr_trim_last_char( this->value);
         return TRUE;
      }
      
      return FALSE;
   }
   
   if( USING_PHONE_KEYPAD)
   {
      BOOL        value_was_replaced;
      const char* translated_char = roadmap_phone_keyboard_get_multiple_key_value( 
                                       this, 
                                       utf8char,
                                       ctx->input_type,
                                       &value_was_replaced);

      if( !translated_char)
         return FALSE;
      
      if( value_was_replaced)
         sttstr_trim_last_char( this->value);
      
      utf8char = translated_char;
   }
   else
   {
      if( !is_valid_key( utf8char, ctx->input_type))
         return FALSE;
   }
   
   sttstr_append_string( this->value, utf8char, SSD_TEXT_MAXIMUM_TEXT_LENGTH);
   return TRUE;
}

static roadmap_input_type ssd_text__get_input_type( SsdWidget this)
{
   text_ctx_ptr ctx = (text_ctx_ptr)this->data;
   return ctx->input_type;
}

SsdWidget ssd_text_new( const char* name, 
                        const char* value,
                        int         size, 
                        int         flags)
{
   SsdWidget   w;
   
   text_ctx_ptr   ctx =(text_ctx_ptr)calloc (1, sizeof(*ctx));
   text_ctx_init( ctx);
   
   ctx->size = size;

   if( !initialized)
      init_containers();
      
   // Default:
   flags |= SSD_TEXT_READONLY;
   
   w                    = ssd_widget_new (name, ssd_text_on_key_pressed, flags);
   w->_typeid           = "Text";
   w->draw              = draw;
   w->set_value         = set_value;
   w->flags             = flags;
   w->data              = ctx;
   w->value             = calloc( 1, SSD_TEXT_MAXIMUM_TEXT_LENGTH+1);
   w->get_input_type    = ssd_text__get_input_type;
   
   sttstr_reset( w->value);
   set_value( w, value);
   
   return w;
}


int ssd_text_get_char_height(int size)
{
   static int s_char_height= 0;
   static int s_last_size  = 0;
   
   if( !s_char_height || (s_last_size != size))
   {
      int text_width;
      int text_ascent;
      int text_descent;

      roadmap_canvas_get_text_extents( "aAbB19Xx8", size, &text_width, &text_ascent, &text_descent, NULL);
      s_char_height  = 3 + text_ascent + text_descent;
      s_last_size    = size;
   }
   
   return s_char_height;
}

int ssd_text_get_char_width()
{
   static int s_char_width = 0;
   
   if( !s_char_width)
   {
      int text_width;
      int text_ascent;
      int text_descent;

      roadmap_canvas_get_text_extents( "W", -1, &text_width, &text_ascent, &text_descent, NULL);
      s_char_width = text_width;
   }
   
   return s_char_width;
}

void ssd_text_set_readonly( SsdWidget this, BOOL read_only)
{
   int flags = this->flags;
   
   if( read_only)
      flags |=  SSD_TEXT_READONLY;
   else
      flags &= ~SSD_TEXT_READONLY;
      
   this->flags = flags;
}

