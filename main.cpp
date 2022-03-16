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
#include <exception>    // std::exception
#include <iostream>     // std::cerr
#include <sstream>      // std::ostringstream
#include <functional>   // std::function
#include <utility>      // std::move, std::forward
#include <optional>     // std::optional
#include <type_traits>  // std::is_trivially_destructible_v


namespace logging
{
    template<typename... Ts>
    std::ostream& myLogImpl(std::ostream& logStream, Ts&&... args);

    #define MY_LOG(...) logging::myLogImpl(std::cerr, __FILE__ ":", __LINE__, ": ", __VA_ARGS__, '\n')

    #define MY_LOG_X11_CALL(FUNC_CALL)                                  \
    [&] {                                                               \
        MY_LOG(#FUNC_CALL, "...");                                      \
        auto result = FUNC_CALL;                                        \
        MY_LOG("    ...returned ", result);                             \
        return result;                                                  \
    }()

    void logX11Event(const XKeyEvent& event);
    void logX11Event(const XButtonEvent& event);
}

std::string XModifiersStateToString(decltype(XKeyEvent::state) state);


template<typename T>
class XRAIIWrapper
{
    static_assert( std::is_trivially_destructible_v<T> );

public:
    template<typename CustomDeleter>
    XRAIIWrapper(T&& resource, CustomDeleter&& deleter)
        : impl_{ std::in_place, std::move(resource), std::forward<CustomDeleter>(deleter) }
    {}

    XRAIIWrapper(T&& resource)
        : XRAIIWrapper(std::move(resource), nullptr)
    {}

    XRAIIWrapper(const XRAIIWrapper&) = delete;
    XRAIIWrapper(XRAIIWrapper&&) = default;

    ~XRAIIWrapper()
    {
        reset();
    }

public:
    XRAIIWrapper& operator=(const XRAIIWrapper&) = delete;
    XRAIIWrapper& operator=(XRAIIWrapper&& rhs)
    {
        if (&rhs != this)
        {
            reset();
            impl_ = std::move(rhs.impl_);
        }

        return *this;
    }

public:
    T& getResource() { return impl_.value().first; }
    [[nodiscard]] const T& getResource() const { return impl_.value().first; }

    operator const T&() const { return impl_.value().first; }

    template<typename R>
    explicit operator R() const { return (R)impl_.value().first; }

private:
    void reset()
    {
        if (impl_.has_value())
        {
            auto& deleter = impl_->second;
            if (deleter)
                deleter(impl_->first);
            impl_.reset();
        }
    }

private:
    std::optional< std::pair<T, std::function<void(T&)>> > impl_;
};


int main()
{
    try
    {
        const XRAIIWrapper<Display*> display{
            MY_LOG_X11_CALL(XOpenDisplay(nullptr)),
            [](auto& d){ if (d != nullptr) MY_LOG_X11_CALL(XCloseDisplay(d)); }
        };
        if (display == nullptr)
            throw std::runtime_error("XOpenDisplay failed");

        const XRAIIWrapper<Window> displayWindow = MY_LOG_X11_CALL(DefaultRootWindow(display));
        const int displayScreenIndex = MY_LOG_X11_CALL(DefaultScreen(display));

        const XRAIIWrapper<Window> window{
            MY_LOG_X11_CALL(XCreateSimpleWindow(
                /* display      */ display,
                /* parent       */ displayWindow,
                /* x            */ 150,
                /* y            */ 50,
                /* width        */ 400,
                /* height       */ 300,
                /* border_width */ 5,
                /* border       */ BlackPixel(display, displayScreenIndex),
                /* background   */ WhitePixel(display, displayScreenIndex)
            )),
            [&](auto& w) { MY_LOG_X11_CALL(XDestroyWindow(display, w)); }
        };

        // "Subscribes" to delete window message.
        // Then received ClientMessage with attached wmDeleteMessage in the event loop (see below) will mean
        //   user have closed the window.
        Atom wmDeleteMessage = MY_LOG_X11_CALL(XInternAtom(display, "WM_DELETE_WINDOW", False));
        if (const Status status = MY_LOG_X11_CALL(XSetWMProtocols(display, window, &wmDeleteMessage, 1)); status == 0)
            throw std::runtime_error("XSetWMProtocols failed (tried to set WM_DELETE_WINDOW to False)");

        // Subscribe to keyboard and mouse events
        MY_LOG_X11_CALL(XSelectInput(
            display,
            window,
            KeyPressMask | KeyReleaseMask | KeymapStateMask | ButtonPressMask | ButtonReleaseMask
        ));

        // Show window
        MY_LOG_X11_CALL(XMapWindow(display, window));

        MY_LOG("Starting the event loop...");

        // The event loop
        // https://tronche.com/gui/x/xlib/event-handling/
        bool shouldExit = false;
        do
        {
            XEvent event;
            MY_LOG_X11_CALL(XNextEvent(display, &event));

            switch (event.type)
            {
                case ClientMessage:
                {
                    MY_LOG("ClientMessage EVENT");
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteMessage)
                    {
                        MY_LOG("wmDeleteMessage received. Exit the event loop...");
                        shouldExit = true;
                    }
                    break;
                }
                case KeymapNotify:
                {
                    MY_LOG("KeymapNotify EVENT");
                    break;
                }
                // https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
                // https://tronche.com/gui/x/xlib/input/keyboard-encoding.html
                case KeyPress:
                {
                    MY_LOG("KeyPress EVENT");
                    logging::logX11Event(event.xkey);
                    break;
                }
                case KeyRelease:
                {
                    MY_LOG("KeyRelease EVENT");
                    logging::logX11Event(event.xkey);
                    break;
                }
                case ButtonPress:
                {
                    MY_LOG("ButtonPress EVENT");
                    logging::logX11Event(event.xbutton);
                    break;
                }
                case ButtonRelease:
                {
                    MY_LOG("ButtonRelease EVENT");
                    logging::logX11Event(event.xbutton);
                    break;
                }
            }
        }
        while (!shouldExit);
    }
    catch (const std::exception& err)
    {
        std::cerr << "Caught exception: " << err.what() << std::endl;
        return 1;
    }

