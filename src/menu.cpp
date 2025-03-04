/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *  Copyright (c) 2016 Antonio Radojkovic <antonior.software@gmail.com>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include <math.h>

#include "common.h"

#include "dev.h"

#include "ui/volumewindow.h"

#include "menu.h"
#include "lisp.h"
#include "game.h"
#include "timing.h"
#include "game.h"
#include "id.h"
#include "pmenu.h"
#include "gui.h"
#include "property.h"
#include "clisp.h"
#include "gamma.h"
#include "dprint.h"
#include "demo.h"
#include "loadgame.h"
#include "scroller.h"
#include "netcfg.h"

#include "net/sock.h"

//AR
#include "sdlport/setup.h"
extern Settings settings;
extern int get_key_binding( char const *dir, int i );
//

extern net_protocol *prot;

static VolumeWindow *volume_window;

//percent is 0..256
static void TintArea(ivec2 aa, ivec2 bb,
                     int r_to, int g_to, int b_to, int percent)
{
    ivec2 caa, cbb;
    main_screen->GetClip(caa, cbb);
    aa = Max(aa, caa);
    bb = Min(bb, cbb);

    if (!(aa < bb))
        return;

    percent = 256 - percent;

    main_screen->Lock();
    for (int y = aa.y; y < bb.y; y++)
    {
        uint8_t *sl = main_screen->scan_line(y) + aa.x;
        for (int x = aa.x; x < bb.x; x++, sl++)
        {
            uint8_t *paddr = (uint8_t *)pal->addr() + (*sl) * 3;
            uint8_t r = (((paddr[0] - r_to) * percent) >> 8) + r_to;
            uint8_t g = (((paddr[1] - g_to) * percent) >> 8) + g_to;
            uint8_t b = (((paddr[2] - b_to) * percent) >> 8) + b_to;
            *sl = color_table->Lookup((r) >> 3, (g) >> 3, (b) >> 3);
        }
    }
    main_screen->AddDirty(aa, bb);
    main_screen->Unlock();
}

void DarkWidget(ivec2 aa, ivec2 bb, int br, int dr, int amount)
{
    main_screen->AddDirty(aa, bb);
    main_screen->Line(aa, ivec2(aa.x, bb.y - 1), br);
    main_screen->Line(aa, ivec2(bb.x - 1, aa.y), br);
    main_screen->Line(ivec2(bb.x - 1, aa.y + 1), bb - ivec2(1), dr);
    main_screen->Line(ivec2(aa.x + 1, bb.y - 1), bb - ivec2(1), dr);
    TintArea(aa + ivec2(1), bb, 0, 0, 0, amount);
}

char *men_str(void *arg)
{
  switch (item_type(arg))
  {
    case L_STRING :
    { return lstring_value(arg); } break;
    case L_CONS_CELL :
    { return lstring_value(CAR(arg)); } break;
    default :
    {
      ((LObject *)arg)->Print();
      printf(" is not a valid menu option\n");
      exit(0);
    }
  }
  return NULL;
}

