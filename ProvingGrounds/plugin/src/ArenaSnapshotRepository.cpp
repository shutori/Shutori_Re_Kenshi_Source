#include "ArenaSnapshotRepository.h"
#include "ArenaSnapshotJson.h"

namespace ArenaPersistence
{
    namespace
    {
        LoadResult RecoverSnapshot(Storage& files,
            const std::vector<std::string>& backups,
            const std::string& saveKey,
            const std::string& nativeGeneration,
            const std::string& noMatchMessage,
            const LoadResult* generationMatch)
        {
            LoadResult result;
            result.code = WrongSnapshot;
            result.snapshot.saveKey = saveKey;
            result.snapshot.generation = nativeGeneration;
            result.message = noMatchMessage;

            LoadResult rebound;
            bool hasRebound = false;
            if (generationMatch && generationMatch->code == Ready)
            {
                rebound = *generationMatch;
                hasRebound = true;
            }

            for (size_t i = 0; i < backups.size(); ++i)
            {
                std::string text;
                std::string error;
                const ReadCode read = files.Read(backups[i], text, error);
                if (read == ReadFailed)
                {
                    result.code = IoFailure;
                    result.message = error.empty() ?
                        "Could not read arena snapshot backup " + backups[i] :
                        error;
                    return result;
                }
                if (read == NotFound)
                    continue;

                LoadResult candidate = DecodeSnapshot(text);
                if (candidate.code != Ready ||
                    candidate.snapshot.generation != nativeGeneration)
                    continue;
                if (candidate.snapshot.saveKey != saveKey)
                {
                    if (!nativeGeneration.empty() && !hasRebound)
                    {
                        rebound = candidate;
                        rebound.message =
                            "Recovered generation-matched snapshot copied from save '" +
                            candidate.snapshot.saveKey + "' at " + backups[i];
                        hasRebound = true;
                    }
                    continue;
                }
                candidate.message = "Recovered matching snapshot from " +
                    backups[i];
                return candidate;
            }
            if (hasRebound)
                return rebound;
            return result;
        }
    }

    LoadResult LoadSnapshot(Storage& files, const std::string& path,
        const std::string& saveKey, const std::string& nativeGeneration)
    {
        LoadResult result;
        result.snapshot.saveKey = saveKey;
        result.snapshot.generation = nativeGeneration;

        std::string text;
        std::string error;
        const ReadCode read = files.Read(path, text, error);
        if (read == ReadFailed)
        {
            result.code = IoFailure;
            result.message = error.empty() ? "Could not read arena snapshot" : error;
            return result;
        }
        if (read == NotFound)
        {
            if (nativeGeneration.empty())
            {
                result.code = Missing;
                result.message = "No arena snapshot exists for a fresh world";
                return result;
            }

            std::vector<std::string> backups;
            if (!files.ListBackups(path, backups, error))
            {
                result.code = IoFailure;
                result.message = error.empty() ?
                    "Could not inspect arena snapshot backups" : error;
                return result;
            }
            return RecoverSnapshot(files, backups, saveKey,
                nativeGeneration,
                "Native snapshot has no matching arena sidecar", NULL);
        }

        LoadResult parsed = DecodeSnapshot(text);
        if (parsed.code != Ready)
            return parsed;
        if (parsed.snapshot.saveKey == saveKey &&
            parsed.snapshot.generation == nativeGeneration)
            return parsed;

        LoadResult generationMatch;
        LoadResult* generationMatchPtr = NULL;
        if (!nativeGeneration.empty() &&
            parsed.snapshot.generation == nativeGeneration)
        {
            generationMatch = parsed;
            generationMatch.message =
                "Recovered generation-matched snapshot copied from save '" +
                parsed.snapshot.saveKey + "'";
            generationMatchPtr = &generationMatch;
        }

        std::vector<std::string> backups;
        if (!files.ListBackups(path, backups, error))
        {
            result.code = IoFailure;
            result.message = error.empty() ?
                "Could not inspect arena snapshot backups" : error;
            return result;
        }
        const std::string mismatch = parsed.snapshot.saveKey != saveKey ?
            "Arena sidecar belongs to a different native save" :
            "Arena sidecar generation does not match the native snapshot";
        return RecoverSnapshot(files, backups, saveKey, nativeGeneration,
            mismatch, generationMatchPtr);
    }

    bool PublishSnapshot(Storage& files, const std::string& path,
        const Snapshot& snapshot,
        std::string& error)
    {
        std::string text;
        if (!EncodeSnapshot(snapshot, text, error))
            return false;

        std::string currentText;
        const ReadCode read = files.Read(path, currentText, error);
        if (read == ReadFailed)
        {
            if (error.empty())
                error = "Could not inspect the existing arena snapshot";
            return false;
        }
        if (read == NotFound)
            return files.Replace(path, text, error);

        const LoadResult current = DecodeSnapshot(currentText);
        if (current.code != Ready && current.code != LegacyReset)
        {
            error = current.field;
            if (!error.empty() && !current.message.empty())
                error += ": ";
            error += current.message.empty() ?
                "Existing arena snapshot is not replaceable" :
                current.message;
            return false;
        }

        std::string backupPath;
        if (!files.Preserve(path, backupPath, error))
            return false;
        return files.Replace(path, text, error);
    }
}
