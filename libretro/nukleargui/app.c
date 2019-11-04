/* nuklear - v1.00 - public domain */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <time.h>

#include <libretro.h>
#include <libretro-core.h>

extern void filebrowser_init();
extern void filebrowser_free();

extern int snapshot_save (char *pchFileName);
extern void play_tape();
extern void Screen_SetFullUpdate(int scr);
extern void vkbd_key(int key,int pressed);

extern long GetTicks(void);

extern retro_input_poll_t input_poll_cb;
extern retro_input_state_t input_state_cb;

extern char RPATH[512];

//EMU FLAGS
int showkeyb=-1;
int SHIFTON=-1;
int KBMOD=-1;
int RSTOPON=-1;
int CTRLON=-1;
int NUMDRV=1;
int NPAGE=-1;
int KCOL=1;
int LOADCONTENT=-1;
int STATUTON=-1;
int LDRIVE=8;
int SND=1;
int vkey_pressed;
unsigned char MXjoy[2]; // joy
char LCONTENT[512];
char Core_Key_Sate[512];
char Core_old_Key_Sate[512];

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION

#define NK_RETRO_SOFT_IMPLEMENTATION

#include "nuklear.h"
#include "nuklear_retro_soft.h"
#include "retro_events.h"

static RSDL_Surface *screen_surface;
extern void restore_bgk();
extern void save_bkg();

/* macros */

#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])

/* Platform */

float bg[4];
struct nk_color background;
/* GUI */
struct nk_context *ctx;

static nk_retro_Font *RSDL_font;

#include "style.c"
#include "filebrowser.c"
#include "gui.i"

int app_init(int width, int height)
{
    #ifdef M16B
    screen_surface=Retro_CreateRGBSurface16(width,height,16,0,0,0,0);
    #else
    screen_surface=Retro_CreateRGBSurface32(width,height,32,0,0,0,0);
    #endif

    Retro_Screen=(PIXEL_TYPE *)screen_surface->pixels;

    RSDL_font = (nk_retro_Font*)calloc(1, sizeof(nk_retro_Font));
    RSDL_font->width = 4;
    RSDL_font->height = 7;
    if (!RSDL_font)
        return -1;

    /* GUI */
    ctx = nk_retro_init(RSDL_font,screen_surface,width,height);

    /* style.c */
    /* THEME_BLACK, THEME_WHITE, THEME_RED, THEME_BLUE, THEME_DARK */
    set_style(ctx, THEME_DARK);

    /* icons */

    filebrowser_init();
    sprintf(LCONTENT,"%s\0",RPATH);

	memset(Core_Key_Sate,0,512);
	memset(Core_old_Key_Sate ,0, sizeof(Core_old_Key_Sate));

    printf("Init nuklear %ux%u\n", width, height);

 return 0;
}

int app_free()
{
   //FIXME: memory leak here
   if (RSDL_font)
      free(RSDL_font);
   RSDL_font = NULL;
   filebrowser_free();
   nk_retro_shutdown();

   Retro_FreeSurface(screen_surface);
   if (screen_surface)
      free(screen_surface);
   screen_surface = NULL;

   return 0;
}
