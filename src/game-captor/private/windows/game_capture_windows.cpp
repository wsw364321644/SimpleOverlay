#include "game_capture_windows.h"
#include <ChildProcessManager.h>
#include <LoggerHelper.h>
#include <HOOK/hook_synchronized.h>
#include <RPC/JrpcHookHelperEvent.h>
#include <RPC/JrpcHookHelper.h>
#include <windows_helper.h>
#include <sm_util.h>
#include <INIReader.h>
#include <filesystem>
#include <LoggerHelper.h>
#include <Psapi.h>
#include <Graphic/GraphicSubsystem.h>
#include <Graphic/GraphicSubsystemDXGI.h>

std::atomic_bool FGameCaptureWindows::inited{ false };
FGraphicSubsystem* FGameCaptureWindows::GraphicSubsystem{ nullptr };
FGraphicSubsystemDevice* FGameCaptureWindows::GraphicSubsystemDevice{ nullptr };

graphics_offsets_t FGameCaptureWindows::offsets32 { 0 };
graphics_offsets_t FGameCaptureWindows::offsets64 { 0 };




FGameCapture* FGameCapture::CreateGameCapture()
{
    return new FGameCaptureWindows;
}


LocalHookWindowInfo_t::~LocalHookWindowInfo_t()
{
    if (SharedInfo) {
        UnmapSharedMemory(SharedInfo);
    }
    if (SharedMemHandle) {
        CloseSharedMemory(SharedMemHandle);
    }
}


FGameCaptureWindows::FGameCaptureWindows()
{
    LocalWindowInfos.emplace(0, std::make_shared<LocalHookWindowInfo_t>());
}

