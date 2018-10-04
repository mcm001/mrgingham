#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <assert.h>

#include "point.hh"
#include "mrgingham-internal.h"

extern "C"
{
#include "ChESS.h"
}

// The various tunable parameters

// When we find a connected component of high-enough corner responses, the peak
// must have a response at least this strong for the component to be accepted
#define RESPONSE_MIN_PEAK_THRESHOLD         200

// Corner responses must be at least this strong to be included into our
// connected component
#define RESPONSE_MIN_THRESHOLD              20

// Corner responses must be at least this strong to be included into our
// connected component. This is based on the maximum response we have so far
// encountered in our component
#define RESPONSE_MIN_THRESHOLD_RATIO_OF_MAX(response_max) (((uint16_t)(response_max)) >> 4)

// When looking at a candidate corner (peak of a connected component), we look
// at the variance of the intensities of the pixels in a region around the
// candidate corner. This has to be "relatively high". If we somehow end up
// looking at a region inside a chessboard square instead of on a corner, then
// the region will be relatively flat (same color), and the variance will be too
// low. These parameters set the size of this search window and the threshold
// for the standard deviation (sqrt(variance))
#define CONSTANCY_WINDOW_R                  5
#define STDEV_THRESHOLD                     25




#define VARIANCE_THRESHOLD                  (STDEV_THRESHOLD*STDEV_THRESHOLD)


using namespace mrgingham;


static bool high_variance( int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* image )
{
    if(x-CONSTANCY_WINDOW_R < 0 || x+CONSTANCY_WINDOW_R >= w ||
       y-CONSTANCY_WINDOW_R < 0 || y+CONSTANCY_WINDOW_R >= h )
    {
        // I give up on edges
        return false;
    }

    // I should be able to do this with opencv, but it's way too much of a pain
    // in my ass, so I do it myself
    int32_t sum = 0;
    for(int dy = -CONSTANCY_WINDOW_R; dy <=CONSTANCY_WINDOW_R; dy++)
        for(int dx = -CONSTANCY_WINDOW_R; dx <=CONSTANCY_WINDOW_R; dx++)
        {
            uint8_t val = image[ x+dx + (y+dy)*w ];
            sum += (int32_t)val;
        }

    int32_t mean = sum / ((1 + 2*CONSTANCY_WINDOW_R)*
                          (1 + 2*CONSTANCY_WINDOW_R));
    int32_t sum_deviation_sq = 0;
    for(int dy = -CONSTANCY_WINDOW_R; dy <=CONSTANCY_WINDOW_R; dy++)
        for(int dx = -CONSTANCY_WINDOW_R; dx <=CONSTANCY_WINDOW_R; dx++)
        {
            uint8_t val = image[ x+dx + (y+dy)*w ];
            int32_t deviation = (int32_t)val - mean;
            sum_deviation_sq += deviation*deviation;
        }

    int32_t var = sum_deviation_sq / ((1 + 2*CONSTANCY_WINDOW_R)*
                                      (1 + 2*CONSTANCY_WINDOW_R));

    // // to show the variances and empirically find the threshold
    // printf("%d %d %d\n", x, y, var);
    // return false;

    return var > VARIANCE_THRESHOLD;
}

// The point-list data structure for the connected-component traversal
struct xy_t { int16_t x, y; };
struct xylist_t
{
    struct xy_t* xy;
    int N;
};

static struct xylist_t xylist_alloc()
{
    struct xylist_t l = {};

    // start out large-enough for most use cases (should have connected
    // components with <10 pixels generally). Will realloc if really needed
    l.xy = (struct xy_t*)malloc( 128 * sizeof(struct xy_t) );

    return l;
}
static void xylist_reset_with(struct xylist_t* l, int16_t x, int16_t y)
{
    l->N = 1;
    l->xy[0].x = x;
    l->xy[0].y = y;
}
static void xylist_free(struct xylist_t* l)
{
    free(l->xy);
    l->N = -1;
}
static void xylist_push(struct xylist_t* l, int16_t x, int16_t y)
{
    l->N++;
    l->xy = (struct xy_t*)realloc(l->xy, l->N * sizeof(struct xy_t)); // no-op most of the time

    l->xy[l->N-1].x = x;
    l->xy[l->N-1].y = y;
}
static bool xylist_pop(struct xylist_t* l, int16_t *x, int16_t *y)
{
    if(l->N <= 0)
        return false;

    *x = l->xy[ l->N-1 ].x;
    *y = l->xy[ l->N-1 ].y;
    l->N--;
    return true;
}

static void mark_invalid(int16_t x, int16_t y, int16_t w, int16_t h, int16_t* d)
{
    d[x + y*w] = 0;
}


struct connected_component_t
{
    uint64_t sum_w_x, sum_w_y, sum_w;
    int N;

    // I keep track of the position and magnitude of the peak, and I reject all
    // points whose response is smaller than some (small) ratio of the max. Note
    // that the max is updated as I go, so it's possible to accumulate some
    // points that have become invalid (because the is_valid threshold has moved
    // with the max). However, since I weigh everything by the response value,
    // this won't be a strong effect: the incorrectly-accumulated points will
    // have a small weight
    uint16_t x_peak, y_peak;
    int16_t  response_max;
};
struct connected_component_t connected_component_init(void)
{
    struct connected_component_t c = {};
    return c;
}

