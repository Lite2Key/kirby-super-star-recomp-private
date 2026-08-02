#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>

#include "kss/native_host.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace kss {
namespace {

class Win32HostPlatform final : public NativeHostPlatform {
public:
    ~Win32HostPlatform() override { close(); }

    bool open(std::uint16_t width, std::uint16_t height) override {
        source_width_ = width;
        source_height_ = height;
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        window_class.lpfnWndProc = &window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        window_class.lpszClassName = kWindowClass;
        if (RegisterClassExW(&window_class) == 0
            && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT rectangle{0, 0, static_cast<LONG>(width) * 3,
            static_cast<LONG>(height) * 3};
        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        if (!AdjustWindowRectEx(&rectangle, style, FALSE, 0)) return false;
        window_ = CreateWindowExW(0, kWindowClass,
            L"Kirby Super Star static recompilation", style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
            nullptr, nullptr, instance_, this);
        if (!window_) return false;
        load_xinput();
        ShowWindow(window_, SW_SHOWDEFAULT);
        UpdateWindow(window_);
        return true;
    }

    bool present(const RgbaFrame& frame) override {
        if (!window_ || !frame.valid() || frame.width != source_width_
            || frame.height != source_height_) {
            return false;
        }
        bgra_.resize(frame.pixels.size());
        for (std::size_t offset = 0; offset < bgra_.size(); offset += 4U) {
            bgra_[offset] = frame.pixels[offset + 2U];
            bgra_[offset + 1U] = frame.pixels[offset + 1U];
            bgra_[offset + 2U] = frame.pixels[offset];
            bgra_[offset + 3U] = frame.pixels[offset + 3U];
        }
        bitmap_ = {};
        bitmap_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_.bmiHeader.biWidth = frame.width;
        bitmap_.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
        bitmap_.bmiHeader.biPlanes = 1;
        bitmap_.bmiHeader.biBitCount = 32;
        bitmap_.bmiHeader.biCompression = BI_RGB;
        InvalidateRect(window_, nullptr, FALSE);
        return true;
    }

    HostPollStatus poll(HostInputSnapshot& input) override {
        const auto wait = MsgWaitForMultipleObjectsEx(
            0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) return HostPollStatus::platform_error;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return HostPollStatus::clean_shutdown;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!window_) return HostPollStatus::clean_shutdown;
        if (key_down(VK_ESCAPE)) {
            DestroyWindow(window_);
            return HostPollStatus::clean_shutdown;
        }

        HostKeyboardInput keyboard{};
        keyboard.up = key_down(VK_UP);
        keyboard.down = key_down(VK_DOWN);
        keyboard.left = key_down(VK_LEFT);
        keyboard.right = key_down(VK_RIGHT);
        keyboard.b = key_down('Z');
        keyboard.a = key_down('X');
        keyboard.y = key_down('A');
        keyboard.x = key_down('S');
        keyboard.select = key_down(VK_RSHIFT);
        keyboard.start = key_down(VK_RETURN);
        keyboard.l = key_down('Q');
        keyboard.r = key_down('W');
        input.controller_buttons.fill(0);
        input.controller_buttons[0] = map_keyboard_to_snes(keyboard);
        for (DWORD port = 0; port < SnesControllerPorts::kPortCount; ++port) {
            XINPUT_STATE state{};
            if (xinput_get_state_ && xinput_get_state_(port, &state) == ERROR_SUCCESS) {
                const HostGamepadInput gamepad{state.Gamepad.wButtons,
                    state.Gamepad.sThumbLX, state.Gamepad.sThumbLY};
                input.controller_buttons[port] = static_cast<std::uint16_t>(
                    input.controller_buttons[port] | map_gamepad_to_snes(gamepad));
            }
        }
        return HostPollStatus::running;
    }

    void close() noexcept override {
        if (window_) DestroyWindow(window_);
        window_ = nullptr;
        if (xinput_module_) FreeLibrary(xinput_module_);
        xinput_module_ = nullptr;
        xinput_get_state_ = nullptr;
    }

private:
    using XInputGetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    static constexpr const wchar_t* kWindowClass = L"KssStaticRecompHost";

    static bool key_down(int key) noexcept {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    void load_xinput() noexcept {
        constexpr std::array<const wchar_t*, 3> libraries{
            L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
        for (const auto* library : libraries) {
            xinput_module_ = LoadLibraryW(library);
            if (!xinput_module_) continue;
            const auto address = GetProcAddress(xinput_module_, "XInputGetState");
            static_assert(sizeof(address) == sizeof(xinput_get_state_));
            std::memcpy(&xinput_get_state_, &address, sizeof(address));
            if (xinput_get_state_) return;
            FreeLibrary(xinput_module_);
            xinput_module_ = nullptr;
        }
    }

    static LRESULT CALLBACK window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Win32HostPlatform*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<Win32HostPlatform*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
        }
        if (self) return self->handle_message(window, message, wparam, lparam);
        return DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint(window);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            window_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    void paint(HWND window) noexcept {
        PAINTSTRUCT paint_state{};
        const auto device = BeginPaint(window, &paint_state);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(device, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (!bgra_.empty()) {
            const auto client_width = client.right - client.left;
            const auto client_height = client.bottom - client.top;
            const auto scale = std::max<LONG>(1, std::min(
                client_width / static_cast<LONG>(source_width_),
                client_height / static_cast<LONG>(source_height_)));
            const auto width = static_cast<LONG>(source_width_) * scale;
            const auto height = static_cast<LONG>(source_height_) * scale;
            const auto x = (client_width - width) / 2;
            const auto y = (client_height - height) / 2;
            SetStretchBltMode(device, COLORONCOLOR);
            StretchDIBits(device, x, y, width, height, 0, 0,
                source_width_, source_height_, bgra_.data(), &bitmap_,
                DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(window, &paint_state);
    }

    HINSTANCE instance_{};
    HWND window_{};
    HMODULE xinput_module_{};
    XInputGetStateFunction xinput_get_state_{};
    std::uint16_t source_width_{};
    std::uint16_t source_height_{};
    BITMAPINFO bitmap_{};
    std::vector<std::uint8_t> bgra_;
};

} // namespace

std::unique_ptr<NativeHostPlatform> make_win32_host_platform() {
    return std::make_unique<Win32HostPlatform>();
}

} // namespace kss
