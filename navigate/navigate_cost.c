/* navigate_cost.c - cost calculations
 *
 * LICENSE:
 *
 *   Copyright 2007 Ehud Shabtai
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
 *   See navigate_cost.h
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "roadmap.h"
#include "roadmap_config.h"
#include "roadmap_line.h"
#include "roadmap_lang.h"
#include "roadmap_start.h"
#include "roadmap_square.h"
#include "roadmap_line_route.h"
#include "roadmap_line_speed.h"
#include "roadmap_point.h"
#include "roadmap_math.h"

#include "navigate_traffic.h"
#include "navigate_main.h"
#include "navigate_graph.h"
#include "navigate_cost.h"

#include "ssd/ssd_checkbox.h"
#include "ssd/ssd_contextmenu.h"
#include "ssd/ssd_bitmap.h"
#include "ssd/ssd_separator.h"

#include "Realtime/RealtimeAlerts.h"
#include "Realtime/RealtimeTrafficInfo.h"

#define PENALTY_NONE  0
#define PENALTY_SMALL 1
#define PENALTY_AVOID 2

static time_t start_time;

static RoadMapConfigDescriptor CostTypeCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Type");
static RoadMapConfigDescriptor PreferSameStreetCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Prefer same street");
static RoadMapConfigDescriptor CostAvoidPrimaryCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Avoid primaries");
static RoadMapConfigDescriptor CostAvoidTollRoadsCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Avoid tolls");

static RoadMapConfigDescriptor CostAvoidTrailCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Avoid trails");

static RoadMapConfigDescriptor CostAllowUnknownsCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Allow unknown directions");

static RoadMapConfigDescriptor TollRoadsCfg =
                  ROADMAP_CONFIG_ITEM("Routing", "Tolls roads");


static void cost_preferences (void);

#ifndef TOUCH_SCREEN
static void set_softkeys( SsdWidget dialog);

// Context menu:
static ssd_cm_item      context_menu_items[] =
{
   SSD_CM_INIT_ITEM  ( "Recalculate",  nc_cm_recalculate),
   SSD_CM_INIT_ITEM  ( "Ok",           nc_cm_save),
   SSD_CM_INIT_ITEM  ( "Exit_key",     nc_cm_exit)
};
static ssd_contextmenu  context_menu = SSD_CM_INIT_MENU( context_menu_items);
#endif

int navigate_cost_use_traffic (void) {

	return TRUE;
}

int navigate_cost_prefer_same_street (void) {

	return roadmap_config_match(&PreferSameStreetCfg, "yes");
}


int navigate_cost_avoid_primaries (void) {

	return roadmap_config_match(&CostAvoidPrimaryCfg, "yes");
}

int navigate_cost_avoid_toll_roads(void){
   return roadmap_config_match(&CostAvoidTollRoadsCfg, "yes");
}

int navigate_cost_avoid_trails (void) {

	if (roadmap_config_match(&CostAvoidTrailCfg, "Allow"))
		return AVOID_TRAILS_ALL;
	else if (roadmap_config_match(&CostAvoidTrailCfg, "Don't allow"))
		return AVOID_TRAILS_NONE;
	else
		return AVOID_TRAILS_LONG;
}

int navigate_cost_allow_unknowns (void) {

	return ( !roadmap_config_match(&CostAllowUnknownsCfg, "no") );
}

static int calc_penalty (int line_id, int cfcc, int prev_line_id) {

   switch (cfcc) {
      case ROADMAP_ROAD_FREEWAY:
      case ROADMAP_ROAD_PRIMARY:
      case ROADMAP_ROAD_SECONDARY:
         if (navigate_cost_avoid_primaries ())
            return PENALTY_AVOID;
         break;
      case ROADMAP_ROAD_4X4:
      case ROADMAP_ROAD_TRAIL:
      	{
	      	int option = navigate_cost_avoid_trails ();
	         if (option == AVOID_TRAILS_ALL)
	            return PENALTY_AVOID;

	         if (option == AVOID_TRAILS_LONG) {
	            int length = roadmap_line_length (line_id);

	            if (length > 300) return PENALTY_AVOID;
	         }
      	}
         break;
      case ROADMAP_ROAD_PEDESTRIAN:
      case ROADMAP_ROAD_WALKWAY:
         return PENALTY_AVOID;
   }

   if (navigate_cost_prefer_same_street ()) {
      if (roadmap_line_get_street (line_id) !=
         (roadmap_line_get_street (prev_line_id))) {

         return PENALTY_SMALL;
      }
   }

   return PENALTY_NONE;
}


static int cost_shortest (int line_id, int is_revesred, int cur_cost,
                          int prev_line_id, int is_prev_reversed,
                          int node_id) {

   int cfcc = roadmap_line_cfcc (line_id);
   int penalty = 0;

   if (node_id != -1) {

   	penalty = calc_penalty (line_id, cfcc, prev_line_id);

	   switch (penalty) {
	      case PENALTY_AVOID:
	         penalty = 100000;
	         break;
	      case PENALTY_SMALL:
	         penalty = 500;
	         break;
	      case PENALTY_NONE:
	         penalty = 0;
	         break;
	   }
   }

   return penalty + roadmap_line_length (line_id);
}

static int cost_fastest (int line_id, int is_revesred, int cur_cost,
                         int prev_line_id, int is_prev_reversed,
                         int node_id) {

   int length, length_m;
   int cfcc = roadmap_line_cfcc (line_id);

   int penalty = 0;
   float m_s;

   if (node_id != -1) {
      penalty = calc_penalty (line_id, cfcc, prev_line_id);

      switch (penalty) {
         case PENALTY_AVOID:
            penalty = 100000;
            break;
         case PENALTY_SMALL:
            penalty = 500;
            break;
         case PENALTY_NONE:
            penalty = 0;
            break;
      }
   }

   length = penalty + roadmap_line_length (line_id);

   switch (cfcc) {
      case ROADMAP_ROAD_FREEWAY:
         m_s = (float)(100 / 3.6);
         break;
      case ROADMAP_ROAD_PRIMARY:
         m_s = (float)(75 / 3.6);
         break;
      case ROADMAP_ROAD_SECONDARY:
      case ROADMAP_ROAD_RAMP:
         m_s = (float)(65 / 3.6);
         break;
      case ROADMAP_ROAD_MAIN:
         m_s = (float)(30 / 3.6);
         break;
      default:
         m_s = (float)(20 / 3.6);
         break;
   }

   length_m = roadmap_math_to_cm(length) / 100;

   return (int) (length_m / m_s) + 1;
}

static int cost_fastest_traffic (int line_id, int is_reversed, int cur_cost,
                                 int prev_line_id, int is_prev_reversed,
                                 int node_id) {

   int cross_time = 0;
   int cfcc = roadmap_line_cfcc (line_id);
   int penalty = PENALTY_NONE;
   int square = roadmap_square_active ();
	int test_square = square;
	int test_line = line_id;
	int test_reversed = is_reversed;
	int i;
	RoadMapPosition start_position;
	RoadMapPosition end_position;

	cross_time = RTTrafficInfo_Get_Avg_Cross_Time(line_id, square ,is_reversed);

	/* the following loop is supposed to prevent navigation from
	 * issuing instructions to exit a highway and enter right back
	 * on the same exit when traffic is slow
	 */
	if (is_reversed) {
		roadmap_line_to (line_id, &start_position);
	} else {
		roadmap_line_from (line_id, &start_position);
	}

	for (i = 0; i < 3 && !cross_time; i++) {
		/* if this segment has only one successor, use traffic info from the successor */
		int from;
		int to;
		struct successor successors[2];
		int count;
		int speed;

		roadmap_square_set_current (test_square);
		if (test_reversed) {
			roadmap_line_points (test_line, &to, &from);
		} else {
			roadmap_line_points (test_line, &from, &to);
		}

		roadmap_point_position (to, &end_position);
		if (roadmap_math_distance (&start_position, &end_position) > 1000) break;

	   count = get_connected_segments (test_square, test_line, test_reversed,
	            							  to, successors, 2, 1, 1);
	   if (count != 1) break;

	   test_square = successors[0].square_id;
	   test_line = successors[0].line_id;
	   test_reversed = successors[0].reversed;

		if (test_square == square &&
			 test_line == line_id &&
			 test_reversed == is_reversed) {

			break;
		}

		speed = RTTrafficInfo_Get_Avg_Speed(test_line, test_square ,test_reversed);
		if (speed) {
			int length_m;
			length_m = 	roadmap_math_to_cm(roadmap_line_length (test_line)) / 100;
			cross_time = (int)(length_m *3.6  / speed) + 1;
		}
	}
	roadmap_square_set_current (square);

   if (!cross_time) cross_time =
		roadmap_line_speed_get_cross_time_at (line_id, is_reversed,
                       (start_time + (time_t)cur_cost));

   if (!cross_time) cross_time =
         roadmap_line_speed_get_avg_cross_time (line_id, is_reversed);

   if (node_id != -1) {

	   cross_time += RTAlerts_Penalty(line_id, is_reversed);

   	penalty = calc_penalty (line_id, cfcc, prev_line_id);
   }

   switch (penalty) {
      case PENALTY_AVOID:
         return cross_time + 3600;
      case PENALTY_SMALL:
         return cross_time + 60;
      case PENALTY_NONE:
      default:
         return cross_time;
   }
}

