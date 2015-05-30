/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <OculusTracker.cpp> and
Class <OculusTracker> :
Copyright (C) 2012 Andres Hernandez

Vireio Perception Version History:
v1.0.0 2012 by Andres Hernandez
v1.0.X 2013 by John Hicks, Neil Schneider
v1.1.x 2013 by Primary Coding Author: Chris Drain
Team Support: John Hicks, Phil Larkson, Neil Schneider
v2.0.x 2013 by Denis Reischl, Neil Schneider, Joshua Brown

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
#define _WINSOCKAPI_
#include "OculusTracker.h"
#include "HMDisplayInfoFactory.h"
#include "D3DProxyDevice.h"
#include <string>

#include <OVR.h>
//#include <OVR_Stereo.h>
//#include <OVR_Profile.h>
//#include <CAPI/CAPI_HMDState.h>
//#include <Sensors/OVR_DeviceConstants.h>

using namespace vireio;

/**
* Constructor.
* Calls init function.
***/ 
OculusTracker::OculusTracker(ProxyConfig *config)
	: MotionTracker(config)
{
	status = MTS_NOTINIT;
	init();
}

/**
* Destructor.
* Clears and destroys Oculus system.
***/
OculusTracker::~OculusTracker()
{
	ovrHmd_Destroy(hmd);
	ovr_Shutdown();
}

/**
* Oculus init.
* Calls libOVR System::Init, gets device manager, hmd-device and sensor.
***/
void OculusTracker::init()
{
	OutputDebugString("OculusTracker Start");
	hmd = NULL;
	status = MTS_INITIALISING;
	ovrResult res = ovr_Initialize(NULL); // start LibOVR

	if (OVR_SUCCESS(res))
	{
		debugf("OculusTracker Initialized");
	}
	else
	{
		ovrErrorInfo errorInfo;
		ovr_GetLastErrorInfo(&errorInfo);
		debugf("OculusTracker Initialize call failed: %s", errorInfo.ErrorString);
		status = MTS_DRIVERFAIL;
		return;
	}
	 
	int detected = ovrHmd_Detect();
	if (detected == 0)
	{
		OutputDebugString("No HMD detected, use a dummy DK1");
		ovrHmd_CreateDebug(ovrHmd_DK1, &hmd);
		status = MTS_NOHMDDETECTED;
		strcpy_s(trackerDescription, "No HMD Detected");
		return;
	}
	else
	{
		ovrHmd_Create(0, &hmd);
		strcpy_s(trackerDescription, (std::string(hmd->ProductName) + "   Serial: " + hmd->SerialNumber).c_str());
		if (hmd->Type == ovrHmd_DK2)
			supportsPositional = true;
	}


	unsigned int trackingCaps = ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position;
	ovrBool success = ovrHmd_ConfigureTracking(hmd, trackingCaps, 0);

	if (!success)
	{
		OutputDebugString("oculus tracker failed to initialise tracking");
		status = MTS_INITFAIL;
	}

	resetOrientationAndPosition();

	OutputDebugString("oculus tracker initted");
	if (status == MTS_INITIALISING)
		status = MTS_OK;

#ifdef SHOW_CALLS
	debugf("init: %i", (int)status);
#endif
}

char* OculusTracker::GetTrackerDescription()
{
	return trackerDescription;
}

bool OculusTracker::SupportsPositionTracking()
{
	return supportsPositional;
}


void OculusTracker::BeginFrame()
{
	static unsigned int frameID = 0;
	//return;
	if (status >= MTS_OK  && useSDKPosePrediction)
	{
		//FrameRef=ovrHmd_BeginFrameTiming(hmd,frameID++);
		ovrHmd_GetFrameTiming(hmd, frameID++);
	}
}

void OculusTracker::WaitTillTime()
{
	// Commented out when updating SDK to 0.6, as TimewarpPointSeconds is no longer
	// a member of FrameRef. This is okay because WaitTillTime was never called. I
	// believe this is the remnant of an incompleted attempt to implement the thing
	// where you wait until close to scanout to do your distortion.
	/*if (status >= MTS_OK && useSDKPosePrediction)
	{
		ovr_WaitTillTime(FrameRef.TimewarpPointSeconds);
	}*/
}

void OculusTracker::EndFrame()
{
	if (status >= MTS_OK && useSDKPosePrediction)
	{
		//ovrHmd_EndFrameTiming(hmd);
	}
}

