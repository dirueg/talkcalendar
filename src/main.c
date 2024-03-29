/*
 * main.c
 * 
 * Copyright 2021 Alan Crispin <crispinalan@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <math.h>  //compile with -lm
#include <string.h>

/**
 * 
 * @title: Talk Calendar
 * @short_description: Gtk 4 version of Talk Calendar
 * @author Alan crispin
 * Compile with:  
 * gcc $(pkg-config --cflags gtk4) -o talkcalendar main.c $(pkg-config --libs gtk4) -lm
 * 
*/

#define CONFIG_DIRNAME "talkcal-gtk4-1"
#define CONFIG_FILENAME "talkcal-gtk4-config-1"

//declarations

static void update_calendar(GtkWindow *window);
static void update_header (GtkWindow *window);
static GtkWidget *create_widget (gpointer item, gpointer user_data);
static void update_store(int m_year,int m_month,int m_day);
static void update_marked_dates(int month, int year);
static void reset_marked_dates();
void load_csv_file();
gchar* get_css_string();
GDate* calculate_easter(gint year);
gboolean check_day_events_for_overlap();

//Event Dialogs
static void callbk_check_button_allday_toggled (GtkCheckButton *check_button, gpointer user_data);
static void callbk_new_event_response(GtkDialog *dialog, gint response_id,  gpointer  user_data);
static void callbk_new_event(GtkButton *button, gpointer  user_data);
void callbk_edit_event_response(GtkDialog *dialog, gint response_id,  gpointer  user_data);
static void callbk_edit_event(GtkButton *button, gpointer  user_data);

//Actions
static void callbk_speak(GSimpleAction* action, GVariant *parameter,gpointer user_data);
static void callbk_speak_about(GSimpleAction* action,G_GNUC_UNUSED  GVariant *parameter,gpointer user_data);
static void callbk_about(GSimpleAction* action, GVariant *parameter, gpointer user_data);
static void callbk_home(GSimpleAction* action, GVariant *parameter, gpointer user_data);
static void callbk_delete(GSimpleAction* action, GVariant *parameter,  gpointer user_data);
static void callbk_quit(GSimpleAction* action,G_GNUC_UNUSED GVariant *parameter, gpointer user_data);
static void callbk_delete_selected(GtkButton *button, gpointer  user_data);
static void set_button_blue(GtkButton *button);
static void set_button_red_with_borders(GtkButton *button);
static void set_button_red(GtkButton *button);
static void set_button_green(GtkButton *button);

static GMutex lock; //talking thread locked
//config
static char * m_config_file = NULL;
static int m_talk =1;
static int m_speed = 160; //espeak words per minute (fixed)
static int m_talk_at_startup=0;
static int m_font_size=20;
static const gchar* m_font_name="Sans";
static int m_holidays=0; //show holidays
static int m_show_end_time=0; //show end_time


static int m_row_index=-1; //selection index
static const char* m_title ="title";
static const char* m_location ="";
static int m_year=0;
static int m_month=0;
static int m_day=0;
static float m_start_time=0.0;
static float m_end_time=0.0;
static int m_priority=0;
static int m_is_yearly=0;
static int m_is_allday=0;

static GListStore *m_store;

static int m_id_selection=-1;

typedef struct {
	int id;	
	char title[101];
	char location[101];
	int year;
	int month;
	int day;
	float start_time;
	float end_time;	
	int priority;
	int is_yearly;
	int is_allday;
} Event;

int max_records=5000;
Event *db_store=NULL; 

int m_db_size=0;
int marked_date[31]; //month days with events
int num_marked_dates = 0;
//---------------------------------------------------------------------
// map menu actions to callbacks
const GActionEntry app_actions[] = { 
  { "speak", callbk_speak },   
  { "version", callbk_speak_about},
  { "home", callbk_home},  
  { "about",  callbk_about},
  { "quit",   callbk_quit}
};


//--------------------------------------------------------------------
// DisplayObject functions
//--------------------------------------------------------------------
enum
{
  
  PROP_LABEL = 1, //Prop label must start with 1
  PROP_ID,  
  PROP_STARTTIME, //for sorting
  LAST_PROPERTY //the size of the GParamSpec properties array
};

static GParamSpec *properties[LAST_PROPERTY] = { NULL, };

typedef struct
{
  GObject parent; //inheritance i.e. parent is a GObject 
  //fields
  char *label; //label to be displayed in listview
  int id; //id  
  int starttime;  //for sorting
  
} DisplayObject;


typedef struct
{
  GObjectClass parent_class;
} DisplayObjectClass;

//declaring a GType
static GType display_object_get_type (void);
//------------------------------------------------------------------
static void config_load_default()
{		
	if(!m_talk) m_talk=1;
	if(!m_talk_at_startup) m_talk_at_startup=1;		
	m_font_name="Sans";
	if(!m_font_size) m_font_size=22;  	
	if(!m_holidays) m_holidays=0;
	if(!m_show_end_time) m_show_end_time=0;	
	
}

static void config_read()
{
	// Clean up previously loaded configuration values	
	m_talk = 1;	
	m_talk_at_startup=1;	
	m_font_name="Sans"; 
	m_font_size=22; 	
	m_holidays=0;
	m_show_end_time=0;		
	
	// Load keys from keyfile
	GKeyFile * kf = g_key_file_new();
	g_key_file_load_from_file(kf, m_config_file, G_KEY_FILE_NONE, NULL);
	m_talk = g_key_file_get_integer(kf, "calendar_settings", "talk", NULL);
	m_talk_at_startup=g_key_file_get_integer(kf, "calendar_settings", "talk_startup", NULL);	
	m_holidays = g_key_file_get_integer(kf, "calendar_settings", "holidays", NULL);	
	m_show_end_time = g_key_file_get_integer(kf, "calendar_settings", "show_end_time", NULL);				
	m_font_name=g_key_file_get_string(kf, "calendar_settings", "font_name", NULL);	
	m_font_size=g_key_file_get_integer(kf, "calendar_settings", "font_size", NULL);	
	g_key_file_free(kf);	
		
}

void config_write()
{
	
	GKeyFile * kf = g_key_file_new();
	g_key_file_set_integer(kf, "calendar_settings", "talk", m_talk);
	g_key_file_set_integer(kf, "calendar_settings", "talk_startup", m_talk_at_startup);		
	g_key_file_set_integer(kf, "calendar_settings", "holidays", m_holidays);
	g_key_file_set_integer(kf, "calendar_settings", "show_end_time", m_show_end_time);	
	g_key_file_set_string(kf, "calendar_settings", "font_name", m_font_name);
	g_key_file_set_integer(kf, "calendar_settings", "font_size", m_font_size);	
	gsize length;
	gchar * data = g_key_file_to_data(kf, &length, NULL);
	g_file_set_contents(m_config_file, data, -1, NULL);
	g_free(data);
	g_key_file_free(kf);
	
}

void config_initialize() {
	
	gchar * config_dir = g_build_filename(g_get_user_config_dir(), CONFIG_DIRNAME, NULL);
	m_config_file = g_build_filename(config_dir, CONFIG_FILENAME, NULL);

	// Make sure config directory exists
	if(!g_file_test(config_dir, G_FILE_TEST_IS_DIR))
		g_mkdir(config_dir, 0777);

	// If a config file doesn't exist, create one with defaults otherwise
	// read the existing one
	if(!g_file_test(m_config_file, G_FILE_TEST_EXISTS))
	{
		config_load_default();
		config_write();
	}
	else
	{
		config_read();
	}

	g_free(config_dir);
	
}

//---------------------------------------------------------------------

G_DEFINE_TYPE (DisplayObject, display_object, G_TYPE_OBJECT)

static void display_object_init (DisplayObject *obj)
{
}

static void display_object_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec){
							
							
  DisplayObject *obj = (DisplayObject *)object;
  //property_id has to map to property enumeration
  switch (property_id)
    {
    case PROP_LABEL:
    g_value_set_string (value, obj->label);
    break;
    case PROP_ID:
    g_value_set_int (value, obj->id);
    break; 
    case PROP_STARTTIME: //for sorting
    g_value_set_int (value, obj->starttime);
    break;    
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
    }
}

static void display_object_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                        GParamSpec   *pspec){
							
  DisplayObject *obj = (DisplayObject *)object;  
 
  //setting the display object properties
  switch (property_id)
    {
    case PROP_LABEL:
      g_free (obj->label);
      obj->label = g_value_dup_string (value); //label defines what is displayed
      break;
    case PROP_ID:
      obj->id = g_value_get_int (value); //get the int from the GValue
      break;     
     case PROP_STARTTIME:
      obj->starttime = g_value_get_int (value);
      break;       
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void display_object_finalize (GObject *obj)
{
  DisplayObject *object = (DisplayObject *)obj;

  g_free (object->label);

  G_OBJECT_CLASS (display_object_parent_class)->finalize (obj);
}

static void display_object_class_init (DisplayObjectClass *class)
{
  
  
  GObjectClass *object_class = G_OBJECT_CLASS (class);
   
   //the object_class has virtual functions called get_property and set_property
   //so you override these to provide your own getter and setter implementations
   
   //override the get_property of the object_class
   object_class->get_property = display_object_get_property;
   //override the set property of the object_class
   object_class->set_property = display_object_set_property;
   object_class->finalize = display_object_finalize;

 //property label used for listview display  and make it read writable
  properties[PROP_LABEL] = g_param_spec_string ("label", "label", "used for list view display",
                                                NULL, G_PARAM_READWRITE);
  properties[PROP_ID] = g_param_spec_int ("id", "id", "id",
                                          0, G_MAXINT, 0, G_PARAM_READWRITE);
     
  properties[PROP_STARTTIME] = g_param_spec_int ("starttime", "starttime", "starttime",
                                          0, G_MAXINT, 0, G_PARAM_READWRITE);
                                          
     
  //install the properties using the object_class, the number of proerties and the properties
  g_object_class_install_properties (object_class, LAST_PROPERTY, properties);
}


//---------------------------------------------------------------------
// create widget
//---------------------------------------------------------------------

static GtkWidget *create_widget (gpointer item, gpointer user_data)
{
  DisplayObject *obj = (DisplayObject *)item;
  GtkWidget *label;

  label = gtk_label_new ("");
  g_object_bind_property (obj, "label", label, "label", G_BINDING_SYNC_CREATE);

  return label;
}

//--------------------------------------------------------------------
// Compare
//---------------------------------------------------------------------

static int compare_items (gconstpointer a, gconstpointer b, gpointer data)
{
  int starttime_a, starttime_b;
  g_object_get ((gpointer)a, "starttime", &starttime_a, NULL);
  g_object_get ((gpointer)b, "starttime", &starttime_b, NULL);
  return starttime_a - starttime_b;
}

//--------------------------------------------------------------------
// calendar functions
//---------------------------------------------------------------------
static int first_day_of_month(int month, int year)
{
    if (month < 3) {
        month += 12;
        year--;
    }
    int century = year / 100;
    year = year % 100;
    return (((13 * (month + 1)) / 5) +
            (century / 4) + (5 * century) +
            year + (year / 4)) % 7;
}

//---------------------------------------------------------------------
// calculate easter
//---------------------------------------------------------------------

GDate* calculate_easter(gint year) {

	GDate *edate;
	
	gint Yr = year;
    gint a = Yr % 19;
    gint b = Yr / 100;
    gint c = Yr % 100;
    gint d = b / 4;
    gint e = b % 4;
    gint f = (b + 8) / 25;
    gint g = (b - f + 1) / 3;
    gint h = (19 * a + b - d - g + 15) % 30;
    gint i = c / 4;
    gint k = c % 4;
    gint L = (32 + 2 * e + 2 * i - h - k) % 7;
    gint m = (a + 11 * h + 22 * L) / 451;
    gint month = (h + L - 7 * m + 114) / 31;
    gint day = ((h + L - 7 * m + 114) % 31) + 1;	
	edate = g_date_new_dmy(day, month, year);

	return edate;
}

//--------------------------------------------------------------------
// new event
//---------------------------------------------------------------------

static void callbk_check_button_allday_toggled (GtkCheckButton *check_button, gpointer user_data)
{
 	
 	GtkWidget *spin_button_start_time; 	
 	GtkWidget *spin_button_end_time;
 	
 	spin_button_start_time =g_object_get_data(G_OBJECT(user_data), "cb_allday_spin_start_time_key");	
	spin_button_end_time= g_object_get_data(G_OBJECT(user_data), "cb_allday_spin_end_time_key");
	
 	if (gtk_check_button_get_active(GTK_CHECK_BUTTON(check_button))){
	gtk_widget_set_sensitive(spin_button_start_time,FALSE);	
	gtk_widget_set_sensitive(spin_button_end_time,FALSE);
	}     
	else{
	gtk_widget_set_sensitive(spin_button_start_time,TRUE);	
	gtk_widget_set_sensitive(spin_button_end_time,TRUE);
	
	}
  
}

static void callbk_new_event_response(GtkDialog *dialog, gint response_id,  gpointer  user_data)
{
	
	if(!GTK_IS_DIALOG(dialog)) { 
	//g_print("New Event Response: not a dialog\n");	
	return;
	}
	
	
	GtkWidget *window = user_data;
	
	GtkEntryBuffer *buffer_title; 	
	GtkWidget *entry_title = g_object_get_data(G_OBJECT(dialog), "entry-title-key");
	GtkEntryBuffer *buffer_location; 
	GtkWidget *entry_location = g_object_get_data(G_OBJECT(dialog), "entry-location-key");
	
	  
	GtkWidget *spin_button_start_time= g_object_get_data(G_OBJECT(dialog), "spin-start-time-key");  
    GtkWidget *spin_button_end_time= g_object_get_data(G_OBJECT(dialog), "spin-end-time-key");       
   
	GtkWidget *check_button_allday= g_object_get_data(G_OBJECT(dialog), "check-button-allday-key"); 
    GtkWidget *check_button_isyearly= g_object_get_data(G_OBJECT(dialog), "check-button-isyearly-key");  
    GtkWidget *check_button_priority= g_object_get_data(G_OBJECT(dialog), "check-button-priority-key");
		
	
	if(response_id==GTK_RESPONSE_OK)
	{
	
	//set data
	buffer_title = gtk_entry_get_buffer (GTK_ENTRY(entry_title));	
	m_title= gtk_entry_buffer_get_text (buffer_title);		
	buffer_location = gtk_entry_get_buffer (GTK_ENTRY(entry_location));	
	m_location= gtk_entry_buffer_get_text (buffer_location);
	int fd;
	Event event;
	event.id =m_db_size;	
	strcpy(event.title,m_title);
	strcpy(event.location,m_location);
	event.year=m_year;
	event.month=m_month;
	event.day=m_day;
	//float start_time, end_time;
	m_start_time =gtk_spin_button_get_value (GTK_SPIN_BUTTON(spin_button_start_time));
	m_end_time =gtk_spin_button_get_value (GTK_SPIN_BUTTON(spin_button_end_time));
		
	event.start_time=m_start_time;
	event.end_time=m_end_time;
	
	if(event.end_time<event.start_time) {
		event.end_time=event.start_time;
	}
    
	event.is_yearly=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_isyearly));
	event.is_allday=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_allday));
	event.priority=gtk_check_button_get_active(GTK_CHECK_BUTTON(check_button_priority));
	
	db_store[m_db_size] =event;			
	m_db_size=m_db_size+1;	
	update_calendar(GTK_WINDOW(window));
	update_store(m_year,m_month,m_day);
	m_id_selection=-1;			
	}		
	gtk_window_destroy(GTK_WINDOW(dialog));	
}


