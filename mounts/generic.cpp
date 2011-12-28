// Generic.cpp - Handler for all generic volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "generic.hpp"
#include "utility.hpp"
#include "fstab.hpp"
#include "mounts.hpp"

GenericVolume::GenericVolume(string device)
{
    // We're going to reduce any device link to it's raw device
    device = Utility::readlink_recursive(device);

    FSTabEntry* entry = VolumeManager::getRecoveryFsTab()->GetFsTabEntry(device);
    if (entry == NULL)
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

    mDisplayName = entry->getMountPoint().substr(1);     // Drop the forward-slash
    mLabel = mDisplayName;
    mDevice = device;
    mVolumeSize = Utility::getVolumeSizeOfPartition(device);
    mMountPoint = entry->getMountPoint();
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
    default:
        mIsMountable = false;
        mBackupMethod = backup_image;
        break;
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
}

GenericVolume::~GenericVolume()
{
}

bool GenericVolume::isMounted()
{
    return mIsMountable ? Utility::isMounted(mMountPoint) : false;
}

int GenericVolume::mount()
{
    if (!mIsMountable)  return -1;
    return (isMounted() ? 0 : Utility::mount(mDevice, mMountPoint, mFSType));
}

int GenericVolume::unmount()
{
    if (!mIsMountable)  return -1;
    return (isMounted() ? Utility::unmount(mMountPoint) : 0);
}

long long GenericVolume::getVolumeUsed()
{
    struct statfs st;

    if (mount())        return -1;

    string file = mMountPoint + "/.";
    if (statfs(file.c_str(), &st) != 0)
        return -1;

    return ((st.f_blocks - st.f_bfree) * st.f_bsize);
}

int GenericVolume::format(eFSType newType /* = unknown */)
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

int GenericVolume::backup(string destFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

int GenericVolume::restore(string backupFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

