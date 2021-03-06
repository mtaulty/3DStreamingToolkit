#define SUPPORT_D3D11		1
#define WEBRTC_WIN			1

#define SHOW_CONSOLE 0

// ImageNativeServerPlugin.cpp : Defines the exported functions for the DLL application.
//
#include <iostream>
#include <thread>
#include <string>
#include <fstream>
#include <cstdint>

#include "config_parser.h"
#include "flagdefs.h"
#include "multi_peer_conductor.h"
#include "ImageMultiPeerConductor.h"
#include "server_main_window.h"

#include "webrtc/rtc_base/checks.h"
#include "webrtc/rtc_base/ssladapter.h"
#include "webrtc/rtc_base/win32socketinit.h"
#include "webrtc/rtc_base/win32socketserver.h"
#include "webrtc/rtc_base/logging.h"

#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "dmoguids.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "msdmo.lib")
#pragma comment(lib, "strmiids.lib")

#pragma comment(lib, "common_video.lib")
#pragma comment(lib, "webrtc.lib")
#pragma comment(lib, "boringssl_asm.lib")
#pragma comment(lib, "field_trial_default.lib")
#pragma comment(lib, "metrics_default.lib")
#pragma comment(lib, "protobuf_full.lib")

#define ULOG(sev, msg)						\
	if (s_callbackMap.onLog)				\
	{										\
		(*s_callbackMap.onLog)(sev, msg);	\
	}										\
											\
	LOG(sev) << msg		

typedef void(__stdcall*NoParamFuncType)();
typedef void(__stdcall*IntParamFuncType)(const int val);
typedef void(__stdcall*BoolParamFuncType)(const bool val);
typedef void(__stdcall*StrParamFuncType)(const char* str);
typedef void(__stdcall*IntStringParamsFuncType)(const int val, const char* str);

static std::thread*					s_messageThread;
static rtc::Thread*					s_rtcMainThread;
static std::string					s_server = "signalingserveruri";
static uint32_t						s_port = 3000;
static std::shared_ptr<ImageMultiPeerConductor> s_cond;
static IntStringParamsFuncType s_pCallbackFunction = nullptr;


struct CallbackMap
{
	IntStringParamsFuncType onDataChannelMessage;
	IntStringParamsFuncType onLog;
	IntStringParamsFuncType onPeerConnect;
	IntParamFuncType onPeerDisconnect;
	NoParamFuncType onSignIn;
	NoParamFuncType onDisconnect;
	IntStringParamsFuncType onMessageFromPeer;
	IntParamFuncType onMessageSent;
	NoParamFuncType onServerConnectionFailure;
	IntParamFuncType onSignalingChange;
	StrParamFuncType onAddStream;
	StrParamFuncType onRemoveStream;
	StrParamFuncType onDataChannel;
	NoParamFuncType onRenegotiationNeeded;
	IntParamFuncType onIceConnectionChange;
	IntParamFuncType onIceGatheringChange;
	StrParamFuncType onIceCandidate;
	BoolParamFuncType onIceConnectionReceivingChange;
} s_callbackMap;

