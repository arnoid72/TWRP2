// Mounts.cpp - Handler for all mount points
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <dirent.h>

#include "mounts.hpp"
#include "utility.hpp"
#include "emmc.hpp"
#include "generic.hpp"
#include "folder.hpp"
#include "fstab.hpp"

vector<IVolume*> VolumeManager::mVolumes;
FSTab* VolumeManager::mFsTab = NULL;

bool IVolume::operator==(string name)
{
    if (name.empty())                       return false;

    if (getVolumeDisplayName() == name)     return true;
    if (getVolumeLabel() == name)           return true;
    if (getMountPoint() == name)            return true;
    if (getBlockDevice() == name)           return true;
    if (getMemoryDevice() == name)          return true;
    return false;
}

int VolumeManager::loadVolumes()
{
    fprintf(stderr, "VolumeManager::loadVolumes started.\n");

    // Refresh the FS type cache
    Utility::getDeviceFSType("ignored", false);

    // We need to clean the array first
    vector<IVolume*>::iterator iter;
    for (iter = mVolumes.begin(); iter < mVolumes.end(); iter++)
    {
        delete (*iter);
    }
    mVolumes.clear();

    fprintf(stderr, "All volumes cleared\n");

    // Now, rebuild the arrays
    DIR* d;
    d = opendir("/dev/block");
    if (d == NULL)      return -1;

    struct dirent* de;
    while ((de = readdir(d)) != NULL)
    {
        // We only process block devices
        if (de->d_type != DT_BLK)   continue;

        string name = "/dev/block/";
        name += de->d_name;

        fprintf(stderr, " Processing: %s\n", name.c_str());

        if (memcmp(de->d_name, "mtd", 3) == 0)
        {
            // We found an MTD device
            // TODO
        }
        if (memcmp(de->d_name, "mmcblk", 3) == 0)
        {
            fprintf(stderr, "  Creating EMMC volume\n");
            // We found an EMMC device
            IVolume* volume = (IVolume*) new EMMCVolume(name);
            if (volume->isMountable() == false && 
                (volume->getVolumeBackupMethod() == backup_none || volume->getVolumeBackupMethod() == backup_unknown))
            {
                delete volume;
            }
            else
                mVolumes.push_back(volume);
        }
        if (memcmp(de->d_name, "cyasblk", 7) == 0)
        {
            fprintf(stderr, "  Creating Generic volume\n");
            // We found a generic device
            IVolume* volume = (IVolume*) new GenericVolume(name);
            mVolumes.push_back(volume);
        }
    }
    closedir(d);

    fprintf(stderr, "  Locating /data\n");

    // Now, as a special check, if we don't already have an emmc, we'll check for /data/media
    IVolume* volume = findVolume("/data");
    if (volume && findVolume("emmc") == NULL)
    {
        fprintf(stderr, "  Mounting /data\n");
        volume->mount();
        struct stat st;
        if (stat("/data/media", &st) == 0)
        {
            fprintf(stderr, "  Found /data/media\n");
            // We have an EMMC partition embedded in the data segment
            IVolume* newVol = (IVolume*) new FolderVolume(volume, "/data/media", "emmc");
            mVolumes.push_back(newVol);
        }
    }

    fprintf(stderr, "+----------+----------+------+-----------+--------+---+---+----------+----------+\n");
    fprintf(stderr, "|   Name   |  Label   | mem  |  backup   | fstype | M | A | Size(KB) | Used(KB) |\n");
    fprintf(stderr, "+----------+----------+------+-----------+--------+---+---+----------+----------+\n");
    fprintf(stderr, "|   Mount Point       | Block Device              | Memory Device               |\n");
    fprintf(stderr, "+---------------------+---------------------------+-----------------------------+\n");

    // Finally, let's create the /etc/fstab
    FSTab tab("/etc/recovery.fstab");
    for (iter = mVolumes.begin(); iter < mVolumes.end(); iter++)
    {
        FSTabEntry* entry = tab.GetFsTabEntry((*iter)->getMountPoint());

        // While we're here, let's dump the table details to stderr
        fprintf(stderr, "| %8.8s | %8.8s | %4.4s | %9.9s | %6.6s | %c | %c | %8.8lld | %8.8lld |\n", 
                (*iter)->getVolumeDisplayName().c_str(), 
                (*iter)->getVolumeLabel().c_str(), 
                (*iter)->getVolumeMemoryType().c_str(), 
                Utility::getBackupMethodStr((*iter)->getVolumeBackupMethod()).c_str(), 
                Utility::getFsTypeStr((*iter)->getVolumeFilesystemType()).c_str(), 
                (*iter)->isMounted() ? 'y' : 'n', 
                (*iter)->isMountable() ? 'y' : 'n', 
                (*iter)->getVolumeSize() > 0 ? (*iter)->getVolumeSize() / 1024 : 0, 
                (*iter)->getVolumeUsed() > 0 ? (*iter)->getVolumeUsed() / 1024 : 0);
        fprintf(stderr, "| %19.19s | %25.25s | %27.27s |\n", 
                (*iter)->getMountPoint().c_str(), 
                (*iter)->getBlockDevice().c_str(), 
                (*iter)->getMemoryDevice().c_str());

        fprintf(stderr, "+-------------------------------------------------------------------------------+\n");

        if (entry)
        {
            entry->setFsType(Utility::getFsTypeStr((*iter)->getVolumeFilesystemType()));
            entry->setMountable((*iter)->isMountable());

            if ((*iter)->isMountable())
                mkdir((*iter)->getMountPoint().c_str(), 0777);
        }
    }
    tab.WriteFstab("/etc/fstab");
    return 0;
}

IVolume* VolumeManager::findVolume(string name)
{
    vector<IVolume*>::iterator iter;

    // Because we could receive a symbolic link, we'll reduce to the raw device
    name = Utility::readlink_recursive(name);

    // This is to ensure we've at least attempted to load the volumes
    if (mVolumes.size() == 0)   loadVolumes();

    for (iter = mVolumes.begin(); iter < mVolumes.end(); iter++)
    {
        if (**iter == name)      return (*iter);
    }
    return NULL;
}

// getRecoveryFsTab - Retrieve the recovery.fstab class
FSTab* VolumeManager::getRecoveryFsTab()
{
    if (!mFsTab)    mFsTab = new FSTab("/etc/recovery.fstab");
    return mFsTab;
}

extern "C" int loadVolumes(void)
{
    return VolumeManager::loadVolumes();
}