bool FGameCaptureWindows::Init(const char* workpath)
{ 
    if (inited) {
        return true;
    }


    if (workpath) {
        Workpath = std::filesystem::path(workpath);
    }
    else {
        return false;
    }
    IpcServer = NewMessageServer({ EMessageFoundation::LIBUV });
    IpcServer->OpenServer(EMessageConnectionType::EMCT_IPC, HOOK_IPC_PIPE);
    IpcServer->AddOnConnectDelegate(
        [&](IMessageSession* session) {
            auto PMessageProcesser = std::make_shared<MessageProcesser>(session);
            std::shared_ptr<RPCProcesser> PRPCProcesser = std::make_shared<RPCProcesser>(PMessageProcesser.get());
            auto rp = sessionMap.emplace(std::piecewise_construct, std::make_tuple(session->GetPID()), std::make_tuple(session,PMessageProcesser, PRPCProcesser, session->GetPID()));
            if (!rp.second) {
                SIMPLELOG_LOGGER_ERROR(nullptr, "add MessageProcesser error");
                return;
            }
            SessionInfo_t& SessionInfo= rp.first->second;
            PRPCProcesser->AddOnRPCConsumedErrorDelegate(
                [&, session, PRPCProcesser](std::shared_ptr<RPCRequest>) {

                    session->Disconnect();
                }
            );
            session->AddOnDisconnectDelegate(
                [&](IMessageSession* session) {
                    sessionMap.erase(session->GetPID());
                    auto itr = HookInfos.find(session->GetPID());
                    if (itr == HookInfos.end()) {
                        return;
                    }
                    itr->second->status = ECaptureStatus::ECS_None;
                }
            );
            auto HookHelperEventInterface = PRPCProcesser->GetInterface<JRPCHookHelperEventAPI>();
            auto HookHelperInterface = PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
            HookHelperInterface->RegisterConnectToHost(
                [&,HookHelperInterface, HookHelperEventInterface](RPCHandle_t handle,uint64_t processId, const char* commandline) {
                    if (HookInfos.find(processId) == HookInfos.end()) {
                        HookHelperInterface->RespondError(handle,-1);
                        return;
                    }
                    if (SessionInfo.ProcessId != processId) {
                        HookHelperInterface->RespondError(handle, -1);
                        return;
                    }
                    HookHelperInterface->RespondConnectToHost(handle);

                    HotKeyList_t list;
                    list.emplace(key_with_modifier_t{ SDLK_TAB,KMOD_CTRL }, OVERLAY_HOT_KEY_NAME);
                    HookHelperEventInterface->HotkeyListUpdate(list);
                }
            );

            HookHelperEventInterface->RegisterOverlayMouseMotionEvent([&](uint64_t winId, mouse_motion_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo=itr->second;
                pWinInfo->TriggerMouseMotionEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo),e);
                });

            HookHelperEventInterface->RegisterOverlayMouseButtonEvent([&](uint64_t winId, mouse_button_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo = itr->second;
                pWinInfo->TriggerMouseButtonEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo), e);
                });

            HookHelperEventInterface->RegisterOverlayMouseWheelEvent([&](uint64_t winId, mouse_wheel_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo = itr->second;
                pWinInfo->TriggerMouseWheelEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo), e);
                });

            HookHelperEventInterface->RegisterOverlayKeyboardEvent([&](uint64_t winId, keyboard_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo = itr->second;
                pWinInfo->TriggerKeyboardEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo), e);
                });

            HookHelperEventInterface->RegisterOverlayCharEvent([&](uint64_t winId, overlay_char_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo = itr->second;
                pWinInfo->TriggerOverlayCharEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo), e);
                });
            HookHelperEventInterface->RegisterOverlayWindowEvent([&](uint64_t winId, window_event_t& e) {
                auto itr = LocalWindowInfos.find(winId);
                if (itr == LocalWindowInfos.end()) {
                    return;
                }
                auto pWinInfo = itr->second;
                pWinInfo->TriggerWindowEventDelegates(std::dynamic_pointer_cast<CaptureWindowHandle_t>(pWinInfo), e);
                });
        }
    );


    auto init64 = std::thread([&]() {
        {
            auto fpath = Workpath/ "get-graphics-offsets64.exe";
            if (!std::filesystem::exists(fpath)) {
                return;
            }

            FChildProcessManager ChildProcessManager;
            auto handle = ChildProcessManager.SpawnProcess((char*)fpath.u8string().c_str());
            if (ChildProcessManager.CheckIsFinished(handle)) {
                return;
            }
            std::string* pstrbuf = new std::string;
            ChildProcessManager.RegisterOnRead(handle, [&](CommonHandle_t handle, const char* str, ssize_t size) {
                {
                    auto& strbuf = *pstrbuf;
                    if (size >= 0) {
                        strbuf.append(str, size);
                        return;
                    }
                }
                }
            );
            ChildProcessManager.RegisterOnExit(handle,
                [&](CommonHandle_t, int64_t, int) {
                    auto& strbuf = *pstrbuf;
                    load_offsets_from_string(&offsets64, strbuf.c_str());
                    delete pstrbuf;
                }
            );
            ChildProcessManager.Run();
        }
        });
    init64.join();
    auto init32 = std::thread([&]() {
        {
            auto fpath = Workpath/ "get-graphics-offsets32.exe";
            if (!std::filesystem::exists(fpath)) {
                return;
            }

            FChildProcessManager ChildProcessManager;
            auto handle = ChildProcessManager.SpawnProcess((char*)fpath.u8string().c_str());
            if (ChildProcessManager.CheckIsFinished(handle)) {
                return;
            }
            std::string* pstrbuf = new std::string;
            ChildProcessManager.RegisterOnRead(handle, [&](CommonHandle_t handle, const char* str, ssize_t size) {
                {
                    auto& strbuf = *pstrbuf;
                    if (size >= 0) {
                        strbuf.append(str, size);
                        return;
                    }
                }
                }
            );
            ChildProcessManager.RegisterOnExit(handle,
                [&](CommonHandle_t, int64_t, int) {
                    auto& strbuf = *pstrbuf;
                    load_offsets_from_string(&offsets32, strbuf.c_str());
                    delete pstrbuf;
                }
            );
            ChildProcessManager.Run();
        }
        });
    init32.join();
    

    GraphicSubsystem = GetGraphicSubsystem(EGraphicSubsystem::DX11);
    //uint32_t id;
    //GraphicSubsystem->DeviceEnumAdapters(
    //    [](void* param, const char* name, uint32_t id)->bool {
    //        uint32_t* id = (uint32_t*)param;
    //        return true;
    //    }, & id);
    GraphicSubsystem->DeviceCreate(&GraphicSubsystemDevice, 0);

    
    inited = true;
    return inited;
}

