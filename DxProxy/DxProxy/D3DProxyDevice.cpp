/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <D3DProxyDevice.cpp> and
Class <D3DProxyDevice> :
Copyright (C) 2012 Andres Hernandez
Modifications Copyright (C) 2013 Chris Drain

Vireio Perception Version History:
v1.0.0 2012 by Andres Hernandez
v1.0.X 2013 by John Hicks, Neil Schneider
v1.1.x 2013 by Primary Coding Author: Chris Drain
Team Support: John Hicks, Phil Larkson, Neil Schneider
v2.0.x 2013 by Denis Reischl, Neil Schneider, Joshua Brown
v2.0.4 onwards 2014 by Grant Bagwell, Simon Brown and Neil Schneider

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
********************************************************************/

#include "D3DProxyDevice.h"
#include "D3D9ProxySurface.h"
#include "StereoViewFactory.h"
#include "MotionTrackerFactory.h"
#include "HMDisplayInfoFactory.h"
#include <typeinfo>
#include <assert.h>
#include <comdef.h>
#include <tchar.h>
#include "Resource.h"
#include <D3DX9Shader.h>

#ifdef _DEBUG
#include "DxErr.h"
#endif

#include "Version.h"

#pragma comment(lib, "d3dx9.lib")

#define SMALL_FLOAT 0.001f
#define	SLIGHTLY_LESS_THAN_ONE 0.999f

#define PI 3.141592654
#define RADIANS_TO_DEGREES(rad) ((float) rad * (float) (180.0 / PI))

#define OUTPUT_HRESULT(hr) { _com_error err(hr); LPCTSTR errMsg = err.ErrorMessage(); OutputDebugString(errMsg); }

#define MAX_PIXEL_SHADER_CONST_2_0 32
#define MAX_PIXEL_SHADER_CONST_2_X 32
#define MAX_PIXEL_SHADER_CONST_3_0 224

using namespace VRBoost;

/**
* Returns the mouse wheel scroll lines.
***/
UINT GetMouseScrollLines()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetMouseScrollLines");
	#endif
	int nScrollLines = 3;
	HKEY hKey;

	if (RegOpenKeyEx(HKEY_CURRENT_USER,  _T("Control Panel\\Desktop"),
		0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
	{
		TCHAR szData[128];
		DWORD dwKeyDataType;
		DWORD dwDataBufSize = sizeof(szData);

		if (RegQueryValueEx(hKey, _T("WheelScrollLines"), NULL, &dwKeyDataType,
			(LPBYTE) &szData, &dwDataBufSize) == ERROR_SUCCESS)
		{
			nScrollLines = _tcstoul(szData, NULL, 10);
		}

		RegCloseKey(hKey);
	}

	return nScrollLines;
}

std::string VRboostAxisString(UINT axis)
{
	switch (axis)
	{
	case VRboostAxis::TrackerYaw:
		return "Yaw";
		break;
	case VRboostAxis::TrackerPitch:
		return "Pitch";
		break;
	case VRboostAxis::TrackerRoll:
		return "Roll";
		break;
	case VRboostAxis::Zero:
		return "Zero";
		break;
	case VRboostAxis::One:
		return "One";
		break;
	case VRboostAxis::WorldFOV:
		return "WorldFOV";
		break;
	case VRboostAxis::PlayerFOV:
		return "PlayerFOV";
		break;
	case VRboostAxis::FarPlaneFOV:
		return "FarPlaneFOV";
		break;
	case VRboostAxis::CameraTranslateX:
		return "CameraTranslateX";
		break;
	case VRboostAxis::CameraTranslateY:
		return "CameraTranslateY";
		break;
	case VRboostAxis::CameraTranslateZ:
		return "CameraTranslateZ";
		break;
	case VRboostAxis::CameraDistance:
		return "CameraDistance";
		break;
	case VRboostAxis::CameraZoom:
		return "CameraZoom";
		break;
	case VRboostAxis::CameraHorizonAdjustment:
		return "CameraHorizonAdjustment";
		break;
	case VRboostAxis::ConstantValue1:
		return "ConstantValue1";
		break;
	case VRboostAxis::ConstantValue2:
		return "ConstantValue2";
		break;
	case VRboostAxis::ConstantValue3:
		return "ConstantValue3";
		break;
	}

	return "UnknownAxis";
}
/**
* Constructor : creates game handler and sets various states.
***/
D3DProxyDevice::D3DProxyDevice(IDirect3DDevice9* pDevice, BaseDirect3D9* pCreatedBy):BaseDirect3DDevice9(pDevice, pCreatedBy),
	m_activeRenderTargets (1, NULL),
	m_activeTextureStages(),
	m_activeVertexBuffers(),
	m_activeSwapChains(),
	m_gameXScaleUnits(),
	m_telescopicSightMode(false),
	controls(),
	dinput(),
	activePopup(VPT_NONE),
	show_fps(FPS_NONE),
	calibrate_tracker(false),
	hmdInfo(NULL),
	m_saveConfigTimer(MAXDWORD),
	m_comfortModeYaw(0.0f),
	//Yaw increment stored in degrees for easier comparison
	m_comfortModeYawIncrement(90.0f),
	m_comfortModeLeftKey(VK_LEFT),
	m_comfortModeRightKey(VK_RIGHT),
	m_disableAllHotkeys(false)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called D3DProxyDevice");
	#endif
	OutputDebugString("D3D ProxyDev Created\n");
	
	InitVRBoost();

	// rift info
	ProxyHelper helper = ProxyHelper();
	helper.LoadUserConfig(userConfig);
	hmdInfo = HMDisplayInfoFactory::CreateHMDisplayInfo(static_cast<StereoView::StereoTypes>(userConfig.mode)); 
	OutputDebugString(("Created HMD Info for: " + hmdInfo->GetHMDName()).c_str());

	m_spShaderViewAdjustment = std::make_shared<ViewAdjustment>(hmdInfo, 1.0f, 0);
	m_pGameHandler = new GameHandler();

	// Check the maximum number of supported render targets
	D3DCAPS9 capabilities;
	BaseDirect3DDevice9::GetDeviceCaps(&capabilities);
	DWORD maxRenderTargets = capabilities.NumSimultaneousRTs;
	m_activeRenderTargets.resize(maxRenderTargets, NULL);
	fMinFPS = 73;
	
	bSkipFrame = false;
	m_b2dDepthMode = false;

	D3DXMatrixIdentity(&m_leftView);
	D3DXMatrixIdentity(&m_rightView);
	D3DXMatrixIdentity(&m_leftProjection);
	D3DXMatrixIdentity(&m_rightProjection);	

	m_currentRenderingSide = vireio::Left;
	m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix(); 
	m_pCurrentView = &m_leftView;
	m_pCurrentProjection = &m_leftProjection;

	// get pixel shader max constants
	auto major_ps=D3DSHADER_VERSION_MAJOR(capabilities.PixelShaderVersion);
	auto minor_ps=D3DSHADER_VERSION_MINOR(capabilities.PixelShaderVersion);
	DWORD MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_2_0;
	if ((major_ps>=2) && (minor_ps>0)) MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_2_X;
	if ((major_ps>=3) && (minor_ps>=0)) MaxPixelShaderConst = MAX_PIXEL_SHADER_CONST_3_0;

	m_spManagedShaderRegisters = std::make_shared<ShaderRegisters>(MaxPixelShaderConst, capabilities.MaxVertexShaderConst, pDevice);	
	m_pActiveStereoDepthStencil = NULL;
	m_pActiveIndicies = NULL;
	m_pActivePixelShader = NULL;
	m_pActiveVertexShader = NULL;
	m_pActiveVertexDeclaration = NULL;
	m_bActiveViewportIsDefault = true;
	m_bViewportIsSquished = false;
	m_bDoNotDrawVShader = false;
	m_bDoNotDrawPShader = false;
	m_bViewTransformSet = false;
	m_bProjectionTransformSet = false;
	m_bInBeginEndStateBlock = false;
	m_pCapturingStateTo = NULL;
	m_isFirstBeginSceneOfFrame = true;
	InitVPMENU();
	//Create Direct Input Mouse Device
	bool directInputActivated = dinput.Init(GetModuleHandle(NULL), ::GetActiveWindow());
	if(directInputActivated)
	{		
		dinput.Activate();		
	}	

	m_telescopeTargetFOV = FLT_MAX;
	m_telescopeCurrentFOV = FLT_MAX;

	//Restore duck and cover settings
	m_DuckAndCover.LoadFromRegistry();

}

/**
* Destructor : calls ReleaseEverything() and releases swap chains.
* @see ReleaseEverything()
***/
D3DProxyDevice::~D3DProxyDevice()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ~D3DProxyDevice");
	#endif
	ReleaseEverything();

	m_spShaderViewAdjustment.reset();

	delete m_pGameHandler;
	m_spManagedShaderRegisters.reset();

	FreeLibrary(hmVRboost);

	// always do this last
	auto it = m_activeSwapChains.begin();
	while (it != m_activeSwapChains.end()) {

		if ((*it) != NULL) {
			(*it)->Release();
			delete (*it);
		}

		it = m_activeSwapChains.erase(it);
	}
}

#define IF_GUID(riid,a,b,c,d,e,f,g,h,i,j,k) if ((riid.Data1==a)&&(riid.Data2==b)&&(riid.Data3==c)&&(riid.Data4[0]==d)&&(riid.Data4[1]==e)&&(riid.Data4[2]==f)&&(riid.Data4[3]==g)&&(riid.Data4[4]==h)&&(riid.Data4[5]==i)&&(riid.Data4[6]==j)&&(riid.Data4[7]==k))
/**
* Catch QueryInterface calls and increment the reference counter if necesarry. 
***/
HRESULT WINAPI D3DProxyDevice::QueryInterface(REFIID riid, LPVOID* ppv)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called QueryInterface");
	#endif
	//DEFINE_GUID(IID_IDirect3DDevice9Ex, 0xb18b10ce, 0x2649, 0x405a, 0x87, 0xf, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a);
	IF_GUID(riid,0xb18b10ce,0x2649,0x405a,0x87,0xf,0x95,0xf7,0x77,0xd4,0x31,0x3a)
	{
		if (ppv==NULL)
			return E_POINTER;

		this->AddRef();
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	return BaseDirect3DDevice9::QueryInterface(riid,ppv);
}

/**
* Currently base TestCooperativeLevel() functionality.
***/
HRESULT WINAPI D3DProxyDevice::TestCooperativeLevel()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called TestCooperativeLevel");
	#endif
	return BaseDirect3DDevice9::TestCooperativeLevel();
	// The calling application will start releasing resources after TestCooperativeLevel returns D3DERR_DEVICENOTRESET.
}

/**
* Calls SetCursorProperties() using the actual left surface from the proxy of pCursorBitmap.
***/
HRESULT WINAPI D3DProxyDevice::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetCursorProperties");
	#endif
	if (!pCursorBitmap)
		return BaseDirect3DDevice9::SetCursorProperties(XHotSpot, YHotSpot, NULL);

	return BaseDirect3DDevice9::SetCursorProperties(XHotSpot, YHotSpot, static_cast<D3D9ProxySurface*>(pCursorBitmap)->getActualLeft());
}

/**
* Creates a proxy (or wrapped) swap chain.
* @param pSwapChain [in, out] Proxy (wrapped) swap chain to be returned.
***/
HRESULT WINAPI D3DProxyDevice::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DSwapChain9** pSwapChain)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateAdditionalSwapChain");
	#endif
	IDirect3DSwapChain9* pActualSwapChain;
	HRESULT result = BaseDirect3DDevice9::CreateAdditionalSwapChain(pPresentationParameters, &pActualSwapChain);

	if (SUCCEEDED(result)) {
		D3D9ProxySwapChain* wrappedSwapChain = new D3D9ProxySwapChain(pActualSwapChain, this, true);
		*pSwapChain = wrappedSwapChain;
		m_activeSwapChains.push_back(wrappedSwapChain);
	}

	return result;
}

/**
* Provides the swap chain from the intern vector of active proxy (wrapped) swap chains.
* @param pSwapChain [in, out] The proxy (wrapped) swap chain to be returned.
* @see D3D9ProxySwapChain
***/
HRESULT WINAPI D3DProxyDevice::GetSwapChain(UINT iSwapChain,IDirect3DSwapChain9** pSwapChain)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetSwapChain");
	#endif
	try {
		*pSwapChain = m_activeSwapChains.at(iSwapChain); 
		//Device->GetSwapChain increases ref count on the chain (docs don't say this)
		(*pSwapChain)->AddRef();
	}
	catch (std::out_of_range) {
		OutputDebugString("GetSwapChain: out of range fetching swap chain");
		return D3DERR_INVALIDCALL;
	}

	return D3D_OK;
}

/**
* Calls release functions here and in stereo view class, releases swap chains and restores everything.
* Subclasses which override this method must call through to super method at the end of the subclasses
* implementation.
* @see ReleaseEverything()
* @see StereoView::ReleaseEverything()
* @see OnCreateOrRestore()
***/
HRESULT WINAPI D3DProxyDevice::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called Reset");
	#endif
	if(stereoView)
		stereoView->ReleaseEverything();

	ReleaseEverything();

	m_bInBeginEndStateBlock = false;

	auto it = m_activeSwapChains.begin();
	while (it != m_activeSwapChains.end()) {

		if ((*it) != NULL)
			(*it)->Release();

		delete (*it);

		it = m_activeSwapChains.erase(it);
	}

	HRESULT hr = BaseDirect3DDevice9::Reset(pPresentationParameters);

	// if the device has been successfully reset we need to recreate any resources we created
	if (hr == D3D_OK)  {
		OnCreateOrRestore();
		stereoView->PostReset();
	}
	else {
#ifdef _DEBUG
		char buf[256];
		sprintf_s(buf, "Error: %s error description: %s\n",
			DXGetErrorString(hr), DXGetErrorDescription(hr));

		OutputDebugString(buf);				
#endif
		OutputDebugString("Device reset failed");
	}

	return hr;
}

/**
* Here the chosen stereoviews draw function is called to render to wrapped back buffer.
* All other final screen output is also done here.
***/
HRESULT WINAPI D3DProxyDevice::Present(CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called Present");
	#endif
	
	try {
		IDirect3DSurface9* pWrappedBackBuffer = NULL;
		m_activeSwapChains.at(0)->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pWrappedBackBuffer);
		if (stereoView->initialized)
			stereoView->Draw(static_cast<D3D9ProxySurface*>(pWrappedBackBuffer));
				
		pWrappedBackBuffer->Release();
	}
	catch (std::out_of_range) {
		OutputDebugString("Present: No primary swap chain found. (Present probably called before device has been reset)");
	}

	// did set this now also in proxy swap chain ? solved ?
	// (this can break if device present is followed by present on another swap chain... or not work well anyway)
	m_isFirstBeginSceneOfFrame = true; 

	VPMENU_UpdateBorder();

	//Now calculate frames per second
	fps = CalcFPS();

	//Write FPS to debug log every half second
	static DWORD lastFPSTick = GetTickCount();
	if (GetTickCount() - lastFPSTick > 500)
	{
		lastFPSTick = GetTickCount();
		char buffer[256];
		sprintf_s(buffer, "%.1f", fps);
		//OutputDebugString((std::string("FPS: ") + buffer).c_str());

		//Now write FPS to the registry for the Perception App (hopefully this is a very quick operation)
		HKEY hKey;
		LONG openRes = RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Vireio\\Perception", 0, KEY_ALL_ACCESS , &hKey);
		if (openRes==ERROR_SUCCESS)
		{
			RegSetValueEx(hKey, "FPS", 0, REG_SZ, (LPBYTE)buffer, strlen(buffer)+1);
			RegCloseKey(hKey);
		}
	}
	HRESULT hr =  BaseDirect3DDevice9::Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);	

	if (tracker)
		tracker->EndFrame();

	return hr;
}

/**
* Calls the backbuffer using the stored active proxy (wrapped) swap chain.
***/
HRESULT WINAPI D3DProxyDevice::GetBackBuffer(UINT iSwapChain,UINT iBackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface9** ppBackBuffer)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetBackBuffer");
	#endif
	HRESULT result;
	try {
		result = m_activeSwapChains.at(iSwapChain)->GetBackBuffer(iBackBuffer, Type, ppBackBuffer);
		// ref count increase happens in the swapchain GetBackBuffer so we don't add another ref here as we are just passing the value through
	}
	catch (std::out_of_range) {
		OutputDebugString("GetBackBuffer: out of range getting swap chain");
		result = D3DERR_INVALIDCALL;
	}

	return result;
}

