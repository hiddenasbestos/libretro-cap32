#include "libretro.h"
#include "libretro-core.h"
#include "retro_events.h"
#include "retro_snd.h"
#include "tape.h"

//CORE VAR
#ifdef _WIN32
char slash = '\\';
#else
char slash = '/';
#endif

char RETRO_DIR[512];

char DISKA_NAME[512]="\0";
char DISKB_NAME[512]="\0";
char cart_name[512]="\0";

//TIME
#ifdef __CELLOS_LV2__
#include "sys/sys_time.h"
#include "sys/timer.h"
#define usleep  sys_timer_usleep
#else
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#endif

// DISK CONTROL
#include "retro_disk_control.h"
static dc_storage* dc;

// TAPE CONTROL
extern int iTapeBlockCount;

// LOG
retro_log_printf_t log_cb;
#define LOGI(...) log_cb( RETRO_LOG_INFO, __VA_ARGS__ )

computer_cfg_t retro_computer_cfg;

extern int retro_disk_auto();
extern void change_model(int val);
extern int snapshot_load (char *pchFileName);
extern int attach_disk(char *arv, int drive);
extern int detach_disk(int drive);
extern int tape_insert (char *pchFileName);
extern int cart_insert (char *pchFileName);
extern void kbd_buf_feed(char *s);
extern void kbd_buf_update();
extern void retro_loop(void);
extern int video_set_palette (void);
extern int InitOSGLU(void);
extern int  UnInitOSGLU(void);
extern void emu_reset(void);
extern void emu_restart(void);
extern void change_ram(int val);
extern uint8_t* get_ram_ptr();
extern size_t get_ram_size();
extern void change_lang(int val);
extern int snapshot_save (char *pchFileName);
extern void play_tape();
extern void retro_key_down(int key);
extern void retro_key_up(int key);

//VIDEO
uint32_t bmp[WINDOW_MAX_SIZE];

//SOUND
short signed int SNDBUF[1024*2];
int snd_sampler = 44100 / 50;

//PATH
char RPATH[512];

extern int app_init(int width, int height);
extern int app_free(void);

static const int retro_scr_w = 768;
static const int retro_scr_h = 544;

unsigned amstrad_devices[ 2 ];

int DRIVE_SOUNDS=0;
int SND=1;
int SHIFTON=-1;
int autorun=0;

int emu_status = COMPUTER_OFF;

//CAP32 DEF BEGIN
#include "cap32.h"
#include "slots.h"
//#include "z80.h"
extern t_CPC CPC;
extern uint8_t *pbSndBuffer;
//CAP32 DEF END

extern void update_input(void);
extern void input_gui(void);

const char *retro_save_directory;
const char *retro_system_directory;
const char *retro_content_directory;
char retro_system_data_directory[512];

/*static*/ retro_input_state_t input_state_cb;
/*static*/ retro_input_poll_t input_poll_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
/*static*/ retro_audio_sample_batch_t audio_batch_cb;
/*static*/ retro_environment_t environ_cb;

// allowed file types
#define CDT_FILE_EXT "cdt"
#define DSK_FILE_EXT "dsk"
#define M3U_FILE_EXT "m3u"
#define SNA_FILE_EXT "sna"
#define CPR_FILE_EXT "cpr"

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

int retro_getStyle(){
    return 4;
}

int retro_getGfxBpp(){
    return 16 * PIXEL_BYTES;
}

int retro_getGfxBps(){
    return retro_scr_w;
}

uint32_t * retro_getScreenPtr(){
    return (uint32_t *)&bmp[0];
}

#include <ctype.h>

//Args for experimental_cmdline
static char ARGUV[64][1024];
static unsigned char ARGUC=0;

// Args for Core
static char XARGV[64][1024];
static const char* xargv_cmd[64];
int PARAMCOUNT=0;

extern int  skel_main(int argc, char *argv[]);
void parse_cmdline( const char *argv );

