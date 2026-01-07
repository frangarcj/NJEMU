/******************************************************************************

	ui.c

	User Interface Processing

******************************************************************************/

// #include "psp.h"
#include "emumain.h"
#include "stdarg.h"


#ifdef PSVITA
#include <unistd.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

UI_PALETTE ui_palette[UI_PAL_MAX] =
{
	{ 255, 255, 255 },	// UI_PAL_TITLE
	{ 255, 255, 255 },	// UI_PAL_SELECT
	{ 180, 180, 180 },	// UI_PAL_NORMAL
	{ 255, 255,  64 },	// UI_PAL_INFO
	{ 255,  64,  64 },	// UI_PAL_WARNING
	{  48,  48,  48 },	// UI_PAL_BG1
	{   0,   0, 160 },	// UI_PAL_BG2
	{   0,   0,   0 },	// UI_PAL_FRAME
	{  40,  40,  40 },	// UI_PAL_FILESEL1
	{ 120, 120, 120 }	// UI_PAL_FILESEL2
};

int cheat_num = 0;
gamecheat_t* gamecheat[MAX_CHEATS];

#if VIDEO_32BPP
int bgimage_type;
int bgimage_blightness;
#endif

void show_progress(const char *text)
{
	printf("show_progress: %s\n", text);
}

void update_progress(void)
{
}

void showmenu(void)
{
}

int draw_volume_status(int draw) {
	return 0;
}

int draw_battery_status(int draw) {
	return 0;	
}

void msg_screen_clear(void) {

}

void show_exit_screen(void) {

}

void load_background(int number)
{

}

int ui_show_popup(int draw) {
	return 0;
}

void file_browser(void) {
	Loop = LOOP_EXEC;
#ifdef PSVITA
	char base_dir[PATH_MAX];
	#if (EMU_SYSTEM == MVS)
		strcpy(base_dir, "ux0:/data/mvs");
	#elif (EMU_SYSTEM == CPS1)
		strcpy(base_dir, "ux0:/data/cps1");
	#elif (EMU_SYSTEM == CPS2)
		strcpy(base_dir, "ux0:/data/cps2");
	#elif (EMU_SYSTEM == NCDZ)
		strcpy(base_dir, "ux0:/data/ncdz");
	#else
		strcpy(base_dir, "ux0:/data/NJEMU");
	#endif

	// Create base directory before chdir
	sceIoMkdir(base_dir, 0777);
	
	printf("NJEMU Debug: Changing directory to %s\n", base_dir);
	if (chdir(base_dir) != 0) {
		printf("NJEMU Debug: FAILED to chdir to %s\n", base_dir);
	}

	// Set global directory variables using relative paths
	strcpy(launchDir, "./");
	strcpy(game_dir, "roms");
	strcpy(screenshotDir, "pict");
	#if USE_CACHE
	strcpy(cache_dir, "cache");
	#endif

	// Create subdirectories relative to base_dir
	sceIoMkdir(game_dir, 0777);
	sceIoMkdir(screenshotDir, 0777);
	#if USE_CACHE
	sceIoMkdir(cache_dir, 0777);
	#endif

	// Create other subdirs used by the core
	sceIoMkdir("state", 0777);
	sceIoMkdir("config", 0777);
	sceIoMkdir("nvram", 0777);
	sceIoMkdir("memcard", 0777);

	printf("NJEMU Debug: launchDir: %s\n", launchDir);
	printf("NJEMU Debug: game_dir: %s\n", game_dir);
#else
	strcpy(game_dir, "roms");
	#if USE_CACHE
	sprintf(cache_dir, "cache");
	#endif
#endif

	// Get the game name from a file called game_name.ini
	// Try to find it in the app dir or the data dir
	printf("NJEMU Debug: Searching for game_name.ini...\n");
	FILE *fp = fopen("game_name.ini", "r");
	if (!fp) {
		char ini_path[PATH_MAX];
		sprintf(ini_path, "%sgame_name.ini", launchDir);
		printf("NJEMU Debug: Not found in current dir, trying %s\n", ini_path);
		fp = fopen(ini_path, "r");
	}

	if (fp) {
		fgets(game_name, 255, fp);
		fclose(fp);
		// Remove newline if present
		char *p = strchr(game_name, '\n');
		if (p) *p = 0;
		p = strchr(game_name, '\r');
		if (p) *p = 0;
		printf("NJEMU Debug: Found game_name: [%s]\n", game_name);
	} else {
		printf("NJEMU Debug: game_name.ini NOT FOUND!\n");
	}

#if (EMU_SYSTEM == NCDZ)
	// For NCDZ, game_dir usually points to the game specific folder
	char ncdz_game_dir[PATH_MAX];
	sprintf(ncdz_game_dir, "%s/%s", game_dir, game_name);
	strcpy(game_dir, ncdz_game_dir);
	sceIoMkdir(game_dir, 0777);

	sprintf(mp3_dir, "%s/mp3", game_dir);
	sceIoMkdir(mp3_dir, 0777);
#endif
	emu_main();
}

void small_font_print(int sx, int sy, const char *s, int bg) {

}

void uifont_print_center(int sy, int r, int g, int b, const char *s) {

}

void uifont_print_shadow(int sx, int sy, int r, int g, int b, const char *s) {

}

void textfont_print(int sx, int sy, int r, int g, int b, const char *s, int flag) {
	printf("textfont_print: %s\n", s);
}

int uifont_get_string_width(const char *s) {
	return 1;
}

void uifont_print_shadow_center(int sy, int r, int g, int b, const char *s) {
	printf("uifont_print_shadow_center: %s\n", s);
}

void uifont_print(int sx, int sy, int r, int g, int b, const char *s) {
	printf("uifont_print: %s\n", s);
}

void small_icon_shadow(int sx, int sy, int r, int g, int b, int no) {

}

void show_background(void) {

}

void boxfill_alpha(int sx, int sy, int ex, int ey, int r, int g, int b, int alpha) {

}

void ui_popup_reset(void) {

}

void draw_dialog(int sx, int sy, int ex, int ey) {

}

int save_png(const char *path) {
	return 0;
}

void msg_screen_init(int wallpaper, int icon, const char *title) {

}

void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line) {

}

void ui_popup(const char *text, ...) {

}

int help(int number) {
	return 0;
}

void save_gamecfg(const char *name) {

}

void ui_init(void) {

}

void load_gamecfg(const char *name) {

}

void delete_files(const char *dirname, const char *pattern) {

}

void small_icon(int sx, int sy, int r, int g, int b, int no) {

}