/**
* Creates a proxy (or wrapped) texture (D3DProxyTexture).
* Texture to be created only gets both stereo textures if game handler agrees.
* @see D3DProxyTexture
* @see GameHandler::ShouldDuplicateTexture()
***/
HRESULT WINAPI D3DProxyDevice::CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateTexture");
	#endif
	HRESULT creationResult;
	IDirect3DTexture9* pLeftTexture = NULL;
	IDirect3DTexture9* pRightTexture = NULL;	

	// try and create left
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateTexture(Width, Height, Levels, Usage, Format, Pool, &pLeftTexture, pSharedHandle))) {

		// Does this Texture need duplicating?
		if (!m_b2dDepthMode && m_pGameHandler->ShouldDuplicateTexture(Width, Height, Levels, Usage, Format, Pool)) {

			if (FAILED(BaseDirect3DDevice9::CreateTexture(Width, Height, Levels, Usage, Format, Pool, &pRightTexture, pSharedHandle))) {
				OutputDebugString("Failed to create right eye texture while attempting to create stereo pair, falling back to mono\n");
				pRightTexture = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create texture\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppTexture = new D3D9ProxyTexture(pLeftTexture, pRightTexture, this);

	return creationResult;
}

/**
* Creates a a proxy (or wrapped) volume texture (D3D9ProxyVolumeTexture).
* Volumes can't be used as render targets and therefore don't need to be stereo (in DX9).
* @see D3D9ProxyVolumeTexture
***/	
HRESULT WINAPI D3DProxyDevice::CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateVolumeTexture");
	#endif
	IDirect3DVolumeTexture9* pActualTexture = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, &pActualTexture, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppVolumeTexture = new D3D9ProxyVolumeTexture(pActualTexture, this);

	return creationResult;
}

/**
* Creates a proxy (or wrapped) cube texture (D3D9ProxyCubeTexture).
* Texture to be created only gets both stereo textures if game handler agrees.
* @see D3D9ProxyCubeTexture
* @see GameHandler::ShouldDuplicateCubeTexture() 
***/
HRESULT WINAPI D3DProxyDevice::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateCubeTexture");
	#endif
	HRESULT creationResult;
	IDirect3DCubeTexture9* pLeftCubeTexture = NULL;
	IDirect3DCubeTexture9* pRightCubeTexture = NULL;	

	if(bSkipFrame)
		return D3D_OK;

	// try and create left
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pLeftCubeTexture, pSharedHandle))) {

		// Does this Texture need duplicating?
		if (!m_b2dDepthMode && m_pGameHandler->ShouldDuplicateCubeTexture(EdgeLength, Levels, Usage, Format, Pool)) {

			if (FAILED(BaseDirect3DDevice9::CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pRightCubeTexture, pSharedHandle))) {
				OutputDebugString("Failed to create right eye texture while attempting to create stereo pair, falling back to mono\n");
				pRightCubeTexture = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create texture\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppCubeTexture = new D3D9ProxyCubeTexture(pLeftCubeTexture, pRightCubeTexture, this);

	return creationResult;
}

/**
* Creates base vertex buffer pointer (BaseDirect3DVertexBuffer9).
* @see BaseDirect3DVertexBuffer9
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateVertexBuffer");
	#endif
	IDirect3DVertexBuffer9* pActualBuffer = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexBuffer(Length, Usage, FVF, Pool, &pActualBuffer, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppVertexBuffer = new BaseDirect3DVertexBuffer9(pActualBuffer, this);

	return creationResult;
}

/**
* * Creates base index buffer pointer (BaseDirect3DIndexBuffer9).
* @see BaseDirect3DIndexBuffer9
***/
HRESULT WINAPI D3DProxyDevice::CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateIndexBuffer");
	#endif
	IDirect3DIndexBuffer9* pActualBuffer = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateIndexBuffer(Length, Usage, Format, Pool, &pActualBuffer, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppIndexBuffer = new BaseDirect3DIndexBuffer9(pActualBuffer, this);

	return creationResult;
}

/**
* Calls the public overloaded function.
* The IDirect3DSurface9** ppSurface returned should always be a D3D9ProxySurface. Any class overloading
* this method should ensure that this remains true.
***/
HRESULT WINAPI D3DProxyDevice::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
												  DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateRenderTarget");
	#endif
	// call public overloaded function
	return CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, false);
}

/**
* Creates a proxy (or wrapped) depth stencil surface (D3D9ProxySurface).
* Surface to be created only gets both stereo surfaces if game handler agrees.
* @see D3D9ProxySurface
* @see GameHandler::ShouldDuplicateDepthStencilSurface() 
***/
HRESULT WINAPI D3DProxyDevice::CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateDepthStencilSurface");
	#endif
	
	if(bSkipFrame)
		return D3D_OK;
		
	IDirect3DSurface9* pDepthStencilSurfaceLeft = NULL;
	IDirect3DSurface9* pDepthStencilSurfaceRight = NULL;
	HRESULT creationResult;
	
	// create left/mono
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, &pDepthStencilSurfaceLeft, pSharedHandle))) {

		// TODO Should we always duplicated Depth stencils? I think yes, but there may be exceptions
		if (!m_b2dDepthMode && m_pGameHandler->ShouldDuplicateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard)) 
		{
			if (m_b2dDepthMode || FAILED(BaseDirect3DDevice9::CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, &pDepthStencilSurfaceRight, pSharedHandle))) {
				OutputDebugString("Failed to create right eye Depth Stencil Surface while attempting to create stereo pair, falling back to mono\n");
				pDepthStencilSurfaceRight = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create Depth Stencil Surface\n"); 
	}

	if (SUCCEEDED(creationResult))
		*ppSurface = new D3D9ProxySurface(pDepthStencilSurfaceLeft, pDepthStencilSurfaceRight, this, NULL);

	return creationResult;
}

/**
* Copies rectangular subsets of pixels from one proxy (wrapped) surface to another.
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::UpdateSurface(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestinationSurface,CONST POINT* pDestPoint)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called UpdateSurface");
	#endif
	if (!pSourceSurface || !pDestinationSurface)
		return D3DERR_INVALIDCALL;

	IDirect3DSurface9* pSourceSurfaceLeft = static_cast<D3D9ProxySurface*>(pSourceSurface)->getActualLeft();
	IDirect3DSurface9* pSourceSurfaceRight = static_cast<D3D9ProxySurface*>(pSourceSurface)->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = static_cast<D3D9ProxySurface*>(pDestinationSurface)->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = static_cast<D3D9ProxySurface*>(pDestinationSurface)->getActualRight();

	HRESULT result = BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceLeft, pSourceRect, pDestSurfaceLeft, pDestPoint);

	if (SUCCEEDED(result)) {
		if (!pSourceSurfaceRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: UpdateSurface - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceLeft, pSourceRect, pDestSurfaceRight, pDestPoint))) {
				OutputDebugString("ERROR: UpdateSurface - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceSurfaceRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: UpdateSurface - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceSurfaceRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::UpdateSurface(pSourceSurfaceRight, pSourceRect, pDestSurfaceRight, pDestPoint))) {
				OutputDebugString("ERROR: UpdateSurface - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Calls a helper function to unwrap the textures and calls the super method for both sides.
* The super method updates the dirty portions of a texture.
* @see vireio::UnWrapTexture()
***/
HRESULT WINAPI D3DProxyDevice::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture,IDirect3DBaseTexture9* pDestinationTexture)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called UpdateTexture");
	#endif
	if (!pSourceTexture || !pDestinationTexture)
		return D3DERR_INVALIDCALL;


	IDirect3DBaseTexture9* pSourceTextureLeft = NULL;
	IDirect3DBaseTexture9* pSourceTextureRight = NULL;
	IDirect3DBaseTexture9* pDestTextureLeft = NULL;
	IDirect3DBaseTexture9* pDestTextureRight = NULL;

	vireio::UnWrapTexture(pSourceTexture, &pSourceTextureLeft, &pSourceTextureRight);
	vireio::UnWrapTexture(pDestinationTexture, &pDestTextureLeft, &pDestTextureRight);

	HRESULT result = BaseDirect3DDevice9::UpdateTexture(pSourceTextureLeft, pDestTextureLeft);

	if (SUCCEEDED(result)) {
		if (!pSourceTextureRight && pDestTextureRight) {
			//OutputDebugString("INFO: UpdateTexture - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::UpdateTexture(pSourceTextureLeft, pDestTextureRight))) {
				OutputDebugString("ERROR: UpdateTexture - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceTextureRight && !pDestTextureRight) {
			//OutputDebugString("INFO: UpdateTexture - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceTextureRight && pDestTextureRight)	{
			if (FAILED(BaseDirect3DDevice9::UpdateTexture(pSourceTextureRight, pDestTextureRight))) {
				OutputDebugString("ERROR: UpdateTexture - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Copies the render-target data from proxy (wrapped) source surface to proxy (wrapped) destination surface.
***/
HRESULT WINAPI D3DProxyDevice::GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetRenderTarget");
	#endif
	if ((pDestSurface == NULL) || (pRenderTarget == NULL))
		return D3DERR_INVALIDCALL;

	D3D9ProxySurface* pWrappedRenderTarget = static_cast<D3D9ProxySurface*>(pRenderTarget);
	D3D9ProxySurface* pWrappedDest = static_cast<D3D9ProxySurface*>(pDestSurface);

	IDirect3DSurface9* pRenderTargetLeft = pWrappedRenderTarget->getActualLeft();
	IDirect3DSurface9* pRenderTargetRight = pWrappedRenderTarget->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = pWrappedDest->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = pWrappedDest->getActualRight();

	HRESULT result = BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetLeft, pDestSurfaceLeft);

	if (SUCCEEDED(result)) {
		if (!pRenderTargetRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: GetRenderTargetData - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetLeft, pDestSurfaceRight))) {
				OutputDebugString("ERROR: GetRenderTargetData - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pRenderTargetRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: GetRenderTargetData - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pRenderTargetRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::GetRenderTargetData(pRenderTargetRight, pDestSurfaceRight))) {
				OutputDebugString("ERROR: GetRenderTargetData - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Gets the front buffer data from the internal stored active proxy (or wrapped) swap chain.
* @see D3D9ProxySwapChain
***/
HRESULT WINAPI D3DProxyDevice::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetFrontBufferData");
	#endif
	HRESULT result;
	try {
		result = m_activeSwapChains.at(iSwapChain)->GetFrontBufferData(pDestSurface);
	}
	catch (std::out_of_range) {
		OutputDebugString("GetFrontBufferData: out of range fetching swap chain");
		result = D3DERR_INVALIDCALL;
	}

	return result;
}

/**
* Copy the contents of the source proxy (wrapped) surface rectangles to the destination proxy (wrapped) surface rectangles.
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called StretchRect");
	#endif
	if (!pSourceSurface || !pDestSurface)
		return D3DERR_INVALIDCALL;

	D3D9ProxySurface* pWrappedSource = static_cast<D3D9ProxySurface*>(pSourceSurface);
	D3D9ProxySurface* pWrappedDest = static_cast<D3D9ProxySurface*>(pDestSurface);

	IDirect3DSurface9* pSourceSurfaceLeft = pWrappedSource->getActualLeft();
	IDirect3DSurface9* pSourceSurfaceRight = pWrappedSource->getActualRight();
	IDirect3DSurface9* pDestSurfaceLeft = pWrappedDest->getActualLeft();
	IDirect3DSurface9* pDestSurfaceRight = pWrappedDest->getActualRight();

	HRESULT result = BaseDirect3DDevice9::StretchRect(pSourceSurfaceLeft, pSourceRect, pDestSurfaceLeft, pDestRect, Filter);

	if (SUCCEEDED(result)) {
		if (!pSourceSurfaceRight && pDestSurfaceRight) {
			//OutputDebugString("INFO: StretchRect - Source is not stereo, destination is stereo. Copying source to both sides of destination.\n");

			if (FAILED(BaseDirect3DDevice9::StretchRect(pSourceSurfaceLeft, pSourceRect, pDestSurfaceRight, pDestRect, Filter))) {
				OutputDebugString("ERROR: StretchRect - Failed to copy source left to destination right.\n");
			}
		} 
		else if (pSourceSurfaceRight && !pDestSurfaceRight) {
			//OutputDebugString("INFO: StretchRect - Source is stereo, destination is not stereo. Copied Left side only.\n");
		}
		else if (pSourceSurfaceRight && pDestSurfaceRight)	{
			if (FAILED(BaseDirect3DDevice9::StretchRect(pSourceSurfaceRight, pSourceRect, pDestSurfaceRight, pDestRect, Filter))) {
				OutputDebugString("ERROR: StretchRect - Failed to copy source right to destination right.\n");
			}
		}
	}

	return result;
}

/**
* Fills the rectangle for both stereo sides if switchDrawingSide() agrees and sets the render target accordingly.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::ColorFill(IDirect3DSurface9* pSurface,CONST RECT* pRect,D3DCOLOR color)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ColorFill");
	#endif
	HRESULT result;
	
	if(bSkipFrame)
		return D3D_OK;

	D3D9ProxySurface* pDerivedSurface = static_cast<D3D9ProxySurface*> (pSurface);
	if (SUCCEEDED(result = BaseDirect3DDevice9::ColorFill(pDerivedSurface->getActualLeft(), pRect, color)))
	{
		if (!m_b2dDepthMode && pDerivedSurface->IsStereo())
			BaseDirect3DDevice9::ColorFill(pDerivedSurface->getActualRight(), pRect, color);
	}

	return result;
}

/**
* Creates a wrapped mono surface with only one (left) side.
* OffscreenPlainSurfaces doesn't need to be Stereo. 
* They can't be used as render targets and they can't have rendertargets copied to them with stretch
* rect, so don't need to be stereo capable.
* See table at bottom of 
* <http://msdn.microsoft.com/en-us/library/windows/desktop/bb174471%28v=vs.85%29.aspx> 
* for stretch rect restrictions.
**/
HRESULT WINAPI D3DProxyDevice::CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle)
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateOffscreenPlainSurface");
	#endif
	IDirect3DSurface9* pActualSurface = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateOffscreenPlainSurface(Width, Height, Format, Pool, &pActualSurface, pSharedHandle);

	if (SUCCEEDED(creationResult))
		*ppSurface = new D3D9ProxySurface(pActualSurface, NULL, this, NULL);

	return creationResult;
}

/**
* Updates render target accordingly to current render side.
* Updates proxy collection of stereo render targets to reflect new actual render target.
***/
HRESULT WINAPI D3DProxyDevice::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetRenderTarget");
	#endif
	D3D9ProxySurface* newRenderTarget = static_cast<D3D9ProxySurface*>(pRenderTarget);

#ifdef _DEBUG
	if (newRenderTarget && !newRenderTarget->getActualLeft() && !newRenderTarget->getActualRight()) {
		OutputDebugString("RenderTarget is not a valid (D3D9ProxySurface) stereo capable surface\n"); 
	}
#endif

	//// Update actual render target ////
	HRESULT result;

	// Removing a render target
	if (newRenderTarget == NULL) {
		if (RenderTargetIndex == 0) {
			// main render target should never be set to NULL
			result = D3DERR_INVALIDCALL; 
		}		
		else {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, NULL);
		}
	}
	// Setting a render target
	else {
		if (m_currentRenderingSide == vireio::Left) {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, newRenderTarget->getActualLeft());
		}
		else {
			result = BaseDirect3DDevice9::SetRenderTarget(RenderTargetIndex, newRenderTarget->getActualRight());
		}
	}

	//// update proxy collection of stereo render targets to reflect new actual render target ////
	if (result == D3D_OK) {		
		// changing rendertarget resets viewport to fullsurface
		m_bActiveViewportIsDefault = true;

		// release old render target
		if (m_activeRenderTargets[RenderTargetIndex] != NULL)
			m_activeRenderTargets[RenderTargetIndex]->Release();

		// replace with new render target (may be NULL)
		m_activeRenderTargets[RenderTargetIndex] = newRenderTarget;
		if (m_activeRenderTargets[RenderTargetIndex] != NULL)
			m_activeRenderTargets[RenderTargetIndex]->AddRef();
	}

	return result;
}

/**
* Provides render target from the internally stored active proxy (wrapped) render targets.
***/
HRESULT WINAPI D3DProxyDevice::GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetRenderTarget");
	#endif
	if ((RenderTargetIndex >= m_activeRenderTargets.capacity()) || (RenderTargetIndex < 0)) {
		return D3DERR_INVALIDCALL;
	}

	IDirect3DSurface9* targetToReturn = m_activeRenderTargets[RenderTargetIndex];
	if (!targetToReturn)
		return D3DERR_NOTFOUND;
	else {
		*ppRenderTarget = targetToReturn;
		targetToReturn->AddRef();
		return D3D_OK;
	}
}

/**
* Updates depth stencil accordingly to current render side.
* Updates stored proxy (or wrapped) depth stencil.
***/
HRESULT WINAPI D3DProxyDevice::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetDepthStencilSurface");
	#endif
	D3D9ProxySurface* pNewDepthStencil = static_cast<D3D9ProxySurface*>(pNewZStencil);

	IDirect3DSurface9* pActualStencilForCurrentSide = NULL;
	if (pNewDepthStencil) {
		if (m_currentRenderingSide == vireio::Left)
			pActualStencilForCurrentSide = pNewDepthStencil->getActualLeft();
		else
			pActualStencilForCurrentSide = pNewDepthStencil->getActualRight();
	}

	// Update actual depth stencil
	HRESULT result = BaseDirect3DDevice9::SetDepthStencilSurface(pActualStencilForCurrentSide);

	// Update stored proxy depth stencil
	if (SUCCEEDED(result)) {
		if (m_pActiveStereoDepthStencil) {
			m_pActiveStereoDepthStencil->Release();
		}

		m_pActiveStereoDepthStencil = pNewDepthStencil;
		if (m_pActiveStereoDepthStencil) {
			m_pActiveStereoDepthStencil->AddRef();
		}
	}

	return result;
}

