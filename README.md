# Build requirements

On Windows, we need Boost and Opencv installed. I had to add `opencv/build/x64/vc16/bin` to my PATH, per the CMake warning. Point cmake to them with:

```
mkdir build
cd build
cmake .. -DBOOST_ROOT="C:\Users\mcmorley\Documents\boost_1_52_0\boost_1_52_0" -DOpenCV_DIR="C:\Users\mcmorley\Documents\opencv\opencv\build"
cmake --build . --config Release
```

We pull OpenCV from WPILib's thirdparty-opencv maven publication automatically. The architecture name needs to be passed in with `-DOPENCV_ARCH`, which is one of:

- linuxx86-64
- linuxarm32
- linuxarm64
- osxarm64
- osxuniversal
- osxx86-64
- windowsarm64
- windowsx86-64
