/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ctype.h>
#include "cutils/misc.h"
#include "cutils/properties.h"
#include <dirent.h>
#include <getopt.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/limits.h>
#include <termios.h>
#include <time.h>

#include "tw_reboot.h"
#include "bootloader.h"
#include "common.h"
#include "extra-functions.h"
#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "recovery_ui.h"
#include "roots.h"
#include "backstore.h"
#include "themes.h"
#include "data.hpp"

#include "mounts/mounts.hpp"

//kang system() from bionic/libc/unistd and rename it __system() so we can be even more hackish :)
#undef _PATH_BSHELL
#define _PATH_BSHELL "/sbin/sh"

extern char **environ;

// sdcard partitioning variables
char swapsize[32];
int swapSz;
char extsize[32];
int ext;
int ext_format; // 3 or 4

int
__system(const char *command)
{
  pid_t pid;
	sig_t intsave, quitsave;
	sigset_t mask, omask;
	int pstat;
	char *argp[] = {"sh", "-c", NULL, NULL};

	if (!command)		/* just checking... */
		return(1);

	argp[2] = (char *)command;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	switch (pid = vfork()) {
	case -1:			/* error */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		return(-1);
	case 0:				/* child */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		execve(_PATH_BSHELL, argp, environ);
    _exit(127);
  }

	intsave = (sig_t)  bsd_signal(SIGINT, SIG_IGN);
	quitsave = (sig_t) bsd_signal(SIGQUIT, SIG_IGN);
	pid = waitpid(pid, (int *)&pstat, 0);
	sigprocmask(SIG_SETMASK, &omask, NULL);
	(void)bsd_signal(SIGINT, intsave);
	(void)bsd_signal(SIGQUIT, quitsave);
	return (pid == -1 ? -1 : pstat);
}

static struct pid {
	struct pid *next;
	FILE *fp;
	pid_t pid;
} *pidlist;

FILE *
__popen(const char *program, const char *type)
{
	struct pid * volatile cur;
	FILE *iop;
	int pdes[2];
	pid_t pid;

	if ((*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return (NULL);
	}

	if ((cur = malloc(sizeof(struct pid))) == NULL)
		return (NULL);

	if (pipe(pdes) < 0) {
		free(cur);
		return (NULL);
	}

	switch (pid = vfork()) {
	case -1:			/* Error. */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		free(cur);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
	    {
		struct pid *pcur;
		/*
		 * because vfork() instead of fork(), must leak FILE *,
		 * but luckily we are terminally headed for an execl()
		 */
		for (pcur = pidlist; pcur; pcur = pcur->next)
			close(fileno(pcur->fp));

		if (*type == 'r') {
			int tpdes1 = pdes[1];

			(void) close(pdes[0]);
			/*
			 * We must NOT modify pdes, due to the
			 * semantics of vfork.
			 */
			if (tpdes1 != STDOUT_FILENO) {
				(void)dup2(tpdes1, STDOUT_FILENO);
				(void)close(tpdes1);
				tpdes1 = STDOUT_FILENO;
			}
		} else {
			(void)close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				(void)dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
		}
		execl(_PATH_BSHELL, "sh", "-c", program, (char *)NULL);
		_exit(127);
		/* NOTREACHED */
	    }
	}

	/* Parent; assume fdopen can't fail. */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid =  pid;
	cur->next = pidlist;
	pidlist = cur;

	return (iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int
__pclose(FILE *iop)
{
	struct pid *cur, *last;
	int pstat;
	pid_t pid;

	/* Find the appropriate file pointer. */
	for (last = NULL, cur = pidlist; cur; last = cur, cur = cur->next)
		if (cur->fp == iop)
			break;

	if (cur == NULL)
		return (-1);

	(void)fclose(iop);

	do {
		pid = waitpid(cur->pid, &pstat, 0);
	} while (pid == -1 && errno == EINTR);

	/* Remove the entry from the linked list. */
	if (last == NULL)
		pidlist = cur->next;
	else
		last->next = cur->next;
	free(cur);

	return (pid == -1 ? -1 : pstat);
}

#define CMDLINE_SERIALNO        "androidboot.serialno="
#define CMDLINE_SERIALNO_LEN    (strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO        "Serial"
#define CPUINFO_SERIALNO_LEN    (strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE        "Hardware"
#define CPUINFO_HARDWARE_LEN    (strlen(CPUINFO_HARDWARE))

void get_device_id()
{
	FILE *fp;
    char line[2048];
	char hardware_id[32];
	char* token;

    // Assign a blank device_id to start with
    device_id[0] = 0;

    // First, try the cmdline to see if the serial number was supplied
	fp = fopen("/proc/cmdline", "rt");
	if (fp != NULL)
    {
        // First step, read the line. For cmdline, it's one long line
        fgets(line, sizeof(line), fp);
        fclose(fp);

        // Now, let's tokenize the string
        token = strtok(line, " ");

        // Let's walk through the line, looking for the CMDLINE_SERIALNO token
        while (token)
        {
            // We don't need to verify the length of token, because if it's too short, it will mismatch CMDLINE_SERIALNO at the NULL
            if (memcmp(token, CMDLINE_SERIALNO, CMDLINE_SERIALNO_LEN) == 0)
            {
                // We found the serial number!
                strcpy(device_id, token + CMDLINE_SERIALNO_LEN);
                return;
            }
            token = strtok(NULL, " ");
        }
    }

	// Now we'll try cpuinfo for a serial number
	fp = fopen("/proc/cpuinfo", "rt");
	if (fp != NULL)
    {
		while (fgets(line, sizeof(line), fp) != NULL) { // First step, read the line.
			if (memcmp(line, CPUINFO_SERIALNO, CPUINFO_SERIALNO_LEN) == 0)  // check the beginning of the line for "Serial"
			{
				// We found the serial number!
				token = line + CPUINFO_SERIALNO_LEN; // skip past "Serial"
				while ((*token > 0 && *token <= 32 ) || *token == ':') token++; // skip over all spaces and the colon
				if (*token != NULL) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
						memset(device_id, 0, sizeof(device_id));
						strncpy(device_id, token, strlen(token) - 1);
					} else {
						strcpy(device_id, token);
					}
					LOGI("=> serial from cpuinfo: '%s'\n", device_id);
					fclose(fp);
					return;
				}
			} else if (memcmp(line, CPUINFO_HARDWARE, CPUINFO_HARDWARE_LEN) == 0) {// We're also going to look for the hardware line in cpuinfo and save it for later in case we don't find the device ID
				// We found the hardware ID
				token = line + CPUINFO_HARDWARE_LEN; // skip past "Hardware"
				while ((*token > 0 && *token <= 32 ) || *token == ':')  token++; // skip over all spaces and the colon
				if (*token != NULL) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
                        memset(hardware_id, 0, sizeof(hardware_id));
						strncpy(hardware_id, token, strlen(token) - 1);
					} else {
						strcpy(hardware_id, token);
					}
					LOGI("=> hardware id from cpuinfo: '%s'\n", hardware_id);
				}
			}
		}
		fclose(fp);
    }
	
	if (hardware_id[0] != 0) {
		LOGW("\nusing hardware id for device id: '%s'\n", hardware_id);
		strcpy(device_id, hardware_id);
		return;
	}

    strcpy(device_id, "serialno");
	LOGE("=> device id not found, using '%s'.", device_id);
    return;
}

