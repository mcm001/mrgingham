class MrginghamJNI {
    class PointDouble {
        double x;
        double y;
        int refinementLevel;
    }

    private static native Object[] detectChessboardNative(
        long imageNativeObj,
        boolean doClAHE,
        int blurRadius,
        boolean do_refine
    );

    public static PointDouble[] detectChessboard(
        long imageNativeObj,
        boolean doClAHE,
        int blurRadius,
        boolean do_refine
    ) {
        return (PointDouble[]) detectChessboardNative(imageNativeObj, doClAHE, blurRadius, do_refine);
    }

}