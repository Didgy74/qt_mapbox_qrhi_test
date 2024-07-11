ANDROID_SDK_HOME=C:\Users\np_sk\AppData\Local\Android\Sdk
ANDROID_NDK_HOME=C:\Users\np_sk\AppData\Local\Android\Sdk\ndk\25.1.8937393
VCPKG_ROOT=C:\vcpkg

-DCMAKE_TOOLCHAIN_FILE:FILEPATH=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=C:\Users\np_sk\AppData\Local\Android\Sdk\ndk\25.1.8937393\build\cmake\android.toolchain.cmake
-DVCPKG_TARGET_TRIPLET=arm64-android
-DANDROID_ABI=arm64-v8a (?)



cmake -S C:/Users/np_sk/Nextcloud/Documents/Projects/qtquicktest -B C:/Users/np_sk/Nextcloud/Documents/Projects/qtquicktest/build/Android_Qt_6_8_0_Clang_arm64_v8a-Debug "-DANDROID_SDK_ROOT:PATH=C:/Users/np_sk/AppData/Local/Android/Sdk" "-DCMAKE_C_COMPILER:FILEPATH=C:/Users/np_sk/AppData/Local/Android/Sdk/ndk/25.1.8937393/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" "-DCMAKE_PROJECT_INCLUDE_BEFORE:FILEPATH=C:\Users\np_sk\Nextcloud\Documents\Projects\qtquicktest\build\Android_Qt_6_8_0_Clang_arm64_v8a-Debug/.qtc/package-manager/auto-setup.cmake" "-DANDROID_PLATFORM:STRING=android-23" "-DANDROID_ABI:STRING=arm64-v8a" "-DCMAKE_BUILD_TYPE:STRING=Debug" "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_FIND_ROOT_PATH:PATH=C:/Qt/6.8.0/android_arm64_v8a" "-DQT_QMAKE_EXECUTABLE:FILEPATH=C:/Qt/6.8.0/android_arm64_v8a/bin/qmake.bat" "-DQT_HOST_PATH:PATH=C:/Qt/6.8.0/mingw_64" "-DQT_USE_TARGET_ANDROID_BUILD_DIR:BOOL=ON" "-DANDROID_STL:STRING=c++_shared" "-DVCPKG_TARGET_TRIPLET:STRING=arm64-android" "-DCMAKE_GENERATOR:STRING=Ninja" "-DCMAKE_PREFIX_PATH:PATH=C:/Qt/6.8.0/android_arm64_v8a" "-DANDROID_USE_LEGACY_TOOLCHAIN_FILE:BOOL=OFF" "-DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL:BOOL=ON" "-DCMAKE_CXX_FLAGS_INIT:STRING=-DQT_QML_DEBUG" "-DCMAKE_CXX_COMPILER:FILEPATH=C:/Users/np_sk/AppData/Local/Android/Sdk/ndk/25.1.8937393/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe" "-DANDROID_NDK:PATH=C:/Users/np_sk/AppData/Local/Android/Sdk/ndk/25.1.8937393"




# Android
Tested NDK version: 26.1.10909125
```
vcpkg install protobuf:arm64-android
```
Environment variables:
```
ANDROID_SDK_HOME=C:\Users\np_sk\AppData\Local\Android\Sdk
ANDROID_NDK_HOME=C:\Users\np_sk\AppData\Local\Android\Sdk\ndk\26.1.10909125
```

CMake variables that need to be set:
```
CMAKE_TOOLCHAIN_FILE:FILEPATH=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
VCPKG_CHAINLOAD_TOOLCHAIN_FILE=C:\Users\np_sk\AppData\Local\Android\Sdk\ndk\26.1.10909125\build\cmake\android.toolchain.cmake
VCPKG_TARGET_TRIPLET=arm64-android
```