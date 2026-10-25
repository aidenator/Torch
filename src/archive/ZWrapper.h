#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include "BinaryWrapper.h"

struct mz_zip_archive_tag;

class ZWrapper : public BinaryWrapper {
public:
    explicit ZWrapper(const std::string& path, bool storeUncompressed = false);
    ~ZWrapper();

    int32_t CreateArchive(void) override;
    bool AddFile(const std::string& path, std::vector<char> data) override;
    int32_t Close(void) override;

private:
    // offset is miniz's mz_uint64
    static size_t WriteAt(void* opaque, unsigned long long offset, const void* buffer, size_t size);
    bool FlushFinished(uint64_t limit);

    static constexpr size_t kBlockSize = 1024 * 1024;

    // Entries go to the file as they are added, so the archive is never held in memory
    mz_zip_archive_tag* mArchive = nullptr;
    FILE* mFile = nullptr;
    unsigned int mLevel;
    // The archive from mWindowStart (always a multiple of kBlockSize) on, not yet written
    std::vector<char> mWindow;
    uint64_t mWindowStart = 0;
};
