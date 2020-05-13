#include "../../types.h"
#include "../../fceu.h"
#include "../../cheat.h"
#include "../../debug.h"
#include "../../driver.h"
#include "../../version.h"
#include "../../movie.h"
#include "../../palette.h"
#include "../../fds.h"
#include "../common/configSys.h"

#include "sdl.h"
#include "gui.h"
#include "dface.h"
#include "input.h"
#include "config.h"
#include "cheat.h"
#include "icon.xpm"
#include "ramwatch.h"

#ifdef _S9XLUA_H
#include "../../fceulua.h"
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#ifdef _GTK3
#include <gdk/gdkkeysyms-compat.h>
#endif

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <list>

// Fix compliation errors for older version of GTK (Ubuntu 10.04 LTS)
#if GTK_MINOR_VERSION < 24 && GTK_MAJOR_VERSION == 2
  #define GTK_COMBO_BOX_TEXT GTK_COMBO_BOX
  #define gtk_combo_box_text_new gtk_combo_box_new
  #define gtk_combo_box_text_get_active_text gtk_combo_box_get_active_text
  #define gtk_combo_box_text_append_text gtk_combo_box_append_text
#endif

// NES resolution = 256x240
const int NES_WIDTH=256;
const int NES_HEIGHT=240;

void toggleSound(GtkWidget* check, gpointer data);
void loadGame (void);
void closeGame(void);
extern Config *g_config;

GtkWidget* MainWindow = NULL;
GtkWidget* evbox = NULL;
GtkWidget* padNoCombo = NULL;
GtkWidget* configNoCombo = NULL;
GtkWidget* buttonMappings[10];
GtkWidget* Menubar;
GtkRadioAction* stateSlot = NULL;
bool gtkIsStarted = false;
bool menuTogglingEnabled = false;

static GtkTreeStore *hotkey_store = NULL;

/* Surface to store black screen */
static cairo_surface_t *surface = NULL;

// check to see if a particular GTK version is available
// 2.24 is required for most of the dialogs -- ie: checkGTKVersion(2,24);
bool checkGTKVersion(int major_required, int minor_required)
{
	int major = GTK_MAJOR_VERSION;
	int minor = GTK_MINOR_VERSION;

	if(major > major_required)
	{
		return true;
	} else if (major == major_required)
	{
		if(minor >= minor_required)
		{
			return true;
		}
		else
		{
			return false;
		}
	} else
	{
		return false;
	}
}

void setCheckbox(GtkWidget* w, const char* configName)
{
	int buf;
	g_config->getOption(configName, &buf);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), buf);
}

// This function configures a single button on a gamepad
int configGamepadButton(GtkButton* button, gpointer p)
{
	gint x = ((gint)(glong)(p));
	//gint x = GPOINTER_TO_INT(p);
	int padNo = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(padNoCombo))) - 1;
	int configNo = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(configNoCombo))) - 1;
		
	char buf[256];
	std::string prefix;
    
	// only configure when the "Change" button is pressed in, not when it is unpressed
	if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		return 0;
    
	ButtonConfigBegin();
    
	snprintf(buf, sizeof(buf), "SDL.Input.GamePad.%d", padNo);
	prefix = buf;
	DWaitButton(NULL, &GamePadConfig[padNo][x], configNo);

	g_config->setOption(prefix + GamePadNames[x], GamePadConfig[padNo][x].ButtonNum[configNo]);

	if(GamePadConfig[padNo][x].ButtType[0] == BUTTC_KEYBOARD)
	{
		g_config->setOption(prefix + "DeviceType", "Keyboard");
	} else if(GamePadConfig[padNo][x].ButtType[0] == BUTTC_JOYSTICK) {
		g_config->setOption(prefix + "DeviceType", "Joystick");
	} else {
		g_config->setOption(prefix + "DeviceType", "Unknown");
	}
	g_config->setOption(prefix + "DeviceNum", GamePadConfig[padNo][x].DeviceNum[configNo]);
    
	snprintf(buf, sizeof(buf), "<tt>%s</tt>", ButtonName(&GamePadConfig[padNo][x], configNo));
	gtk_label_set_markup(GTK_LABEL(buttonMappings[x]), buf);

	ButtonConfigEnd();
    
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
    
	return 0;
}

void resetVideo(void)
{
	resizeGtkWindow();
	KillVideo();
	InitVideo(GameInfo);
}

void closeVideoWin(GtkWidget* w, gint response, gpointer p)
{
	resetVideo();
	if(response != GTK_RESPONSE_APPLY)
	{
		gtk_widget_destroy(w);
	}
}

void closeDialog(GtkWidget* w, GdkEvent* e, gpointer p)
{
	gtk_widget_destroy(w);
}
void toggleLowPass(GtkWidget* w, gpointer p)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
	{
		g_config->setOption("SDL.Sound.LowPass", 1);
		FCEUI_SetLowPass(1);
	}
	else
	{
		g_config->setOption("SDL.Sound.LowPass", 0);
		FCEUI_SetLowPass(0);
	}
	g_config->save();
}

void toggleSwapDuty(GtkWidget* w, gpointer p)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
	{
		g_config->setOption("SDL.SwapDuty", 1);
		swapDuty = 1;
	}
	else
	{
		g_config->setOption("SDL.SwapDuty", 0);
		swapDuty = 0;
	}
	g_config->save();
}

// Wrapper for pushing GTK options into the config file
// p : pointer to the string that names the config option
// w : toggle widget
void toggleOption(GtkWidget* w, gpointer p)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
		g_config->setOption((char*)p, 1);
	else
		g_config->setOption((char*)p, 0);
	
	g_config->save();
	UpdateEMUCore(g_config);
}

int setTint(GtkWidget* w, gpointer p)
{
	int v = gtk_range_get_value(GTK_RANGE(w));
	g_config->setOption("SDL.Tint", v);
	g_config->save();
	int c, h;
	g_config->getOption("SDL.NTSCpalette", &c);
	g_config->getOption("SDL.Hue", &h);
	FCEUI_SetNTSCTH(c, v, h);
	
	return 0;
}
int setHue(GtkWidget* w, gpointer p)
{
	int v = gtk_range_get_value(GTK_RANGE(w));
	g_config->setOption("SDL.Hue", v);
	g_config->save();
	int c, t;
	g_config->getOption("SDL.Tint", &t);
	g_config->getOption("SDL.SDL.NTSCpalette", &c);
	FCEUI_SetNTSCTH(c, t, v);
	
	return 0;
}
void loadPalette (GtkWidget* w, gpointer p)
{
	GtkWidget* fileChooser;
	
	fileChooser = gtk_file_chooser_dialog_new ("Open NES Palette", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), "/usr/share/fceux/palettes");

	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		g_config->setOption("SDL.Palette", filename);
		g_config->setOption("SDL.SDL.NTSCpalette", 0);
		LoadCPalette(filename);
		
		gtk_entry_set_text(GTK_ENTRY(p), filename);
		
	}
	gtk_widget_destroy (fileChooser);
}

void clearPalette(GtkWidget* w, gpointer p)
{
	g_config->setOption("SDL.Palette", 0);
	gtk_entry_set_text(GTK_ENTRY(p), "");
}

void openPaletteConfig(void)
{
	GtkWidget* win;
	GtkWidget* vbox;
	GtkWidget* paletteFrame;
	GtkWidget* paletteHbox;
	GtkWidget* paletteButton;
	GtkWidget* paletteEntry;
	GtkWidget* clearButton;
	GtkWidget* ntscColorChk;
	GtkWidget* slidersFrame;
	GtkWidget* slidersVbox;
	GtkWidget* tintFrame;
	GtkWidget* tintHscale;
	GtkWidget* hueFrame;
	GtkWidget* hueHscale;
	
	win = gtk_dialog_new_with_buttons("Palette Options",
									  GTK_WINDOW(MainWindow),
									  (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
									  "_Close",
									  GTK_RESPONSE_OK,
									  NULL);
	gtk_window_set_icon_name(GTK_WINDOW(win), "Color");
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(win));
	
	paletteFrame = gtk_frame_new("Custom palette: ");
	paletteHbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_container_set_border_width(GTK_CONTAINER(paletteHbox), 5);
	gtk_container_add(GTK_CONTAINER(paletteFrame), paletteHbox);
	paletteButton = gtk_button_new_with_label("Open palette");
	paletteEntry = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(paletteEntry), FALSE);
	
	clearButton = gtk_button_new_with_label("Clear");
	
	gtk_box_pack_start(GTK_BOX(paletteHbox), paletteButton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(paletteHbox), paletteEntry, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(paletteHbox), clearButton, FALSE, FALSE, 0);
	
	g_signal_connect(paletteButton, "clicked", G_CALLBACK(loadPalette), paletteEntry);
	g_signal_connect(clearButton, "clicked", G_CALLBACK(clearPalette), paletteEntry);
	
	// sync with config
	std::string fn;
	g_config->getOption("SDL.Palette", &fn);
	gtk_entry_set_text(GTK_ENTRY(paletteEntry), fn.c_str());
	
	// ntsc color check
	ntscColorChk = gtk_check_button_new_with_label("Use NTSC palette");
	
	g_signal_connect(ntscColorChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.NTSCpalette");
	setCheckbox(ntscColorChk, "SDL.NTSCpalette");
	
	// color / tint / hue sliders
	slidersFrame = gtk_frame_new("NTSC palette controls");
	slidersVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	tintFrame = gtk_frame_new("Tint");
	tintHscale = gtk_scale_new_with_range( GTK_ORIENTATION_HORIZONTAL, 0, 128, 1);
	gtk_container_add(GTK_CONTAINER(tintFrame), tintHscale);
	hueFrame = gtk_frame_new("Hue");
	hueHscale = gtk_scale_new_with_range( GTK_ORIENTATION_HORIZONTAL, 0, 128, 1);
	gtk_container_add(GTK_CONTAINER(hueFrame), hueHscale);
	
	g_signal_connect(tintHscale, "button-release-event", G_CALLBACK(setTint), NULL);
	g_signal_connect(hueHscale, "button-release-event", G_CALLBACK(setHue), NULL);
	
	// sync with config
	int h, t;
	g_config->getOption("SDL.Hue", &h);
	g_config->getOption("SDL.Tint", &t);

	gtk_range_set_value(GTK_RANGE(hueHscale), h);
	gtk_range_set_value(GTK_RANGE(tintHscale), t);
	
	gtk_container_add(GTK_CONTAINER(slidersFrame), slidersVbox);
	gtk_box_pack_start(GTK_BOX(slidersVbox), ntscColorChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(slidersVbox), tintFrame, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(slidersVbox), hueFrame, FALSE, TRUE, 5);
	
	gtk_box_pack_start(GTK_BOX(vbox), paletteFrame, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), slidersFrame, FALSE, TRUE, 5);
	
	g_signal_connect(win, "delete-event", G_CALLBACK(closeDialog), NULL);
	g_signal_connect(win, "response", G_CALLBACK(closeDialog), NULL);
	
	gtk_widget_show_all(win);
	
	return;
}


GtkWidget* ipEntry;
GtkWidget* portSpin;
GtkWidget* pwEntry;

