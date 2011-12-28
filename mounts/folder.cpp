// Folder.cpp - Handler for all folder volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#include <stdio.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "folder.hpp"
#include "utility.hpp"

FolderVolume::FolderVolume(IVolume* baseVolume, string path, string label)
{
    mBaseVolume = baseVolume;
    mLabel = label;
    mMountPoint = path;
}

FolderVolume::~FolderVolume()
{
}

int FolderVolume::format(eFSType newType /* = unknown */)
{
    // We don't support changing the fstype
    if (newType != fs_unknown && newType != getVolumeFilesystemType())   return -1;

    return Utility::rm_folder(mMountPoint);
}

int FolderVolume::backup(string destFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

int FolderVolume::restore(string backupFile)
{
    // This method is not yet supported in TWRP 2
    return -1;
}

