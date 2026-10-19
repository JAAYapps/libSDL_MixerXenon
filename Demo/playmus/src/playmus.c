/*
    PLAYMUS:  A test application for the SDL mixer library.
    Copyright (C) 1997-2009 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

/* $Id: playmus.c 4211 2008-12-08 00:27:32Z slouken $ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#ifdef unix
#include <unistd.h>
#endif

#include "SDL.h"
#include "SDL_mixer.h"

#include <xetypes.h>
#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <usb/usbmain.h>
#include <diskio/ata.h>
#include <console/console.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_soc/xenon_power.h>
#include <libfat/fat.h>

int bdev_enum(int handle, const char **name);
extern void delay(int);

static int audio_open = 0;
static Mix_Music *music = NULL;
static int next_track = 0;

void CleanUp(int exitcode)
{
	if( Mix_PlayingMusic() ) {
		Mix_FadeOutMusic(1500);
		SDL_Delay(1500);
	}
	if ( music ) {
		Mix_FreeMusic(music);
		music = NULL;
	}
	if ( audio_open ) {
		Mix_CloseAudio();
		audio_open = 0;
	}
	SDL_Quit();
	exit(exitcode);
}

void Usage(char *argv0)
{
	fprintf(stderr, "Usage: %s [-i] [-l] [-8] [-r rate] [-c channels] [-b buffers] [-v N] [-rwops] <musicfile>\n", argv0);
}

void Menu(void)
{
	char buf[10];

	printf("Available commands: (p)ause (r)esume (h)alt volume(v#) > ");
	fflush(stdin);
	scanf("%s",buf);
	switch(buf[0]){
	case 'p': case 'P':
		Mix_PauseMusic();
		break;
	case 'r': case 'R':
		Mix_ResumeMusic();
		break;
	case 'h': case 'H':
		Mix_HaltMusic();
		break;
	case 'v': case 'V':
		Mix_VolumeMusic(atoi(buf+1));
		break;
	}
	printf("Music playing: %s Paused: %s\n", Mix_PlayingMusic() ? "yes" : "no",
		   Mix_PausedMusic() ? "yes" : "no");
}

void IntHandler(int sig)
{
	switch (sig) {
	        case SIGINT:
			next_track++;
			break;
	}
}

int main(int argc, char *argv[])
{
	Mix_Music *bg_music = NULL;
	//Mix_Chunk *select_sfx = NULL;

	//SDL_RWops *rwfp;
	int audio_rate;
	Uint16 audio_format;
	int audio_channels;
	int audio_buffers;
	int audio_volume = MIX_MAX_VOLUME;
	//int looping = 0;
	//int interactive = 0;
	//int rwops = 0;
	//int i = 0;

		int handle = -1;
		const char *s;
		char mountpath[32];
     	xenon_make_it_faster(XENON_SPEED_FULL);
    	xenos_init(VIDEO_MODE_AUTO);
        xenon_sound_init();
    	console_init();
    	usb_init();
    	usb_do_poll();

		xenon_ata_init();
		xenon_atapi_init();

		fatInitDefault();

		printf("Waiting for USB device to mount...\n");
		handle=bdev_enum(handle,&s);
		if(handle<0) return 0;

		sprintf(mountpath, "%s:/", s);
		chdir(mountpath);

		char /*sfx_path[64],*/ mus_path[64];
		//sprintf(sfx_path, "%s:/test.wav", s);
		sprintf(mus_path, "%s:/test2.ogg", s);

	/* Initialize variables */
	audio_rate = 48000;
	audio_format = AUDIO_S16;
	audio_channels = 2;
	audio_buffers = 16384;


	/* Initialize the SDL library */
	if ( SDL_Init(SDL_INIT_AUDIO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(255);
	}

	signal(SIGINT, IntHandler);
	signal(SIGTERM, CleanUp);

	/* Open the audio device */
	if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) < 0) {
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
		return(2);
	} else {
		Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
		printf("Opened audio at %d Hz %d bit %s (%s), %d bytes audio buffer\n", audio_rate,
			(audio_format&0xFF),
			(audio_channels > 2) ? "surround" : (audio_channels > 1) ? "stereo" : "mono",
			(audio_format&0x1000) ? "BE" : "LE",
			audio_buffers );
	}
	audio_open = 1;

	/* Set the music volume */
	Mix_VolumeMusic(audio_volume);

	/* Set the external music player, if any */
	Mix_SetMusicCMD(getenv("MUSIC_CMD"));

	//select_sfx = Mix_LoadWAV(sfx_path);

	// 1. Open the file natively via LibXenon
	FILE *file = fopen(mus_path, "rb");
	if (!file) {
		printf("Failed to open file!\n");
		return -1;
	}

	// 2. Get the exact file size
	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);

	printf("Loading %ld bytes directly into RAM...\n", fileSize);

	// 3. Allocate memory and read the ENTIRE file into RAM
	unsigned char *audioBuffer = (unsigned char *)malloc(fileSize);
	fread(audioBuffer, 1, fileSize, file);
	fclose(file);

	// 4. Hand the memory buffer over to SDL_mixer
	SDL_RWops *rw = SDL_RWFromMem(audioBuffer, fileSize);

	// Revert to the standard function since the Xenon fork lacks MUSType
	bg_music = Mix_LoadMUS_RW(rw);

	// 5. Check if it actually loaded
	if (bg_music == NULL) {
		printf("\n[ERROR] Mix_LoadMUS failed: %s\n", Mix_GetError());
		SDL_Delay(7000); // Pause for 7 seconds so you can read the screen!
		return -1;
	}

	// 6. Check if it actually started playing
	if (Mix_PlayMusic(bg_music, -1) == -1) {
		printf("\n[ERROR] Mix_PlayMusic failed: %s\n", Mix_GetError());
		SDL_Delay(7000);
		return -1;
	}

	printf("\nSUCCESS: Playing directly from RAM!\n");

	// 7. Simplified, clean testing loop
	while (Mix_PlayingMusic()) {
		usb_do_poll();
		SDL_Delay(10); // Keep the main thread alive
	}

	Mix_FreeMusic(bg_music);
	SDL_FreeRW(rw); // Clean up the RWops
	free(audioBuffer); // Clean up our massive RAM allocation
	CleanUp(0);

	/* Not reached, but fixes compiler warnings */
	return 0;
}
