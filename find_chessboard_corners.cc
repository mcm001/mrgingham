#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <assert.h>
#include <sys/stat.h>

#include "point.hh"
#include "mrgingham-internal.h"
#include "windows_defines.h"

#include "ChESS.h"

// The various tunable parameters

// When we find a connected component of high-enough corner responses, the peak
// must have a response at least this strong for the component to be accepted
#define RESPONSE_MIN_PEAK_THRESHOLD         120

// Corner responses must be at least this strong to be included into our
// connected component
#define RESPONSE_MIN_THRESHOLD              15

// Corner responses must be at least this strong to be included into our
// connected component. This is based on the maximum response we have so far
// encountered in our component
#define RESPONSE_MIN_THRESHOLD_RATIO_OF_MAX(response_max) (((uint16_t)(response_max)) >> 4)

#define CONNECTED_COMPONENT_MIN_SIZE        2

// When looking at a candidate corner (peak of a connected component), we look
// at the variance of the intensities of the pixels in a region around the
// candidate corner. This has to be "relatively high". If we somehow end up
// looking at a region inside a chessboard square instead of on a corner, then
// the region will be relatively flat (same color), and the variance will be too
// low. These parameters set the size of this search window and the threshold
// for the standard deviation (sqrt(variance))
#define CONSTANCY_WINDOW_R                  10
#define STDEV_THRESHOLD                     20




#define VARIANCE_THRESHOLD                  (STDEV_THRESHOLD*STDEV_THRESHOLD)