void Add_Option(const char* option)
{
   static int first=0;

   if(first==0)
   {
      PARAMCOUNT=0;
      first++;
   }

   sprintf(XARGV[PARAMCOUNT++],"%s",option);
}

int pre_main(const char *argv)
{
   int i;
   bool Only1Arg;

   parse_cmdline(argv);

   Only1Arg = (strcmp(ARGUV[0],"x64") == 0) ? 0 : 1;

   for (i = 0; i<64; i++)
      xargv_cmd[i] = NULL;


   if(Only1Arg)
   {  Add_Option("x64");

      if (strlen(RPATH) >= strlen("crt"))
         if(!strcasecmp(&RPATH[strlen(RPATH)-strlen("crt")], "crt"))
            Add_Option("-cartcrt");

      Add_Option(RPATH/*ARGUV[0]*/);
   }
   else
   { // Pass all cmdline args
      for(i = 0; i < ARGUC; i++)
         Add_Option(ARGUV[i]);
   }

   for (i = 0; i < PARAMCOUNT; i++)
   {
      xargv_cmd[i] = (char*)(XARGV[i]);
//      LOGI("%2d  %s\n",i,XARGV[i]);
   }

   skel_main(PARAMCOUNT,( char **)xargv_cmd);

   xargv_cmd[PARAMCOUNT - 2] = NULL;

   return 0;
}

void parse_cmdline(const char *argv)
{
	char *p,*p2,*start_of_word;
	int c,c2;
	static char buffer[512*4];
	enum states { DULL, IN_WORD, IN_STRING } state = DULL;

	strcpy(buffer,argv);
	strcat(buffer," \0");

	for (p = buffer; *p != '\0'; p++)
   {
      c = (unsigned char) *p; /* convert to unsigned char for is* functions */
      switch (state)
      {
         case DULL: /* not in a word, not in a double quoted string */
            if (isspace(c)) /* still not in a word, so ignore this char */
               continue;
            /* not a space -- if it's a double quote we go to IN_STRING, else to IN_WORD */
            if (c == '"')
            {
               state = IN_STRING;
               start_of_word = p + 1; /* word starts at *next* char, not this one */
               continue;
            }
            state = IN_WORD;
            start_of_word = p; /* word starts here */
            continue;
         case IN_STRING:
            /* we're in a double quoted string, so keep going until we hit a close " */
            if (c == '"')
            {
               /* word goes from start_of_word to p-1 */
               //... do something with the word ...
               for (c2 = 0,p2 = start_of_word; p2 < p; p2++, c2++)
                  ARGUV[ARGUC][c2] = (unsigned char) *p2;
               ARGUC++;

               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_STRING or we handled the end above */
         case IN_WORD:
            /* we're in a word, so keep going until we get to a space */
            if (isspace(c))
            {
               /* word goes from start_of_word to p-1 */
               //... do something with the word ...
               for (c2 = 0,p2 = start_of_word; p2 <p; p2++,c2++)
                  ARGUV[ARGUC][c2] = (unsigned char) *p2;
               ARGUC++;

               state = DULL; /* back to "not in word, not in string" state */
            }
            continue; /* either still IN_WORD or we handled the end above */
      }
   }
}

long GetTicks(void)
{ // in MSec
#ifndef _ANDROID_

#ifdef __CELLOS_LV2__

   //#warning "GetTick PS3\n"

   unsigned long	ticks_micro;
   uint64_t secs;
   uint64_t nsecs;

   sys_time_get_current_time(&secs, &nsecs);
   ticks_micro =  secs * 1000000UL + (nsecs / 1000);

   return ticks_micro;///1000;
#else
   struct timeval tv;
   gettimeofday (&tv, NULL);
   return (tv.tv_sec*1000000 + tv.tv_usec);///1000;

#endif

#else

   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   return (now.tv_sec*1000000 + now.tv_nsec/1000);///1000;
#endif

}

int HandleExtension(char *path,char *ext)
{
   int len = strlen(path);

   if (len >= 4 &&
         path[len-4] == '.' &&
         path[len-3] == ext[0] &&
         path[len-2] == ext[1] &&
         path[len-1] == ext[2])
   {
      return 1;
   }

   return 0;
}

void retro_mouse(int a,int b) {}
void retro_mouse_but0(int a) {}
void retro_mouse_but1(int a) {}
void enter_options(void) {}

#ifdef AND
#define DEFAULT_PATH "/mnt/sdcard/amstrad/"
#else

#ifdef PS3PORT
#define DEFAULT_PATH "/dev_hdd0/HOMEBREW/amstrad/"
#else
#define DEFAULT_PATH "/"
#endif

#endif

void retro_message(const char *text) {
   struct retro_message msg;
   char msg_local[256];

   snprintf(msg_local, sizeof(msg_local), "CPC: %s", text);
   msg.msg = msg_local;
   msg.frames = 100;

   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, (void*)&msg);

}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   bool allow_no_game = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allow_no_game);

	static const struct retro_controller_description p1_controllers[] = {
		{ "Amstrad Joystick", RETRO_DEVICE_AMSTRAD_JOYSTICK },
	};

	/*static const struct retro_controller_description p2_controllers[] = {
		{ "Amstrad Joystick", RETRO_DEVICE_AMSTRAD_JOYSTICK },
	};*/

	static const struct retro_controller_info ports[] =
	{
		{ p1_controllers, 1  }, // port 1
//		{ p2_controllers, 1  }, // port 2
		{ NULL, 0 }
	};

   environ_cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports );

   struct retro_variable variables[] =
   {
		{
			"cap32_autorun",
			"Auto Start; enabled|disabled",
		},
		{
			"cap32_scr_tube",
			"Monitor Type; Color|Green",
		},
#if FORCE_MACHINE == 6128
		{
			"cap32_drive_snd",
			"Drive Sounds; disabled|enabled",
		},
#endif // FORCE_MACHINE
		/*{
			"cap32_lang_layout",
			"System Language; English|French|Spanish",
		},*/

      { NULL, NULL },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}
