#include <game_capture.h>
#include <windows_helper.h>
#include <thread>
#include <filesystem>
#include <LoggerHelper.h>
int main() {
    auto logger=CreateAsyncLogger({ "", {std::make_shared<MSVCLoggerInfo_t>(),std::make_shared<StdoutLoggerInfo_t>()} });
#ifdef NDEBUG
    logger->set_level(spdlog::level::warn);
#else
    logger->set_level(spdlog::level::debug);
#endif
    auto cap=FGameCapture::CreateGameCapture();

    //auto Workpath = std::filesystem::Workpath;
    //if (Workpath.filename().string().find("bin") != std::string::npos) {
    //    Workpath = Workpath.parent_path();
    //}
    if (!cap->Init((const char*)std::filesystem::current_path().u8string().c_str())) {
        return 0;
    }
    //auto handle=find_window_by_title("D9GameWindow");
    auto handle=find_window_by_title("dx9app");
    if (handle == NULL) {
        return 0;
    }
    unsigned long process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);

    auto captureHandle=cap->StartCapture(process_id);
    ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> windowHandle;
    uint32_t width, height;
    std::vector<uint8_t> windowImg;
    uint32_t R = 255;
    uint32_t G = 0;
    uint32_t B = 0;
    uint32_t A = 200;
    uint32_t mscount = 50;
    captureHandle.GetValue()->AddOnGraphicDataUpdateDelegate([&,cap](ThroughCRTWrapper<std::shared_ptr<CaptureProcessHandle_t>>captureHandle) {
        width=captureHandle.GetValue()->GetClientWidth();
        height=captureHandle.GetValue()->GetClientHeight();

        hook_window_info_t hook_window_info;
        hook_window_info.hook_window_type = EHookWindowType::Background;
        hook_window_info.height = height;
        hook_window_info.width = width;
        windowHandle = cap->AddOverlayWindow(captureHandle.GetValue(), hook_window_info);

        windowHandle.GetValue()->AddMouseMotionEventDelegate([](ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, mouse_motion_event_t& e) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "mouse_motion_event_t: x {} y {} dx {} dy {}", e.x, e.y, e.xrel, e.yrel);
            });

        windowHandle.GetValue()->AddMouseButtonEventDelegate([](ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, mouse_button_event_t& e) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "mouse_button_event_t: x {} y {} state {} button {}", e.x, e.y, (int)e.state, (int)e.button);
            });

        windowHandle.GetValue()->AddMouseWheelEventDelegate([](ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, mouse_wheel_event_t& e) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "mouse_wheel_event_t: x {} y {} dx {} dy {}", e.x, e.y,e.preciseX,e.preciseY);
            });

        windowHandle.GetValue()->AddKeyboardEventDelegate([](ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, keyboard_event_t& e) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "keyboard_event_t: key_code {} ", (int)e.key_code);
            });   
        
        windowHandle.GetValue()->AddWindowEventDelegate([](ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, window_event_t& e) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "window_event_t: event{}", (int)e.event);
            });

        });
    while (true) {
        cap->CaptureTick(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(mscount));
        if (windowHandle.GetValue()) {
            windowImg.resize(windowHandle.GetValue()->GetDataSize());
            uint32_t val = ((A << 8 | R) << 8 | G) << 8 | B;
            std::fill((uint32_t*)windowImg.data(), (uint32_t*)(windowImg.data()+ windowImg.size()), val);
            cap->CopyData(windowHandle.GetValue(), windowImg.data());
        }

    }
}