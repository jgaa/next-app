#include "SoundPlayer.h"
#include <QResource>
#include <QByteArray>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>
#include <QThread>

#include "logging.h"
#include "util.h"

#ifdef min
//  Thank you so much Microsoft. We all love polluted namespaces.
#   undef min
#endif

using namespace std;

namespace {

struct VfsFile {
    template <typename T>
    VfsFile(const T& data) : data_{data} {}

    std::span<const char> data_;
    size_t pos_{0};
};

struct Vfs {
    ma_vfs_callbacks callbacks{};
    SoundPlayer *player{nullptr};
};

ma_result vfsOpen_(ma_vfs* pVFS, const QString& path, ma_uint32 openMode, ma_vfs_file* pFile) {
    if (!pVFS || !pFile) {
        return ma_result::MA_INVALID_ARGS;
    }

    Vfs* vfs = static_cast<Vfs*>(pVFS);
    if (!vfs || !vfs->player) {
        LOG_ERROR_N << "VFS is not initialized or player is null";
        return ma_result::MA_INVALID_OPERATION;
    }

    auto data = vfs->player->getSoundData(path);
    if (data.empty()) {
        return ma_result::MA_DOES_NOT_EXIST;
    }

    auto pfile = new VfsFile(data);
    if (!pfile) {
        LOG_ERROR_N << "Failed to allocate VfsFile for path:" << path;
        return ma_result::MA_OUT_OF_MEMORY;
    }

    *pFile = reinterpret_cast<ma_vfs_file*>(pfile);

    return ma_result::MA_SUCCESS;
}


extern "C" {

    ma_result vfsOpenW(ma_vfs* pVFS, const wchar_t* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile) {
        return vfsOpen_(pVFS, QString::fromWCharArray(pFilePath), openMode, pFile);
    };

    ma_result vfsOpen(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile) {
        return vfsOpen_(pVFS, QString::fromUtf8(pFilePath), openMode, pFile);
    };

    ma_result vfsClose(ma_vfs* pVFS, ma_vfs_file file) {
        // Once loaded, the
        assert(pVFS);
        assert(file);
        if (auto* pfile = reinterpret_cast<VfsFile*>(file)) {
            delete pfile; // Clean up the VfsFile
        } else {
            LOG_ERROR_N << "Invalid VfsFile pointer";
            return ma_result::MA_INVALID_ARGS;
        }
        return ma_result::MA_SUCCESS;
    };


    ma_result vfsRead(ma_vfs* pVFS, ma_vfs_file file, void* pDst, size_t sizeInBytes, size_t* pBytesRead) {
        assert(pVFS);
        assert(file);
        assert(pDst);
        assert(pBytesRead);

        auto * fp = reinterpret_cast<VfsFile*>(file);
        if (!fp) {
            LOG_ERROR_N << "Invalid VfsFile pointer";
            return ma_result::MA_INVALID_ARGS;
        }
        if (fp->pos_ >= fp->data_.size()) {
            *pBytesRead = 0;
            return ma_result::MA_AT_END;
        }
        size_t bytesToRead = std::min(sizeInBytes, fp->data_.size() - fp->pos_);
        std::memcpy(pDst, fp->data_.data() + fp->pos_, bytesToRead);
        fp->pos_ += bytesToRead;
        *pBytesRead = bytesToRead;
        return ma_result::MA_SUCCESS;
    }

    ma_result vfsSeek(ma_vfs* pVFS, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
        assert(pVFS);
        assert(file);
        auto * fp = reinterpret_cast<VfsFile*>(file);
        if (!fp) {
            LOG_ERROR_N << "Invalid VfsFile pointer";
            return ma_result::MA_INVALID_ARGS;
        }
        switch (origin) {
            case ma_seek_origin_start:
                fp->pos_ = static_cast<size_t>(offset);
                break;
            case ma_seek_origin_current:
                fp->pos_ += static_cast<size_t>(offset);
                break;
            case ma_seek_origin_end:
                fp->pos_ = fp->data_.size() + static_cast<size_t>(offset);
                break;
            default:
                LOG_ERROR_N << "Invalid seek origin";
                return ma_result::MA_INVALID_ARGS;
        }

        if (fp->pos_ >= fp->data_.size()) {
            fp->pos_ = fp->data_.size(); // Clamp to end of data
        };

        return ma_result::MA_SUCCESS;
    }

    ma_result vfsTell(ma_vfs* pVFS, ma_vfs_file file, ma_int64* pCursor) {
        assert(pVFS);
        assert(file);
        auto * fp = reinterpret_cast<VfsFile*>(file);
        if (!fp) {
            LOG_ERROR_N << "Invalid VfsFile pointer";
            return ma_result::MA_INVALID_ARGS;
        }
        if (!pCursor) {
            LOG_ERROR_N << "pCursor is null";
            return ma_result::MA_INVALID_ARGS;
        }
        *pCursor = static_cast<ma_int64>(fp->pos_);
        return ma_result::MA_SUCCESS;
    }

    ma_result vfsInfo(ma_vfs* pVFS, ma_vfs_file file, ma_file_info* pInfo) {
        assert(pVFS);
        assert(file);
        auto * fp = reinterpret_cast<VfsFile*>(file);
        if (!fp) {
            LOG_ERROR_N << "Invalid VfsFile pointer";
            return ma_result::MA_INVALID_ARGS;
        }
        if (!pInfo) {
            LOG_ERROR_N << "pInfo is null";
            return ma_result::MA_INVALID_ARGS;
        }
        pInfo->sizeInBytes = fp->data_.size();
        return ma_result::MA_SUCCESS;
    }

} // "C"

ma_vfs_callbacks VfsCallbacks() {
    ma_vfs_callbacks cb;
    cb.onOpen = vfsOpen;
    cb.onOpenW = vfsOpenW;
    cb.onRead = vfsRead;
    cb.onWrite = nullptr; // Not used in this context
    cb.onSeek = vfsSeek;
    cb.onTell = vfsTell;
    cb.onInfo = vfsInfo;
    cb.onClose = vfsClose;

    return cb;
};

ma_vfs_callbacks *getCallbacks() {
    static ma_vfs_callbacks callbacks = VfsCallbacks();
    return &callbacks;
}

} // anon ns

