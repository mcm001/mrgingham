#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "point.hh"
#include "mrgingham.hh"
#include "mrgingham_internal.h"

using namespace mrgingham;

void find_blobs_from_image_array( std::vector<Point>* points,
                                  const cv::Mat& image,
                                  bool dodump )
{
    cv::SimpleBlobDetector::Params blobDetectorParams;
    blobDetectorParams.minArea             = 40;
    blobDetectorParams.maxArea             = 80000;
    blobDetectorParams.minDistBetweenBlobs = 15;
    blobDetectorParams.blobColor           = 255; // white-on-black dots


    cv::SimpleBlobDetector blobDetector(blobDetectorParams);

    std::vector<cv::KeyPoint> keypoints;
    blobDetector.detect(image, keypoints);

    for(std::vector<cv::KeyPoint>::iterator it = keypoints.begin();
        it != keypoints.end();
        it++)
    {
        if( dodump )
        {
            printf("%f %f\n", it->pt.x, it->pt.y);
        }
        else
        {
            points->push_back( Point((int)(it->pt.x * FIND_GRID_SCALE + 0.5),
                                     (int)(it->pt.y * FIND_GRID_SCALE + 0.5)));
        }
    }
}

bool find_blobs_from_image_file( std::vector<Point>* points,
                                 const char* filename,
                                 bool dodump )
{
    cv::Mat image = cv::imread(filename, CV_LOAD_IMAGE_COLOR);
    if( image.data == NULL )
        return false;

    find_blobs_from_image_array( points, image, dodump );
    return true;
}
