
#include "Common.hpp"
#include "DebevecWeight.hpp"
#include "HDRImage.hpp"
#include "LinearLeastSquares.hpp"
#include "ReinhardAlgo.hpp"
#include "rawImage.hpp"

#include <opencv2/core/mat.hpp>
#include <opencv2/opencv.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <vector>

const std::string kDefaultBasePath = "../InputImage/";
const std::string kDefaultFileList = "list.txt";

// (1/shutter_speed)
// Note: factor of two
static constexpr std::array defaultShutterSpeed = {1 / 32, 1 / 16, 1 / 8, 1 / 4, 1 / 2, 1,   2,   4,
                                                   8,      16,     32,    64,    128,   256, 512, 1024};

static void outputCurve(const cv::Mat &curve) {
  auto tmpW = curve.size().width;
  auto tmpH = curve.size().height;

  std::ofstream fout("out.txt");

  for (auto q = 0U; q < tmpH; ++q) {
    for (auto p = 0U; p < tmpW; ++p) {
      fout << curve.at<double>(q, p) << std::endl;
    }
  }
}

static auto shrinkImages(const std::vector<HDRI::RawImage> &in) -> std::vector<cv::Mat> {

  std::vector<cv::Mat> out;

  const size_t kRatio = 100;

  for (const auto &img : in) {

    const auto &ref = img.getImageData();

    // size_t resizeCol = ref.cols / kRatio;
    // size_t resizeRow = ref.rows / kRatio;

    int resizeCol = 15;
    int resizeRow = 15;

    if (resizeCol < 15) {
      resizeCol = 15;
    }

    if (resizeRow < 15) {
      resizeRow = 15;
    }

    std::cerr << "sample Size:" << resizeCol << " " << resizeRow << '\n';

    cv::Mat shrinkMat;
    cv::resize(ref, shrinkMat, cv::Size(ref.cols, ref.rows));
    cv::resize(shrinkMat, shrinkMat, cv::Size(resizeCol, resizeRow));

    out.push_back(shrinkMat);
  }

  return out;
}

static auto generateRawPixelData(const std::vector<cv::Mat> &shrinkMat) -> std::vector<std::vector<PixelData>> {

  auto width = shrinkMat[0].size().width;
  auto height = shrinkMat[0].size().height;

  std::vector<std::vector<PixelData>> pixelRaw(shrinkMat.size());

  for (auto idx = 0U; idx < shrinkMat.size(); ++idx) {

    pixelRaw[idx].resize(width * height);
    for (auto y = 0; y < height; ++y) {
      for (auto x = 0; x < width; ++x) {

        pixelRaw[idx][y * width + x].b = shrinkMat[idx].at<cv::Vec3b>(y, x)[0];
        pixelRaw[idx][y * width + x].g = shrinkMat[idx].at<cv::Vec3b>(y, x)[1];
        pixelRaw[idx][y * width + x].r = shrinkMat[idx].at<cv::Vec3b>(y, x)[2];
      }
    }
  }

  return pixelRaw;
}

static auto convertToZ(const std::vector<std::vector<PixelData>> &pixelRaw, const size_t imageSize,
                       const size_t numOfImage) -> std::array<std::vector<std::vector<int>>, 3> {

  std::array<std::vector<std::vector<int>>, 3> Z; // r, g, b

  for (auto &zColors : Z) {
    zColors.resize(imageSize);
  }

  for (size_t i = 0; i < imageSize; ++i) { // image pixel

    Z[0][i].resize(numOfImage);
    Z[1][i].resize(numOfImage);
    Z[2][i].resize(numOfImage);

    for (size_t j = 0; j < numOfImage; ++j) { // num of iamge
      Z[0][i][j] = pixelRaw[j][i].b;
      Z[1][i][j] = pixelRaw[j][i].g;
      Z[2][i][j] = pixelRaw[j][i].r;
    }
  }

  return Z;
}

auto main(int argc, char *argv[]) -> int {

  std::vector<HDRI::RawImage> imageFiles;

  std::string basePath;
  if (argc > 1) {
    basePath = argv[1];
  } else {
    basePath = kDefaultBasePath;
  }

  std::string fileList;
  if (argc > 2) {
    fileList = argv[2];
  } else {
    fileList = kDefaultFileList;
  }

  auto start_time = std::chrono::high_resolution_clock::now();

  try {
    loadRawImages(basePath, fileList, imageFiles);
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    std::exit(-1);
  }

  HDRI::DebevecWeight dwf;

  auto shrinkMat = shrinkImages(imageFiles);
  std::vector<std::vector<PixelData>> pixelRaw = generateRawPixelData(shrinkMat);

  std::cerr << "Convert\n";

  // convert
  auto Z = convertToZ(pixelRaw, shrinkMat[0].total(), shrinkMat.size());

  // set exp
  std::vector<double> expo;
  expo.reserve(imageFiles.size());

  for (const auto &img : imageFiles) {
    expo.push_back(img.getExposure());
  }

  std::cerr << "Linear Least Squares\n";
  constexpr int lambda = 10;
  std::array<cv::Mat, 3> gCurves; // R, G, B

  std::array<std::future<cv::Mat>, 3> gFutures;
  for (auto c = 0U; c < Z.size(); ++c) {
    gFutures[c] = std::async(std::launch::async, HDRI::LinearLeastSquares::solver, Z[c], expo, dwf, lambda);
    std::cerr << "Async Compute\n";
  }

  for (auto c = 0U; c < Z.size(); ++c) {
    gCurves[c] = gFutures[c].get();
  }

  std::cerr << "Done\n";

  // test
  outputCurve(gCurves[0]);

  std::cerr << "Compute radiance\n";
  // radiance ?
  // cv::Mat hdrImg = constructRadiance(imageFiles, gCurves, dwf, expo);

  HDRI::HDRImage hdrImage;
  hdrImage.computeRadiance(imageFiles, gCurves, dwf, expo);

  try {
    cv::imwrite("radiance.hdr", hdrImage.getRadiance());
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    std::exit(-1);
  }

  // tone map

  std::cerr << "tone map\n";

  HDRI::ReinhardAlgo rAlgo;

  hdrImage.setToneMappingAlgorithm(&rAlgo);
  auto outimage = hdrImage.getToneMappingResult();

  try {
    cv::imwrite("output_image.jpg", outimage);
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    std::exit(-1);
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto time_span = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time);

  std::cout << "Execution time : " << time_span.count() << "s\n";

  return 0;
}
