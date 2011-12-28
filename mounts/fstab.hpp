// fstab.hpp - Basic interfaces for fstab and recovery.fstab
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _FSTAB_HPP
#define _FSTAB_HPP

#include <vector>
#include <string>

using namespace std;

class FSTabEntry
{
public:
    FSTabEntry(const string mountPoint, const string fstype, const string device, const bool bMountable);

public:
    string getMountPoint()          { return mMountPoint; }
    string getFsType()              { return mFsType; }
    string getDevice()              { return mDevice; }
    bool isMountable()              { return mMountable; }

    void setFsType(string fsType)   { mFsType = fsType; }
    void setMountable(bool bMount)  { mMountable = bMount; }

protected:
    string mMountPoint;
    string mFsType;
    string mDevice;
    bool mMountable;
};

class FSTab
{
public:
    FSTab(string file);

public:
    FSTabEntry* GetFsTabEntry(const string name);
    bool WriteFstab(string file);

protected:
    vector<FSTabEntry> mEntries;
};

#endif  // _FSTAB_HPP

