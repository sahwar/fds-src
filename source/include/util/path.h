/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_UTIL_PATH_H_
#define SOURCE_INCLUDE_UTIL_PATH_H_
#include <string>
#include <vector>

#include <shared/fds_types.h>
namespace fds {
namespace util {

//checks if the given path is a directory
bool dirExists(const std::string& dirname);

// checks if the given path is a file
bool fileExists(const std::string& filename);

// gets the list of directories in the given dir
void getSubDirectories(const std::string& dirname, std::vector<std::string>& vecDirs);

// gets the list fo files in the given directory
void getFiles(const std::string& dirname, std::vector<std::string>& vecDirs);

std::string getFileChecksum(const std::string& filename);

// get bytes from human readable string - 1KB, 2MB, 3G ...
fds_uint64_t getBytesFromHumanSize(const std::string& strFileSize);

// get the PATH env variable
void getPATH(std::vector<std::string>& paths);

// add the given path to the PATH env variable
bool addPATH(const std::string& path);

// remove the given path from the PATH env variable
bool removePATH(const std::string& path);

// get the binary location of the given command
std::string which(const std::string& path);

// used by test and drivers to populate root directories
void populateTempRootDirectories(std::vector<std::string> &roots, int numOfNodes);
}  // namespace util
}  // namespace fds

#endif  // SOURCE_INCLUDE_UTIL_PATH_H_
