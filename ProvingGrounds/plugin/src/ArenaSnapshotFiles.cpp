#include "ArenaSnapshotFiles.h"
#include "ArenaSnapshotJson.h"
#include "ArenaNativePath.h"
#include <windows.h>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>

namespace ArenaPersistence
{
    namespace
    {
        volatile LONG g_fileSequence = 0;

        void SetError(std::string& error, const char* action,
            const std::wstring& path, DWORD code)
        {
            error = NativePath::Failure(action, path, code);
        }

        std::wstring DirectoryPrefix(const std::wstring& path)
        {
            const size_t separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring() :
                path.substr(0, separator + 1);
        }

        std::wstring UniqueSuffix()
        {
            FILETIME time;
            GetSystemTimeAsFileTime(&time);
            ULARGE_INTEGER ticks;
            ticks.LowPart = time.dwLowDateTime;
            ticks.HighPart = time.dwHighDateTime;
            const LONG sequence = InterlockedIncrement(&g_fileSequence);
            std::wostringstream suffix;
            suffix << std::hex << std::setfill(L'0') << std::setw(16)
                << ticks.QuadPart << '.' << std::setw(8)
                << GetCurrentProcessId() << '.' << std::setw(8)
                << static_cast<unsigned long>(sequence);
            return suffix.str();
        }

        bool WriteAll(HANDLE file, const std::string& text, DWORD& failure)
        {
            size_t offset = 0;
            while (offset < text.size())
            {
                const size_t remaining = text.size() - offset;
                const DWORD request = remaining > MAXDWORD ? MAXDWORD :
                    static_cast<DWORD>(remaining);
                DWORD written = 0;
                if (!WriteFile(file, text.data() + offset, request, &written,
                    NULL) || written == 0)
                {
                    failure = GetLastError();
                    return false;
                }
                offset += written;
            }
            return true;
        }

        bool CloseWrittenFile(HANDLE file, DWORD& failure)
        {
            if (!FlushFileBuffers(file))
            {
                failure = GetLastError();
                CloseHandle(file);
                return false;
            }
            if (!CloseHandle(file))
            {
                failure = GetLastError();
                return false;
            }
            return true;
        }
    }

    bool SnapshotFiles::SaveKey(const std::string& folder,
        const std::string& name, std::string& key, std::string& error)
    {
        key.clear(); error.clear();
        std::wstring path, wideName;
        UINT codepage;
        if (!NativePath::Resolve(folder + "/proving_grounds_mmr.json",
            path, codepage, error) ||
            !NativePath::Decode(name, codepage,
                codepage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, wideName) ||
            (key = NativePath::Utf8(wideName)).empty())
        {
            if (error.empty()) error = "Cannot encode native save name as Unicode";
            return false;
        }
        return true;
    }