//
// This method is only used by the (menu) Lisp method, which was
// never tested.
//
int menu(void *args, JCFont *font)             // reurns -1 on esc
{
  main_menu();
  char *title=NULL;
  if (!NILP(CAR(args)))
    title=lstring_value(CAR(args));
  Cell *def=lcar(lcdr(lcdr(args)));
  args=CAR(CDR(args));

  int options = ((LList *)args)->GetLength();
  int mh=(font->Size().y+1)*options+10,maxw=0;

  Cell *c=(Cell *)args;
  for (; !NILP(c); c=CDR(c))
  {
    if( strlen(men_str(CAR(c))) > (unsigned)maxw)
      maxw = strlen(men_str(CAR(c)));
  }

  int mw=(font->Size().x)*maxw+20;
  int mx=main_screen->Size().x/2-mw/2,
      my=main_screen->Size().y/2-mh/2;


  main_screen->AddDirty(ivec2(mx, my), ivec2(mx + mw, my + mh));

  if (title)
  {
    int tl=strlen(title)*font->Size().x;
    int tx=main_screen->Size().x/2-tl/2;
    DarkWidget(ivec2(tx - 2, my-font->Size().y - 4), ivec2(tx + tl + 3, my - 1), wm->medium_color(),wm->dark_color(),180);
    font->PutString(main_screen, ivec2(tx + 1, my-font->Size().y - 2), title, wm->bright_color());
  }

  DarkWidget(ivec2(mx, my), ivec2(mx + mw, my + mh),
             wm->medium_color(), wm->dark_color(), 200);


  int y=my+5;
  for (c=(Cell *)args; !NILP(c); c=CDR(c))
  {
    char *ms=men_str(CAR(c));
    font->PutString(main_screen, ivec2(mx + 10 + 1, y + 1), ms, wm->black());
    font->PutString(main_screen, ivec2(mx + 10, y), ms, wm->bright_color());
    y+=font->Size().y+1;
  }

  wm->flush_screen();
  Event ev;
  int choice=0,done=0;
  int bh=font->Size().y+3;
  image *save = new image(ivec2(mw - 2,bh));
  int color=128,cdir=50;

  time_marker *last_color_time=NULL;
  if (!NILP(def))
    choice=lnumber_value(def);
  do
  {
    wm->flush_screen();
    if (wm->IsPending())
    {
      wm->get_event(ev);
      if (ev.type==EV_KEY)
      {
    switch (ev.key)
    {
      case JK_ESC :
      { choice=-1; done=1; } break;
      case JK_ENTER :
      { done=1; } break;
      case JK_DOWN :
      { if (choice<options-1)
        choice++;
      else choice=0;
      } break;
      case JK_UP :
      {
        if (choice>0)
        choice--;
        else choice=options-1;
      } break;
    }
      } else if (ev.type==EV_MOUSE_BUTTON && ev.mouse_button)
      {
    if (ev.mouse_move.x>mx && ev.mouse_move.x<mx+mw && ev.mouse_move.y>my &&
        ev.mouse_move.y<my+mh)
    {
      int msel=(ev.mouse_move.y-my)/(font->Size().y+1);
      if (msel>=options) msel=options-1;
      if (msel==choice)                    // clicked on already selected item, return it
        done=1;
      else choice=msel;                    // selects an item
    }
      }
    }

    time_marker cur_time;
    if (!last_color_time || (int)(cur_time.diff_time(last_color_time)*1000)>120)
    {
      if (last_color_time)
        delete last_color_time;
      last_color_time=new time_marker;

      int by1=(font->Size().y+1)*choice+my+5-2;
      int by2=by1+bh-1;

      save->PutPart(main_screen, ivec2(0, 0), ivec2(mx + 1, by1), ivec2(mx + mw - 1, by2 + 1));
      TintArea(ivec2(mx + 1, by1), ivec2(mx + mw - 1, by2 + 1),
               63, 63, 63, color);

      char *cur=men_str(nth(choice,args));
      font->PutString(main_screen, ivec2(mx + 10 + 1, by1 + 3), cur, wm->black());
      font->PutString(main_screen, ivec2(mx + 10, by1 + 2), cur, wm->bright_color());
      main_screen->Rectangle(ivec2(mx + 1, by1), ivec2(mx + mw - 2, by2),
                             wm->bright_color());

      color+=cdir;

      if (color<12 || color>256)
      {
    cdir=-cdir;
    color+=cdir;
      }
      wm->flush_screen();
      main_screen->PutImage(save, ivec2(mx + 1, by1));
    } else { Timer tmp; tmp.WaitMs(10); }

  } while (!done);
  if (last_color_time)
    delete last_color_time;
  delete save;
  the_game->draw(the_game->state==SCENE_STATE);

  if (choice!=-1)
  {
    void *val=nth(choice,args);
    if (item_type(val)==L_CONS_CELL)   // is there another value that the user want us to return?
      return lnumber_value(lcdr(val));
  }
  return choice;
}