ThroughCRTWrapper<std::shared_ptr<CaptureProcessHandle_t>> FGameCaptureWindows::StartCapture(uint64_t _processid)
{
    auto localInfo = std::make_shared<LocalHookInfo_t>();
    localInfo->processid = _processid;
    localInfo->status = ECaptureStatus::ECS_Initing;
    if (!InitHook(localInfo.get())) {
        FinishCapture(localInfo.get(), ECaptureError::ECS_OpenProcessError);
        return nullptr;
    }
    localInfo->status = ECaptureStatus::ECS_Inited;
    HookInfos.emplace(localInfo->processid,localInfo);
    return std::dynamic_pointer_cast<CaptureProcessHandle_t>(localInfo);
}

ThroughCRTWrapper<std::shared_ptr<CaptureWindowHandle_t>> FGameCaptureWindows::AddOverlayWindow(CaptureProcessHandle_t* handle, const hook_window_info_t info)
{
    auto itr=HookInfos.find(handle->GetID());
    if (itr == HookInfos.end()) {
        return nullptr;
    }
    auto& plocalInfo = itr->second;
    if (plocalInfo->status != ECaptureStatus::ECS_Ready) {
        return nullptr;
    }
    if (LocalWindowInfos.size() == std::numeric_limits<uint64_t>::max()) {
        return nullptr;
    }
    uint64_t newWindowID = 1;
    for (; newWindowID <= std::numeric_limits<uint64_t>::max(); newWindowID++) {
        if (!LocalWindowInfos.contains(newWindowID)) {
            break;
        }
    }

    auto windowInfo = std::make_shared<LocalHookWindowInfo_t>();
    windowInfo->Owner = plocalInfo;
    windowInfo->WindowID= newWindowID;
    windowInfo->SharedMemHandle= CreateSharedMemory(GetNamePlusID(SHMEM_HOOK_WINDOW_INFO, newWindowID).c_str(),sizeof(hook_window_info_t));
    if (!windowInfo->SharedMemHandle || !windowInfo->SharedMemHandle->IsValid()) {
        return nullptr;
    }
    windowInfo->SharedInfo = (hook_window_info_t*)MapSharedMemory(windowInfo->SharedMemHandle);
    if (!windowInfo->SharedInfo) {
        return nullptr;
    }
    *windowInfo->SharedInfo = info;

    if (!InterUpdateOverlayWindowTextureSize(windowInfo.get())) {
        return nullptr;
    }
    if (!DuplicateWindowTextureHandle(windowInfo.get())) {
        ReleaseWindowTexture(windowInfo.get());
        return nullptr;
    }
    auto HookHelperInterface = sessionMap[handle->GetID()].PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
    windowInfo->UpdateWindowTextureRpcHandle= HookHelperInterface->AddWindow(windowInfo->GetID(), GetNamePlusID(SHMEM_HOOK_WINDOW_INFO, newWindowID).c_str(),
        [windowInfo](RPCHandle_t handle) {
            windowInfo->UpdateWindowTextureRpcHandle = NullHandle;
        },
        [windowInfo](RPCHandle_t, int64_t code, const char*, const char*) {
            SIMPLELOG_LOGGER_DEBUG(nullptr, "AddWindow rpc error:{}", code);
            windowInfo->UpdateWindowTextureRpcHandle = NullHandle;
        });

    LocalWindowInfos.emplace(windowInfo->GetID(), windowInfo);
    return std::dynamic_pointer_cast<CaptureWindowHandle_t>(windowInfo);
}

