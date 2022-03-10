// Copyright 2022 Nikita Provotorov
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.


#include <X11/Xlib.h>
#include <exception>
#include <iostream>


int main()
{
    try
    {
        Display* const display = XOpenDisplay(nullptr);
        if (display == nullptr)
            throw std::runtime_error("XOpenDisplay failed");

        const Window displayWindow = DefaultRootWindow(display);
        const int displayScreenIndex = DefaultScreen(display);

        const Window window = XCreateSimpleWindow(
            /* display      */ display,
            /* parent       */ displayWindow,
            /* x            */ 150,
            /* y            */ 50,
            /* width        */ 400,
            /* height       */ 300,
            /* border_width */ 5,
            /* border       */ BlackPixel(display, displayScreenIndex),
            /* background   */ WhitePixel(display, displayScreenIndex)
        );

        // Show window
        XMapWindow(display, window);

        XDestroyWindow(display, window);

        // XCloseDisplay returns int but there is no information about returned values,
        //   so the returned value is just ignored.
        XCloseDisplay(display);
    }
    catch (const std::exception& err)
    {
        std::cerr << "Caught exception: " << err.what() << std::endl;
        return 1;
    }

    return 0;
}