/*
static void update_geometry_ex()
{
	struct retro_game_geometry_ex av_info;

	av_info.geometry.base_width   = retro_scr_w;
	av_info.geometry.base_height  = retro_scr_h;
	av_info.geometry.max_width    = retro_scr_w;
	av_info.geometry.max_height   = retro_scr_h;
	av_info.geometry.aspect_ratio = 4.0 / 3.0;

	av_info.visible_x = 64;
	av_info.visible_width = 640;
	av_info.visible_y = 82;
	av_info.visible_height = 400;

	// This never changes from initial AV info,
	// so only need to call Grid Cartographer special function.
	environ_cb( RETRO_ENVIRONMENT_SET_GEOMETRY_EX, &av_info );
}
*/
static void update_variables(void)
{
	// Machine specification
	if ( emu_status == COMPUTER_OFF )
	{
#if FORCE_MACHINE == 464
		LOGI( "Machine: CPC 464\n" );
		retro_computer_cfg.model = 0;
		retro_computer_cfg.ram = 64;//KB
#elif FORCE_MACHINE == 6128
		LOGI( "Machine: CPC 6128\n" );
		retro_computer_cfg.model = 2;
		retro_computer_cfg.ram = 128;//KB
#endif // FORCE_MACHINE

		// English
		retro_computer_cfg.lang = 0;
	}

   struct retro_variable var;

   var.key = "cap32_autorun";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         autorun = 1;
   }

#if FORCE_MACHINE == 6128
	var.key = "cap32_drive_snd";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		if (strcmp(var.value, "enabled") == 0)
			DRIVE_SOUNDS = 1;
		else
			DRIVE_SOUNDS = 0;
	}
