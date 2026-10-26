#include "art.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include <trg/download.h>

#include "games.h"

namespace whopping {
namespace fs = std::filesystem;

namespace {

const char* kExtensions[] = {".png", ".jpg"};

bool ReadFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !out.empty();
}

}  // namespace

bool DecodeImage(const std::string& bytes, int& w, int& h, std::vector<unsigned char>& rgba);

namespace {

bool Decode(const std::string& bytes, int& w, int& h, std::vector<unsigned char>& rgba) {
  return DecodeImage(bytes, w, h, rgba);
}

}  // namespace

void (*g_free_texture)(Texture& tex) = nullptr;

bool DecodeImage(const std::string& bytes, int& w, int& h, std::vector<unsigned char>& rgba) {
  int n = 0;
  unsigned char* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h,
                                            &n, 4);
  if (!px) return false;
  rgba.assign(px, px + size_t(w) * h * 4);
  stbi_image_free(px);
  return true;
}

namespace {

// The image shrunk to 96 pixels wide (box average) and softened twice more:
// stretched over the window with linear filtering it reads as a heavy blur.
void SmallBlur(const std::vector<unsigned char>& src, int w, int h, std::vector<unsigned char>& out, int& ow,
               int& oh) {
  ow = 96;
  oh = std::max(1, h * ow / std::max(1, w));
  std::vector<float> acc(size_t(ow) * oh * 4, 0.0f), cnt(size_t(ow) * oh, 0.0f);
  for (int y = 0; y < h; ++y) {
    const int sy = y * oh / h;
    for (int x = 0; x < w; ++x) {
      const int sx = x * ow / w;
      const unsigned char* p = &src[(size_t(y) * w + x) * 4];
      float* a = &acc[(size_t(sy) * ow + sx) * 4];
      for (int c = 0; c < 4; ++c) a[c] += p[c];
      cnt[size_t(sy) * ow + sx] += 1.0f;
    }
  }
  for (size_t i = 0; i < cnt.size(); ++i)
    for (int c = 0; c < 4; ++c) acc[i * 4 + c] /= std::max(1.0f, cnt[i]);
  for (int pass = 0; pass < 3; ++pass) {
    std::vector<float> tmp(acc.size());
    for (int y = 0; y < oh; ++y)
      for (int x = 0; x < ow; ++x)
        for (int c = 0; c < 4; ++c) {
          float sum = 0, n = 0;
          for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
              const int xx = std::clamp(x + dx, 0, ow - 1), yy = std::clamp(y + dy, 0, oh - 1);
              sum += acc[(size_t(yy) * ow + xx) * 4 + c];
              n += 1;
            }
          tmp[(size_t(y) * ow + x) * 4 + c] = sum / n;
        }
    acc.swap(tmp);
  }
  out.resize(acc.size());
  for (size_t i = 0; i < acc.size(); ++i) out[i] = (unsigned char)std::clamp(acc[i], 0.0f, 255.0f);
  for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
}

// The cached file of a kind, whichever extension it was saved with ("" when none).
fs::path CachedFile(int id, ArtKind kind) {
  std::error_code ec;
  for (const char* ext : kExtensions) {
    fs::path p = fs::u8path(ArtFolder(id)) / (std::string(ArtName(kind)) + ext);
    if (fs::exists(p, ec) && fs::file_size(p, ec) > 0) return p;
  }
  return {};
}

}  // namespace

std::string ArtFolder(int steamgriddb_id) { return CacheDir() + "/art/" + std::to_string(steamgriddb_id); }

const char* ArtName(ArtKind kind) {
  switch (kind) {
    case ArtKind::kGrid: return "grid";
    case ArtKind::kHero: return "hero";
    case ArtKind::kLogo: return "logo";
    default: return "art";
  }
}

// Where each kind comes from. Filled in by FetchUrl below.
std::string FetchArt(int steamgriddb_id, ArtKind kind, std::string& bytes, std::string& ext);

ArtCache::ArtCache(std::vector<int> ids, UploadFn upload)
    : ids_(std::move(ids)), upload_(upload), textures_(ids_.size()), blurred_(ids_.size()) {}

ArtCache::~ArtCache() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

void ArtCache::Start() {
  if (worker_.joinable()) return;
  worker_ = std::thread([this] { Worker(); });
}

void ArtCache::Worker() {
  int fetched = 0, failed = 0;
  for (size_t g = 0; g < ids_.size() && !stop_; ++g) {
    for (int k = 0; k < int(ArtKind::kCount) && !stop_; ++k) {
      const ArtKind kind = ArtKind(k);
      std::string bytes;
      fs::path cached = CachedFile(ids_[g], kind);
      if (cached.empty() || !ReadFile(cached, bytes)) {
        std::string ext;
        {
          std::lock_guard<std::mutex> lock(mu_);
          status_ = "Downloading game art";
        }
        const std::string err = FetchArt(ids_[g], kind, bytes, ext);
        if (!err.empty() || bytes.empty()) {
          ++failed;
          continue;
        }
        std::error_code ec;
        fs::create_directories(fs::u8path(ArtFolder(ids_[g])), ec);
        const fs::path out = fs::u8path(ArtFolder(ids_[g])) / (std::string(ArtName(kind)) + ext);
        std::ofstream(out, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
        ++fetched;
      }
      Decoded d{int(g), k, 0, 0, {}};
      if (!Decode(bytes, d.width, d.height, d.rgba)) {
        // A damaged cache file: delete it so the next start fetches it again.
        std::error_code ec;
        if (!cached.empty()) fs::remove(cached, ec);
        continue;
      }
      if (kind == ArtKind::kHero) {
        Decoded b{int(g), -1, 0, 0, {}};
        SmallBlur(d.rgba, d.width, d.height, b.rgba, b.width, b.height);
        std::lock_guard<std::mutex> lock(mu_);
        ready_.push_back(std::move(b));
      }
      std::lock_guard<std::mutex> lock(mu_);
      ready_.push_back(std::move(d));
    }
  }
  std::lock_guard<std::mutex> lock(mu_);
  status_ = failed ? "Some game art could not be downloaded; it is tried again next time." : "";
  (void)fetched;
}

void ArtCache::Poll() {
  std::vector<Decoded> ready;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ready.swap(ready_);
  }
  for (Decoded& d : ready) {
    Texture t = upload_(d.width, d.height, d.rgba.data());
    if (d.kind < 0)
      blurred_[d.game] = t;
    else
      textures_[d.game][d.kind] = t;
  }
}

std::string ArtCache::status() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mu_));
  return status_;
}

}  // namespace whopping