void show_fake_main_menu() {
	// Main Menu
	#define ITEM_APPLY_SDCARD        0
	#define ITEM_NANDROID_MENU     	 1
	#define ITEM_MAIN_WIPE_MENU      2
	#define ITEM_ADVANCED_MENU       3
	#define ITEM_MOUNT_MENU       	 4
	#define ITEM_USB_TOGGLE          5
	#define ITEM_REBOOT              6
	#define ITEM_SHUTDOWN		     7
    
	char** headers = prepend_title((const char**)MENU_HEADERS);

    char* MENU_ITEMS[] = {  (char*) "Install Zip",
                            (char*) "Nandroid Menu",
                            (char*) "Wipe Menu",
                            (char*) "Advanced Menu",
                            (char*) "Mount Menu",
                            (char*) "USB Storage Toggle",
                            (char*) "Reboot system now",
                            (char*) "Power down system",
                            NULL };

	go_home = 0;
	go_menu = 0;
	menu_loc_idx = 0;
	ui_reset_progress();
	
	int chosen_item = get_menu_selection(headers, MENU_ITEMS, 0, 0);

	// device-specific code may take some action here.  It may
	// return one of the core actions handled in the switch
	// statement below.
	chosen_item = device_perform_action(chosen_item);

   
	switch (chosen_item) {
		case ITEM_APPLY_SDCARD:
			install_zip_menu(0);
			break;

		case ITEM_NANDROID_MENU:
			nandroid_menu();
			break;
			
		case ITEM_MAIN_WIPE_MENU:
			main_wipe_menu();
			break;

		case ITEM_ADVANCED_MENU:
			advanced_menu();
			break;

		case ITEM_MOUNT_MENU:
			mount_menu(0);
			break;
			
		case ITEM_USB_TOGGLE:
			usb_storage_toggle();
			break;

		case ITEM_REBOOT:
			go_reboot = 1;
			return;

	case ITEM_SHUTDOWN:
		tw_reboot(rb_poweroff);
	break;
	}
	if (go_menu) {
		advanced_menu();
	}
	if (go_restart || go_home) {
		go_restart = 0;
		go_home = 0;
	}
	ui_end_menu();
	return;
}



/*partial kangbang from system/vold
TODO: Currently only one mount is supported, defaulting
/mnt/sdcard to lun0 and anything else gets no love. Fix this.
*/
#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/devices/platform/usb_mass_storage/lun"
#endif