#endif // FORCE_MACHINE

   var.key = "cap32_scr_tube";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if(emu_status & COMPUTER_READY) {
         if (strcmp(var.value, "Color") == 0){
            CPC.scr_tube = CPC_MONITOR_COLOR;
            video_set_palette();
         }
   		else if (strcmp(var.value, "Green") == 0){
            CPC.scr_tube = CPC_MONITOR_GREEN;
            video_set_palette();
         }
		}
   }

   /*var.key = "cap32_lang_layout";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int val = 0; // DEFAULT ENGLISH
      if (strcmp(var.value, "French") == 0) val=1;
      else if (strcmp(var.value, "Spanish") == 0) val=2;

      if (retro_computer_cfg.lang != val) {
         retro_computer_cfg.lang = val;
         if(emu_status & COMPUTER_READY) {
            change_lang(val);
            LOGI("REBOOT - CPC LANG: %u (emu_status = %x)\n", val, emu_status);
         }
      }
   }*/

   // check if emulation need a restart (model/lang/... is changed)
   if(retro_computer_cfg.is_dirty)
      emu_restart();
}

void retro_shutdown_core(void)
{
	LOGI("SHUTDOWN\n");

	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

void retro_reset(void)
{
	emu_reset();
}

//*****************************************************************************
//*****************************************************************************
// Disk control (6128)

#if FORCE_MACHINE == 6128

static bool disk_set_eject_state(bool ejected)
{
	if (dc)
	{
		dc->eject_state = ejected;

		if(dc->eject_state)
		{
			detach_disk(0);
		}
		else
		{
			attach_disk((char *)dc->files[dc->index],0);
		}
	}

	return true;
}

static bool disk_get_eject_state(void)
{
	if (dc)
		return dc->eject_state;

	return true;
}

static unsigned disk_get_image_index(void)
{
	if (dc)
		return dc->index;

	return 0;
}

static bool disk_set_image_index(unsigned index)
{
	// Insert disk
	if (dc)
	{
		// Same disk...
		// This can mess things in the emu
//		if(index == dc->index) // <-- GC fix
//			return true;

		if ((index < dc->count) && (dc->files[index]))
		{
			dc->index = index;

#if FORCE_MACHINE == 464
			log_cb(RETRO_LOG_INFO, "Tape(%d) inserted: %s\n", dc->index+1, dc->files[dc->index]);
			tape_insert( dc->files[dc->index] );
			play_tape();
#elif FORCE_MACHINE == 6128
			log_cb(RETRO_LOG_INFO, "Disk(%d) inserted: %s\n", dc->index+1, dc->files[dc->index]);
			attach_disk((char *)dc->files[dc->index],0);
#endif // FORCE_MACHINE

			return true;
		}
	}

	return false;
}

static unsigned disk_get_num_images(void)
{
	if (dc)
		return dc->count;

	return 0;
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
	if (dc)
	{
		if (index >= dc->count)
			return false;

		if(dc->files[index])
		{
			free(dc->files[index]);
			dc->files[index] = NULL;
		}

		// TODO : Handling removing of a disk image when info = NULL

		if(info != NULL)
			dc->files[index] = strdup(info->path);
	}

	return false;
}

static bool disk_add_image_index(void)
{
	if (dc)
	{
		if(dc->count <= DC_MAX_SIZE)
		{
			dc->files[dc->count] = NULL;
			dc->count++;
			return true;
		}
	}

	return false;
}

static struct retro_disk_control_callback disk_interface =
{
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

#endif // CPC 6128


//*****************************************************************************
//*****************************************************************************
// Init
static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
}

// load bios content
void computer_load_bios() {
   // TODO add load customs bios

   // cart is like a system bios
   if (strlen(RPATH) >= strlen(CPR_FILE_EXT))
      if(!strcasecmp(&RPATH[strlen(RPATH)-strlen(CPR_FILE_EXT)], CPR_FILE_EXT))
      {
         int result = cart_insert(RPATH);
         if(result != 0) {
            retro_message("Error Loading Cart...");
         } else {
            sprintf(RPATH,"%s",RPATH);
         }
      }
}

// load content
void computer_load_file()
{
   // If it's a m3u file
   if (strlen(RPATH) >= strlen(M3U_FILE_EXT))
      if(!strcasecmp(&RPATH[strlen(RPATH)-strlen(M3U_FILE_EXT)], M3U_FILE_EXT))
      {
		int i;

        // Parse the m3u file
         dc_parse_m3u(dc, RPATH);

         // Some debugging
         log_cb(RETRO_LOG_INFO, "m3u file parsed, %d file(s) found\n", dc->count);
         for(i = 0; i < dc->count; i++)
         {
            log_cb(RETRO_LOG_INFO, "file %d: %s\n", i+1, dc->files[i]);
         }

         // Init first disk/tape
         dc->index = 0;
         dc->eject_state = false;

#if FORCE_MACHINE == 464

		log_cb(RETRO_LOG_INFO, "Tape(%d) inserted: %s\n", dc->index+1, dc->files[dc->index]);
		tape_insert( dc->files[dc->index] );
		play_tape();

#elif FORCE_MACHINE == 6128

		log_cb(RETRO_LOG_INFO, "Disk(%d) inserted: %s\n", dc->index+1, dc->files[dc->index]);
		attach_disk((char *)dc->files[dc->index],0);

#endif // FORCE_MACHINE

         // If command was specified
         if(dc->command)
         {
            // Execute the command
            log_cb(RETRO_LOG_INFO, "Executing the specified command: %s\n", dc->command);
            char* command = calloc(strlen(dc->command) + 1, sizeof(char));
            sprintf(command, "%s\n", dc->command);
            kbd_buf_feed(command);
            free(command);
         }
         else
         {
			// Autoplay
#if FORCE_MACHINE == 464
			if ( autorun )
            	kbd_buf_feed("run\"\n^");
#elif FORCE_MACHINE == 6128
            retro_disk_auto();
#endif // FORCE_MACHINE
         }

         // Prepare SNA
         sprintf(RPATH,"%s%d.SNA",RPATH,0);

         return;
      }

#if FORCE_MACHINE == 6128

   // If it's a disk
   if (strlen(RPATH) >= strlen(DSK_FILE_EXT))
      if(!strcasecmp(&RPATH[strlen(RPATH)-strlen(DSK_FILE_EXT)], DSK_FILE_EXT))
      {
         // Add the file to disk control context
         // Maybe, in a later version of retroarch, we could add disk on the fly (didn't find how to do this)
         dc_add_file(dc, RPATH);

         // Init first disk
         dc->index = 0;
         dc->eject_state = false;
         LOGI("Disk (%d) inserted into drive A: %s\n", dc->index+1, dc->files[dc->index]);
         attach_disk((char *)dc->files[dc->index],0);

		// Autoplay
		retro_disk_auto();

         // Prepare SNA
         sprintf(RPATH,"%s%d.SNA",RPATH,0);

         return;
      }
#endif // 6128

#if FORCE_MACHINE == 464

   // If it's a tape
   if (strlen(RPATH) >= strlen(CDT_FILE_EXT))
      if(!strcasecmp(&RPATH[strlen(RPATH)-strlen(CDT_FILE_EXT)], CDT_FILE_EXT))
      {
         dc_add_file(dc, RPATH);

         int error = tape_insert ((char *)RPATH);
         if (!error)
         {
			dc->index = 0;
			dc->eject_state = false;

			if ( autorun )
            	kbd_buf_feed("run\"\n^");

            LOGI("Tape inserted: %s\n", (char *)RPATH);
       }
         else
         {
            LOGI("Tape Error (%d): %s\n", error, (char *)RPATH);
         }

         return;

      }

#endif // 464

   // If it's a snapshot
   /*if (strlen(RPATH) >= strlen(SNA_FILE_EXT))
      if(!strcasecmp(&RPATH[strlen(RPATH)-strlen(SNA_FILE_EXT)], SNA_FILE_EXT))
      {
         int error = snapshot_load (RPATH);
         if (!error) {
            LOGI("SNA loaded: %s\n", (char *)RPATH);
         } else {
            LOGI("SNA Error (%d): %s", error, (char *)RPATH);
         }

         return;
      }*/

}

void retro_init(void)
{
   struct retro_log_callback log;
   const char *system_dir = NULL;
   dc = dc_create();

	// Init log
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
		log_cb = log.log;
	else
		log_cb = fallback_log;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      // if defined, use the system directory
      retro_system_directory=system_dir;
   }

   const char *content_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      // if defined, use the system directory
      retro_content_directory=content_dir;
   }

   const char *save_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *save_dir ? save_dir : retro_system_directory;
   }
   else
   {
      // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
      retro_save_directory=retro_system_directory;
   }

   if(retro_system_directory==NULL)sprintf(RETRO_DIR, "%c",'.');
   else sprintf(RETRO_DIR, "%s", retro_system_directory);

   sprintf(retro_system_data_directory, "%s%cdata",RETRO_DIR, slash); // TODO: unused ?

