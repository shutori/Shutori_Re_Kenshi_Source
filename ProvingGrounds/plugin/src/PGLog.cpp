#include "PGLog.h"
#include <Windows.h>
#include <cstdio>
#include <sstream>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    struct Sink {
        CRITICAL_SECTION lock;
        std::wstring directory;
        std::string session;
        unsigned sequence;
        bool warned;
        Sink() : sequence(0), warned(false) {
            InitializeCriticalSection(&lock);
            wchar_t path[32768] = {};
            // Resolve this plugin's module, not the game's executable or working directory.
            const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, 32768);
            if (length > 0 && length < 32768) {
                directory.assign(path, length);
                const size_t slash = directory.find_last_of(L"\\/");
                directory = slash == std::wstring::npos ? L"" : directory.substr(0, slash + 1);
            }
            FILETIME time; GetSystemTimeAsFileTime(&time);
            char id[80]; sprintf_s(id, "%08lx%08lx-%lu", time.dwHighDateTime, time.dwLowDateTime, GetCurrentProcessId());
            session = id;
        }
        ~Sink() { DeleteCriticalSection(&lock); }
    } sink;
    struct Guard {
        Guard() { EnterCriticalSection(&sink.lock); }
        ~Guard() { LeaveCriticalSection(&sink.lock); }
    };
    std::string Timestamp() {
        SYSTEMTIME t; GetSystemTime(&t);
        char out[40]; sprintf_s(out, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        return out;
    }
    void Append(const wchar_t* name, const std::string& line) {
        FILE* file = NULL;
        if (!sink.directory.empty() && _wfopen_s(&file, (sink.directory + name).c_str(), L"ab") == 0 && file) {
            const bool written = fwrite(line.data(), 1, line.size(), file) == line.size();
            const int closed = fclose(file); // Flush each event for long unattended runs/crashes.
            if (written && !closed) return;
        }
        if (!sink.warned) {
            sink.warned = true;
            OutputDebugStringA("Proving Grounds: could not write pg_debug.log, pg_combat_debug.log or pg_challenge_debug.log beside ProvingGrounds.dll in the mod folder\n");
        }
    }
}
namespace PGLog {
    std::string Quote(const std::string& value) {
        std::string out = "\"";
        for (size_t i = 0; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c < 32) { char escaped[7]; sprintf_s(escaped, "\\u%04x", static_cast<unsigned>(c)); out += escaped; }
            else out += c;
        }
        return out + "\"";
    }
    void Debug(const std::string& message) {
        Guard guard;
        Append(L"pg_debug.log", Timestamp() + " [" + sink.session + "] DEBUG " + message + "\r\n");
    }
    void Error(const std::string& message) {
        Guard guard;
        Append(L"pg_debug.log", Timestamp() + " [" + sink.session + "] ERROR " + message + "\r\n");
    }
    // The JSON envelope is identical on every channel; only the destination
    // differs. The session id is shared, so lines from the two files can be
    // ordered against each other on (session, record).
    namespace { std::string Envelope(const std::string& fields) {
        return "{\"utc\":" + Quote(Timestamp()) + ",\"session\":" + Quote(sink.session) + "," + fields + "}\r\n";
    } }
    void Combat(const std::string& fields) {
        Guard guard;
        Append(L"pg_combat_debug.log", Envelope(fields));
    }
    void Challenge(const std::string& fields) {
        Guard guard;
        Append(L"pg_challenge_debug.log", Envelope(fields));
    }
    unsigned NextId() { Guard guard; return ++sink.sequence; }
    const std::wstring& Directory() { return sink.directory; }
}
