#include <hongtu/gis/shapefile_reader.hpp>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace hongtu::gis {

// Little-endian read helpers
static i32 readI32LE(const u8* p) {
  return static_cast<i32>(p[0]) | (static_cast<i32>(p[1]) << 8) |
         (static_cast<i32>(p[2]) << 16) | (static_cast<i32>(p[3]) << 24);
}

static i32 readI32BE(const u8* p) {
  return (static_cast<i32>(p[0]) << 24) | (static_cast<i32>(p[1]) << 16) |
         (static_cast<i32>(p[2]) << 8) | static_cast<i32>(p[3]);
}

static f64 readF64LE(const u8* p) {
  f64 val;
  std::memcpy(&val, p, 8);
  return val;
}

// ============================================================================
// SHP Parsing
// ============================================================================

bool ShapefileReader::parseSHP(const u8* data, size_t size) {
  if (size < 100) { error_ = "SHP file too small"; return false; }

  // Verify file code (0x0000270A = 9994 big-endian)
  i32 file_code = readI32BE(data);
  if (file_code != 9994) { error_ = "Invalid SHP file code"; return false; }

  shape_type_ = static_cast<ShapeType>(readI32LE(data + 32));

  // Bounding box
  bounds_[0] = readF64LE(data + 36);  // min X
  bounds_[1] = readF64LE(data + 44);  // min Y
  bounds_[2] = readF64LE(data + 52);  // max X
  bounds_[3] = readF64LE(data + 60);  // max Y

  // Parse records
  size_t offset = 100;
  i64 fid = 0;
  while (offset + 8 < size) {
    i32 record_length = readI32BE(data + offset + 4) * 2; // In 16-bit words -> bytes
    offset += 8; // Skip record header

    if (offset + static_cast<size_t>(record_length) > size) break;

    const u8* rec = data + offset;
    i32 rec_shape_type = readI32LE(rec);
    bool has_z = (rec_shape_type >= 11 && rec_shape_type <= 18);

    Geometry geom;
    switch (static_cast<ShapeType>(rec_shape_type)) {
      case ShapeType::Point:
      case ShapeType::PointZ:
      case ShapeType::PointM:
        geom = parsePointRecord(rec + 4, record_length - 4, has_z);
        break;
      case ShapeType::PolyLine:
      case ShapeType::PolyLineZ:
      case ShapeType::PolyLineM:
        geom = parsePolyRecord(rec + 4, record_length - 4, GeometryType::LineString, has_z);
        break;
      case ShapeType::Polygon:
      case ShapeType::PolygonZ:
      case ShapeType::PolygonM:
        geom = parsePolyRecord(rec + 4, record_length - 4, GeometryType::Polygon, has_z);
        break;
      case ShapeType::MultiPoint:
      case ShapeType::MultiPointZ:
      case ShapeType::MultiPointM:
        geom = parseMultiPointRecord(rec + 4, record_length - 4, has_z);
        break;
      case ShapeType::Null:
        geom.type = GeometryType::Unknown;
        break;
      default:
        geom.type = GeometryType::Unknown;
        break;
    }

    Feature feature;
    feature.geometry = std::move(geom);
    feature.fid = fid++;
    features_.features.push_back(std::move(feature));

    offset += record_length;
  }

  return true;
}

Geometry ShapefileReader::parsePointRecord(const u8* data, size_t size, bool has_z) {
  Geometry geom;
  geom.type = GeometryType::Point;
  if (size < 16) return geom;

  geom.point.x = readF64LE(data);
  geom.point.y = readF64LE(data + 8);
  if (has_z && size >= 24) {
    geom.point.z = readF64LE(data + 16);
    geom.point.has_z = true;
  }
  return geom;
}

Geometry ShapefileReader::parsePolyRecord(const u8* data, size_t size, GeometryType type, bool has_z) {
  Geometry geom;
  geom.type = type;
  if (size < 40) return geom;

  // Skip bounding box (32 bytes)
  i32 num_parts = readI32LE(data + 32);
  i32 num_points = readI32LE(data + 36);

  if (num_parts <= 0 || num_points <= 0) return geom;

  const u8* parts_ptr = data + 40;
  const u8* points_ptr = parts_ptr + num_parts * 4;

  // Read part offsets
  std::vector<i32> part_offsets(num_parts);
  for (i32 i = 0; i < num_parts; ++i) {
    part_offsets[i] = readI32LE(parts_ptr + i * 4);
  }

  // Read points into rings
  for (i32 p = 0; p < num_parts; ++p) {
    i32 start = part_offsets[p];
    i32 end = (p + 1 < num_parts) ? part_offsets[p + 1] : num_points;

    Ring ring;
    for (i32 i = start; i < end; ++i) {
      GeoPoint pt;
      pt.x = readF64LE(points_ptr + i * 16);
      pt.y = readF64LE(points_ptr + i * 16 + 8);
      ring.push_back(pt);
    }
    geom.rings.push_back(std::move(ring));
  }

  // Read Z values if present
  if (has_z) {
    size_t z_offset = 40 + num_parts * 4 + num_points * 16 + 16; // Skip Z range
    if (z_offset + static_cast<size_t>(num_points) * 8 <= size) {
      i32 pt_idx = 0;
      for (auto& ring : geom.rings) {
        for (auto& pt : ring) {
          pt.z = readF64LE(data + z_offset + pt_idx * 8);
          pt.has_z = true;
          ++pt_idx;
        }
      }
    }
  }

  return geom;
}