//   LOGI("Retro SYSTEM_DIRECTORY %s\n",retro_system_directory);
//   LOGI("Retro SAVE_DIRECTORY %s\n",retro_save_directory);
//   LOGI("Retro CONTENT_DIRECTORY %s\n",retro_content_directory);

	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
	{
		LOGI("PIXEL FORMAT is not supported.\n");
		exit(0);
	}

   // events initialize - joy and keyboard
   ev_init();

#if FORCE_MACHINE == 464

	struct retro_content_fileexts tape_info;
	tape_info.TapeInsert = "cdt";
	tape_info.TapeCreate = NULL;
	tape_info.DiskControl = NULL;
	environ_cb(RETRO_ENVIRONMENT_SET_CONTENT_FILEEXTS, &tape_info);

#elif FORCE_MACHINE == 6128

	// Floppy disk control interface
	environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

#endif

   // prepare shared variables
   retro_computer_cfg.model = -1;
   retro_computer_cfg.ram = -1;
   retro_computer_cfg.lang = -1;

   update_variables();

   emu_status = COMPUTER_BOOTING;
   pre_main(RPATH);

   if(!init_retro_snd((int16_t*) pbSndBuffer))
      LOGI("AUDIO FORMAT is not supported.\n");

}

extern void main_exit();
void retro_deinit(void)
{
   UnInitOSGLU();

   // Clean the m3u storage
   if(dc)
      dc_free(dc);

   free_retro_snd();
   LOGI("Retro DeInit\n");
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}