static void callbk_new_event(GtkButton *button, gpointer  user_data){
 
  GtkWidget *window =user_data;
  
  GtkWidget *dialog;  
  gint response; 
  GtkWidget *grid; 
  GtkWidget *box;  
  GtkWidget *label_date;  
  GtkEntryBuffer *buffer; 
  GtkWidget *label_entry_title; 
  GtkWidget *entry_title;
  
  GtkWidget *label_location; 
  GtkWidget *entry_location;		
 
  //Start time
  GtkWidget *label_start_time;  
  GtkWidget *spin_button_start_time;  
  GtkWidget *box_start_time;
  
  //End time
  GtkWidget *label_end_time;  
  GtkWidget *spin_button_end_time; 
  GtkWidget *box_end_time;
  
  
  //Check buttons
  GtkWidget *check_button_allday;	
  GtkWidget *check_button_isyearly;
  GtkWidget *check_button_priority;

  GDate *event_date; 
  event_date = g_date_new();
  event_date = g_date_new_dmy(m_day,m_month,m_year);
  int event_day= g_date_get_day(event_date);
  int event_month= g_date_get_month(event_date);
  int event_year= g_date_get_year(event_date);
  
  gchar * date_str="Event Date: ";
  gchar *day_str = g_strdup_printf("%d",event_day); 
  gchar *month_str = g_strdup_printf("%d",event_month); 
  gchar *year_str = g_strdup_printf("%d",event_year);
  
  date_str =g_strconcat(date_str,day_str,"-",month_str,"-",year_str,NULL); 
  
  dialog = gtk_dialog_new_with_buttons ("New Event", GTK_WINDOW(window),   
  GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_USE_HEADER_BAR,
  "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);
  
  gtk_window_set_title (GTK_WINDOW (dialog), "New Event");
  gtk_window_set_default_size(GTK_WINDOW(dialog),350,100);  
  
  box =gtk_box_new(GTK_ORIENTATION_VERTICAL,1);  
  gtk_window_set_child (GTK_WINDOW (dialog), box);
    
  label_date =gtk_label_new(date_str);  
  
  label_entry_title =gtk_label_new("Event Title"); 
  entry_title =gtk_entry_new(); 
  gtk_entry_set_max_length(GTK_ENTRY(entry_title),100);
  
  label_location =gtk_label_new("Location"); 
  entry_location =gtk_entry_new(); 
  gtk_entry_set_max_length(GTK_ENTRY(entry_location),100);
  
  
  gtk_box_append(GTK_BOX(box), label_date);
  gtk_box_append(GTK_BOX(box), label_entry_title);
  gtk_box_append(GTK_BOX(box), entry_title);
  gtk_box_append(GTK_BOX(box), label_location);
  gtk_box_append(GTK_BOX(box), entry_location);
     
  g_object_set_data(G_OBJECT(dialog), "entry-title-key",entry_title);
  g_object_set_data(G_OBJECT(dialog), "entry-location-key",entry_location);
  g_object_set_data(G_OBJECT(dialog), "dialog-window-key",window); 
 
  //--------------------------------------------------------
  // Start time spin buttons
  //--------------------------------------------------------- 
   
  GtkAdjustment *adjustment_start;
  //value,lower,upper,step_increment,page_increment,page_size
  adjustment_start = gtk_adjustment_new (8.00, 0.0, 23.59, 1.0, 1.0, 0.0);
  //start time spin
  label_start_time =gtk_label_new("Start Time (24 hour) "); 
  spin_button_start_time = gtk_spin_button_new (adjustment_start, 1.0, 2);  
  box_start_time=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,1);
  gtk_box_append (GTK_BOX(box_start_time),label_start_time);
  gtk_box_append (GTK_BOX(box_start_time),spin_button_start_time);  
  gtk_box_append(GTK_BOX(box), box_start_time);
  g_object_set_data(G_OBJECT(dialog), "spin-start-time-key",spin_button_start_time); 
  
  //end time spin
   GtkAdjustment *adjustment_end;
  //value,lower,upper,step_increment,page_increment,page_size
  adjustment_end = gtk_adjustment_new (8.00, 0.0, 23.59, 1.0, 1.0, 0.0);
  label_end_time =gtk_label_new("End Time (24 hour) ");
  spin_button_end_time = gtk_spin_button_new (adjustment_end, 1.0, 2);  
  box_end_time=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,1);
  gtk_box_append (GTK_BOX(box_end_time),label_end_time);
  gtk_box_append (GTK_BOX(box_end_time),spin_button_end_time);  
  gtk_box_append(GTK_BOX(box), box_end_time);
  g_object_set_data(G_OBJECT(dialog), "spin-end-time-key",spin_button_end_time);   
  
  //check buttons 
  check_button_allday = gtk_check_button_new_with_label ("Is All Day");  
  
  g_object_set_data(G_OBJECT(check_button_allday), "cb_allday_spin_start_time_key",spin_button_start_time);
  g_object_set_data(G_OBJECT(check_button_allday), "cb_allday_spin_end_time_key",spin_button_end_time); 

  g_signal_connect_swapped (GTK_CHECK_BUTTON(check_button_allday), "toggled", 
					G_CALLBACK (callbk_check_button_allday_toggled), check_button_allday);
    
  check_button_isyearly = gtk_check_button_new_with_label ("Is Yearly");
  check_button_priority = gtk_check_button_new_with_label ("Is High Priority");
  gtk_box_append(GTK_BOX(box), check_button_allday);
  gtk_box_append(GTK_BOX(box), check_button_isyearly);
  gtk_box_append(GTK_BOX(box), check_button_priority);
  
  g_object_set_data(G_OBJECT(dialog), "check-button-allday-key",check_button_allday);
  g_object_set_data(G_OBJECT(dialog), "check-button-isyearly-key",check_button_isyearly);
  g_object_set_data(G_OBJECT(dialog), "check-button-priority-key",check_button_priority);
  

    GtkStyleContext *context_dialog;	
	gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
	GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
	//finally load style provider 
	gtk_style_context_add_provider(context_dialog,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	//gtk_widget_show(dialog);	  
   
  
  g_signal_connect (dialog, "response", G_CALLBACK (callbk_new_event_response),window);
  
  gtk_window_present (GTK_WINDOW (dialog)); 
  
}
//----------------------------------------------------------------------
// Edit event
//----------------------------------------------------------------------

void callbk_edit_event_response(GtkDialog *dialog, gint response_id,  gpointer  user_data)
{
	  
  
	
	
	if(!GTK_IS_DIALOG(dialog)) { 
	g_print("New Event Response: not a dialog\n");	
	return;
	}
		
	GtkWidget *window = user_data;	
	GtkEntryBuffer *buffer_title; 	
	GtkWidget *entry_title = g_object_get_data(G_OBJECT(dialog), "entry-title-key");
	GtkEntryBuffer *buffer_location; 
	GtkWidget *entry_location = g_object_get_data(G_OBJECT(dialog), "entry-location-key");
	
	GtkWidget *spin_button_start_time= g_object_get_data(G_OBJECT(dialog), "spin-start-time-key");  
    GtkWidget *spin_button_end_time= g_object_get_data(G_OBJECT(dialog), "spin-end-time-key");       
    
    GtkWidget *check_button_allday= g_object_get_data(G_OBJECT(dialog), "check-button-allday-key"); 
    GtkWidget  *check_button_isyearly= g_object_get_data(G_OBJECT(dialog), "check-button-isyearly-key");  
    GtkWidget  *check_button_priority= g_object_get_data(G_OBJECT(dialog), "check-button-priority-key");
	
			
	if(response_id==GTK_RESPONSE_OK)
	{
	
	//set data
	buffer_title = gtk_entry_get_buffer (GTK_ENTRY(entry_title));	
	m_title= gtk_entry_buffer_get_text (buffer_title);	
		buffer_location = gtk_entry_get_buffer (GTK_ENTRY(entry_location));	
	m_location= gtk_entry_buffer_get_text (buffer_location);	
		
	//insert cahnge into database	
	Event event;
    for(int i=0; i<m_db_size; i++)
    {
	event=db_store[i];
	if(event.id==m_id_selection){		
	
	strcpy(event.title, m_title); 
	strcpy(event.location, m_location); 	
	event.year=m_year;
	event.month=m_month;
	event.day=m_day;	
	//float start_time, end_time;
	m_start_time =gtk_spin_button_get_value (GTK_SPIN_BUTTON(spin_button_start_time));
	m_end_time =gtk_spin_button_get_value (GTK_SPIN_BUTTON(spin_button_end_time));		
		
	event.start_time=m_start_time;
	event.end_time=m_end_time;
	
	if(event.end_time<event.start_time) {
		event.end_time=event.start_time;
	}
	//GTK_IS_CHECK_BUTTON
	event.is_yearly=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_isyearly));
	event.is_allday=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_allday));
	event.priority=gtk_check_button_get_active(GTK_CHECK_BUTTON(check_button_priority));	
	db_store[i]=event;	
	break;	
	}
	
    }
		
	update_calendar(GTK_WINDOW(window));
	update_store(m_year,m_month,m_day);
	m_row_index=-1;
	m_id_selection=-1;
	
	}
			
	gtk_window_destroy(GTK_WINDOW(dialog));	
}