void launchNet(GtkWidget* w, gpointer p)
{
	char* ip = (char*)gtk_entry_get_text(GTK_ENTRY(ipEntry));
	char* pw = (char*)gtk_entry_get_text(GTK_ENTRY(pwEntry));
	int port = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(portSpin));
	
	g_config->setOption("SDL.NetworkIP", ip);
	g_config->setOption("SDL.NetworkPassword", pw);
	g_config->setOption("SDL.NetworkPort", port);
	
	gtk_widget_destroy(GTK_WIDGET(w));
	
	loadGame();
}

void setUsername(GtkWidget* w, gpointer p)
{
	char* s = (char*)gtk_entry_get_text(GTK_ENTRY(w));
	g_config->setOption("SDL.NetworkUsername", s);
}

void netResponse(GtkWidget* w, gint response_id, gpointer p)
{
	if(response_id == GTK_RESPONSE_OK)
		launchNet(w, p);
	else
		gtk_widget_destroy(w);
}

void openNetworkConfig(void)
{
	GtkWidget* win;
	GtkWidget* box;
	GtkWidget* userBox;
	GtkWidget* userEntry;
	GtkWidget* userLbl;
	GtkWidget* frame;
	GtkWidget* vbox;
	GtkWidget* ipBox;
	GtkWidget* ipLbl;
	
	GtkWidget* portBox;
	GtkWidget* portLbl;
	
	//GtkWidget* localPlayersCbo;
	GtkWidget* pwBox;
	GtkWidget* pwLbl;
	
	win = gtk_dialog_new_with_buttons("Network Options",GTK_WINDOW(MainWindow), (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),"_Close",  GTK_RESPONSE_CLOSE,  "_Connect", GTK_RESPONSE_OK, NULL);
	gtk_window_set_icon_name(GTK_WINDOW(win), "network-workgroup");
	box = gtk_dialog_get_content_area(GTK_DIALOG(win));
	
	userBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	userLbl = gtk_label_new("Username:");
	userEntry = gtk_entry_new();
	std::string s;
	g_config->getOption("SDL.NetworkUsername", &s);
	gtk_entry_set_text(GTK_ENTRY(userEntry), s.c_str());
	
	g_signal_connect(userEntry, "changed", G_CALLBACK(setUsername), NULL);

	frame = gtk_frame_new("Network options");
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	ipBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	ipLbl = gtk_label_new("Server IP:");
	ipEntry = gtk_entry_new();
	portBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	portLbl = gtk_label_new("Server port:");
	portSpin = gtk_spin_button_new_with_range(0, 999999, 1);
	//localPlayersCbo = gtk_combo_box_new_text();
	pwBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	pwLbl = gtk_label_new("Server password:");
	pwEntry = gtk_entry_new();
	
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(portSpin), 4046);
	
	gtk_box_pack_start(GTK_BOX(userBox), userLbl, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(userBox), userEntry, TRUE , TRUE, 3);
	
	gtk_box_pack_start(GTK_BOX(portBox), portLbl, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(portBox), portSpin, FALSE , FALSE, 3);
	
	gtk_box_pack_start(GTK_BOX(ipBox), ipLbl, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(ipBox), ipEntry, TRUE , TRUE, 3);
	
	gtk_box_pack_start(GTK_BOX(pwBox), pwLbl, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(pwBox), pwEntry, TRUE , TRUE, 3);
	
	gtk_box_pack_start(GTK_BOX(vbox), ipBox, FALSE, FALSE, 3);
	gtk_box_pack_start(GTK_BOX(vbox), portBox, FALSE, FALSE, 3);
	//gtk_box_pack_start_defaults(GTK_BOX(vbox), localPlayersCbo);
	gtk_box_pack_start(GTK_BOX(vbox), pwBox, FALSE, FALSE, 3);
	
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	
	gtk_box_pack_start(GTK_BOX(box), userBox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);
	
	gtk_widget_show_all(win);
	
	g_signal_connect(win, "delete-event", G_CALLBACK(closeDialog), NULL);
	g_signal_connect(win, "response", G_CALLBACK(netResponse), NULL);
}

// handler prototype

void flushGtkEvents(void)
{
	while (gtk_events_pending ())
		gtk_main_iteration_do (FALSE);

	return;
}


static void hotKeyWindowRefresh(void)
{
    GtkTreeIter iter;
    std::string prefix = "SDL.Hotkeys.";

    if ( hotkey_store == NULL ) return;

    gtk_tree_store_clear(hotkey_store);
    
    gtk_tree_store_append(hotkey_store, &iter, NULL); // aquire iter
     
    int keycode;
    for(int i=0; i<HK_MAX; i++)
    {
        std::string optionName = prefix + HotkeyStrings[i];
        g_config->getOption(optionName.c_str(), &keycode);
        gtk_tree_store_set(hotkey_store, &iter, 
                0, optionName.c_str(),
                1,
#if SDL_VERSION_ATLEAST(2, 0, 0)                                                    
				SDL_GetKeyName(keycode),
#else
				SDL_GetKeyName((SDLKey)keycode),
#endif
                -1);
        gtk_tree_store_append(hotkey_store, &iter, NULL); // acquire child iterator
    }

}

static gint hotKeyPressCB(GtkTreeView* tree, GdkEventKey* event, gpointer cb_data)
{
	int numListRows;
	GList *selListRows, *tmpList;
	GtkTreeModel *model = NULL;
   GtkTreeSelection *treeSel = gtk_tree_view_get_selection(tree);

   printf("Hot Key Press: %i \n", event->keyval );

	numListRows = gtk_tree_selection_count_selected_rows( treeSel );

   if ( numListRows == 0 )
   {
      return FALSE;
   }
	//printf("Number of Rows Selected: %i\n", numListRows );

	selListRows = gtk_tree_selection_get_selected_rows( treeSel, &model );

	tmpList = selListRows;

	while ( tmpList )
	{
		int depth;
		int *indexArray;
		GtkTreePath *path = (GtkTreePath*)tmpList->data;

		depth = gtk_tree_path_get_depth(path);
		indexArray = gtk_tree_path_get_indices(path);

		if ( depth > 0 )
		{
         int sdlkey = 0;
         std::string hotKeyName = "SDL.Hotkeys.";
         hotKeyName.append( HotkeyStrings[indexArray[0]] );

       	// Convert this keypress from GDK to SDL.
         #if SDL_VERSION_ATLEAST(2, 0, 0)
         	sdlkey = GDKToSDLKeyval(event->keyval);
         #else
         	sdlkey = (SDLKey)GDKToSDLKeyval(event->keyval);
         #endif
         printf("HotKey Index: %i   '%s'  %i   %i \n", 
               indexArray[0], hotKeyName.c_str(), event->keyval, sdlkey );

	      g_config->setOption( hotKeyName, sdlkey );

         setHotKeys(); // Update Hot Keys in Input Model

         hotKeyWindowRefresh();
		}

      tmpList = tmpList->next;
	}

   g_list_free_full(selListRows, (GDestroyNotify) gtk_tree_path_free);

   return TRUE;
}

// creates and opens hotkey config window
void openHotkeyConfig(void)
{
	GtkWidget* win = gtk_dialog_new_with_buttons("Hotkey Configuration",
			GTK_WINDOW(MainWindow), (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
			"_Close",
			GTK_RESPONSE_OK,
			NULL);
	gtk_window_set_default_size(GTK_WINDOW(win), 400, 800);
	GtkWidget *tree;
	GtkWidget *vbox;
	GtkWidget *scroll;

	vbox = gtk_dialog_get_content_area(GTK_DIALOG(win));
	
	hotkey_store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

   hotKeyWindowRefresh();

	tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(hotkey_store));
	GtkCellRenderer *renderer;
	GtkTreeViewColumn* column;

	g_signal_connect(G_OBJECT(tree), "key-press-event", G_CALLBACK(hotKeyPressCB), NULL);
	
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Command", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
	column = gtk_tree_view_column_new_with_attributes("Key", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
			GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), tree);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 5);
	gtk_widget_show_all(win);

	g_signal_connect(win, "delete-event", G_CALLBACK(closeDialog), NULL);
   g_signal_connect(win, "response", G_CALLBACK(closeDialog), NULL);

}

GtkWidget* 	typeCombo;

// TODO: finish this
int setInputDevice(GtkWidget* w, gpointer p)
{
	std::string s = "SDL.Input.";
	s = s + (char*)p;
	printf("%s", s.c_str());
	g_config->setOption(s, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(typeCombo)));
	g_config->save();
	
	return 1;
}

void updateGamepadConfig(GtkWidget* w, gpointer p)
{
	int i;
	char strBuf[128];
	int padNo = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(padNoCombo))) - 1;
	int configNo = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(configNoCombo))) - 1;
	
	for(i=0; i<10; i++)
	{
		GtkWidget* mappedKey = buttonMappings[i];
		if(GamePadConfig[padNo][i].ButtType[configNo] == BUTTC_KEYBOARD)
		{
#if SDL_VERSION_ATLEAST(2, 0, 0)                                                    
			snprintf(strBuf, sizeof(strBuf), "<tt>%s</tt>", 
					SDL_GetKeyName(GamePadConfig[padNo][i].ButtonNum[configNo]));
#else
			snprintf(strBuf, sizeof(strBuf), "<tt>%s</tt>", 
					SDL_GetKeyName((SDLKey)GamePadConfig[padNo][i].ButtonNum[configNo]));
#endif
		}
		else // FIXME: display joystick button/hat/axis names properly
			strncpy(strBuf, "<tt>Joystick</tt>", sizeof(strBuf));
	
		gtk_label_set_text(GTK_LABEL(mappedKey), strBuf);
		gtk_label_set_use_markup(GTK_LABEL(mappedKey), TRUE);
	}
}