/**
* Provides the active proxy (wrapped) depth stencil.
***/
HRESULT WINAPI D3DProxyDevice::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetDepthStencilSurface");
	#endif
	if (!m_pActiveStereoDepthStencil)
		return D3DERR_NOTFOUND;

	*ppZStencilSurface = m_pActiveStereoDepthStencil;
	(*ppZStencilSurface)->AddRef();

	return D3D_OK;
}

/**
* Updates tracker if device says it should.  Handles controls if this is the first scene of the frame.
* Because input for this frame would already have been handled here so injection of any mouse 
* manipulation ?
***/
HRESULT WINAPI D3DProxyDevice::BeginScene()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called BeginScene");
	#endif

	if (m_isFirstBeginSceneOfFrame)
	{
		static int spashtick = GetTickCount();
		if ((GetTickCount() - spashtick)  < 4000)
		{
			std::string date(__DATE__);
			std::string buildDate = date.substr(4, 2) + "-" + date.substr(0, 3) + "-" + date.substr(7, 4);

			//Show a splash screen on startup
			VireioPopup splashPopup(VPT_SPLASH_1, VPS_INFO, 4000);
			strcpy_s(splashPopup.line[0], "Vireio Perception: Stereoscopic 3D Driver");
			strcpy_s(splashPopup.line[1], (std::string("Version: ") + APP_VERSION + "   Build Date: " + buildDate).c_str());
			strcpy_s(splashPopup.line[2], "This program is distributed in the hope that it will be useful,"); 
			strcpy_s(splashPopup.line[3], "but WITHOUT ANY WARRANTY; without even the implied warranty of "); 
			strcpy_s(splashPopup.line[4], "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.");
			strcpy_s(splashPopup.line[5], "See the GNU LGPL: http://www.gnu.org/licenses/ for more details. ");
			ShowPopup(splashPopup);
		}

		if ((GetTickCount() - spashtick)  > 4000 &&
			(GetTickCount() - spashtick)  < 8000)
		{
			//Show a splash screen on startup
			VireioPopup splashPopup(VPT_SPLASH_2, VPS_INFO, 4000);
			strcpy_s(splashPopup.line[0], "Vireio Perception: Stereoscopic 3D Driver");
			strcpy_s(splashPopup.line[1], "Useful Hot-keys:"); 
			strcpy_s(splashPopup.line[2], "     <CTRL> + <Q>\t\t\t:  Show Vireio In-Game Menu"); 
			strcpy_s(splashPopup.line[3], "     Mouse Wheel Click\t:  Disconnected Screen View");
			strcpy_s(splashPopup.line[4], "     <LSHIFT> + <R>\t\t\t:  Reset HMD Orientation");
			strcpy_s(splashPopup.line[5], "     <LSHIFT> + <F>\t\t\t:  FPS Counter");
			ShowPopup(splashPopup);
		}
	
		if ((GetTickCount() - spashtick)  > 8000)
		{
			if (calibrate_tracker)
			{
				VireioPopup popup(VPT_CALIBRATE_TRACKER, VPS_INFO, 15000);
				strcpy_s(popup.line[1], "Please Calibrate HMD/Tracker:");
				strcpy_s(popup.line[2], "     -  Sit comfortably with your head facing forwards");
				strcpy_s(popup.line[3], "     -  Press any of the following:");
				strcpy_s(popup.line[4], "           <CTRL> + <R> / <LSHIFT> + <R>");
				strcpy_s(popup.line[5], "           L + R Shoulder Buttons on Xbox 360 Controller");
				ShowPopup(popup);
			}
		}

		//If we need to save configuration, do it now
		if (m_saveConfigTimer != MAXDWORD &&
			//Wait 10 seconds before saving
			(GetTickCount() - m_saveConfigTimer) > 10000)
		{
			VPMENU_UpdateConfigSettings();
			//Make sure we don't come back in here
			m_saveConfigTimer = MAXDWORD;
		}

		if (tracker)
			tracker->BeginFrame();

		// save screenshot before first clear() is called
		if (screenshot>0)
		{
			if (screenshot==1)
				stereoView->SaveScreen();
			screenshot--;
		}

		/*if(fps < fMinFPS && stereoView->bReprojection && !bSkipFrame)
		{			
			bSkipFrame = true;			
		}
		else
		{
			bSkipFrame = false;			
		}*/

		// set last frame vertex shader count
		m_VertexShaderCountLastFrame = m_VertexShaderCount;

		// avoid squished viewport in case of vp menu being drawn
		/*
		if ((m_bViewportIsSquished) && (VPMENU_mode>=VPMENU_Modes::MAINMENU) && (VPMENU_mode<VPMENU_Modes::VPMENU_ENUM_RANGE))
		{
			if (m_bViewportIsSquished)
				BaseDirect3DDevice9::SetViewport(&m_LastViewportSet);
			m_bViewportIsSquished = false;
		}*/

		// handle controls - Only handled on the first begin scene
		if (m_deviceBehavior.whenToHandleHeadTracking == DeviceBehavior::WhenToDo::BEGIN_SCENE)
			HandleTracking();

		// draw menu
		if (m_deviceBehavior.whenToRenderVPMENU == DeviceBehavior::WhenToDo::BEGIN_SCENE)
		{
			if ((VPMENU_mode>=VPMENU_Modes::MAINMENU) && (VPMENU_mode<VPMENU_Modes::VPMENU_ENUM_RANGE))
				VPMENU();
			else
				VPMENU_AdditionalOutput();
		}		
		// handle controls
		HandleControls();

		// set vertex shader call count to zero
		m_VertexShaderCount = 0;
	}
	else
	{
		// draw
		if (m_deviceBehavior.whenToRenderVPMENU == DeviceBehavior::WhenToDo::BEGIN_SCENE)
		{
			if ((VPMENU_mode>=VPMENU_Modes::MAINMENU) && (VPMENU_mode<VPMENU_Modes::VPMENU_ENUM_RANGE))
				VPMENU();
			else
				VPMENU_AdditionalOutput();
		}
	}

	

	return BaseDirect3DDevice9::BeginScene();
}

/**
* VPMENU called here for source engine games.
***/
HRESULT WINAPI D3DProxyDevice::EndScene()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called EndScene");
	#endif
	// handle controls 
	if (m_deviceBehavior.whenToHandleHeadTracking == DeviceBehavior::WhenToDo::END_SCENE) 
		HandleTracking();

	// draw menu
	if (m_deviceBehavior.whenToRenderVPMENU == DeviceBehavior::WhenToDo::END_SCENE) 
	{
		if ((VPMENU_mode>=VPMENU_Modes::MAINMENU) && (VPMENU_mode<VPMENU_Modes::VPMENU_ENUM_RANGE))
			VPMENU();
		else
			VPMENU_AdditionalOutput();
	}

	return BaseDirect3DDevice9::EndScene();
}

/**
* Clears both stereo sides if switchDrawingSide() agrees.
***/
HRESULT WINAPI D3DProxyDevice::Clear(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called Clear");
	#endif
	HRESULT result;

	if (SUCCEEDED(result = BaseDirect3DDevice9::Clear(Count, pRects, Flags, Color, Z, Stencil))) {
		if (!m_b2dDepthMode && switchDrawingSide()) {
			HRESULT hr;
			if (FAILED(hr = BaseDirect3DDevice9::Clear(Count, pRects, Flags, Color, Z, Stencil))) {

#ifdef _DEBUG
				char buf[256];
				sprintf_s(buf, "Error: %s error description: %s\n",
				DXGetErrorString(hr), DXGetErrorDescription(hr));

				OutputDebugString(buf);
				OutputDebugString("Clear failed\n");

#endif

			}
		}
	}

	return result;
}

/**
* Catches transform for stored proxy state block accordingly or updates proxy device.
* @see D3D9ProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetTransform");
	#endif
	if(State == D3DTS_VIEW)
	{
		D3DXMATRIX tempLeft;
		D3DXMATRIX tempRight;
		D3DXMATRIX* pViewToSet = NULL;
		bool tempIsTransformSet = false;

		if (!pMatrix) {
			D3DXMatrixIdentity(&tempLeft);
			D3DXMatrixIdentity(&tempRight);
		}
		else {

			D3DXMATRIX sourceMatrix(*pMatrix);

			// If the view is set to the identity then we don't need to perform any adjustments
			if (D3DXMatrixIsIdentity(&sourceMatrix)) {

				D3DXMatrixIdentity(&tempLeft);
				D3DXMatrixIdentity(&tempRight);
			}
			else {
				// If the view matrix is modified we need to apply left/right adjustments (for stereo rendering)
				tempLeft = sourceMatrix * m_spShaderViewAdjustment->LeftViewTransform();
				tempRight = sourceMatrix * m_spShaderViewAdjustment->RightViewTransform();

				tempIsTransformSet = true;
			}
		}


		// If capturing state block capture without updating proxy device
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureViewTransform(tempLeft, tempRight);
			if (m_currentRenderingSide == vireio::Left) {
				pViewToSet = &tempLeft;
			}
			else {
				pViewToSet = &tempRight;
			}
		}
		else { // otherwise update proxy device

			m_bViewTransformSet = tempIsTransformSet;
			m_leftView = tempLeft;
			m_rightView = tempRight;

			if (m_currentRenderingSide == vireio::Left) {
				m_pCurrentView = &m_leftView;
			}
			else {
				m_pCurrentView = &m_rightView;
			}

			pViewToSet = m_pCurrentView;
		}

		return BaseDirect3DDevice9::SetTransform(State, pViewToSet);

	}
	else if(State == D3DTS_PROJECTION)
	{

		D3DXMATRIX tempLeft;
		D3DXMATRIX tempRight;
		D3DXMATRIX* pProjectionToSet = NULL;
		bool tempIsTransformSet = false;

		if (!pMatrix) {

			D3DXMatrixIdentity(&tempLeft);
			D3DXMatrixIdentity(&tempRight);
		}
		else {
			D3DXMATRIX sourceMatrix(*pMatrix);

			// world scale mode ? in case, add all possible actual game x scale units
			if (VPMENU_mode == VPMENU_Modes::WORLD_SCALE_CALIBRATION)
			{
				// store the actual projection matrix for game unit calculation
				D3DXMATRIX m_actualProjection = D3DXMATRIX(*pMatrix);

				// get the scale the 
				float gameXScale = m_actualProjection._11;

				// add if not present
				if (std::find(m_gameXScaleUnits.begin(), m_gameXScaleUnits.end(), gameXScale) == m_gameXScaleUnits.end()) {
					m_gameXScaleUnits.push_back(gameXScale);
				}
			}

			// If the view is set to the identity then we don't need to perform any adjustments
			if (D3DXMatrixIsIdentity(&sourceMatrix)) {

				D3DXMatrixIdentity(&tempLeft);
				D3DXMatrixIdentity(&tempRight);
			}
			else {


				tempLeft = sourceMatrix;
				tempRight = sourceMatrix;

				tempIsTransformSet = true;
			}			
		}

		// If capturing state block capture without updating proxy device
		if (m_pCapturingStateTo) {

			m_pCapturingStateTo->SelectAndCaptureProjectionTransform(tempLeft, tempRight);
			if (m_currentRenderingSide == vireio::Left) {
				pProjectionToSet = &tempLeft;
			}
			else {
				pProjectionToSet = &tempRight;
			}
		}
		else { // otherwise update proxy device

			m_bProjectionTransformSet = tempIsTransformSet;
			m_leftProjection = tempLeft;
			m_rightProjection = tempRight;

			if (m_currentRenderingSide == vireio::Left) {
				m_pCurrentProjection = &m_leftProjection;
			}
			else {
				m_pCurrentProjection = &m_rightProjection;
			}

			pProjectionToSet = m_pCurrentProjection;
		}

		return BaseDirect3DDevice9::SetTransform(State, pProjectionToSet);
	}

	return BaseDirect3DDevice9::SetTransform(State, pMatrix);
}

/**
* Not implemented now - fix in case it needs fixing, calls super method.
***/
HRESULT WINAPI D3DProxyDevice::MultiplyTransform(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called MultiplyTransform");
	#endif
	OutputDebugString(__FUNCTION__); 
	OutputDebugString("\n"); 
	OutputDebugString("Not implemented - Fix Me! (if i need fixing)\n"); 

	return BaseDirect3DDevice9::MultiplyTransform(State, pMatrix);
}

/**
* Try and set, if success save viewport.
* Also, it captures the viewport state in stored proxy state block.
* If viewport width and height match primary render target size and zmin is 0 and zmax 1 set 
* m_bActiveViewportIsDefault flag true.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
* @see m_bActiveViewportIsDefault
***/
HRESULT WINAPI D3DProxyDevice::SetViewport(CONST D3DVIEWPORT9* pViewport)
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called SetViewport");
	#endif
	HRESULT result = BaseDirect3DDevice9::SetViewport(pViewport);

	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(*pViewport);
		}
		else {
			m_bActiveViewportIsDefault = isViewportDefaultForMainRT(pViewport);
			m_LastViewportSet = *pViewport;
		}
	}

	
	if (m_bViewportIsSquished)
		SetGUIViewport();
	
	return result;
}

/**
* Creates proxy state block.
* Also, selects capture type option according to state block type.
* @param ppSB [in, out] The proxy (or wrapped) state block returned.
* @see D3DProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::CreateStateBlock(D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateStateBlock");
	#endif
	IDirect3DStateBlock9* pActualStateBlock = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateStateBlock(Type, &pActualStateBlock);

	if (SUCCEEDED(creationResult)) {

		D3D9ProxyStateBlock::CaptureType capType;

		switch (Type) {
		case D3DSBT_ALL: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Full;
				break;
			}

		case D3DSBT_PIXELSTATE: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Pixel;
				break;
			}

		case D3DSBT_VERTEXSTATE: 
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Vertex;
				break;
			}

		default:
			{
				capType = D3D9ProxyStateBlock::Cap_Type_Full;
				break;
			}    
		}

		*ppSB = new D3D9ProxyStateBlock(pActualStateBlock, this, capType, m_currentRenderingSide == vireio::Left);
	}

	return creationResult;
}

/**
* Creates and stores proxy state block.
* @see D3DProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::BeginStateBlock()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called BeginStateBlock");
	#endif
	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::BeginStateBlock())) {
		m_bInBeginEndStateBlock = true;
		m_pCapturingStateTo = new D3D9ProxyStateBlock(NULL, this, D3D9ProxyStateBlock::Cap_Type_Selected, m_currentRenderingSide == vireio::Left);
	}

	return result;
}

/**
* Calls both super method and method from stored proxy state block.
* @param [in, out] The returned proxy (or wrapped) state block.
* @see D3D9ProxyStateBlock::EndStateBlock()
***/
HRESULT WINAPI D3DProxyDevice::EndStateBlock(IDirect3DStateBlock9** ppSB)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ppSB");
	#endif
	IDirect3DStateBlock9* pActualStateBlock = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::EndStateBlock(&pActualStateBlock);

	if (SUCCEEDED(creationResult)) {
		m_pCapturingStateTo->EndStateBlock(pActualStateBlock);
		*ppSB = m_pCapturingStateTo;
	}
	else {
		m_pCapturingStateTo->Release();
		if (m_pCapturingStateTo) delete m_pCapturingStateTo;
	}

	m_pCapturingStateTo = NULL;
	m_bInBeginEndStateBlock = false;

	return creationResult;
}

/**
* Provides texture from stored active (mono) texture stages.
* @see D3D9ProxyTexture
***/
HRESULT WINAPI D3DProxyDevice::GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetTexture");
	#endif
	if (m_activeTextureStages.count(Stage) != 1)
		return D3DERR_INVALIDCALL;
	else {
		*ppTexture = m_activeTextureStages[Stage];
		if ((*ppTexture))
			(*ppTexture)->AddRef();
		return D3D_OK;
	}
}

/**
* Calls a helper function to unwrap the textures and calls the super method for both sides.
* Update stored active (mono) texture stages if new texture was successfully set.
*
* @see vireio::UnWrapTexture() 
***/
HRESULT WINAPI D3DProxyDevice::SetTexture(DWORD Stage,IDirect3DBaseTexture9* pTexture)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetTexture");
	#endif
	HRESULT result;
	if (pTexture) {

		IDirect3DBaseTexture9* pActualLeftTexture = NULL;
		IDirect3DBaseTexture9* pActualRightTexture = NULL;

		vireio::UnWrapTexture(pTexture, &pActualLeftTexture, &pActualRightTexture);

		// Try and Update the actual devices textures
		if ((pActualRightTexture == NULL) || (m_currentRenderingSide == vireio::Left)) // use left (mono) if not stereo or one left side
			result = BaseDirect3DDevice9::SetTexture(Stage, pActualLeftTexture);
		else
			result = BaseDirect3DDevice9::SetTexture(Stage, pActualRightTexture);

	}
	else {
		result = BaseDirect3DDevice9::SetTexture(Stage, NULL);
	}

	// Update m_activeTextureStages if new texture was successfully set
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(Stage, pTexture);
		}
		else {

			// remove existing texture that was active at Stage if there is one
			if (m_activeTextureStages.count(Stage) == 1) { 

				IDirect3DBaseTexture9* pOldTexture = m_activeTextureStages.at(Stage);
				if (pOldTexture)
					pOldTexture->Release();

				m_activeTextureStages.erase(Stage);
			}

			// insert new texture (can be a NULL pointer, this is important for StateBlock tracking)
			if(m_activeTextureStages.insert(std::pair<DWORD, IDirect3DBaseTexture9*>(Stage, pTexture)).second) {
				//success
				if (pTexture)
					pTexture->AddRef();
			}
			else {
				OutputDebugString(__FUNCTION__);
				OutputDebugString("\n");
				OutputDebugString("Unable to store active Texture Stage.\n");
				assert(false);

				//If we get here the state of the texture tracking is fubared and an implosion is imminent.

				result = D3DERR_INVALIDCALL;
			}
		}
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawPrimitive");
	#endif
	
	if(bSkipFrame)
		return D3D_OK;

	//If we shouldn't draw this shader, then just return immediately
	if (m_bDoNotDrawVShader || m_bDoNotDrawPShader)
		return S_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount))) {
		if (!m_b2dDepthMode && switchDrawingSide())
			BaseDirect3DDevice9::DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawIndexedPrimitive");
	#endif
	
	if(bSkipFrame)
		return D3D_OK;

	//If we shouldn't draw this shader, then just return immediately
	if (m_bDoNotDrawVShader || m_bDoNotDrawPShader)
		return S_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount))) {
		if (!m_b2dDepthMode && switchDrawingSide()) {			
			HRESULT result2 = BaseDirect3DDevice9::DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			if (result != result2)
				OutputDebugString("moop\n");
		}
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT PrimitiveCount,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawPrimitiveUP");
	#endif
	
	if(bSkipFrame)
		return D3D_OK;

	//If we shouldn't draw this shader, then just return immediately
	if (m_bDoNotDrawVShader || m_bDoNotDrawPShader)
		return S_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride))) {
		if (!m_b2dDepthMode && switchDrawingSide())
			BaseDirect3DDevice9::DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	return result;
}

