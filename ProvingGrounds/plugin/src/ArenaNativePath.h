#pragma once

#include <windows.h>
#include <climits>
#include <sstream>
#include <string>

namespace ArenaPersistence
{
    namespace NativePath
    {
        inline bool Decode(const std::string& bytes, UINT codepage,
            DWORD flags, std::wstring& wide)
        {
            if (bytes.empty() || bytes.find('\0') != std::string::npos ||
                bytes.size() > INT_MAX) return false;
            const int size = MultiByteToWideChar(codepage, flags, bytes.data(),
                static_cast<int>(bytes.size()), NULL, 0);
            if (!size) return false;
            wide.resize(size);
            return MultiByteToWideChar(codepage, flags, bytes.data(),
                static_cast<int>(bytes.size()), &wide[0], size) == size;
        }

        inline std::string Utf8(const std::wstring& wide)
        {
            if (wide.empty() || wide.size() > INT_MAX) return std::string();
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
            if (!size) return std::string();
            std::string text(size, '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                static_cast<int>(wide.size()), &text[0], size, NULL, NULL) != size)
                return std::string();
            return text;
        }

        inline bool IsDirectory(const std::wstring& path)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        inline std::wstring Parent(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : path.substr(0, slash);
        }

        inline std::string Failure(const char* action, const std::wstring& path,
            DWORD code);

        // Kenshi's narrow string may be UTF-8 or the active Windows ANSI page.
        // Prefer the encoding whose parent actually exists, not the locale alone.
        inline bool Resolve(const std::string& path, std::wstring& wide,
            UINT& codepage, std::string& error)
        {
            std::wstring utf8, acp;
            const bool validUtf8 = Decode(path, CP_UTF8, MB_ERR_INVALID_CHARS, utf8);
            const bool validAcp = Decode(path, CP_ACP, 0, acp);
            if (!validUtf8 && !validAcp)
            {
                error = "Cannot decode native save path as UTF-8 or Windows ANSI";
                return false;
            }
            const bool utf8Parent = validUtf8 && IsDirectory(Parent(utf8));
            const bool acpParent = validAcp && IsDirectory(Parent(acp));
            codepage = validUtf8 && (utf8Parent || !acpParent) ? CP_UTF8 : CP_ACP;
            wide = codepage == CP_UTF8 ? utf8 : acp;
            const DWORD length = GetFullPathNameW(wide.c_str(), 0, NULL, NULL);
            if (length)
            {
                std::wstring absolute(length, L'\0');
                const DWORD count = GetFullPathNameW(wide.c_str(), length,
                    &absolute[0], NULL);
                if (count && count < length) wide.assign(absolute, 0, count);
            }
            if (!utf8Parent && !acpParent)
            {
                error = Failure("Resolving save directory for", wide,
                    ERROR_PATH_NOT_FOUND);
                return false;
            }
            return true;
        }

        inline std::string ErrorText(DWORD code)
        {
            LPWSTR buffer = NULL;
            const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, NULL);
            std::string text;
            if (length && buffer) text = Utf8(std::wstring(buffer, length));
            if (buffer) LocalFree(buffer);
            while (!text.empty() && (text[text.size()-1] == '\r' ||
                text[text.size()-1] == '\n' || text[text.size()-1] == ' '))
                text.erase(text.size()-1);
            return text;
        }

        inline std::string Failure(const char* action, const std::wstring& path,
            DWORD code)
        {
            std::ostringstream message;
            message << action << " '" << Utf8(path) << "' failed (Win32 error "
                << code << ": " << ErrorText(code) << ')';
            return message.str();
        }
    }
}