bool FGameCaptureWindows::UpdateOverlayWindowTextureSize(CaptureWindowHandle_t* windowHanlde,  uint16_t width, uint16_t height)
{
    LocalHookWindowInfo_t* windowInfo = dynamic_cast<LocalHookWindowInfo_t*>(windowHanlde);
    if (windowInfo->SharedInfo->render_width == width && windowInfo->SharedInfo->render_height == height) {
        return true;
    }
    if (windowInfo->Owner->status != ECaptureStatus::ECS_Ready) {
        return false;
    }
    auto cacheWidth = windowInfo->SharedInfo->render_width;
    auto cacheHeight = windowInfo->SharedInfo->render_height;
    SyncWindowTextureCache_t syncCache{ .GraphicSubsystemSharedTexture=windowInfo->GraphicSubsystemSharedTexture ,
        .GraphicSubsystemTexture=windowInfo->GraphicSubsystemTexture };

    auto restoreFn = [&cacheWidth, &cacheHeight, &syncCache, &windowInfo]() {
        windowInfo->SharedInfo->render_width = cacheWidth;
        windowInfo->SharedInfo->render_height = cacheHeight;
        windowInfo->GraphicSubsystemSharedTexture = syncCache.GraphicSubsystemSharedTexture;
        windowInfo->GraphicSubsystemTexture = syncCache.GraphicSubsystemTexture;
        };
    windowInfo->SharedInfo->render_width = width;
    windowInfo->SharedInfo->render_height = height;
    
    if (!InterUpdateOverlayWindowTextureSize(windowInfo)) {
        restoreFn();
        return false;
    }
    if (windowInfo->bRequestSyncWindowTexture) {
        ReleaseSyncWindowTextureCache(syncCache);
    }
    else {
        windowInfo->SyncWindowTextureCache=syncCache;
        windowInfo->bRequestSyncWindowTexture = true;
    }

    return true;
}

void FGameCaptureWindows::RemoveOverlayWindow( CaptureWindowHandle_t* windowHanlde)
{
    auto winId = windowHanlde->GetID();
    auto windowItr=LocalWindowInfos.find(winId);
    if (windowItr == LocalWindowInfos.end()) {
        return;
    }
    auto& pLocalWindowInfo=windowItr->second;
    if (pLocalWindowInfo->bRequestRemove) {
        return;
    }
    if (pLocalWindowInfo->Owner->status != ECaptureStatus::ECS_Ready) {
        return;
    }
    pLocalWindowInfo->bRequestRemove = true;

    auto HookHelperInterface = sessionMap[pLocalWindowInfo->Owner->GetID()].PRPCProcesser->GetInterface<JRPCHookHelperAPI>();
    HookHelperInterface->RemoveWindow(windowHanlde->GetID(),
        [&, pLocalWindowInfo](RPCHandle_t handle) {
            ReleaseWindowTexture(pLocalWindowInfo.get());
            LocalWindowInfos.erase(pLocalWindowInfo->GetID());
        },
        [&, pLocalWindowInfo](RPCHandle_t, int64_t, const char*, const char*) {
            pLocalWindowInfo->bRequestRemove = false;
        });
}

void FGameCaptureWindows::ShowOverlayWindow(CaptureWindowHandle_t* windowHanlde)
{
    auto winId = windowHanlde->GetID();
    auto windowItr = LocalWindowInfos.find(winId);
    if (windowItr == LocalWindowInfos.end()) {
        return;
    }
    auto& pLocalWindowInfo = windowItr->second;
    pLocalWindowInfo->SharedInfo->bShow = true;
}

void FGameCaptureWindows::HideOverlayWindow(CaptureWindowHandle_t* windowHanlde)
{
    auto winId = windowHanlde->GetID();
    auto windowItr = LocalWindowInfos.find(winId);
    if (windowItr == LocalWindowInfos.end()) {
        return;
    }
    auto& pLocalWindowInfo = windowItr->second;
    pLocalWindowInfo->SharedInfo->bShow = false;
}