void retro_set_controller_port_device( unsigned port, unsigned device )
{
	if ( port < 2 )
	{
		amstrad_devices[ port ] = device;

		// LOGI("retro_set_controller_port_device(%d) = %d\n",port,device);
	}
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
#if FORCE_MACHINE == 464
   info->library_name = "CaPriCe 464";
   info->valid_extensions = "cdt";
#elif FORCE_MACHINE == 6128
   info->library_name = "CaPriCe 6128";
   info->valid_extensions = "dsk|m3u";
#endif // FORCE_MACHINE
   #ifndef GIT_VERSION
   #define GIT_VERSION "4.2"
   #endif
   info->library_version = GIT_VERSION " (" __DATE__ ")";
   info->need_fullpath    = true;
   info->block_extract = false;

}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	/* FIXME handle NTSC */
	struct retro_game_geometry geom =
	{
		retro_scr_w, retro_scr_h,
		retro_scr_w, retro_scr_h,

		4.0 / 3.0
	};

	struct retro_system_timing timing = { 50.0, 44100.0 };

	info->geometry = geom;
	info->timing   = timing;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_audio_cb( short l, short r)
{
	audio_cb(l,r);
}

void retro_audiocb(signed short int *sound_buffer,int sndbufsize)
{
	int x;
	for(x=0;x<sndbufsize;x++)
	{
		audio_cb(sound_buffer[x],sound_buffer[x]);
	}
}

