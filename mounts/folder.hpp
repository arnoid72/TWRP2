// folder.hpp - Basic interfaces for folder-based volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _FOLDER_HPP
#define _FOLDER_HPP

#include "mounts.hpp"

class FolderVolume : public IVolume
{
public:
    FolderVolume(IVolume* baseVolume, string path, string label);
    virtual ~FolderVolume();

public:
    string getVolumeDisplayName()           { return mLabel; }
    string getVolumeLabel()                 { return mLabel; }
    string getVolumeMemoryType()            { return mBaseVolume->getVolumeMemoryType(); }

    eBackupMethod getVolumeBackupMethod()   { return backup_files; }
    eFSType getVolumeFilesystemType()       { return mBaseVolume->getVolumeFilesystemType(); }
    string getMountPoint()                  { return mMountPoint; }
    bool isMounted()                        { return mBaseVolume->isMounted(); }
    bool isMountable()                      { return mBaseVolume->isMountable(); }
    string getBlockDevice()                 { return ""; }
    string getMemoryDevice()                { return ""; }
    
    int mount()                             { return mBaseVolume->mount(); }
    int unmount()                           { return mBaseVolume->unmount(); }

    long long getVolumeSize()               { return mBaseVolume->getVolumeSize(); }
    long long getVolumeUsed()               { return -1; }

    int format(eFSType newType = fs_unknown);

    int backup(string destFile);
    int restore(string backupFile);

protected:
    IVolume*        mBaseVolume;
    string          mLabel;
    string          mMountPoint;
};

#endif  // _FOLDER_HEADER