/**
* Applies all dirty shader registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawIndexedPrimitiveUP");
	#endif
	
	if(bSkipFrame)
		return D3D_OK;

	//If we shouldn't draw this shader, then just return immediately
	if (m_bDoNotDrawVShader || m_bDoNotDrawPShader)
		return S_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride))) {
		if (!m_b2dDepthMode && switchDrawingSide())		
			BaseDirect3DDevice9::DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
	}

	return result;
}

/**
* Applies all dirty shader registers, processes vertices.
***/
HRESULT WINAPI D3DProxyDevice::ProcessVertices(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer9* pDestBuffer,IDirect3DVertexDeclaration9* pVertexDecl,DWORD Flags)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ProcessVertices");
	#endif
	if (!pDestBuffer)
		return D3DERR_INVALIDCALL;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	BaseDirect3DVertexBuffer9* pCastDestBuffer = static_cast<BaseDirect3DVertexBuffer9*>(pDestBuffer);
	BaseDirect3DVertexDeclaration9* pCastVertexDeclaration = NULL;

	HRESULT result;
	if (pVertexDecl) {
		pCastVertexDeclaration = static_cast<BaseDirect3DVertexDeclaration9*>(pVertexDecl);
		result = BaseDirect3DDevice9::ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pCastDestBuffer->getActual(), pCastVertexDeclaration->getActual(), Flags);
	}
	else {
		result = BaseDirect3DDevice9::ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pCastDestBuffer->getActual(), NULL, Flags);
	}

	return result;
}

/**
* Creates base vertex declaration (BaseDirect3DVertexDeclaration9).
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateVertexDeclaration");
	#endif
	IDirect3DVertexDeclaration9* pActualVertexDeclaration = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexDeclaration(pVertexElements, &pActualVertexDeclaration );

	if (SUCCEEDED(creationResult))
		*ppDecl = new BaseDirect3DVertexDeclaration9(pActualVertexDeclaration, this);

	return creationResult;
}

/**
* Catches vertex declaration in stored proxy state block.
* First, set vertex declaration by base function.
* @see D3D9ProxyStateBlock
***/
HRESULT WINAPI D3DProxyDevice::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetVertexDeclaration");
	#endif
	BaseDirect3DVertexDeclaration9* pWrappedVDeclarationData = static_cast<BaseDirect3DVertexDeclaration9*>(pDecl);

	// Update actual Vertex Declaration
	HRESULT result;
	if (pWrappedVDeclarationData)
		result = BaseDirect3DDevice9::SetVertexDeclaration(pWrappedVDeclarationData->getActual());
	else
		result = BaseDirect3DDevice9::SetVertexDeclaration(NULL);

	// Update stored proxy Vertex Declaration
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedVDeclarationData);
		}
		else {

			if (m_pActiveVertexDeclaration) {
				m_pActiveVertexDeclaration->Release();
			}

			m_pActiveVertexDeclaration = pWrappedVDeclarationData;
			if (m_pActiveVertexDeclaration) {
				m_pActiveVertexDeclaration->AddRef();
			}
		}
	}

	return result;
}

/**
* Provides currently stored vertex declaration.
***/
HRESULT WINAPI D3DProxyDevice::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetVertexDelcaration");
	#endif
	if (!m_pActiveVertexDeclaration) 
		// TODO check this is the response if no declaration set
		//In Response to TODO:  JB, Jan 12. I believe it crashes most times this happens, tested by simply nulling out the ppDecl pointer and passing it into the base d3d method
		return D3DERR_INVALIDCALL; 

	*ppDecl = m_pActiveVertexDeclaration;

	return D3D_OK;
}

/**
* Creates proxy (wrapped) vertex shader.
* @param ppShader [in, out] The created proxy vertex shader.
* @see D3D9ProxyVertexShader
***/
HRESULT WINAPI D3DProxyDevice::CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateVertexShader");
	#endif
	IDirect3DVertexShader9* pActualVShader = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateVertexShader(pFunction, &pActualVShader);

	std::string shaderReplacementCode;
	if (m_pGameHandler->GetShaderModificationRepository() &&
		m_pGameHandler->GetShaderModificationRepository()->ReplaceShaderCode(pActualVShader, shaderReplacementCode))
	{
		ID3DXBuffer *pBuffer = NULL;
		ProxyHelper ph;
		std::string shaderCodeFilename = ph.GetBaseDir() + std::string("shader_replacements\\") + shaderReplacementCode;
		OutputDebugString(("ReplaceShaderCode: " + shaderCodeFilename).c_str());
		HRESULT hr = D3DXAssembleShaderFromFile(shaderCodeFilename.c_str(), NULL, NULL, 0, &pBuffer, NULL);
		if (FAILED(hr))
		{
			OutputDebugString("ReplaceShaderCode - FAILED - Using original Shader");
		}
		else
		{
			pActualVShader->Release();
			pActualVShader = NULL;
			creationResult = BaseDirect3DDevice9::CreateVertexShader((const DWORD*)pBuffer->GetBufferPointer(), &pActualVShader);
		}
	}

	if (SUCCEEDED(creationResult)) {
		*ppShader = new D3D9ProxyVertexShader(pActualVShader, this, m_pGameHandler->GetShaderModificationRepository());
	}
	else
	{
		OutputDebugString("Failed to create the vertex shader!");
	}

	return creationResult;
}

/**
* Sets and updates stored proxy vertex shader.
* @see D3D9ProxyVertexShader
***/
HRESULT WINAPI D3DProxyDevice::SetVertexShader(IDirect3DVertexShader9* pShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetVertexSHader");
	#endif
	D3D9ProxyVertexShader* pWrappedVShaderData = static_cast<D3D9ProxyVertexShader*>(pShader);

	// Update actual Vertex shader
	HRESULT result;
	if (pWrappedVShaderData)
		result = BaseDirect3DDevice9::SetVertexShader(pWrappedVShaderData->getActual());
	else
		result = BaseDirect3DDevice9::SetVertexShader(NULL);

	// Update stored proxy Vertex shader
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedVShaderData);
		}
		else {
			if (m_pActiveVertexShader) {
				m_pActiveVertexShader->Release();
			}

			m_pActiveVertexShader = pWrappedVShaderData;
			if (m_pActiveVertexShader) {
				m_pActiveVertexShader->AddRef();
			}

			m_spManagedShaderRegisters->ActiveVertexShaderChanged(m_pActiveVertexShader);
		}
	}

	if (pWrappedVShaderData)
	{
		//Flag whether we should even draw this shader
		m_bDoNotDrawVShader = pWrappedVShaderData->DoNotDraw();

		if (pWrappedVShaderData->SquishViewport())
		{
			SetGUIViewport();
		}
		else
		{
			if (m_bViewportIsSquished)
				BaseDirect3DDevice9::SetViewport(&m_LastViewportSet);
			m_bViewportIsSquished = false;
		}
	}
	else
		m_bDoNotDrawVShader = false;

	// increase vertex shader call count
	++m_VertexShaderCount;
	return result;
}

/**
* Returns the stored and active proxy vertex shader.
***/
HRESULT WINAPI D3DProxyDevice::GetVertexShader(IDirect3DVertexShader9** ppShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetVertexShader");
	#endif
	if (!m_pActiveVertexShader)
		return D3DERR_INVALIDCALL;

	*ppShader = m_pActiveVertexShader;

	return D3D_OK;
}

/**
* Sets shader constants either at stored proxy state block or in managed shader register class.
* @see D3D9ProxyStateBlock
* @see ShaderRegisters
***/
HRESULT WINAPI D3DProxyDevice::SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SEtVertexShadewrConstantF");
	#endif
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_pCapturingStateTo) {
		result = m_pCapturingStateTo->SelectAndCaptureStateVSConst(StartRegister, pConstantData, Vector4fCount);
	}
	else { 
		result = m_spManagedShaderRegisters->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
	}

	return result;
}

/**
* Provides constant registers from managed shader register class.
* @see ShaderRegisters
* @see ShaderRegisters::GetVertexShaderConstantF()
***/
HRESULT WINAPI D3DProxyDevice::GetVertexShaderConstantF(UINT StartRegister,float* pData,UINT Vector4fCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetVertexShaderConstantF");
	#endif
	return m_spManagedShaderRegisters->GetVertexShaderConstantF(StartRegister, pData, Vector4fCount);
}

/**
* Sets stream source and updates stored vertex buffers.
* Also, it calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called SetStreamSource");
	#endif
	BaseDirect3DVertexBuffer9* pCastStreamData = static_cast<BaseDirect3DVertexBuffer9*>(pStreamData);
	HRESULT result;
	if (pStreamData) {		
		result = BaseDirect3DDevice9::SetStreamSource(StreamNumber, pCastStreamData->getActual(), OffsetInBytes, Stride);
	}
	else {
		result = BaseDirect3DDevice9::SetStreamSource(StreamNumber, NULL, OffsetInBytes, Stride);
	}


	// Update m_activeVertexBuffers if new vertex buffer was successfully set
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(StreamNumber, pCastStreamData);
		}
		else {
			// remove existing vertex buffer that was active at StreamNumber if there is one
			if (m_activeVertexBuffers.count(StreamNumber) == 1) { 

				IDirect3DVertexBuffer9* pOldBuffer = m_activeVertexBuffers.at(StreamNumber);
				if (pOldBuffer == pStreamData)
					return result;

				if (pOldBuffer)
					pOldBuffer->Release();

				m_activeVertexBuffers.erase(StreamNumber);
			}

			// insert new vertex buffer
			if(m_activeVertexBuffers.insert(std::pair<UINT, BaseDirect3DVertexBuffer9*>(StreamNumber, pCastStreamData)).second) {
				//success
				if (pStreamData)
					pStreamData->AddRef();
			}
			else {
				OutputDebugString(__FUNCTION__);
				OutputDebugString("\n");
				OutputDebugString("Unable to store active Texture Stage.\n");
				assert(false);

				//If we get here the state of the texture tracking is fubared and an implosion is imminent.

				result = D3DERR_INVALIDCALL;
			}
		}
	}

	return result;
}

/**
* Provides stream data from stored vertex buffers.
* TODO ppStreamData is marked in and out in docs. Potentially it can be a get when the stream hasn't been set before???
* Category of probleme: Worry about it if it breaks.
***/
HRESULT WINAPI D3DProxyDevice::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData,UINT* pOffsetInBytes,UINT* pStride)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetSTreamSource");
	#endif
	// This whole methods implementation is highly questionable. Not sure exactly how GetStreamSource works
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_activeVertexBuffers.count(StreamNumber) == 1) {

		//IDirect3DVertexBuffer9* pCurrentActual = m_activeVertexBuffers[StreamNumber]->getActual();

		//IDirect3DVertexBuffer9* pActualResultBuffer = NULL;
		//HRESULT result = BaseDirect3DDevice9::GetStreamSource(StreamNumber, &pCurrentActual, pOffsetInBytes, pStride);

		*ppStreamData = m_activeVertexBuffers[StreamNumber];
		if ((*ppStreamData))
			(*ppStreamData)->AddRef();

		result = D3D_OK;
	}
	return result;
}

/**
* Sets indices and calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetIndices(IDirect3DIndexBuffer9* pIndexData)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetIndices");
	#endif
	BaseDirect3DIndexBuffer9* pWrappedNewIndexData = static_cast<BaseDirect3DIndexBuffer9*>(pIndexData);

	// Update actual index buffer
	HRESULT result;
	if (pWrappedNewIndexData)
		result = BaseDirect3DDevice9::SetIndices(pWrappedNewIndexData->getActual());
	else
		result = BaseDirect3DDevice9::SetIndices(NULL);

	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedNewIndexData);
		}
		else {
			// Update stored proxy index buffer
			if (m_pActiveIndicies) {
				m_pActiveIndicies->Release();
			}

			m_pActiveIndicies = pWrappedNewIndexData;
			if (m_pActiveIndicies) {
				m_pActiveIndicies->AddRef();
			}
		}
	}

	return result;
}

/**
* Provides stored indices.
***/
HRESULT WINAPI D3DProxyDevice::GetIndices(IDirect3DIndexBuffer9** ppIndexData)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetIndices");
	#endif
	if (!m_pActiveIndicies)
		return D3DERR_INVALIDCALL;

	*ppIndexData = m_pActiveIndicies;
	m_pActiveIndicies->AddRef();

	return D3D_OK;
}

/**
* Base CreatePixelShader functionality.
***/
HRESULT WINAPI D3DProxyDevice::CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreatePixelSHader");
	#endif
	IDirect3DPixelShader9* pActualPShader = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreatePixelShader(pFunction, &pActualPShader);

	if (SUCCEEDED(creationResult)) {
		*ppShader = new D3D9ProxyPixelShader(pActualPShader, this, m_pGameHandler->GetShaderModificationRepository());
	}

	return creationResult;
}

/**
* Sets pixel shader and calls proxy state block to capture states.
* @see D3D9ProxyStateBlock::SelectAndCaptureState()
***/
HRESULT WINAPI D3DProxyDevice::SetPixelShader(IDirect3DPixelShader9* pShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetPixelShader");
	#endif
	D3D9ProxyPixelShader* pWrappedPShaderData = static_cast<D3D9ProxyPixelShader*>(pShader);

	// Update actual pixel shader
	HRESULT result;
	if (pWrappedPShaderData)
		result = BaseDirect3DDevice9::SetPixelShader(pWrappedPShaderData->getActual());
	else
	{
		result = BaseDirect3DDevice9::SetPixelShader(NULL);
		return result;
	}

	// Update stored proxy pixel shader
	if (SUCCEEDED(result)) {

		// If in a Begin-End StateBlock pair update the block state rather than the current proxy device state
		if (m_pCapturingStateTo) {
			m_pCapturingStateTo->SelectAndCaptureState(pWrappedPShaderData);
		}
		else {

			if (m_pActivePixelShader) {
				m_pActivePixelShader->Release();
			}

			m_pActivePixelShader = pWrappedPShaderData;
			if (m_pActivePixelShader) {
				m_pActivePixelShader->AddRef();
			}

			m_spManagedShaderRegisters->ActivePixelShaderChanged(m_pActivePixelShader);
		}
	}

	if (pWrappedPShaderData)
	{
		//Flag whether we should even draw this shader
		m_bDoNotDrawPShader = pWrappedPShaderData->DoNotDraw();
	}
	else
		m_bDoNotDrawPShader = false;

	return result;
}

/**
* Provides stored pixel shader.
***/
HRESULT WINAPI D3DProxyDevice::GetPixelShader(IDirect3DPixelShader9** ppShader)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetPixelSHader");
	#endif
	if (!m_pActivePixelShader)
		return D3DERR_INVALIDCALL;

	*ppShader = m_pActivePixelShader;

	return D3D_OK;
}

/**
* Sets shader constants either at stored proxy state block or in managed shader register class.
* @see D3D9ProxyStateBlock
* @see ShaderRegisters
***/
HRESULT WINAPI D3DProxyDevice::SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetPixelShaderConstantF");
	#endif
	HRESULT result = D3DERR_INVALIDCALL;

	if (m_pCapturingStateTo) {
		result = m_pCapturingStateTo->SelectAndCaptureStatePSConst(StartRegister, pConstantData, Vector4fCount);
	}
	else { 
		result = m_spManagedShaderRegisters->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
	}

	return result;
}

/**
* Provides constant registers from managed shader register class.
* @see ShaderRegisters
* @see ShaderRegisters::GetPixelShaderConstantF()
***/
HRESULT WINAPI D3DProxyDevice::GetPixelShaderConstantF(UINT StartRegister,float* pData,UINT Vector4fCount)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called GetPixelSHaderConstantF");
	#endif
	return m_spManagedShaderRegisters->GetPixelShaderConstantF(StartRegister, pData, Vector4fCount);
}

