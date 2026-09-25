#pragma once

#include "ArenaSnapshotRepository.h"

namespace ArenaPersistence
{
    class SnapshotFiles : public Storage
    {
    public:
        virtual ReadCode Read(const std::string& path, std::string& text,
            std::string& error);
        virtual bool ListBackups(const std::string& path,
            std::vector<std::string>& paths, std::string& error);
        virtual bool Preserve(const std::string& path,
            std::string& backupPath, std::string& error);
        virtual bool Replace(const std::string& path, const std::string& text,
            std::string& error);
    };
}
