#pragma once

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
    // Entries go straight to the file as they are added, so the archive is never held in memory
    mz_zip_archive_tag* mArchive = nullptr;
    unsigned int mLevel;
};