//----------------------------------------------------------------------
static void callbk_edit_event(GtkButton *button, gpointer  user_data){
	
	
	if (m_id_selection==-1) return;
		
	GtkWindow *window =user_data;
	
	GtkWidget *dialog;  
	gint response; 
	GtkWidget *grid; 
	GtkWidget *box;  
	GtkWidget *label_date;  
	GtkEntryBuffer *buffer_title; 
	GtkEntryBuffer *buffer_location; 
	
	GtkWidget *label_entry_title; 
	GtkWidget *entry_title;	
	
	GtkWidget *label_location; 
    GtkWidget *entry_location;	
	
	//Start time
	GtkWidget *label_start_time;
	GtkWidget *spin_button_start_time;	
	GtkWidget *box_start_time;
	
	//End time
	GtkWidget *label_end_time;	
	GtkWidget *spin_button_end_time;	
	GtkWidget *box_end_time;
	
	//Check buttons
	GtkWidget *check_button_allday;	
	GtkWidget *check_button_isyearly;
	GtkWidget *check_button_priority;
	
	GDate *event_date; 
	event_date = g_date_new();
	event_date = g_date_new_dmy(m_day,m_month,m_year);
	int event_day= g_date_get_day(event_date);
	int event_month= g_date_get_month(event_date);
	int event_year= g_date_get_year(event_date);
	
	gchar * date_str="Event Date: ";
	gchar *day_str = g_strdup_printf("%d",event_day); 
	gchar *month_str = g_strdup_printf("%d",event_month); 
	gchar *year_str = g_strdup_printf("%d",event_year);
	
	date_str =g_strconcat(date_str,day_str,"-",month_str,"-",year_str,NULL); 
	
    dialog = gtk_dialog_new_with_buttons ("Edit Event", GTK_WINDOW(window), 
	GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_USE_HEADER_BAR,
	"OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);
	
	gtk_window_set_title (GTK_WINDOW (dialog), "Edit Event");
	gtk_window_set_default_size(GTK_WINDOW(dialog),350,100);  
	
	box =gtk_box_new(GTK_ORIENTATION_VERTICAL,1);  
	gtk_window_set_child (GTK_WINDOW (dialog), box);
		
	//find event in database    
	Event e;
    for(int i=0; i<m_db_size; i++)
    {
	e=db_store[i];	
	if(e.id==m_id_selection){	
	m_title =e.title;
	m_location =e.location;
	m_year=e.year;
	m_month=e.month;
	m_day=e.day;            
	break;	
	}	
    }
	
	
	label_date =gtk_label_new(date_str);  
	
	label_entry_title =gtk_label_new("Event Title"); 
    entry_title =gtk_entry_new(); 
    gtk_entry_set_max_length(GTK_ENTRY(entry_title),100);
    buffer_title=gtk_entry_buffer_new(m_title,-1); //show  title
	gtk_entry_set_buffer(GTK_ENTRY(entry_title),buffer_title);
	
    label_location =gtk_label_new("Location"); 
	entry_location =gtk_entry_new(); 
	gtk_entry_set_max_length(GTK_ENTRY(entry_location),100);
	buffer_location=gtk_entry_buffer_new(m_location,-1); //show  title
	gtk_entry_set_buffer(GTK_ENTRY(entry_location),buffer_location);			
	
	
	 gtk_box_append(GTK_BOX(box), label_date);
     gtk_box_append(GTK_BOX(box), label_entry_title);
     gtk_box_append(GTK_BOX(box), entry_title);
     gtk_box_append(GTK_BOX(box), label_location);
     gtk_box_append(GTK_BOX(box), entry_location);
     
  g_object_set_data(G_OBJECT(dialog), "entry-title-key",entry_title);
  g_object_set_data(G_OBJECT(dialog), "entry-location-key",entry_location);
  g_object_set_data(G_OBJECT(dialog), "dialog-window-key",window); 
	
	
  //start time spin
  GtkAdjustment *adjustment_start;
  //value,lower,upper,step_increment,page_increment,page_size
  adjustment_start = gtk_adjustment_new (8.00, 0.0, 23.59, 1.0, 1.0, 0.0);
  //start time spin
  label_start_time =gtk_label_new("Start Time (24 hour) "); 
  spin_button_start_time = gtk_spin_button_new (adjustment_start, 1.0, 2); 
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button_start_time),e.start_time);	 
  box_start_time=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,1);
  gtk_box_append (GTK_BOX(box_start_time),label_start_time);
  gtk_box_append (GTK_BOX(box_start_time),spin_button_start_time);  
  gtk_box_append(GTK_BOX(box), box_start_time);
  g_object_set_data(G_OBJECT(dialog), "spin-start-time-key",spin_button_start_time); 
  
  //end time spin
   GtkAdjustment *adjustment_end;
  //value,lower,upper,step_increment,page_increment,page_size
  adjustment_end = gtk_adjustment_new (8.00, 0.0, 23.59, 1.0, 1.0, 0.0);
  label_end_time =gtk_label_new("End Time (24 hour) ");
  spin_button_end_time = gtk_spin_button_new (adjustment_end, 1.0, 2); 
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button_end_time),e.end_time);	 
  box_end_time=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,1);
  gtk_box_append (GTK_BOX(box_end_time),label_end_time);
  gtk_box_append (GTK_BOX(box_end_time),spin_button_end_time);  
  gtk_box_append(GTK_BOX(box), box_end_time);
  g_object_set_data(G_OBJECT(dialog), "spin-end-time-key",spin_button_end_time);  
  
  
  //check buttons 
  check_button_allday = gtk_check_button_new_with_label ("Is All Day");
  gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_allday), e.is_allday);  
  
  g_object_set_data(G_OBJECT(check_button_allday), "cb_allday_spin_start_time_key",spin_button_start_time);
  g_object_set_data(G_OBJECT(check_button_allday), "cb_allday_spin_end_time_key",spin_button_end_time); 
  
  g_signal_connect_swapped (GTK_CHECK_BUTTON(check_button_allday), "toggled", 
  G_CALLBACK (callbk_check_button_allday_toggled), check_button_allday);
  
  if(e.is_allday) {
  gtk_widget_set_sensitive(spin_button_start_time,FALSE);	   
  gtk_widget_set_sensitive(spin_button_end_time,FALSE);	    
  }
  else {
  gtk_widget_set_sensitive(spin_button_start_time,TRUE);		
  gtk_widget_set_sensitive(spin_button_end_time,TRUE);		
  }
    
  check_button_isyearly = gtk_check_button_new_with_label("Is Yearly");
  check_button_priority = gtk_check_button_new_with_label ("Is High Priority");
  gtk_box_append(GTK_BOX(box), check_button_allday);
  gtk_box_append(GTK_BOX(box), check_button_isyearly);
  gtk_box_append(GTK_BOX(box), check_button_priority);
  
  gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_isyearly), e.is_yearly);
  
  gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_priority), e.priority);
	
  g_object_set_data(G_OBJECT(dialog), "check-button-allday-key",check_button_allday);
  g_object_set_data(G_OBJECT(dialog), "check-button-isyearly-key",check_button_isyearly);
  g_object_set_data(G_OBJECT(dialog), "check-button-priority-key",check_button_priority);
    
   GtkStyleContext *context_dialog;	
	gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
	GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
	//finally load style provider 
	gtk_style_context_add_provider(context_dialog,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	//gtk_widget_show(dialog);	    
  
  g_signal_connect (dialog, "response", G_CALLBACK (callbk_edit_event_response),window);
  
  gtk_window_present (GTK_WINDOW (dialog)); 

}
 
static void callbk_delete_selected(GtkButton *button, gpointer  user_data){
		
	if (m_row_index==-1) return;
	
	GtkWindow *window =user_data;
	
	//remove event from db  
	int j=0;
	Event e;
	for(int i=0; i<m_db_size; i++)
	{
	e=db_store[i];      
	if(e.id==m_id_selection){
	continue;
	}
	db_store[j] = db_store[i];
	j++;
	}
	
	////Decrement db size
	m_db_size=m_db_size-1;
	
	g_list_store_remove (m_store, m_row_index); //remove selected
	update_calendar(GTK_WINDOW(window));
	update_store(m_year, m_month, m_day); 
	
}

//----------------------------------------------------------------------
// flat csv database functions
//----------------------------------------------------------------------

int break_fields(char *s, char** data, int n)
{
	//n = number of fields
	//assumes comma delimiter
	
	int fields=0;
	int i;
	char *start=s; //start at begiining of string
	char *end=s; //walking pointer
	
	for(i=0; i<n; i++)
	{
		while(*end !=',' && *end != '\0') {
			end++;			
		}
		
		if(*end =='\0') {
			
			data[i]=(char*) malloc(strlen(start)+1);
			strcpy(data[i],start);
			fields++;
			break;
		}
		else if (*end ==',') {
			*end ='\0';
			data[i]=(char*) malloc(strlen(start)+1);
			strcpy(data[i],start);
			start=end+1;
			end=start;
			fields++;
		}
		
	}
		
	return fields;
	
} 

void load_csv_file(){
	
	
	int field_num =11;
	
	char *data[field_num]; // fields
	int i = 0; //counter	
    int total_num_lines = 0; //total number of lines
    int ret;
    
	GFile *file;
    GFileInputStream *file_stream=NULL;   
	GDataInputStream *input = NULL;
    
    file = g_file_new_for_path("events.csv");

	file_stream = g_file_read(file, NULL, NULL);	
	if(!file_stream) {
		g_print("error: unable to open database\n");
		return;
	}
	
	input = g_data_input_stream_new (G_INPUT_STREAM (file_stream));
		
	while (TRUE) {
		
		char *line;
		line = g_data_input_stream_read_line (input, NULL, NULL, NULL);		
		if (line == NULL) break;
		
		Event e;  
		      
        ret=break_fields(line,data,field_num);
        //g_print("break_fields return value %d\n",ret);
		
		for (int j=0; j<field_num;j++) {
			
			
			if (j==0) e.id =m_db_size; 
			if (j==1) strcpy(e.title,data[j]);
			if (j==2) strcpy(e.location,data[j]);
			if (j==3) e.year=atoi(data[j]);
			if (j==4) e.month=atoi(data[j]);
			if (j==5) e.day=atoi(data[j]);				          
			          
			if (j==6) e.start_time=atof(data[j]);
			if (j==7) e.end_time=atof(data[j]);          
			
			if (j==8) e.priority=atoi(data[j]);
			if (j==9) e.is_yearly=atoi(data[j]);
			if (j==10) e.is_allday=atoi(data[j]);
			
			//printf("data[%d] = %s\n",j, data[j]);
			free(data[j]);
			
		}
		
		db_store[m_db_size] =e;			
		m_db_size=m_db_size+1;		
		if(m_db_size>max_records)
		{
			g_print("Error: max number of records exceeded\n");
			exit(1);
		}
		i++;		
	}
	
	total_num_lines=i;
    //g_print("total_number_of_lines =%d\n",total_num_lines);	
	g_object_unref (file_stream);	
	g_object_unref (file);	
		
}

void save_csv_file(){

    GFile *file;
	gchar *file_name ="events.csv";

	GFileOutputStream *file_stream;
	GDataOutputStream *data_stream;
	
	//g_print("Saving csv data with filename = %s\n",file_name);
	
	file = g_file_new_for_path (file_name);
	file_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL);
	
	if (file_stream == NULL) {
		g_object_unref (file);
		g_print("error: unable to open abd save database file\n");
		return;
	}

	data_stream = g_data_output_stream_new (G_OUTPUT_STREAM (file_stream));
	
	
	for (int i=0; i<m_db_size; i++)
	{
	char *line="";  
	Event e;
	e=db_store[i];     
	//g_print("Save CSV: e.id =%d e.title =%s date =%d-%d-%d\n",e.id,e.title,e.day,e.month,e.year);
	
	gchar *id_str = g_strdup_printf("%d", e.id); 
	gchar *year_str = g_strdup_printf("%d", e.year); 
	gchar *month_str = g_strdup_printf("%d", e.month);
	gchar *day_str = g_strdup_printf("%d", e.day); 
	
	gchar *starttime_str = g_strdup_printf("%0.2f", e.start_time); 
	gchar *endtime_str = g_strdup_printf("%0.2f", e.end_time);
	
	gchar *priority_str = g_strdup_printf("%d", e.priority); 
	gchar *isyearly_str = g_strdup_printf("%d", e.is_yearly); 
	gchar *isallday_str = g_strdup_printf("%d", e.is_allday); 
		
	
	line =g_strconcat(line,
	id_str,",",
	e.title,",",
	e.location,",",
	year_str,",",
	month_str,",",
	day_str,",",
	starttime_str,",",
	endtime_str,",",	
	priority_str,",",
	isyearly_str,",",
	isallday_str,",",
	"\n", NULL);
	
	g_data_output_stream_put_string (data_stream, line, NULL, NULL)	;
	}
	
	g_object_unref (data_stream);
	g_object_unref (file_stream);
	g_object_unref (file);
	
}
 



//----------------------------------------------------------------------