bool FGameCaptureWindows::CopyData(CaptureWindowHandle_t* handle, const uint8_t* data)
{
    auto itr = LocalWindowInfos.find(handle->GetID());
    if (itr == LocalWindowInfos.end()) {
        return false;
    }
    auto& pwindowInfo=itr->second;
    auto& pcapInfo= pwindowInfo->Owner;
    uint8_t* outPtr;
    uint32_t RowPitch;
    auto width= pwindowInfo->GraphicSubsystemTexture->GetWidth();
    auto height =  pwindowInfo->GraphicSubsystemTexture->GetHeight();
    auto lineSize=pwindowInfo->GraphicSubsystemTexture->GetByteSize()/ height;
    if (!GraphicSubsystem->TextureMap(pwindowInfo->GraphicSubsystemTexture, &outPtr, &RowPitch)) {
        return false;
    }
    for (int i = 0; i < height; i++) {
        memcpy(outPtr + i * RowPitch, data + i * lineSize, lineSize);
    }
    GraphicSubsystem->TextureUnmap(pwindowInfo->GraphicSubsystemTexture);
    if (!pwindowInfo->GraphicSubsystemSharedTexture->AcquireSync(0,0)) {
        return false;
    }
    GraphicSubsystem->DeviceCopyTexture(GraphicSubsystemDevice, pwindowInfo->GraphicSubsystemSharedTexture, pwindowInfo->GraphicSubsystemTexture);
    pwindowInfo->GraphicSubsystemSharedTexture->ReleaseSync(1);
    return true;
}

void FGameCaptureWindows::CaptureTick(float seconds)
{
    if (!inited) {
        return;
    }
    for (auto pair: HookInfos) {
        auto hookInfo = pair.second;
        if (!IsCapturing(hookInfo.get())) {
            continue;
        }

        switch (hookInfo->status) {
        case ECaptureStatus::ECS_Inited: {
            if (!AttemptExistingHook(hookInfo.get())) {
                if (!InjectHook(hookInfo.get())) {
                    FinishCapture(hookInfo.get(), ECaptureError::ECS_InjectFailed);
                    continue;
                }
            }
            break;
        }
        case ECaptureStatus::ECS_HookSyncing:{
            if (!InitHookSync(hookInfo.get())) {
                //FinishCapture(hookInfo.get(), ECaptureError::ECS_SyncError);
                continue;
            }
            break;
        }
        case ECaptureStatus::ECS_GraphicDataSyncing: {
            if (!sessionMap.contains(hookInfo->GetID())) {
                continue;
            }
            auto handle=open_mutex_plus_id(HOOK_READY_KEEPALIVE, hookInfo->GetID(), false);
            if (handle != NULL) {
                CloseHandle(handle);
                hookInfo->status = ECaptureStatus::ECS_Ready;
                hookInfo->TriggerOnGraphicDataUpdateDelegates(std::dynamic_pointer_cast<CaptureProcessHandle_t>(hookInfo));
            }
            
            break;
        }
        case ECaptureStatus::ECS_Ready:{
            auto handle = open_mutex_plus_id(HOOK_READY_KEEPALIVE, hookInfo->GetID(), false);
            if (handle != NULL) {
                CloseHandle(handle);
            }
            else {
                hookInfo->status = ECaptureStatus::ECS_GraphicDataSyncing;
            }
            for (auto& pair : LocalWindowInfos) {
                auto& pWinInfo = pair.second;
                if (pWinInfo->bRequestSyncWindowTexture&& !pWinInfo->UpdateWindowTextureRpcHandle.IsValid()) {
                    if (!DuplicateWindowTextureHandle(pWinInfo.get())) {
                        SIMPLELOG_LOGGER_ERROR(nullptr, "DuplicateWindowTextureHandle error");
                        continue;
                    }
                    auto SyncWindowTextureCache=pWinInfo->SyncWindowTextureCache;
                    auto HookHelperInterface = sessionMap[pWinInfo->Owner->GetID()].PRPCProcesser->GetInterface<JRPCHookHelperAPI>();

                    pWinInfo->UpdateWindowTextureRpcHandle = HookHelperInterface->UpdateWindowTexture(pWinInfo->GetID(),
                        [&,pWinInfo, SyncWindowTextureCache](RPCHandle_t handle) {
                            ReleaseSyncWindowTextureCache(SyncWindowTextureCache);
                            pWinInfo->UpdateWindowTextureRpcHandle = NullHandle;
                        },
                        [&, pWinInfo, SyncWindowTextureCache](RPCHandle_t, int64_t code, const char*, const char*) {
                            SIMPLELOG_LOGGER_DEBUG(nullptr, "UpdateWindowTexture failed error:{}", code);
                            ReleaseSyncWindowTextureCache(SyncWindowTextureCache);
                            pWinInfo->UpdateWindowTextureRpcHandle = NullHandle;
                        });
                    pWinInfo->bRequestSyncWindowTexture = false;
                }
            }
            break;
        }
        }
    }
    IpcServer->Tick(seconds);
}

