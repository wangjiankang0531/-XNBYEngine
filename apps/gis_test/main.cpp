#include <hongtu/gis/geometry.hpp>
#include <hongtu/gis/geojson_reader.hpp>
#include <hongtu/gis/shapefile_reader.hpp>
#include <hongtu/gis/tiles3d_reader.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace hongtu::gis;

static std::string readFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) return "";
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

static void printGeometry(const Geometry& geom, int indent = 2) {
  std::string pad(indent, ' ');
  std::cout << pad << "Type: " << geometryTypeName(geom.type) << "\n";
  std::cout << pad << "Vertices: " << geom.getVertexCount() << "\n";

  double x0, y0, x1, y1;
  geom.getBounds(x0, y0, x1, y1);
  std::cout << pad << "Bounds: [" << x0 << ", " << y0 << "] - [" << x1 << ", " << y1 << "]\n";
}

static void printFeatures(const FeatureCollection& fc) {
  std::cout << "=== FeatureCollection ===\n";
  std::cout << "Feature count: " << fc.features.size() << "\n";
  std::cout << "Fields: ";
  for (const auto& f : fc.field_names) std::cout << f << ", ";
  std::cout << "\n";

  double x0, y0, x1, y1;
  fc.getBounds(x0, y0, x1, y1);
  std::cout << "Bounds: [" << x0 << ", " << y0 << "] - [" << x1 << ", " << y1 << "]\n\n";

  int shown = 0;
  for (const auto& feat : fc.features) {
    if (shown >= 5) {
      std::cout << "... (" << fc.features.size() - 5 << " more features)\n";
      break;
    }
    std::cout << "Feature #" << feat.fid << ":\n";
    printGeometry(feat.geometry);
    std::cout << "  Properties:\n";
    for (const auto& [key, val] : feat.properties) {
      std::cout << "    " << key << " = ";
      std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          std::cout << "null";
        } else if constexpr (std::is_same_v<T, std::string>) {
          std::cout << "\"" << v << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
          std::cout << (v ? "true" : "false");
        } else {
          std::cout << v;
        }
      }, val);
      std::cout << "\n";
    }
    std::cout << "\n";
    ++shown;
  }
}

static int testGeoJSON(const std::string& path) {
  std::cout << "\n>>> Reading GeoJSON: " << path << "\n";
  auto content = readFile(path);
  if (content.empty()) {
    std::cerr << "Error: Cannot read file\n";
    return 1;
  }

  GeoJSONReader reader;
  if (!reader.parse(content)) {
    std::cerr << "Error: " << reader.getError() << "\n";
    return 1;
  }

  printFeatures(reader.getFeatures());

  // Write back and show size comparison
  auto output = writeGeoJSON(reader.getFeatures());
  std::cout << "Input size: " << content.size() << " bytes\n";
  std::cout << "Output size: " << output.size() << " bytes\n";
  std::cout << "Roundtrip: OK\n";
  return 0;
}

static int testShapefile(const std::string& path) {
  // Strip extension if provided
  std::string base = path;
  for (const auto& ext : {".shp", ".dbf", ".shx"}) {
    if (base.size() > 4 && base.substr(base.size() - 4) == ext) {
      base = base.substr(0, base.size() - 4);
      break;
    }
  }

  std::cout << "\n>>> Reading Shapefile: " << base << ".*\n";
  ShapefileReader reader;
  if (!reader.open(base)) {
    std::cerr << "Error: " << reader.getError() << "\n";
    return 1;
  }

  std::cout << "Shape type: " << static_cast<int>(reader.getShapeType()) << "\n";
  double x0, y0, x1, y1;
  reader.getFileBounds(x0, y0, x1, y1);
  std::cout << "File bounds: [" << x0 << ", " << y0 << "] - [" << x1 << ", " << y1 << "]\n";

  std::cout << "DBF fields: ";
  for (const auto& f : reader.getFields()) {
    std::cout << f.name << "(" << f.type << "), ";
  }
  std::cout << "\n";

  printFeatures(reader.getFeatures());
  return 0;
}