/**
* Oculus reset.
* Resets the orientation - As per Oculus VR, only yaw/x/y/z should be reset
***/
void OculusTracker::resetOrientationAndPosition()
{
	SHOW_CALL("OculusTracker resetOrientationAndPosition\n");

	offsetYaw = 0.0f;
	offsetPitch = 0.0f;
	offsetRoll = 0.0f;
	offsetX = 0.0f;
	offsetY = 0.0f;
	offsetZ = 0.0f;

	//Force OVR positional reset
	ovrHmd_RecenterPose(hmd);

	ovrTrackingState ts = ovrHmd_GetTrackingState(hmd, ovr_GetTimeInSeconds());
	
	if (ts.StatusFlags & ovrStatus_OrientationTracked)
	{
		Quatf hmdOrient=ts.HeadPose.ThePose.Orientation;
		hmdOrient.GetEulerAngles<Axis_Y,Axis_X,Axis_Z>(&offsetYaw, &offsetPitch, &offsetRoll);
	}
	else
		status = MTS_NOORIENTATION;
	
	if (ts.StatusFlags & ovrStatus_PositionConnected)
	{
		if (ts.StatusFlags & ovrStatus_PositionTracked)
		{
			offsetX = ts.HeadPose.ThePose.Position.x;
			offsetY = ts.HeadPose.ThePose.Position.y;
			offsetZ = ts.HeadPose.ThePose.Position.z;
		}
		else
			status = MTS_LOSTPOSITIONAL;
	}

#ifdef SHOW_CALLS
	debugf("resetOrientationAndPosition: %i", (int)status);
#endif 
}

/**
* Oculus reset.
* Resets the positional values only - As per Oculus VR, only x/y/z should be reset
***/
void OculusTracker::resetPosition()
{
	SHOW_CALL("OculusTracker resetPosition\n");

	offsetX = 0.0f;
	offsetY = 0.0f;
	offsetZ = 0.0f;

	//Force OVR positional reset
	ovrHmd_RecenterPose(hmd);

	ovrTrackingState ts = ovrHmd_GetTrackingState(hmd, ovr_GetTimeInSeconds());
	
	if (ts.StatusFlags & ovrStatus_PositionConnected)
	{
		if (ts.StatusFlags & ovrStatus_PositionTracked)
		{
			offsetX = ts.HeadPose.ThePose.Position.x;
			offsetY = ts.HeadPose.ThePose.Position.y;
			offsetZ = ts.HeadPose.ThePose.Position.z;
		}
		else
			status = MTS_LOSTPOSITIONAL;
	}

#ifdef SHOW_CALLS
	debugf("resetPosition: %i", (int)status);
#endif
}

/**
* Retrieve Oculus tracker orientation.
* Reads device input and returns orientation (yaw and roll negated). All Orientations are in degrees.
* Roll gets converted back to radians in updateOrientation.
***/
int OculusTracker::getOrientationAndPosition(float* yaw, float* pitch, float* roll, float* x, float* y, float* z) 
{
	SHOW_CALL("OculusTracker getOrientationAndPosition\n");

	ovrTrackingState ts = ovrHmd_GetTrackingState(hmd, useSDKPosePrediction ? FrameRef.DisplayMidpointSeconds : ovr_GetTimeInSeconds());

	if (ts.StatusFlags & ovrStatus_OrientationTracked)
	{
		Quatf hmdOrient=ts.HeadPose.ThePose.Orientation;
		hmdOrient.GetEulerAngles<Axis_Y,Axis_X,Axis_Z>(yaw, pitch, roll);
		
		*roll = -*roll;
		*pitch = -*pitch;

		status = MTS_OK;
	}
	else
		status = MTS_NOORIENTATION;

	if (ts.StatusFlags & ovrStatus_PositionConnected && status == MTS_OK)
	{
		if (!(ts.StatusFlags & ovrStatus_CameraPoseTracked))
		{
			//Camera still initialising/calibrating
			//Should probably warn user if this doesn't get set after a period of time
			static DWORD tick = GetTickCount();
			if (((tick - GetTickCount()) / 1000) > 15)
				status = MTS_CAMERAMALFUNCTION;
		}
		else if (ts.StatusFlags & ovrStatus_PositionTracked)
		{
			*x = ts.HeadPose.ThePose.Position.x;
			*y = ts.HeadPose.ThePose.Position.y;
			*z = ts.HeadPose.ThePose.Position.z;
			status = MTS_OK;
		}
		else
			status = MTS_LOSTPOSITIONAL;
	}

	return (int)status; 
}

/**
* Returns oculus tracker status as Motion Tracker Status enum
***/
MotionTrackerStatus OculusTracker::getStatus()
{
	return status;
}