SoundPlayer::SoundPlayer()
{
    auto vfs = new Vfs();
    vfs->callbacks = VfsCallbacks();
    vfs->player = this;
    vfs_ = reinterpret_cast<ma_vfs*>(vfs);

    ma_resource_manager_config rm_config = ma_resource_manager_config_init();
    rm_config.pVFS              = vfs_;
    rm_config.decodedFormat     = ma_format_f32;
    rm_config.decodedChannels   = 2;
    rm_config.decodedSampleRate = 48000;

    if (ma_resource_manager_init(&rm_config, &resource_manager_) != MA_SUCCESS) {
        LOG_ERROR_N << "Failed to initialize resource manager";
    }

    ma_engine_config eng_config = ma_engine_config_init();
    eng_config.pResourceManager = &resource_manager_;

    auto res = ma_engine_init(&eng_config, &engine_);
    if (res != MA_SUCCESS) {
        LOG_ERROR_N << "Failed to initialize audio engine";
        return;
    }
    setState(State::INITIALIZED);
    LOG_DEBUG_N << "SoundPlayer initialized";
}

void SoundPlayer::playSound(const QString &resourcePath, double volume)
{
    if (state_ != State::INITIALIZED) {
        LOG_WARN_N << "SoundPlayer is not initialized, cannot play sound";
        return;
    }

    if (volume < 0.0 || volume > 1.0) {
        LOG_WARN_N << "Volume must be between 0.0 and 1.0";
        return;
    }

    ma_engine_set_volume(&engine_, static_cast<float>(volume));

    auto str = resourcePath.toStdString();
    ma_engine_play_sound(&engine_, str.c_str(), NULL);
}

SoundPlayer::~SoundPlayer() {
    close();
}

void SoundPlayer::close()
{
    unique_lock lock(mutex_);
    if (state_ == State::INITIALIZED) {
        ma_engine_uninit(&engine_);
    }

    if (vfs_) {
        auto vfs = reinterpret_cast<Vfs*>(vfs_);
        delete vfs; // Clean up the VFS
        vfs_ = nullptr;
    }

    setState(State::CLOSED);
    sounds_.clear();
}

// We lazily cache the data so we don't have to track the lifetime of the buffers.
QByteArrayView SoundPlayer::getSoundData(const QString &resourcePath) {
    QString adjusted_path= resourcePath;

    // Remove "qrc:/" prefix if present
    if (adjusted_path.startsWith("qrc:/")) {
        adjusted_path = adjusted_path.mid(3);  // Remove "qrc"
    }

    unique_lock lock(mutex_);
    if (auto it = sounds_.find(adjusted_path); it != sounds_.end()) {
        return it->second;
    }

    QResource resource(adjusted_path);
    if (!resource.isValid()) {
        LOG_ERROR_N << "Resource not found:" << resourcePath;
        return {};
    }

    auto [it, _] = sounds_.emplace(adjusted_path, QByteArray{reinterpret_cast<const char*>(resource.data()), resource.size()});

    return it->second;
}

SoundPlayer &SoundPlayer::instance() noexcept {
    static SoundPlayer instance;
    return instance;
}