int usb_storage_enable(void)
{
    int fd;

    Volume *vol = volume_for_path("/sdcard"); 
    if (!vol)
    {
        LOGE("Unable to locate volume information.");
        return -1;
    }

    if ((fd = open(CUSTOM_LUN_FILE"0/file", O_WRONLY)) < 0)
    {
        LOGE("Unable to open ums lunfile: (%s)", strerror(errno));
        return -1;
    }

    if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
        (!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
        LOGE("Unable to write to ums lunfile: (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int usb_storage_disable(void)
{
    int fd;

    if ((fd = open(CUSTOM_LUN_FILE"0/file", O_WRONLY)) < 0)
    {
        LOGE("Unable to open ums lunfile: (%s)", strerror(errno));
        return -1;
    }

    char ch = 0;
    if (write(fd, &ch, 1) < 0)
    {
        LOGE("Unable to write to ums lunfile: (%s)", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void usb_storage_toggle()
{
/*maybe make this a header instead?
    static char* headers[] = {  "Mounting USB as storage device",
                                "",
                                NULL
    };
*/
    ui_print("\nMounting USB as storage device...");

    if (usb_storage_enable())
        return;

    ui_clear_key_queue();
    ui_print("\nUSB as storage device mounted!\n");
    ui_print("\nPress Power to disable,");
    ui_print("\nand return to menu\n");

    for (;;) {
        int key = ui_wait_key();
        if (key == KEY_POWER) {
            ui_print("\nDisabling USB as storage device...");

            usb_storage_disable();
            break;
        }
    }
    return;
}

char* zip_verify()
{
	char* tmp_set = (char*)malloc(40);
	strcpy(tmp_set, "[ ] Zip Signature Verification");
	if (DataManager::GetIntValue(TW_SIGNED_ZIP_VERIFY_VAR) == 1) {
		tmp_set[1] = 'x';
	}
	return tmp_set;
}

char* reboot_after_flash()
{
	char* tmp_set = (char*)malloc(40);
	strcpy(tmp_set, "[ ] Reboot After Successful Flash");
	if (DataManager::GetIntValue(TW_REBOOT_AFTER_FLASH_VAR) == 1) {
		tmp_set[1] = 'x';
	}
	return tmp_set;
}

char* force_md5_check()
{
    char* tmp_set = (char*)malloc(40);
    strcpy(tmp_set, "[ ] Force md5 verification");
    if (DataManager::GetIntValue(TW_FORCE_MD5_CHECK_VAR) == 1) {
        tmp_set[1] = 'x';
    }
    return tmp_set;
}

char* sort_by_date_option()
{
    char* tmp_set = (char*)malloc(40);
    strcpy(tmp_set, "[ ] Sort Zips by Date");
    if (DataManager::GetIntValue(TW_SORT_FILES_BY_DATE_VAR) == 1) {
        tmp_set[1] = 'x';
    }
    return tmp_set;
}

char* rm_rf_option()
{
    char* tmp_set = (char*)malloc(40);
    strcpy(tmp_set, "[ ] rm -rf Instead of Format");
    if (DataManager::GetIntValue(TW_RM_RF_VAR) == 1) {
        tmp_set[1] = 'x';
    }
    return tmp_set;
}

void install_zip_menu(int pIdx)
{
	// INSTALL ZIP MENU
	#define ITEM_CHOOSE_ZIP           0
	#define ITEM_FLASH_ZIPS           1
	#define ITEM_CLEAR_ZIPS           2
	#define ITEM_WIPE_CACHE_DALVIK    3
	#define ITEM_SORT_BY_DATE         4
	#define ITEM_REBOOT_AFTER_FLASH   5
	#define ITEM_TOGGLE_SIG           6
	#define ITEM_TOGGLE_FORCE_MD5	  7
	#define ITEM_ZIP_RBOOT			  8
	#define ITEM_ZIP_BACK		      9
	
    ui_set_background(BACKGROUND_ICON_FLASH_ZIP);
    static char* MENU_FLASH_HEADERS[] = { (char*) "Install Zip Menu",
    									  (char*) "Flash Zip From SD Card:",
                                          NULL };

	char* MENU_INSTALL_ZIP[] = {  (char*) "--> Choose Zip To Flash",
	                              (char*) "Flash Zips Now",
								  (char*) "Clear Zip Queue",
								  (char*) "Wipe Cache and Dalvik Cache",
								  sort_by_date_option(),
			  	  	  	  	  	  reboot_after_flash(),
								  zip_verify(),
								  force_md5_check(),
                                  (char*) "--> Reboot To System",
	                              (char*) "<-- Back To Main Menu",
	                              NULL };

	char** headers = prepend_title((const char**)MENU_FLASH_HEADERS);
	int zip_loop_index, status;
	
    inc_menu_loc(ITEM_ZIP_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_INSTALL_ZIP, 0, pIdx);
        pIdx = chosen_item;
        switch (chosen_item)
        {
            case ITEM_CHOOSE_ZIP:
				if (multi_zip_index < 10) {
					status = sdcard_directory(DataManager::GetStrValue(TW_ZIP_LOCATION_VAR).c_str());
				} else {
					ui_print("Maximum of %i zips queued.\n", multi_zip_index);
				}
				break;
			case ITEM_FLASH_ZIPS:
				if (multi_zip_index == 0) {
					ui_print("No zips selected to install.\n");
				} else {
					for (zip_loop_index; zip_loop_index < multi_zip_index; zip_loop_index++) {
						LOGI("Installing %s\n", multi_zip_array[zip_loop_index]);
						status = install_zip_package(multi_zip_array[zip_loop_index]);
						ui_reset_progress();  // reset status bar so it doesnt run off the screen 
						if (status != INSTALL_SUCCESS) {
							if (notError == 1) {
								ui_set_background(BACKGROUND_ICON_ERROR);
								ui_print("Installation aborted due to an error.\n");
								multi_zip_index = 0; // clear zip queue
								break;
							}
						} // end if (status != INSTALL_SUCCESS)
					} // end for loop
					if (!ui_text_visible()) {
						multi_zip_index = 0; // clear zip queue
						return;  // reboot if logs aren't visible
					} else {
						if (DataManager::GetIntValue(TW_REBOOT_AFTER_FLASH_VAR)) {
							tw_reboot(rb_system);
							return;
						}
						if (go_home != 1) {
							ui_print("\nInstall from sdcard complete.\n");
						}
						multi_zip_index = 0; // clear zip queue
					} // end if (!ui_text_visible())
				} // end if (multi_zip_index == 0)
				break;
			case ITEM_CLEAR_ZIPS:
				multi_zip_index = 0;
				ui_print("\nZip Queue Cleared\n\n");
				break;
			case ITEM_WIPE_CACHE_DALVIK:
				ui_print("\n-- Wiping Cache Partition...\n");
                erase_volume("/cache");
                ui_print("-- Cache Partition Wipe Complete!\n");
				wipe_dalvik_cache();
				break;
            case ITEM_REBOOT_AFTER_FLASH:
                DataManager::ToggleIntValue(TW_REBOOT_AFTER_FLASH_VAR);
                break;
			case ITEM_SORT_BY_DATE:
                DataManager::ToggleIntValue(TW_SORT_FILES_BY_DATE_VAR);
                break;
            case ITEM_TOGGLE_SIG:
                DataManager::ToggleIntValue(TW_SIGNED_ZIP_VERIFY_VAR);
                break;
			case ITEM_TOGGLE_FORCE_MD5:
                DataManager::ToggleIntValue(TW_FORCE_MD5_CHECK_VAR);
                break;            
            case ITEM_ZIP_RBOOT:
                tw_reboot(rb_system);
                break;
            case ITEM_ZIP_BACK:
    	        dec_menu_loc();
                ui_set_background(BACKGROUND_ICON_MAIN);
                return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
        break;
    }
	ui_end_menu();
    dec_menu_loc();
	install_zip_menu(pIdx);
}

void wipe_dalvik_cache()
{
    ui_set_background(BACKGROUND_ICON_WIPE);
    
    // Retrieve the volumes
    IVolume* data = VolumeManager::findVolume("/data");
    IVolume* cache = VolumeManager::findVolume("/data");
    IVolume* sdext = VolumeManager::findVolume("/sd-ext");
    
    if (data)   data->mount();
    if (cache)  cache->mount();
    
    ui_print("\n-- Wiping Dalvik Cache Directories...\n");
    __system("rm -rf /data/dalvik-cache");
    ui_print("Cleaned: /data/dalvik-cache...\n");
    __system("rm -rf /cache/dalvik-cache");
    ui_print("Cleaned: /cache/dalvik-cache...\n");
    __system("rm -rf /cache/dc");
    ui_print("Cleaned: /cache/dc\n");
    
    if (sdext && sdext->isMountable())
    {
        LOGI("Mounting /sd-ext\n");
        sdext->mount();
        __system("rm -rf /sd-ext/dalvik-cache");
        ui_print("Cleaned: /sd-ext/dalvik-cache...\n");
    }
    if (data)   data->unmount();
    ui_print("-- Dalvik Cache Directories Wipe Complete!\n\n");
    ui_set_background(BACKGROUND_ICON_MAIN);
    if (!ui_text_visible()) return;
}

void reboot_menu()
{

	#define ITEM_SYSTEM      0
	#define ITEM_RECOVERY    1
	#define ITEM_BOOTLOADER  2
	#define ITEM_POWEROFF    3
	#define ITEM_BACKK		 4
	

    static char* MENU_REBOOT_HEADERS[] = {  (char*) "Reboot Menu",
    										(char*) "Choose Your Destiny:",
                                            NULL };
    
	// REBOOT MENU
	char* MENU_REBOOT[] = { (char*) "Reboot To System",
	                        (char*) "Reboot To Recovery",
	                        (char*) "Reboot To Bootloader",
	                        (char*) "Power Off",
	                        (char*) "<-- Back To Advanced Menu",
	                        NULL };

    
    char** headers = prepend_title((const char**)MENU_REBOOT_HEADERS);

    // This is an old, common method for ensuring a flush
    sync();
    sync();
    
    inc_menu_loc(ITEM_BACKK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_REBOOT, 0, 0);
        switch (chosen_item)
        {
            case ITEM_RECOVERY:
                tw_reboot(rb_recovery);
                break;

            case ITEM_BOOTLOADER:
                tw_reboot(rb_bootloader);
                break;

            case ITEM_POWEROFF:
                tw_reboot(rb_poweroff);
                break;

            case ITEM_SYSTEM:
                tw_reboot(rb_system);
                break;

            case ITEM_BACKK:
            	dec_menu_loc();
                return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
    }
}

// BATTERY STATS
void wipe_battery_stats()
{
    ensure_path_mounted("/data");
    struct stat st;
    if (0 != stat("/data/system/batterystats.bin", &st))
    {
        ui_print("No Battery Stats Found. No Need To Wipe.\n");
    } else {
        ui_set_background(BACKGROUND_ICON_WIPE);
        remove("/data/system/batterystats.bin");
        ui_print("Cleared: Battery Stats...\n");
        ensure_path_unmounted("/data");
    }
}

// ROTATION SETTINGS
void wipe_rotate_data()
{
    ui_set_background(BACKGROUND_ICON_WIPE);
    ensure_path_mounted("/data");
    __system("rm -r /data/misc/akmd*");
    __system("rm -r /data/misc/rild*");
    ui_print("Cleared: Rotatation Data...\n");
    ensure_path_unmounted("/data");
}   

void fix_perms()
{
	ensure_path_mounted("/data");
	ensure_path_mounted("/system");
	ui_show_progress(1,30);
    ui_print("\n-- Fixing Permissions\n");
	ui_print("This may take a few minutes.\n");
	__system("./sbin/fix_permissions.sh");
	ui_print("-- Done.\n\n");
	ui_reset_progress();
}

void advanced_menu()
{
	// ADVANCED MENU
	#ifdef BOARD_HAS_NO_REAL_SDCARD
	#define ITEM_REBOOT_MENU       0
	#define ITEM_FORMAT_MENU       1
	#define ITEM_FIX_PERM          2
	#define ITEM_ALL_SETTINGS      3
	#define ITEM_SDCARD_PART       99
	#define ITEM_CPY_LOG		   4
	#define ADVANCED_MENU_BACK     5
    
	char* MENU_ADVANCED[] = { "Reboot Menu",
	                          "Format Menu",
	                          "Fix Permissions",
	                          "Change twrp Settings",
	                          "Copy recovery log to /sdcard",
	                          "<-- Back To Main Menu",
	                          NULL };
	#else
	#define ITEM_REBOOT_MENU       0
	#define ITEM_FORMAT_MENU       1
	#define ITEM_FIX_PERM          2
	#define ITEM_ALL_SETTINGS      3
	#define ITEM_SDCARD_PART       4
	#define ITEM_CPY_LOG		   5
	#define ADVANCED_MENU_BACK     6
    
	char* MENU_ADVANCED[] = { (char*) "Reboot Menu",
	                          (char*) "Format Menu",
	                          (char*) "Fix Permissions",
	                          (char*) "Change twrp Settings",
							  (char*) "Partition SD Card",
	                          (char*) "Copy recovery log to /sdcard",
	                          (char*) "<-- Back To Main Menu",
	                          NULL };
	#endif
	
	static char* MENU_ADVANCED_HEADERS[] = { (char*) "Advanced Menu",
    										 (char*) "Reboot, Format, or twrp!",
                                              NULL };

    char** headers = prepend_title((const char**)MENU_ADVANCED_HEADERS);
    
    inc_menu_loc(ADVANCED_MENU_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_ADVANCED, 0, 0);
        switch (chosen_item)
        {
            case ITEM_REBOOT_MENU:
                reboot_menu();
                break;
            case ITEM_FORMAT_MENU:
                format_menu();
                break;
            case ITEM_FIX_PERM:
            	fix_perms();
                break;
            case ITEM_ALL_SETTINGS:
			    all_settings_menu(0);
				break;
			case ITEM_SDCARD_PART:
				show_menu_partition();
				break;
            case ITEM_CPY_LOG:
                ensure_path_mounted("/sdcard");
            	__system("cp /tmp/recovery.log /sdcard");
				sync();
                ui_print("Copied recovery log to /sdcard.\n");
            	break;
            case ADVANCED_MENU_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
    }
}

int erase_volume(const char *volume)
{
    IVolume* volume = VolumeManager::findVolume(volume);
    if (volume)
    {
        if (volume == VolumeManager::findVolume("/cache"))
        {
            // Reset the tmplog_offset, since we'll wipe out the logfile
            tmplog_offset = 0;
        }

        ui_print("Formatting %s...\n", volume);
        volume->format();
    }
    return -1;
}

// FORMAT MENU
void
format_menu()
{
	struct stat st;
	
	#define ITEM_FORMAT_SYSTEM      0
	#define ITEM_FORMAT_DATA        1
	#define ITEM_FORMAT_CACHE       2
	#define ITEM_FORMAT_SDCARD      3
	#define ITEM_FORMAT_SDEXT		4
	#define ITEM_FORMAT_BACK        5
	
	char* part_headers[] = {    (char*) "Format Menu",
                                (char*) "Choose a Partition to Format: ",
                                NULL };
	
    char* part_items[] = { 	(char*) "Format SYSTEM (/system)",
                            (char*) "Format DATA (/data)",
            				(char*) "Format CACHE (/cache)",
                            (char*) "Format SDCARD (/sdcard)",
                            (char*) "Format SD-EXT (/sd-ext)",
						    (char*) "<-- Back To Advanced Menu",
						    NULL };

    char** headers = prepend_title((const char**)part_headers);

    IVolume* sdeext = VolumeManager::findVolume("/sd-ext");
    
    inc_menu_loc(ITEM_FORMAT_BACK);
	for (;;)
	{
                ui_set_background(BACKGROUND_ICON_WIPE_CHOOSE);
		int chosen_item = get_menu_selection(headers, part_items, 0, 0);
		switch (chosen_item)
		{
			case ITEM_FORMAT_SYSTEM:
				confirm_format("SYSTEM", "/system");
				break;
			case ITEM_FORMAT_DATA:
                confirm_format("DATA", "/data");
                break;
			case ITEM_FORMAT_CACHE:
                confirm_format("CACHE", "/cache");
                break;
			case ITEM_FORMAT_SDCARD:
                confirm_format("SDCARD", "/sdcard");
                break;
            case ITEM_FORMAT_SDEXT:
            	if (sdeext && sdeext->isMountable())
            	{
                    sdeext->mount();
                    confirm_format("SD-EXT", "/sd-ext");
            	} else {
            		ui_print("\n/sd-ext not detected! Aborting.\n");
            	}
            	break;
			case ITEM_FORMAT_BACK:
            	dec_menu_loc();
                ui_set_background(BACKGROUND_ICON_MAIN);
				return;
		}
	    if (go_home) { 
    	        dec_menu_loc();
                ui_set_background(BACKGROUND_ICON_MAIN);
	        return;
	    }
	}
}

void
confirm_format(char* volume_name, char* volume_path) {

    char* confirm_headers[] = { (char*) "Confirm Format of Partition: ",
                                volume_name,
                                (char*) "",
                                (char*) "  THIS CAN NOT BE UNDONE!",
                                NULL };

    char* items[] = {   (char*) "No",
                        (char*) "Yes -- Permanently Format",
                        NULL };
    
    char** headers = prepend_title((const char**)confirm_headers);
    
    inc_menu_loc(0);
    int chosen_item = get_menu_selection(headers, items, 1, 0);
    if (chosen_item != 1) {
        dec_menu_loc();
        return;
    } else {
        ui_set_background(BACKGROUND_ICON_WIPE);
        ui_print("\n-- Wiping %s Partition...\n", volume_name);
        erase_volume(volume_path);
        ui_print("-- %s Partition Wipe Complete!\n", volume_name);
        dec_menu_loc();
    }
}

int get_battery_level(void)
{
    static int lastVal = -1;
    static time_t nextSecCheck = 0;

    struct timeval curTime;
    gettimeofday(&curTime, NULL);
    if (curTime.tv_sec > nextSecCheck)
    {
        char cap_s[4];
        FILE * cap = fopen("/sys/class/power_supply/battery/capacity","rt");
        if (cap)
        {
            fgets(cap_s, 4, cap);
            fclose(cap);
            lastVal = atoi(cap_s);
            if (lastVal > 100)  lastVal = 100;
            if (lastVal < 0)    lastVal = 0;
        }
        nextSecCheck = curTime.tv_sec + 60;
    }
    return lastVal;
}

char* 
print_batt_cap()  {
	char* full_cap_s = (char*)malloc(30);
	char full_cap_a[30];
	
	int cap_i = get_battery_level();
    
    //int len = strlen(cap_s);
	//if (cap_s[len-1] == '\n') {
	//	cap_s[len-1] = 0;
	//}
	
	// Get a usable time
	struct tm *current;
	time_t now;
	now = time(0);
	current = localtime(&now);
	
	sprintf(full_cap_a, "Battery Level: %i%% @ %02D:%02D", cap_i, current->tm_hour, current->tm_min);
	strcpy(full_cap_s, full_cap_a);
	
	return full_cap_s;
}

void time_zone_menu()
{
	#define TZ_REBOOT           0
	#define TZ_MINUS            1
	#define TZ_PLUS             2
	#define TZ_MAIN_BACK        3

    static char* MENU_TZ_HEADERS[] = { (char*) "Time Zone",
    								   (char*) "Select Region:",
                                       NULL };
    
	char* MENU_TZ[] =       { (char*) "[RESTART MENU AND APPLY TIME ZONE]",
            				  (char*) "Minus (GMT 0 to -11)",
							  (char*) "Plus  (GMT +1 to +12)",
							  (char*) "<-- Back To twrp Settings",
	                          NULL };

    char** headers = prepend_title((const char**)MENU_TZ_HEADERS);
    
    inc_menu_loc(TZ_MAIN_BACK);
	
	ui_print("Currently selected time zone: %s\n", DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str());
	
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_TZ, 0, 1);
        switch (chosen_item)
        {
			case TZ_REBOOT:
				go_home = 1;
				go_restart = 1;
				break;
            case TZ_MINUS:
            	time_zone_minus();
                break;
            case TZ_PLUS:
            	time_zone_plus();
                break;
            case TZ_MAIN_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
    }
}

void time_zone_minus()
{
	#define TZ_GMT_MINUS11		0
	#define TZ_GMT_MINUS10		1
	#define TZ_GMT_MINUS09		2
	#define TZ_GMT_MINUS08		3
	#define TZ_GMT_MINUS07		4
	#define TZ_GMT_MINUS06		5
	#define TZ_GMT_MINUS05		6
	#define TZ_GMT_MINUS04		7
	#define TZ_GMT_MINUS03		8
	#define TZ_GMT_MINUS02		9
	#define TZ_GMT_MINUS01		10
	#define TZ_GMT      		11
	#define TZ_GMTCUT      		12
	#define TZ_MINUS_MENU_BACK  13

    static char* MENU_TZ_HEADERS_MINUS[] =    { (char*) "Time Zone",
    								            (char*) "Instant Time Machine:",
                                                NULL };
    
	char* MENU_TZ_MINUS[] =       { (char*) "GMT-11 (BST)",
									(char*) "GMT-10 (HST)",
							        (char*) "GMT -9 (AKST)",
						            (char*) "GMT -8 (PST)",
							        (char*) "GMT -7 (MST)",
							        (char*) "GMT -6 (CST)",
							        (char*) "GMT -5 (EST)",
							        (char*) "GMT -4 (AST)",
									(char*) "GMT -3 (GRNLNDST)",
									(char*) "GMT -2 (FALKST)",
									(char*) "GMT -1 (AZOREST)",
									(char*) "GMT  0 (GMT)",
									(char*) "GMT  0 (CUT)",
	                                (char*) "<-- Back To twrp Settings",
	                                NULL };

    char** headers = prepend_title((const char**)MENU_TZ_HEADERS_MINUS);
    
    inc_menu_loc(TZ_MINUS_MENU_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_TZ_MINUS, 0, 6);
        switch (chosen_item)
        {
            case TZ_GMT_MINUS11:
            	DataManager::SetStrValue(TW_TIME_ZONE_VAR, "BST11");
				strcpy(time_zone_dst_string, "BDT");
                break;
			case TZ_GMT_MINUS10:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "HST10");
				strcpy(time_zone_dst_string, "HDT");
                break;
            case TZ_GMT_MINUS09:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "AST9");
				strcpy(time_zone_dst_string, "ADT");
                break;
            case TZ_GMT_MINUS08:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "PST8");
				strcpy(time_zone_dst_string, "PDT");
                break;
            case TZ_GMT_MINUS07:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "MST7");
				strcpy(time_zone_dst_string, "MDT");
                break;
            case TZ_GMT_MINUS06:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "CST6");
				strcpy(time_zone_dst_string, "CDT");
                break;
            case TZ_GMT_MINUS05:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "EST5");
				strcpy(time_zone_dst_string, "EDT");
                break;
            case TZ_GMT_MINUS04:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "AST4");
				strcpy(time_zone_dst_string, "ADT");
                break;
			case TZ_GMT_MINUS03:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "GRNLNDST3");
				strcpy(time_zone_dst_string, "GRNLNDDT");
                break;
			case TZ_GMT_MINUS02:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "FALKST2");
				strcpy(time_zone_dst_string, "FALKDT");
                break;
			case TZ_GMT_MINUS01:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "AZOREST1");
				strcpy(time_zone_dst_string, "AZOREDT");
                break;
			case TZ_GMT:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "GMT0");
				strcpy(time_zone_dst_string, "BST");
                break;
			case TZ_GMTCUT:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "CUT0");
				strcpy(time_zone_dst_string, "GDT");
                break;
            case TZ_MINUS_MENU_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    } else {
			ui_print("New time zone: %s", DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str());
			time_zone_offset();
			time_zone_dst();
			update_tz_environment_variables();
			ui_print("\nTime zone change requires reboot.\n");
			dec_menu_loc();
			return;
		}
    }
}

