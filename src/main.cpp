/**
 * SPDX-FileComment: Metadata Extraction and flatbuffer Indexing Component
 * SPDX-FileType: SOURCE
 * SPDX-FileContributor: ZHENG Robert
 * SPDX-FileCopyrightText: 2026 ZHENG Robert
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.cpp
 * @brief Entry point and logic for metadata extraction and indexing
 * @version 0.3.0
 * @date 2026-04-15
 *
 * @author ZHENG Robert (robert@hase-zheng.net)
 * @copyright Copyright (c) 2026 ZHENG Robert
 *
 * @license Apache-2.0
 */

// src/main.cpp
// SIMD-optimiert via xxHash (XXH3). Optionaler 3. Parameter: output-folder
// Build recommendation:
//   g++ -std=c++23 -O3 -march=native -mavx2 -mfma -pthread src/main.cpp -o
//   photo_indexer \
//       $(pkg-config --cflags --libs exiv2) -lflatbuffers -lxxhash

#include <atomic>
#include <execution>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "metadata_generated.h"
#include <exiv2/exiv2.hpp>
#include <xxhash.h>

namespace fs = std::filesystem;
using namespace gallery;

// -------------------- Utilities --------------------

/**
 * @brief Extracts a string value from EXIF data by key.
 *
 * @param exif The Exiv2 ExifData object.
 * @param key The EXIF key to retrieve.
 * @return std::string The extracted string, or empty if not found.
 */
static inline std::string exifStr(const Exiv2::ExifData &exif,
                                  const char *key) {
  auto it = exif.findKey(Exiv2::ExifKey(key));
  return it == exif.end() ? std::string() : it->toString();
}

/**
 * @brief Extracts a string value from IPTC data by key.
 *
 * @param iptc The Exiv2 IptcData object.
 * @param key The IPTC key to retrieve.
 * @return std::string The extracted string, or empty if not found.
 */
static inline std::string iptcStr(const Exiv2::IptcData &iptc,
                                  const char *key) {
  auto it = iptc.findKey(Exiv2::IptcKey(key));
  return it == iptc.end() ? std::string() : it->toString();
}

/**
 * @brief Extracts a string value from XMP data by key.
 *
 * @param xmp The Exiv2 XmpData object.
 * @param key The XMP key to retrieve.
 * @return std::string The extracted string, or empty if not found.
 */
static inline std::string xmpStr(const Exiv2::XmpData &xmp, const char *key) {
  auto it = xmp.findKey(Exiv2::XmpKey(key));
  return it == xmp.end() ? std::string() : it->toString();
}

/**
 * @brief Extracts a GPS coordinate array (e.g., latitude or longitude) from EXIF data.
 *
 * @param exif The Exiv2 ExifData object.
 * @param key The GPS related EXIF key.
 * @return std::vector<double> A vector containing the rational values evaluated to double.
 */
static inline std::vector<double> gpsArray(const Exiv2::ExifData &exif,
                                           const char *key) {
  std::vector<double> out;
  auto it = exif.findKey(Exiv2::ExifKey(key));
  if (it == exif.end())
    return out;
  for (int i = 0; i < it->count(); ++i) {
    auto r = it->toRational(i);
    out.push_back(double(r.first) / r.second);
  }
  return out;
}

/**
 * @brief Extracts the GPS altitude from EXIF data.
 *
 * @param exif The Exiv2 ExifData object.
 * @param key The GPS altitude EXIF key.
 * @return double The altitude as a double.
 */
static inline double gpsAlt(const Exiv2::ExifData &exif, const char *key) {
  auto it = exif.findKey(Exiv2::ExifKey(key));
  if (it == exif.end())
    return 0.0;
  auto r = it->toRational();
  return double(r.first) / r.second;
}

/**
 * @brief Parses an ISO 8601 like date string into a uint32_t value (YYYYMMDD).
 *
 * @param s String representing the date.
 * @return uint32_t The parsed date in YYYYMMDD format, or 0 on failure.
 */