static int cost_fastest_no_traffic (int line_id, int is_reversed, int cur_cost,
                                 int prev_line_id, int is_prev_reversed,
                                 int node_id) {

   int cross_time = 0;
   int cfcc = roadmap_line_cfcc (line_id);
   int penalty = PENALTY_NONE;

   if (node_id != -1) penalty = calc_penalty (line_id, cfcc, prev_line_id);

  cross_time = roadmap_line_speed_get_cross_time_at (line_id, is_reversed,
                       (start_time + (time_t)cur_cost));

   if (!cross_time) cross_time =
         roadmap_line_speed_get_avg_cross_time (line_id, is_reversed);

   switch (penalty) {
      case PENALTY_AVOID:
         return cross_time + 3600;
      case PENALTY_SMALL:
         return cross_time + 60;
      case PENALTY_NONE:
      default:
         return cross_time;
   }

}

void navigate_cost_reset (void) {
   start_time = time(NULL);
}

NavigateCostFn navigate_cost_get (void) {

   if (navigate_cost_type () == COST_FASTEST) {
      if (navigate_cost_use_traffic ()) {
         return &cost_fastest_traffic;
      } else {
         return &cost_fastest;
      }
   } else {
      return &cost_shortest;
   }
}

int navigate_cost_time (int line_id, int is_revesred, int cur_cost,
                        int prev_line_id, int is_prev_reversed) {

     if (navigate_cost_use_traffic ()) {
					return cost_fastest_traffic (line_id, is_revesred, cur_cost,
               					                 prev_line_id, is_prev_reversed, -1);
      } else {
					return cost_fastest_no_traffic (line_id, is_revesred, cur_cost,
               					                 prev_line_id, is_prev_reversed, -1);
     }

}

