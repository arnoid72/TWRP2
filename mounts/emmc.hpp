// emmc.hpp - Basic interfaces for EMMC volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _EMMC_HPP
#define _EMMC_HPP

#include "mounts.hpp"

class EMMCVolume : public IVolume
{
public:
    EMMCVolume(string device);
    virtual ~EMMCVolume();

public:
    string getVolumeDisplayName()           { return mDisplayName; }
    string getVolumeLabel()                 { return mLabel; }
    string getVolumeMemoryType()            { return "emmc"; }

    eBackupMethod getVolumeBackupMethod()   { return mBackupMethod; }
    eFSType getVolumeFilesystemType()       { return mFSType; }
    string getMountPoint()                  { return mMountPoint; }
    bool isMounted();
    bool isMountable()                      { return mIsMountable; }
    string getBlockDevice()                 { return mDevice; }
    string getMemoryDevice()                { return mDevice; }
    
    int mount();
    int unmount();

    long long getVolumeSize()               { return mVolumeSize; }
    long long getVolumeUsed();

    int format(eFSType newType = fs_unknown);

    int backup(string destFile);
    int restore(string backupFile);

protected:
    string          mDisplayName;
    string          mLabel;
    eBackupMethod   mBackupMethod;
    eFSType         mFSType;
    string          mMountPoint;
    bool            mIsMountable;
    string          mDevice;
    long long       mVolumeSize;
};

#endif  // _EMMC_HEADER