static inline uint32_t parseDate(const std::string &s) {
  if (s.size() < 10)
    return 0;
  try {
    return std::stoi(s.substr(0, 4)) * 10000 + std::stoi(s.substr(5, 2)) * 100 +
           std::stoi(s.substr(8, 2));
  } catch (...) {
    return 0;
  }
}

// -------------------- PhotoMeta --------------------

/**
 * @brief Structure to hold extracted metadata for a single photo.
 */
struct PhotoMeta {
  uint64_t id;
  std::string file;

  uint32_t iso = 0;
  uint32_t date = 0;
  std::string camera;
  std::vector<std::string> tags;

  // EXIF
  std::string make, model, dt, dt_orig, copyright;
  std::string gps_lat_ref, gps_lon_ref, gps_ts, gps_map;
  std::vector<double> gps_lat, gps_lon;
  uint8_t gps_alt_ref = 0;
  double gps_alt = 0.0;

  // IPTC
  std::string iptc_date, iptc_time, iptc_city, iptc_sub, iptc_state;
  std::string iptc_ccode, iptc_cname, iptc_copy;

  // XMP
  std::string x_addr_en, x_addr_loc, x_ccode, x_rights, x_title;
  std::string x_dt_orig, x_city, x_country, x_state, x_ps_ccode;
  std::string x_cont, x_cca2, x_cca3, x_capital, x_tz, x_cowner;
};

// -------------------- XXH3 fingerprint --------------------

/**
 * @brief Generates an ultra-fast XXH3 64-bit fingerprint for a string.
 *
 * Uses SIMD internally when compiled with AVX2. Used for building the tag and camera indices.
 *
 * @param s The input string to hash.
 * @return uint64_t The 64-bit hash.
 */
static inline uint64_t fingerprint_xxh3(const std::string &s) {
  // XXH3_64bits is extremely fast and uses SIMD internally when compiled with
  // SIMD flags.
  return static_cast<uint64_t>(XXH3_64bits(s.data(), s.size()));
}

// -------------------- extract metadata --------------------

/**
 * @brief Extracts all required metadata (EXIF, IPTC, XMP) from an image file.
 *
 * @param id The unique identifier assigned to this photo.
 * @param p The filesystem path to the photo.
 * @return PhotoMeta Structure populated with extracted fields, or empty on failure.
 */