    return 0;
}


namespace logging
{
    template<typename... Ts>
    std::ostream& myLogImpl(std::ostream& logStream, Ts&&... args)
    {
        std::ostringstream strStream;
        [[maybe_unused]] const int dummy[sizeof...(Ts)] = {(strStream << std::forward<Ts>(args), 0)...};
        return logStream << strStream.str();
    }

    void logX11Event(const XKeyEvent& event)
    {
        const auto eventTypeStr = (event.type == KeyPress) ? "KeyPress"
                                   : (event.type == KeyRelease) ? "KeyRelease"
                                   : "<Unknown>";

        myLogImpl(std::cerr, "event@", &event, ": \n",
                             "         type: ", eventTypeStr, " (", event.type, ")", "\n",
                             "       serial: ", event.serial, "\n",
                             "   send_event: ", event.send_event ? "true" : "false", "\n",
                             "      display: ", event.display, "\n",
                             "       window: ", event.window, "\n",
                             "         root: ", event.root, "\n",
                             "    subwindow: ", event.subwindow, "\n",
                             "         time: ", event.time, " ms.", "\n",
                             "            x: ", event.x, "\n",
                             "            y: ", event.y, "\n",
                             "       x_root: ", event.x_root, "\n",
                             "       y_root: ", event.y_root, "\n",
                             "        state: ", event.state, " (", XModifiersStateToString(event.state), ")", "\n",
                             "      keycode: ", event.keycode, "\n",
                             "  same_screen: ", event.same_screen ? "true" : "false",
                             "\n"
        );
    }

    void logX11Event(const XButtonEvent& event)
    {
        const auto eventTypeStr = (event.type == ButtonPress) ? "ButtonPress"
                                   : (event.type == ButtonRelease) ? "ButtonRelease"
                                   : "<Unknown>";

        myLogImpl(std::cerr, "event@", &event, ": \n",
                             "         type: ", eventTypeStr, " (", event.type, ")", "\n",
                             "       serial: ", event.serial, "\n",
                             "   send_event: ", event.send_event ? "true" : "false", "\n",
                             "      display: ", event.display, "\n",
                             "       window: ", event.window, "\n",
                             "         root: ", event.root, "\n",
                             "    subwindow: ", event.subwindow, "\n",
                             "         time: ", event.time, " ms.", "\n",
                             "            x: ", event.x, "\n",
                             "            y: ", event.y, "\n",
                             "       x_root: ", event.x_root, "\n",
                             "       y_root: ", event.y_root, "\n",
                             "        state: ", event.state, " (", XModifiersStateToString(event.state), ")", "\n",
                             "       button: ", event.button, "\n",
                             "  same_screen: ", event.same_screen ? "true" : "false",
                             "\n"
        );
    }
}


std::string XModifiersStateToString(const decltype(XKeyEvent::state) state)
{
    std::string result = "[";

    if ( (state & Button1Mask) == Button1Mask )
        result += result.length() < 2 ? "Button1" : ", Button1";
    if ( (state & Button2Mask) == Button2Mask )
        result += result.length() < 2 ? "Button2" : ", Button2";
    if ( (state & Button3Mask) == Button3Mask )
        result += result.length() < 2 ? "Button3" : ", Button3";
    if ( (state & Button4Mask) == Button4Mask )
        result += result.length() < 2 ? "Button4" : ", Button4";
    if ( (state & Button5Mask) == Button5Mask )
        result += result.length() < 2 ? "Button5" : ", Button5";
    if ( (state & ShiftMask) == ShiftMask )
        result += result.length() < 2 ? "Shift" : ", Shift";
    if ( (state & LockMask) == LockMask )
        result += result.length() < 2 ? "Lock" : ", Lock";
    if ( (state & ControlMask) == ControlMask )
        result += result.length() < 2 ? "Control" : ", Control";
    if ( (state & Mod1Mask) == Mod1Mask )
        result += result.length() < 2 ? "Mod1" : ", Mod1";
    if ( (state & Mod2Mask) == Mod2Mask )
        result += result.length() < 2 ? "Mod2" : ", Mod2";
    if ( (state & Mod3Mask) == Mod3Mask )
        result += result.length() < 2 ? "Mod3" : ", Mod3";
    if ( (state & Mod4Mask) == Mod4Mask )
        result += result.length() < 2 ? "Mod4" : ", Mod4";
    if ( (state & Mod5Mask) == Mod5Mask )
        result += result.length() < 2 ? "Mod5" : ", Mod5";

    return result += ']';
}
