# X11KeyboardWindow
Exploring Xlib windowing and keyboard management

## Build requirements
* [CMake](https://cmake.org/download/) >= 3.14
* X11 development libraries:
  * apt:
    ```bash
    sudo apt-get install libx11-dev libxext-dev libxrender-dev libxrandr-dev libxtst-dev libxt-dev
    ```
  * rpm:
    ```bash
    sudo yum install libXtst-devel libXt-devel libXrender-devel libXrandr-devel libXi-devel
    ```

## Build
Just use CMake, e.g.:
```bash
cmake -D "CMAKE_BUILD_TYPE=<build-type>" -G "<generator>" -S "<source-dir>" -B "<build-dir>"
cmake --build "<build-dir>"
```