void FGameCaptureWindows::ReleaseWindowTexture(LocalHookWindowInfo_t* windowInfo)
{
    if (windowInfo->GraphicSubsystemSharedTexture) {
        GraphicSubsystem->TextureDestroy(windowInfo->GraphicSubsystemSharedTexture);
    }
    if (windowInfo->GraphicSubsystemTexture) {
        GraphicSubsystem->TextureDestroy(windowInfo->GraphicSubsystemTexture);
    }
}

void FGameCaptureWindows::ReleaseSyncWindowTextureCache(SyncWindowTextureCache_t Cache)
{
    if (Cache.GraphicSubsystemSharedTexture) {
        GraphicSubsystem->TextureDestroy(Cache.GraphicSubsystemSharedTexture);
    }
    if (Cache.GraphicSubsystemTexture) {
        GraphicSubsystem->TextureDestroy(Cache.GraphicSubsystemTexture);
    }
}

bool FGameCaptureWindows::InterUpdateOverlayWindowTextureSize(LocalHookWindowInfo_t* windowInfo)
{
    TextureFlag_t flags;
    flags.set(ETextureFlag::MISC_SHARED_KEYEDMUTEX);
    flags.set(ETextureFlag::MISC_SHARED_NTHANDLE);
    //flags.set(ETextureFlag::MISC_SHARED);
    flags.set(ETextureFlag::BIND_SHADER_RESOURCE);
    flags.set(ETextureFlag::BIND_RENDER_TARGET);
    auto tex = GraphicSubsystem->DeviceTextureCreate(GraphicSubsystemDevice, windowInfo->SharedInfo->render_width, windowInfo->SharedInfo->render_height,
        ConvertDXGITextureFormat((DXGI_FORMAT)windowInfo->Owner->shared_hook_info->format), 1, nullptr, flags);
    if (!tex) {
        return false;
    }
    windowInfo->GraphicSubsystemSharedTexture = dynamic_cast<FGraphicSubsystemDXGITexture*>(tex);
    if (!windowInfo->GraphicSubsystemSharedTexture) {
        return false;
    }
    flags.reset();
    flags.set(ETextureFlag::CPU_ACCESS_WRITE);
    flags.set(ETextureFlag::BIND_SHADER_RESOURCE);
    windowInfo->GraphicSubsystemTexture = GraphicSubsystem->DeviceTextureCreate(GraphicSubsystemDevice, windowInfo->SharedInfo->render_width, windowInfo->SharedInfo->render_height,
        ConvertDXGITextureFormat((DXGI_FORMAT)windowInfo->Owner->shared_hook_info->format), 1, nullptr, flags);
    if (!windowInfo->GraphicSubsystemTexture) {
        ReleaseWindowTexture(windowInfo);
        return false;
    }
    return true;
}

bool FGameCaptureWindows::DuplicateWindowTextureHandle(LocalHookWindowInfo_t* windowInfo)
{
    windowInfo->SharedInfo->bNT_shared = windowInfo->GraphicSubsystemSharedTexture->IsNTShared();
    HANDLE outHandle = (HANDLE)(intptr_t)windowInfo->GraphicSubsystemSharedTexture->GetSharedHandle();
    if (windowInfo->SharedInfo->bNT_shared) {
        HANDLE TargetHandle = (HANDLE)(intptr_t)windowInfo->GraphicSubsystemSharedTexture->GetSharedHandle();
        auto bres = DuplicateHandle(GetCurrentProcess(), TargetHandle,
            windowInfo->Owner->windowsProcess, &outHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (!bres) {
            auto err = GetLastError();
            SIMPLELOG_LOGGER_ERROR(nullptr, "DuplicateHandle failed:{}", err);
            return false;
        }
    }
    windowInfo->SharedInfo->shared_handle = (intptr_t)outHandle;
    return true;
}

bool FGameCaptureWindows::IsCapturing(LocalHookInfo_t* info)
{
    return info->status != ECaptureStatus::ECS_None;
}

bool FGameCaptureWindows::FinishCapture(LocalHookInfo_t* info, ECaptureError errorcode, const char* message)
{
    info->errCode = errorcode;
    //if (errorcode == ECaptureError::ECS_OK) {
    //    info->status = ECaptureStatus::ECS_None;
    //    return true;
    //}
    info->status = ECaptureStatus::ECS_None;
    if (message) {
        info->errMsg = message;
    }
    return true;
}

bool FGameCaptureWindows::InitHook(LocalHookInfo_t* localInfo)
{
    localInfo->windowsProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE| SYNCHRONIZE, false, localInfo->processid);
    if (localInfo->windowsProcess == NULL) {
        return false;
    }
    localInfo->b64bit = is_64bit_process(localInfo->windowsProcess);
    localInfo->keepalive_mutex = create_mutex_plus_id(WINDOW_HOOK_KEEPALIVE, localInfo->processid,false);
    if (!localInfo->keepalive_mutex) {
        SIMPLELOG_LOGGER_ERROR(nullptr,"Failed to create keepalive mutex: {}", GetLastError());
        return false;
    }
    return true;
}