void navigate_cost_initialize (void) {

   roadmap_config_declare_enumeration
      ("user", &CostTypeCfg, NULL, "Fastest", "Shortest", NULL);

   roadmap_config_declare_enumeration
      ("user", &CostAvoidPrimaryCfg, NULL, "no", "yes", NULL);

   roadmap_config_declare_enumeration
      ("user", &CostAvoidTollRoadsCfg, NULL, "no", "yes", NULL);

   roadmap_config_declare_enumeration
      ("user", &PreferSameStreetCfg, NULL, "no", "yes", NULL);

   roadmap_config_declare_enumeration
       ("user", &CostAvoidTrailCfg, NULL,  "Don't allow", "Allow", "Avoid long ones", NULL);

   roadmap_config_declare_enumeration
       ("preferences", &TollRoadsCfg, NULL, "no", "yes",  NULL);

   roadmap_config_declare_enumeration
      ("user", &CostAllowUnknownsCfg, NULL, "yes", "no", NULL);

   roadmap_start_add_action ("traffic", "Routing preferences", NULL, NULL,
      "Change routing preferences",
      cost_preferences);
}

int navigate_cost_type (void) {
   if (roadmap_config_match(&CostTypeCfg, "Fastest")) {
      return COST_FASTEST;
   } else {
      return COST_SHORTEST;
   }
}