static void create_volume_window()
{
    volume_window = new VolumeWindow();
    volume_window->inm->allow_no_selections();
    volume_window->inm->clear_current();
    volume_window->show();

    wm->grab_focus(volume_window);
    wm->flush_screen();

    while(volume_window)
    {
        Event ev;

        do
        {
            wm->get_event(ev);
        }
        while(ev.type == EV_MOUSE_MOVE && wm->IsPending());

        wm->flush_screen();

        if(ev.type == EV_CLOSE_WINDOW
                 || (ev.type == EV_KEY && ev.key == JK_ESC))
        {
            wm->close_window(volume_window);
            volume_window = NULL;
        }

        if(!volume_window)
            break;

        if(ev.type == EV_MESSAGE)
        {
            char const *s;

            switch(ev.message.id)
            {
            case ID_SFX_UP:
                sfx_volume += 16;
                if(sfx_volume > 127)
                    sfx_volume = 127;
                volume_window->draw_sfx_vol();
                s = "sfx/ambtech1.wav";
                if(sound_avail & SFX_INITIALIZED)
                    cache.sfx(cache.reg(s, s, SPEC_EXTERN_SFX, 1))
                        ->play(sfx_volume);
                break;
            case ID_SFX_DOWN:
                sfx_volume -= 16;
                if(sfx_volume < 0)
                    sfx_volume = 0;
                volume_window->draw_sfx_vol();
                s = "sfx/ambtech1.wav";
                if(sound_avail & SFX_INITIALIZED)
                    cache.sfx(cache.reg(s, s, SPEC_EXTERN_SFX, 1))
                        ->play(sfx_volume);
                break;

            case ID_MUSIC_UP:
                music_volume += 16;
                if(music_volume > 127)
                    music_volume = 127;
                volume_window->draw_music_vol();
                if(current_song)
                    current_song->set_volume(music_volume);
                break;
            case ID_MUSIC_DOWN:
                music_volume -= 16;
                if(music_volume < 0)
                    music_volume = 0;
                volume_window->draw_music_vol();
                if(current_song)
                    current_song->set_volume(music_volume);
                break;
            }
        }
    }

    wm->close_window(volume_window);
}

void save_difficulty()
{
  FILE *fp = prefix_fopen("hardness.lsp", "wb");

  if(!fp) dprintf("Unable to write to file hardness.lsp\n");
	else
	{
		fprintf(fp,"(setf difficulty '");
		if(DEFINEDP(symbol_value(l_difficulty)))
		{
			if (symbol_value(l_difficulty)==l_extreme)		fprintf(fp,"extreme)\n");
			else if (symbol_value(l_difficulty)==l_hard)	fprintf(fp,"hard)\n");
			else if (symbol_value(l_difficulty)==l_easy)	fprintf(fp,"easy)\n");
			else fprintf(fp,"medium)\n");
		}
		else fprintf(fp,"medium)\n");

		fclose(fp);
	}
}

void fade_out(int steps);
void fade_in(image *im, int steps);

image *credits_hires = NULL;

void show_sell(int abortable)
{
	//AR credits screen, enabled hires image

	if(settings.hires && !credits_hires) credits_hires = cache.img(cache.reg("art/fore/endgame.spe","credit_hires",SPEC_IMAGE,1));

  LSymbol *ss = LSymbol::FindOrCreate("sell_screens");
  if (!DEFINEDP(ss->GetValue()))
  {
    LSpace *sp = LSpace::Current;
    LSpace::Current = &LSpace::Perm;
//    char *prog="((\"art/help.spe\" . \"sell2\")(\"art/help.spe\" . \"sell4\")(\"art/help.spe\" . \"sell3\")(\"art/fore/endgame.spe\" . \"credit\"))";
//    char *prog="((\"art/fore/endgame.spe\" . \"credit\") (\"art/help.spe\" . \"sell6\"))";
    char const *prog = "((\"art/fore/endgame.spe\" . \"credit\"))";
    ss->SetValue(LObject::Compile(prog));
    LSpace::Current = sp;
  }

  if (DEFINEDP(ss->GetValue()))
  {
    image blank(ivec2(2, 2)); blank.clear();
    wm->SetMouseShape(blank.copy(), ivec2(0, 0));      // don't show mouse

    LObject *tmp = (LObject *)ss->GetValue();
    int quit=0;
    while (tmp && !quit)
    {
		if(settings.hires && credits_hires) fade_in(credits_hires,16);
		else
		{
			int im = cache.reg_object("art/help.spe",CAR(tmp),SPEC_IMAGE,1);
			fade_in(cache.img(im),16);
		}

      Event ev;
      do
      { wm->flush_screen();
    wm->get_event(ev);
      } while (ev.type!=EV_KEY);
      if (ev.key==JK_ESC && abortable)
        quit=1;
      fade_out(16);
      tmp = (LObject *)CDR(tmp);
    }
    wm->SetMouseShape(cache.img(c_normal)->copy(), ivec2(1, 1));
  }
}