static void callbk_day_selected (GtkButton *button, gpointer user_data)
{
   
  GDate *date =user_data;  
  GtkWidget *window = g_object_get_data(G_OBJECT(button), "button-window-key");  
  int day =g_date_get_day(date);
  int month =g_date_get_month(date);  
  int year =g_date_get_year(date);
  m_day =day;
  m_month=month;
  m_year=year;  
  update_calendar(GTK_WINDOW(window)); 
  update_store(m_year,m_month,m_day);
}

static void callbk_next_month(GtkButton *button, gpointer user_data)
{
	
	GtkWindow *window = user_data;	
	
	m_month=m_month+1;
	
	if (m_month >= 13) {
	m_month = 1;
	m_year =m_year+1;
	}
	m_id_selection=-1;
	m_row_index=-1;
	update_calendar(GTK_WINDOW (window));
}

static void callbk_prev_month(GtkButton *button, gpointer user_data)
{
	
	GtkWindow *window = user_data;	
	
	m_month=m_month-1;
	
	if (m_month < 1)
	{
	m_month = 12;
	m_year=m_year-1;
	}
	m_id_selection=-1;
	m_row_index=-1;
	update_calendar(GTK_WINDOW (window));
	
}
//---------------------------------------------------------------------
//list box methods
//---------------------------------------------------------------------
static void add_separator (GtkListBoxRow *row, GtkListBoxRow *before, gpointer data)
{
  if (!before)
    return;

  gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}
//---------------------------------------------------------------------
static void callbk_row_activated (GtkListBox    *listbox,
                                  GtkListBoxRow *row,
                                  gpointer       user_data){
   
  
  m_row_index = gtk_list_box_row_get_index (row);
  //g_print ("m_row_index = %d\n", m_row_index);
  
 
  DisplayObject *obj = g_list_model_get_item (G_LIST_MODEL (m_store), m_row_index); 
   
   if(obj==NULL) return; 
   
   gint id_value;
   gchar* label_value;
  
   g_object_get (obj, "id", &id_value, NULL);
   g_object_get (obj, "label", &label_value, NULL);
  //g_print("DisplayObject id = %d\n",id_value);
  //g_print("DisplayObject label = %s\n",label_value);
  
  m_id_selection=id_value;
  //g_print("m_id_selection = %d\n",m_id_selection);    
  //g_print("m_db_size =%d\n",m_db_size);
  
}
//---------------------------------------------------------------------
static void update_store(int year, int month, int day) {	
   
  g_list_store_remove_all (m_store);//clear
 
  Event e; 
  int start_time=0; 
  for (int i=0; i<m_db_size; i++)
  {
  e=db_store[i]; 
 
  if ((year==e.year && month ==e.month && day==e.day) || (e.is_yearly && month==e.month && day==e.day))
  {  
  
  DisplayObject *obj; 
  char *time_str="";
  char *title_str="";
  char* starthour_str="";
  char *startmin_str="";
  char* endhour_str="";
  char *endmin_str="";
  
  float integral_part, fractional_part;
  fractional_part = modff(e.start_time, &integral_part);  
  int start_hour =(int) integral_part; //start_hour
  fractional_part=round(fractional_part *100);
  int start_min=(int) (fractional_part); //start_min
  
  if (e.start_time<=12.59) //am
  {    
    starthour_str = g_strdup_printf("%d", start_hour); 
    startmin_str = g_strdup_printf("%d", start_min);  
    
    if(start_min==0)
    {
    time_str = g_strconcat(time_str, starthour_str," am ",NULL);
    }
    
    else if (start_min <10){       
     time_str = g_strconcat(time_str, starthour_str,":0", startmin_str," am ",NULL);
    }
   
    else {
	 time_str = g_strconcat(time_str, starthour_str,":", startmin_str," am ",NULL);	 
    }
   }
    
   else { //pm 
    
    start_hour =start_hour-12;    
    starthour_str = g_strdup_printf("%d", start_hour); 
    startmin_str = g_strdup_printf("%d", start_min); 
   
    if(start_min==0)
    {
    time_str = g_strconcat(time_str, starthour_str," pm ",NULL);
    }
    else if (start_min <10){       
     time_str = g_strconcat(time_str, starthour_str,":0", startmin_str," pm ",NULL);
    }
    else {
	 time_str = g_strconcat(time_str, starthour_str,":", startmin_str," pm ",NULL);
	}
 } //else pm
 
 // now end time (optional)

 if(m_show_end_time){
	 
  float integral_part_end, fractional_part_end;
  fractional_part_end = modff(e.end_time, &integral_part_end);  
  int end_hour =(int) integral_part_end; //end_hour
  fractional_part_end=round(fractional_part_end *100);
  int end_min=(int) (fractional_part_end); //start_min
	 
  if (e.end_time<=12.59) //am
  {

    endhour_str = g_strdup_printf("%d", end_hour); 
    endmin_str = g_strdup_printf("%d", end_min);  
    
    if(end_min==0)
    {
    time_str = g_strconcat(time_str,"to ", endhour_str," am ",NULL);
    }
    
    else if (end_min <10){       
     time_str = g_strconcat(time_str,"to ",  endhour_str," to :0", endmin_str," am ",NULL);
    }
   
    else {
	 time_str = g_strconcat(time_str, "to ", endhour_str,":", endmin_str," am ",NULL);	   
    
    }
   }
    
   else { //pm 
    
    end_hour =end_hour-12;
    endhour_str = g_strdup_printf("%d", end_hour); 
    endmin_str = g_strdup_printf("%d", end_min);  	
    
    if(end_min==0)
    {
    time_str = g_strconcat(time_str,"to ", endhour_str," pm ",NULL);
    }
    else if (start_min <10){       
     time_str = g_strconcat(time_str, "to ", endhour_str,":0", endmin_str," pm ",NULL);
    }
    else {
	 time_str = g_strconcat(time_str, "to ", endhour_str,":", endmin_str," pm ",NULL);
	}
 } //else pm
 
 }//show_end_time 
  
  
  if (e.is_allday) {
	  time_str="All day. ";
  }
  else {
	 time_str = g_strconcat(time_str,NULL);
  }
    
  char *display_str ="";
  title_str=e.title;  
  if(strlen(e.location) ==0)
  {
	display_str = g_strconcat(display_str,time_str,title_str,".\n", NULL);  
  }
  else { 
   display_str = g_strconcat(display_str,time_str,title_str, " at ",e.location, ".", NULL);
  }
  
  if(e.priority) {	  
	  display_str=g_strconcat(display_str, " High Priority.", NULL);
  }
  
  display_str=g_strconcat(display_str, "\n", NULL);
    
   start_hour =(int) e.start_time;	
   double fract1 = round(e.start_time-floor(e.start_time));  	
   start_min=fract1 *100;
  start_time = 60 * 60 * start_hour + 60 * start_min; //seconds
  
  obj = g_object_new (display_object_get_type (),
						//"id",     e.id,
						"id",     e.id,
						"label",  display_str, 
						"starttime", start_time, 
						
						NULL); 
  
  g_list_store_insert_sorted(m_store, obj, compare_items, NULL); 
  g_object_unref (obj);
  } //if selected date 
  } //for
}

static void set_button_blue(GtkButton *button){
	
  GtkStyleContext *context_button;
  GtkCssProvider *cssProvider; 	
  cssProvider = gtk_css_provider_new();	
  gtk_widget_set_name (GTK_WIDGET(button), "cssView");
   
  gchar* css_str = g_strdup_printf (
  "#cssView {" 
  "color: blue;" 
  "font-weight: bold;"
  "border-top-width: 3px;" 
  "border-top-color: blue;"
  "border-left-width: 3px;" 
  "border-left-color: blue;"
  "border-bottom-width: 3px;" 
  "border-bottom-color: blue;"
  "border-right-width: 3px;" 
  "border-right-color: blue;" 
  " }");
  
    
  gtk_css_provider_load_from_data(cssProvider, css_str,-1);   
  context_button= gtk_widget_get_style_context(GTK_WIDGET(button));		 
  gtk_style_context_add_provider(context_button, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
}


//---------------------------------------------------------------------
static void set_button_green(GtkButton *button){
	
  GtkStyleContext *context_button;
  GtkCssProvider *cssProvider; 	
  cssProvider = gtk_css_provider_new();	
  gtk_widget_set_name (GTK_WIDGET(button), "cssView");
 
  
  gchar* css_str = g_strdup_printf (
  "#cssView {" 
  "color: green;" 
  "font-weight: bold;"
  "border-top-width: 3px;" 
  "border-top-color: green;"
  "border-left-width: 3px;" 
  "border-left-color: green;"
  "border-bottom-width: 3px;" 
  "border-bottom-color: green;"
  "border-right-width: 3px;" 
  "border-right-color: green;" 
  " }");
  
  gtk_css_provider_load_from_data(cssProvider, css_str,-1);   
  context_button= gtk_widget_get_style_context(GTK_WIDGET(button));		 
  gtk_style_context_add_provider(context_button, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
}

static void set_button_red_with_borders(GtkButton *button){
	
  GtkStyleContext *context_button;
  GtkCssProvider *cssProvider; 	
  cssProvider = gtk_css_provider_new();	
  gtk_widget_set_name (GTK_WIDGET(button), "cssView");  
    
  gchar* css_str = g_strdup_printf (
  "#cssView {" 
  "color: red;" 
  "font-weight: bold;"
  "border-top-width: 3px;" 
  "border-top-color: red;"
  "border-left-width: 3px;" 
  "border-left-color: red;"
  "border-bottom-width: 3px;" 
  "border-bottom-color: red;"
  "border-right-width: 3px;" 
  "border-right-color: red;" 
  " }");  
  
  gtk_css_provider_load_from_data(cssProvider, css_str,-1);   
  context_button= gtk_widget_get_style_context(GTK_WIDGET(button));		 
  gtk_style_context_add_provider(context_button, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
}

static void set_button_red(GtkButton *button){
	
  GtkStyleContext *context_button;
  GtkCssProvider *cssProvider; 	
  cssProvider = gtk_css_provider_new();	
  gtk_widget_set_name (GTK_WIDGET(button), "cssView");
    
  gchar* css_str = g_strdup_printf (
  "#cssView {" 
  "color: red;" 
  "font-weight: bold;"  
  " }");
  
  
  gtk_css_provider_load_from_data(cssProvider, css_str,-1);   
  context_button= gtk_widget_get_style_context(GTK_WIDGET(button));		 
  gtk_style_context_add_provider(context_button, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
}
//----------------------------------------------------------------------
static void reset_marked_dates() {
	
  //reset marked dates
  int i=0;
  for (i=0;i<31;i++) marked_date[i] = FALSE;
  num_marked_dates = 0;
  
}

static void update_marked_dates(int month, int year) {
	
  //reset marked dates 
  num_marked_dates = 0;  
  Event e;  
  for (int i=0; i<m_db_size; i++)
  {
  e=db_store[i];  
  if ((e.month==month && e.year ==year)  || (e.is_yearly && month==e.month))  
  {
	  marked_date[e.day-1]=TRUE; //zero index so 1=0
	  num_marked_dates= num_marked_dates+1;
  } //if
  } //for 
}


gchar* get_css_string() {
	
	char *size_str = g_strdup_printf("%i", m_font_size); 
	size_str =g_strconcat(size_str,"px;", NULL);	
	const gchar* font_name = m_font_name; 
    font_name =g_strconcat(font_name,";", NULL); 	
	gchar* css_str = g_strdup_printf ("#cssView {font-family: %s font-size: %s }", font_name, size_str);
	return css_str;	
}
//--------------------------------------------------------------------
// About
//----------------------------------------------------------------------
static void callbk_about_close (GtkAboutDialog *dialog, gint response_id, gpointer user_data)
{
  // destroy the about dialog 
  gtk_window_destroy(GTK_WINDOW(dialog));
  
}
//-------------------------------------------------------------------
// About callbk
//-------------------------------------------------------------------
static void callbk_about(GSimpleAction * action, GVariant *parameter, gpointer user_data){
	
	
	GtkWindow *window = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (user_data),
                                                 GTK_TYPE_WINDOW));
		
	const gchar *authors[] = {"Alan Crispin", NULL};		
	GtkWidget *about_dialog;
	about_dialog = gtk_about_dialog_new();	
	gtk_window_set_transient_for(GTK_WINDOW(about_dialog),GTK_WINDOW(window));
	gtk_widget_set_size_request(about_dialog, 200,200);
    gtk_window_set_modal(GTK_WINDOW(about_dialog),TRUE);	
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about_dialog), "Gtk Talk Calendar");
	gtk_about_dialog_set_version (GTK_ABOUT_DIALOG(about_dialog), "Gtk4 Version 1.0");
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about_dialog),"Copyright © 2021");
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about_dialog),"Calendar Assistant"); 
	gtk_about_dialog_set_license_type (GTK_ABOUT_DIALOG(about_dialog), GTK_LICENSE_GPL_3_0);
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(about_dialog),"https://github.com/crispinalan/"); 
	gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(about_dialog),"Talk Calendar Website");
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about_dialog), authors);
	//gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(about_dialog), NULL);	
	gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(about_dialog), "x-office-calendar");
	
	GtkStyleContext *context_about_dialog;	
	gtk_widget_set_name (GTK_WIDGET(about_dialog), "cssView"); 
	GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_about_dialog = gtk_widget_get_style_context(GTK_WIDGET(about_dialog));	
	//finally load style provider 
	gtk_style_context_add_provider(context_about_dialog,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	gtk_widget_show(about_dialog);	
		
}