    ReadCode SnapshotFiles::Read(const std::string& path, std::string& text,
        std::string& error)
    {
        text.clear();
        error.clear();
        std::wstring wide;
        UINT codepage;
        if (!NativePath::Resolve(path, wide, codepage, error)) return ReadFailed;
        HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD failure = GetLastError();
            if (failure == ERROR_FILE_NOT_FOUND ||
                failure == ERROR_PATH_NOT_FOUND)
                return NotFound;
            SetError(error, "Reading", wide, failure);
            return ReadFailed;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size))
        {
            const DWORD failure = GetLastError();
            CloseHandle(file);
            SetError(error, "Sizing", wide, failure);
            return ReadFailed;
        }
        if (size.QuadPart < 0 ||
            static_cast<unsigned __int64>(size.QuadPart) >
                static_cast<unsigned __int64>(kMaxSnapshotBytes))
        {
            CloseHandle(file);
            error = "Arena snapshot '" + NativePath::Utf8(wide) +
                "' exceeds the 16 MiB safety limit";
            return ReadFailed;
        }

        try
        {
            text.resize(static_cast<size_t>(size.QuadPart));
        }
        catch (const std::bad_alloc&)
        {
            CloseHandle(file);
            text.clear();
            try
            {
                error = "Arena snapshot read allocation failed";
            }
            catch (const std::bad_alloc&)
            {
                error.clear();
            }
            return ReadFailed;
        }
        size_t offset = 0;
        while (offset < text.size())
        {
            const size_t remaining = text.size() - offset;
            const DWORD request = remaining > MAXDWORD ? MAXDWORD :
                static_cast<DWORD>(remaining);
            DWORD received = 0;
            if (!ReadFile(file, &text[0] + offset, request, &received, NULL) ||
                received == 0)
            {
                const DWORD failure = GetLastError();
                CloseHandle(file);
                text.clear();
                SetError(error, "Reading", wide,
                    failure == ERROR_SUCCESS ? ERROR_HANDLE_EOF : failure);
                return ReadFailed;
            }
            offset += received;
        }
        if (!CloseHandle(file))
        {
            const DWORD failure = GetLastError();
            text.clear();
            SetError(error, "Closing", wide, failure);
            return ReadFailed;
        }
        return ReadOk;
    }

    bool SnapshotFiles::ListBackups(const std::string& path,
        std::vector<std::string>& paths, std::string& error)
    {
        paths.clear();
        error.clear();
        std::wstring wide;
        UINT codepage;
        if (!NativePath::Resolve(path, wide, codepage, error)) return false;
        const std::wstring pattern = wide + L".backup.*";
        WIN32_FIND_DATAW entry;
        HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE)
        {
            const DWORD failure = GetLastError();
            if (failure == ERROR_FILE_NOT_FOUND ||
                failure == ERROR_PATH_NOT_FOUND)
                return true;
            SetError(error, "Listing backups for", wide, failure);
            return false;
        }

        const std::wstring directory = DirectoryPrefix(wide);
        do
        {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                paths.push_back(NativePath::Utf8(directory + entry.cFileName));
        } while (FindNextFileW(search, &entry));
        const DWORD failure = GetLastError();
        if (!FindClose(search))
        {
            SetError(error, "Closing backup search for", wide,
                GetLastError());
            paths.clear();
            return false;
        }
        if (failure != ERROR_NO_MORE_FILES)
        {
            SetError(error, "Listing backups for", wide, failure);
            paths.clear();
            return false;
        }
        std::sort(paths.begin(), paths.end(), std::greater<std::string>());
        return true;
    }

    bool SnapshotFiles::Preserve(const std::string& path,
        std::string& backupPath, std::string& error)
    {
        backupPath.clear();
        error.clear();
        std::wstring wide;
        UINT codepage;
        if (!NativePath::Resolve(path, wide, codepage, error)) return false;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const std::wstring candidate = wide + L".backup." + UniqueSuffix();
            if (CopyFileW(wide.c_str(), candidate.c_str(), TRUE))
            {
                backupPath = NativePath::Utf8(candidate);
                return true;
            }
            const DWORD failure = GetLastError();
            if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS)
            {
                SetError(error, "Preserving", wide, failure);
                return false;
            }
        }
        error = "Could not allocate a unique backup path for '" +
            NativePath::Utf8(wide) + "'";
        return false;
    }

    bool SnapshotFiles::Replace(const std::string& path,
        const std::string& text, std::string& error)
    {
        error.clear();
        std::wstring wide, temporary;
        UINT codepage;
        if (!NativePath::Resolve(path, wide, codepage, error)) return false;
        HANDLE file = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            temporary = wide + L".tmp." + UniqueSuffix();
            file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE)
                break;
            const DWORD failure = GetLastError();
            if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS)
            {
                SetError(error, "Creating temporary snapshot", temporary,
                    failure);
                return false;
            }
        }
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "Could not allocate a unique temporary path for '" +
                NativePath::Utf8(wide) + "'";
            return false;
        }

        DWORD failure = ERROR_SUCCESS;
        if (!WriteAll(file, text, failure))
        {
            CloseHandle(file);
            DeleteFileW(temporary.c_str());
            SetError(error, "Writing temporary snapshot", temporary, failure);
            return false;
        }
        if (!CloseWrittenFile(file, failure))
        {
            DeleteFileW(temporary.c_str());
            SetError(error, "Writing temporary snapshot", temporary, failure);
            return false;
        }

        if (!MoveFileExW(temporary.c_str(), wide.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            failure = GetLastError();
            DeleteFileW(temporary.c_str());
            SetError(error, "Replacing snapshot", wide, failure);
            return false;
        }
        return true;
    }
}
