#pragma once

#include <filesystem>
#include <unordered_map>
#include <memory>
#include <delegate_macros.h>
#include <ThroughCRTWrapper.h>
#include <HOOK/mouse_event.h>
#include <HOOK/window_event.h>
#include <HOOK/input_event.h>
#include <HOOK/window_info.h>
#include <Graphic/GraphicSubsystem.h>
#include "game_capture_export_defs.h"

#define INVALID_CAPTURE_ID 0

struct graphics_offsets_t;

enum class ECaptureStatus
{
	ECS_None,
	ECS_Initing,
	ECS_Inited,
	ECS_Injecting,
	ECS_HookSyncing,
	ECS_GraphicDataSyncing,
	ECS_Ready,
};

enum class ECaptureError
{
	ECS_OK,
	ECS_InjectFailed,
	ECS_OpenProcessError,
	ECS_SyncError,
};

struct GAME_CAPTURE_EXPORT CaptureProcessHandle_t {
	virtual ~CaptureProcessHandle_t() = default;
	virtual uint64_t GetID() = 0;
	virtual uint32_t GetClientWidth() = 0;
	virtual uint32_t GetClientHeight() = 0;

	DEFINE_EVENT_ONE_PARAM(OnGraphicDataUpdate, ThroughCRTWrapper<std::shared_ptr<CaptureProcessHandle_t>>);
};

struct GAME_CAPTURE_EXPORT CaptureWindowHandle_t {
	virtual ~CaptureWindowHandle_t() = default;
	virtual uint64_t GetID() const  = 0;
	virtual uint64_t GetDataSize() const = 0;
	virtual EGraphicSubsystemColorFormat GetDataColorFormat()const = 0;
	DEFINE_EVENT_TWO_PARAM(MouseWheelEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, mouse_wheel_event_t&);
	DEFINE_EVENT_TWO_PARAM(MouseButtonEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, mouse_button_event_t&);
	DEFINE_EVENT_TWO_PARAM(MouseMotionEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, mouse_motion_event_t&);
	DEFINE_EVENT_TWO_PARAM(KeyboardEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, keyboard_event_t&);
	DEFINE_EVENT_TWO_PARAM(OverlayCharEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, overlay_char_event_t&);
	DEFINE_EVENT_TWO_PARAM(WindowEvent, ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>, window_event_t&);
};

class GAME_CAPTURE_EXPORT FGameCapture {
public:
	static FGameCapture* CreateGameCapture();
	virtual void CaptureTick(float seconds) = 0;
	virtual bool Init(const char* workpath = nullptr) = 0;
	virtual ThroughCRTWrapper<std::shared_ptr<CaptureProcessHandle_t>> StartCapture(uint64_t processid) = 0;
	virtual ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>>  AddOverlayWindow(CaptureProcessHandle_t* handle, const hook_window_info_t info) = 0;
	virtual void RemoveOverlayWindow(CaptureWindowHandle_t* windowHanlde) =0;
	virtual bool CopyData(CaptureWindowHandle_t* handle,const uint8_t* data) = 0;
};

GAME_CAPTURE_EXPORT uint64_t get_default_frame_interval();
GAME_CAPTURE_EXPORT bool load_offsets_from_string(graphics_offsets_t* offsets, const char* str);