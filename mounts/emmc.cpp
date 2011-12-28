// emmc.cpp - Handler for EMMC devices
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "emmc.hpp"
#include "utility.hpp"
#include "fstab.hpp"
#include "mounts.hpp"

EMMCVolume::EMMCVolume(string device)
{
    // We're going to reduce any device link to it's raw device
    device = Utility::readlink_recursive(device);

    fprintf(stderr, "Creating emmc volume: %s\n", device.c_str());

    // Our best way to get details is /proc/emmc
    FILE* fp = fopen("/proc/emmc", "rt");
    if (fp)
    {
        fprintf(stderr, "Using /proc/emmc\n");

        char line[255];
        while (fgets(line, sizeof(line), fp) != NULL)
        {
            char dev[32], label[32];
            unsigned long size = 0;

            sscanf(line, "%s %lx %*s %*c%s", dev, &size, label);

            // Skip header and blank lines
            if ((strcmp(dev, "dev:") == 0) || (strlen(line) < 8))
                continue;

            // Strip off the : and " from the details
            dev[strlen(dev)-1] = '\0';
            label[strlen(label)-1] = '\0';

            // Now, let's see if this is the correct device
            if (device.find(dev) == string::npos)   continue;

            mDisplayName = label;
            mLabel = label;
            mDevice = device;
            mVolumeSize = (long long) size;
            break;
        }
        fclose(fp);
    }

    FSTabEntry* entry = VolumeManager::getRecoveryFsTab()->GetFsTabEntry(device);
    fprintf(stderr, "Entry %s\n", entry ? "found" : "not found");

    if (mDisplayName.empty() && entry == NULL)
    {
        // We can't find the device... So we create a dead node
        mDisplayName = device;
        mLabel = device;
        mBackupMethod = backup_none;
        mFSType = fs_unknown;
        mMountPoint.clear();
        mIsMountable = false;
        mDevice = device;
        mVolumeSize = 0;
        return;
    }

    if (entry != NULL)
    {
        if (mDisplayName.empty())
        {
            mDisplayName = entry->getMountPoint().substr(1);     // Drop the forward-slash
            mLabel = mDisplayName;
            mDevice = device;
            fprintf(stderr, "Getting volume size of partition\n");
            mVolumeSize = Utility::getVolumeSizeOfPartition(device);
        }
        mMountPoint = entry->getMountPoint();
    }

    fprintf(stderr, "Getting fstype\n");
    mFSType = Utility::getDeviceFSType(mDevice);

    // We never let boot be a file backup
    switch (mFSType)
    {
    case fs_vfat:
    case fs_ext2:
    case fs_ext3:
    case fs_ext4:
    case fs_yaffs2:
        mIsMountable = true;
        mBackupMethod = backup_files;
        break;
    case fs_mtd:
        mIsMountable = false;
        mBackupMethod = backup_image;
    default:
        mIsMountable = false;
        mBackupMethod = backup_none;
        break;
    }

    // Handle the cases where there isn't a mount point
    if (mMountPoint.empty())
    {
        mIsMountable = false;
        mBackupMethod = backup_image;
    }

    // Override backup method for boot
    if (mLabel == "boot")   mBackupMethod = backup_image;

    if (mLabel == EXPAND(SP1_NAME))
    {
        mDisplayName = EXPAND(SP1_DISPLAY_NAME);
        mBackupMethod = Utility::getBackupMethodFromStr(EXPAND(SP1_BACKUP_METHOD));
        mIsMountable = SP1_MOUNTABLE ? true : false;
    }
    if (mLabel == EXPAND(SP2_NAME))
    {
        mDisplayName = EXPAND(SP2_DISPLAY_NAME);
        mBackupMethod = Utility::getBackupMethodFromStr(EXPAND(SP2_BACKUP_METHOD));
        mIsMountable = SP2_MOUNTABLE ? true : false;
    }
    if (mLabel == EXPAND(SP3_NAME))
    {
        mDisplayName = EXPAND(SP3_DISPLAY_NAME);
        mBackupMethod = Utility::getBackupMethodFromStr(EXPAND(SP3_BACKUP_METHOD));
        mIsMountable = SP3_MOUNTABLE ? true : false;
    }
    fprintf(stderr, "Done\n");
}

EMMCVolume::~EMMCVolume()
{
}

bool EMMCVolume::isMounted()
{
    return mIsMountable ? Utility::isMounted(mMountPoint) : false;
}

int EMMCVolume::mount()
{
    if (!mIsMountable)  return -1;
    return (isMounted() ? 0 : Utility::mount(mDevice, mMountPoint, mFSType));
}

int EMMCVolume::unmount()
{
    if (!mIsMountable)  return -1;
    return (isMounted() ? Utility::unmount(mMountPoint) : 0);
}

long long EMMCVolume::getVolumeUsed()
{
    struct statfs st;

    if (mount())        return -1;

    string file = mMountPoint + "/.";
    if (statfs(file.c_str(), &st) != 0)
        return -1;

    return ((st.f_blocks - st.f_bfree) * st.f_bsize);
}

int EMMCVolume::format(eFSType newType /* = unknown */)
{
    // For now, we don't support changing the fstype
    if (newType != fs_unknown && newType != mFSType)   return -1;

    if (mMountPoint == "/data" && Utility::fileExists("/data/media"))
    {
        if (mount())    return -1;
        return Utility::rm_folder_safe(mMountPoint);
    }
        
    switch (mFSType)
    {
    case fs_ext2:
    case fs_ext3:
    case fs_ext4:
        return Utility::format_ext(mFSType, mDevice);
    default:
        if (mount())    return -1;
        return Utility::rm_folder_safe(mMountPoint);
    }
}

int EMMCVolume::backup(string destFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

int EMMCVolume::restore(string backupFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