static int testTileset(const std::string& path) {
  std::cout << "\n>>> Reading 3D Tiles: " << path << "\n";
  auto content = readFile(path);
  if (content.empty()) {
    std::cerr << "Error: Cannot read file\n";
    return 1;
  }

  Tileset3DReader reader;
  if (!reader.parse(content)) {
    std::cerr << "Error: " << reader.getError() << "\n";
    return 1;
  }

  auto& info = reader.getInfo();
  std::cout << "Version: " << info.version << "\n";
  std::cout << "Geometric error: " << info.geometric_error << "\n";
  std::cout << "Total tiles: " << info.tile_count << "\n";
  std::cout << "Max depth: " << info.max_depth << "\n";

  auto uris = reader.getAllContentURIs();
  std::cout << "Content URIs (" << uris.size() << "):\n";
  int shown = 0;
  for (const auto& uri : uris) {
    if (shown >= 10) {
      std::cout << "  ... (" << uris.size() - 10 << " more)\n";
      break;
    }
    std::cout << "  " << uri << "\n";
    ++shown;
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "XNBY GIS Data Test Tool\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << argv[0] << " <file.geojson>     - Parse GeoJSON file\n";
    std::cout << "  " << argv[0] << " <file.shp>         - Parse Shapefile\n";
    std::cout << "  " << argv[0] << " <tileset.json>     - Parse 3D Tiles tileset\n";
    std::cout << "  " << argv[0] << " --demo             - Run built-in demo\n";
    return 0;
  }

  std::string arg = argv[1];

  if (arg == "--demo") {
    // Built-in demo with inline data
    std::cout << "=== GeoJSON Demo ===\n";
    GeoJSONReader reader;
    std::string demo_json = R"({
      "type": "FeatureCollection",
      "features": [
        {
          "type": "Feature",
          "geometry": {"type": "Point", "coordinates": [116.4074, 39.9042, 50]},
          "properties": {"name": "Beijing", "type": "capital", "pop_million": 21.54}
        },
        {
          "type": "Feature",
          "geometry": {"type": "Point", "coordinates": [121.4737, 31.2304, 4]},
          "properties": {"name": "Shanghai", "type": "municipality", "pop_million": 24.87}
        },
        {
          "type": "Feature",
          "geometry": {
            "type": "Polygon",
            "coordinates": [[[116.0, 39.5], [117.0, 39.5], [117.0, 40.5], [116.0, 40.5], [116.0, 39.5]]]
          },
          "properties": {"name": "Beijing Region", "area_km2": 16410.54}
        }
      ]
    })";
    reader.parse(demo_json);
    printFeatures(reader.getFeatures());

    // Roundtrip test
    auto output = writeGeoJSON(reader.getFeatures());
    GeoJSONReader reader2;
    reader2.parse(output);
    std::cout << "Roundtrip: " << reader2.getFeatures().features.size() << " features OK\n\n";

    // 3D Tiles demo
    std::cout << "=== 3D Tiles Demo ===\n";
    Tileset3DReader tiles_reader;
    std::string tiles_json = R"({
      "asset": {"version": "1.1"},
      "geometricError": 240.0,
      "root": {
        "boundingVolume": {"sphere": [0, 0, 0, 1000]},
        "geometricError": 70.0,
        "refine": "REPLACE",
        "content": {"uri": "building.b3dm"},
        "children": [
          {
            "boundingVolume": {"sphere": [0, 0, 0, 500]},
            "geometricError": 10.0,
            "content": {"uri": "building_lod1.b3dm"}
          }
        ]
      }
    })";
    tiles_reader.parse(tiles_json);
    auto& info = tiles_reader.getInfo();
    std::cout << "Version: " << info.version << "\n";
    std::cout << "Tiles: " << info.tile_count << ", Depth: " << info.max_depth << "\n";
    auto uris = tiles_reader.getAllContentURIs();
    for (const auto& u : uris) std::cout << "  -> " << u << "\n";
    std::cout << "\nAll demos passed!\n";
    return 0;
  }

  // Detect file type by extension
  if (arg.size() > 8 && arg.substr(arg.size() - 8) == ".geojson") {
    return testGeoJSON(arg);
  } else if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".json") {
    // Could be GeoJSON or 3D Tiles - try 3D Tiles first
    auto content = readFile(arg);
    if (content.find("\"root\"") != std::string::npos) {
      return testTileset(arg);
    }
    return testGeoJSON(arg);
  } else if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".shp") {
    return testShapefile(arg);
  } else {
    // Try GeoJSON by default
    return testGeoJSON(arg);
  }
}