/**
* Applies all dirty registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide()
***/
HRESULT WINAPI D3DProxyDevice::DrawRectPatch(UINT Handle,CONST float* pNumSegs,CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawRectPatch");
	#endif
	if(bSkipFrame)
		return D3D_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawRectPatch(Handle, pNumSegs, pRectPatchInfo))) {
		if (!m_b2dDepthMode && switchDrawingSide())		
			BaseDirect3DDevice9::DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
	}

	return result;
}

/**
* Applies all dirty registers, draws both stereo sides if switchDrawingSide() agrees.
* @see switchDrawingSide() 
***/
HRESULT WINAPI D3DProxyDevice::DrawTriPatch(UINT Handle,CONST float* pNumSegs,CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawTriPatch");
	#endif
	if(bSkipFrame)
		return D3D_OK;

	m_spManagedShaderRegisters->ApplyAllDirty(m_currentRenderingSide);

	HRESULT result;
	if (SUCCEEDED(result = BaseDirect3DDevice9::DrawTriPatch(Handle, pNumSegs, pTriPatchInfo))) {
		if (!m_b2dDepthMode && switchDrawingSide())		
			BaseDirect3DDevice9::DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
	}

	return result;
}

/**
* Base CreateQuery functionality.
***/
HRESULT WINAPI D3DProxyDevice::CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateQuery");
	#endif
	// this seems a weird response to me but it's what the actual device does.
	if (!ppQuery)
		return D3D_OK;

	IDirect3DQuery9* pActualQuery = NULL;
	HRESULT creationResult = BaseDirect3DDevice9::CreateQuery(Type, &pActualQuery);

	if (SUCCEEDED(creationResult)) {
		*ppQuery = new BaseDirect3DQuery9(pActualQuery, this);
	}

	return creationResult;
}

/**
* Creates proxy (wrapped) render target, if swapchain buffer returns StereoBackBuffer, otherwise D3D9ProxySurface.
* Duplicates render target if game handler agrees.
* @see GameHandler::ShouldDuplicateRenderTarget()
* @see StereoBackBuffer
* @see D3D9ProxySurface
***/
HRESULT WINAPI D3DProxyDevice::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
												  DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle, bool isSwapChainBackBuffer)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called CreateRenderTarget");
	#endif
	IDirect3DSurface9* pLeftRenderTarget = NULL;
	IDirect3DSurface9* pRightRenderTarget = NULL;
	HRESULT creationResult;
	if(bSkipFrame)
		return D3D_OK;

	// create left/mono
	if (SUCCEEDED(creationResult = BaseDirect3DDevice9::CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, &pLeftRenderTarget, pSharedHandle))) {

		/* "If Needed" heuristic is the complicated part here.
		Fixed heuristics (based on type, format, size, etc) + game specific overrides + isForcedMono + magic? */
		// TODO Should we duplicate this Render Target? Replace "true" with heuristic
		if (!m_b2dDepthMode && m_pGameHandler->ShouldDuplicateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, isSwapChainBackBuffer))
		{
			if (FAILED(BaseDirect3DDevice9::CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, &pRightRenderTarget, pSharedHandle))) {
				OutputDebugString("Failed to create right eye render target while attempting to create stereo pair, falling back to mono\n");
				pRightRenderTarget = NULL;
			}
		}
	}
	else {
		OutputDebugString("Failed to create render target\n"); 
	}

	if (SUCCEEDED(creationResult)) {
		if (!isSwapChainBackBuffer)
			*ppSurface = new D3D9ProxySurface(pLeftRenderTarget, pRightRenderTarget, this, NULL);
		else
			*ppSurface = new StereoBackBuffer(pLeftRenderTarget, pRightRenderTarget, this);
	}

	return creationResult;
}


/**
* This method must be called on the proxy device before the device is returned to the calling application.
* Inits by game configuration.
* Subclasses which override this method must call through to super method.
* Anything that needs to be done before the device is used by the actual application should happen here.
* @param The game (or engine) specific configuration.
***/
void D3DProxyDevice::Init(ProxyHelper::ProxyConfig& cfg)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called Init");
	#endif
	OutputDebugString("D3D ProxyDev Init\n");

	// get config and backup it
	config = cfg;
	memcpy(&m_configBackup, &cfg, sizeof(ProxyHelper::ProxyConfig));

	m_bfloatingMenu = false;
	m_bfloatingScreen = false;
	m_bSurpressHeadtracking = false;
	m_bSurpressPositionaltracking = false;

	char buf[64];
	LPCSTR psz = NULL;
	sprintf_s(buf, "type: %d, aspect: %f\n", config.game_type, config.aspect_multiplier);
	psz = buf;
	OutputDebugString(psz);

	// first time configuration
	m_spShaderViewAdjustment->Load(config);
	m_pGameHandler->Load(config, m_spShaderViewAdjustment);
	stereoView = StereoViewFactory::Get(config, m_spShaderViewAdjustment->HMDInfo());
	stereoView->YOffset = config.YOffset;
	stereoView->HeadYOffset = 0;
	stereoView->HeadZOffset = FLT_MAX;
	stereoView->IPDOffset = config.IPDOffset;
	stereoView->DistortionScale = config.DistortionScale;
	stereoView->m_b2dDepthMode = false;	

	m_maxDistortionScale = config.DistortionScale;

	VPMENU_UpdateDeviceSettings();
	OnCreateOrRestore();
}