void menu_handler(Event &ev, InputManager *inm)
{
  switch (ev.type)
  {
    case EV_MESSAGE :
    {
      switch (ev.message.id)
      {
    case ID_LIGHT_OFF :
    if (!volume_window)
    {
      gamma_correct(pal,1);
    } break;
    case ID_RETURN :
    if (!volume_window)
    {
      the_game->set_state(RUN_STATE);
    } break;
    case ID_START_GAME :
    if (!volume_window)
    {
      the_game->load_level(level_file);
      the_game->set_state(RUN_STATE);
      view *v;
      for (v=player_list; v; v=v->next)
        if (v->m_focus)
          v->reset_player();

	  settings.quick_load = level_file;//AR
    } break;


        case ID_LOAD_PLAYER_GAME :
    if (!volume_window)
    {
      int got_level=load_game(0,symbol_str("LOAD"));
      the_game->reset_keymap();
      if (got_level)
      {
        char name[255];
        sprintf(name,"save%04d.spe", got_level);

        the_game->load_level(name);
        the_game->set_state(RUN_STATE);

		settings.quick_load = name;//AR
      }
    } break;


    case ID_VOLUME :
    if (!volume_window)
    { create_volume_window(); } break;

    case ID_MEDIUM :
    {
      l_difficulty->SetValue(l_medium);
      save_difficulty();
    } break;
    case ID_HARD :
    {
      l_difficulty->SetValue(l_hard);
      save_difficulty();
    } break;
    case ID_EXTREME :
    {
      l_difficulty->SetValue(l_extreme);
      save_difficulty();
    } break;
    case ID_EASY :
    {
      l_difficulty->SetValue(l_easy);
      save_difficulty();
    } break;

    case ID_NETWORKING :
    {
      if (!volume_window)
      {
        net_configuration *cfg=new net_configuration;
        if (cfg->input())
        {
          if (main_net_cfg) delete main_net_cfg;
          main_net_cfg=cfg;
        } else delete cfg;
        the_game->draw(0);
        inm->redraw();
      }
    } break;

    case ID_SHOW_SELL :
    if (!volume_window)
    {
      show_sell(1);
      main_screen->clear();
      if (title_screen>=0)
      {
        image *im = cache.img(title_screen);
        main_screen->PutImage(im, main_screen->Size() / 2 - im->Size() / 2);
      }
      inm->redraw();
      fade_in(NULL,8);
      wm->flush_screen();

    } break;
      } break;
    } break;
    case EV_CLOSE_WINDOW :
    {
      if (ev.window==volume_window)
      { wm->close_window(volume_window); volume_window=NULL; }
    } break;
  }
}

void *current_demo=NULL;

static ico_button *load_icon(int num, int id, int x, int y, int &h, ifield *next, char const *key)
{
	//AR enabled high resolution images

	char name[20];
	char const *base = "newi";
	int a, b, c;

	std::string img_name = "%s%04d.pcx";
	if(settings.hires) img_name += "_hires";

	sprintf(name,img_name.c_str(),base,num*3+1);
	a = cache.reg("art/icons.spe",name,SPEC_IMAGE,1);

	sprintf(name,img_name.c_str(),base,num*3+2);
	b = cache.reg("art/icons.spe",name,SPEC_IMAGE,1);

	sprintf(name,img_name.c_str(),base,num*3+3);
	c = cache.reg("art/icons.spe",name,SPEC_IMAGE,1);
	
	h = cache.img(a)->Size().y;

	return new ico_button(x,y,id,b,b,a,c,next,-1,key);
}