// creates and opens the gamepad config window (requires GTK 2.24)
void openGamepadConfig(void)
{
	// GTK 2.24 required for this dialog
	if (checkGTKVersion(2, 24) == false)
	{
		// TODO: present this in a GTK MessageBox?
		printf(" Warning: GTK >= 2.24 required for this dialog.\nTo configure the gamepads, use \"--inputcfg\" from the command line (ie: \"fceux --inputcfg gamepad1\").\n");
		return;
	}

	GtkWidget* win;
	GtkWidget* vbox;
	GtkWidget* hboxPadNo;
	GtkWidget* padNoLabel;
	//GtkWidget* configNoLabel;
	GtkWidget* fourScoreChk;
	GtkWidget* oppositeDirChk;
	GtkWidget* buttonFrame;
	GtkWidget* buttonTable;
	
	win = gtk_dialog_new_with_buttons("Controller Configuration",
									  GTK_WINDOW(MainWindow),
									  (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
									  "_Close",
									  GTK_RESPONSE_OK,
									  NULL);
	gtk_window_set_title(GTK_WINDOW(win), "Controller Configuration");
	gtk_window_set_icon_name(GTK_WINDOW(win), "input-gaming");
	gtk_widget_set_size_request(win, 350, 500);
	
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(win));
	gtk_box_set_homogeneous(GTK_BOX(vbox), FALSE);
	
	hboxPadNo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	padNoLabel = gtk_label_new("Port:");
	//configNoLabel = gtk_label_new("Config Number:");
	fourScoreChk = gtk_check_button_new_with_label("Enable Four Score");
	oppositeDirChk = gtk_check_button_new_with_label("Allow Up+Down / Left+Right");
	
	typeCombo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), "gamepad");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), "zapper");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), "powerpad.0");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), "powerpad.1");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(typeCombo), "arkanoid");
	
	gtk_combo_box_set_active(GTK_COMBO_BOX(typeCombo), 0);
	
	
	padNoCombo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(padNoCombo), "1");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(padNoCombo), "2");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(padNoCombo), "3");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(padNoCombo), "4");
	gtk_combo_box_set_active(GTK_COMBO_BOX(padNoCombo), 0);
	g_signal_connect(padNoCombo, "changed", G_CALLBACK(updateGamepadConfig), NULL);
	
	configNoCombo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(configNoCombo), "1");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(configNoCombo), "2");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(configNoCombo), "3");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(configNoCombo), "4");
	gtk_combo_box_set_active(GTK_COMBO_BOX(configNoCombo), 0);
	g_signal_connect(padNoCombo, "changed", G_CALLBACK(updateGamepadConfig), NULL);
	
	
	g_signal_connect(typeCombo, "changed", G_CALLBACK(setInputDevice), 
		gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(typeCombo)));
	
	setCheckbox(fourScoreChk, "SDL.FourScore");
	g_signal_connect(fourScoreChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.FourScore");
	setCheckbox(oppositeDirChk, "SDL.Input.EnableOppositeDirectionals");
	g_signal_connect(oppositeDirChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.Input.EnableOppositeDirectionals");
	
	
	gtk_box_pack_start(GTK_BOX(hboxPadNo), padNoLabel, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hboxPadNo), padNoCombo, TRUE, TRUE, 5);
	//gtk_box_pack_start(GTK_BOX(hboxPadNo), configNoLabel, TRUE, TRUE, 5);
	//gtk_box_pack_start(GTK_BOX(hboxPadNo), configNoCombo, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hboxPadNo, FALSE, TRUE, 5);
	//gtk_box_pack_start_defaults(GTK_BOX(vbox), typeCombo);
	
	gtk_box_pack_start(GTK_BOX(vbox), fourScoreChk, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), oppositeDirChk, FALSE, TRUE, 5);
	
	
	// create gamepad buttons
	buttonFrame = gtk_frame_new("<b><i>Buttons</i></b>");
	gtk_label_set_use_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(buttonFrame))), TRUE);
	buttonTable = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(buttonFrame), buttonTable);

	for (int i=0; i<3; i++)
	{
	   gtk_grid_insert_column( GTK_GRID(buttonTable), i );
	}
	gtk_grid_set_column_spacing( GTK_GRID(buttonTable), 5 );
	gtk_grid_set_column_homogeneous( GTK_GRID(buttonTable), TRUE );
	gtk_grid_set_row_spacing( GTK_GRID(buttonTable), 3 );

	for(int i=0; i<10; i++)
	{
		GtkWidget* buttonName = gtk_label_new(GamePadNames[i]);
		GtkWidget* mappedKey = gtk_label_new(NULL);
		GtkWidget* changeButton = gtk_toggle_button_new();
		char strBuf[128];

		gtk_grid_insert_row( GTK_GRID(buttonTable), i );

		sprintf(strBuf, "%s:", GamePadNames[i]);
		gtk_label_set_text(GTK_LABEL(buttonName), strBuf);
		
		gtk_button_set_label(GTK_BUTTON(changeButton), "Change");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(changeButton), FALSE);
		
		gtk_grid_attach(GTK_GRID(buttonTable), buttonName, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(buttonTable), mappedKey, 1, i, 1, 1);
		gtk_grid_attach(GTK_GRID(buttonTable), changeButton, 2, i, 1, 1);

		g_signal_connect(changeButton, "clicked", G_CALLBACK(configGamepadButton), GINT_TO_POINTER(i));
		buttonMappings[i] = mappedKey;
	}
	
	// display the button mappings for the currently selected configuration
	updateGamepadConfig(NULL, NULL);
	
	gtk_box_pack_start(GTK_BOX(vbox), buttonFrame, TRUE, TRUE, 5);
	
	g_signal_connect(win, "delete-event", G_CALLBACK(closeDialog), NULL);
	g_signal_connect(win, "response", G_CALLBACK(closeDialog), NULL);
	
	gtk_widget_show_all(win);
	
	return;
}

int setBufSize(GtkWidget* w, gpointer p)
{
	int x = gtk_range_get_value(GTK_RANGE(w));
	g_config->setOption("SDL.Sound.BufSize", x);
	// reset sound subsystem for changes to take effect
	KillSound();
	InitSound();
	return false;
}

void setRate(GtkWidget* w, gpointer p)
{
	char* str = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));
	g_config->setOption("SDL.Sound.Rate", atoi(str));
	// reset sound subsystem for changes to take effect
	KillSound();
	InitSound();
	g_config->save();	
	return;
}

void setQuality(GtkWidget* w, gpointer p)
{
	char* str = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));
	if(!strcmp(str, "Very High"))
		g_config->setOption("SDL.Sound.Quality", 2);
	if(!strcmp(str, "High"))
		g_config->setOption("SDL.Sound.Quality", 1);
	if(!strcmp(str, "Low"))
		g_config->setOption("SDL.Sound.Quality", 0);
	// reset sound subsystem for changes to take effect
	KillSound();
	InitSound();
	g_config->save();	
	return;
}

void resizeGtkWindow(void)
{
	//if(GameInfo == 0)
	//{
		double xscale, yscale;
		g_config->getOption("SDL.XScale", &xscale);
		g_config->getOption("SDL.YScale", &yscale);
	   gtk_widget_set_size_request(evbox, NES_WIDTH*xscale, NES_HEIGHT*yscale);
		GtkRequisition req;
		gtk_widget_get_preferred_size(GTK_WIDGET(MainWindow), NULL, &req);
		//printf("GTK Resizing: w:%i  h:%i \n", req.width, req.height );
		gtk_window_resize(GTK_WINDOW(MainWindow), req.width, req.height);
	   gtk_widget_set_size_request(evbox, NES_WIDTH, NES_HEIGHT);
	//}
	return;
}

void setScaler(GtkWidget* w, gpointer p)
{
	int scaler = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	int opengl;
	g_config->getOption("SDL.OpenGL", &opengl);
	if(opengl && scaler)
	{
		FCEUD_PrintError("Scalers not supported in OpenGL mode.");
		gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
		return;
	}

	g_config->setOption("SDL.SpecialFilter", scaler);
	
	// 1=hq2x 2=Scale2x 3=NTSC2x 4=hq3x  5=Scale3x 6=Prescale2x 7=Prescale3x 8=Prescale4x 9=pal
	switch(scaler)
	{
		case 4:	// hq3x
		case 5:	// scale3x
		case 7:	// prescale3x
			g_config->setOption("SDL.XScale", 3.0);
			g_config->setOption("SDL.YScale", 3.0);
			resizeGtkWindow();
			break;
		case 8: // prescale4x
			g_config->setOption("SDL.XScale", 4.0);
			g_config->setOption("SDL.YScale", 4.0);
			break;
		default:
			g_config->setOption("SDL.XScale", 2.0);
			g_config->setOption("SDL.YScale", 2.0);
			resizeGtkWindow();
			break;
	}

	g_config->save();
}

void setRegion(GtkWidget* w, gpointer p)
{
	int region = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	g_config->setOption("SDL.PAL", region);
	FCEUI_SetRegion(region);
	
	g_config->save();
	
}

int setXscale(GtkWidget* w, gpointer p)
{
	double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
	g_config->setOption("SDL.XScale", v);
	g_config->save();
	resizeGtkWindow();
	return 0;
}

int setYscale(GtkWidget* w, gpointer p)
{
	double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
	g_config->setOption("SDL.YScale", v);
	g_config->save();
	resizeGtkWindow();
	return 0;
}

#ifdef OPENGL
void setGl(GtkWidget* w, gpointer p)
{
	int scaler;
	int opengl = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
	g_config->getOption("SDL.SpecialFilter", &scaler);
	if(scaler && opengl)
	{
		FCEUD_PrintError("Scalers not supported in OpenGL mode.");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), 0);
		return;
	}
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
		g_config->setOption("SDL.OpenGL", 1);
	else
		g_config->setOption("SDL.OpenGL", 0);
	g_config->save();
}

void setDoubleBuffering(GtkWidget* w, gpointer p)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
		g_config->setOption("SDL.DoubleBuffering", 1);
	else
		g_config->setOption("SDL.DoubleBuffering", 0);
	g_config->save();
}
#endif

void openVideoConfig(void)
{
	GtkWidget* win;
	GtkWidget* vbox;
	GtkWidget* lbl;
	GtkWidget* hbox1;
	GtkWidget* scalerLbl;
	GtkWidget* scalerCombo;
	GtkWidget* glChk;
	GtkWidget* linearChk;
	GtkWidget* dbChk;
	GtkWidget* palHbox;
	GtkWidget* palLbl;
	GtkWidget* palCombo;
	GtkWidget* ppuChk;
	GtkWidget* spriteLimitChk;
	GtkWidget* frameskipChk;
	GtkWidget* clipSidesChk;
	GtkWidget* xscaleSpin;
	GtkWidget* yscaleSpin;
	GtkWidget* xscaleLbl;
	GtkWidget* yscaleLbl;
	GtkWidget* xscaleHbox;
	GtkWidget* yscaleHbox;
	GtkWidget* showFpsChk;
	
	win = gtk_dialog_new_with_buttons("Video Preferences",
				GTK_WINDOW(MainWindow),
				(GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
				"_Apply", GTK_RESPONSE_APPLY,
				"_Close", GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_icon_name(GTK_WINDOW(win), "video-display");
	//gtk_widget_set_size_request(win, 250, 250);
	
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(win));
	
	lbl = gtk_label_new("Video options will not take\neffect until the emulator is restarted.");
	
	// scalar widgets
	hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	scalerLbl = gtk_label_new("Special Scaler: ");
	scalerCombo = gtk_combo_box_text_new();
	// -Video Modes Tag-
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "none");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "hq2x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "scale2x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "NTSC 2x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "hq3x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "scale3x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "prescale2x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "prescale3x");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "prescale4x");
	//gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scalerCombo), "pal");
	
	// sync with cfg
	int buf;
	g_config->getOption("SDL.SpecialFilter", &buf);
	gtk_combo_box_set_active(GTK_COMBO_BOX(scalerCombo), buf);
	
	g_signal_connect(scalerCombo, "changed", G_CALLBACK(setScaler), NULL);
	gtk_box_pack_start(GTK_BOX(hbox1), scalerLbl, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox1), scalerCombo, FALSE, FALSE, 5);