void retro_run(void)
{
	bool updated = false;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		update_variables();

	retro_loop();

	// blit
	video_cb( bmp, retro_scr_w, retro_scr_h, retro_scr_w << PIXEL_BYTES );

	input_poll_cb(); // retroarch get keys

#if FORCE_MACHINE == 464
	// NOTE: This is not accurate to the real hardware,
	// but it does give a rough indication of progress.
	environ_cb( RETRO_ENVIRONMENT_SET_TAPE_COUNTER, &iTapeBlockCount );

	// Tape state.
	int tape_state;
	if ( CPC.tape_play_button == 0x10 )
	{
		tape_state = ( CPC.tape_motor ) ? RETROTAPE_STATE_PLAYING : RETROTAPE_STATE_HOLD;
		environ_cb( RETRO_ENVIRONMENT_SET_TAPE_STATE, &tape_state );
	}
#endif

	ev_joysticks();
}

bool retro_load_game(const struct retro_game_info *game)
{
   if (game){
      strcpy(RPATH, (const char *) game->path);
   } else {
      RPATH[0]='\0';
   }

   update_variables();
   memset(SNDBUF,0,1024*2*2);

   computer_load_bios();
   computer_load_file();

   return true;

}

void retro_unload_game(void)
{
	//
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_PAL;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   return false;
}

size_t retro_serialize_size(void)
{
   int dwSnapSize = sizeof(t_SNA_header);
   dwSnapSize += get_ram_size();
   return dwSnapSize;
}

bool retro_serialize(void *data, size_t size)
{
   int error;
   error = snapshot_save_mem((uint8_t *) data, size);
   if(!error)
      return true;

   LOGI("SNA-serialized: error %d\n", error);
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   return !snapshot_load_mem((uint8_t *) data, size);
}

void *retro_get_memory_data(unsigned id)
{
   switch ( id & RETRO_MEMORY_MASK )
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return get_ram_ptr();
   }

   /* not supported */
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   switch ( id & RETRO_MEMORY_MASK )
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return get_ram_size();
   }

   /* not supported */
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}


bool retro_tape_command( unsigned command, void* param )
{
	/*log_cb(RETRO_LOG_INFO, "retro_tape_command. Got command %d.\n", command );*/

	bool result;
	result = false;

	switch ( command )
	{

	case RETROTAPE_COMMAND_PLAY:
		play_tape();
		result = true;
		break;

	case RETROTAPE_COMMAND_RECORD:
		log_cb(RETRO_LOG_WARN, "retro_tape_command. RETROTAPE_COMMAND_RECORD not supported.\n" );
		result = false;
		break;

	case RETROTAPE_COMMAND_STOP:
		tape_stop();
		result = true;
		break;

	case RETROTAPE_COMMAND_REWIND:
		Tape_Rewind();
		result = true;
		break;

	case RETROTAPE_COMMAND_FFWD:
		log_cb(RETRO_LOG_WARN, "retro_tape_command. RETROTAPE_COMMAND_FFWD not supported.\n" );
		result = false;
		break;

	case RETROTAPE_COMMAND_RESETCNT:
		log_cb(RETRO_LOG_WARN, "retro_tape_command. RETROTAPE_COMMAND_RESETCNT not supported.\n" );
		result = false;
		break;

	case RETROTAPE_COMMAND_EJECT:
		tape_eject();
		result = true;
		break;

	case RETROTAPE_COMMAND_INSERT:
		{
			int r;
			r = tape_insert( *(char**)param );
			result = ( r == 0 ); // 0 = OK
		}
		break;

	case RETROTAPE_COMMAND_CREATE:
		log_cb(RETRO_LOG_WARN, "retro_tape_command. RETROTAPE_COMMAND_CREATE not supported.\n" );
		result = false;
		break;

	default:
		log_cb(RETRO_LOG_ERROR, "retro_tape_command. Got unknown command %d.\n", command );
		break;

	}

	return result;
}