ico_button *make_default_buttons(int x, int &y, ico_button *append_list)
{
  int h;
  int diff_on;

  if (DEFINEDP(symbol_value(l_difficulty)))
  {
    if (symbol_value(l_difficulty) == l_extreme)
      diff_on = 3;
    else if (symbol_value(l_difficulty) == l_hard)
      diff_on = 2;
    else if (symbol_value(l_difficulty) == l_easy)
      diff_on = 0;
    else
      diff_on = 1;
  }
  else
    diff_on = 3;

  ico_button *start = load_icon(0, ID_START_GAME, x, y, h, NULL, "ic_start");
  y += h;

  // difficulty/hardness icon
  ico_switch_button *set = NULL;
  if (!main_net_cfg || (main_net_cfg->state != net_configuration::SERVER && main_net_cfg->state != net_configuration::CLIENT))
  {
    set = new ico_switch_button(
        x, y, ID_NULL, diff_on,
        load_icon(3, ID_EASY, x, y, h,
                  load_icon(8, ID_MEDIUM, x, y, h,
                            load_icon(9, ID_HARD, x, y, h,
                                      load_icon(10, ID_EXTREME, x, y, h,
                                                NULL, "ic_extreme"),
                                      "ic_hard"),
                            "ic_medium"),
                  "ic_easy"),
        NULL);
    y += h;
  }

  ico_button *color = load_icon(4, ID_LIGHT_OFF, x, y, h, NULL, "ic_gamma");
  y += h;

  ico_button *volume = load_icon(5, ID_VOLUME, x, y, h, NULL, "ic_volume");
  y += h;

  // Multiplayer button
  ico_button *multiplayer = NULL;
  if (prot)
  {
    multiplayer = load_icon(11, ID_NETWORKING, x, y, h, NULL, "ic_networking");
    y += h;
  }

  // credits in full version
  // ico_button *sell = load_icon(2, ID_SHOW_SELL, x, y, h, NULL, "ic_sell");
  // y += h;

  ico_button *quit = load_icon(6, ID_QUIT, x, y, h, NULL, "ic_quit");
  y += h;

  // connect buttons/make list
  if (set)
  {
    start->next = set;
    set->next = color;
  }
  else
    start->next = color;

  color->next = volume;

  if (prot)
  {
    volume->next = multiplayer;
    multiplayer->next = quit;
  }
  else
    volume->next = quit;  

  ico_button *list = append_list;

  if (append_list)
  {
    while (append_list->next)
      append_list = (ico_button *)append_list->next;

    append_list->next = start;
  }
  else
    list = start;

  return list;
}

ico_button *make_conditional_buttons(int x, int &y)
{
	//AR "return to game" and "load game" buttons

	ico_button *start_list = NULL;

	int h;

	//should we include a return icon ?
	if(current_level)
	{
		start_list = load_icon(7,ID_RETURN,x,y,h,NULL,"ic_return");
		y += h;
	}

	ico_button *load = NULL;
	if(show_load_icon())
	{
		load = load_icon(1,ID_LOAD_PLAYER_GAME,x,y,h,NULL,"ic_load");
		y += h;
	}	

	if(start_list) start_list->next = load;
	else start_list = load;

	return start_list;
}