#ifdef OPENGL	
	// openGL check
	glChk = gtk_check_button_new_with_label("Enable OpenGL");
	g_signal_connect(glChk, "clicked", G_CALLBACK(setGl), NULL);
	setCheckbox(glChk, "SDL.OpenGL");
	
	// openGL linear filter check
	linearChk = gtk_check_button_new_with_label("Enable OpenGL linear filter");
	g_signal_connect(linearChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.OpenGLip");
	setCheckbox(linearChk, "SDL.OpenGLip");
	
	// DoubleBuffering check
	dbChk = gtk_check_button_new_with_label("Enable double buffering");
	g_signal_connect(dbChk, "clicked", G_CALLBACK(setDoubleBuffering), NULL);
	setCheckbox(dbChk, "SDL.DoubleBuffering");
#endif
	
	// Region (NTSC/PAL/Dendy)
	palHbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	palLbl = gtk_label_new("Region: ");
	palCombo = gtk_combo_box_text_new();
	// -Video Modes Tag-
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(palCombo), "NTSC");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(palCombo), "PAL");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(palCombo), "Dendy");

	// sync with cfg
	g_config->getOption("SDL.PAL", &buf);
	gtk_combo_box_set_active(GTK_COMBO_BOX(palCombo), buf);
	
	g_signal_connect(palCombo, "changed", G_CALLBACK(setRegion), NULL);
	gtk_box_pack_start(GTK_BOX(palHbox), palLbl, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(palHbox), palCombo, FALSE, FALSE, 5);

	// New PPU check
	ppuChk = gtk_check_button_new_with_label("Enable new PPU");
	g_signal_connect(ppuChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.NewPPU");
	setCheckbox(ppuChk, "SDL.NewPPU");

	// "disable 8 sprite limit" check
	spriteLimitChk = gtk_check_button_new_with_label("Disable sprite limit");
	g_signal_connect(spriteLimitChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.DisableSpriteLimit");
	setCheckbox(spriteLimitChk, "SDL.DisableSpriteLimit");
	
	// frameskip check
	frameskipChk = gtk_check_button_new_with_label("Enable frameskip");
	g_signal_connect(frameskipChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.Frameskip");
	setCheckbox(frameskipChk, "SDL.Frameskip");

	// clip sides check
	clipSidesChk = gtk_check_button_new_with_label("Clip sides");
	g_signal_connect(clipSidesChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.ClipSides");
	setCheckbox(clipSidesChk, "SDL.ClipSides");
	
	// xscale / yscale
	xscaleHbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	xscaleLbl = gtk_label_new("X scaling factor");
	xscaleSpin = gtk_spin_button_new_with_range(1.0, 40.0, .1);
	yscaleHbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	yscaleLbl = gtk_label_new("Y scaling factor");
	yscaleSpin = gtk_spin_button_new_with_range(1.0, 40.0, .1);
	
	gtk_box_pack_start(GTK_BOX(xscaleHbox), xscaleLbl, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(xscaleHbox), xscaleSpin, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(yscaleHbox), yscaleLbl, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(yscaleHbox), yscaleSpin, FALSE, FALSE, 2);
	
	g_signal_connect(xscaleSpin, "value-changed", G_CALLBACK(setXscale), NULL);
	g_signal_connect(yscaleSpin, "value-changed", G_CALLBACK(setYscale), NULL);
	
	double f;
	// sync with config
	g_config->getOption("SDL.XScale", &f);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(xscaleSpin), f);
	g_config->getOption("SDL.YScale", &f);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(yscaleSpin), f);

	// show FPS check
	showFpsChk = gtk_check_button_new_with_label("Show FPS");
	g_signal_connect(showFpsChk, "clicked", G_CALLBACK(toggleOption), (gpointer)"SDL.ShowFPS");
	setCheckbox(showFpsChk, "SDL.ShowFPS");

	gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 5);	
	gtk_box_pack_start(GTK_BOX(vbox), hbox1, FALSE, FALSE, 5);
#ifdef OPENGL
	gtk_box_pack_start(GTK_BOX(vbox), glChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), linearChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), dbChk, FALSE, FALSE, 5);
#endif
	gtk_box_pack_start(GTK_BOX(vbox), palHbox, FALSE, FALSE,5);
	gtk_box_pack_start(GTK_BOX(vbox), ppuChk, FALSE, FALSE, 5);
#ifdef FRAMESKIP
	gtk_box_pack_start(GTK_BOX(vbox), frameskipChk, FALSE, FALSE, 5);
#endif 
	gtk_box_pack_start(GTK_BOX(vbox), spriteLimitChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), clipSidesChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), xscaleHbox, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), yscaleHbox, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), showFpsChk, FALSE, FALSE, 5);
	
	g_signal_connect(win, "delete-event", G_CALLBACK(closeVideoWin), NULL);
	g_signal_connect(win, "response", G_CALLBACK(closeVideoWin), NULL);
	
	gtk_widget_show_all(win);
	
	return;
}
const char* mixerStrings[6] = {"Volume", "Triangle", "Square1", "Square2", "Noise", "PCM"};

int mixerChanged(GtkWidget* w, gpointer p)
{
	int v = gtk_range_get_value(GTK_RANGE(w));
	GtkWidget* parent = gtk_widget_get_parent(w);
	char* lbl = (char*)gtk_frame_get_label(GTK_FRAME(parent));
	if(strcmp(lbl, "Volume") == 0)
	{
		g_config->setOption("SDL.Sound.Volume", v);
		FCEUI_SetSoundVolume(v);
	}
	if(strcmp(lbl, "Triangle") == 0)
	{
		g_config->setOption("SDL.Sound.TriangleVolume", v);
		FCEUI_SetTriangleVolume(v);
	}
	if(strcmp(lbl, "Square1") == 0)
	{
		g_config->setOption("SDL.Sound.Square1Volume", v);
		FCEUI_SetSquare1Volume(v);
	}
	if(strcmp(lbl, "Square2") == 0)
	{
		g_config->setOption("SDL.Sound.Square2Volume", v);
		FCEUI_SetSquare2Volume(v);
	}
	if(strcmp(lbl, "Noise") == 0)
	{
		g_config->setOption("SDL.Sound.NoiseVolume", v);
		FCEUI_SetNoiseVolume(v);
	}
	if(strcmp(lbl, "PCM") == 0)
	{
		g_config->setOption("SDL.Sound.PCMVolume", v);
		FCEUI_SetPCMVolume(v);
	}
	
	return 0;
}

void openSoundConfig(void)
{
	GtkWidget* win;
	GtkWidget* main_hbox;
	GtkWidget* vbox;
	GtkWidget* soundChk;
	GtkWidget* lowpassChk;
	GtkWidget* hbox1;
	GtkWidget* qualityCombo;
	GtkWidget* qualityLbl;
	GtkWidget* hbox2;
	GtkWidget* rateCombo;
	GtkWidget* rateLbl;
	GtkWidget* bufferLbl;
	GtkWidget* bufferHscale;
	GtkWidget* swapDutyChk;
	GtkWidget* mixerFrame;
	GtkWidget* mixerHbox;
	GtkWidget* mixers[6];
	GtkWidget* mixerFrames[6];

	win = gtk_dialog_new_with_buttons("Sound Preferences",
										GTK_WINDOW(MainWindow),
										(GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
										"_Close",
										GTK_RESPONSE_OK,
										NULL);
	gtk_window_set_icon_name(GTK_WINDOW(win), "audio-x-generic");
	main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

	// sound enable check
	soundChk = gtk_check_button_new_with_label("Enable sound");
	g_signal_connect(soundChk, "clicked", G_CALLBACK(toggleSound), NULL);
	setCheckbox(soundChk,"SDL.Sound");

	// low pass filter check
	lowpassChk = gtk_check_button_new_with_label("Enable low pass filter");
	g_signal_connect(lowpassChk, "clicked", G_CALLBACK(toggleLowPass), NULL);
	setCheckbox(lowpassChk, "SDL.Sound.LowPass");
	
	// sound quality combo box
	hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	qualityCombo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(qualityCombo), "Low");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(qualityCombo), "High");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(qualityCombo), "Very High");

	// sync widget with cfg 
	int buf;
	g_config->getOption("SDL.Sound.Quality", &buf);
	gtk_combo_box_set_active(GTK_COMBO_BOX(qualityCombo), buf);
		
	g_signal_connect(qualityCombo, "changed", G_CALLBACK(setQuality), NULL);
	
	qualityLbl = gtk_label_new("Quality: ");
	
	gtk_box_pack_start(GTK_BOX(hbox1), qualityLbl, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox1), qualityCombo, FALSE, FALSE, 5);
	
	// sound rate widgets
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	rateCombo = gtk_combo_box_text_new();
	
	const int rates[5] = {11025, 22050, 44100, 48000, 96000};
	
	char choices[8];
	for(int i=0; i<5;i++)
	{
		sprintf(choices, "%d", rates[i]);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rateCombo), choices);	
	}
	
	// sync widget with cfg 
	g_config->getOption("SDL.Sound.Rate", &buf);
	for(int i=0; i<5; i++)
		if(buf == rates[i])
			gtk_combo_box_set_active(GTK_COMBO_BOX(rateCombo), i);
		
	g_signal_connect(rateCombo, "changed", G_CALLBACK(setRate), NULL);
		
	// sound rate widgets
	rateLbl = gtk_label_new("Rate (Hz): ");
	
	gtk_box_pack_start(GTK_BOX(hbox2), rateLbl, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox2), rateCombo, FALSE, FALSE, 5);
	
	bufferHscale = gtk_scale_new_with_range( GTK_ORIENTATION_HORIZONTAL, 15, 200, 2);
	bufferLbl = gtk_label_new("Buffer size (in ms)");

	// sync widget with cfg 
	g_config->getOption("SDL.Sound.BufSize", &buf);
	gtk_range_set_value(GTK_RANGE(bufferHscale), buf);
	
	g_signal_connect(bufferHscale, "button-release-event", G_CALLBACK(setBufSize), NULL);

	// Swap duty cycles
	swapDutyChk = gtk_check_button_new_with_label("Swap Duty Cycles");
	g_signal_connect(swapDutyChk, "clicked", G_CALLBACK(toggleSwapDuty), NULL);
	setCheckbox(swapDutyChk, "SDL.SwapDuty");

	// mixer
	mixerFrame = gtk_frame_new("Mixer:");
	mixerHbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	for(long int i=0; i<6; i++)
	{
		mixers[i] = gtk_scale_new_with_range( GTK_ORIENTATION_VERTICAL, 0, 256, 1);
		gtk_range_set_inverted(GTK_RANGE(mixers[i]), TRUE);
		mixerFrames[i] = gtk_frame_new(mixerStrings[i]);
		gtk_container_add(GTK_CONTAINER(mixerFrames[i]), mixers[i]);
		gtk_box_pack_start(GTK_BOX(mixerHbox), mixerFrames[i], FALSE, TRUE, 5);
		g_signal_connect(mixers[i], "button-release-event", G_CALLBACK(mixerChanged), (gpointer)i); 
	}
	
	// sync with cfg
	int v;
	g_config->getOption("SDL.Sound.Volume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[0]), v);
	g_config->getOption("SDL.Sound.TriangleVolume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[1]), v);
	g_config->getOption("SDL.Sound.Square1Volume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[2]), v);
	g_config->getOption("SDL.Sound.Square2Volume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[3]), v);
	g_config->getOption("SDL.Sound.NoiseVolume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[4]), v);
	g_config->getOption("SDL.Sound.PCMVolume", &v);
	gtk_range_set_value(GTK_RANGE(mixers[5]), v);

	
	// packing some boxes
	
	gtk_box_pack_start(GTK_BOX(main_hbox), vbox, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), soundChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), lowpassChk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox1, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), bufferLbl, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), bufferHscale, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), swapDutyChk, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(main_hbox), mixerFrame, TRUE, TRUE, 5);
	gtk_container_add(GTK_CONTAINER(mixerFrame), mixerHbox);
	
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(win))), main_hbox, TRUE, TRUE, 0);
	
	g_signal_connect(win, "delete-event", G_CALLBACK(closeDialog), NULL);
	g_signal_connect(win, "response", G_CALLBACK(closeDialog), NULL);
	
	gtk_widget_show_all(win);
	
	return;
}