bool FGameCaptureWindows::AttemptExistingHook(LocalHookInfo_t* info)
{
    info->hook_restart = open_event_plus_id(EVENT_CAPTURE_RESTART, info->processid,false);
    if (info->hook_restart) {
        char szProcessName[MAX_PATH] = TEXT("<unknown>");
        get_process_file_name_by_handle(info->windowsProcess,NULL, szProcessName, MAX_PATH);
        SIMPLELOG_LOGGER_INFO(nullptr,"existing hook found, signaling process: {}", szProcessName);
        SetEvent(info->hook_restart);
        info->status = ECaptureStatus::ECS_HookSyncing;
        return true;
    }
    return false;
}

bool FGameCaptureWindows::InjectHook(LocalHookInfo_t* info)
{

    //std::thread([&]() {
    //    {
    //        std::filesystem::path inject_path, hook_path;

    //        if (is64bit) {
    //            inject_path = Workpath / "inject-helper64.exe";
    //            hook_path = Workpath / ".." / "sdk" / "graphics_hook64.dll";
    //        }
    //        else {
    //            inject_path = Workpath / "inject-helper32.exe";
    //            hook_path = Workpath / ".." / "sdk" / "graphics_hook32.dll";
    //        }
    //        if (!std::filesystem::exists(inject_path)) {
    //            return;
    //        }
    //        if (!std::filesystem::exists(hook_path)) {
    //            return;
    //        }

    //        FChildProcessManager ChildProcessManager;
    //        auto hook_pathu8 = hook_path.u8string();
    //        const char* args[2]{ (const char*)hook_pathu8.c_str(),0 };
    //        auto handle = ChildProcessManager.SpawnProcess((char*)inject_path.u8string().c_str(), args);
    //        if (ChildProcessManager.CheckIsFinished(handle)) {
    //            return;
    //        }
    //        ChildProcessManager.Run();
    //    }
    //    }).detach();


    std::filesystem::path inject_path, hook_path;

    if (info->b64bit) {
        inject_path = Workpath / "inject-helper64.exe";
#ifdef NDEBUG
        hook_path = Workpath/ "graphics_hook64.dll";
#else
        //hook_path = "C:/Project/SDK/build64/rundir/sdk/bin/graphics_hook64.dll";
        hook_path = "C:/Project/SimpleOverlayDll/build64/bin/Debug/graphics_hook64.dll";
#endif
    }
    else {
        inject_path = Workpath / "inject-helper32.exe";
#ifdef NDEBUG
        hook_path = Workpath/ "graphics_hook32.dll";
#else
        hook_path = "C:/Project/SimpleOverlayDll/build64/bin/Debug/graphics_hook32.dll";
#endif
    }
    if (!std::filesystem::exists(inject_path)) {
        return false;
    }
    if (!std::filesystem::exists(hook_path)) {
        return false;
    }

    FChildProcessManager ChildProcessManager;
    auto hook_pathu8 = hook_path.u8string();
    auto processidstr=std::to_string(info->processid);
    const char* args[4]{ (const char*)hook_pathu8.c_str(),"0",processidstr.c_str(),0};
    auto handle = ChildProcessManager.SpawnProcess((char*)inject_path.u8string().c_str(), args);
    if (ChildProcessManager.CheckIsFinished(handle)) {
        return false;
    }
    ChildProcessManager.RegisterOnExit(handle,[&,info](CommonHandle_t, int64_t exit_status, int signal) {
        if (!exit_status|| int32_t(exit_status)==-4) {
            info->status = ECaptureStatus::ECS_HookSyncing;
        }
        else {
            FinishCapture(info, ECaptureError::ECS_InjectFailed,std::format("inject process faild with {} sig {}", exit_status, signal).c_str());
        }
    });
    info->status = ECaptureStatus::ECS_Injecting;
    ChildProcessManager.Run();
    return true;
}