void time_zone_plus()
{
	#define TZ_GMT_PLUS01		0
	#define TZ_GMT_PLUS02USAST  1
	#define TZ_GMT_PLUS02WET	2
	#define TZ_GMT_PLUS03SAUST  3
	#define TZ_GMT_PLUS03MEST	4
	#define TZ_GMT_PLUS04   	5
	#define TZ_GMT_PLUS05   	6
	#define TZ_GMT_PLUS06   	7
	#define TZ_GMT_PLUS07   	8
	#define TZ_GMT_PLUS08TAIST	9
	#define TZ_GMT_PLUS08WAUST 10
	#define TZ_GMT_PLUS09KORST 11
	#define TZ_GMT_PLUS09JST   12
	#define TZ_GMT_PLUS10      13
	#define TZ_GMT_PLUS11      14
	#define TZ_GMT_PLUS12      15
	#define TZ_PLUS_MENU_BACK  16

    static char* MENU_TZ_HEADERS_PLUS[] =    { (char*) "Time Zone",
    								           (char*) "Instant Time Machine:",
                                               NULL };
    
	char* MENU_TZ_PLUS[] =       {     (char*) "GMT +1 (NFT)",
							           (char*) "GMT +2 (USAST)",
						               (char*) "GMT +2 (WET)",
							           (char*) "GMT +3 (SAUST)",
						               (char*) "GMT +3 (MEST)",
							           (char*) "GMT +4 (WST)",
							           (char*) "GMT +5 (PAKST)",
									   (char*) "GMT +6 (TASHST)",
									   (char*) "GMT +7 (THAIST)",
									   (char*) "GMT +8 (TAIST)",
									   (char*) "GMT +8 (WAUST)",
									   (char*) "GMT +9 (KORST)",
									   (char*) "GMT +9 (JST)",
									   (char*) "GMT+10 (EET)",
									   (char*) "GMT+11 (MET)",
									   (char*) "GMT+12 (NZST)",
	                                   (char*) "<-- Back To twrp Settings",
	                                   NULL };

    char** headers = prepend_title((const char**)MENU_TZ_HEADERS_PLUS);
    
    inc_menu_loc(TZ_PLUS_MENU_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_TZ_PLUS, 0, 3); // puts the initially selected item to MST/MDT which should be right in the middle of the most used time zones for ease of use
        switch (chosen_item)
        {
            case TZ_GMT_PLUS01:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "NFT-1");
				strcpy(time_zone_dst_string, "DFT");
                break;
            case TZ_GMT_PLUS02USAST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "USAST-2");
				strcpy(time_zone_dst_string, "USADT");
                break;
            case TZ_GMT_PLUS02WET:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "WET-2");
				strcpy(time_zone_dst_string, "WET");
                break;
            case TZ_GMT_PLUS03SAUST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "SAUST-3");
				strcpy(time_zone_dst_string, "SAUDT");
                break;
            case TZ_GMT_PLUS03MEST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "MEST-3");
				strcpy(time_zone_dst_string, "MEDT");
                break;
            case TZ_GMT_PLUS04:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "WST-4");
				strcpy(time_zone_dst_string, "WDT");
                break;
            case TZ_GMT_PLUS05:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "PAKST-5");
				strcpy(time_zone_dst_string, "PAKDT");
                break;
			case TZ_GMT_PLUS06:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "TASHST-6");
				strcpy(time_zone_dst_string, "TASHDT");
                break;
			case TZ_GMT_PLUS07:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "THAIST-7");
				strcpy(time_zone_dst_string, "THAIDT");
                break;
			case TZ_GMT_PLUS08TAIST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "TAIST-8");
				strcpy(time_zone_dst_string, "TAIDT");
                break;
			case TZ_GMT_PLUS08WAUST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "WAUST-8");
				strcpy(time_zone_dst_string, "WAUDT");
                break;
			case TZ_GMT_PLUS09KORST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "KORST-9");
				strcpy(time_zone_dst_string, "KORDT");
                break;
			case TZ_GMT_PLUS09JST:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "JST-9");
				strcpy(time_zone_dst_string, "JSTDT");
                break;
			case TZ_GMT_PLUS10:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "EET-10");
				strcpy(time_zone_dst_string, "EETDT");
                break;
			case TZ_GMT_PLUS11:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "MET-11");
				strcpy(time_zone_dst_string, "METDT");
                break;
			case TZ_GMT_PLUS12:
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, "NZST-12");
				strcpy(time_zone_dst_string, "NZDT");
                break;
            case TZ_PLUS_MENU_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    } else {
            ui_print("New time zone: %s", DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str());
			time_zone_offset();
			time_zone_dst();
			update_tz_environment_variables();
			ui_print("\nTime zone changes take effect after reboot.\n");
			dec_menu_loc();
			return;
		}
    }
}