//---------------------------------------------------------------------
// speak
//---------------------------------------------------------------------
static gpointer thread_speak_func(gpointer user_data)
{     
    gchar *text =user_data;   
    gchar * command_str ="espeak --stdout";
    gchar *m_str = g_strdup_printf("%i", m_speed); 
    gchar *speed_str ="-s ";
    speed_str= g_strconcat(speed_str,m_str, NULL);   
    command_str= g_strconcat(command_str," '",speed_str,"' "," '",text,"' ", "| aplay", NULL);
    system(command_str);
    g_mutex_unlock (&lock); //thread mutex unlock 
    return NULL;   
}


void callbk_font_response(GtkDialog *dialog, gint response_id,  gpointer  user_data){
	
	if(!GTK_IS_DIALOG(dialog)) { 
	g_print("Font callbk: error not a dialog\n");	
	return;
	}
	
	GtkWidget *window = user_data;
	
	if(response_id==GTK_RESPONSE_OK)
	{
	  char* font_str=gtk_font_chooser_get_font (GTK_FONT_CHOOSER(dialog));
	  //g_print("OK button clicked: selected font = %s\n",font_str);
	  
	  PangoFontFamily* font_family =gtk_font_chooser_get_font_family(GTK_FONT_CHOOSER(dialog));	  
	  const char* font_name =pango_font_family_get_name (font_family);
	
	  m_font_name =font_name;
	  //g_print("m_font name = %s\n",m_font_name); 
	  	  
	  int font_size =gtk_font_chooser_get_font_size(GTK_FONT_CHOOSER(dialog));
	  
	  m_font_size=font_size/1024;
	  //g_print("m_font_size = %d\n", m_font_size);
	  
	  config_write();
	  update_calendar(GTK_WINDOW(window));
	  update_header(GTK_WINDOW(window));
	  
	}
	
	gtk_window_destroy(GTK_WINDOW(dialog));	
	
}

static void callbk_font(GSimpleAction* action, GVariant *parameter,gpointer user_data)
{
	GtkWindow *window = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (user_data),
	GTK_TYPE_WINDOW));
	
	if (!GTK_IS_WINDOW (window))
	{      
	g_print("callbk font: error not a window\n");
	return;      
	}
	
	GtkWidget *dialog;
    dialog = g_object_new (GTK_TYPE_FONT_CHOOSER_DIALOG,
                         "title", "Choose a font",
                         "transient-for", window,
                         NULL);
                         
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);  
  const char *size_str = g_strdup_printf("%i", m_font_size); 		
  const gchar* font = m_font_name; 
  font =g_strconcat(font," ",size_str, NULL); 
  //g_print("set font = %s\n", font);  
  gtk_font_chooser_set_font (GTK_FONT_CHOOSER(dialog), font);    
  gtk_window_present (GTK_WINDOW (dialog));  
  g_signal_connect (dialog, "response", G_CALLBACK (callbk_font_response),window);
}

void callbk_preferences_response(GtkDialog *dialog, gint response_id,  gpointer  user_data)
{
		
	if(!GTK_IS_DIALOG(dialog)) { 
	g_print("Preferences Response: not a dialog\n");	
	return;
	}
	
	GtkWidget *window = user_data;
	
	GtkWidget *check_button_talk= g_object_get_data(G_OBJECT(dialog), "check-button-talk-key"); 
    GtkWidget *check_button_talk_startup= g_object_get_data(G_OBJECT(dialog), "check-button-talk-startup-key");  
    GtkWidget *check_button_holidays= g_object_get_data(G_OBJECT(dialog), "check-button-holidays-key");
    GtkWidget *check_button_end_time= g_object_get_data(G_OBJECT(dialog), "check-button-display-end-time-key");
    
	if(response_id==GTK_RESPONSE_OK)
	{
	m_talk=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_talk));
	m_talk_at_startup=gtk_check_button_get_active (GTK_CHECK_BUTTON(check_button_talk_startup));
	m_holidays=gtk_check_button_get_active(GTK_CHECK_BUTTON(check_button_holidays));
	m_show_end_time=gtk_check_button_get_active(GTK_CHECK_BUTTON(check_button_end_time));
	config_write();	
	update_calendar(GTK_WINDOW(window));
	update_store(m_year,m_month,m_day);
	}	
	gtk_window_destroy(GTK_WINDOW(dialog));	
}

static void callbk_preferences(GSimpleAction* action, GVariant *parameter,gpointer user_data)
{
	GtkWidget *window =user_data;
	
	GtkWidget *dialog; 
	GtkWidget *box; 
	gint response; 
	
	//Check buttons
	GtkWidget *check_button_talk;	
	GtkWidget *check_button_talk_startup;
	GtkWidget *check_button_holidays;
	GtkWidget *check_button_end_time;
	
	dialog = gtk_dialog_new_with_buttons ("New Event", GTK_WINDOW(window),   
	GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_USE_HEADER_BAR,
	"OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);
	
	gtk_window_set_title (GTK_WINDOW (dialog), "Preferences");
	gtk_window_set_default_size(GTK_WINDOW(dialog),350,100);  
	
	box =gtk_box_new(GTK_ORIENTATION_VERTICAL,1);  
	gtk_window_set_child (GTK_WINDOW (dialog), box);
	
	
	check_button_talk = gtk_check_button_new_with_label ("Talk");
	check_button_talk_startup = gtk_check_button_new_with_label ("Talk At Startup");
	check_button_holidays = gtk_check_button_new_with_label ("Show UK Public Holidays");
	check_button_end_time = gtk_check_button_new_with_label ("Display End Time");
	
	gtk_box_append(GTK_BOX(box), check_button_talk);
	gtk_box_append(GTK_BOX(box), check_button_talk_startup);
	gtk_box_append(GTK_BOX(box), check_button_holidays);
	gtk_box_append(GTK_BOX(box), check_button_end_time);
	
	
	g_object_set_data(G_OBJECT(dialog), "check-button-talk-key",check_button_talk);
	g_object_set_data(G_OBJECT(dialog), "check-button-talk-startup-key",check_button_talk_startup);
	g_object_set_data(G_OBJECT(dialog), "check-button-holidays-key",check_button_holidays);
	g_object_set_data(G_OBJECT(dialog), "check-button-display-end-time-key",check_button_end_time);
	
	
	gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_talk), m_talk);
	gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_talk_startup), m_talk_at_startup);
	gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_holidays), m_holidays);
	gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button_end_time), m_show_end_time);	
	
	
	GtkStyleContext *context_dialog;	
	gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
	GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
	//finally load style provider 
	gtk_style_context_add_provider(context_dialog,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	gtk_widget_show(dialog);
	
	g_signal_connect (dialog, "response", G_CALLBACK (callbk_preferences_response),window);
	
	gtk_window_present (GTK_WINDOW (dialog)); 	
}

//-----------------------------------------------------------------
// Keyboard shortcuts
//-----------------------------------------------------------------

static void callbk_shortcuts(GSimpleAction * action, GVariant *parameter, gpointer user_data){

		
	
	GtkWidget *window =user_data;
	GtkWidget *dialog; 
	GtkWidget *box; 
	gint response; 
	
	//labels 	
	GtkWidget *label_speak_sc;
	GtkWidget *label_home_sc;
	GtkWidget *label_about_sc;	
	GtkWidget *label_version_sc;
	GtkWidget *label_quit_sc;
	
	dialog = gtk_dialog_new_with_buttons ("Information", GTK_WINDOW(window), 
	GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
	"Close", GTK_RESPONSE_CLOSE,
	NULL);                                         
	
	gtk_window_set_default_size(GTK_WINDOW(dialog),380,100);  
	
	box =gtk_box_new(GTK_ORIENTATION_VERTICAL,1);  
	gtk_window_set_child (GTK_WINDOW (dialog), box);
		
	 	
	label_speak_sc=gtk_label_new("Speak: Spacebar");  
	label_home_sc=gtk_label_new("Goto Today: Home Key");
	label_about_sc=gtk_label_new("About: <Ctrl A>");	
	label_version_sc=gtk_label_new("Version: <Ctrl V>");
	label_quit_sc=gtk_label_new("Quit: <Ctrl Q>");
		
	
	gtk_box_append(GTK_BOX(box), label_speak_sc);
	gtk_box_append(GTK_BOX(box),label_home_sc);
	gtk_box_append(GTK_BOX(box), label_about_sc);
	gtk_box_append(GTK_BOX(box), label_version_sc);
	gtk_box_append(GTK_BOX(box),label_quit_sc);
	
	GtkStyleContext *context_dialog;	
	gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
	GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
	//finally load style provider 
	gtk_style_context_add_provider(context_dialog,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
	
	gtk_window_present (GTK_WINDOW (dialog));
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_window_destroy), NULL);

}


static void callbk_info(GSimpleAction *action, GVariant *parameter,  gpointer user_data){

	GtkWidget *window =user_data;
	GtkWidget *dialog; 
	GtkWidget *box; 
	gint response; 	
	//Check buttons
	GtkWidget *label_record_number;	
	GtkWidget *label_font_name;
	GtkWidget *label_font_size;	
                                               
   dialog = gtk_dialog_new_with_buttons ("Information", GTK_WINDOW(window), 
   GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
   "Close", GTK_RESPONSE_CLOSE,
   NULL);                                         
   
	gtk_window_set_default_size(GTK_WINDOW(dialog),380,100);  
	
	box =gtk_box_new(GTK_ORIENTATION_VERTICAL,1);  
	gtk_window_set_child (GTK_WINDOW (dialog), box);
	
	char* record_num_str =" Number of records (max 5000) = ";
	char* n_str = g_strdup_printf("%d", m_db_size);   
	record_num_str = g_strconcat(record_num_str, n_str,NULL);   
	label_record_number =gtk_label_new(record_num_str); 
	
	char* font_str ="Font name = ";
	font_str = g_strconcat(font_str, m_font_name,NULL); 
	label_font_name=gtk_label_new(font_str);
	
	
	char* font_size_str =" Font size = ";
	char* font_n_str = g_strdup_printf("%d", m_font_size);   
	font_size_str = g_strconcat(font_size_str, font_n_str,NULL);   
	label_font_size =gtk_label_new(font_size_str); 
  
  gtk_box_append(GTK_BOX(box), label_record_number);
  gtk_box_append(GTK_BOX(box), label_font_name);
  gtk_box_append(GTK_BOX(box),label_font_size);
  
  GtkStyleContext *context_dialog;	
  gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
  GtkCssProvider *cssProvider;	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
  
  // get GtkStyleContext from widget
  context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
  //finally load style provider 
  gtk_style_context_add_provider(context_dialog,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  
  gtk_window_present (GTK_WINDOW (dialog));
  g_signal_connect (dialog, "response", G_CALLBACK (gtk_window_destroy), NULL);
 	
}

static void callbk_delete_all_response(GtkDialog *dialog, gint response_id, gpointer  user_data)								 
{
	
	
	GtkWindow *window = user_data; //window data
    if(response_id==GTK_RESPONSE_OK)
	{
    
    //g_print("Danger: Deleting everything\n");
    
    Event e_empty;
    e_empty.id=0;    
    strcpy(e_empty.title, "");   
    strcpy(e_empty.location,"");
    e_empty.year=0;
    e_empty.month=0;
    e_empty.day=0;
    e_empty.start_time=0;
    e_empty.end_time=0;	
    e_empty.priority;
    e_empty.is_yearly=0;
    e_empty.is_allday=0;
    
    for(int i=0;  i<m_db_size; i++)
    {
		db_store[i]=e_empty;
		
	}
    
    reset_marked_dates();  
    update_calendar(GTK_WINDOW(window));
	update_store(m_year,m_month,m_day);
	m_db_size=0;
	m_id_selection=-1;
	m_row_index=-1; 	
    }
    gtk_window_destroy(GTK_WINDOW(dialog));	       

}

static void callbk_delete_all(GSimpleAction *action, GVariant *parameter,  gpointer user_data){

	
	GtkWindow *window = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (user_data),
	GTK_TYPE_WINDOW));
	
	if (!GTK_IS_WINDOW (window))
	{      
	g_print("callbk font: error not a window\n");
	return;      
	}
 
  GtkWidget *dialog;
  gint response; 

  dialog = GTK_WIDGET (gtk_message_dialog_new (window,
                                               GTK_DIALOG_MODAL|
                                               GTK_DIALOG_DESTROY_WITH_PARENT|
                                               GTK_DIALOG_USE_HEADER_BAR,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_NONE,
                                               "Delete"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            "All events will be delete");
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), 
                          "Cancel", GTK_RESPONSE_CANCEL,
                          "Delete", GTK_RESPONSE_OK,
                          NULL);  

  
  GtkStyleContext *context_dialog;	
  gtk_widget_set_name (GTK_WIDGET(dialog), "cssView"); 
  GtkCssProvider *cssProvider;	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
  
  // get GtkStyleContext from widget
  context_dialog = gtk_widget_get_style_context(GTK_WIDGET(dialog));	
  //finally load style provider 
  gtk_style_context_add_provider(context_dialog,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
  
  g_signal_connect (dialog, "response", G_CALLBACK (callbk_delete_all_response),window);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void callbk_home(GSimpleAction * action, GVariant *parameter, gpointer user_data){
	
		
   GtkWindow *window = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (user_data),
                                                 GTK_TYPE_WINDOW));
     
   if( !GTK_IS_WINDOW(window)) {
	   g_print("callbk home: not a window\n");	   	
       return;
   }
   
  GDate *current_date; 
  current_date = g_date_new();
  g_date_set_time_t (current_date, time (NULL)); 
  
  m_day =g_date_get_day(current_date);
  m_month =g_date_get_month(current_date);  
  m_year =g_date_get_year(current_date); 
  g_date_free (current_date);
    
  //mark days with events
  reset_marked_dates();
  update_marked_dates(m_month,m_year); 
  update_calendar(GTK_WINDOW (window)); 
  update_store(m_year,m_month,m_day); 
   
}

