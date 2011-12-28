// Utility.hpp - Simple utility functions for volumes
//  Copyright(c) 2011 by Kevin Bruckert, for use in TeamWin Recovery Project.
//  Author: Kevin Bruckert

#ifndef _UTILITY_HPP
#define _UTILITY_HPP

#include "mounts.hpp"

namespace Utility
{

string getFsTypeStr(eFSType type);
eFSType getFsTypeFromStr(string type);
string getBackupMethodStr(eBackupMethod method);
eBackupMethod getBackupMethodFromStr(string method);
bool isMounted(string mountpoint);
int mount(string device, string mountpoint, eFSType type);
int unmount(string mountpoint);
bool fileExists(string filename);
int rm_folder(string path);
int rm_folder_safe(string path);
int format_ext(eFSType fstype, string device);
string readlink_recursive(string filename);
eFSType getDeviceFSType(string device, bool cached = true);
long long getVolumeSizeOfPartition(string device);

}

// These are functions from the core of TWRP
extern "C"
{

int __system(const char *command);
FILE * __popen(const char *program, const char *type);
int __pclose(FILE *iop);

}

#endif  // _UTILITY_HPP