void main_menu()
{
  // AR enabled button selection with a controller, enabled highres button images
  // AR let me know we are stuck here
  the_game->ar_stateold = the_game->ar_state;
  the_game->ar_state = AR_MAINMENU;

  // default button size 32x25, hires size 50x39
  int button_w = 32;
  int button_h = 25;
  int padding_x = 1;

  // Calculate total number of buttons
  int total_buttons = 5; // Start, Difficulty, Gamma, Volume, Quit

  // Remove difficulty button if we're in multiplayer server/client mode
  if (main_net_cfg && (main_net_cfg->state == net_configuration::SERVER ||
                       main_net_cfg->state == net_configuration::CLIENT))
  {
    total_buttons--;
  }

  // Add conditional buttons
  if (current_level) total_buttons++; // Return button
  if (show_load_icon()) total_buttons++; // Load button
  if (prot) total_buttons++; // Multiplayer button

  if (settings.hires)
  {
    button_w = 50;
    button_h = 39;
    padding_x = 2;
  }

  // Calculate total menu height and center position
  int total_height = total_buttons * button_h;
  int y = (yres - total_height) / 2;
  int x = xres - button_w - padding_x;

  // Store original y for border calculations
  int original_y = y;

  ico_button *list = make_conditional_buttons(x, y);
  list = make_default_buttons(x, y, list);

  // AR controller ui movement
  int mx, my;                 // mouse position
  int border_up = original_y; // Changed to use the initial y position
  int border_down = y;

  int old_mx = wm->GetMousePos().x;
  int old_my = wm->GetMousePos().y;

  // AR initial position of the mouse in the menu for controller use
  if (settings.ctr_aim)
  {
    mx = x + button_w / 2;
    my = border_up + button_h / 2;
    wm->SetMousePos(ivec2(mx, my));
  }

  InputManager *inm = new InputManager(main_screen, list);
  inm->allow_no_selections();
  inm->clear_current();

  main_screen->AddDirty(ivec2(0), ivec2(320, 200));

  Event ev;
  int stop_menu = 0;
  time_marker start;
  wm->flush_screen();
  
  
  do
	{
        time_marker new_time;

        if (wm->IsPending())
        {
            do
            {
                wm->get_event(ev);
            } while (ev.type==EV_MOUSE_MOVE && wm->IsPending());
            inm->handle_event(ev,NULL);
            if (ev.type==EV_KEY && ev.key==JK_ESC)
              if (current_level)
              {
                the_game->set_state(RUN_STATE);
              }
              else
                wm->Push(new Event(ID_QUIT,NULL));

            menu_handler(ev,inm);
            start.get_time();

            wm->flush_screen();
        }
        else
        {
            // ECS - Added so that main menu doesn't grab 100% of CPU
            Timer tmp; tmp.WaitMs(30);
        }

        if (new_time.diff_time(&start)>10)
        {
            if (volume_window)
                start.get_time();
            else
            {
                if (!current_demo)
                {
                    LSymbol *d = LSymbol::FindOrCreate("demos");
                    if (DEFINEDP(d->GetValue()))
                        current_demo = d->GetValue();
                }
                if (current_demo)
                {
                    demo_man.set_state(demo_manager::PLAYING,lstring_value(CAR(current_demo)));
                    stop_menu=1;
                    current_demo=CDR(current_demo);
                }
            }
        }

        if (volume_window) stop_menu=0;  // can't exit with volume window open
        else if (main_net_cfg && main_net_cfg->restart_state()) stop_menu=1;
        else if (the_game->state==RUN_STATE) stop_menu=1;
        else if (ev.type==EV_MESSAGE)
        {
            if (ev.message.id==ID_START_GAME || ev.message.id==ID_RETURN)
                stop_menu=1;
            else if (ev.message.id==ID_QUIT)
            {
				//AR what is that screen doing in dev.cpp ?
                if (confirm_quit())
                    stop_menu=1;
                else
                {
                    ev.type=EV_SPURIOUS;
                    start.get_time();
                }
            }
		}
		
		//AR move cursor over icons
		if(settings.ctr_aim && ev.type==EV_KEY)
		{
			if((ev.key==get_key_binding("up",0) || ev.key==get_key_binding("up2",0)))
			{
				if(my-button_h>border_up) my -= button_h;
				wm->SetMousePos(ivec2(mx,my));
			}
			if((ev.key==get_key_binding("down",0) || ev.key==get_key_binding("down2",0)))
			{
				if(my+button_h<border_down) my += button_h;
				wm->SetMousePos(ivec2(mx,my));
			}
		}
		//

    } while (!stop_menu);

    delete inm;

    if (ev.type==EV_MESSAGE && ev.message.id==ID_QUIT)   // propogate the quit message
        the_game->end_session();

	//AR let me know we leaving
	the_game->ar_state = the_game->ar_stateold;
	if(settings.ctr_aim) wm->SetMousePos(ivec2(old_mx,old_my));//put mouse where it was on entering
	//
}


