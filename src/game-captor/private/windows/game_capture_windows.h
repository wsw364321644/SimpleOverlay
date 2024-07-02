#include <atomic>

#include <HOOK/hook_info.h>
#include <HOOK/window_info.h>
#include <RPC/MessageFactory.h>
#include <RPC/message_processer.h>
#include <RPC/rpc_processer.h>
#include <delegate_macros.h>
#include <simple_os_defs.h>
#include "game_capture.h"
struct CommonHandle_t;
class FGraphicSubsystem;
class FGraphicSubsystemDevice;
struct LocalHookInfo_t;
struct LocalHookWindowInfo_t;
class FGraphicSubsystemDXGITexture;
class FGraphicSubsystemTexture;
typedef struct LocalHookWindowInfo_t :CaptureWindowHandle_t {

	hook_window_info_t* SharedInfo;
	CommonHandle_t* SharedMemHandle;
	uint64_t WindowID{ 0 };
	std::shared_ptr<LocalHookInfo_t> Owner;
	FGraphicSubsystemDXGITexture* GraphicSubsystemSharedTexture{nullptr};
	FGraphicSubsystemTexture* GraphicSubsystemTexture{nullptr};
	~LocalHookWindowInfo_t() override;
	uint64_t GetID() const override {
		return WindowID;
	}
	uint64_t GetDataSize()const override {
		if (!GraphicSubsystemTexture) {
			return 0;
		}
		return GraphicSubsystemTexture->GetByteSize();
	}
	EGraphicSubsystemColorFormat GetDataColorFormat() const override {
		if (!GraphicSubsystemTexture) {
			return EGraphicSubsystemColorFormat::UNKNOWN;
		}
		return GraphicSubsystemTexture->GetColorFormat();
	}

}LocalHookWindowInfo_t;


typedef struct LocalHookInfo_t :CaptureProcessHandle_t {

	ECaptureStatus status;
	ECaptureError errCode;
	HANDLE windowsProcess{NULL};
	uint64_t processid;
	bool b64bit;
	std::string errMsg;

	HANDLE keepalive_mutex{ 0 };
	HANDLE hook_init{ NULL };
	HANDLE hook_restart{ NULL };
	HANDLE hook_stop{ NULL };
	HANDLE hook_ready{ NULL };
	HANDLE hook_exit{ NULL };
	HANDLE hook_data_map{ NULL };
	HANDLE texture_mutexes[2]{ NULL ,NULL };

	std::vector<std::shared_ptr<LocalHookWindowInfo_t>> Windows;
	hook_info_t* shared_hook_info;
	CommonHandle_t* shared_mem_handle;
	uint64_t GetID() override {
		return processid;
	}
	uint32_t GetClientWidth() override {
		return shared_hook_info->cx;
	}
	uint32_t GetClientHeight() override {
		return shared_hook_info->cy;
	}
}LocalHookInfo_t;



typedef struct SessionInfo_t{
	IMessageSession* Session;
	std::shared_ptr<MessageProcesser> PMessageProcesser;
	std::shared_ptr<RPCProcesser> PRPCProcesser;
	uint64_t ProcessId;
}SessionInfo_t;

class FGameCaptureWindows:public FGameCapture {
public:
	FGameCaptureWindows();
	void CaptureTick(float seconds) override;
	bool Init(const char* workpath) override;
	ThroughCRTWrapper<std::shared_ptr<CaptureProcessHandle_t>> StartCapture(uint64_t processid) override;
	ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> AddOverlayWindow(CaptureProcessHandle_t* handle,const hook_window_info_t info) override;
	bool CopyData(ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> handle, const uint8_t* data) override;

private:
	bool IsCapturing(LocalHookInfo_t* info);
	bool FinishCapture(LocalHookInfo_t* info, ECaptureError errorcode,const char* message=nullptr);
	bool InitHook(LocalHookInfo_t* info);
	bool AttemptExistingHook(LocalHookInfo_t* info);
	bool InjectHook(LocalHookInfo_t* info);
	bool InitHookSync(LocalHookInfo_t* info);
	bool InitHookInfo(LocalHookInfo_t* info);
	bool InitHookEvents(LocalHookInfo_t* info);
	static std::atomic_bool inited;
	static FGraphicSubsystem* GraphicSubsystem;
	static FGraphicSubsystemDevice* GraphicSubsystemDevice;
	static graphics_offsets_t offsets32;
	static graphics_offsets_t offsets64;
	std::unordered_map<uint64_t, std::shared_ptr<LocalHookInfo_t>> HookInfos;
	std::shared_ptr<IMessageServer> IpcServer;
	std::unordered_map<uint64_t, SessionInfo_t> sessionMap;
	std::filesystem::path Workpath;

	std::unordered_map<uint64_t, std::shared_ptr<LocalHookWindowInfo_t>> LocalWindowInfos;
};