/**
* Creates HUD according to viewport height.
***/
void D3DProxyDevice::SetupHUD()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetupHUD");
	#endif
	D3DXCreateFont( this, 32, 0, FW_BOLD, 4, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Calibri", &hudFont );
	D3DXCreateFont( this, 26, 0, FW_BOLD, 4, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Courier New", &errorFont );

	//Create all font size
	for (int fontSize = 0; fontSize < 27; ++fontSize)
		D3DXCreateFont( this, fontSize, 0, FW_BOLD, 4, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Calibri", &popupFont[fontSize] );

	D3DXCreateSprite(this, &hudMainMenu);
	D3DXCreateSprite(this, &hudTextBox);
}


//Persist, just to the registry for now
void D3DProxyDevice::DuckAndCover::SaveToRegistry()
{
	HKEY hKey;
	LONG openRes = RegOpenKeyEx(HKEY_CURRENT_USER, _T("SOFTWARE\\Vireio\\Perception"), 0, KEY_ALL_ACCESS , &hKey);
	if (openRes==ERROR_SUCCESS)
	{
		char buffer[128];
		sprintf_s(buffer, "%i %i %i %i %i %i %i", (int)jumpKey, jumpEnabled ? 1 : 0, (int)crouchKey, crouchToggle ? 1 : 0, (int)proneKey, proneEnabled ? 1 : 0, proneToggle ? 1 : 0);
		RegSetValueEx(hKey, _T("DuckAndCOver"), 0, REG_SZ, (LPBYTE)buffer, strlen(buffer)+1);
		RegCloseKey(hKey);
	}
}

void D3DProxyDevice::DuckAndCover::LoadFromRegistry()
{
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_CURRENT_USER,  _T("SOFTWARE\\Vireio\\Perception"),
		0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
	{
		TCHAR szData[128];
		DWORD dwKeyDataType;
		DWORD dwDataBufSize = sizeof(szData);
		if (RegQueryValueEx(hKey, _T("DuckAndCOver"), NULL, &dwKeyDataType,
			(LPBYTE) &szData, &dwDataBufSize) == ERROR_SUCCESS &&
			_tcslen(szData) > 0)
		{
			int a=0, b=0, c=0, d=0, e=0, f=0, g=0;
			sscanf_s(szData, "%i %i %i %i %i %i %i", &a, &b, &c, &d, &e, &f, &g);
			jumpKey = (byte) a;
			jumpEnabled = b == 1;
			crouchKey = (byte)c;
			crouchToggle = d == 1;
			proneKey = (byte)e;
			proneEnabled = f == 1;
			proneToggle = g == 1;
		}

		RegCloseKey(hKey);
	}
}


/**
* Updates selected motion tracker orientation.
***/
void D3DProxyDevice::HandleTracking()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called HandleTracking");
	#endif

	if(!tracker)
	{
		InitTracker();
	}

	if(!tracker || tracker->getStatus() < MTS_OK)
	{
		if (tracker)
		{
			//Populate with popups about the following
			switch (tracker->getStatus())
			{
				case MTS_NOTINIT:
					{
						VireioPopup popup(VPT_NO_HMD_DETECTED, VPS_ERROR, 10000);
						strcpy_s(popup.line[2], "HMD NOT INITIALISED");
						ShowPopup(popup);
					}
					break;
				case MTS_INITIALISING:
					{
						VireioPopup popup(VPT_NO_HMD_DETECTED);
						strcpy_s(popup.line[2], "HMD INITIALISING");
						ShowPopup(popup);
					}
					break;
				case MTS_NOHMDDETECTED:
					{
						VireioPopup popup(VPT_NO_HMD_DETECTED, VPS_ERROR, 10000);
						strcpy_s(popup.line[2], "HMD NOT DETECTED");
						ShowPopup(popup);
					}
					break;
				case MTS_INITFAIL:
					{
						VireioPopup popup(VPT_NO_HMD_DETECTED, VPS_ERROR, 10000);
						strcpy_s(popup.line[2], "HMD INITIALISATION FAILED");
						ShowPopup(popup);
					}
					break;
				case MTS_DRIVERFAIL:
					{
						VireioPopup popup(VPT_NO_HMD_DETECTED, VPS_ERROR, 10000);
						strcpy_s(popup.line[2], "TRACKER DRIVER FAILED TO INITIALISE");
						ShowPopup(popup);
					}
					break;
				default:
					break;
			}
		}

		tracker->currentRoll = 0;
		return;
	}

	float xPos=0, yPos=0, zPos=0;
	float yaw=0, pitch=0, roll=0;

	if(tracker->getStatus() >= MTS_OK)
	{
		tracker->updateOrientationAndPosition();

		if (tracker->getStatus() == MTS_OK)
		{
			//Dismiss popups related to issues
			DismissPopup(VPT_POSITION_TRACKING_LOST);
			DismissPopup(VPT_NO_HMD_DETECTED);
			DismissPopup(VPT_NO_ORIENTATION);
		}
		else if (tracker->getStatus() == MTS_NOORIENTATION)
		{
			VireioPopup popup(VPT_NO_ORIENTATION, VPS_ERROR);
			strcpy_s(popup.line[2], "HMD ORIENTATION NOT BEING REPORTED");
			ShowPopup(popup);
		}
		else 
		{
			//Only report positional tracking errors if positional tracking is turned on
			if (!m_bSurpressPositionaltracking)
			{
				if (tracker->getStatus() == MTS_CAMERAMALFUNCTION)
				{
					VireioPopup popup(VPT_NO_HMD_DETECTED, VPS_ERROR);
					strcpy_s(popup.line[2], "CAMERA MALFUNCTION - PLEASE WAIT WHILST CAMERA INITIALISES");
					ShowPopup(popup);
				}
				else if (tracker->getStatus() == MTS_LOSTPOSITIONAL)
				{
					if (userConfig.warnPosLost)
					{
						//Show popup regarding lost positional tracking
						VireioPopup popup(VPT_POSITION_TRACKING_LOST);
						strcpy_s(popup.line[4], "HMD POSITIONAL TRACKING LOST");
						ShowPopup(popup);
					}
				}
			}
			else
			{
				//Dismiss popups related to issues
				DismissPopup(VPT_POSITION_TRACKING_LOST);
				DismissPopup(VPT_NO_HMD_DETECTED);
			}
		}

		// update view adjustment class
		if (tracker->getStatus() >= MTS_OK)
		{
			//Roll implementation
			switch (m_spShaderViewAdjustment->RollImpl())
			{
			case 0:
				{
					//Ensure this is 0, presumably VRBoost taking care of business
					stereoView->m_rotation = 0.0f;
				}
			case 1:
				{
					if (tracker)
						m_spShaderViewAdjustment->UpdateRoll(tracker->currentRoll);
					stereoView->m_rotation = 0.0f;
				}
				break;
			case 2:
				{
					//Set rotation on the stereo view and on the shader adjustment
					if (tracker)
					{
						stereoView->m_rotation = tracker->currentRoll;
						m_spShaderViewAdjustment->UpdateRoll(tracker->currentRoll);
					}
				}
				break;
			}

			if (m_bPosTrackingToggle && tracker->getStatus() != MTS_LOSTPOSITIONAL
				&& !m_bSurpressPositionaltracking)
			{
				//Set translation vector multipliers
				m_spShaderViewAdjustment->x_scaler = config.position_x_multiplier;
				m_spShaderViewAdjustment->y_scaler = config.position_y_multiplier;
				m_spShaderViewAdjustment->z_scaler = config.position_z_multiplier;

				//Use reduced Y-position tracking in DFC mode, user should be triggering crouch by moving up and down
				float yPosition = (VRBoostValue[VRboostAxis::CameraTranslateY] / 20.0f) + tracker->primaryY;
				if (m_DuckAndCover.dfcStatus >= DAC_STANDING)
					yPosition *= 0.25f;

				m_spShaderViewAdjustment->UpdatePosition(tracker->primaryYaw, tracker->primaryPitch, tracker->primaryRoll,
					(VRBoostValue[VRboostAxis::CameraTranslateX] / 20.0f) + tracker->primaryX, 
					yPosition,
					(VRBoostValue[VRboostAxis::CameraTranslateZ] / 20.0f) + tracker->primaryZ,
					config.position_multiplier);
			}

			//Now we test for whether we are using "duck for cover" (for crouch and prone)
			if (m_DuckAndCover.dfcStatus == DAC_STANDING)
			{
				//Should we jump?
				if (tracker->y > m_DuckAndCover.yPos_Jump)
				{
					//Trigger jump
					INPUT ip;
					ip.type = INPUT_KEYBOARD;
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.jumpKey, MAPVK_VK_TO_VSC);
					ip.ki.time = 0;
					ip.ki.dwExtraInfo = 0;
					ip.ki.wVk = 0;
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					SendInput(1, &ip, sizeof(INPUT));
					ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
					SendInput(1, &ip, sizeof(INPUT));
				}

				//trigger crouching
				if (tracker->y < (m_DuckAndCover.yPos_Crouch * 0.55f))
				{
					m_DuckAndCover.dfcStatus = DAC_CROUCH;
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 250);
					strcpy_s(popup.line[0], "Crouch");
					ShowPopup(popup);

					//Trigger crouch button
					INPUT ip;
					ip.type = INPUT_KEYBOARD;
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.crouchKey, MAPVK_VK_TO_VSC);
					ip.ki.time = 0;
					ip.ki.dwExtraInfo = 0;
					ip.ki.wVk = 0;
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					SendInput(1, &ip, sizeof(INPUT));
					if (m_DuckAndCover.crouchToggle)
					{
						ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
						SendInput(1, &ip, sizeof(INPUT));
					}
				}
			}
			else if (m_DuckAndCover.dfcStatus == DAC_CROUCH)
			{
				if (tracker->y > (m_DuckAndCover.yPos_Crouch * 0.45f))
				{
					//back to standing
					m_DuckAndCover.dfcStatus = DAC_STANDING;
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 250);
					strcpy_s(popup.line[0], "Standing");
					ShowPopup(popup);

					INPUT ip;
					ip.type = INPUT_KEYBOARD;
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.crouchKey, MAPVK_VK_TO_VSC);
					ip.ki.time = 0;
					ip.ki.dwExtraInfo = 0;
					ip.ki.wVk = 0;
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					if (m_DuckAndCover.crouchToggle)
					{
						SendInput(1, &ip, sizeof(INPUT));
					}
					ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
					SendInput(1, &ip, sizeof(INPUT));
				}
				else if (m_DuckAndCover.proneEnabled && 
					tracker->y < (m_DuckAndCover.yPos_Crouch + m_DuckAndCover.yPos_Prone * 0.55f))
				{
					m_DuckAndCover.dfcStatus = DAC_PRONE;
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 250);
					strcpy_s(popup.line[0], "Prone");
					ShowPopup(popup);

					INPUT ip;
					ip.type = INPUT_KEYBOARD;
					ip.ki.time = 0;
					ip.ki.dwExtraInfo = 0;
					ip.ki.wVk = 0;

					//If crouch isn't toggle, then we need to release crouch key before going prone
					if (!m_DuckAndCover.crouchToggle)
					{
						ip.ki.wScan = MapVirtualKey(m_DuckAndCover.crouchKey, MAPVK_VK_TO_VSC);
						ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
						SendInput(1, &ip, sizeof(INPUT));
					}

					//Trigger prone button
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.proneKey, MAPVK_VK_TO_VSC);
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					SendInput(1, &ip, sizeof(INPUT));
					if (m_DuckAndCover.proneToggle)
					{
						ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
						SendInput(1, &ip, sizeof(INPUT));
					}
				}
			}
			else if (m_DuckAndCover.proneEnabled &&
				m_DuckAndCover.dfcStatus == DAC_PRONE)
			{
				if (tracker->y > (m_DuckAndCover.yPos_Crouch + m_DuckAndCover.yPos_Prone * 0.45f))
				{
					//back to crouching
					m_DuckAndCover.dfcStatus = DAC_CROUCH;
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 250);
					strcpy_s(popup.line[0], "Crouch");
					ShowPopup(popup);

					//Trigger prone button
					INPUT ip;
					ip.type = INPUT_KEYBOARD;
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.proneKey, MAPVK_VK_TO_VSC);
					ip.ki.time = 0;
					ip.ki.dwExtraInfo = 0;
					ip.ki.wVk = 0;
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					if (m_DuckAndCover.proneToggle)
					{
						SendInput(1, &ip, sizeof(INPUT));
					}
					ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
					SendInput(1, &ip, sizeof(INPUT));

					//If crouch isn't toggle, then we need to crouch key before going prone
					ip.ki.wScan = MapVirtualKey(m_DuckAndCover.crouchKey, MAPVK_VK_TO_VSC);
					ip.ki.dwFlags = KEYEVENTF_SCANCODE;
					SendInput(1, &ip, sizeof(INPUT));
					if (m_DuckAndCover.crouchToggle)
					{
						ip.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
						SendInput(1, &ip, sizeof(INPUT));
					}
				}
			}
		}
	}

	if (m_showVRMouse)
	{
		//Get mouse position on screen
		GetCursorPos(&stereoView->m_mousePos);
		if (m_showVRMouse == 1)
			stereoView->SetVRMouseSquish(guiSquishPresets[(int)gui3DDepthMode]);
		else
			stereoView->SetVRMouseSquish(1.0f - hudDistancePresets[(int)hud3DDepthMode]);

		stereoView->PostReset();
	}

	m_spShaderViewAdjustment->ComputeViewTransforms();

	m_isFirstBeginSceneOfFrame = false;

	//Make static so we don't instantly dismiss any fail messages
	static bool scanFailed = false;

	// update vrboost, if present, tracker available and shader count higher than the minimum
	if ((!m_bSurpressHeadtracking) 
		&& (!m_bForceMouseEmulation || !VRBoostStatus.VRBoost_HasOrientation || VRBoostStatus.VRBoost_Scanning) 
		&& (hmVRboost) && (m_VRboostRulesPresent) 
		&& (tracker->getStatus() >= MTS_OK) && (m_bVRBoostToggle)
		&& (m_VertexShaderCountLastFrame>(UINT)config.VRboostMinShaderCount) 
		&& (m_VertexShaderCountLastFrame<(UINT)config.VRboostMaxShaderCount) )
	{
		VRBoostStatus.VRBoost_Active = true;
		// development bool
		bool createNSave = false;

		// apply VRboost memory rules if present
		float yaw = tracker->primaryYaw;
		if ((m_comfortModeYaw == 180.f && tracker->primaryYaw > 0.0f) ||
			(m_comfortModeYaw == -180.f && tracker->primaryYaw < 0.0f))
			yaw += ((-m_comfortModeYaw / 180.0f) * (float)PI);
		else
			yaw += ((m_comfortModeYaw / 180.0f) * (float)PI);

		VRBoostValue[VRboostAxis::TrackerYaw] = yaw;
		VRBoostValue[VRboostAxis::TrackerPitch] = tracker->primaryPitch;
		VRBoostValue[VRboostAxis::TrackerRoll] = tracker->primaryRoll;

		//Telescopic sight mode implementation
		if (m_telescopeTargetFOV != FLT_MAX)
		{
			if (m_telescopeTargetFOV < m_telescopeCurrentFOV)
				m_telescopeCurrentFOV -= 1.0f;
			else if (m_telescopeTargetFOV > m_telescopeCurrentFOV)
				m_telescopeCurrentFOV += 1.0f;

			if (abs(m_telescopeCurrentFOV - m_telescopeTargetFOV) < 1.0)
				m_telescopeCurrentFOV = m_telescopeTargetFOV;

			if (!m_telescopicSightMode &&
				m_telescopeCurrentFOV == m_telescopeTargetFOV)
			{
				m_telescopeTargetFOV = FLT_MAX;
				VRBoostValue[VRboostAxis::WorldFOV] = config.WorldFOV;
			}
			else
				VRBoostValue[VRboostAxis::WorldFOV] = m_telescopeCurrentFOV;
		}

		//If we know VRBoost is scanning, don't even try to apply memory rules this time round
		if (VRBoostStatus.VRBoost_Scanning ||
			m_pVRboost_ApplyMemoryRules(MAX_VRBOOST_VALUES, (float**)&VRBoostValue) != VRBOOST_OK)
		{
			VRBoostStatus.VRBoost_ApplyRules = false;
			if (!createNSave)
			{
				// load VRboost rules
				if (config.VRboostPath != "")
				{
#ifdef SHOW_CALLS
					OutputDebugString(std::string("config.VRboostPath: " + config.VRboostPath).c_str());
#endif 
					VRBoost::ReturnValue vr = m_pVRboost_LoadMemoryRules(config.game_exe, config.VRboostPath);

					switch (vr)
					{
					case VRBoost::VRBOOST_ERROR:
						{
							DismissPopup(VPT_VRBOOST_SCANNING);
							VRBoostStatus.VRBoost_Scanning = false;
							VRBoostStatus.VRBoost_LoadRules = false;
						}
						break;
					case VRBoost::VRBOOST_SCAN_READY:
						{
							//Indicate we are going to be scanning
							scanFailed = false;
							VRBoostStatus.VRBoost_Scanning = true;

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							DismissPopup(VPT_VRBOOST_SCANNING);
							VireioPopup popup(VPT_VRBOOST_SCANNING, VPS_INFO);
							strcpy_s(popup.line[0], "VRBoost Memory Scan");
							strcpy_s(popup.line[1], "===================");
							strcpy_s(popup.line[2], "STATUS: WAITING USER ACTIVATION");
							strcpy_s(popup.line[3], " - Once you are \"in-game\", press NUMPAD5 to start memory scan");
							strcpy_s(popup.line[4], " - Press NUMPAD5 to repeat if memory scan fails");
							strcpy_s(popup.line[5], " - Press NUMPAD8 to cancel VRBoost and turn on mouse emulation");
							ShowPopup(popup);
						}
						break;
					case VRBoost::VRBOOST_SCAN_INITIALISING:
						{
							//Indicate we are going to be scanning
							scanFailed = false;
							VRBoostStatus.VRBoost_Scanning = true;

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							DismissPopup(VPT_VRBOOST_SCANNING);
							VireioPopup popup(VPT_VRBOOST_SCANNING, VPS_INFO);
							strcpy_s(popup.line[0], "VRBoost Memory Scan");
							strcpy_s(popup.line[1], "===================");

							float percent = m_pVRboost_GetScanInitPercent();
							sprintf_s(popup.line[2], "STATUS: INITIALISING - %.1f%% Complete", percent);
							strcpy_s(popup.line[3], "Setting up scanner parameters - Please wait..");
							ShowPopup(popup);
						}
						break;
					case VRBoost::VRBOOST_SCANNING:
						{
							scanFailed = false;
							VRBoostStatus.VRBoost_Scanning = true;

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							DismissPopup(VPT_VRBOOST_SCANNING);
							VireioPopup popup(VPT_VRBOOST_SCANNING, VPS_INFO);
							strcpy_s(popup.line[0], "VRBoost Memory Scan");
							strcpy_s(popup.line[1], "===================");
							UINT candidates = m_pVRboost_GetScanCandidates();
							sprintf_s(popup.line[2], "STATUS: SCANNING (%i candidates)", candidates);
							strcpy_s(popup.line[4], "Please look around to assist with orientation detection");
							ShowPopup(popup);
						}
						break;
					//This is the case where the scanner needs the user to follow particular steps to identify
					//the addresses, such as LOOK UP and LOOK DOWN
					case VRBoost::VRBOOST_SCANNING_ASSIST:
						{
							scanFailed = false;
							VRBoostStatus.VRBoost_Scanning = true;

							char *instruction = new char[256];
							ZeroMemory(instruction, 256);
							DWORD timeToEvent = 0;
							m_pVRboost_GetScanAssist((char**)&instruction, &timeToEvent);

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							DismissPopup(VPT_VRBOOST_SCANNING);
							VireioPopup popup(VPT_VRBOOST_SCANNING, VPS_INFO);
							strcpy_s(popup.line[0], "VRBoost Memory Scan");
							strcpy_s(popup.line[1], "===================");
							UINT candidates = m_pVRboost_GetScanCandidates();
							sprintf_s(popup.line[2], "STATUS: SCANNING (%i candidates) - REQUIRES USER ASSISTANCE", candidates);
							if (timeToEvent == MAXDWORD)
							{
								strcpy_s(popup.line[3], "    PLEASE LOOK STRAIGHT-AHEAD THEN");
								strcpy_s(popup.line[4], "    PRESS SCAN TRIGGER (NUMPAD5) TO START \"ASSISTED\" SCAN");
							}
							else
								sprintf_s(popup.line[3], "       ***  PLEASE LOOK:    %s   -   %i  ***", instruction, (timeToEvent/1000)+1);
							delete []instruction;
							ShowPopup(popup);
						}
						break;
					case VRBoost::VRBOOST_SCAN_FAILED:
						{
							scanFailed = true;
							VRBoostStatus.VRBoost_Scanning = false;
							DismissPopup(VPT_VRBOOST_SCANNING);

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							//If we get here, then the VRBoost memory scanner came up with no good results :(
							VireioPopup popup(VPT_VRBOOST_FAILURE, VPS_ERROR, 5000);
							strcpy_s(popup.line[0], "VRBoost Memory Scan");
							strcpy_s(popup.line[1], "===================");
							strcpy_s(popup.line[2], "STATUS: FAILED");

							//Reason
							char *failReason = new char[256];
							ZeroMemory(failReason, 256);
							m_pVRboost_GetScanFailReason((char**)&failReason);
							sprintf_s(popup.line[3], "REASON: %s", failReason);
							delete []failReason;
							strcpy_s(popup.line[4], "VRBoost is now disabled");
							strcpy_s(popup.line[5], "Re-run the scan with NUMPAD5");
							ShowPopup(popup);
							VRBoostStatus.VRBoost_Active = false;
						}
						break;
					case VRBoost::VRBOOST_ADDRESSES_LOST:
						{
							scanFailed = true;
							VRBoostStatus.VRBoost_Scanning = false;
							DismissPopup(VPT_VRBOOST_SCANNING);

							//Enable mouse emulation whilst VRBoost is not active
							m_bForceMouseEmulation = true;
							tracker->setMouseEmulation(true);

							VireioPopup popup(VPT_VRBOOST_FAILURE, VPS_ERROR, 10000);
							strcpy_s(popup.line[0], "VRBoost");
							strcpy_s(popup.line[1], "=======");
							strcpy_s(popup.line[2], "STATUS: ADDRESSES LOST");
							strcpy_s(popup.line[4], "VRBoost is now disabled");
							strcpy_s(popup.line[5], "Re-run the scan with NUMPAD5");
							ShowPopup(popup);
							VRBoostStatus.VRBoost_Active = false;
						}
						break;
					case VRBoost::VRBOOST_OK:
						{
							scanFailed = false;
							DismissPopup(VPT_VRBOOST_SCANNING);
							if (VRBoostStatus.VRBoost_Scanning)
							{
								//Find which axes we have VRBoost capabilities for
								ActiveAxisInfo axes[30];
								memset(axes, 0xFF, sizeof(ActiveAxisInfo) * 30);
								UINT count = m_pVRboost_GetActiveRuleAxes((ActiveAxisInfo**)&axes);
								std::string axisNames;
								UINT i = 0;
								VRBoostStatus.VRBoost_HasOrientation = false;
								while (i < count)
								{
									if (axes[i].Axis == VRboostAxis::TrackerPitch)
										VRBoostStatus.VRBoost_HasOrientation = true;
									if (axes[i].Axis == MAXDWORD)
										break;
									axisNames += VRboostAxisString(axes[i].Axis) + " ";
									i++;
								}				

								//Only disable mouse emulation if we have orientation addresses
								if (VRBoostStatus.VRBoost_HasOrientation)
								{
									m_bForceMouseEmulation = false;
									tracker->setMouseEmulation(false);
								}


								VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 5000);
								strcpy_s(popup.line[0], "    VRBoost Memory Scan");
								strcpy_s(popup.line[1], "    ===================");
								strcpy_s(popup.line[2], "    STATUS:   SUCCESS");
								strcpy_s(popup.line[3], "    Found addresses: ");
								sprintf_s(popup.line[4], "       %s", axisNames.c_str());
								strcpy_s(popup.line[5], "    VRBoost is now active");
								ShowPopup(popup);
								//No longer scanning
								VRBoostStatus.VRBoost_Scanning = false;
							}
							VRBoostStatus.VRBoost_LoadRules = true;
							//As we've only just loaded the rules, we don't want to report an apply error this time round:
							VRBoostStatus.VRBoost_ApplyRules = true;
						}
						break;
					case VRBoost::VRBOOST_CANDIDATES:
						{
							scanFailed = false;
							DismissPopup(VPT_VRBOOST_SCANNING);
							if (VRBoostStatus.VRBoost_Scanning)
							{
								//Enable mouse emulation whilst VRBoost is not active
								m_bForceMouseEmulation = false;
								tracker->setMouseEmulation(false);

								VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 8000);
								strcpy_s(popup.line[0], "VRBoost Memory Scan");
								strcpy_s(popup.line[1], "===================");
								strcpy_s(popup.line[2], "STATUS: SUCCESS - MULTIPLE CANDIDATES");
								sprintf_s(popup.line[3], "Found %i candidate orientation addresses", m_pVRboost_GetScanCandidates());
								strcpy_s(popup.line[4], "Use NUMPAD4/NUMPAD6 to cycle through candidates");
								strcpy_s(popup.line[5], "VRBoost is now active");
								ShowPopup(popup);
								//No longer scanning
								VRBoostStatus.VRBoost_Scanning = false;
								VRBoostStatus.VRBoost_Candidates = true;
							}
							VRBoostStatus.VRBoost_LoadRules = true;
							//As we've only just loaded the rules, we don't want to report an apply error this time round:
							VRBoostStatus.VRBoost_ApplyRules = true;
						}
						break;
					}
				}
			}
		}
		else
		{
			VRBoostStatus.VRBoost_ApplyRules = true;
		}
	}
	else
	{
		VRBoostStatus.VRBoost_Active = false;
	}

	if (!m_bForceMouseEmulation && VRBoostStatus.VRBoost_Active && !VRBoostStatus.VRBoost_Scanning)
	{
		if (!VRBoostStatus.VRBoost_LoadRules)
		{
			VireioPopup popup(VPT_VRBOOST_FAILURE, VPS_ERROR);
			strcpy_s(popup.line[2], "VRBoost LoadRules Failed");
			strcpy_s(popup.line[3], "To Enable head tracking, turn on Force Mouse Emulation");
			strcpy_s(popup.line[4], "in VP Settings");
			ShowPopup(popup);
			return;
		}
		else if (!VRBoostStatus.VRBoost_ApplyRules)
		{
			VireioPopup popup(VPT_VRBOOST_FAILURE, VPS_ERROR);
			strcpy_s(popup.line[1], "VRBoost rules loaded but could not be applied");
			strcpy_s(popup.line[2], "Mouse Emulation is not Enabled,");
			strcpy_s(popup.line[3], "To Enable head tracking, turn on Force Mouse Emulation");
			strcpy_s(popup.line[4], "in VP Settings");
			ShowPopup(popup);
			return;
		}
	}

	if (!scanFailed)
	{
		//All is well
		DismissPopup(VPT_VRBOOST_FAILURE);
	}
}

/**
* Handles all updates if Present() is called in an extern swap chain.
***/
void D3DProxyDevice::HandleUpdateExtern()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called HandleUpdateExtern");
	#endif
	m_isFirstBeginSceneOfFrame = true;

	VPMENU_UpdateBorder();
}