void time_zone_offset() {
    #define TZ_OFF00             0
	#define TZ_OFF15             1
	#define TZ_OFF30             2
	#define TZ_OFF45             3
	#define TZ_OFF_BACK          4

    static char* MENU_TZOFF_HEADERS[] = { (char*) "Time Zone",
    								      (char*) "Select Offset:",
                                          NULL };
    
	char* MENU_TZOFF[] =       {  (char*) "No Offset",
					    		  (char*) ":15",
								  (char*) ":30",
								  (char*) ":45",
	                              (char*) "<-- Back To Time Zones",
	                          NULL };

    char** headers = prepend_title((const char**)MENU_TZOFF_HEADERS);
    
    inc_menu_loc(TZ_OFF_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_TZOFF, 0, 0);
        switch (chosen_item)
        {
            case TZ_OFF00:
				strcpy(time_zone_offset_string, "");
				break;
			case TZ_OFF15:
            	strcpy(time_zone_offset_string, ":15");
                break;
            case TZ_OFF30:
            	strcpy(time_zone_offset_string, ":30");
                break;
			case TZ_OFF45:
            	strcpy(time_zone_offset_string, ":45");
                break;
            case TZ_OFF_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    } else {
			dec_menu_loc();
			ui_print_overwrite("New time zone: %s%s", DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str(), time_zone_offset_string);
			return;
		}
    }
}

