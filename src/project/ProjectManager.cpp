#include "ProjectManager.h"
#include "utils/FileUtils.h"

Project ProjectManager::openOrCreate(const std::string& path) {
    Project p;
    p.path = path;
    if (!utils::file::exists(path)) {
        utils::file::createDirs(path);
    }
    return p;
}

bool ProjectManager::isValid(const std::string& path) const {
    return utils::file::exists(path) && utils::file::isDir(path);
}
