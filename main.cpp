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
#include <X11/Xatom.h>  // Atom, XInternAtom
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

        // "Subscribes" to delete window message.
        // Then received ClientMessage with attached wmDeleteMessage in the event loop (see below) will mean
        //   user have closed the window.
        Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (const Status status = XSetWMProtocols(display, window, &wmDeleteMessage, 1); status == 0)
        {
            //throw std::runtime_error("XSetWMProtocols failed (tried to set WM_DELETE_WINDOW to False)");

            // temporary solution until RAII wrappers are implemented
            std::cerr << "XSetWMProtocols failed (tried to set WM_DELETE_WINDOW to False)" << std::endl;
            goto temp_cleanup;
        }

        // Show window
        XMapWindow(display, window);

        // The event loop
        // https://tronche.com/gui/x/xlib/event-handling/
        bool shouldExit;
        shouldExit = false;
        do
        {
            XEvent event;
            XNextEvent(display, &event);

            switch (event.type)
            {
                case ClientMessage:
                {
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteMessage) {
                        shouldExit = true;
                    }
                    break;
                }
            }
        }
        while (!shouldExit);

    temp_cleanup:
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
