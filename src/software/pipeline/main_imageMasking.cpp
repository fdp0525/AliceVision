// This file is part of the AliceVision project.
// Copyright (c) 2015 AliceVision contributors.
// Copyright (c) 2012 openMVG contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aliceVision/sfmData/SfMData.hpp>
#include <aliceVision/sfmDataIO/jsonIO.hpp>
#include <aliceVision/image/io.hpp>
#include <aliceVision/imageMasking/imageMasking.hpp>
#include <aliceVision/system/Logger.hpp>
#include <aliceVision/system/cmdline.hpp>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>


// These constants define the current software version.
// They must be updated when the command line is changed.
#define ALICEVISION_SOFTWARE_VERSION_MAJOR 1
#define ALICEVISION_SOFTWARE_VERSION_MINOR 0

using namespace aliceVision;

namespace po = boost::program_options;
namespace fs = boost::filesystem;

enum class EAlgorithm {
    HSV,
    GrabCut,
};

inline std::string EAlgorithm_enumToString(EAlgorithm value)
{
  switch(value)
  {
    case EAlgorithm::HSV:
      return "hsv";
    case EAlgorithm::GrabCut:
      return "grabcut";
  }
  throw std::out_of_range("Invalid Algorithm type Enum: " + std::to_string(int(value)));
}

inline EAlgorithm EAlgorithm_stringToEnum(const std::string& value)
{
  if(value == "hsv")
    return EAlgorithm::HSV;
  if(value == "grabcut")
    return EAlgorithm::GrabCut;
  throw std::out_of_range("Invalid Algorithm type string " + value);
}

inline std::ostream& operator<<(std::ostream& os, EAlgorithm s)
{
    return os << EAlgorithm_enumToString(s);
}

inline std::istream& operator>>(std::istream& in, EAlgorithm& s)
{
    std::string token;
    in >> token;
    s = EAlgorithm_stringToEnum(token);
    return in;
}

/**
 * @brief Write mask images from input images based on chosen algorithm.
 */