static void callbk_quit(GSimpleAction * action,
							G_GNUC_UNUSED GVariant      *parameter,
							              gpointer       user_data)
{
	
	g_application_quit(G_APPLICATION(user_data));
	
}

int compare (const void * a, const void * b)
{

  Event *eventA = (Event *)a;
  Event *eventB = (Event *)b;
  
  if (eventA->start_time>eventB->start_time) return 1;
    else if (eventA->start_time<eventB->start_time)return -1;
    return 0;
}
//--------------------------------------------------------------------
// speak events
//--------------------------------------------------------------------

static void speak_events() {
	
	if(m_talk==0) return;
	int event_count=0;
	Event e;  
	for (int i=0; i<m_db_size; i++)
	{  
	e=db_store[i];
	if(m_year==e.year && m_month ==e.month && m_day==e.day)
	{		
	event_count++;
	}//if		
	}//for
      
   Event day_events[event_count]; 
   
   //load day events 
   int j=0;
   for (int i=0; i<m_db_size; i++)
	{  
	e=db_store[i];
	if(m_year==e.year && m_month ==e.month && m_day==e.day)
	{		
	day_events[j] =e;
	j++;
	}//if		
	}//for
   
   //sort
   
   qsort (day_events, event_count, sizeof(Event), compare);
   
   Event day;
   
   for(int i=0; i<event_count; i++)
   {
   char *speak_str ="";
   char *starts;   
   char *start_time_str="";
   char *time_str="";
   char *title_str="";
   char *location_str="";
   char *starthour_str = ""; 
   char *startmin_str = ""; 
   char *endhour_str = ""; 
   char *endmin_str = "";  
   day =day_events[i];     
   if (day.is_allday) {
   time_str="All day Event. ";
   }   
   else { 
    float integral_part, fractional_part;
    fractional_part = modff(day.start_time, &integral_part);  
    int start_hour =(int) integral_part;    
    fractional_part=round(fractional_part *100);
    int start_min=(int) (fractional_part);
   
    if (day.start_time<=12.59) //am
    {
    starthour_str = g_strdup_printf("%d", start_hour); 
    startmin_str = g_strdup_printf("%d", start_min);  
    
    if(start_min==0)
    {
    time_str = g_strconcat(time_str, starthour_str," am ",NULL);
    }
    
    else if (start_min <10){       
     time_str = g_strconcat(time_str, starthour_str," o", startmin_str," am ",NULL);
    }
   
    else {
	 time_str = g_strconcat(time_str, starthour_str," ", startmin_str," am ",NULL);
    }
    }
    else { //pm 
    
    start_hour =start_hour-12;    
    
    starthour_str = g_strdup_printf("%d", start_hour); 
    startmin_str = g_strdup_printf("%d", start_min);
    
    if(start_min==0)
    {
    time_str = g_strconcat(time_str, starthour_str," pm ",NULL);
    }
    else if (start_min <10){       
     time_str = g_strconcat(time_str, starthour_str," o", startmin_str," pm ",NULL);
    }
    else {
	 time_str = g_strconcat(time_str, starthour_str," ", startmin_str," pm ",NULL);
	}
    
    }  //else pm 
    
    //Now speak end time if required
    
    if(m_show_end_time){
    
    float integral_part_end, fractional_part_end;
    fractional_part_end = modff(day.end_time, &integral_part_end); 
    int end_hour =(int) integral_part_end; //end_hour
    
    fractional_part_end=round(fractional_part_end *100);
    int end_min=(int) (fractional_part_end); //end_min
    
    if (day.end_time<=12.59) //am
    {
       
    endhour_str = g_strdup_printf("%d", end_hour); 
    endmin_str = g_strdup_printf("%d", end_min);  
    
    if(end_min==0)
    {
    time_str = g_strconcat(time_str,"to ", endhour_str," am. ",NULL);
    }
    
    else if (end_min <10){       
    time_str = g_strconcat(time_str,"to ",  endhour_str," to :0", endmin_str," am. ",NULL);
    }
    
    else {
    time_str = g_strconcat(time_str, "to ", endhour_str,":", endmin_str," am. ",NULL);	   
     }
    }
    
    else { //pm 
    
    end_hour =end_hour-12;
    endhour_str = g_strdup_printf("%d", end_hour); 
    endmin_str = g_strdup_printf("%d", end_min); 
    
    if(end_min==0)
    {
    time_str = g_strconcat(time_str,"to ", endhour_str," pm. ",NULL);
    }
    else if (start_min <10){       
    time_str = g_strconcat(time_str, "to ", endhour_str,":0", endmin_str," pm. ",NULL);
    }
    else {
    time_str = g_strconcat(time_str, "to ", endhour_str,":", endmin_str," pm. ",NULL);
	}
    } //else pm
	
    }//show end time
   
   } //else not allday
   
      
   title_str=day.title;
      
   if(strlen(day.location) ==0)
   {
   location_str=""; 
   }
   else { 
   location_str = g_strconcat(location_str, " at ",day.location, ".", NULL);  }
      
   speak_str= g_strconcat(speak_str, time_str, title_str, ".  ",location_str,". ", NULL);
   
   if(day.priority) {
   speak_str=g_strconcat(speak_str, " This is a high priority event.  ", NULL);
   }
 
   GThread *thread_speak;   
    if(m_talk) {	
	g_mutex_lock (&lock);
    thread_speak = g_thread_new(NULL, thread_speak_func, speak_str);   
	}	
	g_thread_unref (thread_speak);	
 } 

		
}

static void callbk_speak(GSimpleAction* action, GVariant *parameter,gpointer user_data){
		
	speak_events();	
	
}
	


static void callbk_speak_about(GSimpleAction *action,
							G_GNUC_UNUSED  GVariant      *parameter,
							  gpointer       user_data){
		
	GThread *thread_speak; 
		
	gchar* message_speak ="Talk Calendar. Gtk4 Version 1.0";    
		
	if(m_talk) {	
	g_mutex_lock (&lock);
    thread_speak = g_thread_new(NULL, thread_speak_func, message_speak);   
	}	
	g_thread_unref (thread_speak);	
}

//---------------------------------------------------------------------
// startup and shutdown
//---------------------------------------------------------------------
int file_exists(const char *file_name)
{
    FILE *file;
    file = fopen(file_name, "r");
    if (file){       
        fclose(file);
        return 1; //file exists return 1
    }
    return 0; //file does not exist
}

static void startup (GtkApplication *app)
{
	
	//g_print("Allocating memory of size %d bytes\n",max_records*sizeof(Event));	
	db_store=malloc(max_records*sizeof(Event));
	
	if (db_store==NULL){
	g_print("memory allocation failed -not enough RAM\n");
	exit(1);
	}
	
	
	if(file_exists("events.csv"))
	{
		//g_print("events.csv exists-load it\n");
		load_csv_file();
	}
	
	
	
	 //---------------------------------------------------
  
		
}

void callbk_shutdown(GtkWindow *window, gint response_id,  gpointer  user_data){
	//g_print("shutdown function called\n");	
	save_csv_file();
	free(db_store);
}


//--------------------------------------------------------------------
// public holidays
//---------------------------------------------------------------------
gboolean is_public_holiday(int day) {
	
// UK public holidays 
// New Year's Day: 1 January (DONE)
// Good Friday: March or April  (DONE)
// Easter Monday: March or April (DONE)
// Early May: First Monday of May (DONE)
// Spring Bank Holiday: Last Monday of May (DONE)
// Summer Bank Holiday: Last Monday of August (DONE)
// Christmas Day: 25 December (DONE)
// Boxing day: 26 December (DONE)
	
	//markup public holidays
	if (m_month==1 && day ==1) {  
	//new year	
	 return TRUE;
	}
	
	if (m_month==12 && day==25) {
	//christmas day
	return TRUE;	  
	} 
	
	if (m_month==12 && day==26) {
	//boxing day
	return TRUE;	  
	}
	
	
	if (m_month == 5) {
     //May complicated
     GDate *first_monday_may;
     first_monday_may = g_date_new_dmy(1, m_month, m_year);
             
     while (g_date_get_weekday(first_monday_may) != G_DATE_MONDAY)
       g_date_add_days(first_monday_may,1);
       
     int may_day = g_date_get_day(first_monday_may);  
       
     if( day==may_day) return TRUE;
     //else return FALSE;
     
     int days_in_may =g_date_get_days_in_month (m_month, m_year);
     int plus_days = 0;
     
     if (may_day + 28 <= days_in_may) {
       plus_days = 28;
     } else {
       plus_days = 21;
     }
     
     GDate *spring_bank =g_date_new_dmy (may_day, m_month, m_year);
     
     g_date_add_days(spring_bank,plus_days);
      
     int spring_bank_day = g_date_get_day(spring_bank);   
      
     if (g_date_valid_dmy (spring_bank_day,m_month,m_year) && day ==spring_bank_day) 
     return TRUE;       
	} //m_month==5 (may)
	
	GDate *easter_date =calculate_easter(m_year); 
	int easter_day = g_date_get_day(easter_date);
	int easter_month =g_date_get_month(easter_date);
	
	if(m_month==easter_month && day == easter_day)
	{
	//easter day
	return TRUE;
	}
	
	g_date_subtract_days(easter_date,2);
	int easter_friday = g_date_get_day(easter_date); 
	int easter_friday_month =g_date_get_month(easter_date); 
	
	if(m_month==easter_friday_month && day ==easter_friday)
	{
	//easter friday
	return TRUE;
	}
	
	g_date_add_days(easter_date,3);
	int easter_monday = g_date_get_day(easter_date); //easter monday
	int easter_monday_month =g_date_get_month(easter_date); 
    
	if(m_month==easter_monday_month && day ==easter_monday)
	{
	//easter monday
	return TRUE;
	}
	
	if (m_month == 8) {
      //August complicated
    GDate *first_monday_august;
     first_monday_august = g_date_new_dmy(1, m_month, m_year);
             
     while (g_date_get_weekday(first_monday_august) != G_DATE_MONDAY)
       g_date_add_days(first_monday_august,1);
       
     int august_day = g_date_get_day(first_monday_august);  
       
          
     int days_in_august =g_date_get_days_in_month (m_month, m_year);
     int plus_days = 0;
     
     if (august_day + 28 <= days_in_august) {
       plus_days = 28;
     } else {
       plus_days = 21;
     }
     
     GDate *august_bank =g_date_new_dmy (august_day, m_month, m_year);
     
     g_date_add_days(august_bank,plus_days);
      
     int august_bank_day = g_date_get_day(august_bank);   
      
     if (g_date_valid_dmy (august_bank_day,m_month,m_year) && day ==august_bank_day) 
     return TRUE;         
      
      
    } //m_month==8
		
	return FALSE;
}

