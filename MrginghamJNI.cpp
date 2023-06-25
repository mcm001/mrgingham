#include "MrginghamJNI.h"
#include "point.hh"
#include "mrgingham.hh"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

/**
 * Finds a class and keeps it as a global reference.
 *
 * Use with caution, as the destructor does NOT call DeleteGlobalRef due to
 * potential shutdown issues with doing so.
 */
class JClass
{
public:
    JClass() = default;

    JClass(JNIEnv *env, const char *name)
    {
        jclass local = env->FindClass(name);
        if (!local)
        {
            return;
        }
        m_cls = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
    }

    void free(JNIEnv *env)
    {
        if (m_cls)
        {
            env->DeleteGlobalRef(m_cls);
        }
        m_cls = nullptr;
    }

    explicit operator bool() const { return m_cls; }

    operator jclass() const { return m_cls; }

protected:
    jclass m_cls = nullptr;
};

extern "C"
{

    JClass detectionClass;

    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
    {
        JNIEnv *env;
        if (vm->GetEnv((void **)(&env), JNI_VERSION_1_6) != JNI_OK)
        {
            return JNI_ERR;
        }

        detectionClass = JClass(env, "org/mrgingham/PointDouble");

        if (!detectionClass)
        {
            printf("Couldn't find class!");
            return JNI_ERR;
        }

        return JNI_VERSION_1_6;
    }

    static jobject MakeJObject(JNIEnv *env, const mrgingham::PointDouble &point_out, const int refinement)
    {
        // Constructor signature must match Java! I = int, F = float, [D = double array
        static jmethodID constructor =
            env->GetMethodID(detectionClass, "<init>", "(DDI)V");

        if (!constructor)
        {
            return nullptr;
        }

        // Actually call the constructor
        auto ret = env->NewObject(
            detectionClass, constructor,
            (jdouble)point_out.x,
            (jdouble)point_out.y,
            (jint)refinement);

        // TODO we don't seem to need this... or at least, it doesnt leak rn
        // env->ReleaseDoubleArrayElements(harr, h, 0);
        // env->ReleaseDoubleArrayElements(carr, corners, 0);

        return ret;
    }

    JNIEXPORT jobjectArray JNICALL Java_org_mrgingham_MrginghamJNI_detectChessboardNative(JNIEnv *env, jclass, jlong matPtr, jboolean doclahe, jint blur_radius, jboolean do_refine, jint gridn)
    {
        static cv::Ptr<cv::CLAHE> clahe;

        cv::Mat &image = *(reinterpret_cast<cv::Mat *>(matPtr));

        if (doclahe && !clahe)
        {
            clahe = cv::createCLAHE();
            clahe->setClipLimit(8);
        }

        // The buffer. I'll realloc() this as I go. MUST free at the end
        signed char *refinement_level = NULL;

        if (doclahe)
        {
            // CLAHE doesn't by itself use the full dynamic range all the time.
            // I explicitly apply histogram equalization and then CLAHE
            cv::equalizeHist(image, image);
            clahe->apply(image, image);
        }
        if (blur_radius > 0)
        {
            cv::blur(image, image,
                     cv::Size(1 + 2 * blur_radius,
                              1 + 2 * blur_radius));
        }

        // Stuff I need to fill in. Not sure if this is all correct...
        int image_pyramid_level = -1;
        bool debug = false;
        mrgingham::debug_sequence_t debug_sequence{};

        std::vector<mrgingham::PointDouble> points_out;
        bool result;
        int found_pyramid_level; // need this because ctx.image_pyramid_level could be -1
        const char *debug_image_filename = nullptr;

        found_pyramid_level =
            mrgingham::find_chessboard_from_image_array(points_out,
                                                        do_refine ? &refinement_level : NULL,
                                                        gridn,
                                                        image,
                                                        image_pyramid_level,
                                                        debug, debug_sequence,
                                                        debug_image_filename);
        result = (found_pyramid_level >= 0);

        // printf("Found %i corners\n", points_out.size());

        // Object array to return to Java
        jobjectArray jarr = env->NewObjectArray(points_out.size(), detectionClass, nullptr);
        if (!jarr)
        {
            printf("Couldn't make array\n");
            return nullptr;
        }

        // Fill in the array with the results
        for (int i = 0; i < (int)points_out.size(); i++)
        {
            jobject obj = MakeJObject(env, points_out[i],
                                      (refinement_level == NULL) ? found_pyramid_level : (int)refinement_level[i]);

            env->SetObjectArrayElement(jarr, i, obj);
        }
        free(refinement_level);

        return jarr;
    }
}