/**** temporary dialog ****/

#include "ssd/ssd_dialog.h"
#include "ssd/ssd_container.h"
#include "ssd/ssd_text.h"
#include "ssd/ssd_choice.h"
#include "ssd/ssd_button.h"

static void save_changes(){
      roadmap_config_set (&CostTypeCfg,
                           (const char *)ssd_dialog_get_data ("type"));
      if (roadmap_config_match(&TollRoadsCfg, "yes")){
         roadmap_config_set (&CostAvoidTollRoadsCfg,
                     (const char *)ssd_dialog_get_data ("avoidtolls"));
      }
      roadmap_config_set (&CostAvoidPrimaryCfg,
                           (const char *)ssd_dialog_get_data ("avoidprime"));
      roadmap_config_set (&PreferSameStreetCfg,
                           (const char *)ssd_dialog_get_data ("samestreet"));
      roadmap_config_set (&CostAvoidTrailCfg,
                           (const char *)ssd_dialog_get_data ("avoidtrails"));
}

static void on_close_dialog (int exit_code, void* context){
#ifdef TOUCH_SCREEN
	if (exit_code == dec_ok)
		save_changes();
#endif
}
static const char *yesno_label[2];
static const char *yesno[2];
static const char *type_label[2];
static const char *type_value[2];
static const char *trails_label[3];
static const char *trails_value[3];

/////////////////////////////////////////////////////////////////////
static SsdWidget space(int height){
	SsdWidget space;
	space = ssd_container_new ("spacer", NULL, SSD_MAX_SIZE, height, SSD_WIDGET_SPACE|SSD_END_ROW);
	ssd_widget_set_color (space, NULL,NULL);
	return space;
}

