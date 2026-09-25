#pragma once

#include "ArenaSnapshot.h"
#include <string>
#include <vector>

namespace ArenaPersistence
{
    enum ReadCode
    {
        ReadOk,
        NotFound,
        ReadFailed
    };

    struct Storage
    {
        virtual ~Storage() {}
        virtual ReadCode Read(const std::string& path, std::string& text,
            std::string& error) = 0;
        virtual bool ListBackups(const std::string& path,
            std::vector<std::string>& paths, std::string& error) = 0;
        virtual bool Preserve(const std::string& path,
            std::string& backupPath, std::string& error) = 0;
        virtual bool Replace(const std::string& path, const std::string& text,
            std::string& error) = 0;
    };

    LoadResult LoadSnapshot(Storage& files, const std::string& path,
        const std::string& saveKey, const std::string& nativeGeneration);
    bool PublishSnapshot(Storage& files, const std::string& path,
        const Snapshot& snapshot, std::string& error);
}
