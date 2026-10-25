#include "ZWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>

#include "spdlog/spdlog.h"
#include <Companion.h>
#include <miniz/zip_file.hpp>

namespace fs = std::filesystem;

ZWrapper::ZWrapper(const std::string& path, bool storeUncompressed) {
    this->mPath = path;
    this->mLevel = storeUncompressed ? MZ_NO_COMPRESSION : MZ_BEST_COMPRESSION;
}

ZWrapper::~ZWrapper() {
    if (this->mArchive != nullptr) {
        mz_zip_writer_end(this->mArchive);
        delete this->mArchive;
    }
}

int32_t ZWrapper::CreateArchive() {
    this->mArchive = new mz_zip_archive();
    if (!mz_zip_writer_init_file(this->mArchive, this->mPath.c_str(), 0)) {
        SPDLOG_ERROR("[ZWrapper] Could not create {}", this->mPath);
        delete this->mArchive;
        this->mArchive = nullptr;
        throw std::runtime_error("Could not create " + this->mPath);
    }
    SPDLOG_INFO("Loaded ZIP (O2R) archive: {}", mPath.c_str());
    return 0;
}

bool ZWrapper::AddFile(const std::string& path, std::vector<char> data) {
    char* fileData = data.data();
    size_t fileSize = data.size();

    if (Companion::Instance != nullptr && Companion::Instance->IsDebug()) {
        SPDLOG_INFO("Creating debug file: debug/{}", path);
        std::string dpath = "debug/" + path;
        if (!fs::exists(fs::path(dpath).parent_path())) {
            fs::create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream stream(dpath, std::ios::binary);
        stream.write(fileData, fileSize);
        stream.close();
    }

    if (!mz_zip_writer_add_mem(this->mArchive, path.c_str(), fileData, fileSize, this->mLevel)) {
        // miniz's own error says nothing useful, so log which entry blew up and how big.
        SPDLOG_ERROR("[ZWrapper] Failed to add '{}' ({} bytes)", path, fileSize);
        throw std::runtime_error("write error");
    }
    return true;
}

int32_t ZWrapper::Close(void) {
    if (this->mArchive == nullptr) {
        return -1;
    }
    const bool ok = mz_zip_writer_finalize_archive(this->mArchive);
    mz_zip_writer_end(this->mArchive);
    delete this->mArchive;
    this->mArchive = nullptr;
    if (!ok) {
        SPDLOG_ERROR("[ZWrapper] Could not finish {}", this->mPath);
        return -1;
    }
    return 0;
}