struct UnityServerPeerObserver : 
	public PeerConnectionClientObserver,
	public webrtc::PeerConnectionObserver
{
	virtual void OnSignedIn() override
	{
		if (s_callbackMap.onSignIn)
		{
			(*s_callbackMap.onSignIn)();
		}
	}

	virtual void OnDisconnected() override
	{
		if (s_callbackMap.onDisconnect)
		{
			(*s_callbackMap.onDisconnect)();
		}
	}

	virtual void OnPeerConnected(int id, const std::string& name) override
	{
		if (s_callbackMap.onPeerConnect)
		{
			(*s_callbackMap.onPeerConnect)(id, name.c_str());
		}
	}

	virtual void OnPeerDisconnected(int peer_id) override
	{
		if (s_callbackMap.onPeerDisconnect)
		{
			(*s_callbackMap.onPeerDisconnect)(peer_id);
		}
	}

	virtual void OnMessageFromPeer(int peer_id, const std::string& message) override
	{
		if (s_callbackMap.onMessageFromPeer)
		{
			(*s_callbackMap.onMessageFromPeer)(peer_id, message.c_str());
		}
	}

	virtual void OnMessageSent(int err) override
	{
		if (s_callbackMap.onMessageSent)
		{
			(*s_callbackMap.onMessageSent)(err);
		}
	}

	virtual void OnHeartbeat(int heartbeat_status) override
	{
		// TODO(bengreenier): wire up to unity
	}

	virtual void OnServerConnectionFailure() override
	{
		if (s_callbackMap.onServerConnectionFailure)
		{
			(*s_callbackMap.onServerConnectionFailure)();
		}
	}

	virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override
	{
		if (s_callbackMap.onSignalingChange)
		{
			(*s_callbackMap.onSignalingChange)(new_state);
		}
	}

	virtual void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override
	{
		if (s_callbackMap.onAddStream)
		{
			(*s_callbackMap.onAddStream)(stream->label().c_str());
		}
	}

	virtual void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override
	{
		if (s_callbackMap.onRemoveStream)
		{
			(*s_callbackMap.onRemoveStream)(stream->label().c_str());
		}
	}

	virtual void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override
	{
		if (s_callbackMap.onDataChannel)
		{
			(*s_callbackMap.onDataChannel)(channel->label().c_str());
		}
	}

	virtual void OnRenegotiationNeeded() override
	{
		if (s_callbackMap.onRenegotiationNeeded)
		{
			(*s_callbackMap.onRenegotiationNeeded)();
		}
	}

	virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override
	{
		if (s_callbackMap.onIceConnectionChange)
		{
			(*s_callbackMap.onIceConnectionChange)(new_state);
		}
	}

	virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override
	{
		if (s_callbackMap.onIceGatheringChange)
		{
			(*s_callbackMap.onIceGatheringChange)(new_state);
		}
	}

	virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override
	{
		if (s_callbackMap.onIceCandidate)
		{
			std::string sdp;

			if (candidate->ToString(&sdp))
			{
				(*s_callbackMap.onIceCandidate)(sdp.c_str());
			}
		}
	}

	virtual void OnIceConnectionReceivingChange(bool receiving) override
	{
		if (s_callbackMap.onIceConnectionReceivingChange)
		{
			(*s_callbackMap.onIceConnectionReceivingChange)(receiving);
		}
	}

} s_clientObserver;

void InitWebRTC()
{
	ULOG(INFO, __FUNCTION__);

	// Setup the config parsers.
	ConfigParser::ConfigureConfigFactories();

	auto fullServerConfig = GlobalObject<FullServerConfig>::Get();
	auto nvEncConfig = GlobalObject<NvEncConfig>::Get();

	rtc::EnsureWinsockInit();
	rtc::Win32SocketServer w32_ss;
	rtc::Win32Thread w32_thread(&w32_ss);
	rtc::ThreadManager::Instance()->SetCurrentThread(&w32_thread);
	s_rtcMainThread = &w32_thread;

	// Initializes SSL.
	rtc::InitializeSSL();

	s_server = fullServerConfig->webrtc_config->server_uri;
	s_port = fullServerConfig->webrtc_config->port;

	// Initializes the conductor.
	s_cond.reset(new ImageMultiPeerConductor(fullServerConfig));

	// Registers observer to update Unity's window UI.
	s_cond->PeerConnection().RegisterObserver(&s_clientObserver);

	// Handles data channel messages.
	std::function<void(int, const string&)> dataChannelMessageHandler([&](
		int peerId,
		const std::string& message)
	{
		ULOG(INFO, message.c_str());

		if (s_pCallbackFunction)
		{
			s_pCallbackFunction(peerId, message.c_str());
		}
	});

	s_cond->SetDataChannelMessageHandler(dataChannelMessageHandler);

	s_cond->StartLogin(s_server, s_port);
}

void NativeInitWebRTC()
{
	s_messageThread = new std::thread(InitWebRTC);
}

void ConnectToPeer(const int peerId)
{
	ULOG(INFO, __FUNCTION__);

	// Marshal to main thread.
	s_rtcMainThread->Invoke<void>(RTC_FROM_HERE, [&] {
		s_cond->ConnectToPeer(peerId);
	});
}

void SendToPeer(const int peerId, BYTE* pBuffer, ULONG size)
{
	for (auto a : s_cond->Peers())
	{
		// TODO: this isn't finished, got to figure out how to get to the
		// channel.
		rtc::CopyOnWriteBuffer buffer(pBuffer, size);
		webrtc::DataBuffer dataBuffer(buffer, size);		
	}
}

void RegisterPeerReceiver(IntStringParamsFuncType callback)
{
	if (!s_pCallbackFunction)
	{
		s_pCallbackFunction = callback;
	}
}