char* get_holiday(int day) {
	
// UK public holidays 
// New Year's Day: 1 January (DONE)
// Good Friday: March or April  (DONE)
// Easter Monday: March or April (DONE)
// Early May: First Monday of May (TODO)
// Spring Bank Holiday: Last Monday of May (DONE)
// Summer Bank Holiday: Last Monday of August (DONE)
// Christmas Day: 25 December (DONE)
// Boxing day: 26 December (DONE)
	
	//markup public holidays
	if (m_month==1 && day ==1) { 
	return "New Year's Day";
	}
	
	if (m_month==12 && day==25) {
	//christmas day
	return "Christmas Day";	  
	} 
	
	if (m_month==12 && day==26) {
	//boxing day
	return "Boxing Day";	  
	}
	
	if (m_month == 5) {
     //May complicated
     GDate *first_monday_may;
     first_monday_may = g_date_new_dmy(1, m_month, m_year);
        
     
     while (g_date_get_weekday(first_monday_may) != G_DATE_MONDAY)
       g_date_add_days(first_monday_may,1);
       
     int may_day = g_date_get_day(first_monday_may);  
       
     if( day==may_day) return "May Bank Holiday";
     
     int days_in_may =g_date_get_days_in_month (m_month, m_year);
     
     int plus_days = 0;
     
     if (may_day + 28 <= days_in_may) {
       plus_days = 28;
     } else {
       plus_days = 21;
     }
     
     GDate *spring_bank =g_date_new_dmy (may_day, m_month, m_year);     
     g_date_add_days(spring_bank,plus_days);      
     int spring_bank_day = g_date_get_day(spring_bank);        
     if (g_date_valid_dmy (spring_bank_day,m_month,m_year) && day ==spring_bank_day) 
     return "Spring Bank Holiday";     
           
	} //m_month ==5 (May)
	
	GDate *easter_date =calculate_easter(m_year); 
	int easter_day = g_date_get_day(easter_date);
	int easter_month =g_date_get_month(easter_date);
	
	if(m_month==easter_month && day == easter_day)
	{
	//easter day
	return "Easter Day";
	}
	
	g_date_subtract_days(easter_date,2);
	int easter_friday = g_date_get_day(easter_date); 
	int easter_friday_month =g_date_get_month(easter_date); 
	
	if(m_month==easter_friday_month && day ==easter_friday)
	{
	//easter friday
	return "Easter Friday";
	}
	
	g_date_add_days(easter_date,3);
	int easter_monday = g_date_get_day(easter_date); //easter monday
	int easter_monday_month =g_date_get_month(easter_date); 
    
	if(m_month==easter_monday_month && day ==easter_monday)
	{
	//easter monday
	return "Easter Monday";
	}
	
	if (m_month == 8) {
      //August complicated
    GDate *first_monday_august;
     first_monday_august = g_date_new_dmy(1, m_month, m_year);
             
     while (g_date_get_weekday(first_monday_august) != G_DATE_MONDAY)
       g_date_add_days(first_monday_august,1);
       
     int august_day = g_date_get_day(first_monday_august);  
       
          
     int days_in_august =g_date_get_days_in_month (m_month, m_year);
     int plus_days = 0;
     
     if (august_day + 28 <= days_in_august) {
       plus_days = 28;
     } else {
       plus_days = 21;
     }
     
     GDate *august_bank =g_date_new_dmy (august_day, m_month, m_year);
     
     g_date_add_days(august_bank,plus_days);
      
     int august_bank_day = g_date_get_day(august_bank);   
      
     if (g_date_valid_dmy (august_bank_day,m_month,m_year) && day ==august_bank_day) 
     return "August Bank Holiday";   
     
    } //m_month==8
		
	return "";
}