Geometry ShapefileReader::parseMultiPointRecord(const u8* data, size_t size, bool has_z) {
  Geometry geom;
  geom.type = GeometryType::MultiPoint;
  if (size < 40) return geom;

  // Skip bounding box (32 bytes)
  i32 num_points = readI32LE(data + 32);
  const u8* points_ptr = data + 36;

  for (i32 i = 0; i < num_points; ++i) {
    GeoPoint pt;
    pt.x = readF64LE(points_ptr + i * 16);
    pt.y = readF64LE(points_ptr + i * 16 + 8);
    geom.points.push_back(pt);
  }

  if (has_z) {
    size_t z_offset = 36 + num_points * 16 + 16;
    if (z_offset + static_cast<size_t>(num_points) * 8 <= size) {
      for (i32 i = 0; i < num_points; ++i) {
        geom.points[i].z = readF64LE(data + z_offset + i * 8);
        geom.points[i].has_z = true;
      }
    }
  }

  return geom;
}

// ============================================================================
// DBF Parsing
// ============================================================================

bool ShapefileReader::parseDBF(const u8* data, size_t size) {
  if (size < 32) { error_ = "DBF file too small"; return false; }

  u8 version = data[0];
  if ((version & 0x07) != 3 && (version & 0x07) != 4) {
    // Accept dBASE III/IV variants
  }

  i32 num_records = readI32LE(data + 4);
  i16 header_size = static_cast<i16>(readI32LE(data + 8) & 0xFFFF);
  i16 record_size = static_cast<i16>(readI32LE(data + 10) & 0xFFFF);

  // Parse field descriptors (32 bytes each, starting at offset 32)
  fields_.clear();
  size_t field_offset = 32;
  while (field_offset + 32 <= static_cast<size_t>(header_size) && data[field_offset] != 0x0D) {
    DBFField field;
    field.name = std::string(reinterpret_cast<const char*>(data + field_offset), 11);
    // Trim null bytes
    field.name.erase(field.name.find_last_not_of('\0') + 1);
    field.type = static_cast<char>(data[field_offset + 11]);
    field.length = data[field_offset + 16];
    field.decimal_count = data[field_offset + 17];
    fields_.push_back(field);
    features_.field_names.push_back(field.name);
    field_offset += 32;
  }

  // Parse records
  size_t rec_offset = header_size;
  for (i32 r = 0; r < num_records && rec_offset + record_size <= size; ++r) {
    if (data[rec_offset] == '*') {
      rec_offset += record_size;
      continue; // Deleted record
    }

    size_t col_offset = rec_offset + 1; // Skip deletion flag
    if (static_cast<size_t>(r) < features_.features.size()) {
      auto& feature = features_.features[r];
      for (const auto& field : fields_) {
        std::string raw(reinterpret_cast<const char*>(data + col_offset), field.length);
        // Trim whitespace
        auto start = raw.find_first_not_of(' ');
        auto end = raw.find_last_not_of(' ');
        if (start != std::string::npos) {
          raw = raw.substr(start, end - start + 1);
        } else {
          raw.clear();
        }

        switch (field.type) {
          case 'N':
          case 'F':
            if (!raw.empty()) {
              if (field.decimal_count > 0 || raw.find('.') != std::string::npos) {
                feature.properties[field.name] = std::stod(raw);
              } else {
                feature.properties[field.name] = static_cast<i64>(std::stoll(raw));
              }
            }
            break;
          case 'L':
            feature.properties[field.name] = (raw == "T" || raw == "Y" || raw == "t" || raw == "y");
            break;
          default: // C, D, etc.
            feature.properties[field.name] = raw;
            break;
        }
        col_offset += field.length;
      }
    }
    rec_offset += record_size;
  }

  return true;
}

// ============================================================================
// Public Interface
// ============================================================================

bool ShapefileReader::open(const std::string& base_path) {
  auto readFile = [](const std::string& path) -> std::vector<u8> {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    file.seekg(0);
    std::vector<u8> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
  };

  auto shp_data = readFile(base_path + ".shp");
  auto dbf_data = readFile(base_path + ".dbf");

  if (shp_data.empty()) { error_ = "Cannot read .shp file: " + base_path + ".shp"; return false; }

  return openFromBuffers(shp_data, dbf_data);
}

bool ShapefileReader::openFromBuffers(const std::vector<u8>& shp_data,
                                       const std::vector<u8>& dbf_data) {
  features_ = {};
  fields_.clear();
  error_.clear();

  if (!parseSHP(shp_data.data(), shp_data.size())) return false;
  if (!dbf_data.empty()) {
    parseDBF(dbf_data.data(), dbf_data.size());
  }
  return true;
}

void ShapefileReader::getFileBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const {
  min_x = bounds_[0]; min_y = bounds_[1];
  max_x = bounds_[2]; max_y = bounds_[3];
}

} // namespace hongtu::gis