static int16_t response_at(int16_t x, int16_t y, int16_t w, const int16_t* d)
{
    return d[x + y*w];
}
static bool is_valid(int16_t x, int16_t y, int16_t w, int16_t h, const int16_t* d,
                     const struct connected_component_t* c)
{
    int16_t response = response_at(x,y,w,d);

    return
        response > RESPONSE_MIN_THRESHOLD &&
        (c == NULL || response > RESPONSE_MIN_THRESHOLD_RATIO_OF_MAX(c->response_max));
}
static void accumulate(int16_t x, int16_t y, int16_t w, int16_t h, const int16_t* d,
                       struct connected_component_t* c)
{
    int16_t response = response_at(x,y,w,d);
    if( response > c->response_max)
    {
        c->response_max = response;
        c->x_peak       = x;
        c->y_peak       = y;
    }

    c->sum_w_x += response * x;
    c->sum_w_y += response * y;
    c->sum_w   += response;
    c->N++;


    // // to show the responses and empirically find the threshold
    // printf("%d %d %d\n", x, y, response);

}
static bool connected_component_is_valid(const struct connected_component_t* c,

                                         int16_t w, int16_t h,
                                         const uint8_t* image)
{
    // We're looking at a candidate peak. I don't want to find anything
    // inside a chessboard square, which the detector does sometimes. I
    // can detect this condition by looking at a local variance of the
    // original image at this point. The image will be relatively
    // constant in a chessboard square, and I throw out this candidate
    // then
    return
        c->N > 1                                      &&
        c->response_max > RESPONSE_MIN_PEAK_THRESHOLD &&
        high_variance(c->x_peak, c->y_peak,
                      w,h, image);
}
static void check_and_push_candidate(struct xylist_t* l,
                                     bool* touched_margin,
                                     int16_t x, int16_t y, int16_t w, int16_t h,
                                     const int16_t* d,
                                     int margin)
{
    if( !(x >= margin && x < w-margin &&
          y >= margin && y < h-margin ))
    {
        *touched_margin = true;
        return;
    }

    if( response_at(x, y, w,d) <= 0 )
        return;

    xylist_push(l, x, y);
}
static void follow_connected_component(struct xylist_t* l,
                                       int16_t w, int16_t h, int16_t* d,

                                       const uint8_t* image,
                                       std::vector<Point>* points,
                                       FILE* debugfp,
                                       uint16_t coord_scale,
                                       int margin)
{
    struct connected_component_t c = connected_component_init();

    bool touched_margin = false;

    int16_t x, y;
    while( xylist_pop(l, &x, &y))
    {
        if(!is_valid(x,y,w,h,d, &c))
            continue;

        accumulate  (x,y,w,h,d, &c);
        mark_invalid(x,y,w,h,d);

        check_and_push_candidate(l, &touched_margin, x+1, y,   w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x-1, y,   w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x,   y+1, w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x,   y-1, w,h,d,margin);
    }

    // If I touched the margin, this connected component is NOT valid
    if( !touched_margin &&
        connected_component_is_valid(&c, w,h,image) )
    {
        double x = (double)c.sum_w_x / (double)c.sum_w;
        double y = (double)c.sum_w_y / (double)c.sum_w;

        // My (x,y) coords here are based on a downsampled image, and I want to
        // up-sample them. An NxN image consists of a grid of NxN cells. The
        // MIDDLE of each cell is indexed by integer coords. Thus the top-left
        // corner of the image is at the top-left corner of the top-left cell at
        // coords (-0.5,-0.5) at ANY resolution. So (-0.5,-0.5) is a fixed point
        // of the scaling, not (0,0). Thus to change the scaling, I translate to
        // a coord system with its origin at (-0.5,-0.5), scale, and then
        // translate back
        x = (x + 0.5) * (double)coord_scale - 0.5;
        y = (y + 0.5) * (double)coord_scale - 0.5;

        if( debugfp )
            fprintf(debugfp, "%f %f\n", x, y);
        points->push_back( Point((int)(0.5 + x * FIND_GRID_SCALE),
                                 (int)(0.5 + y * FIND_GRID_SCALE)));
    }
}

static void process_connected_components(int w, int h, int16_t* d,

                                         const uint8_t* image,
                                         std::vector<Point>* points,
                                         FILE* debugfp,
                                         uint16_t coord_scale,
                                         int margin)
{
    struct xylist_t l = xylist_alloc();

    for(int16_t y = margin+1; y<h-margin-1; y++)
        for(int16_t x = margin+1; x<w-margin-1; x++)
        {
            if( !is_valid(x,y,w,h,d, NULL) )
                continue;

            xylist_reset_with(&l, x, y);
            follow_connected_component(&l, w,h,d,
                                       image, points, debugfp, coord_scale, margin);
        }
}