//---------------------------------------------------------------------
// update ui
//---------------------------------------------------------------------
static void update_calendar(GtkWindow *window) {
	
 
  GtkWidget* label_date;
  GtkWidget *button;
  GtkWidget *grid;
  GtkWidget *button_next_month;
  GtkWidget *button_prev_month; 
  GtkWidget *sw; //scrolled window
  GtkWidget* listbox;
  GtkWidget *label; //for days of week Mon, Tue etc..
  GtkCssProvider *cssProvider;
  
  
  //mark days with events
  reset_marked_dates();
  update_marked_dates(m_month,m_year);
     
   grid =gtk_grid_new();
   gtk_window_set_child (GTK_WINDOW (window), grid);
  
   //GListStore *store;		
   m_store = g_list_store_new (display_object_get_type ()); 
   
   gchar* day_str =	g_strdup_printf("%d",m_day);    
   gchar *year_str = g_strdup_printf("%d",m_year ); 
   
   gchar * day_month_year_str="";
   
   day_month_year_str =g_strconcat(day_month_year_str,day_str, " ", NULL);
   
   switch(m_month)
     {
         case G_DATE_JANUARY:                    
          day_month_year_str =g_strconcat(day_month_year_str,"January ",year_str, NULL);
          break;
         case G_DATE_FEBRUARY:          
           day_month_year_str =g_strconcat(day_month_year_str,"February ",year_str, NULL);
           break;
         case G_DATE_MARCH:
           day_month_year_str =g_strconcat(day_month_year_str,"March ",year_str, NULL);
           break;
          case G_DATE_APRIL:
           day_month_year_str =g_strconcat(day_month_year_str,"April ",year_str, NULL); 
           break;        
          case G_DATE_MAY:
           day_month_year_str =g_strconcat(day_month_year_str,"May ",year_str, NULL);
           break;         
          case G_DATE_JUNE:
          day_month_year_str =g_strconcat(day_month_year_str,"June ",year_str, NULL); 
           break;        
          case G_DATE_JULY:
           day_month_year_str =g_strconcat(day_month_year_str,"July ",year_str, NULL);
           break;         
          case G_DATE_AUGUST:
           day_month_year_str =g_strconcat(day_month_year_str,"August ",year_str, NULL);
           break;         
          case G_DATE_SEPTEMBER:
           day_month_year_str =g_strconcat(day_month_year_str,"September ",year_str, NULL); 
           break;                  
          case G_DATE_OCTOBER:
           day_month_year_str =g_strconcat(day_month_year_str,"October ",year_str, NULL);
           break;         
          case G_DATE_NOVEMBER:
           day_month_year_str =g_strconcat(day_month_year_str,"November ",year_str, NULL);
           break;         
          case G_DATE_DECEMBER:
           day_month_year_str =g_strconcat(day_month_year_str,"December ",year_str, NULL);
           break;         
         default:
           day_month_year_str =g_strconcat(day_month_year_str,"Unknown ",year_str, NULL);
    }
      
   
   if (m_holidays) {
	   
	   //append holiday text
	   char * holiday_str = get_holiday(m_day);
	   day_month_year_str =g_strconcat(day_month_year_str," ",holiday_str, NULL);
   }
   
   label_date = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(label_date), 0.5);
   gtk_label_set_text(GTK_LABEL(label_date),day_month_year_str);
   
   PangoAttrList *attrs;
   attrs = pango_attr_list_new ();
   pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
   gtk_label_set_attributes (GTK_LABEL (label_date), attrs);
   pango_attr_list_unref (attrs);
   
   
  
  int first_day_month = first_day_of_month(m_month,m_year);  
  int week_start = 1; //Monday 
  int day=0;  
  int remainder = (first_day_of_month(m_month,m_year) - week_start + 7) % 7;   
  day = 1 - remainder;   
  int n_cols=7;
  int n_rows=8;  
  gchar* i_str;
  gchar* j_str;
  gchar* btn_str="";
  
  button_next_month=gtk_button_new_with_label (">>");
  g_signal_connect (button_next_month, "clicked", G_CALLBACK (callbk_next_month),window);
  
  button_prev_month=gtk_button_new_with_label ("<<");
  g_signal_connect (button_prev_month, "clicked", G_CALLBACK (callbk_prev_month),window);  
   
  gtk_grid_attach(GTK_GRID(grid),button_prev_month,0,0,1,1); 
  gtk_grid_attach(GTK_GRID(grid),label_date,1,0,5,1);
  gtk_grid_attach(GTK_GRID(grid),button_next_month,6,0,1,1);
    	
 
  label= gtk_label_new("Mon");
  
  GtkStyleContext *context_label_mon;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 

  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_mon = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_mon,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,0,1,1,1);
  
  label= gtk_label_new("Tue");
  GtkStyleContext *context_label_tue;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
 
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_tue = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_tue,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	  
  gtk_grid_attach(GTK_GRID(grid),label,1,1,1,1);
  
  label= gtk_label_new("Wed");
  GtkStyleContext *context_label_wed;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
  	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_wed = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_wed,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,2,1,1,1);
  
  label= gtk_label_new("Thu");
  GtkStyleContext *context_label_thur;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
 	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_thur = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_thur,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,3,1,1,1);
  
  label= gtk_label_new("Fri");
  GtkStyleContext *context_label_fri;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
  	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_fri = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_fri,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,4,1,1,1);
  
  label= gtk_label_new("Sat");
  GtkStyleContext *context_label_sat;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
  	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_sat = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_sat,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,5,1,1,1);
  
  label= gtk_label_new("Sun");
  GtkStyleContext *context_label_sun;	
  gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
  	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
  context_label_sun = gtk_widget_get_style_context(GTK_WIDGET(label));		
  gtk_style_context_add_provider(context_label_sun,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
  gtk_grid_attach(GTK_GRID(grid),label,6,1,1,1);
  
  GDate *today_date; 
  today_date = g_date_new();
  g_date_set_time_t (today_date, time (NULL));
  int today_day= g_date_get_day(today_date);
  int today_month= g_date_get_month(today_date);
  int today_year= g_date_get_year(today_date);
  
   
  int days_in_month =g_date_get_days_in_month (m_month, m_year); 
 
  for (int row=2; row<n_rows; row++)
    {
        
        for(int col=0; col<n_cols; col++)
        {
                
        if (day > 0 && day <= days_in_month) {
        
        btn_str =g_strdup_printf ("%d", day); //%i
        button = gtk_button_new_with_label (btn_str); 
		gtk_widget_set_hexpand(button, TRUE); 
		gtk_widget_set_vexpand(button,TRUE); 
		
		 GDate *current_date = g_date_new();
		 g_date_set_dmy (current_date, day, m_month, m_year);
		
		//markup event days
		if(marked_date[day-1]) {			
			set_button_red_with_borders(GTK_BUTTON(button));			
		}
		
		if(is_public_holiday(day) && m_holidays) {
		set_button_blue(GTK_BUTTON(button));
	    }
		
		
		
		if(day==today_day && m_month==today_month && m_year==today_year) {
			set_button_green(GTK_BUTTON(button));
		}
		
		GtkStyleContext *context_button;	
		gtk_widget_set_name (GTK_WIDGET(button), "cssView"); 
			
		cssProvider = gtk_css_provider_new();
		gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
		context_button = gtk_widget_get_style_context(GTK_WIDGET(button));		
		gtk_style_context_add_provider(context_button,    
		GTK_STYLE_PROVIDER(cssProvider), 
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
		
		
		
		g_object_set_data(G_OBJECT(button), "button-window-key",window);  
       
        g_signal_connect (button, "clicked", G_CALLBACK (callbk_day_selected), current_date);
		
        
        gtk_grid_attach(GTK_GRID(grid),button,col,row,1,1);	
        btn_str="";         
         }                        
        day=day+1;        
        }
        
    }

  //set scrolled window
  sw = gtk_scrolled_window_new ();
  gtk_widget_set_hexpand (GTK_WIDGET (sw), true);
  gtk_widget_set_vexpand (GTK_WIDGET (sw), true);
  
  listbox = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_SINGLE);
  
  gtk_list_box_set_show_separators (GTK_LIST_BOX (listbox), TRUE);
 
  gtk_list_box_set_header_func (GTK_LIST_BOX (listbox), add_separator, NULL, NULL); 
  gtk_list_box_bind_model (GTK_LIST_BOX (listbox), G_LIST_MODEL (m_store), create_widget, NULL, NULL);
  g_signal_connect (listbox, "row-activated", G_CALLBACK (callbk_row_activated),NULL);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), listbox);
  //col-rows (span all 7 days and a further 8 rows)
  gtk_grid_attach(GTK_GRID(grid),sw,0,n_rows+1,7,8);
  
  g_object_set_data(G_OBJECT(window), "window-listbox-key",listbox);
  
  
    //---------------------------------------------------------------
    GtkStyleContext *context_label_date;	
	gtk_widget_set_name (GTK_WIDGET(label_date), "cssView"); 
	//GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1);	
	context_label_date = gtk_widget_get_style_context(GTK_WIDGET(label_date));		
	gtk_style_context_add_provider(context_label_date,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
    
    //------------------------------------------------------------------
    GtkStyleContext *context_button_next_month;	
	gtk_widget_set_name (GTK_WIDGET(button_next_month), "cssView"); 
	//GtkCssProvider *cssProvider;	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
	context_button_next_month = gtk_widget_get_style_context(GTK_WIDGET(button_next_month));		
	gtk_style_context_add_provider(context_button_next_month,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
    //------------------------------------------------------------------
    GtkStyleContext *context_button_prev_month;	
	gtk_widget_set_name (GTK_WIDGET(button_prev_month), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
	context_button_prev_month = gtk_widget_get_style_context(GTK_WIDGET(button_prev_month));		
	gtk_style_context_add_provider(context_button_prev_month,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
    //------------------------------------------------------------------
        
    GtkStyleContext *context_listbox;	
	gtk_widget_set_name (GTK_WIDGET(listbox), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1);
	context_listbox = gtk_widget_get_style_context(GTK_WIDGET(listbox));
	gtk_style_context_add_provider(context_listbox,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
	
	//------------------------------------------------------------------
	
	GtkStyleContext *context_label;	
	gtk_widget_set_name (GTK_WIDGET(label), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 	
	context_label = gtk_widget_get_style_context(GTK_WIDGET(label));		
	gtk_style_context_add_provider(context_label,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
    //------------------------------------------------------------------

}

static void update_header (GtkWindow *window)
{
	GtkWidget *header;
	GtkWidget *button_new_event;
	GtkWidget *button_edit_event;
	GtkWidget *button_delete_selected;
	GtkWidget *button_test;
	GtkWidget *menu_button; 
	GtkCssProvider *cssProvider;	
		
	header = gtk_header_bar_new ();	
	gtk_window_set_titlebar (GTK_WINDOW(window), header);
	button_new_event = gtk_button_new_with_label ("New");	
	gtk_widget_set_tooltip_text(button_new_event, "New Event");
	g_signal_connect (button_new_event, "clicked", G_CALLBACK (callbk_new_event), window);	
	g_object_set_data(G_OBJECT(button_new_event), "button-window-key",window); 
	
	button_edit_event = gtk_button_new_with_label ("Edit");	
	gtk_widget_set_tooltip_text(button_edit_event, "Edit Selected Event");
	g_signal_connect (button_edit_event, "clicked", G_CALLBACK (callbk_edit_event), window);    
	
	button_delete_selected = gtk_button_new_with_label ("Delete"); 
	gtk_widget_set_tooltip_text(button_delete_selected, "Delete Selected");  
	g_signal_connect (button_delete_selected, "clicked", G_CALLBACK (callbk_delete_selected), window);
	
		
	//Packing
	gtk_header_bar_pack_start(GTK_HEADER_BAR (header),button_new_event);
	gtk_header_bar_pack_start(GTK_HEADER_BAR (header),button_edit_event);   
	gtk_header_bar_pack_start(GTK_HEADER_BAR (header), button_delete_selected);
	//gtk_header_bar_pack_end(GTK_HEADER_BAR (header), button_test);
	
	
	//Menu model
	GMenu *menu, *section; 	
	menu = g_menu_new ();  
	
	section = g_menu_new ();
	g_menu_append (section, "Preferences", "app.preferences");	
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);
	
	section = g_menu_new ();
	g_menu_append (section, "Font", "app.font"); 	
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);
	
	section = g_menu_new ();
	g_menu_append (section, "Delete All", "app.delete");	
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);
	
	section = g_menu_new ();
	g_menu_append (section, "Information", "app.info"); //show app info
	g_menu_append (section, "Shortcuts", "app.shortcuts"); //show shortcuts
	g_menu_append (section, "Speak Version", "app.version");  //speak version	
	g_menu_append (section, "About", "app.about");
	g_menu_append_submenu (menu, "_Help", G_MENU_MODEL (section));
	g_object_unref (section);
	
	section = g_menu_new ();
	g_menu_append (section, "Quit", "app.quit");
	g_menu_append_section (menu, NULL, G_MENU_MODEL(section));
	g_object_unref (section);
	
	//Now hamburger style menu button
	menu_button = gtk_menu_button_new();
	gtk_widget_set_tooltip_text(menu_button, "Menu");
	gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button),"open-menu-symbolic"); 		
	gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), G_MENU_MODEL(menu));
	
	//gtk_widget_show (menu_button);  
	gtk_header_bar_pack_end(GTK_HEADER_BAR (header), menu_button);
	//gtk_header_bar_pack_end(GTK_HEADER_BAR (header), button_test);
	
	//Style
	
	GtkStyleContext *context_header;	
	gtk_widget_set_name (GTK_WIDGET(header), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_header = gtk_widget_get_style_context(GTK_WIDGET(header));	
	//finally load style provider 
	gtk_style_context_add_provider(context_header,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);	
	//--------------------------------------------------------------
	
	GtkStyleContext *context_button_new_event;	
	gtk_widget_set_name (GTK_WIDGET(button_new_event), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_button_new_event = gtk_widget_get_style_context(GTK_WIDGET(button_new_event));	
	//finally load style provider 
	gtk_style_context_add_provider(context_button_new_event,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	
	//--------------------------------------------------------------
	
	GtkStyleContext *context_button_edit_event;	
	gtk_widget_set_name (GTK_WIDGET(button_edit_event), "cssView"); 
		
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_button_edit_event = gtk_widget_get_style_context(GTK_WIDGET(button_edit_event));	
	//finally load style provider 
	gtk_style_context_add_provider(context_button_edit_event,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	
	//--------------------------------------------------------------
	GtkStyleContext *context_button_delete_selected;	
	gtk_widget_set_name (GTK_WIDGET(button_delete_selected), "cssView"); 
	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_button_delete_selected = gtk_widget_get_style_context(GTK_WIDGET(button_delete_selected));	
	//finally load style provider 
	gtk_style_context_add_provider(context_button_delete_selected,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
	//-----------------------------------------------------------------
	
	GtkStyleContext *context_menu_button;	
	gtk_widget_set_name (GTK_WIDGET(menu_button), "cssView"); 
	
	cssProvider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
	
	// get GtkStyleContext from widget
	context_menu_button = gtk_widget_get_style_context(GTK_WIDGET(menu_button));	
	//finally load style provider 
	gtk_style_context_add_provider(context_menu_button,    
	GTK_STYLE_PROVIDER(cssProvider), 
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		

	
}

//----------------------------------------------------------------------

static void activate (GtkApplication *app, gpointer  user_data)
{
  GtkWidget *window;
  //header
  GtkWidget *header;
  GtkWidget *button_header;	
  GtkWidget *menu_button; 
  GtkWidget *icon;
  GtkStyleContext *context;  
  
  GtkWidget *button_new_event;
  GtkWidget *button_edit_event;
  GtkWidget *button_delete_selected;
  GtkWidget *button_test;
  GtkWidget *button_store;
  
  GtkCssProvider *cssProvider;
  
  // define keyboard accelerators
  
  const gchar *speak_accels[2] = { "space", NULL };
  const gchar *version_accels[2] = { "<Ctrl>V", NULL };
  const gchar *home_accels[2] = { "Home", NULL };
  const gchar *about_accels[2] =  { "<Ctrl>A", NULL };
  const gchar *quit_accels[2] =   { "<Ctrl>Q", NULL };
  
  
  // create a new window, and set its title 
  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Talk Calendar");
  gtk_window_set_default_size(GTK_WINDOW (window),760,400);
  g_signal_connect (window, "destroy", G_CALLBACK (callbk_shutdown), NULL);
    
  GDate *current_date; 
  current_date = g_date_new();
  g_date_set_time_t (current_date, time (NULL)); 
  
  m_day =g_date_get_day(current_date);
  m_month =g_date_get_month(current_date);  
  m_year =g_date_get_year(current_date);
  g_date_free (current_date);
  
  
  //mark days with events
  reset_marked_dates();
  update_marked_dates(m_month,m_year);  
  
  	
	GSimpleAction *speak_action;	
	speak_action=g_simple_action_new("speak",NULL); //app.speak
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(speak_action)); //make visible	
	g_signal_connect(speak_action, "activate",  G_CALLBACK(callbk_speak), window);
	
	GSimpleAction *speak_about_action;	
	speak_about_action=g_simple_action_new("version",NULL); //app.version
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(speak_about_action)); //make visible	
	g_signal_connect(speak_about_action, "activate",  G_CALLBACK(callbk_speak_about), window);
	
	GSimpleAction *about_action;	
	about_action=g_simple_action_new("about",NULL); //app.about
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(about_action)); //make visible	
	g_signal_connect(about_action, "activate",  G_CALLBACK(callbk_about), window);
	
	GSimpleAction *preferences_action;	
	preferences_action=g_simple_action_new("preferences",NULL); //app.options
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(preferences_action)); //make visible	
	g_signal_connect(preferences_action, "activate",  G_CALLBACK(callbk_preferences), window);
	
		
	GSimpleAction *font_action;	
	font_action=g_simple_action_new("font",NULL); //app.font
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(font_action)); //make visible	
	g_signal_connect(font_action, "activate",  G_CALLBACK(callbk_font), window);
	
	
	GSimpleAction *home_action;	
	home_action=g_simple_action_new("home",NULL); //app.home
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(home_action)); //make visible	
	g_signal_connect(home_action, "activate",  G_CALLBACK(callbk_home), window);
	
	GSimpleAction *delete_events_action;	
	delete_events_action=g_simple_action_new("delete",NULL); //action = app.delete
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(delete_events_action)); //make visible	
	g_signal_connect(delete_events_action, "activate",  G_CALLBACK(callbk_delete_all), window);
	
		
	GSimpleAction *info_action;	
	info_action=g_simple_action_new("info",NULL); //app.info
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(info_action)); //make visible	
	g_signal_connect(info_action, "activate",  G_CALLBACK(callbk_info), window);
	
	
	GSimpleAction *shortcuts_action;	
	shortcuts_action=g_simple_action_new("shortcuts",NULL); //app.shortcuts
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(shortcuts_action)); //make visible	
	g_signal_connect(shortcuts_action, "activate",  G_CALLBACK(callbk_shortcuts), window);
	
	GSimpleAction *quit_action;	
	quit_action=g_simple_action_new("quit",NULL); //app.quit
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit_action)); //make visible	
	g_signal_connect(quit_action, "activate",  G_CALLBACK(callbk_quit), app);
	
    
  // connect keyboard accelerators
	
	
	gtk_application_set_accels_for_action(GTK_APPLICATION(app),
	"app.speak", speak_accels); 
	
	gtk_application_set_accels_for_action(GTK_APPLICATION(app),
	"app.version", version_accels);
	                                    
	
	gtk_application_set_accels_for_action(GTK_APPLICATION(app),
	"app.home", home_accels); 
	
	gtk_application_set_accels_for_action(GTK_APPLICATION(app),
	"app.about", about_accels);
	
	gtk_application_set_accels_for_action(GTK_APPLICATION(app),
	"app.quit", quit_accels);
	
	
    update_header(GTK_WINDOW(window));
  
  //----------------------------------------------------------------
  GtkStyleContext *context_window;	
  gtk_widget_set_name (GTK_WIDGET(window), "cssView"); 
  	
  cssProvider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(cssProvider, get_css_string(),-1); 
  
  // get GtkStyleContext from widget
  context_window = gtk_widget_get_style_context(GTK_WIDGET(window));	
  //finally load style provider 
  gtk_style_context_add_provider(context_window,    
  GTK_STYLE_PROVIDER(cssProvider), 
  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);		
  //gtk_widget_show(dialog);
  //-----------------------------------------------------------------
     
  update_calendar(GTK_WINDOW (window));  
  //gtk_widget_show (window);
  gtk_window_present (GTK_WINDOW (window));     
  update_store(m_year,m_month,m_day);    
  if(m_talk && m_talk_at_startup) speak_events(); 
}

int main (int  argc, char **argv)
{
  
  config_initialize();
  GtkApplication *app;
  int status;

  app = gtk_application_new ("org.gtk.talkcalendar", G_APPLICATION_FLAGS_NONE);
  g_signal_connect_swapped(app, "startup", G_CALLBACK (startup),app);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