static PhotoMeta extract_meta(uint64_t id, const fs::path &p) {
  PhotoMeta m;
  m.id = id;
  m.file = p.string();

  Exiv2::Image::UniquePtr img;
  try {
    img = Exiv2::ImageFactory::open(m.file);
  } catch (...) {
    return m;
  }
  if (!img)
    return m;

  try {
    img->readMetadata();
  } catch (...) {
    return m;
  }

  const auto &exif = img->exifData();
  const auto &iptc = img->iptcData();
  const auto &xmp = img->xmpData();

  m.make = exifStr(exif, "Exif.Image.Make");
  m.model = exifStr(exif, "Exif.Image.Model");
  m.dt = exifStr(exif, "Exif.Image.DateTime");
  m.dt_orig = exifStr(exif, "Exif.Photo.DateTimeOriginal");
  m.copyright = exifStr(exif, "Exif.Image.Copyright");

  m.camera = m.model;
  m.date = parseDate(m.dt_orig);

  auto itISO = exif.findKey(Exiv2::ExifKey("Exif.Photo.ISOSpeedRatings"));
  if (itISO != exif.end())
    m.iso = static_cast<uint32_t>(itISO->toInt64());

  m.gps_lat_ref = exifStr(exif, "Exif.GPSInfo.GPSLatitudeRef");
  m.gps_lon_ref = exifStr(exif, "Exif.GPSInfo.GPSLongitudeRef");
  m.gps_lat = gpsArray(exif, "Exif.GPSInfo.GPSLatitude");
  m.gps_lon = gpsArray(exif, "Exif.GPSInfo.GPSLongitude");
  m.gps_alt = gpsAlt(exif, "Exif.GPSInfo.GPSAltitude");
  m.gps_ts = exifStr(exif, "Exif.GPSInfo.GPSTimeStamp");
  m.gps_map = exifStr(exif, "Exif.GPSInfo.GPSMapDatum");

  auto itAltRef = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitudeRef"));
  if (itAltRef != exif.end())
    m.gps_alt_ref = static_cast<uint8_t>(itAltRef->toInt64());

  // IPTC
  m.iptc_date = iptcStr(iptc, "Iptc.Application2.DateCreated");
  m.iptc_time = iptcStr(iptc, "Iptc.Application2.TimeCreated");
  m.iptc_city = iptcStr(iptc, "Iptc.Application2.City");
  m.iptc_sub = iptcStr(iptc, "Iptc.Application2.SubLocation");
  m.iptc_state = iptcStr(iptc, "Iptc.Application2.ProvinceState");
  m.iptc_ccode = iptcStr(iptc, "Iptc.Application2.CountryCode");
  m.iptc_cname = iptcStr(iptc, "Iptc.Application2.CountryName");
  m.iptc_copy = iptcStr(iptc, "Iptc.Application2.Copyright");

  // XMP
  m.x_addr_en = xmpStr(xmp, "Xmp.dc.AddressEnglish");
  m.x_addr_loc = xmpStr(xmp, "Xmp.dc.AddressLocal");
  m.x_ccode = xmpStr(xmp, "Xmp.dc.CountryCode");
  m.x_rights = xmpStr(xmp, "Xmp.dc.rights");
  m.x_title = xmpStr(xmp, "Xmp.dc.title");
  m.x_dt_orig = xmpStr(xmp, "Xmp.exif.DateTimeOriginal");
  m.x_city = xmpStr(xmp, "Xmp.photoshop.City");
  m.x_country = xmpStr(xmp, "Xmp.photoshop.Country");
  m.x_state = xmpStr(xmp, "Xmp.photoshop.State");
  m.x_ps_ccode = xmpStr(xmp, "Xmp.photoshop.CountryCode");
  m.x_cont = xmpStr(xmp, "Xmp.photoshop.Continent");
  m.x_cca2 = xmpStr(xmp, "Xmp.photoshop.Cca2");
  m.x_cca3 = xmpStr(xmp, "Xmp.photoshop.Cca3");
  m.x_capital = xmpStr(xmp, "Xmp.photoshop.Capital");
  m.x_tz = xmpStr(xmp, "Xmp.photoshop.Timezone");
  m.x_cowner = xmpStr(xmp, "Xmp.plus.CopyrightOwner");

  for (const auto &kv : xmp) {
    if (kv.key() == "Xmp.dc.subject")
      m.tags.push_back(kv.toString());
  }

  return m;
}

// -------------------- main --------------------

/**
 * @brief Main execution entry point for the photo_indexer.
 *
 * @param argc Count of arguments.
 * @param argv Command line arguments array.
 * @return int 0 on success, non-zero on failure.
 */
