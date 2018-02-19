#ifndef MY_CAMERA_H
#define MY_CAMERA_H

struct camera_info_struct
{
	char playerCameraMode;
	float vehicleToCameraCoordTransform[16];
};

#define PLAYERCAMERA_FP		1
#define PLAYERCAMERA_TP		2
#define PLAYERCAMERA_VEHICLE	3

#endif
