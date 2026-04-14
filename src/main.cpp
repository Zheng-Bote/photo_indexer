#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <execution>
#include <atomic>
#include <fstream>

#include <exiv2/exiv2.hpp>
#include "metadata_generated.h"

namespace fs = std::filesystem;
using namespace gallery;

// ------------------------------------------------------------
// Hilfsfunktionen
// ------------------------------------------------------------

std::string exifStr(const Exiv2::ExifData& exif, const char* key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    return it == exif.end() ? "" : it->toString();
}

std::string iptcStr(const Exiv2::IptcData& iptc, const char* key) {
    auto it = iptc.findKey(Exiv2::IptcKey(key));
    return it == iptc.end() ? "" : it->toString();
}

std::string xmpStr(const Exiv2::XmpData& xmp, const char* key) {
    auto it = xmp.findKey(Exiv2::XmpKey(key));
    return it == xmp.end() ? "" : it->toString();
}

std::vector<double> gpsArray(const Exiv2::ExifData& exif, const char* key) {
    std::vector<double> out;
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end()) return out;
    for (int i = 0; i < it->count(); ++i) {
        auto r = it->toRational(i);
        out.push_back(double(r.first) / r.second);
    }
    return out;
}

double gpsAlt(const Exiv2::ExifData& exif, const char* key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end()) return 0.0;
    auto r = it->toRational();
    return double(r.first) / r.second;
}

uint32_t parseDate(const std::string& s) {
    if (s.size() < 10) return 0;
    try {
        return std::stoi(s.substr(0,4)) * 10000 +
               std::stoi(s.substr(5,2)) * 100 +
               std::stoi(s.substr(8,2));
    } catch (...) { return 0; }
}

// ------------------------------------------------------------
// Datenstruktur
// ------------------------------------------------------------

struct PhotoMeta {
    uint64_t id;
    std::string file;