/**
* Creates or restores class setup.
* Subclasses which override this method must call through to super method.
* Do not directly call this method in subclasses.
* This method should be used to re/create any resources that are held by the device proxy and deleted by Reset.
* 
* The only resources used like this are going to be extra resources that are used by the proxy and are not
* part of the actual calling application. 
* 
* Examples in D3DProxyDevice: The Font used in the VP overlay and the stereo buffer.
* 
* Example of something you wouldn't create here:
* Render targets in the m_activeRenderTargets collection. They need to be released to successfully Reset
* the device, but they just wrap IDirect3DSurface9 objects from the underlying application and will be
* re/created by the underlying application.
* 
* This method will be called when the proxy device is initialised with Init (happens before device is
* passed back to actual application by CreateDevice) and after a successful device Reset.
***/
void D3DProxyDevice::OnCreateOrRestore()
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called OnCreateOrRestore");
	#endif
	m_currentRenderingSide = vireio::Left;
	if(!m_b2dDepthMode)
		m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix();
	m_pCurrentView = &m_leftView;
	m_pCurrentProjection = &m_leftProjection;

	// Wrap the swap chain
	IDirect3DSwapChain9* pActualPrimarySwapChain;
	if (FAILED(BaseDirect3DDevice9::GetSwapChain(0, &pActualPrimarySwapChain))) {
		OutputDebugString("Failed to fetch swapchain.\n");
		exit(1); 
	}

	assert (m_activeSwapChains.size() == 0);
	m_activeSwapChains.push_back(new D3D9ProxySwapChain(pActualPrimarySwapChain, this, false));
	assert (m_activeSwapChains.size() == 1);

	// Set the primary rendertarget to the first stereo backbuffer
	IDirect3DSurface9* pWrappedBackBuffer;
	m_activeSwapChains[0]->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pWrappedBackBuffer);
	SetRenderTarget(0, pWrappedBackBuffer);

	pWrappedBackBuffer->Release();
	pWrappedBackBuffer = NULL;


	BaseDirect3DDevice9::GetViewport(&m_LastViewportSet);

	// If there is an initial depth stencil
	IDirect3DSurface9* pDepthStencil;
	if (SUCCEEDED(BaseDirect3DDevice9::GetDepthStencilSurface(&pDepthStencil))) { 

		D3DSURFACE_DESC stencilDesc;
		pDepthStencil->GetDesc(&stencilDesc);
		pDepthStencil->Release();

		IDirect3DSurface9* pTemp = NULL;
		CreateDepthStencilSurface(stencilDesc.Width, stencilDesc.Height, stencilDesc.Format, stencilDesc.MultiSampleType, stencilDesc.MultiSampleQuality, false, &pTemp, NULL);
		SetDepthStencilSurface(pTemp);
		pTemp->Release();	
	}

	SetupHUD();

	stereoView->Init(getActual());

	m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);
	m_spShaderViewAdjustment->ComputeViewTransforms();

	// set VP main values
	viewportWidth = stereoView->viewport.Width;
	viewportHeight = stereoView->viewport.Height;

	menuTop = viewportHeight*0.32f;
	menuEntryHeight = viewportHeight*0.037f;

	fScaleX = ((float)viewportWidth / (float)VPMENU_PIXEL_WIDTH);
	fScaleY = ((float)viewportHeight / (float)VPMENU_PIXEL_HEIGHT);

	menuHelperRect.left = 0;
	menuHelperRect.right = VPMENU_PIXEL_WIDTH;
	menuHelperRect.top = 0;
	menuHelperRect.bottom = VPMENU_PIXEL_HEIGHT;
}

/**
* Switches rendering to which ever side is specified by side.
* Use to specify the side that you want to draw to.
* Overriding classes should call the base implementation first and then makes any extra needed changes
* based on the result of the base implementation (if the base class doesn't change side then derived shouldn't 
* change either)
* 
* @return True if change succeeded, false if it fails. The switch will fail if you attempt to setDrawingSide(Right)
* when the current primary active render target (target 0  in m_activeRenderTargets) is not stereo.
* Attempting to switch to a side when that side is already the active side will return true without making any changes.
***/
bool D3DProxyDevice::setDrawingSide(vireio::RenderPosition side)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetDrawingSide");
	#endif
	// Already on the correct eye
	if (side == m_currentRenderingSide) {
		return true;
	}

	// should never try and render for the right eye if there is no render target for the main render targets right side
	if (!m_activeRenderTargets[0]->IsStereo() && (side == vireio::Right)) {
		return false;
	}

	// Everything hasn't changed yet but we set this first so we don't accidentally use the member instead of the local and break
	// things, as I have already managed twice.
	m_currentRenderingSide = side;

	// switch render targets to new side
	bool renderTargetChanged = false;
	HRESULT result;
	D3D9ProxySurface* pCurrentRT;
	for(std::vector<D3D9ProxySurface*>::size_type i = 0; i != m_activeRenderTargets.size(); i++) 
	{
		if ((pCurrentRT = m_activeRenderTargets[i]) != NULL) {

			if (side == vireio::Left) 
				result = BaseDirect3DDevice9::SetRenderTarget(i, pCurrentRT->getActualLeft()); 
			else 
				result = BaseDirect3DDevice9::SetRenderTarget(i, pCurrentRT->getActualRight());

			if (result != D3D_OK) {
				OutputDebugString("Error trying to set one of the Render Targets while switching between active eyes for drawing.\n");
			}
			else {
				renderTargetChanged = true;
			}
		}
	}

	// if a non-fullsurface viewport is active and a rendertarget changed we need to reapply the viewport
	if (renderTargetChanged && !m_bActiveViewportIsDefault) {
		BaseDirect3DDevice9::SetViewport(&m_LastViewportSet);
	}

	if (m_bViewportIsSquished)
		SetGUIViewport();

	// switch depth stencil to new side
	if (m_pActiveStereoDepthStencil != NULL) { 
		if (side == vireio::Left) 
			result = BaseDirect3DDevice9::SetDepthStencilSurface(m_pActiveStereoDepthStencil->getActualLeft()); 
		else 
			result = BaseDirect3DDevice9::SetDepthStencilSurface(m_pActiveStereoDepthStencil->getActualRight());
	}

	// switch textures to new side
	IDirect3DBaseTexture9* pActualLeftTexture = NULL;
	IDirect3DBaseTexture9* pActualRightTexture = NULL;

	for(auto it = m_activeTextureStages.begin(); it != m_activeTextureStages.end(); ++it )
	{
		if (it->second) {
			pActualLeftTexture = NULL;
			pActualRightTexture = NULL;
			vireio::UnWrapTexture(it->second, &pActualLeftTexture, &pActualRightTexture);

			// if stereo texture
			if (pActualRightTexture != NULL) { 
				if (side == vireio::Left) 
					result = BaseDirect3DDevice9::SetTexture(it->first, pActualLeftTexture); 
				else 
					result = BaseDirect3DDevice9::SetTexture(it->first, pActualRightTexture);
			}
			// else the texture is mono and doesn't need changing. It will always be set initially and then won't need changing

			if (result != D3D_OK)
				OutputDebugString("Error trying to set one of the textures while switching between active eyes for drawing.\n");
		}
	}

	// update view transform for new side 
	if (m_bViewTransformSet) {

		if (side == vireio::Left) {
			m_pCurrentView = &m_leftView;
		}
		else {
			m_pCurrentView = &m_rightView;
		}

		BaseDirect3DDevice9::SetTransform(D3DTS_VIEW, m_pCurrentView);
	}

	// update projection transform for new side 
	if (m_bProjectionTransformSet) {

		if (side == vireio::Left) {
			m_pCurrentProjection = &m_leftProjection;
		}
		else {
			m_pCurrentProjection = &m_rightProjection;
		}

		BaseDirect3DDevice9::SetTransform(D3DTS_PROJECTION, m_pCurrentProjection);
	}

	// Updated computed view translation (used by several derived proxies - see: ComputeViewTranslation)
	if (side == vireio::Left) {
		m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->LeftAdjustmentMatrix();
	}
	else {
		m_pCurrentMatViewTransform = &m_spShaderViewAdjustment->RightAdjustmentMatrix();
	}

	// Apply active stereo shader constants
	m_spManagedShaderRegisters->ApplyAllStereoConstants(side);

	return true;
}

/**
* Try and toggle to other drawing side. 
* @return False if changes fails due to the current render target being mono.
***/
bool D3DProxyDevice::switchDrawingSide()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SwitchDrawingSide");
	#endif
	bool switched = false;
	
	if (m_currentRenderingSide == vireio::Left) {
		switched = setDrawingSide(vireio::Right);
	}
	else if (m_currentRenderingSide == vireio::Right) {
		switched = setDrawingSide(vireio::Left);
	}
	else {
		DebugBreak();
	}

	return switched;
}

/**
* Adds a default shader rule to the game configuration.
* @return True if rule was added, false if rule already present.
***/
bool D3DProxyDevice::addRule(std::string constantName, bool allowPartialNameMatch, UINT startRegIndex, D3DXPARAMETER_CLASS constantType, UINT operationToApply, bool transpose)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called AddRule");
	#endif
	return m_pGameHandler->AddRule(m_spShaderViewAdjustment, constantName, allowPartialNameMatch, startRegIndex, constantType, operationToApply, transpose);
}

/**
* Adds a default shader rule to the game configuration.
* @return True if rule was added, false if rule already present.
***/
bool D3DProxyDevice::modifyRule(std::string constantName, UINT operationToApply, bool transpose)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ModifyRule");
	#endif
	return m_pGameHandler->ModifyRule(m_spShaderViewAdjustment, constantName, operationToApply, transpose);
}

/**
* Delete rule.
* @return True if rule was deleted, false if rule not present.
***/
bool D3DProxyDevice::deleteRule(std::string constantName)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DeleteRule");
	#endif
	return m_pGameHandler->DeleteRule(m_spShaderViewAdjustment, constantName);
}

/**
* Saves current game shader rules (and game configuration).
***/
void D3DProxyDevice::saveShaderRules()
{ 
	#ifdef SHOW_CALLS
		OutputDebugString("called SaveShaderRules");
	#endif
	m_pGameHandler->Save(config, m_spShaderViewAdjustment);

	ProxyHelper* helper = new ProxyHelper();
	helper->SaveConfig(config);
	delete helper;
}

/**
* Simple helper to clear a rectangle using the specified color.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
***/
void D3DProxyDevice::ClearRect(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ClearRect");
	#endif
	setDrawingSide(renderPosition);
	BaseDirect3DDevice9::Clear(1, &rect, D3DCLEAR_TARGET, color, 0, 0);
}

/**
* Simple helper to clear an empty rectangle or border using the specified color.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param bw The border width.
***/
void D3DProxyDevice::ClearEmptyRect(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, int bw)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ClearEMptyRect");
	#endif
	// helper rectangle
	D3DRECT rect0 = D3DRECT(rect);

	setDrawingSide(renderPosition);

	rect0.y2 = rect.y1 + bw;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.y1 = rect.y2 - bw;
	rect0.y2 = rect.y2;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.y1 = rect.y1;
	rect0.x2 = rect.x1 + bw;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);

	rect0.x1 = rect.x2 - bw;
	rect0.x2 = rect.x2;
	BaseDirect3DDevice9::Clear(1, &rect0, D3DCLEAR_TARGET, color, 0, 0);
}

/**
* Draws a simple selection control.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param selectionIndex The index of the currently chosen selection.
* @param selectionRange The range of the selection.
***/
void D3DProxyDevice::DrawSelection(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, int selectionIndex, int selectionRange)
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawSelection");
	#endif
	// get width of each selection
	float selectionWidth = (rect.x2-rect.x1) / (float)selectionRange;

	// get secondary color
	D3DXCOLOR color2 = D3DXCOLOR(color);
	FLOAT red = color2.r;
	color2.r = color2.g * 0.7f;
	color2.g = red;

	for (int i = 0; i < selectionRange; i++)
	{
		rect.x2 = rect.x1+(int)selectionWidth;
		if (i==selectionIndex)
			ClearRect(renderPosition, rect, color);
		else
			ClearRect(renderPosition, rect, color2);
		rect.x1+=(int)selectionWidth;
	}
}

/**
* Draws a simple selection control.
* @param renderPosition Left or Right render target to be used.
* @param rect The rectangle in pixel space to be cleared.
* @param color The direct 3d color to be used.
* @param selectionIndex The index of the currently chosen selection.
* @param selectionRange The range of the selection.
***/
void D3DProxyDevice::DrawScrollbar(vireio::RenderPosition renderPosition, D3DRECT rect, D3DCOLOR color, float scroll, int scrollbarSize)
{	
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawScrollbar");
	#endif
	if (scroll<0.0f) scroll=0.0f;
	if (scroll>1.0f) scroll=1.0f;

	// get width of each selection
	int scrollHeight = rect.y2-rect.y1-scrollbarSize;
	scrollHeight = (int)(scrollHeight*scroll);

	// get secondary color
	D3DXCOLOR color2 = D3DXCOLOR(color);
	FLOAT red = color2.r;
	color2.r = color2.g * 0.7f;
	color2.g = red;

	ClearRect(renderPosition, rect, color2);
	rect.y1 += scrollHeight;
	rect.y2 = rect.y1+scrollbarSize;
	ClearRect(renderPosition, rect, color);
}

/**
* Draws a text with a dark shadow.
* @see DrawText()
***/
void D3DProxyDevice::DrawTextShadowed(ID3DXFont* font, LPD3DXSPRITE sprite, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format, D3DCOLOR color)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called DrawTextShadowed");
	#endif
	
	if (lprc->top < 0 || lprc->top > viewportHeight)
		return;

	lprc->left+=2; lprc->right+=2; lprc->top+=2; lprc->bottom+=2;
	font->DrawText(sprite, lpchText, -1, lprc, format, D3DCOLOR_ARGB(255, 64, 64, 64));
	lprc->left-=2; lprc->right-=2; lprc->top-=2; lprc->bottom-=2;
	font->DrawText(sprite, lpchText, -1, lprc, format, color);
}

/**
* Changes the HUD scale mode - also changes new scale in view adjustment class.
***/
void D3DProxyDevice::ChangeHUD3DDepthMode(HUD_3D_Depth_Modes newMode)
{
#ifdef SHOW_CALLS
	OutputDebugString("called ChangeHUD3DDepthMode");
#endif
	if (newMode >= HUD_3D_Depth_Modes::HUD_ENUM_RANGE)
		return;

	hud3DDepthMode = newMode;

	m_spShaderViewAdjustment->ChangeHUDDistance(hudDistancePresets[(int)newMode]);
	m_spShaderViewAdjustment->ChangeHUD3DDepth(hud3DDepthPresets[(int)newMode]);
}

/**
* Changes the GUI scale mode - also changes new scale in view adjustment class.
***/
void D3DProxyDevice::ChangeGUI3DDepthMode(GUI_3D_Depth_Modes newMode)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ChangeGUI3DDepthMode");
	#endif
	if (newMode >= GUI_3D_Depth_Modes::GUI_ENUM_RANGE)
		return;

	gui3DDepthMode = newMode;

	m_spShaderViewAdjustment->ChangeGUISquash(guiSquishPresets[(int)newMode]);
	m_spShaderViewAdjustment->ChangeGUI3DDepth(gui3DDepthPresets[(int)newMode]);
}


void D3DProxyDevice::DuckAndCoverCalibrate()
{
	switch (m_DuckAndCover.dfcStatus)
	{
	case DAC_CAL_STANDING:
		{
			VireioPopup popup(VPT_NOTIFICATION, VPS_INFO);
			strcpy_s(popup.line[0], "Duck-and-Cover Mode");
			strcpy_s(popup.line[1], "===================");
			strcpy_s(popup.line[2], "Step 1:");
			strcpy_s(popup.line[3], " - Move to the standing position you will be playing in");
			strcpy_s(popup.line[4], " - Push A on the Xbox 360 controller");
			strcpy_s(popup.line[5], "      or Right Shift");
			ShowPopup(popup);
		}
		break;
	case DAC_CAL_CROUCHING:
		{
			DismissPopup(VPT_NOTIFICATION);
			VireioPopup popup(VPT_NOTIFICATION, VPS_INFO);
			strcpy_s(popup.line[0], "Duck-and-Cover Mode");
			strcpy_s(popup.line[1], "===================");
			strcpy_s(popup.line[2], "Step 2:");
			strcpy_s(popup.line[3], " - Move to a crouching position");
			strcpy_s(popup.line[4], " - Push A on the Xbox 360 controller");
			strcpy_s(popup.line[5], "      or Right Shift");
			ShowPopup(popup);
		}
		break;
	case DAC_CAL_PRONE:
		{
			DismissPopup(VPT_NOTIFICATION);
			VireioPopup popup(VPT_NOTIFICATION, VPS_INFO);
			strcpy_s(popup.line[0], "Duck-and-Cover Mode");
			strcpy_s(popup.line[1], "===================");
			strcpy_s(popup.line[2], "Step 3 (optional):");
			strcpy_s(popup.line[3], " - Move to a prone position");
			strcpy_s(popup.line[4], "    - Push A button on the Xbox360 controller");
			strcpy_s(popup.line[5], "       or Right Shift");
			strcpy_s(popup.line[6], "    - TO SKIP: Push B button on the controller or Escape key");
			ShowPopup(popup);
		}
		break;
	case DAC_CAL_COMPLETE:
		{
			DismissPopup(VPT_NOTIFICATION);
			VireioPopup popup(VPT_NOTIFICATION, VPS_INFO);
			strcpy_s(popup.line[0], "Duck-and-Cover Mode");
			strcpy_s(popup.line[1], "===================");
			strcpy_s(popup.line[2], "Step 4:");
			strcpy_s(popup.line[3], " - Calibration is complete");
			strcpy_s(popup.line[3], " - Return to the standing position you will be playing in");
			strcpy_s(popup.line[4], " - Push A on the Xbox 360 controller");
			strcpy_s(popup.line[5], "      or Right Shift");
			ShowPopup(popup);
		}
		break;
	}
}


//FPS Calculator

#define MAXSAMPLES 100

float D3DProxyDevice::CalcFPS()
{
	static bool init=false;
	static int tickindex=0;
	static LONGLONG ticksum=0;
	static LONGLONG ticklist[MAXSAMPLES];
	static LONGLONG prevTick;
	static LARGE_INTEGER perffreq;
	if (!init)
	{
		//Initialise - should only ever happen once
		memset(ticklist, 0, sizeof(LONGLONG) * MAXSAMPLES);
		QueryPerformanceFrequency(&perffreq);
		init=true;
	}

	//Get the new tick
	LARGE_INTEGER newtick;
	QueryPerformanceCounter(&newtick);
	
	ticksum -= ticklist[tickindex];
    ticksum += newtick.QuadPart - prevTick;
    ticklist[tickindex] = newtick.QuadPart - prevTick;
    tickindex = ++tickindex % MAXSAMPLES;
	prevTick = newtick.QuadPart;

	float FPS = (float)((double)MAXSAMPLES / ((double)ticksum / (double)perffreq.QuadPart));

    return FPS;
}


