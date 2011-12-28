// fstab.cpp - Basic interfaces for fstab and recovery.fstab
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "utility.hpp"
#include "fstab.hpp"

FSTabEntry::FSTabEntry(const string mountPoint, 
                       const string fstype, 
                       const string device, 
                       const bool bMountable)
{
    mMountPoint = mountPoint;
    mFsType = fstype;
    mDevice = device;
    mMountable = bMountable;
}

// Verify if a device is actually valid
static bool isDeviceAvailable(string device)
{
    int fd = open(device.c_str(), O_RDONLY);
    if (fd < 0)     return false;
    char block[512];
    ssize_t chk = read(fd, block, 512);
    close(fd);

    return (chk < 512 ? false : true);
}

int get_strings(char* str, char** p1, char** p2, char** p3, char** p4)
{
    if (!str)   return 0;

    while (*str > 0 && *str < 33)   str++;
    *p1 = str;
    while (*str > 32)               str++;
    if (*str == 0)                  return 1;
    *str++ = 0;

    while (*str > 0 && *str < 33)   str++;
    *p2 = str;
    while (*str > 32)               str++;
    if (*str == 0)                  return 2;
    *str++ = 0;

    while (*str > 0 && *str < 33)   str++;
    *p3 = str;
    while (*str > 32)               str++;
    if (*str == 0)                  return 3;
    *str++ = 0;

    if (p4 == NULL)                 return 3;

    while (*str > 0 && *str < 33)   str++;
    *p4 = str;
    while (*str > 32)               str++;
    *str++ = 0;
    return 4;
}

FSTab::FSTab(string file)
{
    FILE* fp = fopen(file.c_str(), "rt");
    if (fp)
    {
        fprintf(stderr, "Loading fstab: %s\n", file.c_str());
        char str[512];
        while (fgets(str, sizeof(str), fp) != NULL)
        {
            if (str[0] == '#' || strlen(str) < 8)   continue;

            // We're going to break apart the strings
            char* ptr = str;
            char* mountPoint;
            char* fsType;
            char* device;
            char* alt_device = NULL;

            if (file.find("recovery.fstab") != string::npos)
            {
                int ret = get_strings(str, &mountPoint, &fsType, &device, &alt_device);
                if (ret < 3)    continue;
                if (ret == 4 && !isDeviceAvailable(device))
                    device = alt_device;
            }
            else
            {
                // File is parsed as device mountpoint fsType [*]
                int ret = get_strings(str, &device, &mountPoint, &fsType, NULL);
                if (ret < 3)    continue;
            }

            fprintf(stderr, "FSTAB: mountPoint='%s'  fsType='%s'  device='%s'\n", mountPoint, fsType, device);
            FSTabEntry entry(mountPoint, fsType, Utility::readlink_recursive(device), false);
            mEntries.push_back(entry);
        }
        fclose(fp);
    }
}

FSTabEntry* FSTab::GetFsTabEntry(const string name)
{
    vector<FSTabEntry>::iterator iter;

    for (iter = mEntries.begin(); iter < mEntries.end(); iter++)
    {
        if ((*iter).getMountPoint() == name)  return &(*iter);
        if ((*iter).getDevice() == name)      return &(*iter);
    }
    return NULL;
}

bool FSTab::WriteFstab(string file)
{
    FILE* fd = fopen(file.c_str(), "wt");
    if (fd == NULL)     return false;

    vector<FSTabEntry>::iterator iter;

    for (iter = mEntries.begin(); iter < mEntries.end(); iter++)
    {
        if ((*iter).isMountable())
            fprintf(fd, "%s %s %s rw\n", (*iter).getDevice().c_str(), (*iter).getMountPoint().c_str(), (*iter).getFsType().c_str());
    }

    fclose(fd);
    return true;
}

