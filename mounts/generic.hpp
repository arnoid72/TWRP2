// generic.hpp - Basic interfaces for generic volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _GENERIC_HPP
#define _GENERIC_HPP

#include "mounts.hpp"

class GenericVolume : public IVolume
{
public:
    GenericVolume(string device);
    virtual ~GenericVolume();

public:
    string getVolumeDisplayName()           { return mDisplayName; }
    string getVolumeLabel()                 { return mLabel; }
    string getVolumeMemoryType()            { return "img"; }

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

#endif  // _GENERIC_HEADER