bool FGameCaptureWindows::InitHookSync(LocalHookInfo_t* info)
{
    info->status = ECaptureStatus::ECS_HookSyncing;

    info->texture_mutexes[0] = open_mutex_plus_id(MUTEX_TEXTURE1, info->processid, false);
    info->texture_mutexes[1] = open_mutex_plus_id(MUTEX_TEXTURE2, info->processid, false);
    if (!info->texture_mutexes[0] || !info->texture_mutexes[1]) {
        return false;
    }

    if (!InitHookInfo(info)){
        return false;
    }
    if (!InitHookEvents(info)) {
        return false;
    }

    SetEvent(info->hook_init);
    info->status = ECaptureStatus::ECS_GraphicDataSyncing;
    return true;
}

bool FGameCaptureWindows::InitHookInfo(LocalHookInfo_t* info)
{
    info->shared_mem_handle = OpenSharedMemory(GetNamePlusID(SHMEM_HOOK_INFO, info->processid).c_str());
    if (!info->shared_mem_handle || !info->shared_mem_handle->IsValid()) {
        return false;
    }
    info->shared_hook_info = (hook_info_t*)MapSharedMemory(info->shared_mem_handle);
    if (!info->shared_hook_info) {
        return false;
    }
    if (info->b64bit) {
        info->shared_hook_info->offsets = offsets64;
    }
    else {
        info->shared_hook_info->offsets = offsets32;
    }

    info->shared_hook_info->capture_overlay = false;
    info->shared_hook_info->force_shmem = false;
    info->shared_hook_info->UNUSED_use_scale = false;
    info->shared_hook_info->allow_srgb_alias = true;
    info->shared_hook_info->frame_interval = get_default_frame_interval();
    return true;
}

bool FGameCaptureWindows::InitHookEvents(LocalHookInfo_t* info)
{
    if (!info->hook_restart) {
        info->hook_restart = open_event_plus_id(EVENT_CAPTURE_RESTART, info->processid, false);
        if (!info->hook_restart) {
            SIMPLELOG_LOGGER_ERROR(nullptr,"init_events: failed to get hook_restart event: {}", GetLastError());
            return false;
        }
    }

    if (!info->hook_stop) {
        info->hook_stop = open_event_plus_id(EVENT_CAPTURE_STOP, info->processid, false); 
        if (!info->hook_stop) {
            SIMPLELOG_LOGGER_ERROR(nullptr, "init_events: failed to get hook_stop event: {}", GetLastError());
            return false;
        }
    }

    if (!info->hook_init) {
        info->hook_init = open_event_plus_id(EVENT_HOOK_INIT, info->processid, false); 
        if (!info->hook_init) {
            SIMPLELOG_LOGGER_ERROR(nullptr, "init_events: failed to get hook_init event: {}", GetLastError());
            return false;
        }
    }

    if (!info->hook_ready) {
        info->hook_ready = open_event_plus_id(EVENT_HOOK_READY, info->processid, false);
        if (!info->hook_ready) {
            SIMPLELOG_LOGGER_ERROR(nullptr, "init_events: failed to get hook_ready event: {}", GetLastError());
            return false;
        }
    }

    if (!info->hook_exit) {
        info->hook_exit = open_event_plus_id(EVENT_HOOK_EXIT, info->processid, false);
        if (!info->hook_exit) {
            SIMPLELOG_LOGGER_ERROR(nullptr, "init_events: failed to get hook_exit event: {}", GetLastError());
            return false;
        }
    }

    return true;
}