void time_zone_dst() {
	#define TZ_DST_YES           0
	#define TZ_DST_NO            1
	#define TZ_DST_BACK          2

    static char* MENU_TZDST_HEADERS[] = { (char*) "Time Zone",
    								      (char*) "Select Daylight Savings Option:",
                                          NULL };
    
	char* MENU_TZDST[] =       { (char*) "Yes, I have DST",
							     (char*) "No DST",
	                             (char*) "<-- Back To Offsets",
	                             NULL };
	
    char** headers = prepend_title((const char**)MENU_TZDST_HEADERS);
    
    inc_menu_loc(TZ_DST_BACK);

    char tmpStr[64];
    strcpy(tmpStr, DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str());

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_TZDST, 0, 0);
        switch (chosen_item)
        {
            case TZ_DST_YES:
            	strcat(tmpStr, time_zone_offset_string);
				strcat(tmpStr, time_zone_dst_string);
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, tmpStr);
				ui_print_overwrite("New time zone: %s", tmpStr);
                break;
            case TZ_DST_NO:
				strcat(tmpStr, time_zone_offset_string);
                DataManager::SetStrValue(TW_TIME_ZONE_VAR, tmpStr);
                break;
            case TZ_DST_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    } else {
			dec_menu_loc();
			return;
		}
    }
}

void update_tz_environment_variables() {
    setenv("TZ", DataManager::GetStrValue(TW_TIME_ZONE_VAR).c_str(), 1);
    tzset();
}

void inc_menu_loc(int bInt)
{
	menu_loc_idx++;
	menu_loc[menu_loc_idx] = bInt;
	//ui_print("=> Increased Menu Level; %d : %d\n",menu_loc_idx,menu_loc[menu_loc_idx]);  //TURN THIS ON TO DEBUG
}
void dec_menu_loc()
{
	menu_loc_idx--;
	//ui_print("=> Decreased Menu Level; %d : %d\n",menu_loc_idx,menu_loc[menu_loc_idx]); //TURN THIS ON TO DEBUG
}

#define MNT_SYSTEM 	0
#define MNT_DATA	1
#define MNT_CACHE	2
#define MNT_EMMC	3
#define MNT_SDCARD	4
#define MNT_SDEXT	5
#define MNT_BACK	6

char* isMounted(int mInt)
{
    char* volName = NULL;
    switcH (mInt)
    {
    case MNT_SYSTEM:
        volName = "/system";
        break;
    case MNT_DATA:
        volName = "/data";
        break;
    case MNT_CACHE:
        volName = "/cache";
        break;
    case MNT_EMMC:
        volName = "/emmc";
        break;
    case MNT_SDCARD:
        volName = "/sdcard";
        break;
    case MNT_SDEXT:
        volName = "/sd-ext";
        break;
    default:
        break;
    }

    IVolume* volume = volName ? VolumeManager::findVolume(volName) : NULL;

    char* tmp_set = malloc(20);
    if (!volume)
    {
        strcpy(tmp_set, "------- ");
        strcat(tmp_set, volName);
    }
    else if (volume->isMounted())
    {
        strcpy(tmp_set, "unmount ");
        strcat(tmp_set, volName);
    }
    else
    {
        strcpy(tmp_set, "mount ");
        strcat(tmp_set, volName);
    }
	return tmp_set;
}

void mount_menu(int pIdx)
{
	static char* MENU_MNT_HEADERS[] = { (char*) "Mount Menu",
										(char*) "Pick a Partition to Mount:",
										NULL };
	
	char* MENU_MNT[] = { isMounted(MNT_SYSTEM),
						 isMounted(MNT_DATA),
						 isMounted(MNT_CACHE),
                         isMounted(MNT_EMMC),
						 isMounted(MNT_SDCARD),
						 isMounted(MNT_SDEXT),
						 "<-- Back To Main Menu",
						 NULL };
	
	char** headers = prepend_title((const char**)MENU_MNT_HEADERS);
    IVolume* volume;
	
	inc_menu_loc(MNT_BACK);
	for (;;)
	{
		int chosen_item = get_menu_selection(headers, MENU_MNT, 0, pIdx);
		pIdx = chosen_item;
		switch (chosen_item)
		{
        case MNT_SYSTEM:
            volume = VolumeManager::findVolume("/system");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/system has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/system has been mounted.\n");
            }
			break;
		case MNT_DATA:
            volume = VolumeManager::findVolume("/data");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/data has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/data has been mounted.\n");
            }
			break;
		case MNT_CACHE:
            volume = VolumeManager::findVolume("/cache");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/cache has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/cache has been mounted.\n");
            }
			break; 
        case MNT_EMMC:
            volume = VolumeManager::findVolume("/emmc");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/emmc has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/emmc has been mounted.\n");
            }
            break; 
		case MNT_SDCARD:
            volume = VolumeManager::findVolume("/sdcard");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/emmc has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/emmc has been mounted.\n");
            }
            break; 
		case MNT_SDEXT:
            volume = VolumeManager::findVolume("/sd-ext");
            if (volume && volume->isMounted())
            {
                volume->unmount();
                ui_print("/emmc has been unmounted.\n");
            }
            else if (volume)
            {
                volume->mount();
                ui_print("/emmc has been mounted.\n");
            }
			break;
		case MNT_BACK:
			dec_menu_loc();
			return;
		}
	    break;
	}
	ui_end_menu();
    dec_menu_loc();
    mount_menu(pIdx);
}

