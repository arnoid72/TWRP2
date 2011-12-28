// Utility.cpp - Simple utility functions for volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>

#include <map>

#include "utility.hpp"
#include "../data.hpp"

string Utility::getFsTypeStr(eFSType type)
{
    switch (type)
    {
    case fs_vfat:       return "vfat";
    case fs_ext2:       return "ext2";
    case fs_ext3:       return "ext3";
    case fs_ext4:       return "ext4";
    case fs_yaffs2:     return "yaffs2";
    case fs_mtd:        return "mtd";
    default:            return "unknown";
    }
}

eFSType Utility::getFsTypeFromStr(string type)
{
    if (type == "vfat")     return fs_vfat;
    if (type == "ext2")     return fs_ext2;
    if (type == "ext3")     return fs_ext3;
    if (type == "ext4")     return fs_ext4;
    if (type == "yaffs2")   return fs_yaffs2;
    if (type == "mtd")      return fs_mtd;
    return fs_unknown;
}

string Utility::getBackupMethodStr(eBackupMethod method)
{
    switch (method)
    {
    case backup_image:      return "image";
    case backup_files:      return "files";
    case backup_none:       return "none";
    default:                return "unknown";
    }
}

eBackupMethod Utility::getBackupMethodFromStr(string method)
{
    if (method == "image")  return backup_image;
    if (method == "files")  return backup_files;
    if (method == "none")   return backup_none;
    return backup_unknown;
}

bool Utility::isMounted(string mountpoint)
{
    if (mountpoint.empty() || mountpoint.size() > 200)     return false;

    struct stat st1, st2;
    string path;

    path = "/" + mountpoint + "/.";
    if (stat(path.c_str(), &st1) != 0)  return false;

    path = "/" + mountpoint + "/../.";
    if (stat(path.c_str(), &st2) != 0)  return false;

    return (st1.st_dev != st2.st_dev) ? 1 : 0;
}

int Utility::mount(string device, string mountpoint, eFSType type)
{
    if (device.empty() || mountpoint.empty())       return -1;
    if (isMounted(mountpoint))                      return 0;

    if (::mount(device.c_str(), mountpoint.c_str(), getFsTypeStr(type).c_str(), 0, NULL) != 0)
        return -1;

    return 0;
}

int Utility::unmount(string mountpoint)
{
    if (mountpoint.empty())                 return -1;
    if (!isMounted(mountpoint))             return 0;

    if (umount(mountpoint.c_str()) != 0)    return -1;
    return 0;
}

bool Utility::fileExists(string filename)
{
    struct stat st;

    if (stat(filename.c_str(), &st) == 0)  return true;
    return false;
}

int Utility::rm_folder(string path)
{
    DIR* d;
    d = opendir(path.c_str());
    if (d != NULL)
    {
        struct dirent* de;
        while ((de = readdir(d)) != NULL)
        {
            // Ignore . and ..
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)    continue;

            string fullName = path + "/" + de->d_name;

            if (de->d_type == DT_DIR)
            {
                rm_folder_safe(fullName);
                rmdir(fullName.c_str());
            }
            else
                unlink(fullName.c_str());
        }
        closedir(d);
    }
    return 0;
}

int Utility::rm_folder_safe(string path)
{
    // The reason for "safe" is that we won't erase '/data/media'
    DIR* d;
    d = opendir(path.c_str());
    if (d != NULL)
    {
        struct dirent* de;
        while ((de = readdir(d)) != NULL)
        {
            // Ignore . and ..
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)    continue;

            string fullName = path + "/" + de->d_name;

            if (de->d_type == DT_DIR)
            {
                // Make sure this isn't /data/media
                if (fullName != "/data/media")
                {
                    rm_folder_safe(fullName);
                    rmdir(fullName.c_str());
                }
            }
            else
                unlink(fullName.c_str());
        }
        closedir(d);
    }
    return 0;
}

int Utility::format_ext(eFSType fstype, string device)
{
    if (!fileExists("/sbin/mke2fs"))       return -1;

    string exe = "mke2fs -t " + getFsTypeStr(fstype) + "-m 0 " + device;
    __system(exe.c_str());
    return 0;
}

string Utility::readlink_recursive(string filename)
{
    string out = filename;

    // Attempt to flip mounts until we find the block device
    char realDevice[512];
    memset(realDevice, 0, sizeof(realDevice));
    while (readlink(out.c_str(), realDevice, sizeof(realDevice)) > 0)
    {
        out = realDevice;
        memset(realDevice, 0, sizeof(realDevice));
    }
    return out;
}

eFSType Utility::getDeviceFSType(string device, bool cached /* = true */)
{
    static map<string,string> cachedOutput;

    if (!cached || cachedOutput.size() == 0)
    {
        // This routine uses blkid
        FILE* fp = __popen("blkid", "r");
        if (!fp)                                    return fs_unknown;

        char blkOutput[256];
        while (fgets(blkOutput, sizeof(blkOutput), fp) != NULL)
        {
            char* ptr = blkOutput;
            while (*ptr > 0 && *ptr != ':')     ptr++;
            if (*ptr == 0)  continue;
            *ptr = 0;

            ptr++;
            while (strlen(ptr) > 5)
            {
                if (memcmp(ptr, "TYPE=", 5) == 0)
                {
                    // Skip the open quote too
                    ptr += 6;
                    string type = ptr;
                    type = type.substr(0, type.size() - 2);

                    cachedOutput.insert(make_pair(blkOutput, type));
                    break;
                }
                ptr++;
            }
        }
        __pclose(fp);
    }

    map<string,string>::iterator iter = cachedOutput.find(device);
    if (iter == cachedOutput.end())   return fs_unknown;
    return getFsTypeFromStr(iter->second);
}

long long Utility::getVolumeSizeOfPartition(string device)
{
    FILE* fp = fopen("/proc/partitions", "rt");
    if (!fp)        return -1;

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        unsigned long major, minor, blocks;
        char dev[64];
        char tmpString[64];

        if (strlen(line) < 7 || line[0] == 'm')     continue;
        sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, dev);

        // Adjust block size to byte size
        long long size = blocks * 1024LL;
        if (device.find(dev) != string::npos)
        {
            fclose(fp);
            return size;
        }
    }
    fclose(fp);
    return -1;
}