using namespace mrgingham;
namespace mrgingham {

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
static void xylist_reset(struct xylist_t* l)
{
    l->N = 0;
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
    l->xy = NULL;
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

typedef struct
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
}  connected_component_t;

static bool is_valid(int16_t x, int16_t y, int16_t w, int16_t h, const int16_t* d,
                     const connected_component_t* c)
{
    if(x<0 || x>=w ||
       y<0 || y>=h)
        return false;

    int16_t response = d[x+y*w];

    return
        response > RESPONSE_MIN_THRESHOLD &&
        (c == NULL || response > RESPONSE_MIN_THRESHOLD_RATIO_OF_MAX(c->response_max));
}
static void accumulate(int16_t x, int16_t y, int16_t w, int16_t h, const int16_t* d,
                       connected_component_t* c)
{
    int16_t response = d[x+y*w];
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
static bool connected_component_is_valid(const connected_component_t* c,

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
        c->N >= CONNECTED_COMPONENT_MIN_SIZE          &&
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

    if( d[x+y*w] <= 0 )
        return;

    xylist_push(l, x, y);
}
static bool follow_connected_component(PointDouble* out,

                                       struct xylist_t* l,
                                       int16_t w, int16_t h, int16_t* d,

                                       const uint8_t* image,
                                       int margin)
{
    connected_component_t c = {};

    bool touched_margin = false;

    int16_t x, y;
    while( xylist_pop(l, &x, &y))
    {
        if(!is_valid(x,y,w,h,d, &c))
        {
            d[x + y*w] = 0; // mark invalid; just in case
            continue;
        }

        accumulate  (x,y,w,h,d, &c);
        d[x + y*w] = 0; // mark invalid

        check_and_push_candidate(l, &touched_margin, x+1, y,   w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x-1, y,   w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x,   y+1, w,h,d,margin);
        check_and_push_candidate(l, &touched_margin, x,   y-1, w,h,d,margin);
    }

    // If I touched the margin, this connected component is NOT valid
    if( !touched_margin &&
        connected_component_is_valid(&c, w,h,image) )
    {
        out->x = (double)c.sum_w_x / (double)c.sum_w;
        out->y = (double)c.sum_w_y / (double)c.sum_w;
        return true;
    }
    return false;
}

static PointDouble scale_image_coord(const PointDouble* pt, double scale)
{
    // My (x,y) coords here are based on a downsampled image, and I want to
    // up-sample them. An NxN image consists of a grid of NxN cells. The MIDDLE
    // of each cell is indexed by integer coords. Thus the top-left corner of
    // the image is at the top-left corner of the top-left cell at coords
    // (-0.5,-0.5) at ANY resolution. So (-0.5,-0.5) is a fixed point of the
    // scaling, not (0,0). Thus to change the scaling, I translate to a coord
    // system with its origin at (-0.5,-0.5), scale, and then translate back
    return PointDouble( (pt->x + 0.5) * scale - 0.5,
                        (pt->y + 0.5) * scale - 0.5 );
}

#define DUMP_FILENAME_CORNERS_BASE   "/tmp/mrgingham-1-corners"
#define DUMP_FILENAME_CORNERS        DUMP_FILENAME_CORNERS_BASE ".vnl"
static int process_connected_components(int w, int h, int16_t* d,

                                        const uint8_t* image,
                                        std::vector<PointInt>* points_scaled_out,
                                        std::vector<mrgingham::PointDouble>* points_refinement,
                                        signed char*                         level_refinement,
                                        bool debug, const char* debug_image_filename,
                                        int image_pyramid_level,
                                        int margin)
{
    FILE* debugfp = NULL;
    const char* debug_filename = NULL;
    if(debug)
    {
        if(points_refinement == NULL)
            debug_filename = DUMP_FILENAME_CORNERS;
        else
        {
            char filename[256];
            sprintf(filename, DUMP_FILENAME_CORNERS_BASE "-refinement-level%d.vnl", image_pyramid_level);
            debug_filename = filename;
        }
        fprintf(stderr, "Writing self-plotting corner dump to %s\n", debug_filename);

        debugfp = fopen(debug_filename, "w");
        assert(debugfp);
        if(debug_image_filename != NULL)
            fprintf(debugfp, "#!/usr/bin/feedgnuplot --dom --with 'points pt 7 ps 2' --square --image %s\n", debug_image_filename);
        else
            fprintf(debugfp, "#!/usr/bin/feedgnuplot --dom --square --set 'yr [:] rev'\n");
        fprintf(debugfp, "# x y\n");
    }



    uint16_t coord_scale = 1U << image_pyramid_level;

    struct xylist_t l = xylist_alloc();

    int N = 0;

    // I assume that points_scaled_out and points_refinement aren't both non-NULL

    // I loop through all the pixels in the image. For each one I expand it into
    // the connected component that contains it. If I'm refining, I only look
    // for the connected component around the points I'm interested in
    if(points_scaled_out != NULL)
    {
        for(int16_t y = margin+1; y<h-margin-1; y++)
            for(int16_t x = margin+1; x<w-margin-1; x++)
            {
                if( !is_valid(x,y,w,h,d, NULL) )
                    continue;

                xylist_reset_with(&l, x, y);

                PointDouble pt;
                if( follow_connected_component(&pt,
                                               &l, w,h,d,
                                               image,
                                               margin) )
                {
                    pt = scale_image_coord(&pt, (double)coord_scale);
                    if( debugfp )
                        fprintf(debugfp, "%f %f\n", pt.x, pt.y);

                    points_scaled_out->push_back(PointInt((int)(0.5 + pt.x * FIND_GRID_SCALE),
                                                          (int)(0.5 + pt.y * FIND_GRID_SCALE)));
                }
            }
        N = points_scaled_out->size();
    }
    else if(points_refinement != NULL)
    {
        for(unsigned i=0; i<points_refinement->size(); i++)
        {
            // I can only refine the current estimate if it was computed at one
            // level higher than what I'm at now
            if( level_refinement[i] != image_pyramid_level+1 )
                continue;

            PointDouble& pt_full = (*points_refinement)[i];

            // The point pt indexes the full-size image, while the
            // connected-component stuff looks at a downsampled image. I convert
            PointDouble pt_downsampled = scale_image_coord(&pt_full, 1.0 / coord_scale);

            int x = (int)(pt_downsampled.x + 0.5);
            int y = (int)(pt_downsampled.y + 0.5);

            // I seed my refinement from the 3x3 neighborhood around the
            // previous center point. It is possible for the ChESS response
            // right in the center to be invalid, and I'll then not be able to
            // refine the point at all
            xylist_reset(&l);
            for(int dx = -1; dx<=1; dx++)
                for(int dy = -1; dy<=1; dy++)
                    if( is_valid(x+dx,y+dy,w,h,d, NULL))
                        xylist_push(&l,x+dx,y+dy);

            PointDouble pt;
            if(follow_connected_component(&pt,
                                          &l, w,h,d,
                                          image,
                                          margin))
            {
                pt_full = scale_image_coord(&pt, (double)coord_scale);
                if( debugfp )
                    fprintf(debugfp, "%f %f\n", pt_full.x, pt_full.y);
                level_refinement[i] = image_pyramid_level;
                N++;
            }
        }
    }

    xylist_free(&l);

    if(debug)
    {
        fclose(debugfp);
#ifdef WPI_LINUX
        chmod(debug_filename,
              S_IRUSR | S_IRGRP | S_IROTH |
              S_IWUSR | S_IWGRP |
              S_IXUSR | S_IXGRP | S_IXOTH);
#endif
    }

    return N;
}

// returns a scaled image, or NULL on failure
#define SCALED_PROCESSED_IMAGE_FILENAME   "/tmp/mrgingham-scaled-processed-level%d.png"
static const cv::Mat*
apply_image_pyramid_scaling(// out

                            // This MAY be used for the output image. The
                            // pointer returned by this function is what the
                            // caller should use. The caller should provide a
                            // cv::Mat object that this function can use for its
                            // purposes. When the caller is done with the scaled
                            // image, they may free this object
                            cv::Mat& image_buffer_output,

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
        return NULL;
    }

    const cv::Mat* image;

    if(image_pyramid_level == 0)
        image = &image_input;
    else
    {
        double scale = 1.0 / ((double)(1 << image_pyramid_level));
        cv::resize( image_input, image_buffer_output, cv::Size(), scale, scale, cv::INTER_LINEAR );
        image = &image_buffer_output;
    }
    if( debug )
    {
        char filename[256];
        sprintf(filename, SCALED_PROCESSED_IMAGE_FILENAME, image_pyramid_level);
        cv::imwrite(filename, *image);
        fprintf(stderr, "Wrote scaled,processed image to %s\n", filename);
    }

    if( !image->isContinuous() )
    {
        fprintf(stderr, "%s:%d in %s(): I can only handle continuous arrays (stride == width) currently."
                " Sorry.\n", __FILE__, __LINE__, __func__);
        return NULL;
    }

    if( image->type() != CV_8U )
    {
        fprintf(stderr, "%s:%d in %s(): I can only handle CV_8U arrays currently."
                " Sorry.\n", __FILE__, __LINE__, __func__);
        return NULL;
    }

    return image;
}

#define CHESS_RESPONSE_FILENAME                     "/tmp/mrgingham-chess-response%s-level%d.png"
#define CHESS_RESPONSE_POSITIVE_FILENAME            "/tmp/mrgingham-chess-response%s-level%d-positive.png"

int _find_or_refine_chessboard_corners_from_image_array ( // out
                                                          std::vector<mrgingham::PointInt>* points_scaled_out,

                                                          std::vector<mrgingham::PointDouble>* points_refinement,
                                                          signed char*                         level_refinement,

                                                          // in
                                                          const cv::Mat& image_input,

                                                          int image_pyramid_level,
                                                          bool debug,
                                                          const char* debug_image_filename)
{
    cv::Mat _image;
    const cv::Mat* image = apply_image_pyramid_scaling(_image,
                                                       image_input, image_pyramid_level,
                                                       debug);
    if( image == NULL ) return 0;

    const int w = image->cols;
    const int h = image->rows;

    // I don't NEED to zero this out, but it makes the debugging easier.
    // Otherwise the edges will contain uninitialized garbage, and the actual
    // data will be hard to see in the debug images
    cv::Mat response = cv::Mat::zeros( cv::Size(w, h), CV_16S );

    uint8_t* imageData    = image->data;
    int16_t* responseData = (int16_t*)response.data;

    mrgingham_ChESS_response_5( responseData, imageData, w, h, w );

    if(debug)
    {
        cv::Mat out;
        cv::normalize(response, out, 0, 255, cv::NORM_MINMAX);
        char filename[256];
        sprintf(filename, CHESS_RESPONSE_FILENAME,
                (points_refinement==NULL) ? "" : "-refinement",
                image_pyramid_level);
        cv::imwrite(filename, out);
        fprintf(stderr, "Wrote a normalized ChESS response to %s\n", filename);
    }

    // I set all responses <0 to "0". These are not valid as candidates, and
    // I'll use "0" to mean "visited" in the upcoming connectivity search
    for( int xy = 0; xy < w*h; xy++ )
        if(responseData[xy] < 0)
            responseData[xy] = 0;

    if(debug)
    {
        cv::Mat out;
        cv::normalize(response, out, 0, 255, cv::NORM_MINMAX);
        char filename[256];
        sprintf(filename, CHESS_RESPONSE_POSITIVE_FILENAME,
                (points_refinement==NULL) ? "" : "-refinement",
                image_pyramid_level);
        cv::imwrite(filename, out);
        fprintf(stderr, "Wrote positive-only, normalized ChESS response to %s\n", filename);
    }

    // I have responses. I
    //
    // - Find local peaks
    // - Ignore invalid local peaks
    // - Find center-of-mass of the region around the local peak

    // This serves both to throw away duplicate nearby points at the same corner
    // and to provide sub-pixel-interpolation for the corner location
    return
        process_connected_components(w, h, responseData,
                                     (uint8_t*)image->data,
                                     points_scaled_out,
                                     points_refinement, level_refinement,
                                     debug, debug_image_filename,
                                     image_pyramid_level,

                                     // The ChESS response is invalid at a 7-pixel
                                     // margin around the image. This is a property
                                     // of the ChESS implementation. Anything that
                                     // needs to touch pixels in this 7-pixel-wide
                                     // ring is invalid
                                     7);
}

// WPI_EXPORT
bool find_chessboard_corners_from_image_array( // out

                                              // integers scaled up by
                                              // FIND_GRID_SCALE to get more
                                              // resolution
                                              std::vector<mrgingham::PointInt>* points_scaled_out,

                                              // in
                                              const cv::Mat& image_input,

                                              // set to 0 to just use the image
                                              int image_pyramid_level,
                                              bool debug,
                                              const char* debug_image_filename)
{
    return
        _find_or_refine_chessboard_corners_from_image_array(points_scaled_out, NULL, NULL,
                                                            image_input, image_pyramid_level,
                                                            debug, debug_image_filename) > 0;
}

// Returns how many points were refined
// WPI_EXPORT
int refine_chessboard_corners_from_image_array( // out/int

                                                // initial coordinates on input,
                                                // refined coordinates on output
                                                std::vector<mrgingham::PointDouble>* points,

                                                // level[ipoint] is the
                                                // decimation level used to
                                                // compute that point.
                                                // if(level[ipoint] ==
                                                // image_pyramid_level+1) then
                                                // that point could be refined.
                                                // If I successfully refine a
                                                // point, I update level[ipoint]
                                                signed char* level,

                                                // in
                                                const cv::Mat& image_input,

                                                int image_pyramid_level,
                                                bool debug,
                                                const char* debug_image_filename)
{
    return
        _find_or_refine_chessboard_corners_from_image_array( NULL,
                                                             points, level,
                                                             image_input, image_pyramid_level,
                                                             debug, debug_image_filename);
}


WPI_EXPORT
bool find_chessboard_corners_from_image_file( // out

                                              // integers scaled up by
                                              // FIND_GRID_SCALE to get more
                                              // resolution
                                              std::vector<mrgingham::PointInt>* points,

                                              // in
                                              const char* filename,

                                              // set to 0 to just use the image
                                              int image_pyramid_level,
                                              bool debug )
{
    cv::Mat image = cv::imread(filename,
                               cv::IMREAD_IGNORE_ORIENTATION |
                               cv::IMREAD_GRAYSCALE);
    if( image.data == NULL )
    {
        fprintf(stderr, "%s:%d in %s(): Couldn't open image '%s'."
                " Sorry.\n", __FILE__, __LINE__, __func__, filename);
        return false;
    }

    return find_chessboard_corners_from_image_array( points, image, image_pyramid_level, debug, filename );
}

}