static void create_ssd_dialog (void) {

   SsdWidget box, box2;
   SsdWidget container;
#ifdef TOUCH_SCREEN
	int tab_flag = SSD_WS_TABSTOP;
#else
   	int tab_flag = SSD_WS_TABSTOP;
#endif
   SsdWidget dialog = ssd_dialog_new ("route_prefs",
                      roadmap_lang_get ("Routing preferences"),
                      on_close_dialog,
                      SSD_CONTAINER_TITLE);
#ifdef TOUCH_SCREEN
   ssd_widget_add(dialog, space(5));
#endif

   if (!yesno[0]) {
      yesno[0] = "Yes";
      yesno[1] = "No";
      yesno_label[0] = roadmap_lang_get (yesno[0]);
      yesno_label[1] = roadmap_lang_get (yesno[1]);
      type_value[0] = "Fastest";
      type_value[1] = "Shortest";
      type_label[0] = roadmap_lang_get (type_value[0]);
      type_label[1] = roadmap_lang_get (type_value[1]);
      trails_value[0] = "Allow";
      trails_value[1] = "Don't allow";
      trails_value[2] = "Avoid long ones";
      trails_label[0] = roadmap_lang_get (trails_value[0]);
      trails_label[1] = roadmap_lang_get (trails_value[1]);
      trails_label[2] = roadmap_lang_get (trails_value[2]);
   }

   container = ssd_container_new ("conatiner", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                            SSD_WIDGET_SPACE|SSD_END_ROW|SSD_ROUNDED_CORNERS|SSD_ROUNDED_WHITE|SSD_CONTAINER_BORDER|SSD_POINTER_NONE);

   box = ssd_container_new ("type group", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                            SSD_WIDGET_SPACE|SSD_END_ROW|tab_flag);

   ssd_widget_set_color (box, "#000000", "#ffffff");

   ssd_widget_add (box,
      ssd_text_new ("type_label",
                     roadmap_lang_get ("Route preference"),
                    -1, SSD_TEXT_LABEL|SSD_ALIGN_VCENTER|SSD_WIDGET_SPACE));

    ssd_widget_add (box,
         ssd_choice_new ("type", 2,
                         (const char **)type_label,
                         (const void **)type_value,
                         SSD_ALIGN_RIGHT, NULL));

   ssd_widget_add(box, space(1));
   ssd_widget_add(box, ssd_separator_new("separator", SSD_ALIGN_BOTTOM));
   ssd_widget_add (container, box);

   box = ssd_container_new ("avoidtrails group", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                            SSD_WIDGET_SPACE|SSD_END_ROW|tab_flag);
   ssd_widget_set_color (box, "#000000", "#ffffff");

   box2 = ssd_container_new ("box2", NULL, roadmap_canvas_width()/3, SSD_MIN_SIZE,
                             SSD_ALIGN_VCENTER|tab_flag);
   ssd_widget_set_color (box2, NULL, NULL);
   
   ssd_widget_add (box2,
      ssd_text_new ("avoidtrails_label",
                     roadmap_lang_get ("Dirt roads"),
                    -1, SSD_TEXT_LABEL|SSD_ALIGN_VCENTER|SSD_WIDGET_SPACE));
   ssd_widget_add(box, box2);

   ssd_widget_add (box,
         ssd_choice_new ("avoidtrails", 3,
                         (const char **)trails_label,
                         (const void **)trails_value,
                         SSD_ALIGN_RIGHT, NULL));
   ssd_widget_add(box, space(1));
   ssd_widget_add(box, ssd_separator_new("separator", SSD_ALIGN_BOTTOM));
   ssd_widget_add (container, box);

   box = ssd_container_new ("samestreet group", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                            SSD_WIDGET_SPACE|SSD_END_ROW|tab_flag);
   ssd_widget_set_color (box, "#000000", "#ffffff");

   ssd_widget_add (box,
      ssd_text_new ("samestreet_label",
                     roadmap_lang_get ("Minimize turns"),
                    -1, SSD_TEXT_LABEL|SSD_ALIGN_VCENTER|SSD_WIDGET_SPACE));

    ssd_widget_add (box,
         ssd_checkbox_new ("samestreet",FALSE, SSD_ALIGN_RIGHT, NULL,NULL,NULL,CHECKBOX_STYLE_ON_OFF));

   ssd_widget_add(box, space(1));
   ssd_widget_add(box, ssd_separator_new("separator", SSD_ALIGN_BOTTOM));
   ssd_widget_add (container, box);

   box = ssd_container_new ("avoidprime group", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                            SSD_WIDGET_SPACE|SSD_END_ROW|tab_flag);
   ssd_widget_set_color (box, "#000000", "#ffffff");

   box2 = ssd_container_new ("show icons group", NULL, roadmap_canvas_width()/2, SSD_MIN_SIZE,
                            SSD_ALIGN_VCENTER|tab_flag);
   ssd_widget_set_color (box2, NULL, NULL);

   ssd_widget_add (box2,
      ssd_text_new ("avoidprime_label",
                     roadmap_lang_get ("Avoid highways"),
                    -1, SSD_TEXT_LABEL|SSD_ALIGN_VCENTER|SSD_WIDGET_SPACE));
   ssd_widget_add(box, box2);
   ssd_widget_add (box,
         ssd_checkbox_new ("avoidprime", TRUE,   SSD_ALIGN_RIGHT, NULL,NULL,NULL,CHECKBOX_STYLE_ON_OFF));

   ssd_widget_add(box, space(1));
   if (roadmap_config_match(&TollRoadsCfg, "yes")) 
      ssd_widget_add(box, ssd_separator_new("separator", SSD_ALIGN_BOTTOM));
   ssd_widget_add (container, box);


   if (roadmap_config_match(&TollRoadsCfg, "yes")){
      box = ssd_container_new ("avoidtolls group", NULL, SSD_MAX_SIZE, SSD_MIN_SIZE,
                                SSD_WIDGET_SPACE|SSD_END_ROW|tab_flag);
      ssd_widget_set_color (box, "#000000", "#ffffff");

      ssd_widget_add (box,
               ssd_text_new ("avoidtolls_label",
                        roadmap_lang_get ("Avoid toll roads"),
                        -1, SSD_TEXT_LABEL|SSD_ALIGN_VCENTER|SSD_WIDGET_SPACE));

      ssd_widget_add (box,
         ssd_checkbox_new ("avoidtolls", TRUE,   SSD_ALIGN_RIGHT, NULL,NULL,NULL,CHECKBOX_STYLE_ON_OFF));

      ssd_widget_add (container, box);
   }

   ssd_widget_add (dialog, container);
#ifndef TOUCH_SCREEN
	set_softkeys(dialog);
#endif
}