#define DUMP_FILENAME_CORNERS   "/tmp/mrgingham-1-corners.vnl"
#define SCALED_IMAGE_FILENAME   "/tmp/mrgingham-scaled.png"
#define CHESS_RESPONSE_FILENAME "/tmp/chess-response.png"
__attribute__((visibility("default")))
bool find_chessboard_corners_from_image_array( // out

                                               // integers scaled up by
                                               // FIND_GRID_SCALE to get more
                                               // resolution
                                               std::vector<mrgingham::Point>* points,

                                               // in
                                               const cv::Mat& image_input,

                                               // set to 0 to just use the image
                                               int image_pyramid_level,
                                               bool debug )
{
    if( image_pyramid_level < 0 ||

        // 10 is an arbitrary high number
        image_pyramid_level > 10 )
    {
        fprintf(stderr, "%s:%d in %s(): Got an unreasonable image_pyramid_level = %d."
                " Sorry.\n", __FILE__, __LINE__, __func__, image_pyramid_level);
        return false;
    }

    const cv::Mat* image;
    cv::Mat _image;

    if(image_pyramid_level == 0)
    {
        image = &image_input;
        if( debug )
            fprintf(stderr, "This is level-0 so I'm not rescaling the image, and not writing the scaled version to disk\n");
    }
    else
    {

        double scale = 1.0 / ((double)(1 << image_pyramid_level));
        cv::resize( image_input, _image, cv::Size(), scale, scale, cv::INTER_LINEAR );
        image = &_image;

        if( debug )
        {
            cv::imwrite(SCALED_IMAGE_FILENAME, *image);
            fprintf(stderr, "Wrote scaled image to " SCALED_IMAGE_FILENAME "\n");
        }
    }


    if( !image->isContinuous() )
    {
        fprintf(stderr, "%s:%d in %s(): I can only handle continuous arrays (stride == width) currently."
                " Sorry.\n", __FILE__, __LINE__, __func__);
        return false;
    }

    if( image->type() != CV_8U )
    {
        fprintf(stderr, "%s:%d in %s(): I can only handle CV_8U arrays currently."
                " Sorry.\n", __FILE__, __LINE__, __func__);
        return false;
    }

    const int w = image->cols;
    const int h = image->rows;
    cv::Mat response( h, w, CV_16S );

    uint8_t* imageData    = image->data;
    int16_t* responseData = (int16_t*)response.data;

    ChESS_response_5( responseData, imageData, w, h );

    if(debug)
    {
        cv::Mat out;
        cv::normalize(response, out, 0, 255, cv::NORM_MINMAX);
        cv::imwrite(CHESS_RESPONSE_FILENAME, out);

        fprintf(stderr, "Wrote a normalized ChESS response to " CHESS_RESPONSE_FILENAME "\n");

        // for( int y = 0; y < h; y++ )
        //     for( int x = 0; x < w; x++ )
        //         printf("%d %d %d\n", x, y, responseData[x+y*w] );

    }

    // I set all responses <0 to "0". These are not valid as candidates, and
    // I'll use "0" to mean "visited" in the upcoming connectivity search
    for( int xy = 0; xy < w*h; xy++ )
        if(responseData[xy] < 0)
            responseData[xy] = 0;

    // I have responses. I
    //
    // - Find local peaks
    // - Ignore invalid local peaks
    // - Find center-of-mass of the region around the local peak

    // This serves both to throw away duplicate nearby points at the same corner
    // and to provide sub-pixel-interpolation for the corner location
    FILE* debugfp = NULL;
    if(debug)
    {
        debugfp = fopen(DUMP_FILENAME_CORNERS, "w");
        assert(debugfp);

        fprintf(debugfp, "# x y\n");
    }
    process_connected_components(w, h, responseData,
                                 (uint8_t*)image->data, points,
                                 debugfp,
                                 1U << image_pyramid_level,

                                 // The ChESS response is invalid at a 7-pixel
                                 // margin around the image. This is a property
                                 // of the ChESS implementation. Anything that
                                 // needs to touch pixels in this 7-pixel-wide
                                 // ring is invalid
                                 7);
    if(debug)
    {
        fprintf(stderr, "Wrote a corner dump to " DUMP_FILENAME_CORNERS "\n");
        fclose(debugfp);
    }
    return true;
}

__attribute__((visibility("default")))
bool find_chessboard_corners_from_image_file( // out

                                              // integers scaled up by
                                              // FIND_GRID_SCALE to get more
                                              // resolution
                                              std::vector<mrgingham::Point>* points,

                                              // in
                                              const char* filename,

                                              // set to 0 to just use the image
                                              int image_pyramid_level,
                                              bool debug )
{
    cv::Mat image = cv::imread(filename, CV_LOAD_IMAGE_GRAYSCALE);
    if( image.data == NULL )
    {
        fprintf(stderr, "%s:%d in %s(): Couldn't open image '%s'."
                " Sorry.\n", __FILE__, __LINE__, __func__, filename);
        return false;
    }

    return find_chessboard_corners_from_image_array( points, image, image_pyramid_level, debug );
}
