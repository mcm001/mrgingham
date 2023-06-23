#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include "opencv2/highgui.hpp"
#include "opencv2/calib3d.hpp"
#include <iostream>
using namespace cv;
using namespace std;
int main()
{
    std::string filename =
        "C:\\Users\\mcmorley\\Documents\\GitHub\\mrgingham\\testimgs\\1686868697564383507.jpeg";
    cv::Mat image = cv::imread(filename,
                               cv::IMREAD_IGNORE_ORIENTATION |
                                   cv::IMREAD_GRAYSCALE);

    Size patternsize(9, 7); // interior number of corners

    // CALIB_CB_FAST_CHECK saves a lot of time on images
    // that do not contain any chessboard corners
    vector<Point2f> corners; // this will be filled by the detected corners
    using std::cout;
    using std::endl;
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    using std::chrono::system_clock;
    for (int i = 0; i < 10; i++)
    {
        auto start = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        bool patternfound = findChessboardCorners(image, patternsize, corners,
                                                  CALIB_CB_ADAPTIVE_THRESH + CALIB_CB_NORMALIZE_IMAGE + CALIB_CB_FAST_CHECK);
        auto end = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        auto dt = end - start;
        std::cout << "dt: " << dt << " ms found? " << (patternfound ? "YES" : "NO") << endl;
    }

    cv::imshow("test 1", image);
    cv::waitKey();
}