    // Indexfelder
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

// ------------------------------------------------------------
// Metadaten extrahieren
// ------------------------------------------------------------

PhotoMeta extract(uint64_t id, const fs::path& p) {
    PhotoMeta m;
    m.id = id;
    m.file = p.string();

    auto img = Exiv2::ImageFactory::open(m.file);
    if (!img) return m;
    img->readMetadata();

    const auto& exif = img->exifData();
    const auto& iptc = img->iptcData();
    const auto& xmp  = img->xmpData();

    // EXIF
    m.make     = exifStr(exif, "Exif.Image.Make");
    m.model    = exifStr(exif, "Exif.Image.Model");
    m.dt       = exifStr(exif, "Exif.Image.DateTime");
    m.dt_orig  = exifStr(exif, "Exif.Photo.DateTimeOriginal");
    m.copyright= exifStr(exif, "Exif.Image.Copyright");

    m.camera = m.model;
    m.date   = parseDate(m.dt_orig);

    auto itISO = exif.findKey(Exiv2::ExifKey("Exif.Photo.ISOSpeedRatings"));
    if (itISO != exif.end()) m.iso = itISO->toInt64();

    m.gps_lat_ref = exifStr(exif, "Exif.GPSInfo.GPSLatitudeRef");
    m.gps_lon_ref = exifStr(exif, "Exif.GPSInfo.GPSLongitudeRef");
    m.gps_lat     = gpsArray(exif, "Exif.GPSInfo.GPSLatitude");
    m.gps_lon     = gpsArray(exif, "Exif.GPSInfo.GPSLongitude");
    m.gps_alt     = gpsAlt(exif, "Exif.GPSInfo.GPSAltitude");
    m.gps_ts      = exifStr(exif, "Exif.GPSInfo.GPSTimeStamp");
    m.gps_map     = exifStr(exif, "Exif.GPSInfo.GPSMapDatum");

    auto itAltRef = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitudeRef"));
    if (itAltRef != exif.end()) m.gps_alt_ref = itAltRef->toInt64();

    // IPTC
    m.iptc_date  = iptcStr(iptc, "Iptc.Application2.DateCreated");
    m.iptc_time  = iptcStr(iptc, "Iptc.Application2.TimeCreated");
    m.iptc_city  = iptcStr(iptc, "Iptc.Application2.City");
    m.iptc_sub   = iptcStr(iptc, "Iptc.Application2.SubLocation");
    m.iptc_state = iptcStr(iptc, "Iptc.Application2.ProvinceState");
    m.iptc_ccode = iptcStr(iptc, "Iptc.Application2.CountryCode");
    m.iptc_cname = iptcStr(iptc, "Iptc.Application2.CountryName");
    m.iptc_copy  = iptcStr(iptc, "Iptc.Application2.Copyright");

    // XMP
    m.x_addr_en  = xmpStr(xmp, "Xmp.dc.AddressEnglish");
    m.x_addr_loc = xmpStr(xmp, "Xmp.dc.AddressLocal");
    m.x_ccode    = xmpStr(xmp, "Xmp.dc.CountryCode");
    m.x_rights   = xmpStr(xmp, "Xmp.dc.rights");
    m.x_title    = xmpStr(xmp, "Xmp.dc.title");
    m.x_dt_orig  = xmpStr(xmp, "Xmp.exif.DateTimeOriginal");
    m.x_city     = xmpStr(xmp, "Xmp.photoshop.City");
    m.x_country  = xmpStr(xmp, "Xmp.photoshop.Country");
    m.x_state    = xmpStr(xmp, "Xmp.photoshop.State");
    m.x_ps_ccode = xmpStr(xmp, "Xmp.photoshop.CountryCode");
    m.x_cont     = xmpStr(xmp, "Xmp.photoshop.Continent");
    m.x_cca2     = xmpStr(xmp, "Xmp.photoshop.Cca2");
    m.x_cca3     = xmpStr(xmp, "Xmp.photoshop.Cca3");
    m.x_capital  = xmpStr(xmp, "Xmp.photoshop.Capital");
    m.x_tz       = xmpStr(xmp, "Xmp.photoshop.Timezone");
    m.x_cowner   = xmpStr(xmp, "Xmp.plus.CopyrightOwner");

    for (const auto& kv : xmp)
        if (kv.key() == "Xmp.dc.subject")
            m.tags.push_back(kv.toString());

    return m;
}

// ------------------------------------------------------------
// Hauptprogramm
// ------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: photo_indexer <input-folder> <output-prefix>\n";
        return 1;
    }

    fs::path input = argv[1];
    std::string out = argv[2];

    // Dateien sammeln
    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(input)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG")
            files.push_back(e.path());
    }

    std::cout << "Found " << files.size() << " images\n";

    // Parallel verarbeiten
    std::vector<PhotoMeta> metas(files.size());
    std::atomic<uint64_t> counter{1};

    std::for_each(std::execution::par, files.begin(), files.end(),
        [&](const fs::path& p) {
            uint64_t id = counter.fetch_add(1);
            metas[id - 1] = extract(id, p);
        }
    );

    // Ungültige entfernen
    std::vector<PhotoMeta> final;
    for (auto& m : metas)
        if (!m.file.empty())
            final.push_back(std::move(m));

    std::cout << "Processed " << final.size() << " images\n";

    // --------------------------------------------------------
    // FlatBuffers: MetadataList
    // --------------------------------------------------------

    flatbuffers::FlatBufferBuilder fb(1024 * 1024);
    std::vector<flatbuffers::Offset<Metadata>> vec;

    for (auto& m : final) {
        auto exif = CreateExif(
            fb,
            fb.CreateString(m.make),
            fb.CreateString(m.model),
            fb.CreateString(m.dt),
            fb.CreateString(m.copyright),
            fb.CreateString(m.dt_orig),
            CreateExifGps(
                fb,
                fb.CreateString(m.gps_lat_ref),
                fb.CreateVector(m.gps_lat),
                fb.CreateString(m.gps_lon_ref),
                fb.CreateVector(m.gps_lon),
                m.gps_alt_ref,
                m.gps_alt,
                fb.CreateString(m.gps_ts),
                fb.CreateString(m.gps_map)
            )
        );

        auto iptc = CreateIptc(
            fb,
            fb.CreateString(m.iptc_date),
            fb.CreateString(m.iptc_time),
            fb.CreateString(m.iptc_city),
            fb.CreateString(m.iptc_sub),
            fb.CreateString(m.iptc_state),
            fb.CreateString(m.iptc_ccode),
            fb.CreateString(m.iptc_cname),
            fb.CreateString(m.iptc_copy)
        );

        std::vector<flatbuffers::Offset<flatbuffers::String>> tags;
        for (auto& t : m.tags) tags.push_back(fb.CreateString(t));

        auto xmp = CreateXmp(
            fb,
            fb.CreateString(m.x_addr_en),
            fb.CreateString(m.x_addr_loc),
            fb.CreateString(m.x_ccode),
            fb.CreateString(m.x_rights),
            fb.CreateVector(tags),
            fb.CreateString(m.x_title),
            fb.CreateString(m.x_dt_orig),
            fb.CreateString(m.x_city),
            fb.CreateString(m.x_country),
            fb.CreateString(m.x_state),
            fb.CreateString(m.x_ps_ccode),
            fb.CreateString(m.x_cont),
            fb.CreateString(m.x_cca2),
            fb.CreateString(m.x_cca3),
            fb.CreateString(m.x_capital),
            fb.CreateString(m.x_tz),
            fb.CreateString(m.x_cowner)
        );

        vec.push_back(CreateMetadata(
            fb,
            m.id,
            fb.CreateString(m.file),
            exif,
            iptc,
            xmp
        ));
    }

    fb.Finish(CreateMetadataList(fb, fb.CreateVector(vec)));

    {
        std::ofstream ofs(out + "_metadata.bin", std::ios::binary);
        ofs.write((char*)fb.GetBufferPointer(), fb.GetSize());
    }

    // --------------------------------------------------------
    // Indexe
    // --------------------------------------------------------

    std::unordered_map<uint32_t, std::vector<uint64_t>> isoIdx;
    std::unordered_map<uint32_t, std::vector<uint64_t>> dateIdx;
    std::unordered_map<std::string, std::vector<uint64_t>> camIdx;
    std::unordered_map<std::string, std::vector<uint64_t>> tagIdx;

    for (auto& m : final) {
        if (m.iso) isoIdx[m.iso].push_back(m.id);
        if (m.date) dateIdx[m.date].push_back(m.id);
        if (!m.camera.empty()) camIdx[m.camera].push_back(m.id);
        for (auto& t : m.tags) tagIdx[t].push_back(m.id);
    }

    // ISO Index
    {
        flatbuffers::FlatBufferBuilder b;
        std::vector<flatbuffers::Offset<IsoEntry>> entries;
        for (auto& [iso, ids] : isoIdx)
            entries.push_back(CreateIsoEntry(b, iso, b.CreateVector(ids)));
        b.Finish(CreateIndexIso(b, b.CreateVector(entries)));
        std::ofstream ofs(out + "_index_iso.bin", std::ios::binary);
        ofs.write((char*)b.GetBufferPointer(), b.GetSize());
    }

    // Date Index
    {
        flatbuffers::FlatBufferBuilder b;
        std::vector<flatbuffers::Offset<DateEntry>> entries;
        for (auto& [d, ids] : dateIdx)
            entries.push_back(CreateDateEntry(b, d, b.CreateVector(ids)));
        b.Finish(CreateIndexDate(b, b.CreateVector(entries)));
        std::ofstream ofs(out + "_index_date.bin", std::ios::binary);
        ofs.write((char*)b.GetBufferPointer(), b.GetSize());
    }

    // Camera Index
    {
        flatbuffers::FlatBufferBuilder b;
        std::vector<flatbuffers::Offset<CameraEntry>> entries;
        for (auto& [cam, ids] : camIdx)
            entries.push_back(CreateCameraEntry(b, b.CreateString(cam), b.CreateVector(ids)));
        b.Finish(CreateIndexCamera(b, b.CreateVector(entries)));
        std::ofstream ofs(out + "_index_camera.bin", std::ios::binary);
        ofs.write((char*)b.GetBufferPointer(), b.GetSize());
    }

    // Tag Index
    {
        flatbuffers::FlatBufferBuilder b;
        std::vector<flatbuffers::Offset<TagEntry>> entries;
        for (auto& [tag, ids] : tagIdx)
            entries.push_back(CreateTagEntry(b, b.CreateString(tag), b.CreateVector(ids)));
        b.Finish(CreateIndexTags(b, b.CreateVector(entries)));
        std::ofstream ofs(out + "_index_tags.bin", std::ios::binary);
        ofs.write((char*)b.GetBufferPointer(), b.GetSize());
    }

    std::cout << "Done.\n";
    return 0;
}
