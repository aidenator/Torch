#include "ZWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "spdlog/spdlog.h"
#include <Companion.h>
#include <miniz/zip_file.hpp>

namespace fs = std::filesystem;

// fseeko is POSIX; MSVC's 64-bit seek is _fseeki64
static int SeekTo(FILE* file, uint64_t offset, int origin) {
#ifdef _WIN32
    return _fseeki64(file, (__int64)offset, origin);
#else
    return fseeko(file, (off_t)offset, origin);
#endif
}

ZWrapper::ZWrapper(const std::string& path, bool storeUncompressed) {
    this->mPath = path;
    this->mLevel = storeUncompressed ? MZ_NO_COMPRESSION : MZ_BEST_COMPRESSION;
}

ZWrapper::~ZWrapper() {
    if (this->mArchive != nullptr) {
        mz_zip_writer_end(this->mArchive);
        delete this->mArchive;
    }
    if (this->mFile != nullptr) {
        fclose(this->mFile);
    }
}

// miniz writes each entry's data first and then seeks back to fill in its header, and the C
// library flushes its buffer on every seek. Where each write is a slow request to the file system
// that makes tens of thousands of tiny writes, and the 3DS's SD card is also very slow at writes
// that do not start and end on a block boundary. So the archive is kept in memory from the last
// written block on, and goes to the file only in whole aligned blocks, once nothing in them can
// change (see FlushFinished).
size_t ZWrapper::WriteAt(void* opaque, unsigned long long offset, const void* buffer, size_t size) {
    auto* self = static_cast<ZWrapper*>(opaque);
    if (offset < self->mWindowStart) {
        // Already written; miniz only seeks back within the entry it is adding, so this does not happen
        SPDLOG_ERROR("[ZWrapper] Write at 0x{:X} is before the unwritten part (0x{:X})", offset, self->mWindowStart);
        return 0;
    }
    const size_t at = offset - self->mWindowStart;
    if (at + size > self->mWindow.size()) {
        self->mWindow.resize(at + size);
    }
    memcpy(self->mWindow.data() + at, buffer, size);
    return size;
}

// Writes the whole blocks below `limit`, everything before which is final
bool ZWrapper::FlushFinished(uint64_t limit) {
    size_t blocks = 0;
    while (this->mWindowStart + (blocks + 1) * kBlockSize <= limit &&
           (blocks + 1) * kBlockSize <= this->mWindow.size()) {
        blocks++;
    }
    if (blocks == 0) {
        return true;
    }
    const size_t bytes = blocks * kBlockSize;
    const bool ok = SeekTo(this->mFile, this->mWindowStart, SEEK_SET) == 0 &&
                    fwrite(this->mWindow.data(), 1, bytes, this->mFile) == bytes;
    this->mWindow.erase(this->mWindow.begin(), this->mWindow.begin() + bytes);
    this->mWindowStart += bytes;
    return ok;
}

int32_t ZWrapper::CreateArchive() {
    this->mFile = fopen(this->mPath.c_str(), "wb");
    if (this->mFile == nullptr) {
        SPDLOG_ERROR("[ZWrapper] Could not create {}", this->mPath);
        throw std::runtime_error("Could not create " + this->mPath);
    }
    // Writes already arrive in large blocks
    setvbuf(this->mFile, nullptr, _IONBF, 0);
    this->mWindow.reserve(2 * kBlockSize);
    this->mArchive = new mz_zip_archive();
    this->mArchive->m_pWrite = WriteAt;
    this->mArchive->m_pIO_opaque = this;
    if (!mz_zip_writer_init(this->mArchive, 0)) {
        SPDLOG_ERROR("[ZWrapper] Could not start {}", this->mPath);
        delete this->mArchive;
        this->mArchive = nullptr;
        throw std::runtime_error("Could not start " + this->mPath);
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
    // Nothing before the end of the archive changes again
    if (!this->FlushFinished(this->mArchive->m_archive_size)) {
        SPDLOG_ERROR("[ZWrapper] Failed to write {}", this->mPath);
        throw std::runtime_error("write error");
    }
    return true;
}

int32_t ZWrapper::Close(void) {
    if (this->mArchive == nullptr) {
        return -1;
    }
    bool ok = mz_zip_writer_finalize_archive(this->mArchive);
    mz_zip_writer_end(this->mArchive);
    delete this->mArchive;
    this->mArchive = nullptr;
    // The rest, including the unaligned end
    if (!this->mWindow.empty()) {
        ok = SeekTo(this->mFile, this->mWindowStart, SEEK_SET) == 0 &&
             fwrite(this->mWindow.data(), 1, this->mWindow.size(), this->mFile) == this->mWindow.size() && ok;
    }
    ok = fclose(this->mFile) == 0 && ok;
    this->mFile = nullptr;
    std::vector<char>().swap(this->mWindow);
    if (!ok) {
        SPDLOG_ERROR("[ZWrapper] Could not finish {}", this->mPath);
        return -1;
    }
    return 0;
}