void quit (void)
{
	// manually flush GTK event queue
	while(gtk_events_pending())
		gtk_main_iteration_do(FALSE);

   if (surface)
   {
      cairo_surface_destroy (surface);
      surface = NULL;
   }
	// this is not neccesary to be explicitly called
	// it raises a GTK-Critical when its called
	//gtk_main_quit();
	FCEUI_CloseGame();
	FCEUI_Kill();
	// LoadGame() checks for an IP and if it finds one begins a network session
	// clear the NetworkIP field so this doesn't happen unintentionally
	g_config->setOption("SDL.NetworkIP", "");
	g_config->save();
	SDL_Quit();
	exit(0);
}
const char* Authors[]= {
	"Linux/SDL Developers:",
	" Lukas Sabota //punkrockguy318", " Soules", " Bryan Cain", " radsaq", " Shinydoofy",
	"FceuX 2.0 Developers:",
	" SP", " zeromus", " adelikat", " caH4e3", " qfox",
	" Luke Gustafson", " _mz", " UncombedCoconut", " DwEdit", " AnS", "rainwarrior", "feos",
	"Pre 2.0 Guys:",
	" Bero", " Xodnizel", " Aaron Oneal", " Joe Nahmias",
	" Paul Kuliniewicz", " Quietust", " Ben Parnell", " Parasyte &amp; bbitmaster",
	" blip &amp; nitsuja",
	"Included components:",
	" Mitsutaka Okazaki - YM2413 emulator", " Andrea Mazzoleni - Scale2x/Scale3x scalers", " Gilles Vollant - unzip.c PKZIP fileio",
	NULL};

void openAbout (void)
{
	GdkPixbuf* logo = gdk_pixbuf_new_from_xpm_data(icon_xpm);
	
	gtk_show_about_dialog(GTK_WINDOW(MainWindow),
		"program-name", "fceuX",
		"version", FCEU_VERSION_STRING,
		"copyright", "© 2016 FceuX development team",
		"license", "GPL-2; See COPYING",
		//"license-type", GTK_LICENSE_GPL_2_0,
		"website", "http://fceux.com",
		"authors", Authors,
		"logo", logo, NULL);
}

void toggleSound(GtkWidget* check, gpointer data)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
	{
		int last_soundopt;
		g_config->getOption("SDL.Sound", &last_soundopt);
		g_config->setOption("SDL.Sound", 1);
		if(GameInfo && !last_soundopt)
			InitSound();
	}
	else
	{
		g_config->setOption("SDL.Sound", 0);
		KillSound();
	}
}

void emuReset ()
{
	if(isloaded)
		ResetNES();
}

void hardReset ()
{
	if(isloaded)
	{
		closeGame();
		const char* lastFile;
		g_config->getOption("SDL.LastOpenFile", &lastFile);
		LoadGame(lastFile);
		resizeGtkWindow();
	}
}

void enableFullscreen (void)
{
	if(isloaded)
		ToggleFS();
}

void toggleMenuToggling(GtkCheckMenuItem *item,  gpointer  user_data)
{
	bool toggleMenu = gtk_check_menu_item_get_active(item);

	//printf("ToggleMenu: %i\n", (int)toggleMenu);
	g_config->setOption("SDL.ToggleMenu", (int)toggleMenu);
	menuTogglingEnabled = toggleMenu;
}

void toggleAutoResume(GtkCheckMenuItem *item,  gpointer  user_data)
{
	bool autoResume = gtk_check_menu_item_get_active(item);

	//printf("AutoResume: %i\n", (int)autoResume);
	g_config->setOption("SDL.AutoResume", (int)autoResume);
	AutoResumePlay = autoResume;
}

void recordMovie()
{
	if(isloaded)
	{
		std::string name = FCEU_MakeFName(FCEUMKF_MOVIE, 0, 0);
		FCEUI_printf("Recording movie to %s\n", name.c_str());
		FCEUI_SaveMovie(name.c_str(), MOVIE_FLAG_NONE, L"");
	}
    
	return;
}
void recordMovieAs ()
{
	if(!isloaded)
	{
		return;
	}
	GtkWidget* fileChooser;
	
	GtkFileFilter* filterFm2;
	GtkFileFilter* filterAll;
	
	filterFm2 = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterFm2, "*.fm2");
	gtk_file_filter_set_name(filterFm2, "FM2 Movies");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	fileChooser = gtk_file_chooser_dialog_new ("Save FM2 movie for recording", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Save", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(fileChooser), ".fm2");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterFm2);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), getcwd(NULL, 0));
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		std::string fname;
		
		fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		if (!fname.size())
			return; // no filename selected, quit the whole thing
		
		std::string s = GetUserText("Author name");
		std::wstring author(s.begin(), s.end());

		
		FCEUI_SaveMovie(fname.c_str(), MOVIE_FLAG_NONE, author);
	}
	gtk_widget_destroy (fileChooser);
}

void loadMovie (void)
{
	GtkWidget* fileChooser;
	
	GtkFileFilter* filterMovies;
	GtkFileFilter* filterFm2;
	GtkFileFilter* filterAll;

	filterMovies = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterMovies, "*.fm2");
	gtk_file_filter_add_pattern(filterMovies, "*.FM2f");
	gtk_file_filter_add_pattern(filterMovies, "*.fm3");
	gtk_file_filter_set_name(filterMovies, "FM2 Movies, TAS Editor Projects");

	filterFm2 = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterFm2, "*.fm2");
	gtk_file_filter_add_pattern(filterFm2, "*.FM2f");
	gtk_file_filter_set_name(filterFm2, "FM2 Movies");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	fileChooser = gtk_file_chooser_dialog_new ("Open FM2 Movie", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
			
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterMovies);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterFm2);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), getcwd(NULL, 0));
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* fname;
		
		fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		static int pauseframe;
		g_config->getOption("SDL.PauseFrame", &pauseframe);
		g_config->setOption("SDL.PauseFrame", 0);
		FCEUI_printf("Playing back movie located at %s\n", fname);
		if(FCEUI_LoadMovie(fname, false, pauseframe ? pauseframe : false) == FALSE)
		{
			GtkWidget* d;
			d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
				"Could not open the movie file.");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
		}
	}
	gtk_widget_destroy (fileChooser);
}

#ifdef _S9XLUA_H
void loadLua (void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterLua;
	GtkFileFilter* filterAll;
	
	filterLua = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterLua, "*.lua");
	gtk_file_filter_add_pattern(filterLua, "*.LUA");
	gtk_file_filter_set_name(filterLua, "Lua scripts");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	fileChooser = gtk_file_chooser_dialog_new ("Open LUA Script", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterLua);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	const char* last_file;
	g_config->getOption("SDL.LastLoadLua", &last_file);
	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fileChooser), last_file);
	
	if(strcmp(last_file, "") == 0)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), "/usr/share/fceux/luaScripts");
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		g_config->setOption("SDL.LastLoadLua", filename);
		gtk_widget_destroy(fileChooser);
		if(FCEU_LoadLuaCode(filename) == 0)
		{
			// This is necessary because lua scripts do not use FCEUD_PrintError to print errors.
			GtkWidget* d;
			d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
				"Could not open the selected lua script.");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
		}
		g_free(filename);
	}
	else
		gtk_widget_destroy (fileChooser);
}
#endif


void loadFdsBios (void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterDiskSys;
	GtkFileFilter* filterRom;
	GtkFileFilter* filterAll;
	
	
	filterDiskSys = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterDiskSys, "disksys.rom");
	gtk_file_filter_set_name(filterDiskSys, "disksys.rom");
	
	filterRom = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterRom, "*.rom");
	gtk_file_filter_set_name(filterRom, "*.rom");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	
	fileChooser = gtk_file_chooser_dialog_new ("Load FDS BIOS (disksys.rom)", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterDiskSys);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterRom);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), getcwd(NULL, 0));
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		// copy BIOS file to proper place (~/.fceux/disksys.rom)
		std::ifstream fdsBios (filename,std::fstream::binary);
		std::string output_filename = FCEU_MakeFName(FCEUMKF_FDSROM, 0, "");
		std::ofstream outFile (output_filename.c_str(),std::fstream::trunc|std::fstream::binary);
		outFile<<fdsBios.rdbuf();
		if(outFile.fail())
		{
			FCEUD_PrintError("Error copying the FDS BIOS file.");
		}
		else
		{	
			GtkWidget* d;
			d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, 
			"Famicom Disk System BIOS loaded.  If you are you having issues, make sure your BIOS file is 8KB in size.");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
		}
	}
	gtk_widget_destroy (fileChooser);
}

// TODO: is there somewhere else we can move this?  works for now though
void enableGameGenie(int enabled)
{
	g_config->setOption("SDL.GameGenie", enabled);
	g_config->save();
	FCEUI_SetGameGenie(enabled);
}

void toggleGameGenie(GtkCheckMenuItem *item, gpointer userData)
{
	enableGameGenie( gtk_check_menu_item_get_active(item) );
}

void togglePause(GtkMenuItem *item, gpointer userData )
{
	SDL_Event sdlev;
	int paused;
	
	if(isloaded)
	{
		paused = FCEUI_EmulationPaused();
	
		sdlev.type = SDL_FCEU_HOTKEY_EVENT;
		sdlev.user.code = HK_PAUSE;
		if(SDL_PushEvent(&sdlev) < 0)
		{
			FCEU_printf("Failed to push SDL event to %s game.\n", paused ? "resume" : "pause");
			return;
		}
		gtk_menu_item_set_label( item, paused ? "Pause" : "Resume");
		//gtk_action_set_stock_id(action, paused ? GTK_STOCK_MEDIA_PAUSE : GTK_STOCK_MEDIA_PLAY);
	}
}

void loadGameGenie ()
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterGG;
	GtkFileFilter* filterRom;
	GtkFileFilter* filterNes;
	GtkFileFilter* filterAll;
	
	
	filterGG = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterGG, "gg.rom");
	gtk_file_filter_set_name(filterGG, "gg.rom");
	
	filterRom = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterRom, "*.rom");
	gtk_file_filter_set_name(filterRom, "*.rom");
	
	filterNes = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterNes, "*.nes");
	gtk_file_filter_set_name(filterNes, "*.nes");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	
	fileChooser = gtk_file_chooser_dialog_new ("Load Game Genie ROM", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterGG);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterRom);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterNes);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), getcwd(NULL, 0));
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		// copy file to proper place (~/.fceux/gg.rom)
		std::ifstream f1 (filename,std::fstream::binary);
		std::string fn_out = FCEU_MakeFName(FCEUMKF_GGROM, 0, "");
		std::ofstream f2 (fn_out.c_str(),std::fstream::trunc|std::fstream::binary);
		gtk_widget_destroy (fileChooser);
		GtkWidget* d;
		enableGameGenie(TRUE);
		d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, 
     		"Game Genie ROM copied to: '%s'.", fn_out.c_str());
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);

		f2<<f1.rdbuf();
		g_free(filename);
	}
	else
		gtk_widget_destroy (fileChooser);

}

