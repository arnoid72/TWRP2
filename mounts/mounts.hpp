// Mounts.hpp - Basic interfaces for volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _MOUNTS_HPP
#define _MOUNTS_HPP

#include <string>
#include <vector>

#include "fstab.hpp"

using namespace std;

enum eBackupMethod {
    backup_unknown = 0, 
    backup_none, 
    backup_image, 
    backup_files,
};

enum eFSType {
    fs_unknown = 0,
    fs_vfat, 
    fs_ext2,
    fs_ext3,
    fs_ext4,
    fs_yaffs2,
    fs_mtd,
};

class IVolume
{
public:
    virtual ~IVolume()                                  { }

public:
    virtual string getVolumeDisplayName() = 0;
    virtual string getVolumeLabel() = 0;
    virtual string getVolumeMemoryType() = 0;

    virtual eBackupMethod getVolumeBackupMethod()       { return backup_unknown; }
    virtual eFSType getVolumeFilesystemType()           { return fs_unknown; }
    virtual string getMountPoint()                      { return ""; }
    virtual bool isMounted()                            { return false; }
    virtual bool isMountable()                          { return false; }
    virtual string getBlockDevice()                     { return ""; }
    virtual string getMemoryDevice()                    { return ""; }
    
    virtual int mount()                                 { return -1; }
    virtual int unmount()                               { return -1; }

    virtual long long getVolumeSize()                   { return -1; }
    virtual long long getVolumeUsed()                   { return -1; }

    virtual int format(eFSType newType = fs_unknown)    { return -1; }

    virtual int backup(string destFile)                 { return -1; }
    virtual int restore(string backupFile)              { return -1; }

public:
    // This operator overload must recognize 
    bool operator==(string name);
};

class VolumeManager
{
public:
    // loadVolumes - Rebuild the volume table
    static int loadVolumes();

    // findVolume - Locate a volume by label, mount point, device, etc...
    static IVolume* findVolume(string name);

    // getRecoveryFsTab - Retrieve the recovery.fstab class
    static FSTab* getRecoveryFsTab();

protected:
    static vector<IVolume*> mVolumes;
    static FSTab* mFsTab;

protected:
    static void loadEmmcVolumes();
    static void loadMtdVolumes();
    static void loadMiscVolumes();
};


// This handles the special partitions
#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

#ifndef SP1_NAME
#define SP1_NAME
#define SP1_BACKUP_METHOD none
#define SP1_MOUNTABLE 0
#endif
#ifndef SP1_DISPLAY_NAME
#define SP1_DISPLAY_NAME SP1_NAME
#endif
#ifndef SP2_NAME
#define SP2_NAME
#define SP2_BACKUP_METHOD none
#define SP2_MOUNTABLE 0
#endif
#ifndef SP2_DISPLAY_NAME
#define SP2_DISPLAY_NAME SP2_NAME
#endif
#ifndef SP3_NAME
#define SP3_NAME
#define SP3_BACKUP_METHOD none
#define SP3_MOUNTABLE 0
#endif
#ifndef SP3_DISPLAY_NAME
#define SP3_DISPLAY_NAME SP3_NAME
#endif


#endif  // _MOUNTS_HPP