int main(int argc, char **argv)
{
  // command-line parameters
  std::string sfmFilePath;
  std::string outputFilePath;
  std::string verboseLevel = system::EVerboseLevel_enumToString(system::Logger::getDefaultVerboseLevel());

  // program range
  int rangeStart = 0;
  int rangeSize = -1;

  EAlgorithm algorithm = EAlgorithm::HSV;
  float hue = 0.33f, hueRange = 0.1f, minSaturation = 0.3f, minValue = 0.3f;

  po::options_description allParams("AliceVision masking");

  po::options_description requiredParams("Required parameters");
  requiredParams.add_options()
    ("input,i", po::value<std::string>(&sfmFilePath)->default_value(sfmFilePath),
      "A SfMData file (*.sfm) [if specified, --imageFolder cannot be used].")
    ("algorithm,a", po::value<EAlgorithm>(&algorithm)->default_value(algorithm),
      std::string("Masking algorithm:\n"
      " * " + EAlgorithm_enumToString(EAlgorithm::HSV) + ": selected range in the Hue Saturation Value color space.\n"
      " * " + EAlgorithm_enumToString(EAlgorithm::GrabCut) + ": not implemented"
      ).c_str())
    ("output,o", po::value<std::string>(&outputFilePath)->default_value("cameraInit.sfm"),
      "Output file path for the new SfMData file");

  po::options_description hsvParams("HSV parameters");
  hsvParams.add_options()
    ("hue", po::value<float>(&hue)->default_value(hue),
      "Hue value to isolate in [0,1] range. 0 = red, 0.33 = green, 0.66 = blue, 1 = red.")
    ("hueRange", po::value<float>(&hueRange)->default_value(hueRange),
      "Tolerance around the hue value to isolate.")
    ("minSaturation", po::value<float>(&minSaturation)->default_value(minSaturation),
      "Hue is meaningless if saturation is low. Do not mask pixels below this threshold.")
    ("minValue", po::value<float>(&minValue)->default_value(minValue),
      "Hue is meaningless if value is low. Do not mask pixels below this threshold.")
      ;

  po::options_description optionalParams("Optional parameters");
  optionalParams.add_options()
    ("rangeStart", po::value<int>(&rangeStart)->default_value(rangeStart),
      "Compute a sub-range of images from index rangeStart to rangeStart+rangeSize.")
    ("rangeSize", po::value<int>(&rangeSize)->default_value(rangeSize),
      "Compute a sub-range of N images (N=rangeSize).")
    ;

  po::options_description logParams("Log parameters");
  logParams.add_options()
    ("verboseLevel,v", po::value<std::string>(&verboseLevel)->default_value(verboseLevel),
      "verbosity level (fatal, error, warning, info, debug, trace).");

  allParams.add(requiredParams).add(hsvParams).add(optionalParams).add(logParams);

  po::variables_map vm;
  try
  {
    po::store(po::parse_command_line(argc, argv, allParams), vm);

    if(vm.count("help") || (argc == 1))
    {
      ALICEVISION_COUT(allParams);
      return EXIT_SUCCESS;
    }
    po::notify(vm);
  }
  catch(boost::program_options::required_option& e)
  {
    ALICEVISION_CERR("ERROR: " << e.what());
    ALICEVISION_COUT("Usage:\n\n" << allParams);
    return EXIT_FAILURE;
  }
  catch(boost::program_options::error& e)
  {
    ALICEVISION_CERR("ERROR: " << e.what());
    ALICEVISION_COUT("Usage:\n\n" << allParams);
    return EXIT_FAILURE;
  }

  ALICEVISION_COUT("Program called with the following parameters:");
  ALICEVISION_COUT(vm);

  // set verbose level
  system::Logger::get()->setLogLevel(verboseLevel);

  // check user choose at least one input option
  if(sfmFilePath.empty())
  {
    ALICEVISION_LOG_ERROR("Program need -i option" << std::endl << "No input images.");
    return EXIT_FAILURE;
  }

  // check sfm file
  if(!sfmFilePath.empty() && !fs::exists(sfmFilePath) && !fs::is_regular_file(sfmFilePath))
  {
    ALICEVISION_LOG_ERROR("The input sfm file doesn't exist");
    return EXIT_FAILURE;
  }

  sfmData::SfMData sfmData;
  if(!sfmDataIO::Load(sfmData, sfmFilePath, sfmDataIO::ESfMData::VIEWS))
  {
    ALICEVISION_LOG_ERROR("The input SfMData file '" + sfmFilePath + "' cannot be read.");
    return EXIT_FAILURE;
  }

  // check output string
  if(outputFilePath.empty())
  {
    ALICEVISION_LOG_ERROR("Invalid output");
    return EXIT_FAILURE;
  }

  // ensure output folder exists
  if(!outputFilePath.empty() && !fs::exists(outputFilePath))
  {
    if(!fs::create_directory(outputFilePath))
    {
      ALICEVISION_LOG_ERROR("Cannot create output folder");
      return EXIT_FAILURE;
    }
  }

  // check program range
  if(rangeStart < 0 || rangeStart >= sfmData.views.size())
  {
    ALICEVISION_LOG_ERROR("invalid subrange of views to process.");
    return EXIT_FAILURE;
  }

  // algorithms
  using OutImage = imageMasking::OutImage;
  using InImagePath = imageMasking::InImagePath;
  std::function<void(OutImage&, const InImagePath&)> process;
  if(algorithm == EAlgorithm::HSV)
  {
    // check options
    if(hue < 0.f || hue > 1.f)
    {
      ALICEVISION_LOG_ERROR("hue must be in the [0, 1] range");
      return EXIT_FAILURE;
    }

    if(minSaturation < 0.f || minSaturation > 1.f)
    {
      ALICEVISION_LOG_ERROR("minSaturation must be in the [0, 1] range");
      return EXIT_FAILURE;
    }

    if(minValue < 0.f || minValue > 1.f)
    {
      ALICEVISION_LOG_ERROR("minValue must be in the [0, 1] range");
      return EXIT_FAILURE;
    }

    process = [&](OutImage& result, const InImagePath& inputPath)
    {
      imageMasking::hsv(result, inputPath, hue, hueRange, minSaturation, minValue);
    };
  }

  if(!process)
  {
    ALICEVISION_LOG_ERROR("Invalid algorithm");
    return EXIT_FAILURE;
  }

  system::Timer timer;
  const auto& views = sfmData.getViews();
  const auto viewPairItBegin = views.begin();
  const int size = std::min(int(views.size()) - rangeStart, (rangeSize < 0 ? std::numeric_limits<int>::max() : rangeSize));

  #pragma omp parallel for
  for(int i = 0; i < size; ++i)
  {
    const auto& item = std::next(viewPairItBegin, rangeStart + i);
    const IndexT& index = item->first;
    const sfmData::View& view = *item->second;

    image::Image<unsigned char> result;
    process(result, view.getImagePath());

    const auto resultFilename = fs::path(view.getImagePath()).filename().replace_extension("png");
    const std::string resultPath = (fs::path(outputFilePath) / resultFilename).string();
    image::writeImage(resultPath, result, image::EImageColorSpace::LINEAR);
  }

  ALICEVISION_LOG_INFO("Task done in (s): " + std::to_string(timer.elapsed()));
  return EXIT_SUCCESS;
}