void loadNSF (void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterNSF;
	GtkFileFilter* filterZip;
	GtkFileFilter* filterAll;
	
	filterNSF = gtk_file_filter_new();
	filterZip = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterNSF, "*.nsf");
	gtk_file_filter_add_pattern(filterNSF, "*.NSF");
	gtk_file_filter_add_pattern(filterZip, "*.zip");
	gtk_file_filter_add_pattern(filterZip, "*.ZIP");
	gtk_file_filter_set_name(filterNSF, "NSF sound files");
	gtk_file_filter_set_name(filterZip, "Zip archives");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	fileChooser = gtk_file_chooser_dialog_new ("Open NSF File", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	
	const char* last_dir;
	g_config->getOption("SDL.LastOpenNSF", &last_dir);
	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fileChooser), last_dir);
	
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterNSF);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterZip);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		gtk_widget_destroy (fileChooser);
		LoadGame(filename);
		// no longer required with GTK FCEUD_PrintError implementation
		/*if(LoadGame(filename) == 0)
		{
			
			GtkWidget* d;
			d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
				"Could not open the selected NSF file.");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
		}*/
		g_config->setOption("SDL.LastOpenNSF", filename);
		g_free(filename);
	}
	else
		gtk_widget_destroy (fileChooser);
}

void closeGame(void)
{
	//GdkColor bg = {0, 0, 0, 0};
	//gtk_widget_modify_bg(evbox, GTK_STATE_NORMAL, &bg);
	CloseGame(); 
}

void loadGame (void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterFCEU;
	GtkFileFilter* filterNes;
	GtkFileFilter* filterFds;
	GtkFileFilter* filterNSF;
	GtkFileFilter* filterZip;
	GtkFileFilter* filterAll;
	
	filterFCEU = gtk_file_filter_new();
	filterNes = gtk_file_filter_new();
	filterFds = gtk_file_filter_new();
	filterNSF = gtk_file_filter_new();
	filterZip = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterFCEU, "*.nes");
	gtk_file_filter_add_pattern(filterFCEU, "*.NES");
	gtk_file_filter_add_pattern(filterFCEU, "*.fds");
	gtk_file_filter_add_pattern(filterFCEU, "*.FDS");
	gtk_file_filter_add_pattern(filterFCEU, "*.zip");
	gtk_file_filter_add_pattern(filterFCEU, "*.ZIP");
	gtk_file_filter_add_pattern(filterFCEU, "*.Nes");
	gtk_file_filter_add_pattern(filterFCEU, "*.Fds");
	gtk_file_filter_add_pattern(filterFCEU, "*.Zip");
	gtk_file_filter_add_pattern(filterFCEU, "*.nsf");
	gtk_file_filter_add_pattern(filterFCEU, "*.NSF");
	gtk_file_filter_add_pattern(filterNes, "*.nes");
	gtk_file_filter_add_pattern(filterNes, "*.NES");
	gtk_file_filter_add_pattern(filterFds, "*.fds");
	gtk_file_filter_add_pattern(filterFds, "*.FDS");
	gtk_file_filter_add_pattern(filterNSF, "*.nsf");
	gtk_file_filter_add_pattern(filterNSF, "*.NSF");
	gtk_file_filter_add_pattern(filterZip, "*.zip");
	gtk_file_filter_add_pattern(filterZip, "*.zip");
	gtk_file_filter_set_name(filterFCEU, "*.nes;*.fds;*.nsf;*.zip");
	gtk_file_filter_set_name(filterNes, "NES ROM files");
	gtk_file_filter_set_name(filterFds, "FDS ROM files");
	gtk_file_filter_set_name(filterNSF, "NSF sound files");
	gtk_file_filter_set_name(filterZip, "Zip archives");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	
	
	fileChooser = gtk_file_chooser_dialog_new ("Open ROM", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
	const char* last_dir;
	g_config->getOption("SDL.LastOpenFile", &last_dir);
	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fileChooser), last_dir);
	
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterFCEU);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterNes);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterFds);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterNSF);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterZip);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		gtk_widget_destroy (fileChooser);
		g_config->setOption("SDL.LastOpenFile", filename);
		closeGame();
		LoadGame(filename);
		// Error dialog no longer required with GTK implementation of FCEUD_PrintError()
		/*if(LoadGame(filename) == 0)
		{
			
			GtkWidget* d;
			d = gtk_message_dialog_new(GTK_WINDOW(MainWindow), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
				"Could not open the selected ROM file.");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
		}*/
		resizeGtkWindow();
		g_free(filename);
	}
	else
		gtk_widget_destroy (fileChooser);
}

void saveStateAs(void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterSav;
	GtkFileFilter* filterAll;
	
	filterSav = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterSav, "*.sav");
	gtk_file_filter_add_pattern(filterSav, "*.SAV");
	gtk_file_filter_set_name(filterSav, "SAV files");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	const char* last_dir;
	g_config->getOption("SDL.LastSaveStateAs", &last_dir);
	
	fileChooser = gtk_file_chooser_dialog_new ("Save State As", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Save", GTK_RESPONSE_ACCEPT, NULL);
			
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), last_dir);
	
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(fileChooser), ".sav");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterSav);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		FCEUI_SaveState(filename);
		g_config->setOption("SDL.LastSaveStateAs", filename);
		g_free(filename);
	}
	gtk_widget_destroy (fileChooser);
	
	
}

void loadStateFrom(void)
{
	GtkWidget* fileChooser;
	GtkFileFilter* filterFcs;
	GtkFileFilter* filterSav;
	GtkFileFilter* filterAll;
	
	filterSav = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterSav, "*.sav");
	gtk_file_filter_add_pattern(filterSav, "*.SAV");
	gtk_file_filter_set_name(filterSav, "SAV files");
	
	filterFcs = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterFcs, "*.fc?");
	gtk_file_filter_add_pattern(filterFcs, "*.FC?");
	gtk_file_filter_set_name(filterFcs, "FCS files");
	
	filterAll = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filterAll, "*");
	gtk_file_filter_set_name(filterAll, "All Files");
	
	fileChooser = gtk_file_chooser_dialog_new ("Load State From", GTK_WINDOW(MainWindow),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT, NULL);
			
	const char* last_dir;
	g_config->getOption("SDL.LastLoadStateFrom", &last_dir);
	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fileChooser), last_dir);

	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterFcs);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterSav);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
	
	if (gtk_dialog_run (GTK_DIALOG (fileChooser)) ==GTK_RESPONSE_ACCEPT)
	{
		char* filename;
		
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fileChooser));
		FCEUI_LoadState(filename);
		g_config->setOption("SDL.LastLoadStateFrom", filename);
		g_free(filename);
	}
	gtk_widget_destroy (fileChooser);
}

void quickLoad(void)
{
	FCEUI_LoadState(NULL);
}

void quickSave(void)
{
	FCEUI_SaveState(NULL);
}

//void changeState(GtkAction *action, GtkRadioAction *current, gpointer data)
void	changeState(GtkRadioMenuItem *radiomenuitem,
                   gpointer          user_data)
{
	//printf("Changing State: %li\n", (long)user_data);
	FCEUI_SelectState( (long)user_data, 0);
}
#if SDL_VERSION_ATLEAST(2, 0, 0)                                                    
// SDL 1.2/2.0 compatibility macros
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#define SDLK_PRINT SDLK_PRINTSCREEN
#define SDLK_BREAK 0
#define SDLK_COMPOSE 0
#define SDLK_NUMLOCK SDLK_NUMLOCKCLEAR
#define SDLK_KP0 SDLK_KP_0
#define SDLK_KP1 SDLK_KP_1
#define SDLK_KP2 SDLK_KP_2
#define SDLK_KP3 SDLK_KP_3
#define SDLK_KP4 SDLK_KP_4
#define SDLK_KP5 SDLK_KP_5
#define SDLK_KP6 SDLK_KP_6
#define SDLK_KP7 SDLK_KP_7
#define SDLK_KP8 SDLK_KP_8
#define SDLK_KP9 SDLK_KP_9
#define SDLK_LSUPER SDLK_LGUI
#define SDLK_RSUPER SDLK_RGUI
#define SDLK_LMETA 0
#define SDLK_RMETA 0
#endif
// Adapted from Gens/GS.  Converts a GDK key value into an SDL key value.
unsigned short GDKToSDLKeyval(int gdk_key)
{
	if (!(gdk_key & 0xFF00))
	{
		// ASCII symbol.
		// SDL and GDK use the same values for these keys.
		
		// Make sure the key value is lowercase.
		gdk_key = tolower(gdk_key);
		
		// Return the key value.
		return gdk_key;
	}
	
	if (gdk_key & 0xFFFF0000)
	{
		// Extended X11 key. Not supported by SDL.
#ifdef GDK_WINDOWING_X11
		fprintf(stderr, "Unhandled extended X11 key: 0x%08X (%s)", gdk_key, XKeysymToString(gdk_key));
#else
		fprintf(stderr, "Unhandled extended key: 0x%08X\n", gdk_key);
#endif
		return 0;
	}
	
	// Non-ASCII symbol.
	static const uint16_t gdk_to_sdl_table[0x100] =
	{
		// 0x00 - 0x0F
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		SDLK_BACKSPACE, SDLK_TAB, SDLK_RETURN, SDLK_CLEAR,
		0x0000, SDLK_RETURN, 0x0000, 0x0000,
		
		// 0x10 - 0x1F
		0x0000, 0x0000, 0x0000, SDLK_PAUSE,
		SDLK_SCROLLOCK, SDLK_SYSREQ, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, SDLK_ESCAPE,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x20 - 0x2F
		SDLK_COMPOSE, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x30 - 0x3F [Japanese keys]
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x40 - 0x4F [unused]
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x50 - 0x5F
		SDLK_HOME, SDLK_LEFT, SDLK_UP, SDLK_RIGHT,
		SDLK_DOWN, SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_END,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x60 - 0x6F
		0x0000, SDLK_PRINT, 0x0000, SDLK_INSERT,
		SDLK_UNDO, 0x0000, 0x0000, SDLK_MENU,
		0x0000, SDLK_HELP, SDLK_BREAK, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0x70 - 0x7F [mostly unused, except for Alt Gr and Num Lock]
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, SDLK_MODE, SDLK_NUMLOCK,
		
		// 0x80 - 0x8F [mostly unused, except for some numeric keypad keys]
		SDLK_KP5, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, SDLK_KP_ENTER, 0x0000, 0x0000,
		
		// 0x90 - 0x9F
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, SDLK_KP7, SDLK_KP4, SDLK_KP8,
		SDLK_KP6, SDLK_KP2, SDLK_KP9, SDLK_KP3,
		SDLK_KP1, SDLK_KP5, SDLK_KP0, SDLK_KP_PERIOD,
		
		// 0xA0 - 0xAF
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, SDLK_KP_MULTIPLY, SDLK_KP_PLUS,
		0x0000, SDLK_KP_MINUS, SDLK_KP_PERIOD, SDLK_KP_DIVIDE,
		
		// 0xB0 - 0xBF
		SDLK_KP0, SDLK_KP1, SDLK_KP2, SDLK_KP3,
		SDLK_KP4, SDLK_KP5, SDLK_KP6, SDLK_KP7,
		SDLK_KP8, SDLK_KP9, 0x0000, 0x0000,
		0x0000, SDLK_KP_EQUALS, SDLK_F1, SDLK_F2,
		
		// 0xC0 - 0xCF
		SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6,
		SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10,
		SDLK_F11, SDLK_F12, SDLK_F13, SDLK_F14,
		SDLK_F15, 0x0000, 0x0000, 0x0000,
		
		// 0xD0 - 0xDF [L* and R* function keys]
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		
		// 0xE0 - 0xEF
		0x0000, SDLK_LSHIFT, SDLK_RSHIFT, SDLK_LCTRL,
		SDLK_RCTRL, SDLK_CAPSLOCK, 0x0000, SDLK_LMETA,
		SDLK_RMETA, SDLK_LALT, SDLK_RALT, SDLK_LSUPER,
		SDLK_RSUPER, 0x0000, 0x0000, 0x0000,
		
		// 0xF0 - 0xFF [mostly unused, except for Delete]
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, SDLK_DELETE,		
	};
	
	unsigned short sdl_key = gdk_to_sdl_table[gdk_key & 0xFF];
	if (sdl_key == 0)
	{
		// Unhandled GDK key.
		fprintf(stderr, "Unhandled GDK key: 0x%04X (%s)", gdk_key, gdk_keyval_name(gdk_key));
		return 0;
	}
	
	// ignore pause and screenshot hotkeys since they is handled by GTK+ as accelerators
	if (sdl_key == Hotkeys[HK_PAUSE] || sdl_key == Hotkeys[HK_SCREENSHOT] || 
		sdl_key == Hotkeys[HK_SAVE_STATE] || sdl_key == Hotkeys[HK_LOAD_STATE])
		return 0;
	
	return sdl_key;
}


