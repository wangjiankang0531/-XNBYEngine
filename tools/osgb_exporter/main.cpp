#include <osg/Geode>
#include <osg/Group>
#include <osg/Texture>
#include <osg/TriangleFunctor>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgUtil/Optimizer>

#include <fstream>
#include <iostream>
#include <map>

struct CountTriangle {
  unsigned long long count = 0;
  void operator()(const osg::Vec3&, const osg::Vec3&, const osg::Vec3&, bool = false) {
    ++count;
  }
};

class SourceTriangles : public osg::NodeVisitor {
public:
  SourceTriangles() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {
  }
  void apply(osg::Geode& geode) override {
    for (unsigned i = 0; i < geode.getNumDrawables(); ++i) {
      osg::TriangleFunctor<CountTriangle> counter;
      geode.getDrawable(i)->accept(counter);
      count += counter.count;
    }
    traverse(geode);
  }
  unsigned long long count = 0;
};

// Rewrite only the in-memory export copy. Embedded historical absolute image
// names must never determine where the OBJ writer writes output textures.
class LocalImages : public osg::NodeVisitor {
public:
  LocalImages() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {
  }
  void rename(osg::StateSet* state) {
    if (!state)
      return;
    for (unsigned unit = 0; unit < state->getTextureAttributeList().size(); ++unit) {
      auto* texture = dynamic_cast<osg::Texture*>(state->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
      if (!texture)
        continue;
      for (unsigned face = 0; face < texture->getNumImages(); ++face) {
        auto* image = texture->getImage(face);
        if (!image)
          continue;
        if (!image->data())
          throw std::runtime_error("referenced texture has no embedded pixels");
        auto result = images.emplace(image, images.size());
        image->setFileName("texture_" + std::to_string(result.first->second) + ".rgb");
      }
    }
  }
  void apply(osg::Node& node) override {
    rename(node.getStateSet());
    traverse(node);
  }
  void apply(osg::Geode& geode) override {
    rename(geode.getStateSet());
    for (unsigned i = 0; i < geode.getNumDrawables(); ++i)
      rename(geode.getDrawable(i)->getStateSet());
    traverse(geode);
  }
  std::map<osg::Image*, size_t> images;
};

int main(int argc, char** argv) {
  try {
    if (argc != 3 && argc != 4)
      throw std::runtime_error("usage: ht_osgb_exporter source-list.txt output.obj [source-triangle-counts.tsv]");
    std::ifstream list(argv[1]);
    if (!list)
      throw std::runtime_error("cannot read source list");
    osg::ref_ptr<osg::Group> group = new osg::Group;
    std::ofstream audit;
    if (argc == 4) {
      audit.open(argv[3]);
      if (!audit)
        throw std::runtime_error("cannot write source triangle audit");
    }
    std::string path;
    while (std::getline(list, path)) {
      if (!path.empty() && path.back() == '\r')
        path.pop_back();
      if (path.empty())
        continue;
      auto node = osgDB::readRefNodeFile(path);
      if (!node)
        throw std::runtime_error("cannot read source node: " + path);
      if (audit) {
        SourceTriangles triangles;
        node->accept(triangles);
        audit << path << '\t' << triangles.count << '\n';
      }
      group->addChild(node);
    }
    if (!group->getNumChildren())
      throw std::runtime_error("empty source list");
    // Native-frontier auditing requires one source group per file. The default
    // optimizer merges adjacent drawables across those source boundaries.
    if (argc == 3) {
      osgUtil::Optimizer optimizer;
      optimizer.optimize(group);
    }
    LocalImages visitor;
    group->accept(visitor);
    osg::ref_ptr<osgDB::Options> options = new osgDB::Options("OutputTextureFiles");
    if (!osgDB::writeNodeFile(*group, argv[2], options))
      throw std::runtime_error("OBJ export failed");
    std::cout << "Exported " << group->getNumChildren() << " nodes, " << visitor.images.size()
              << " textures with local filenames\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