void main_wipe_menu()
{
	#define MAIN_WIPE_CACHE           0
	#define MAIN_WIPE_DALVIK   	  	  1
	#define MAIN_WIPE_DATA            2
	#define ITEM_BATTERY_STATS     	  3
	#define ITEM_ROTATE_DATA       	  4
	#define MAIN_WIPE_BACK            5

    static char* MENU_MAIN_WIPE_HEADERS[] = { (char*) "Wipe Menu",
    										  (char*) "Wipe Front to Back:",
                                              NULL };
    
	char* MENU_MAIN_WIPE[] = { (char*) "Wipe Cache",
	                           (char*) "Wipe Dalvik Cache",
	                           (char*) "Wipe Everything (Data Factory Reset)",
		                       (char*) "Wipe Battery Stats",
		                       (char*) "Wipe Rotation Data",
	                           (char*) "<-- Back To Main Menu",
	                           NULL };

    char** headers = prepend_title((const char**)MENU_MAIN_WIPE_HEADERS);

    int isSdExt = 0;

    IVolume* sdext = VolumeManager::findVolume("/sd-ext");
    if (sdext && sdext->isMountable())
    {
        sdext->mount();
    	isSdExt = 1;
    }
    
    inc_menu_loc(MAIN_WIPE_BACK);
    for (;;)
    {
	ui_set_background(BACKGROUND_ICON_WIPE_CHOOSE);
        int chosen_item = get_menu_selection(headers, MENU_MAIN_WIPE, 0, 0);
        switch (chosen_item)
        {
            case MAIN_WIPE_DALVIK:
                wipe_dalvik_cache();
                break;

            case MAIN_WIPE_CACHE:
            	ui_set_background(BACKGROUND_ICON_WIPE);
                ui_print("\n-- Wiping Cache Partition...\n");
                erase_volume("/cache");
                ui_print("-- Cache Partition Wipe Complete!\n");
                ui_set_background(BACKGROUND_ICON_WIPE_CHOOSE);
                if (!ui_text_visible()) return;
                break;

            case MAIN_WIPE_DATA:
                wipe_data(ui_text_visible());
                ui_set_background(BACKGROUND_ICON_WIPE_CHOOSE);
                if (!ui_text_visible()) return;
                break;

            case ITEM_BATTERY_STATS:
                wipe_battery_stats();
                break;

            case ITEM_ROTATE_DATA:
                wipe_rotate_data();
                break;
                
            case MAIN_WIPE_BACK:
            	dec_menu_loc();
                ui_set_background(BACKGROUND_ICON_MAIN);
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
                ui_set_background(BACKGROUND_ICON_MAIN);
	        return;
	    }
        break;
    }
	ui_end_menu();
        dec_menu_loc();
        ui_set_background(BACKGROUND_ICON_MAIN);
	main_wipe_menu();
}

char* toggle_spam()
{
	char* tmp_set = (char*)malloc(50);
	
	switch (DataManager::GetIntValue(TW_SHOW_SPAM_VAR))
	{
		case 0:
			strcpy(tmp_set, "[o] No Filename Display (Fastest)");
			break;
		case 1:
			strcpy(tmp_set, "[s] Single Line Filename Display (Slower)");
			break;
		case 2:
			strcpy(tmp_set, "[m] Multiple Line Filename Display (Slower)");
			break;
	}
	return tmp_set;
}

void all_settings_menu(int pIdx)
{
	// ALL SETTINGS MENU (ALLS for ALL Settings)
	#define ALLS_SIG_TOGGLE             0
	#define ALLS_REBOOT_AFTER_FLASH     1
	#define ALLS_SPAM				    2
    #define ALLS_FORCE_MD5_CHECK        3
	#define ALLS_SORT_BY_DATE           4
    #define ALLS_RM_RF                  5
    #define ALLS_TIME_ZONE              6
	#define ALLS_ZIP_LOCATION   	    7
	#define ALLS_THEMES                 8
	#define ALLS_DEFAULT                9
	#define ALLS_MENU_BACK              10

    static char* MENU_ALLS_HEADERS[] = { (char*) "Change twrp Settings",
    									 (char*) "twrp or gtfo:",
                                         NULL };
    
	char* MENU_ALLS[] =     { zip_verify(),
	                          reboot_after_flash(),
	                          toggle_spam(),
                              force_md5_check(),
							  sort_by_date_option(),
							  rm_rf_option(),
	                          (char*) "Change Time Zone",
	                          (char*) "Change Zip Default Folder",
	                          (char*) "Change twrp Color Theme",
	                          (char*) "Reset Settings to Defaults",
	                          (char*) "<-- Back To Advanced Menu",
	                          NULL };

    char** headers = prepend_title((const char**)MENU_ALLS_HEADERS);
    
    inc_menu_loc(ALLS_MENU_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_ALLS, 0, pIdx);
        pIdx = chosen_item;
        switch (chosen_item)
        {
            case ALLS_SIG_TOGGLE:
            	DataManager::ToggleIntValue(TW_SIGNED_ZIP_VERIFY_VAR);
                break;
			case ALLS_REBOOT_AFTER_FLASH:
                DataManager::ToggleIntValue(TW_REBOOT_AFTER_FLASH_VAR);
                break;
            case ALLS_FORCE_MD5_CHECK:
                DataManager::ToggleIntValue(TW_FORCE_MD5_CHECK_VAR);
                break;
			case ALLS_SORT_BY_DATE:
                DataManager::ToggleIntValue(TW_SORT_FILES_BY_DATE_VAR);
                break;
			case ALLS_RM_RF:
				DataManager::ToggleIntValue(TW_RM_RF_VAR);
				break;
			case ALLS_SPAM:
				switch (DataManager::GetIntValue(TW_SHOW_SPAM_VAR))
				{
					case 0:
						DataManager::SetValue(TW_SHOW_SPAM_VAR, 1);
						break;
					case 1:
						DataManager::SetValue(TW_SHOW_SPAM_VAR, 2);
						break;
					case 2:
						DataManager::SetValue(TW_SHOW_SPAM_VAR, 0);
						break;
				}
				break;
			case ALLS_THEMES:
			    twrp_themes_menu();
				break;
			case ALLS_TIME_ZONE:
			    time_zone_menu();
				break;
            case ALLS_ZIP_LOCATION:
            	get_new_zip_dir = 1;
            	sdcard_directory(SDCARD_ROOT);
            	get_new_zip_dir = 0;
                break;
			case ALLS_DEFAULT:
				DataManager::ResetDefaults();
				break;
            case ALLS_MENU_BACK:
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
        break;
    }
	ui_end_menu();
    dec_menu_loc();
	all_settings_menu(pIdx);
}

/*
    Checks md5 for a path
    Return values:
        -3 : Different file name in MD5 than provided
        -2 : Zip does not exist
        -1 : MD5 does not exist
        0 : Failed
        1 : Success
*/
int check_md5(char* path) {
    char cmd[PATH_MAX + 30];
    sprintf(cmd, "/sbin/md5check.sh \"%s\"", path);
    
    //ui_print("\nMD5 Command: %s", cmd);
    
    FILE * cs = __popen(cmd, "r");
    char cs_s[7];
    fgets(cs_s, 7, cs);

    //ui_print("\nMD5 Message: %s", cs_);
    __pclose(cs);
    
    int o = -99;
    if (strncmp(cs_s, "OK", 2) == 0)
        o = 1;
    else if (strncmp(cs_s, "FAILURE", 7) == 0)
        o = 0;
    else if (strncmp(cs_s, "NO5", 3) == 0)
        o = -1;
    else if (strncmp(cs_s, "NOZ", 3) == 0)
        o = -2;
    else if (strncmp(cs_s, "DF", 2) == 0)
        o = -3;
    else // Unknown error
        o = -99;

    return o;
}