void cost_preferences (void) {

   const char *value;
   int avoid_trails;

   if (!ssd_dialog_activate ("route_prefs", NULL)) {
      create_ssd_dialog ();
      ssd_dialog_activate ("route_prefs", NULL);
   }

   if (navigate_cost_use_traffic ()) value = yesno[0];
   else value = yesno[1];
   ssd_dialog_set_data ("traffic", (void *) value);

   if (navigate_cost_type () == COST_FASTEST) value = type_value[0];
   else value = type_value[1];
   ssd_dialog_set_data ("type", (void *) value);

   if (navigate_cost_avoid_primaries ()) value = yesno[0];
   else value = yesno[1];
   ssd_dialog_set_data ("avoidprime", (void *) value);

   if (navigate_cost_avoid_toll_roads()) value = yesno[0];
   else value = yesno[1];
   ssd_dialog_set_data ("avoidtolls", (void *) value);

   if (navigate_cost_prefer_same_street ()) value = yesno[0];
   else value = yesno[1];
   ssd_dialog_set_data ("samestreet", (void *) value);

	avoid_trails = navigate_cost_avoid_trails ();
   if (avoid_trails == AVOID_TRAILS_ALL) value = trails_value[0];
   else if (avoid_trails == AVOID_TRAILS_NONE) value = trails_value[1];
   else value = trails_value[2];
   ssd_dialog_set_data ("avoidtrails", (void *) value);


}

#ifndef TOUCH_SCREEN

static   BOOL                 	g_context_menu_is_active= FALSE;


///////////////////////////////////////////////////////////////////////////////////////////
static void on_option_selected(  BOOL              made_selection,
                                 ssd_cm_item_ptr   item,
                                 void*             context)
{
   navigate_context_menu_items   selection;
   int                           exit_code = dec_ok;

   g_context_menu_is_active = FALSE;

   if( !made_selection)
      return;

   selection = (navigate_context_menu_items)item->id;

   switch( selection)
   {
      case nc_cm_recalculate:
      	save_changes();
      	ssd_dialog_hide_current(dec_close);
      	navigate_main_calc_route ();
         break;

      case nc_cm_save:
         save_changes();
         ssd_dialog_hide_current(dec_close);
         break;


      case nc_cm_exit:
         exit_code = dec_cancel;
      	ssd_dialog_hide_all( exit_code);
      	roadmap_screen_refresh ();
         break;

      default:
         break;
   }
}


///////////////////////////////////////////////////////////////////////////////////////////
static int on_options(SsdWidget widget, const char *new_value, void *context)
{
	int menu_x;


   if(g_context_menu_is_active)
   {
      ssd_dialog_hide_current(dec_cancel);
      g_context_menu_is_active = FALSE;
   }


   if  (ssd_widget_rtl (NULL))
	   menu_x = SSD_X_SCREEN_RIGHT;
	else
		menu_x = SSD_X_SCREEN_LEFT;

   ssd_context_menu_show(  menu_x,              // X
                           SSD_Y_SCREEN_BOTTOM, // Y
                           &context_menu,
                           on_option_selected,
                           NULL,
                           dir_default,
                           0);

   g_context_menu_is_active = TRUE;

   return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////
static void set_softkeys( SsdWidget dialog)
{
   ssd_widget_set_left_softkey_text       ( dialog, roadmap_lang_get("Options"));
   ssd_widget_set_left_softkey_callback   ( dialog, on_options);
}

#endif
