cmake_minimum_required(VERSION 3.25)
project(HongTuOsgbExporter LANGUAGES CXX)
add_executable(ht_osgb_exporter main.cpp)
target_compile_features(ht_osgb_exporter PRIVATE cxx_std_17)
target_include_directories(ht_osgb_exporter PRIVATE "${HT_OSG_SOURCE}/include" "${HT_OSG_BUILD}/include")
target_link_libraries(ht_osgb_exporter PRIVATE
  "${HT_OSG_BUILD}/lib/osg.lib" "${HT_OSG_BUILD}/lib/osgDB.lib"
  "${HT_OSG_BUILD}/lib/osgUtil.lib" "${HT_OSG_BUILD}/lib/OpenThreads.lib")
if(MSVC)
  target_compile_options(ht_osgb_exporter PRIVATE /utf-8)
endif()