// Function adapted from Gens/GS (source/gens/input/input_sdl.c)
static gint convertKeypress(GtkWidget *grab, GdkEventKey *event, gpointer user_data)
{
	SDL_Event sdlev;
	int keystate;
#if SDL_VERSION_ATLEAST(2, 0, 0) 
	SDL_Keycode sdlkey;
#else
	SDLKey sdlkey;
#endif	
	switch (event->type)
	{
		case GDK_KEY_PRESS:
			sdlev.type = SDL_KEYDOWN;
			sdlev.key.state = SDL_PRESSED;
			keystate = 1;
			break;
		
		case GDK_KEY_RELEASE:
			sdlev.type = SDL_KEYUP;
			sdlev.key.state = SDL_RELEASED;
			keystate = 0;
			break;
		
		default:
			fprintf(stderr, "Unhandled GDK event type: %d", event->type);
			return FALSE;
	}
	
	// Convert this keypress from GDK to SDL.
#if SDL_VERSION_ATLEAST(2, 0, 0)
	sdlkey = GDKToSDLKeyval(event->keyval);
#else
	sdlkey = (SDLKey)GDKToSDLKeyval(event->keyval);
#endif
	
	// Create an SDL event from the keypress.
	sdlev.key.keysym.sym = sdlkey;
	if (sdlkey != 0)
	{
		SDL_PushEvent(&sdlev);
		
		// Only let the emulator handle the key event if this window has the input focus.
		if(keystate == 0 || gtk_window_is_active(GTK_WINDOW(MainWindow)))
		{
			#if SDL_VERSION_ATLEAST(2, 0, 0)
			// Not sure how to do this yet with SDL 2.0
			// TODO - SDL 2.0
			//SDL_GetKeyboardState(NULL)[SDL_GetScancodeFromKey(sdlkey)] = keystate;
			#else
			SDL_GetKeyState(NULL)[sdlkey] = keystate;
			#endif
		}
	}
	
	// Allow GTK+ to process this key.
	return FALSE;
}

