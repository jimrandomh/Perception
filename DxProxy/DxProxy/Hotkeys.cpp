/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <Hotkeys.cpp> and
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

#ifdef x64
#define PR_SIZET "I64"
#else
#define PR_SIZET ""
#endif


using namespace VRBoost;

/**
* Keyboard input handling
***/
void D3DProxyDevice::HandleControls()
{
	#ifdef SHOW_CALLS
		OutputDebugString("called HandleControls");
	#endif
	controls.UpdateXInputs();

	// loop through hotkeys
	bool hotkeyPressed = false;
	for (int i = 0; i < 5; i++)
	{
		if ((controls.Key_Down(hudHotkeys[i])) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (i==0)
			{
				HUD_3D_Depth_Modes newMode=(HUD_3D_Depth_Modes)(hud3DDepthMode+1);
				if (newMode>=HUD_3D_Depth_Modes::HUD_ENUM_RANGE)
					newMode=HUD_3D_Depth_Modes::HUD_DEFAULT;
				{
					oldHudMode = hud3DDepthMode;
					ChangeHUD3DDepthMode(newMode);

				}
			}
			else
			{
				if (hud3DDepthMode==(HUD_3D_Depth_Modes)(i-1))
				{
					if (controls.Key_Down(VK_RCONTROL))
					{
						oldHudMode = hud3DDepthMode;
						ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(i-1));
					}
					else
					{
						ChangeHUD3DDepthMode(oldHudMode);
					}

				}
				else
				{
					oldHudMode = hud3DDepthMode;
					ChangeHUD3DDepthMode((HUD_3D_Depth_Modes)(i-1));
				}
			}
			hotkeyPressed = true;
		}
		if ((controls.Key_Down(guiHotkeys[i])) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (i==0)
			{
				GUI_3D_Depth_Modes newMode=(GUI_3D_Depth_Modes)(gui3DDepthMode+1);
				if (newMode>=GUI_3D_Depth_Modes::GUI_ENUM_RANGE)
					newMode=GUI_3D_Depth_Modes::GUI_DEFAULT;
				{
					oldGuiMode = gui3DDepthMode;
					ChangeGUI3DDepthMode(newMode);
				}
			}
			else
			{
				if (gui3DDepthMode==(GUI_3D_Depth_Modes)(i-1))
				{
					if (controls.Key_Down(VK_RCONTROL))
					{
						oldGuiMode = gui3DDepthMode;
						ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(i-1));
					}
					else
					{
						ChangeGUI3DDepthMode(oldGuiMode);
					}

				}
				else
				{
					oldGuiMode = gui3DDepthMode;
					ChangeGUI3DDepthMode((GUI_3D_Depth_Modes)(i-1));
				}
			}
			hotkeyPressed=true;
		}
	}

	// avoid double input by using the menu velocity
	if (hotkeyPressed)
		menuVelocity.x+=2.0f;

	// test VRBoost reset hotkey
	if (controls.Key_Down(toggleVRBoostHotkey) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		if (hmVRboost!=NULL)
		{
			m_pVRboost_ReleaseAllMemoryRules();
			m_bVRBoostToggle = !m_bVRBoostToggle;
			if (tracker->getStatus() > MTS_OK)
				tracker->resetOrientationAndPosition();

			// set the indicator to be drawn
			m_fVRBoostIndicator = 1.0f;

			menuVelocity.x += 4.0f;
		}
	}

	//If we are in comfort mode and user has pushed left or right, then change yaw
	if (VRBoostValue[VRboostAxis::ComfortMode] != 0.0f  &&
		VRBoostStatus.VRBoost_Active &&
		(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		if (controls.xInputState.Gamepad.sThumbRX < -16384 ||
			controls.Key_Down(m_comfortModeLeftKey))
		{
			m_comfortModeYaw +=m_comfortModeYawIncrement;
			if (m_comfortModeYaw == 180.0f)
				m_comfortModeYaw = -180.0f;
			menuVelocity.x+=4.0f;
		}

		if (controls.xInputState.Gamepad.sThumbRX > 16384 ||
			controls.Key_Down(m_comfortModeRightKey))
		{
			m_comfortModeYaw -= m_comfortModeYawIncrement;
			if (m_comfortModeYaw == -180.0f)
				m_comfortModeYaw = 180.0f;
			menuVelocity.x+=4.0f;
		}
	}

	//Double clicking the Right-Ctrl will disable or re-enable all Vireio hot-keys
	static DWORD rctrlStartClick = 0;
	if ((controls.Key_Down(VK_RCONTROL) || rctrlStartClick != 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		if (controls.Key_Down(VK_RCONTROL) && rctrlStartClick == 0)
		{
			rctrlStartClick = 1;
		}
		else if (!controls.Key_Down(VK_RCONTROL) && rctrlStartClick == 1)
		{
			rctrlStartClick = GetTickCount();
		}
		else if (controls.Key_Down(VK_RCONTROL) && rctrlStartClick > 1)
		{
			//If we clicked a second time within 500 ms
			if ((GetTickCount() - rctrlStartClick) <= 500)
			{
				if (!m_disableAllHotkeys)
				{
					m_disableAllHotkeys = true;
					VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 2000);
					sprintf_s(popup.line[2], "VIREIO HOT-KEYS: DISABLED");
					ShowPopup(popup);
				}
				else
				{
					m_disableAllHotkeys = false;
					VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 2000);
					sprintf_s(popup.line[2], "VIREIO HOT-KEYS: ENABLED");
					ShowPopup(popup);
				}
			}

			rctrlStartClick = 0;
			menuVelocity.x+=2.0f;
		}
		else if (rctrlStartClick > 1 && 
			(GetTickCount() - rctrlStartClick) > 500)
		{
			//Reset, user clearly not double clicking
			rctrlStartClick = 0;
		}
	}

	//Disconnected Screen View Mode
	if ((controls.Key_Down(edgePeekHotkey) || (controls.Key_Down(VK_MBUTTON) || (controls.Key_Down(VK_LCONTROL) && controls.Key_Down(VK_NUMPAD2)))) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
	{
		static bool bSurpressPositionaltracking = true;
		static bool bForceMouseEmulation = false;
		if (m_bfloatingScreen)
		{
			m_bfloatingScreen = false;
			m_bSurpressHeadtracking = false;
			tracker->setMouseEmulation(bForceMouseEmulation);
			bSurpressPositionaltracking = m_bSurpressPositionaltracking;
			m_bSurpressPositionaltracking = false;
			//TODO Change this back to initial
			this->stereoView->HeadYOffset = 0;
			this->stereoView->HeadZOffset = FLT_MAX;
			this->stereoView->XOffset = 0;
			this->stereoView->PostReset();	
		}
		else
		{
			//Suspend in-game movement whilst showing disconnected screen view
			m_bfloatingScreen = true;
			m_bSurpressHeadtracking = true;
			bForceMouseEmulation = tracker->setMouseEmulation(false);
			m_bSurpressPositionaltracking = bSurpressPositionaltracking;
			if (tracker->getStatus() >= MTS_OK)
			{
				m_fFloatingScreenPitch = tracker->primaryPitch;
				m_fFloatingScreenYaw = tracker->primaryYaw;			
				m_fFloatingScreenZ = tracker->z;			
			}
		}

		VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 700);
		if (m_bfloatingScreen)
			strcpy_s(popup.line[2], "Disconnected Screen Enabled");
		else
			strcpy_s(popup.line[2], "Disconnected Screen Disabled");
		ShowPopup(popup);

		menuVelocity.x += 4.0f;		
	}

	float screenFloatMultiplierY = 0.75;
	float screenFloatMultiplierX = 0.5;
	float screenFloatMultiplierZ = 1.5;
	if(m_bfloatingScreen)
	{
		if (tracker->getStatus() >= MTS_OK)
		{
			this->stereoView->HeadYOffset = (m_fFloatingScreenPitch - tracker->primaryPitch) * screenFloatMultiplierY;
			this->stereoView->XOffset = (m_fFloatingScreenYaw - tracker->primaryYaw) * screenFloatMultiplierX;
			this->stereoView->HeadZOffset = (m_fFloatingScreenZ - tracker->z) * screenFloatMultiplierZ;
			this->stereoView->PostReset();
		}
	}
	else
	{
		if (this->stereoView->m_screenViewGlideFactor < 1.0f)
		{
			float drift = (sinf(1 + (-cosf((1.0f - this->stereoView->m_screenViewGlideFactor) * 3.142f) / 2)) - 0.5f) * 2.0f;
			this->stereoView->HeadYOffset = ((m_fFloatingScreenPitch - tracker->primaryPitch) * screenFloatMultiplierY) 
				* drift;
			this->stereoView->XOffset = ((m_fFloatingScreenYaw - tracker->primaryYaw) * screenFloatMultiplierX) 
				* drift;

			this->stereoView->PostReset();
		}
	}

	//Anything in the following block will be unavailable whilst disable hot-keys is active
	if (!m_disableAllHotkeys)
	{
		//Rset HMD Orientation+Position LSHIFT+R, or L+R Shoulder buttons on Xbox 360 controller
		if ((((controls.Key_Down(VK_LSHIFT) || controls.Key_Down(VK_LCONTROL)) && controls.Key_Down('R')) 
			|| (controls.xButtonsStatus[8] && controls.xButtonsStatus[9]))
			&& (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (calibrate_tracker)
			{
				calibrate_tracker = false;
				//Replace popup
				DismissPopup(VPT_CALIBRATE_TRACKER);
				VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 3000);
				strcpy_s(popup.line[2], "HMD Orientation and Position Calibrated");
				strcpy_s(popup.line[3], "Please repeat if required...");
				ShowPopup(popup);
			}
			else
			{
				VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
				strcpy_s(popup.line[2], "HMD Orientation and Position Reset");
				ShowPopup(popup);
			}
			tracker->resetOrientationAndPosition();
			menuVelocity.x+=2.0f;
		}

		//Duck and cover - trigger crouch and prone keys if Y position of HMD moves appropriately
		if (m_DuckAndCover.dfcStatus > DAC_INACTIVE &&
			m_DuckAndCover.dfcStatus < DAC_DISABLED && tracker &&
			tracker->getStatus() >= MTS_OK)
		{
			if ((controls.xButtonsStatus[0x0c] || controls.Key_Down(VK_RSHIFT)) && menuVelocity == D3DXVECTOR2(0.0f, 0.0f))
			{
				if (m_DuckAndCover.dfcStatus == DAC_CAL_STANDING)
				{
					//Reset positional ready for the next stage
					tracker->resetPosition();
					m_DuckAndCover.dfcStatus = DAC_CAL_CROUCHING;
				}
				else if (m_DuckAndCover.dfcStatus == DAC_CAL_CROUCHING)
				{
					m_DuckAndCover.yPos_Crouch = tracker->y;
					//Slightly randomly decided on this
					m_DuckAndCover.yPos_Jump = fabs(tracker->y) / 3.0f;
					m_DuckAndCover.dfcStatus = DAC_CAL_PRONE;
				}
				else if (m_DuckAndCover.dfcStatus == DAC_CAL_PRONE)
				{
					m_DuckAndCover.proneEnabled = true;
					m_DuckAndCover.yPos_Prone = tracker->y - m_DuckAndCover.yPos_Crouch;
					m_DuckAndCover.dfcStatus = DAC_CAL_COMPLETE;
				}
				else if (m_DuckAndCover.dfcStatus == DAC_CAL_COMPLETE)
				{
					//Ready to go..
					m_DuckAndCover.dfcStatus = DAC_STANDING;
					tracker->resetPosition();
					DismissPopup(VPT_NOTIFICATION);
				}
				menuVelocity.x += 5.0f;
			}
			//B button only skips the prone position
			else if ((controls.xButtonsStatus[0x0d] || controls.Key_Down(VK_ESCAPE)) && menuVelocity == D3DXVECTOR2(0.0f, 0.0f))
			{
				if (m_DuckAndCover.dfcStatus == DAC_CAL_PRONE)
				{
					m_DuckAndCover.proneEnabled = false;
					m_DuckAndCover.dfcStatus = DAC_CAL_COMPLETE;
				}
				menuVelocity.x += 5.0f;
			}
		}


		// Show active VRBoost axes and their addresses (SHIFT+V)
		if (controls.Key_Down(VK_LSHIFT) && (controls.Key_Down('V')) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (hmVRboost!=NULL)
			{
				if (VRBoostStatus.VRBoost_Active)
				{
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 10000);
				
					ActiveAxisInfo axes[30];
					memset(axes, 0xFF, sizeof(ActiveAxisInfo) * 30);
					UINT count = m_pVRboost_GetActiveRuleAxes((ActiveAxisInfo**)&axes);
					sprintf_s(popup.line[0], "VRBoost Axis Addresses: %i", count);

					UINT i = 0;
					while (i < count)
					{
						if (axes[i].Axis == MAXDWORD || i == 6)
							break;

						std::string axisName = VRboostAxisString(axes[i].Axis);
						sprintf_s(popup.line[i+1], "      %s:      0x%"PR_SIZET"x", axisName.c_str(), axes[i].Address);

						i++;
					}	
			
					ShowPopup(popup);
				}

				menuVelocity.x += 4.0f;
			}
		}

		// switch to 2d Depth Mode (Shift + O / Numpad 9)
		if (controls.Key_Down(VK_LSHIFT) && (controls.Key_Down('O') || controls.Key_Down(VK_NUMPAD9)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if(!m_b2dDepthMode)
			{
				m_b2dDepthMode = true;
				stereoView->m_b2dDepthMode = true;

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Depth Perception Mode On");
				ShowPopup(popup);
			}
			else
			{
				m_b2dDepthMode = false;
				stereoView->m_b2dDepthMode = false;
				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Depth Perception Mode Off");
				ShowPopup(popup);
			}
			menuVelocity.x += 4.0f;
		
		}

		// Swap Sides on Depth mode (Alt + O)
		if (controls.Key_Down(VK_MENU) && controls.Key_Down('O') && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if(m_b2dDepthMode)
			{
				if(stereoView->m_bLeftSideActive)
				{
					stereoView->m_bLeftSideActive = false;
				}
				else
				{
					stereoView->m_bLeftSideActive = true;
				}
				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Depth Perception Side Switched");
				ShowPopup(popup);
			}		
			menuVelocity.x += 4.0f;
		
		}

		// cycle Render States
		if (controls.Key_Down(VK_MENU) && (controls.Key_Down(VK_LEFT) || controls.Key_Down(VK_RIGHT)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			std::string _str = "";
			if(controls.Key_Down(VK_LEFT))
			{			
				_str = stereoView->CycleRenderState(false);
			}
			else if(controls.Key_Down(VK_RIGHT))
			{			
				_str = stereoView->CycleRenderState(true);
			}
			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
			sprintf_s(popup.line[2], _str.append(" :: New Render State").c_str());
			ShowPopup(popup);
		
			menuVelocity.x += 4.0f;		
		}

		// Toggle Through Cube Renders -> ALt + 1
		if (controls.Key_Down(VK_MENU) && controls.Key_Down('1') && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
			if(m_pGameHandler->intDuplicateCubeTexture < 3)
			{
				m_pGameHandler->intDuplicateCubeTexture++;
				if(m_pGameHandler->intDuplicateCubeTexture == 1)
					sprintf_s(popup.line[2], "Cube Duplication :: Always False");
				else if(m_pGameHandler->intDuplicateCubeTexture == 2)
					sprintf_s(popup.line[2], "Cube Duplication :: Always True");
				else if(m_pGameHandler->intDuplicateCubeTexture == 3)
					sprintf_s(popup.line[2], "Cube Duplication :: Always IS_RENDER_TARGET(Usage)");
			}
			else
			{
				m_pGameHandler->intDuplicateCubeTexture = 0;
				sprintf_s(popup.line[2], "Cube Duplication :: Default (Game Type)");
			}		
		
			ShowPopup(popup);		
			menuVelocity.x += 4.0f;		
		}
		// Toggle Through Texture Renders -> ALt + 2
		if (controls.Key_Down(VK_MENU) && controls.Key_Down('2') && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
			if(m_pGameHandler->intDuplicateTexture < 4)
			{
				m_pGameHandler->intDuplicateTexture++;
				if(m_pGameHandler->intDuplicateTexture == 1)
					sprintf_s(popup.line[2], "Texture Duplication :: Method 1");
				else if(m_pGameHandler->intDuplicateTexture == 2)
					sprintf_s(popup.line[2], "Texture Duplication :: Method 2 (1 + Width and Height)");
				else if(m_pGameHandler->intDuplicateTexture == 3)
					sprintf_s(popup.line[2], "Texture Duplication :: Always False");
				else if(m_pGameHandler->intDuplicateTexture == 4)
					sprintf_s(popup.line[2], "Texture Duplication :: Always True");
			}
			else
			{
				m_pGameHandler->intDuplicateTexture = 0;
				sprintf_s(popup.line[2], "Texture Duplication :: Default (Game Type)");
			}		
		
			ShowPopup(popup);		
			menuVelocity.x += 4.0f;		
		}

		//When to render vpmenu (Alt + Up)
		if (controls.Key_Down(VK_MENU) && controls.Key_Down(VK_UP) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
			if(m_deviceBehavior.whenToRenderVPMENU == DeviceBehavior::BEGIN_SCENE)
			{
				m_deviceBehavior.whenToRenderVPMENU = DeviceBehavior::END_SCENE;
				sprintf_s(popup.line[2], "VPMENU RENDER = END_SCENE");
			}
			else if(m_deviceBehavior.whenToRenderVPMENU == DeviceBehavior::END_SCENE)
			{
				m_deviceBehavior.whenToRenderVPMENU = DeviceBehavior::PRESENT;
				sprintf_s(popup.line[2], "VPMENU RENDER = PRESENT");
			}
			else
			{
				m_deviceBehavior.whenToRenderVPMENU = DeviceBehavior::BEGIN_SCENE;
				sprintf_s(popup.line[2], "VPMENU RENDER = BEGIN_SCENE");
			}
			ShowPopup(popup);		
			menuVelocity.x += 4.0f;		
		}

		//When to poll headtracking (Alt + Down)
		if (controls.Key_Down(VK_MENU) && controls.Key_Down(VK_DOWN) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
			if(m_deviceBehavior.whenToHandleHeadTracking == DeviceBehavior::BEGIN_SCENE)
			{
				m_deviceBehavior.whenToHandleHeadTracking = DeviceBehavior::END_SCENE;			
				sprintf_s(popup.line[2], "HEADTRACKING = END_SCENE");
			}
			else if(m_deviceBehavior.whenToHandleHeadTracking == DeviceBehavior::END_SCENE)
			{
				m_deviceBehavior.whenToHandleHeadTracking = DeviceBehavior::BEGIN_SCENE;			
				sprintf_s(popup.line[2], "HEADTRACKING = BEGIN SCENE");
			}	
			/*else if(m_deviceBehavior.whenToHandleHeadTracking == DeviceBehavior::PRESENT)
			{
				m_deviceBehavior.whenToHandleHeadTracking = DeviceBehavior::BEGIN_SCENE;
				sprintf_s(popup.line[2], "HEADTRACKING = BEGIN SCENE");
			}//TODO This Crashes for some reason - problem for another day*/
		
			ShowPopup(popup);		
			menuVelocity.x += 4.0f;		
		}

		// Initiate VRBoost Memory Scan (NUMPAD5 or <LCTRL> + </> )
		if ((controls.Key_Down(VK_NUMPAD5) || (controls.Key_Down(VK_OEM_2) && controls.Key_Down(VK_LCONTROL))) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (hmVRboost!=NULL)
			{
				//Use local static, as it is a simple flag
				static bool showRescanWarning = false;
				static bool shownRescanWarning = false;
				if (showRescanWarning && !VRBoostStatus.VRBoost_Scanning)
				{
					//If roll isn't enabled then rolling is done through the game engine using VRBoost
					//In this case, if the user has already run a successful scan then running again would likely
					//fail as the roll address will almost definitely not be 0, which is what the scanner would be looking for
					//so before starting the scan, confirm with the user this is what they actually wish to do
					//This will also prevent an accidental re-run
					//Games that use matrix roll can usually be re-run without issue
					VireioPopup popup(VPT_NOTIFICATION, VPS_INFO, 5000);
					sprintf_s(popup.line[0], "   *WARNING*: re-running a scan once stable");
					sprintf_s(popup.line[1], "   addresses have been found could fail");
					sprintf_s(popup.line[2], "   IF NO SCAN HAS YET SUCCEEDED; IGNORE THIS WARNING");
					sprintf_s(popup.line[4], "   Press scan trigger again to initiate scan");
					sprintf_s(popup.line[5], "   or wait for this message to disappear (No Scan)");
					ShowPopup(popup);
					showRescanWarning = false;
					shownRescanWarning = true;
				}
				else
				{
					//Ensure the previous notification is dismissed
					if (shownRescanWarning)
					{
						DismissPopup(VPT_NOTIFICATION);
						shownRescanWarning = false;
					}

					ReturnValue vr = m_pVRboost_StartMemoryScan();
					if (vr == VRBOOST_ERROR)
					{
						VireioPopup popup(VPT_VRBOOST_FAILURE, VPS_TOAST, 5000);
						sprintf_s(popup.line[2], "VRBoost: StartMemoryScan - Failed");
						ShowPopup(popup);
					}
					//If initialising then we have successfully started a new scan
					else if (vr = VRBOOST_SCAN_INITIALISING)
					{
						VRBoostStatus.VRBoost_Scanning = true;
						//Definitely have no candidates at this point
						VRBoostStatus.VRBoost_Candidates = false;
						showRescanWarning = true;
					}
				}
				menuVelocity.x += 4.0f;
			}
		}

		// Select next scan candidate if there is one
		//  Increase = NUMPAD6 or <LCTRL> + <.> 
		//  Decrease = NUMPAD4 or <LCTRL> + <,> 
		if (VRBoostStatus.VRBoost_Candidates && 
			(controls.Key_Down(VK_NUMPAD6) || controls.Key_Down(VK_NUMPAD4) || (controls.Key_Down(VK_LCONTROL) && (controls.Key_Down(VK_OEM_COMMA) || controls.Key_Down(VK_OEM_PERIOD)))) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (hmVRboost!=NULL)
			{
				static int c = 0;
				UINT candidates = m_pVRboost_GetScanCandidates();
				bool increase = (controls.Key_Down(VK_NUMPAD6) || controls.Key_Down(VK_OEM_PERIOD));
				if (increase)
					c = (c + 1) % candidates;
				else
				{
					if (--c < 0) c = candidates - 1;
				}

				m_pVRboost_SetNextScanCandidate(increase);
				VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "VRBoost: Select Next Scan Candidate: %i / %i", c+1, candidates);
				DismissPopup(VPT_NOTIFICATION);
				ShowPopup(popup);

				menuVelocity.x += 2.0f;
			}
		}
	
		// Cancel VRBoost Memory Scan Mode (NUMPAD8 or <LCTRL> + <;> )
		if ((controls.Key_Down(VK_NUMPAD8) || (controls.Key_Down(VK_OEM_1) && controls.Key_Down(VK_LCONTROL)))
			&& (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			DismissPopup(VPT_VRBOOST_SCANNING);

			VRBoostStatus.VRBoost_Scanning = false;
			VRBoostStatus.VRBoost_Candidates = false;

			m_bForceMouseEmulation = true;
			tracker->setMouseEmulation(true);

			menuVelocity.x-=2.0f;
		}

		//Enabled/Disable Free Pitch (default is disabled), LSHIFT + X
		if (VRBoostStatus.VRBoost_Active && 
			(controls.Key_Down(VK_LSHIFT) && controls.Key_Down('X')) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (VRBoostValue[VRboostAxis::FreePitch] != 0.0f)
			{
				//Disable Free Pitch
				VRBoostValue[VRboostAxis::FreePitch] = 0.0f;

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Pitch Free-look Disabled");
				ShowPopup(popup);
			}
			else
			{
				//Enable Free Pitch
				VRBoostValue[VRboostAxis::FreePitch] = 1.0f;

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Pitch Free-look Enabled");
				ShowPopup(popup);
			}

			menuVelocity.x+=2.0f;
		}

		//Enabled/Disable Comfort Mode - LSHIFT + M
		if (VRBoostStatus.VRBoost_Active && 
			(controls.Key_Down(VK_LSHIFT) && controls.Key_Down('M')) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (VRBoostValue[VRboostAxis::ComfortMode] != 0.0f)
			{
				//Disable Comfort Mode
				VRBoostValue[VRboostAxis::ComfortMode] = 0.0f;
				m_comfortModeYaw = 0.0f;

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "Comfort Mode Disabled");
				ShowPopup(popup);
			}
			else
			{
				//Enable Comfort Mode
				VRBoostValue[VRboostAxis::ComfortMode] = 1.0f;
				m_comfortModeYaw = 0.0f;

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 3000);
				sprintf_s(popup.line[2], "Comfort Mode Enabled");
				ShowPopup(popup);
			}

			menuVelocity.x+=2.0f;
		}

		//Enabled/Disable Black Smear Correction for DK2 (default is disabled), LSHIFT + B
		if ((tracker && tracker->SupportsPositionTracking()) &&
			(controls.Key_Down(VK_LSHIFT) && controls.Key_Down('B')) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (stereoView->m_blackSmearCorrection != 0.0f)
			{
				stereoView->m_blackSmearCorrection = 0.0f;
				stereoView->PostReset();		

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "DK2 Black Smear Correction Disabled");
				ShowPopup(popup);
			}
			else
			{
				stereoView->m_blackSmearCorrection = 0.02f;
				stereoView->PostReset();		

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 1000);
				sprintf_s(popup.line[2], "DK2 Black Smear Correction Enabled");
				ShowPopup(popup);
			}

			menuVelocity.x+=2.0f;
		}


		//Reset IPD Offset to 0  -  F8  or  LSHIFT+I
		if ((controls.Key_Down(VK_F8) || (controls.Key_Down(VK_LSHIFT) && controls.Key_Down('I'))) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			this->stereoView->IPDOffset = 0.0;
			this->stereoView->PostReset();		

			VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
			sprintf_s(popup.line[2], "IPD-Offset: %1.3f", this->stereoView->IPDOffset);
			ShowPopup(popup);
			m_saveConfigTimer = GetTickCount();
			menuVelocity.x+=2.0f;
		}

		//Show FPS Counter / Frame Time counter LSHIFT+F
		if (((controls.Key_Down(VK_LSHIFT) || controls.Key_Down(VK_LCONTROL)) && controls.Key_Down('F')) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			show_fps = (FPS_TYPE)((show_fps+1) % 3);
			menuVelocity.x+=2.0f;
		}

		//Show HMD Stats Counter LSHIFT+H 
		if (((controls.Key_Down(VK_LSHIFT) || controls.Key_Down(VK_LCONTROL)) && controls.Key_Down('H')) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (activePopup.popupType == VPT_STATS)
			{
				DismissPopup(VPT_STATS);
			}
			else
			{
				VireioPopup popup(VPT_STATS);
				ShowPopup(popup);
			}
			menuVelocity.x+=2.0f;
		}

		//Toggle positional tracking
		if ((controls.Key_Down(VK_F11) || ((controls.Key_Down(VK_LSHIFT) || controls.Key_Down(VK_LCONTROL)) && controls.Key_Down('P'))) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			m_bPosTrackingToggle = !m_bPosTrackingToggle;

			VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
			if (m_bPosTrackingToggle)
				strcpy_s(popup.line[2], "HMD Positional Tracking Enabled");
			else
				strcpy_s(popup.line[2], "HMD Positional Tracking Disabled");
			ShowPopup(popup);

			if (!m_bPosTrackingToggle)
				m_spShaderViewAdjustment->UpdatePosition(0.0f, 0.0f, 0.0f);

			menuVelocity.x += 4.0f;
		}

		//Toggle SDK Pose Prediction- LSHIFT + DELETE
		if (hmdInfo->GetHMDManufacturer() == HMD_OCULUS	&&
			(controls.Key_Down(VK_LSHIFT) && controls.Key_Down(VK_DELETE)) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)) && tracker)
		{
			tracker->useSDKPosePrediction = !tracker->useSDKPosePrediction;

			VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
			if (tracker->useSDKPosePrediction)
				strcpy_s(popup.line[2], "SDK Pose Prediction Enabled");
			else
				strcpy_s(popup.line[2], "SDK Pose Prediction Disabled");
			ShowPopup(popup);

			menuVelocity.x += 4.0f;
		}

		//Toggle chromatic abberation correction - SHIFT+J
		if (((controls.Key_Down(VK_LSHIFT) || controls.Key_Down(VK_LCONTROL)) && controls.Key_Down('J'))
			&& (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			stereoView->chromaticAberrationCorrection = !stereoView->chromaticAberrationCorrection;

			VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
			if (stereoView->chromaticAberrationCorrection)
				strcpy_s(popup.line[2], "Chromatic Aberration Correction Enabled");
			else
				strcpy_s(popup.line[2], "Chromatic Aberration Correction Disabled");
			ShowPopup(popup);

			menuVelocity.x += 4.0f;
		}

		//Double clicking the NUMPAD0 key will invoke the VR mouse
		//Double clicking when VR mouse is enabled will either:
		//   - Toggle between GUI and HUD scaling if double click occurs within 2 seconds
		//   - Disable VR Mouse if double click occurs after 2 seconds
		static DWORD numPad0Click = 0;
		if ((controls.Key_Down(VK_NUMPAD0) || numPad0Click != 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (controls.Key_Down(VK_NUMPAD0) && numPad0Click == 0)
			{
				numPad0Click = 1;
			}
			else if (!controls.Key_Down(VK_NUMPAD0) && numPad0Click == 1)
			{
				numPad0Click = GetTickCount();
			}
			else if (controls.Key_Down(VK_NUMPAD0) && numPad0Click > 1)
			{
				//If we clicked a second time within 500 ms, then trigger VR Mouse
				if ((GetTickCount() - numPad0Click) <= 500)
				{
					static DWORD tc = 0;
					if (tc != 0 && (GetTickCount() - tc > 2000))
					{
						tc = 0;
						m_showVRMouse = 0;
						stereoView->m_mousePos.x = 0;
						stereoView->m_mousePos.y = 0;
					}
					else
					{
						tc = GetTickCount();
						if (m_showVRMouse == 2)
							m_showVRMouse = 1;
						else
							m_showVRMouse++;
					}

					stereoView->PostReset();

					VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
					DismissPopup(VPT_NOTIFICATION);
					if (m_showVRMouse == 1)
						strcpy_s(popup.line[2], "VR Mouse - GUI Scaling");
					else if (m_showVRMouse == 2)
						strcpy_s(popup.line[2], "VR Mouse - HUD Scaling");
					else
						strcpy_s(popup.line[2], "VR Mouse - Disabled");
					ShowPopup(popup);

					menuVelocity.x += 4.0f;		
				}

				numPad0Click = 0;
				menuVelocity.x+=2.0f;
			}
			else if (numPad0Click > 1 &&
				(GetTickCount() - numPad0Click) > 500)
			{
				//Reset, user clearly not double clicking
				numPad0Click = 0;
			}
		}
		
		// floaty menus
		if (controls.Key_Down(VK_LCONTROL) && controls.Key_Down(VK_NUMPAD1) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (m_bfloatingMenu)
				m_bfloatingMenu = false;
			else
			{
				m_bfloatingMenu = true;
				if (tracker->getStatus() >= MTS_OK)
				{
					m_fFloatingPitch = tracker->primaryPitch;
					m_fFloatingYaw = tracker->primaryYaw;			
				}
			}

			VireioPopup popup(VPT_NOTIFICATION, VPS_TOAST, 1200);
			if (m_bfloatingMenu)
				strcpy_s(popup.line[2], "Floating Menus Enabled");
			else
				strcpy_s(popup.line[2], "Floating Menus Disabled");
			ShowPopup(popup);

			menuVelocity.x += 4.0f;		
		}

		//Double clicking the start button will invoke the VP menu
		static DWORD startClick = 0;
		if ((controls.xButtonsStatus[4] || startClick != 0) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (controls.xButtonsStatus[4] && startClick == 0)
			{
				startClick = 1;
			}
			else if (!controls.xButtonsStatus[4] && startClick == 1)
			{
				startClick = GetTickCount();
			}
			else if (controls.xButtonsStatus[4] && startClick > 1)
			{
				//If we clicked a second time within 500 ms, then open vp menu
				if ((GetTickCount() - startClick) <= 500)
				{
					if (VPMENU_mode == VPMENU_Modes::INACTIVE)
					{
						borderTopHeight = 0.0f;
						VPMENU_mode = VPMENU_Modes::MAINMENU;
					}
					else
					{
						VPMENU_mode = VPMENU_Modes::INACTIVE;
						VPMENU_UpdateConfigSettings();
					}
				}

				startClick = 0;
				menuVelocity.x+=2.0f;
			}
			else if (startClick > 1 &&
				(GetTickCount() - startClick) > 500)
			{
				//Reset, user clearly not double clicking
				startClick = 0;
			}
		}

		// open VP Menu - <CTRL>+<Q>
		if(controls.Key_Down('Q') && controls.Key_Down(VK_LCONTROL) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if (VPMENU_mode == VPMENU_Modes::INACTIVE)
			{
				borderTopHeight = 0.0f;
				VPMENU_mode = VPMENU_Modes::MAINMENU;
			}
			else
			{
				VPMENU_mode = VPMENU_Modes::INACTIVE;
				VPMENU_UpdateConfigSettings();
			}

			menuVelocity.x+=2.0f;
		}

		// open VP Menu - <LSHIFT>+<*>
		if(controls.Key_Down(VK_MULTIPLY) && controls.Key_Down(VK_LSHIFT) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))		
		{
			if (VPMENU_mode == VPMENU_Modes::INACTIVE)
			{
				borderTopHeight = 0.0f;
				VPMENU_mode = VPMENU_Modes::MAINMENU;
			}
			else
			{
				VPMENU_mode = VPMENU_Modes::INACTIVE;
				VPMENU_UpdateConfigSettings();
			}

			menuVelocity.x+=2.0f;
		}

		//Mouse Wheel Scroll
		if(controls.Key_Down(VK_LCONTROL))
		{
			int _wheel = dinput.GetWheel();
			if(controls.Key_Down(VK_TAB))
			{
				if(_wheel < 0)
				{
					if(this->stereoView->YOffset > -0.1f)
					{
						this->stereoView->YOffset -= 0.005f;
						this->stereoView->PostReset();				
					}
				}
				else if(_wheel > 0)
				{
					if(this->stereoView->YOffset < 0.1f)
					{
						this->stereoView->YOffset += 0.005f;
						this->stereoView->PostReset();										
					}
				}

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
				sprintf_s(popup.line[2], "Y-Offset: %1.3f", this->stereoView->YOffset);
				m_saveConfigTimer = GetTickCount();
				ShowPopup(popup);
			}
			else if(controls.Key_Down(VK_LSHIFT))
 			{			
				if(_wheel < 0)
				{
					if(this->stereoView->IPDOffset > -0.1f)
					{
						this->stereoView->IPDOffset -= 0.001f;
						this->stereoView->PostReset();				
					}
				}
				else if(_wheel > 0)
				{
					if(this->stereoView->IPDOffset < 0.1f)
					{
						this->stereoView->IPDOffset += 0.001f;
						this->stereoView->PostReset();										
					}
 				}

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
				sprintf_s(popup.line[2], "IPD-Offset: %1.3f", this->stereoView->IPDOffset);
				m_saveConfigTimer = GetTickCount();
				ShowPopup(popup);
 			}
			//CTRL + ALT + Mouse Wheel - adjust World Scale dynamically
			else if (controls.Key_Down(VK_MENU))
			{
				float separationChange = 0.05f;
				if(_wheel < 0)
				{
					m_spShaderViewAdjustment->ChangeWorldScale(-separationChange);
				}
				else if(_wheel > 0)
				{
					m_spShaderViewAdjustment->ChangeWorldScale(separationChange);
 				}

				if(_wheel != 0)
				{
					m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);
					VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
					sprintf_s(popup.line[2], "Stereo Separation (World Scale): %1.3f", m_spShaderViewAdjustment->WorldScale());
					m_saveConfigTimer = GetTickCount();
					ShowPopup(popup);
				}
			}
			//CTRL + SPACE + Mouse Wheel - adjust stereo convergence dynamically
			else if(controls.Key_Down(VK_SPACE))
 			{	
				float convergenceChange = 0.1f;
				if(_wheel < 0)
				{
					m_spShaderViewAdjustment->ChangeConvergence(-convergenceChange);
				}
				else if(_wheel > 0)
				{
					m_spShaderViewAdjustment->ChangeConvergence(convergenceChange);
 				}

				if(_wheel != 0)
				{
					m_spShaderViewAdjustment->UpdateProjectionMatrices((float)stereoView->viewport.Width/(float)stereoView->viewport.Height);
					VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
					sprintf_s(popup.line[2], "Stereo Convergence: %1.3f", m_spShaderViewAdjustment->Convergence());
					m_saveConfigTimer = GetTickCount();
					ShowPopup(popup);
				}
			}
			else
			{
				if (_wheel != 0)
				{
					if(_wheel < 0)
					{
						/*if(this->stereoView->DistortionScale > m_spShaderViewAdjustment->HMDInfo()->GetMinDistortionScale())
						{
							this->stereoView->DistortionScale -= 0.05f;
							this->stereoView->PostReset();				
						}*/
						if(this->stereoView->ZoomOutScale > 0.05f)
						{
							this->stereoView->ZoomOutScale -= 0.05f;
							this->stereoView->PostReset();				
						}
					}
					else if(_wheel > 0)
					{
						/*if(this->stereoView->DistortionScale < m_maxDistortionScale)
						{
							this->stereoView->DistortionScale += 0.05f;
							this->stereoView->PostReset();										
						}*/
						if(this->stereoView->ZoomOutScale < 2.00f)
						{
							this->stereoView->ZoomOutScale += 0.05f;
							this->stereoView->PostReset();				
						}
					}

					VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
					sprintf_s(popup.line[2], "Zoom Scale: %1.3f", this->stereoView->ZoomOutScale);
					m_saveConfigTimer = GetTickCount();
					ShowPopup(popup);
				}
			}
		}
	
		//Change Distortion Scale CTRL + + / -
		if(controls.Key_Down(VK_LCONTROL) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			if(controls.Key_Down(VK_ADD))
			{
				this->stereoView->ZoomOutScale = 1.00f;
				this->stereoView->PostReset();	

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
				sprintf_s(popup.line[2], "Zoom Scale: %1.3f", this->stereoView->ZoomOutScale);
				m_saveConfigTimer = GetTickCount();
				ShowPopup(popup);
			}
			else if(controls.Key_Down(VK_SUBTRACT))
			{
				this->stereoView->ZoomOutScale = 0.50f;
				this->stereoView->PostReset();	

				VireioPopup popup(VPT_ADJUSTER, VPS_TOAST, 500);
				sprintf_s(popup.line[2], "Zoom Scale: %1.3f", this->stereoView->ZoomOutScale);
				m_saveConfigTimer = GetTickCount();
				ShowPopup(popup);
			}		
		}	


		// screenshot - <RCONTROL>+<*>
		if(controls.Key_Down(VK_MULTIPLY) && controls.Key_Down(VK_RCONTROL) && (menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))		
		{
			// render 3 frames to get screenshots
			screenshot = 3;
			menuVelocity.x+=8.0f;
		}
	
		//Telescopic mode - use ALT + Mouse Wheel CLick
		if (controls.Key_Down(VK_MENU) &&
			controls.Key_Down(VK_MBUTTON) && 
			(menuVelocity == D3DXVECTOR2(0.0f, 0.0f)))
		{
			//First check whether VRBoost is controlling FOV, we can't use this functionality if it isn't
			bool canUseTelescope = false;
			if (VRBoostStatus.VRBoost_Active)
			{
				ActiveAxisInfo axes[30];
				memset(axes, 0xFF, sizeof(ActiveAxisInfo) * 30);
				UINT count = m_pVRboost_GetActiveRuleAxes((ActiveAxisInfo**)&axes);

				UINT i = 0;
				while (i < count)
				{
					if (axes[i].Axis == MAXDWORD)
						break;
					if (axes[i].Axis == VRboostAxis::WorldFOV)
					{
						canUseTelescope = true;
						break;
					}
					i++;
				}	
			}

			if (canUseTelescope)
			{
				if (!m_telescopicSightMode &&
					m_telescopeTargetFOV == FLT_MAX)
				{   
					//enabling - reduce FOV to 20 (will result in zooming in)
					m_telescopeTargetFOV = 20;
					m_telescopeCurrentFOV = config.WorldFOV;
					stereoView->m_vignetteStyle = StereoView::TELESCOPIC_SIGHT;
					m_telescopicSightMode = true;
				}
				else if (m_telescopicSightMode)
				{
					//disabling
					m_telescopicSightMode = false;
					m_telescopeTargetFOV = config.WorldFOV;
					stereoView->m_vignetteStyle = StereoView::NONE;
				}

				menuVelocity.x += 4.0f;
			}
		}
	}
}