int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: photo_indexer <input-folder> <output-prefix> "
                 "[output-folder]\n";
    return 1;
  }

  fs::path input = argv[1];
  std::string outPrefix = argv[2];
  fs::path outFolder = (argc >= 4) ? fs::path(argv[3]) : fs::current_path();

  std::error_code ec;
  fs::create_directories(outFolder, ec);
  if (ec) {
    std::cerr << "Warning: could not create output folder " << outFolder << ": "
              << ec.message() << "\n";
  }

  // collect files
  std::vector<fs::path> files;
  for (auto &e : fs::recursive_directory_iterator(input)) {
    if (!e.is_regular_file())
      continue;
    auto ext = e.path().extension().string();
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG" ||
        ext == ".png" || ext == ".PNG" || ext == ".tiff" || ext == ".TIF" ||
        ext == ".avif" || ext == ".AVIF")
      files.push_back(e.path());
  }

  std::cout << "Found " << files.size() << " images\n";

  // parallel extraction
  std::vector<PhotoMeta> metas(files.size());
  std::atomic<uint64_t> counter{1};

  std::for_each(std::execution::par, files.begin(), files.end(),
                [&](const fs::path &p) {
                  uint64_t id = counter.fetch_add(1);
                  metas[id - 1] = extract_meta(id, p);
                });

  // compact results
  std::vector<PhotoMeta> final;
  final.reserve(metas.size());
  for (auto &m : metas)
    if (!m.file.empty())
      final.push_back(std::move(m));
  std::cout << "Processed " << final.size() << " images\n";

  // -------------------- FlatBuffers: metadata --------------------
  flatbuffers::FlatBufferBuilder fb(1024 * 1024);
  std::vector<flatbuffers::Offset<Metadata>> fbvec;
  fbvec.reserve(final.size());

  for (auto &m : final) {
    auto gps = CreateExifGps(
        fb, fb.CreateString(m.gps_lat_ref), fb.CreateVector(m.gps_lat),
        fb.CreateString(m.gps_lon_ref), fb.CreateVector(m.gps_lon),
        m.gps_alt_ref, m.gps_alt, fb.CreateString(m.gps_ts),
        fb.CreateString(m.gps_map));

    auto exif =
        CreateExif(fb, fb.CreateString(m.make), fb.CreateString(m.model),
                   fb.CreateString(m.dt), fb.CreateString(m.copyright),
                   fb.CreateString(m.dt_orig), gps);

    auto iptc = CreateIptc(
        fb, fb.CreateString(m.iptc_date), fb.CreateString(m.iptc_time),
        fb.CreateString(m.iptc_city), fb.CreateString(m.iptc_sub),
        fb.CreateString(m.iptc_state), fb.CreateString(m.iptc_ccode),
        fb.CreateString(m.iptc_cname), fb.CreateString(m.iptc_copy));

    std::vector<flatbuffers::Offset<flatbuffers::String>> subj;
    subj.reserve(m.tags.size());
    for (auto &t : m.tags)
      subj.push_back(fb.CreateString(t));

    auto xmp =
        CreateXmp(fb, fb.CreateString(m.x_addr_en),
                  fb.CreateString(m.x_addr_loc), fb.CreateString(m.x_ccode),
                  fb.CreateString(m.x_rights), fb.CreateVector(subj),
                  fb.CreateString(m.x_title), fb.CreateString(m.x_dt_orig),
                  fb.CreateString(m.x_city), fb.CreateString(m.x_country),
                  fb.CreateString(m.x_state), fb.CreateString(m.x_ps_ccode),
                  fb.CreateString(m.x_cont), fb.CreateString(m.x_cca2),
                  fb.CreateString(m.x_cca3), fb.CreateString(m.x_capital),
                  fb.CreateString(m.x_tz), fb.CreateString(m.x_cowner));

    fbvec.push_back(
        CreateMetadata(fb, m.id, fb.CreateString(m.file), exif, iptc, xmp));
  }

  fb.Finish(CreateMetadataList(fb, fb.CreateVector(fbvec)));

  {
    fs::path outFile = outFolder / (outPrefix + "_metadata.bin");
    std::ofstream ofs(outFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(fb.GetBufferPointer()),
              fb.GetSize());
    ofs.close();
    std::cout << "Wrote " << outFile << "\n";
  }

  // -------------------- Build indices (using XXH3 fingerprints for
  // camera/tags) --------------------
  std::unordered_map<uint32_t, std::vector<uint64_t>> isoIdx;
  std::unordered_map<uint32_t, std::vector<uint64_t>> dateIdx;
  std::unordered_map<uint64_t, std::vector<uint64_t>>
      camIdx; // fingerprint -> ids
  std::unordered_map<uint64_t, std::vector<uint64_t>>
      tagIdx; // fingerprint -> ids
  std::unordered_map<uint64_t, std::string> camNames;
  std::unordered_map<uint64_t, std::string> tagNames;

  for (auto &m : final) {
    if (m.iso)
      isoIdx[m.iso].push_back(m.id);
    if (m.date)
      dateIdx[m.date].push_back(m.id);
    if (!m.camera.empty()) {
      uint64_t f = fingerprint_xxh3(m.camera);
      camIdx[f].push_back(m.id);
      camNames.emplace(f, m.camera);
    }
    for (auto &t : m.tags) {
      uint64_t f = fingerprint_xxh3(t);
      tagIdx[f].push_back(m.id);
      tagNames.emplace(f, t);
    }
  }

  // ISO index
  {
    flatbuffers::FlatBufferBuilder b(1024 * 256);
    std::vector<flatbuffers::Offset<IsoEntry>> entries;
    entries.reserve(isoIdx.size());
    for (auto &kv : isoIdx)
      entries.push_back(CreateIsoEntry(b, kv.first, b.CreateVector(kv.second)));
    b.Finish(CreateIndexIso(b, b.CreateVector(entries)));
    fs::path outFile = outFolder / (outPrefix + "_index_iso.bin");
    std::ofstream ofs(outFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(b.GetBufferPointer()),
              b.GetSize());
    std::cout << "Wrote " << outFile << "\n";
  }

  // Date index
  {
    flatbuffers::FlatBufferBuilder b(1024 * 256);
    std::vector<flatbuffers::Offset<DateEntry>> entries;
    entries.reserve(dateIdx.size());
    for (auto &kv : dateIdx)
      entries.push_back(
          CreateDateEntry(b, kv.first, b.CreateVector(kv.second)));
    b.Finish(CreateIndexDate(b, b.CreateVector(entries)));
    fs::path outFile = outFolder / (outPrefix + "_index_date.bin");
    std::ofstream ofs(outFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(b.GetBufferPointer()),
              b.GetSize());
    std::cout << "Wrote " << outFile << "\n";
  }

  // Camera index (store readable camera_model strings)
  {
    flatbuffers::FlatBufferBuilder b(1024 * 256);
    std::vector<flatbuffers::Offset<CameraEntry>> entries;
    entries.reserve(camIdx.size());
    for (auto &kv : camIdx) {
      uint64_t f = kv.first;
      auto it = camNames.find(f);
      std::string camName =
          (it != camNames.end()) ? it->second : std::to_string(f);
      entries.push_back(CreateCameraEntry(b, b.CreateString(camName),
                                          b.CreateVector(kv.second)));
    }
    b.Finish(CreateIndexCamera(b, b.CreateVector(entries)));
    fs::path outFile = outFolder / (outPrefix + "_index_camera.bin");
    std::ofstream ofs(outFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(b.GetBufferPointer()),
              b.GetSize());
    std::cout << "Wrote " << outFile << "\n";
  }

  // Tag index
  {
    flatbuffers::FlatBufferBuilder b(1024 * 256);
    std::vector<flatbuffers::Offset<TagEntry>> entries;
    entries.reserve(tagIdx.size());
    for (auto &kv : tagIdx) {
      uint64_t f = kv.first;
      auto it = tagNames.find(f);
      std::string tagName =
          (it != tagNames.end()) ? it->second : std::to_string(f);
      entries.push_back(CreateTagEntry(b, b.CreateString(tagName),
                                       b.CreateVector(kv.second)));
    }
    b.Finish(CreateIndexTags(b, b.CreateVector(entries)));
    fs::path outFile = outFolder / (outPrefix + "_index_tags.bin");
    std::ofstream ofs(outFile, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(b.GetBufferPointer()),
              b.GetSize());
    std::cout << "Wrote " << outFile << "\n";
  }

  std::cout << "All done.\n";
  return 0;
}