/**
* Releases HUD font, shader registers, render targets, texture stages, vertex buffers, depth stencils, indices, shaders, declarations.
***/
void D3DProxyDevice::ReleaseEverything()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called ReleaseEverything");
	#endif
	// Fonts and any other D3DX interfaces should be released first.
	// They frequently hold stateblocks which are holding further references to other resources.
	if(hudFont) {
		hudFont->Release();
		hudFont = NULL;

		for (int fontSize = 0; fontSize < 27; ++fontSize)
		{
			popupFont[fontSize]->Release();
			popupFont[fontSize] = NULL;
		}
	}

	if (hudMainMenu)
	{
		hudMainMenu->Release();
		hudMainMenu = NULL;
	}

	if (hudTextBox)
	{
		hudTextBox->Release();
		hudTextBox = NULL;
	}

	m_spManagedShaderRegisters->ReleaseResources();

	if (m_pCapturingStateTo) {
		m_pCapturingStateTo->Release();
		m_pCapturingStateTo = NULL;
	}

	// one of these will still have a count of 1 until the backbuffer is released
	for(std::vector<D3D9ProxySurface*>::size_type i = 0; i != m_activeRenderTargets.size(); i++) 
	{
		if (m_activeRenderTargets[i] != NULL) {
			m_activeRenderTargets[i]->Release();
			m_activeRenderTargets[i] = NULL;
		}
	} 


	auto it = m_activeTextureStages.begin();
	while (it != m_activeTextureStages.end()) {
		if (it->second)
			it->second->Release();

		it = m_activeTextureStages.erase(it);
	}


	auto itVB = m_activeVertexBuffers.begin();
	while (itVB != m_activeVertexBuffers.end()) {
		if (itVB->second)
			itVB->second->Release();

		itVB = m_activeVertexBuffers.erase(itVB);
	}




	if (m_pActiveStereoDepthStencil) {
		m_pActiveStereoDepthStencil->Release();
		m_pActiveStereoDepthStencil = NULL;
	}

	if (m_pActiveIndicies) {
		m_pActiveIndicies->Release();
		m_pActiveIndicies = NULL;
	}

	if (m_pActivePixelShader) {
		m_pActivePixelShader->Release();
		m_pActivePixelShader = NULL;
	}

	if (m_pActiveVertexShader) {
		m_pActiveVertexShader->Release();
		m_pActiveVertexShader = NULL;
	}

	if (m_pActiveVertexDeclaration) {
		m_pActiveVertexDeclaration->Release();
		m_pActiveVertexDeclaration = NULL;
	}
}
/**
* Comparison made against active primary render target.
*
***/
bool D3DProxyDevice::isViewportDefaultForMainRT(CONST D3DVIEWPORT9* pViewport)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called isViewportDefaultForMainRT");
	#endif
	D3D9ProxySurface* pPrimaryRenderTarget = m_activeRenderTargets[0];
	D3DSURFACE_DESC pRTDesc;
	pPrimaryRenderTarget->GetDesc(&pRTDesc);

	return  ((pViewport->Height == pRTDesc.Height) && (pViewport->Width == pRTDesc.Width) &&
		(pViewport->MinZ <= SMALL_FLOAT) && (pViewport->MaxZ >= SLIGHTLY_LESS_THAN_ONE));
}

/**
* Stores and sets view transform calling SetTransform() accordingly to current render side.
* @param pLeftMatrix The left view matrix.
* @param pRightMatrix The right view matrix.
* @param apply True to apply calling SetTransform()
* @see BaseDirect3DDevice9::SetTransform()
***/
HRESULT D3DProxyDevice::SetStereoViewTransform(D3DXMATRIX pLeftMatrix, D3DXMATRIX pRightMatrix, bool apply)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetStereoViewTransform");
	#endif
	if (D3DXMatrixIsIdentity(&pLeftMatrix) && D3DXMatrixIsIdentity(&pRightMatrix)) {
		m_bViewTransformSet = false;
	}
	else {
		m_bViewTransformSet = true;
	}

	m_leftView = pLeftMatrix;
	m_rightView = pRightMatrix;

	if (m_currentRenderingSide == vireio::Left) {
		m_pCurrentView = &m_leftView;
	}
	else {
		m_pCurrentView = &m_rightView;
	}

	if (apply)
		return BaseDirect3DDevice9::SetTransform(D3DTS_VIEW, m_pCurrentView);
	else
		return D3D_OK;
}

/**
* Stores and sets projection transform calling SetTransform() accordingly to current render side.
* @param pLeftMatrix The left view matrix.
* @param pRightMatrix The right view matrix.
* @param apply True to apply calling SetTransform()
* @see BaseDirect3DDevice9::SetTransform()
***/
HRESULT D3DProxyDevice::SetStereoProjectionTransform(D3DXMATRIX pLeftMatrix, D3DXMATRIX pRightMatrix, bool apply)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetStereoProjectionTransform");
	#endif
	if (D3DXMatrixIsIdentity(&pLeftMatrix) && D3DXMatrixIsIdentity(&pRightMatrix)) {
		m_bProjectionTransformSet = false;
	}
	else {
		m_bProjectionTransformSet = true;
	}

	m_leftProjection = pLeftMatrix;
	m_rightProjection = pRightMatrix;

	if (m_currentRenderingSide == vireio::Left) {
		m_pCurrentProjection = &m_leftProjection;
	}
	else {
		m_pCurrentProjection = &m_rightProjection;
	}

	if (apply)
		return BaseDirect3DDevice9::SetTransform(D3DTS_PROJECTION, m_pCurrentProjection);
	else
		return D3D_OK;
}

/**
* Sets the viewport to squish the GUI accordingly.
***/
void D3DProxyDevice::SetGUIViewport()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called SetGUIViewport");
	#endif
	
	// do not squish the viewport in case vp menu is open - GBCODE Why?
	//if ((VPMENU_mode>=VPMENU_Modes::MAINMENU) && (VPMENU_mode<VPMENU_Modes::VPMENU_ENUM_RANGE))
	//	return;

	D3DXMATRIX mLeftShift;
	D3DXMATRIX mRightShift;

	// set shift by current gui 3d depth
	float shiftInPixels = gui3DDepthPresets[gui3DDepthMode];
	D3DXMatrixTranslation(&mLeftShift, -shiftInPixels, 0, 0);
	D3DXMatrixTranslation(&mRightShift, shiftInPixels, 0, 0);

	// get matrix
	D3DXMATRIX mVPSquash = mLeftShift * m_spShaderViewAdjustment->Squash();
	if (m_currentRenderingSide != vireio::Left)
		mVPSquash = mRightShift * m_spShaderViewAdjustment->Squash();

	// get viewport
	BaseDirect3DDevice9::GetViewport(&m_ViewportIfSquished);

	// get screen center and translate it
	float centerX = (((FLOAT)stereoView->viewport.Width-(FLOAT)stereoView->viewport.X)/2.0f);
	float centerY = (((FLOAT)stereoView->viewport.Height-(FLOAT)stereoView->viewport.Y)/2.0f);

	// get left/top viewport sides
	D3DXVECTOR3 vIn = D3DXVECTOR3((FLOAT)stereoView->viewport.X-centerX, (FLOAT)stereoView->viewport.Y-centerY,1);
	D3DXVECTOR4 vOut = D3DXVECTOR4();
	D3DXVec3Transform(&vOut,&vIn, &mVPSquash);
	float floatMultiplier = 4;
	int originalX = (int)(vOut.x+centerX);
	int originalY = (int)(vOut.y+centerY);
	if(m_bfloatingMenu && (tracker->getStatus() >= MTS_OK))
	{
		/*char buf[64];
		LPCSTR psz = NULL;
		sprintf_s(buf, "yaw: %f, pitch: %f\n", tracker->primaryYaw, tracker->primaryPitch);
		psz = buf;*/		
		m_ViewportIfSquished.X = (int)(vOut.x+centerX-(((m_fFloatingYaw - tracker->primaryYaw) * floatMultiplier) * (180 / PI)));
		m_ViewportIfSquished.Y = (int)(vOut.y+centerY-(((m_fFloatingPitch - tracker->primaryPitch) * floatMultiplier) * (180 / PI)));
	}
	else
	{
		m_ViewportIfSquished.X = (int)(vOut.x+centerX);
		m_ViewportIfSquished.Y = (int)(vOut.y+centerY);
	}

	// get right/bottom viewport sides
	vIn = D3DXVECTOR3((FLOAT)(stereoView->viewport.Width+stereoView->viewport.X)-centerX, (FLOAT)(stereoView->viewport.Height+stereoView->viewport.Y)-centerY,1);
	vOut = D3DXVECTOR4();
	D3DXVec3Transform(&vOut,&vIn, &mVPSquash);
	m_ViewportIfSquished.Width = (int)(vOut.x+centerX) - originalX;
	m_ViewportIfSquished.Height = (int)(vOut.y+centerY) - originalY;

	// set viewport
	m_bViewportIsSquished = true;
	BaseDirect3DDevice9::SetViewport(&m_ViewportIfSquished);
}

/**
* Rounds the floats to make them more display friendly
**/
float D3DProxyDevice::RoundVireioValue(float val)
{
	#ifdef SHOW_CALLS
		OutputDebugString("called RoundVireioValue");
	#endif
	return (float)floor(val * 1000.0f + 0.5f) / 1000.0f;
}

bool D3DProxyDevice::InitVRBoost()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called InitVRBoost");
	#endif
	bool initSuccess = false;
	OutputDebugString("Try to init VR Boost\n");

#ifdef x64
	// explicit VRboost dll import
	hmVRboost = LoadLibrary("VRboost64.dll");
#else
	// explicit VRboost dll import
	hmVRboost = LoadLibrary("VRboost.dll");
#endif

	VRBoostStatus.VRBoost_Active = false;
	VRBoostStatus.VRBoost_LoadRules = false;
	VRBoostStatus.VRBoost_ApplyRules = false;
	VRBoostStatus.VRBoost_Scanning = false;
	VRBoostStatus.VRBoost_Candidates = false;
	//Assume VRBoost will have orientation (it probably will)
	VRBoostStatus.VRBoost_HasOrientation = true;

	// get VRboost methods
	if (hmVRboost != NULL)
	{
		OutputDebugString("VR Boost Loaded\n");
		// get methods explicit
		m_pVRboost_LoadMemoryRules = (LPVRBOOST_LoadMemoryRules)GetProcAddress(hmVRboost, "VRboost_LoadMemoryRules");
		m_pVRboost_SaveMemoryRules = (LPVRBOOST_SaveMemoryRules)GetProcAddress(hmVRboost, "VRboost_SaveMemoryRules");
		m_pVRboost_CreateFloatMemoryRule = (LPVRBOOST_CreateFloatMemoryRule)GetProcAddress(hmVRboost, "VRboost_CreateFloatMemoryRule");
		m_pVRboost_SetProcess = (LPVRBOOST_SetProcess)GetProcAddress(hmVRboost, "VRboost_SetProcess");
		m_pVRboost_ReleaseAllMemoryRules = (LPVRBOOST_ReleaseAllMemoryRules)GetProcAddress(hmVRboost, "VRboost_ReleaseAllMemoryRules");
		m_pVRboost_ApplyMemoryRules = (LPVRBOOST_ApplyMemoryRules)GetProcAddress(hmVRboost, "VRboost_ApplyMemoryRules");
		m_pVRboost_GetActiveRuleAxes = (LPVRBOOST_GetActiveRuleAxes)GetProcAddress(hmVRboost, "VRboost_GetActiveRuleAxes");
		m_pVRboost_StartMemoryScan = (LPVRBOOST_StartMemoryScan)GetProcAddress(hmVRboost, "VRboost_StartMemoryScan");
		m_pVRboost_GetScanInitPercent = (LPVRBOOST_GetScanInitPercent)GetProcAddress(hmVRboost, "VRboost_GetScanInitPercent");
		m_pVRboost_GetScanFailReason = (LPVRBOOST_GetScanFailReason)GetProcAddress(hmVRboost, "VRboost_GetScanFailReason");
		m_pVRboost_SetNextScanCandidate = (LPVRBOOST_SetNextScanCandidate)GetProcAddress(hmVRboost, "VRboost_SetNextScanCandidate");
		m_pVRboost_GetScanCandidates = (LPVRBOOST_GetScanCandidates)GetProcAddress(hmVRboost, "VRboost_GetScanCandidates");
		m_pVRboost_GetScanAssist = (LPVRBOOST_GetScanAssist)GetProcAddress(hmVRboost, "VRboost_GetScanAssist");
		if ((!m_pVRboost_LoadMemoryRules) || 
			(!m_pVRboost_SaveMemoryRules) || 
			(!m_pVRboost_CreateFloatMemoryRule) || 
			(!m_pVRboost_SetProcess) || 
			(!m_pVRboost_ReleaseAllMemoryRules) || 
			(!m_pVRboost_ApplyMemoryRules) ||
			(!m_pVRboost_GetActiveRuleAxes) ||
			(!m_pVRboost_StartMemoryScan) ||
			(!m_pVRboost_GetScanInitPercent) ||
			(!m_pVRboost_GetScanFailReason) ||
			(!m_pVRboost_SetNextScanCandidate) ||
			(!m_pVRboost_GetScanCandidates) ||
			(!m_pVRboost_GetScanAssist))
		{
			hmVRboost = NULL;
			m_bForceMouseEmulation = false;
			FreeLibrary(hmVRboost);
			OutputDebugString("FAILED loading VRboost methods:");
			if (!m_pVRboost_LoadMemoryRules) OutputDebugString("m_pVRboost_LoadMemoryRules");
			if (!m_pVRboost_SaveMemoryRules) OutputDebugString("m_pVRboost_SaveMemoryRules");
			if (!m_pVRboost_CreateFloatMemoryRule) OutputDebugString("m_pVRboost_CreateFloatMemoryRule");
			if (!m_pVRboost_SetProcess) OutputDebugString("m_pVRboost_SetProcess");
			if (!m_pVRboost_ReleaseAllMemoryRules) OutputDebugString("m_pVRboost_ReleaseAllMemoryRules");
			if (!m_pVRboost_ApplyMemoryRules) OutputDebugString("m_pVRboost_ApplyMemoryRules");
			if (!m_pVRboost_GetActiveRuleAxes) OutputDebugString("m_pVRboost_GetActiveRuleAxes");
			if (!m_pVRboost_StartMemoryScan) OutputDebugString("m_pVRboost_StartMemoryScan");
			if (!m_pVRboost_GetScanInitPercent) OutputDebugString("m_pVRboost_GetScanInitPercent");
			if (!m_pVRboost_GetScanFailReason) OutputDebugString("m_pVRboost_GetScanFailReason");
			if (!m_pVRboost_SetNextScanCandidate) OutputDebugString("m_pVRboost_SetNextScanCandidate");
			if (!m_pVRboost_GetScanCandidates) OutputDebugString("m_pVRboost_GetScanCandidates");
			if (!m_pVRboost_GetScanAssist) OutputDebugString("m_pVRboost_GetScanAssist");
		}
		else
		{
			initSuccess = true;
			m_bForceMouseEmulation = true;
			VRBoostStatus.VRBoost_Active = true;
			OutputDebugString("Success loading VRboost methods.");
		}

		m_VRboostRulesPresent = false;
		m_VertexShaderCount = 0;
		m_VertexShaderCountLastFrame = 0;

		// set common default VRBoost values
		ZeroMemory(&VRBoostValue[0], MAX_VRBOOST_VALUES*sizeof(float));
		VRBoostValue[VRboostAxis::Zero] = 0.0f;
		VRBoostValue[VRboostAxis::One] = 1.0f;
		VRBoostValue[VRboostAxis::WorldFOV] = 95.0f;
		VRBoostValue[VRboostAxis::PlayerFOV] = 125.0f;
		VRBoostValue[VRboostAxis::FarPlaneFOV] = 95.0f;
	}
	else
	{
		initSuccess = false;
	}
	return initSuccess;
}

/*
  * Initializes the tracker, setting the tracker initialized status.
  * @return true if tracker was initialized, false otherwise
  */
 bool D3DProxyDevice::InitTracker()
 {
	#ifdef SHOW_CALLS
		 OutputDebugString("called InitTracker");
	#endif
 	// VRboost rules present ?
 	if (config.VRboostPath != "") m_VRboostRulesPresent = true; else m_VRboostRulesPresent = false;
 
 	OutputDebugString("GB - Try to init Tracker\n");
 	tracker.reset(MotionTrackerFactory::Get(config));
	if (tracker && tracker->getStatus() >= MTS_OK)
 	{
		OutputDebugString("Tracker Got\n");
 		OutputDebugString("Setting Multipliers\n");
		tracker->setMultipliers(config.yaw_multiplier, config.pitch_multiplier, config.roll_multiplier);
 		OutputDebugString("Setting Mouse EMu\n");
		tracker->setMouseEmulation((!m_VRboostRulesPresent) || (hmVRboost==NULL));

		//Set the default timewarp prediction behaviour for this game - this will have no effect on non-Oculus trackers
		tracker->useSDKPosePrediction = config.useSDKPosePrediction;

		//Only advise calibration for positional tracking on DK2
		if (tracker->SupportsPositionTracking())
			calibrate_tracker = true;

		return true;
 	}

 	return false;
 }
