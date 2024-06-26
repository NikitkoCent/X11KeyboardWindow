// Copyright 2022-2024 Nikita Provotorov
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
#include <iostream>     // std::ostream, std::cerr
#include <optional>     // std::optional
#include <string>       // std::string
#include <clocale>      // std::setlocale
#include <exception>    // std::exception
#include <sstream>      // std::ostringstream
#include <iomanip>      // std::setbase
#include <functional>   // std::function
#include <utility>      // std::move, std::forward
#include <type_traits>  // std::is_trivially_destructible_v
#include <thread>       // std::thread, std::this_thread


namespace logging
{
    template<typename... Ts>
    std::ostream& myLogImpl(std::ostream& logStream, Ts&&... args);

    #define MY_LOG(...) logging::myLogImpl(std::cerr, "[tid:", std::this_thread::get_id(), "] ", __FILE__ ":", __LINE__, ": ", __VA_ARGS__, '\n')

    #define MY_LOG_X11_CALL(FUNC_CALL)                                  \
    [&] {                                                               \
        MY_LOG(#FUNC_CALL, "...");                                      \
        auto result_local = FUNC_CALL;                                  \
        MY_LOG("    ...returned ", result_local);                       \
        return result_local;                                            \
    }()

    #define MY_LOG_X11_CALL_VALUELESS(FUNC_CALL)                        \
    [&] {                                                               \
        MY_LOG(#FUNC_CALL, "...");                                      \
        FUNC_CALL;                                                      \
        MY_LOG("    ...finished.");                                     \
    }()

    void logX11Event(const XEvent& event, bool isFilteredOut);
}


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


std::string XModifiersStateToString(decltype(XKeyEvent::state) state);

XRAIIWrapper<XIMStyles*> obtainSupportedInputStyles(XIM inputMethod) noexcept(false);

// Returns the maximum size of the preedit string
static int preeditStartCallback(XIC ic, XPointer client_data, XPointer call_data);
static void preeditDoneCallback(XIC ic, XPointer client_data, XPointer call_data);
static void preeditDrawCallback(XIC ic, XPointer client_data, XIMPreeditDrawCallbackStruct *call_data);
static void preeditCaretCallback(XIC ic, XPointer client_data, XIMPreeditCaretCallbackStruct *call_data);

struct InputMethodText
{
    std::optional<KeySym> keySym;
    std::optional<std::string> composedTextUtf8;

    static InputMethodText obtainFrom(XIC imContext, XKeyPressedEvent& kpEvent);
};


[[maybe_unused]] static void moveImCandidatesWindow(XIC imContext, XPoint newLocation);


int main()
{
    try
    {
        // https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html#X_Locale_Management
        const auto locale = MY_LOG_X11_CALL(std::setlocale(LC_CTYPE, ""));
        if (locale == nullptr)
            throw std::runtime_error("std::setlocale failed");
        if (!MY_LOG_X11_CALL(XSupportsLocale()))
            throw std::runtime_error(std::string("X11 does not support the current locale ") + locale);

        // Set all X modifiers for the current locale to implementation-dependent defaults (of the current locale).
        // The local host X locale modifiers announcer (on POSIX-compliant systems, the XMODIFIERS environment variable) is used.
        // https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html#X_Locale_Management
        if (MY_LOG_X11_CALL(XSetLocaleModifiers("")) == nullptr)
            throw std::runtime_error("XSetLocaleModifiers failed");

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

        // Initialize input methods
        const XRAIIWrapper<XIM> inputMethod{
            MY_LOG_X11_CALL(XOpenIM(display, nullptr, nullptr, nullptr)),
            [](auto& xim) { if (xim != nullptr) MY_LOG_X11_CALL(XCloseIM(xim)); }
        };
        if (inputMethod == nullptr)
            throw std::runtime_error("XOpenIM failed");

        [[maybe_unused]] const XRAIIWrapper supportedInputStyles = obtainSupportedInputStyles(inputMethod);

        // Setup preedit callbacks
        XIMCallback preeditCallbacks[] = {
            { nullptr, reinterpret_cast<XIMProc>((void*)&preeditStartCallback) },
            { nullptr, reinterpret_cast<XIMProc>(&preeditDoneCallback) },
            { nullptr, reinterpret_cast<XIMProc>(&preeditDrawCallback) },
            { nullptr, reinterpret_cast<XIMProc>(&preeditCaretCallback) },
        };
        const XRAIIWrapper<XVaNestedList> preeditAttributes{
            MY_LOG_X11_CALL(XVaCreateNestedList(0,
                XNPreeditStartCallback, &preeditCallbacks[0],
                XNPreeditDoneCallback, &preeditCallbacks[1],
                XNPreeditDrawCallback, &preeditCallbacks[2],
                XNPreeditCaretCallback, &preeditCallbacks[3],
                nullptr
            )),
            [](auto& list) { if (list != nullptr) MY_LOG_X11_CALL_VALUELESS(XFree(list)); }
        };
        if (preeditAttributes == nullptr)
            throw std::runtime_error("XVaCreateNestedList failed");

        // Initialize input context.
        // See
        //   * https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html#Input_Context_Values;
        //   * https://www.x.org/releases/X11R7.6/doc/libX11/specs/libX11/libX11.html#Query_Input_Style.
        //   for the used flags.
        const XRAIIWrapper<XIC> imContext{
            MY_LOG_X11_CALL(XCreateIC(
                inputMethod,
                XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing,
                XNPreeditAttributes, preeditAttributes.getResource(),
                XNClientWindow, static_cast<Window>(window.getResource()),
                nullptr
            )),
            [](auto& xic) { if (xic != nullptr) MY_LOG_X11_CALL_VALUELESS(XDestroyIC(xic)); }
        };
        if (imContext == nullptr)
            throw std::runtime_error("XCreateIC failed");

        // Set focus
        MY_LOG_X11_CALL_VALUELESS(XSetICFocus(imContext));

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

            // XFilterEvent returns True when some input method has filtered the event,
            //   and the client should discard the event.
            [[maybe_unused]] const bool eventWasFiltered = MY_LOG_X11_CALL(XFilterEvent(&event, None));

            logging::logX11Event(event, eventWasFiltered);

            if (eventWasFiltered)
                continue;

            switch (event.type)
            {
                case ClientMessage:
                {
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteMessage)
                    {
                        MY_LOG("wmDeleteMessage received. Exit the event loop...");
                        shouldExit = true;
                    }
                    break;
                }
                case KeymapNotify:
                {
                    break;
                }
                // https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
                // https://tronche.com/gui/x/xlib/input/keyboard-encoding.html
                case KeyPress:
                {
                    const auto [keySym, composedTextUtf8] =
                        InputMethodText::obtainFrom(imContext.getResource(), event.xkey);

                    if (keySym.has_value())
                        logging::myLogImpl(std::cerr, "               keySym: ", *keySym, "\n");
                    if (composedTextUtf8.has_value())
                        logging::myLogImpl(std::cerr, "  composedText (UTF8): \"", *composedTextUtf8, "\"", "\n");

                    break;
                }
                case KeyRelease:
                {
                    break;
                }
                case ButtonPress:
                {
                    break;
                }
                case ButtonRelease:
                {
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

        const auto writer = [&strStream](auto&& value) -> int {
            using WithoutCVRefs = std::remove_cv_t<std::remove_reference_t<decltype(value)>>;
            if constexpr (std::is_pointer_v<WithoutCVRefs>)
            {
                if (std::forward<decltype(value)>(value) == nullptr)
                {
                    strStream << "<nullptr>";
                    return 0;
                }
            }

            strStream << std::forward<decltype(value)>(value);

            return 0;
        };

        [[maybe_unused]] const int dummy[sizeof...(Ts)] = { writer(std::forward<Ts>(args))... };
        return logStream << strStream.str();
    }


    void logX11Event(const XClientMessageEvent& event);
    void logX11Event(const XKeyEvent& event);
    void logX11Event(const XButtonEvent& event);

    void logX11Event(const XEvent& event, bool isFilteredOut)
    {
        const std::string_view prefix = isFilteredOut ? "Filtered " : "";
        std::string_view eventNameForUndetailedLogging;

        switch (event.type)
        {
            default:
                MY_LOG(prefix, "UNKOWN (", event.type, " ) EVENT");
                return;
            case ClientMessage:
                MY_LOG(prefix, "ClientMessage EVENT");
                logX11Event(event.xclient);
                return;
            case KeyPress:
                MY_LOG(prefix, "KeyPress EVENT");
                logX11Event(event.xkey);
                return;
            case KeyRelease:
                MY_LOG(prefix, "KeyRelease EVENT");
                logX11Event(event.xkey);
                return;
            case ButtonPress:
                MY_LOG(prefix, "ButtonPress EVENT");
                logX11Event(event.xbutton);
                return;
            case ButtonRelease:
                MY_LOG(prefix, "ButtonRelease EVENT");
                logX11Event(event.xbutton);
                return;
            case KeymapNotify:
                eventNameForUndetailedLogging = "KeymapNotify";
                break;
            case MotionNotify:
                eventNameForUndetailedLogging = "MotionNotify";
                break;
            case EnterNotify:
                eventNameForUndetailedLogging = "EnterNotify";
                break;
            case LeaveNotify:
                eventNameForUndetailedLogging = "LeaveNotify";
                break;
            case FocusIn:
                eventNameForUndetailedLogging = "FocusIn";
                break;
            case FocusOut:
                eventNameForUndetailedLogging = "FocusOut";
                break;
            case Expose:
                eventNameForUndetailedLogging = "Expose";
                break;
            case GraphicsExpose:
                eventNameForUndetailedLogging = "GraphicsExpose";
                break;
            case NoExpose:
                eventNameForUndetailedLogging = "NoExpose";
                break;
            case VisibilityNotify:
                eventNameForUndetailedLogging = "VisibilityNotify";
                break;
            case CreateNotify:
                eventNameForUndetailedLogging = "CreateNotify";
                break;
            case DestroyNotify:
                eventNameForUndetailedLogging = "DestroyNotify";
                break;
            case UnmapNotify:
                eventNameForUndetailedLogging = "UnmapNotify";
                break;
            case MapNotify:
                eventNameForUndetailedLogging = "MapNotify";
                break;
            case MapRequest:
                eventNameForUndetailedLogging = "MapRequest";
                break;
            case ReparentNotify:
                eventNameForUndetailedLogging = "ReparentNotify";
                break;
            case ConfigureNotify:
                eventNameForUndetailedLogging = "ConfigureNotify";
                break;
            case ConfigureRequest:
                eventNameForUndetailedLogging = "ConfigureRequest";
                break;
            case GravityNotify:
                eventNameForUndetailedLogging = "GravityNotify";
                break;
            case ResizeRequest:
                eventNameForUndetailedLogging = "ResizeRequest";
                break;
            case CirculateNotify:
                eventNameForUndetailedLogging = "CirculateNotify";
                break;
            case CirculateRequest:
                eventNameForUndetailedLogging = "CirculateRequest";
                break;
            case PropertyNotify:
                eventNameForUndetailedLogging = "PropertyNotify";
                break;
            case SelectionClear:
                eventNameForUndetailedLogging = "SelectionClear";
                break;
            case SelectionRequest:
                eventNameForUndetailedLogging = "SelectionRequest";
                break;
            case SelectionNotify:
                eventNameForUndetailedLogging = "SelectionNotify";
                break;
            case ColormapNotify:
                eventNameForUndetailedLogging = "ColormapNotify";
                break;
            case MappingNotify:
                eventNameForUndetailedLogging = "MappingNotify";
                break;
            case GenericEvent:
                eventNameForUndetailedLogging = "GenericEvent";
                break;
        }

        MY_LOG(prefix, eventNameForUndetailedLogging, " EVENT");
    }

    void logX11Event(const XClientMessageEvent& event)
    {
        std::string msgTypeStr;
        if (event.message_type != None)
        {
            if (char* const atomStr = XGetAtomName(event.display, event.message_type); atomStr != nullptr)
            {
                msgTypeStr = atomStr;
                XFree(atomStr);
            }
        }

        const auto dataToStr = [](const auto format, const auto& data) -> std::string {
            std::ostringstream result;
            result << std::setbase(16);

            const auto joinInts = [&result](const auto& intsRange) -> void {
                auto currentIter = std::begin(intsRange);
                const auto endIter = std::end(intsRange);

                if (currentIter == endIter) return;

                constexpr std::string_view prefix = "0x";

                auto first = *currentIter++;
                using UnsignedType = std::make_unsigned_t<decltype(first)>;
                using OutputType = unsigned long long;

                result << prefix << OutputType{ static_cast<UnsignedType>(first) };
                while (currentIter != endIter)
                {
                    result << ", " << prefix << OutputType{ static_cast<UnsignedType>(*currentIter++) };
                }
            };

            switch (format)
            {
                case 8:
                    result << '[';
                    joinInts(data.b);
                    result << ']';
                    break;
                case 16:
                    result << '[';
                    joinInts(data.s);
                    result << ']';
                    break;
                case 32:
                    result << '[';
                    joinInts(data.l);
                    result << ']';
                    break;
                default:
                    result << "<unknown format>";
                    break;
            }

            return result.str();
        };

        myLogImpl(std::cerr, "event@", &event, ": \n",
                             "                 type: ", event.type, " (ClientMessage)", "\n",
                             "               serial: ", event.serial, "\n",
                             "           send_event: ", event.send_event ? "true" : "false", "\n",
                             "              display: ", event.display, "\n",
                             "               window: ", event.window, "\n",
                             "         message_type: ", event.message_type, " (\"", msgTypeStr, "\")", "\n",
                             "               format: ", event.format, "\n",
                             "                 data: ", dataToStr(event.format, event.data),
                             "\n"
        );
    }

    void logX11Event(const XKeyEvent& event)
    {
        const auto eventTypeStr = (event.type == KeyPress) ? "KeyPress"
                                   : (event.type == KeyRelease) ? "KeyRelease"
                                   : "<Unknown>";

        myLogImpl(std::cerr, "event@", &event, ": \n",
                             "                 type: ", eventTypeStr, " (", event.type, ")", "\n",
                             "               serial: ", event.serial, "\n",
                             "           send_event: ", event.send_event ? "true" : "false", "\n",
                             "              display: ", event.display, "\n",
                             "               window: ", event.window, "\n",
                             "                 root: ", event.root, "\n",
                             "            subwindow: ", event.subwindow, "\n",
                             "                 time: ", event.time, " ms.", "\n",
                             "                    x: ", event.x, "\n",
                             "                    y: ", event.y, "\n",
                             "               x_root: ", event.x_root, "\n",
                             "               y_root: ", event.y_root, "\n",
                             "                state: ", event.state, " (", XModifiersStateToString(event.state), ")", "\n",
                             "              keycode: ", event.keycode, "\n",
                             "          same_screen: ", event.same_screen ? "true" : "false",
                             "\n"
        );
    }

    void logX11Event(const XButtonEvent& event)
    {
        const auto eventTypeStr = (event.type == ButtonPress) ? "ButtonPress"
                                   : (event.type == ButtonRelease) ? "ButtonRelease"
                                   : "<Unknown>";

        myLogImpl(std::cerr, "event@", &event, ": \n",
                             "                 type: ", eventTypeStr, " (", event.type, ")", "\n",
                             "               serial: ", event.serial, "\n",
                             "           send_event: ", event.send_event ? "true" : "false", "\n",
                             "              display: ", event.display, "\n",
                             "               window: ", event.window, "\n",
                             "                 root: ", event.root, "\n",
                             "            subwindow: ", event.subwindow, "\n",
                             "                 time: ", event.time, " ms.", "\n",
                             "                    x: ", event.x, "\n",
                             "                    y: ", event.y, "\n",
                             "               x_root: ", event.x_root, "\n",
                             "               y_root: ", event.y_root, "\n",
                             "                state: ", event.state, " (", XModifiersStateToString(event.state), ")", "\n",
                             "               button: ", event.button, "\n",
                             "          same_screen: ", event.same_screen ? "true" : "false",
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


XRAIIWrapper<XIMStyles*> obtainSupportedInputStyles(XIM inputMethod) noexcept(false)
{
    XIMStyles* styles = nullptr;
    if (const char* failedArg = MY_LOG_X11_CALL(XGetIMValues(inputMethod, XNQueryInputStyle, &styles, nullptr));
            failedArg != nullptr)
    {
        throw std::runtime_error(std::string("XGetIMValues failed: \"") + failedArg + "\"");
    }
    if (styles == nullptr)
        throw std::runtime_error("XGetIMValues didn't return values for XNQueryInputStyle");

    logging::myLogImpl(std::cerr, "Supported input styles (XNQueryInputStyle):", '\n');
    std::string buffer;
    buffer.reserve(128);
    for (int i = 0; i < styles->count_styles; ++i)
    {
        if ( (styles->supported_styles[i] & XIMPreeditArea) != 0 )
            buffer += buffer.empty() ? "XIMPreeditArea" : " | XIMPreeditArea";
        if ( (styles->supported_styles[i] & XIMPreeditCallbacks) != 0 )
            buffer += buffer.empty() ? "XIMPreeditCallbacks" : " | XIMPreeditCallbacks";
        if ( (styles->supported_styles[i] & XIMPreeditPosition) != 0 )
            buffer += buffer.empty() ? "XIMPreeditPosition" : " | XIMPreeditPosition";
        if ( (styles->supported_styles[i] & XIMPreeditNothing) != 0 )
            buffer += buffer.empty() ? "XIMPreeditNothing" : " | XIMPreeditNothing";
        if ( (styles->supported_styles[i] & XIMPreeditNone) != 0 )
            buffer += buffer.empty() ? "XIMPreeditNone" : " | XIMPreeditNone";
        if ( (styles->supported_styles[i] & XIMStatusArea) != 0 )
            buffer += buffer.empty() ? "XIMStatusArea" : " | XIMStatusArea";
        if ( (styles->supported_styles[i] & XIMStatusCallbacks) != 0 )
            buffer += buffer.empty() ? "XIMStatusCallbacks" : " | XIMStatusCallbacks";
        if ( (styles->supported_styles[i] & XIMStatusNothing) != 0 )
            buffer += buffer.empty() ? "XIMStatusNothing" : " | XIMStatusNothing";
        if ( (styles->supported_styles[i] & XIMStatusNone) != 0 )
            buffer += buffer.empty() ? "XIMStatusNone" : " | XIMStatusNone";

        logging::myLogImpl(std::cerr, "    ", buffer, " (", styles->supported_styles[i], ')', '\n');
        buffer.clear();
    }

    return {std::move(styles), [](auto& st) { if (st != nullptr) XFree(st); } };
}

// Returns the maximum size of the preedit string
static int preeditStartCallback(XIC ic, XPointer client_data, XPointer call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    MY_LOG(__func__, '(', ic, ", ", static_cast<void*>(client_data), ", ", static_cast<void*>(call_data), ')');
    return -1;
}

static void preeditDoneCallback(XIC ic, XPointer client_data, XPointer call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    MY_LOG(__func__, '(', ic, ", ", static_cast<void*>(client_data), ", ", static_cast<void*>(call_data), ')');
}

static void preeditDrawCallback(XIC ic, XPointer client_data, XIMPreeditDrawCallbackStruct* call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    MY_LOG(__func__, '(', ic, ", ", static_cast<void*>(client_data), ", ", static_cast<void*>(call_data), ')');
}

static void preeditCaretCallback(XIC ic, XPointer client_data, XIMPreeditCaretCallbackStruct* call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    MY_LOG(__func__, '(', ic, ", ", static_cast<void*>(client_data), ", ", static_cast<void*>(call_data), ')');
}


InputMethodText InputMethodText::obtainFrom(XIC imContext, XKeyPressedEvent& kpEvent)
{
    std::string composedText;
    KeySym keySym;
    Status status;

    composedText.resize(129, 0);

    // https://opennet.ru/man.shtml?topic=XmbLookupString

    int composedTextLengthBytes = MY_LOG_X11_CALL(Xutf8LookupString(
        imContext,
        &kpEvent,
        composedText.data(),
        composedText.size() - 1,
        &keySym,
        &status
    ));
    if (status == XBufferOverflow)
    {
        composedText.resize(composedTextLengthBytes + 1, 0);
        composedTextLengthBytes = MY_LOG_X11_CALL(Xutf8LookupString(
            imContext,
            &kpEvent,
            composedText.data(),
            composedText.size() - 1,
            &keySym,
            &status
        ));
    }

    composedText.resize(composedTextLengthBytes, 0);

    switch (status) {
        case XLookupNone:
            return { std::nullopt, std::nullopt };
        case XLookupChars:
            return { std::nullopt, std::move(composedText) };
        case XLookupKeySym:
            return { keySym, std::nullopt };
        case XLookupBoth:
            return { keySym, std::move(composedText) };
        default:
            throw std::runtime_error("Xutf8LookupString: unknown status: " + std::to_string(status));
    }
}


static void moveImCandidatesWindow(XIC imContext, XPoint newLocation)
{
    const XRAIIWrapper<XVaNestedList> newLocationAttr{
        MY_LOG_X11_CALL(XVaCreateNestedList(0, XNSpotLocation, &newLocation, nullptr)),
        [](auto list) { if (list != nullptr) MY_LOG_X11_CALL_VALUELESS(XFree(list)); }
    };

    if (newLocationAttr == nullptr)
        return;

    MY_LOG_X11_CALL(XSetICValues(
        imContext,
        XNPreeditAttributes,
        static_cast<XVaNestedList>(newLocationAttr.getResource()),
        nullptr
    ));
}