static GtkWidget* CreateMenubar( GtkWidget* window)
{
   GtkWidget *menubar, *menu, *submenu, *item;
	GSList *radioGroup;
	GtkAccelGroup *accel_group;

	// Create Menu Bar
   menubar = gtk_menu_bar_new();

	// Create a GtkAccelGroup and add it to the window.
   accel_group = gtk_accel_group_new ();

   gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

	//---------------------------------------
	// Create File Menu
   item = gtk_menu_item_new_with_label("File");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load File Menu Items
	//-File --> Open ROM ---------------------
   item = gtk_menu_item_new_with_label("Open ROM");

   g_signal_connect( item, "activate", G_CALLBACK(loadGame), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Close ROM ------------------
   item = gtk_menu_item_new_with_label("Close ROM");

   g_signal_connect( item, "activate", G_CALLBACK(closeGame), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Play NSF ------------------
	item = gtk_menu_item_new_with_label("Play NSF");

   g_signal_connect( item, "activate", G_CALLBACK(loadNSF), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Load State From ------------------
	item = gtk_menu_item_new_with_label("Load State From");

   g_signal_connect( item, "activate", G_CALLBACK(loadStateFrom), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Save State As ------------------
	item = gtk_menu_item_new_with_label("Save State As");

   g_signal_connect( item, "activate", G_CALLBACK(saveStateAs), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Quick Load ------------------
	item = gtk_menu_item_new_with_label("Quick Load");

   g_signal_connect( item, "activate", G_CALLBACK(quickLoad), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_F7, (GdkModifierType)0, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Quick Save ------------------
	item = gtk_menu_item_new_with_label("Quick Save");

   g_signal_connect( item, "activate", G_CALLBACK(quickSave), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_F5, (GdkModifierType)0, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Change State ------------------
	item = gtk_menu_item_new_with_label("Change State");

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
	
   submenu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), submenu );

	//-File --> Change State --> State 0:9 ------------------
	radioGroup = NULL;

	for (long int i=0; i<10; i++)
	{
		char stmp[32];

		sprintf( stmp, "%li", i );

		item = gtk_radio_menu_item_new_with_label( radioGroup, stmp);
	
		radioGroup = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM(item) );
	
	   gtk_menu_shell_append( GTK_MENU_SHELL(submenu), item );

      g_signal_connect( item, "activate", G_CALLBACK(changeState), (gpointer)i);
	}

#ifdef _S9XLUA_H
	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> Load Lua Script ------------------
	item = gtk_menu_item_new_with_label("Load Lua Script");

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
	
   g_signal_connect( item, "activate", G_CALLBACK(loadLua), NULL);
#endif

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-File --> ScreenShot ------------------
	item = gtk_menu_item_new_with_label("Screenshot");

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
	
   g_signal_connect( item, "activate", G_CALLBACK(FCEUI_SaveSnapshot), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_F12, (GdkModifierType)0, GTK_ACCEL_VISIBLE);

	//-File --> Quit ------------------
	item = gtk_menu_item_new_with_label("Quit");

   g_signal_connect( item, "activate", G_CALLBACK(quit), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//---------------------------------------
	// Create Options Menu
   item = gtk_menu_item_new_with_label("Options");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load Options Menu Items
	//-Options --> Gamepad Config ---------------------
   item = gtk_menu_item_new_with_label("Gamepad Config");

   g_signal_connect( item, "activate", G_CALLBACK(openGamepadConfig), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Hotkey Config ---------------------
   item = gtk_menu_item_new_with_label("Hotkey Config");

   g_signal_connect( item, "activate", G_CALLBACK(openHotkeyConfig), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Sound Config ---------------------
   item = gtk_menu_item_new_with_label("Sound Config");

   g_signal_connect( item, "activate", G_CALLBACK(openSoundConfig), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Video Config ---------------------
   item = gtk_menu_item_new_with_label("Video Config");

   g_signal_connect( item, "activate", G_CALLBACK(openVideoConfig), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Palette Config ---------------------
   item = gtk_menu_item_new_with_label("Palette Config");

   g_signal_connect( item, "activate", G_CALLBACK(openPaletteConfig), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Network Config ---------------------
   item = gtk_menu_item_new_with_label("Network Config");

   g_signal_connect( item, "activate", G_CALLBACK(openNetworkConfig), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Auto-Resume Play ---------------------
   item = gtk_check_menu_item_new_with_label("Auto-Resume Play");

	gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(item), AutoResumePlay);

   g_signal_connect( item, "toggled", G_CALLBACK(toggleAutoResume), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Toggle Menubar ---------------------
   item = gtk_check_menu_item_new_with_label("Toggle Menubar (alt)");

	//gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(item), FALSE);

   g_signal_connect( item, "toggled", G_CALLBACK(toggleMenuToggling), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Options --> Fullscreen ---------------------
   item = gtk_menu_item_new_with_label("Fullscreen");

   g_signal_connect( item, "activate", G_CALLBACK(enableFullscreen), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_Return, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//---------------------------------------
	// Create Emulation Menu
   item = gtk_menu_item_new_with_label("Emulation");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load Emulation Menu Items
	//-Emulation --> Power ---------------------
   item = gtk_menu_item_new_with_label("Power");

   g_signal_connect( item, "activate", G_CALLBACK(FCEUI_PowerNES), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> Reset ---------------------
	item = gtk_menu_item_new_with_label("Reset");

   g_signal_connect( item, "activate", G_CALLBACK(hardReset), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> Soft Reset ---------------------
	item = gtk_menu_item_new_with_label("Soft Reset");

   g_signal_connect( item, "activate", G_CALLBACK(emuReset), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> Pause ---------------------
	item = gtk_menu_item_new_with_label("Pause");

   g_signal_connect( item, "activate", G_CALLBACK(togglePause), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_Pause, (GdkModifierType)0, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulator --> Enable Game Genie ---------------------
   item = gtk_check_menu_item_new_with_label("Enable Game Genie");

	int gameGenieEnabled=0;
	g_config->getOption("SDL.GameGenie", &gameGenieEnabled);
	gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(item), gameGenieEnabled);

   g_signal_connect( item, "toggled", G_CALLBACK(toggleGameGenie), NULL);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> Load Game Genie ROM ---------------------
	item = gtk_menu_item_new_with_label("Load Game Genie ROM");

   g_signal_connect( item, "activate", G_CALLBACK(loadGameGenie), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> Insert Coin ---------------------
	item = gtk_menu_item_new_with_label("Insert Coin");

   g_signal_connect( item, "activate", G_CALLBACK(FCEUI_VSUniCoin), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Emulation --> FDS ------------------
	item = gtk_menu_item_new_with_label("FDS");

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
	
   submenu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), submenu );

	//-Emulation --> FDS --> Switch Disk ---------------------
	item = gtk_menu_item_new_with_label("Switch Disk");

   g_signal_connect( item, "activate", G_CALLBACK(FCEU_FDSSelect), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(submenu), item );

	//-Emulation --> FDS --> Eject Disk ---------------------
	item = gtk_menu_item_new_with_label("Eject Disk");

   g_signal_connect( item, "activate", G_CALLBACK(FCEU_FDSInsert), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(submenu), item );

	//-Emulation --> FDS --> Load BIOS File ---------------------
	item = gtk_menu_item_new_with_label("Load BIOS File");

   g_signal_connect( item, "activate", G_CALLBACK(loadFdsBios), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(submenu), item );

	//---------------------------------------
	// Create Tools Menu
   item = gtk_menu_item_new_with_label("Tools");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load Tools Menu Items
	//-Tools --> Cheats ---------------------
   item = gtk_menu_item_new_with_label("Cheats...");

   g_signal_connect( item, "activate", G_CALLBACK(openCheatsWindow), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Tools --> Ram Watch ---------------------
   item = gtk_menu_item_new_with_label("Ram Watch...");

   g_signal_connect( item, "activate", G_CALLBACK(openMemoryWatchWindow), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//---------------------------------------
	// Create Movie Menu
   item = gtk_menu_item_new_with_label("Movie");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load Movie Menu Items
	//-Movie --> Open ---------------------
   item = gtk_menu_item_new_with_label("Open");

   g_signal_connect( item, "activate", G_CALLBACK(loadMovie), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_F7, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Movie --> Stop ---------------------
   item = gtk_menu_item_new_with_label("Stop");

   g_signal_connect( item, "activate", G_CALLBACK(FCEUI_StopMovie), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_F7, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Add Separator
	item = gtk_separator_menu_item_new();
   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Movie --> Record ---------------------
   item = gtk_menu_item_new_with_label("Record");

   g_signal_connect( item, "activate", G_CALLBACK(recordMovie), NULL);

	gtk_widget_add_accelerator( item, "activate", accel_group,
                              GDK_KEY_F5, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//-Movie --> Record As ---------------------
   item = gtk_menu_item_new_with_label("Record As");

   g_signal_connect( item, "activate", G_CALLBACK(recordMovieAs), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_F5, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	//---------------------------------------
	// Create Help Menu
   item = gtk_menu_item_new_with_label("Help");

   gtk_menu_shell_append( GTK_MENU_SHELL(menubar), item );

   menu = gtk_menu_new();

   gtk_menu_item_set_submenu( GTK_MENU_ITEM(item), menu );

	// Load Help Menu Items
	//-Help --> About ---------------------
   item = gtk_menu_item_new_with_label("About");

   g_signal_connect( item, "activate", G_CALLBACK(openAbout), NULL);

	//gtk_widget_add_accelerator( item, "activate", accel_group,
   //                           GDK_KEY_F7, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);

   gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );

	// Finally, return the actual menu bar created
	return menubar;
}

void pushOutputToGTK(const char* str)
{
	// we don't really do anything with the output right now
	return;
}

void showGui(bool b)
{
	if(b)
		gtk_widget_show_all(MainWindow);
	else
		gtk_widget_hide(MainWindow);
}

gint handleKeyRelease(GtkWidget* w, GdkEvent* event, gpointer cb_data)
{
	if(menuTogglingEnabled)
	{
		static bool menuShown = true;
		if(((GdkEventKey*)event)->keyval == GDK_KEY_Alt_L || ((GdkEventKey*)event)->keyval == GDK_KEY_Alt_R)
		{
			if(menuShown)
			{
				gtk_widget_hide(Menubar);
				menuShown = false;
			}
			else
			{
				gtk_widget_show(Menubar);
				menuShown = true;
			}
		}
	}
	return 0;
};

int GtkMouseData[3] = {0,0,0};

gint handleMouseClick(GtkWidget* widget, GdkEvent *event, gpointer callback_data)
{
	GtkMouseData[0] = ((GdkEventButton*)event)->x;
	GtkMouseData[1] = ((GdkEventButton*)event)->y;
	int button = ((GdkEventButton*)event)->button;
	if(!(((GdkEventButton*)event)->type == GDK_BUTTON_PRESS))
		GtkMouseData[2] = 0;
	else
	{
		if(button == 1)
			GtkMouseData[2] |= 0x1;
		if(button == 3)
			GtkMouseData[2] |= 0x3;
	}

	// this doesn't work because we poll the mouse position rather
	// than use events
	/*
	SDL_Event sdlev;
	sdlev.type = SDL_MOUSEBUTTONDOWN;
	if(((GdkEventButton*)event)->type == GDK_BUTTON_PRESS)
		 sdlev.button.type = SDL_MOUSEBUTTONDOWN;
	else
		sdlev.button.type = SDL_MOUSEBUTTONUP;
	sdlev.button.button = ((GdkEventButton*)event)->button;
	sdlev.button.state = ((GdkEventButton*)event)->state;
	sdlev.button.x = ((GdkEventButton*)event)->x;
	sdlev.button.y = ((GdkEventButton*)event)->y;

	SDL_PushEvent(&sdlev);
	*/
  
	return 0;
}

gboolean handle_resize(GtkWindow* win, GdkEvent* event, gpointer data)
{
   cairo_t *cr = NULL;
	// This should handle resizing so the emulation takes up as much
	// of the GTK window as possible


	// get new window width/height
	int width, height;
	width = event->configure.width;
	height = event->configure.height;
	printf("DEBUG: Configure new window size: %dx%d\n", width, height);

	// get width/height multipliers
	double xscale = width / (double)NES_WIDTH;
	double yscale = height / (double)NES_HEIGHT;

	// TODO check KeepRatio (where is this)
	// do this to keep aspect ratio
	if(xscale > yscale)
		xscale = yscale;
	if(yscale > xscale)
		yscale = xscale;

	//TODO if openGL make these integers
	g_config->setOption("SDL.XScale", xscale);
	g_config->setOption("SDL.YScale", yscale);
    //gtk_widget_realize(evbox);
	flushGtkEvents();
	if(GameInfo != 0)
	{
		KillVideo();
		InitVideo(GameInfo);
   }


   if (surface)
   {
      cairo_surface_destroy (surface);
   }
   surface = gdk_window_create_similar_surface (gtk_widget_get_window (evbox),
                                                CAIRO_CONTENT_COLOR,
                                                gtk_widget_get_allocated_width (evbox),
                                                gtk_widget_get_allocated_height (evbox));


   if ( surface != NULL )
   {
      cr = cairo_create (surface);
      
      if ( cr != NULL )
      {
         cairo_set_source_rgb (cr, 0, 0, 0);
         cairo_paint (cr);
         
         cairo_destroy (cr);
      }
   }

	//gtk_widget_set_size_request(evbox, (int)(NES_WIDTH*xscale), (int)(NES_HEIGHT*yscale));

	// Currently unused; unsure why
	/*	GdkColor black;
	black.red = 0;
	black.green = 0;
	black.blue = 0;
	gtk_widget_modify_bg(GTK_WIDGET(win), GTK_STATE_NORMAL, &black);*/

	printf("DEBUG: new xscale: %f yscale: %f\n", xscale, yscale);

	return FALSE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
draw_cb (GtkWidget *widget,
         cairo_t   *cr,
         gpointer   data)
{
   // Only clear the screen if a game is not loaded
   if (GameInfo == 0)
   {
      cairo_set_source_surface (cr, surface, 0, 0);
      cairo_paint (cr);
   }

  return FALSE;
}


int InitGTKSubsystem(int argc, char** argv)
{
	GtkWidget* vbox;
	
	MainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_events(GTK_WIDGET(MainWindow), GDK_KEY_RELEASE_MASK);
//	gtk_window_set_policy (GTK_WINDOW (MainWindow), FALSE, FALSE, TRUE);
	gtk_window_set_resizable(GTK_WINDOW(MainWindow), TRUE);
	gtk_window_set_title(GTK_WINDOW(MainWindow), FCEU_NAME_AND_VERSION);
	gtk_window_set_default_size(GTK_WINDOW(MainWindow), NES_WIDTH, NES_HEIGHT);
	
	GdkPixbuf* icon = gdk_pixbuf_new_from_xpm_data(icon_xpm);
	gtk_window_set_default_icon(icon);
	gtk_window_set_icon(GTK_WINDOW(MainWindow), icon);
	
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(MainWindow), vbox);
	
	Menubar = CreateMenubar(MainWindow);

	gtk_box_pack_start (GTK_BOX(vbox), Menubar, FALSE, TRUE, 0);
		
	// PRG: this code here is the the windowID "hack" to render SDL
	// in a GTK window.	however, I can't get it to work right now
	// so i'm commenting it out and haivng a seperate GTK2 window with 
	// controls
	// 12/21/09
	// ---
	// uncommented and fixed by Bryan Cain
	// 1/24/11
	//
	// prg - Bryan Cain, you are the man!
	
	//evbox = gtk_event_box_new();
	evbox = gtk_drawing_area_new();
	gtk_box_pack_start (GTK_BOX(vbox), evbox, TRUE, TRUE, 0);
	
	double xscale, yscale;
	g_config->getOption("SDL.XScale", &xscale);
	g_config->getOption("SDL.YScale", &yscale);

	gtk_widget_set_size_request(evbox, NES_WIDTH*xscale, NES_HEIGHT*yscale);
	gtk_widget_realize(evbox);
	gtk_widget_show(evbox);
	gtk_widget_show_all(vbox);
	
	//GdkColor bg = {0, 0, 0, 0};
	//gtk_widget_modify_bg(evbox, GTK_STATE_NORMAL, &bg);
	
	// set up keypress "snooper" to convert GDK keypress events into SDL keypresses
	//gtk_key_snooper_install(convertKeypress, NULL);
	g_signal_connect(G_OBJECT(MainWindow), "key-press-event", G_CALLBACK(convertKeypress), NULL);
	g_signal_connect(G_OBJECT(MainWindow), "key-release-event", G_CALLBACK(convertKeypress), NULL);

	// pass along mouse data from GTK to SDL
	g_signal_connect(G_OBJECT(evbox), "button-press-event", G_CALLBACK(handleMouseClick), NULL);
	g_signal_connect(G_OBJECT(evbox), "button-release-event", G_CALLBACK(handleMouseClick), NULL);

	//g_signal_connect(G_OBJECT(MainWindow), "key-release-event", G_CALLBACK(handleKeyRelease), NULL);
	
	// signal handlers
	g_signal_connect(MainWindow, "delete-event", quit, NULL);
	g_signal_connect(MainWindow, "destroy-event", quit, NULL);
	// resize handler
	g_signal_connect(MainWindow, "configure-event", G_CALLBACK(handle_resize), NULL);
	g_signal_connect( evbox, "draw", G_CALLBACK(draw_cb), NULL);
	
	gtk_widget_show_all(MainWindow);
	
	GtkRequisition req;
	gtk_widget_get_preferred_size(GTK_WIDGET(MainWindow), NULL, &req);
	//printf("Init Resize: w:%i  h:%i \n", req.width, req.height );
	gtk_window_resize(GTK_WINDOW(MainWindow), req.width, req.height );

   // Once the window has been resized, return draw area size request to minimum values
	gtk_widget_set_size_request(evbox, NES_WIDTH, NES_HEIGHT);
	gtkIsStarted = true;
	 
	return 0;
}