static void
show_menu_partition()
{
// I KNOW THAT this menu seems a bit redundant, but it allows us to display the warning message & we're planning to add ext3 to 4 upgrade option and maybe file system error fixing later
    static char* SDheaders[] = { (char*) "Choose partition item,",
			                     (char*) "",
			                     (char*) "",
			                     NULL };

// these constants correspond to elements of the items[] list.
#define ITEM_PART_SD       0
#define ITEM_PART_BACK     1

    ext_format = 3; // default this to 3
	
	static char* items[] = { (char*) "Partition SD Card",
							 (char*) "<-- Back to Advanced Menu",
                             NULL };

    char** headers = prepend_title((const char**)SDheaders);
	
	ui_print("\nBack up your sdcard before partitioning!\nAll files will be lost!\n");
    
    inc_menu_loc(ITEM_PART_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, items, 0, 0);
        switch (chosen_item)
        {
			case ITEM_PART_SD:
				swapSz = 32;
				ui_print("\n");
				choose_swap_size(1);
				if (swapSz == -1) break; // swap size was cancelled, abort
				
				ext = 512;
				ui_print("\n");
				choose_ext_size(1);
				if (ext == -1) break; // ext size was cancelled, abort
				
                // Below seen in Koush's recovery
                char sddevice[256];
                Volume *vol = volume_for_path("/sdcard");
                strcpy(sddevice, vol->device);
                // Just need block not whole partition
                sddevice[strlen("/dev/block/mmcblkX")] = NULL;

				char es[64];
				sprintf(es, "/sbin/sdparted -es %dM -ss %dM -efs ext%i -s > /cache/part.log",ext,swapSz,ext_format);
				LOGI("\nrunning script: %s\n", es);
				run_script((char*) "\nContinue partitioning?",
                           (char*) "\nPartitioning sdcard : ",
                           es,
                           (char*) "\nunable to execute parted!\n(%s)\n",
                           (char*) "\nOops... something went wrong!\nPlease check the recovery log!\n",
                           (char*) "\nPartitioning complete!\n\n",
                           (char*) "\nPartitioning aborted!\n\n", -1);
				
				// recreate TWRP folder and rewrite settings - these will be gone after sdcard is partitioned
				ensure_path_mounted(SDCARD_ROOT);
				mkdir("/sdcard/TWRP", 0777);
				DataManager::Flush();
				break;
			case ITEM_PART_BACK:
				dec_menu_loc();
				return;
        } // end switch
	} // end for
    if (go_home) { 
		dec_menu_loc();
		return;
	}
}

void choose_swap_size(int pIdx) {
	#define SWAP_SET                0
	#define SWAP_INCREASE           1
	#define SWAP_DECREASE           2
	#define SWAP_BACK               3

    static char* MENU_SWAP_HEADERS[] = { (char*) "Swap Partition",
    								     (char*) "Please select size for swap partition:",
                                         NULL };
    
	char* MENU_SWAP[] =        { (char*) "Save Swap Size",
								 (char*) "Increase",
							     (char*) "Decrease",
	                             (char*) "<-- Back To SD Card Partition, Cancel",
	                             NULL };
	
    char** headers = prepend_title((const char**)MENU_SWAP_HEADERS);
    
	sprintf(swapsize, "%4d", swapSz);
	ui_print_overwrite("Swap-size  = %s MB",swapsize);
	if (swapSz == 0) ui_print(" - NONE");
	
    inc_menu_loc(SWAP_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_SWAP, 0, pIdx);
		pIdx = chosen_item;
        switch (chosen_item)
        {
			case SWAP_SET:
				sprintf(swapsize, "%4d", swapSz);
				dec_menu_loc();
				return;
            case SWAP_INCREASE:
            	swapSz = swapSz + 32;
				
                break;
            case SWAP_DECREASE:
				swapSz = swapSz - 32;
				if (swapSz < 0) swapSz = 0;
                break;
            case SWAP_BACK:
				swapSz = -1;
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
		break;
    }
	ui_end_menu();
	dec_menu_loc();
	choose_swap_size(pIdx);
}

char* ext_format_menu_option()
{
	char* tmp_set = (char*)malloc(40);
	strcpy(tmp_set, "[3] SD-EXT File System - EXT3");
	if (ext_format == 4) {
		tmp_set[1] = '4';
		tmp_set[28] = '4';
	}
	return tmp_set;
}

void choose_ext_size(int pIdx) {
	#define EXT_SET                0
	#define EXT_INCREASE           1
	#define EXT_DECREASE           2
	#define EXT_EXT_FORMAT         3
	#define EXT_BACK               4

    static char* MENU_EXT_HEADERS[] = { (char*) "EXT Partition",
    								    (char*) "Please select size for EXT partition:",
                                        NULL };
    
	char* MENU_EXT[] =         { (char*) "Save EXT Size & Commit Changes",
								 (char*) "Increase",
							     (char*) "Decrease",
								 ext_format_menu_option(),
	                             (char*) "<-- Back To SD Card Partition, Cancel",
	                             NULL };
	
    char** headers = prepend_title((const char**)MENU_EXT_HEADERS);
    
	sprintf(extsize, "%4d", ext);
	ui_print_overwrite("EXT-size  = %s MB",extsize);
	if (ext == 0) ui_print(" - NONE");
	
    inc_menu_loc(EXT_BACK);
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, MENU_EXT, 0, pIdx);
		pIdx = chosen_item;
        switch (chosen_item)
        {
			case EXT_SET:
				sprintf(extsize, "%4d", ext);
				dec_menu_loc();
				return;
            case EXT_INCREASE:
            	ext = ext + 128;
                break;
            case EXT_DECREASE:
				ext = ext - 128;
				if (ext < 0) ext = 0;
                break;
			case EXT_EXT_FORMAT:
				if (ext_format == 3) {
					ext_format = 4;
				} else {
					ext_format = 3;
				}
				break;
            case EXT_BACK:
				ext = -1;
            	dec_menu_loc();
            	return;
        }
	    if (go_home) { 
	        dec_menu_loc();
	        return;
	    }
		break;
    }
	ui_end_menu();
	dec_menu_loc();
	choose_ext_size(pIdx);
}

void run_script(char *str1,char *str2,char *str3,char *str4,char *str5,char *str6,char *str7, int request_confirm)
{
	ui_print("%s", str1);
        ui_clear_key_queue();
	ui_print("\nPress Power to confirm,");
       	ui_print("\nany other key to abort.\n");
	int confirm;
	if (request_confirm) // this option is used to skip the confirmation when the gui is in use
		confirm = ui_wait_key();
	else
		confirm = KEY_POWER;
	
		if (confirm == BTN_MOUSE || confirm == KEY_POWER || confirm == SELECT_ITEM) {
                	ui_print("%s", str2);
		        pid_t pid = fork();
                	if (pid == 0) {
                		char *args[] = { (char*) "/sbin/sh", (char*) "-c", str3, (char*) "1>&2", NULL };
                	        execv("/sbin/sh", args);
                	        fprintf(stderr, str4, strerror(errno));
                	        _exit(-1);
                	}
			int status;
			while (waitpid(pid, &status, WNOHANG) == 0) {
				ui_print(".");
               		        sleep(1);
			}
                	ui_print("\n");
			if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
                		ui_print("%s", str5);
                	} else {
                		ui_print("%s", str6);
                	}
		} else {
	       		ui_print("%s", str7);
       	        }
		if (!ui_text_visible()) return;
}
