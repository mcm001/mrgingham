# Build requirements

On Windows, we need Boost and Opencv installed. I had to add `opencv/build/x64/vc16/bin` to my PATH, per the CMake warning. Point cmake to them with:

```
mkdir build
cd build
cmake .. -DBOOST_ROOT="C:\Users\mcmorley\Documents\boost_1_52_0\boost_1_52_0" -DOpenCV_DIR="C:\Users\mcmorley\Documents\opencv\opencv\build"
cmake --build . --config Release
```

On Linux, just install libboost-dev and libopencv-dev, and build like normal.
