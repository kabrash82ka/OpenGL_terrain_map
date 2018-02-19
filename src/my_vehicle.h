#ifndef MY_VEHICLE_H
#define MY_VEHICLE_H

struct vehicle_common_struct //this is also used to draw the wheels (and really is a basic common draw object)
{
	GLuint vbo;
	GLuint vao;
	GLuint ebo;
	GLuint texture_id;
	int num_indices;
	float driverSeatOffset[3];	//offset from car chassis origin to bottom of driver's okole
	float playerExitPoint[6]; //array of vec3, offset from car chassis origin to spot where
};

struct wheel_physics_struct
{
	float cg[3];		//center of gravity in vehicle coordinates.
	float prev_cg[3];	//center of gravity in the previous timestep
	float rel_pos[3];	//relative position from vehicle cg
	float surfNorm[3];	//surface normal from where wheel is on ground
	float orientation[9];
	float mass;
	float wheel_radius;
	float wheel_width;
	float momentOfInertia[9];
	float imomentOfInertia[9];
	float linearVel[3];
	float ls_angularVel[3]; //steering L. local-space angular velocity
	float angularVel[3]; //steering L. warning: angular velocity is in world coordinates
	float linearMomentum[3];
	float angularMomentum[3]; //steering L.
	float fwheelAngularMomentum;
	float fwheelAngularVel;
	float fwheelRad;	//this controls orientation of wheel for drawing
	char flags;
};
/*
flags field for wheel_physics_struct
	1: unused
	2: wheel rotates for steering
	4: wheel can provide torque to push fwd
*/
#define WHEELFLAGS_CAN_STEER 0x2
#define WHEELFLAGS_CAN_PUSH  0x4

/*TODO: Remove this old vehicle struct. Only here because of old funcs
would cause compile errors.
*/
struct vehicle_physics_struct //***THIS STRUCT IS OBSOLETE***
{
	struct wheel_physics_struct wheels[4];
	float cg[3];	//center of gravity vector in world coordinates
	float orientation[9];	//angular displacement in radians
	float mass;
	float momentOfInertia[9]; //3x3 matrix. The moment of inertia tensor.
	float imomentOfInertia[9]; //inverse moment of ineria
	float linearVel[3];	//vector for linear velocity
	float angularVel[3];	//vector for angular velocity
	float linearMomentum[3];
	float angularMomentum[3]; //local coordinates
	float steeringAngularMomentum[3]; //local coordinates
	float steeringLinearMomentum[3];
	float steeringAngle;
	float totalForce[3];	//this is used during a frame to get the force from the wheels
	float totalTorque[3];	//this is used during a frame to get the torque from the wheels
	float totalSpringForce[3]; //force from the wheel springs
	float totalSpringTorque[3]; //torque from the wheel springs
	float wheel_offsets[12]; //4 positions of where the wheels are relative to the CG. (Front-Left, Front-Right, Back-Right, Back-Left)
	float driverSeatOffset[3];	//offset from car chassis origin to bottom of driver's okole
	char flags;		
};
/*
flags field in vehicle_physics_struct2 is a bitfield:
	0 -unused					1
	1 -is accelerating			2
	2 -steer left				4
	3 -steer right				8
	4 -on ground?				16
	5 -zero the steering wheel	32
	6 -brake					64
	7 -parking brake			128
*/
#define VEHICLEFLAGS_ACCELERATE  0x2
#define VEHICLEFLAGS_STEER_LEFT  0x4
#define VEHICLEFLAGS_STEER_RIGHT 0x8
#define VEHICLEFLAGS_ON_GROUND   0x10
#define VEHICLEFLAGS_ZERO_STEER  0x20
#define	VEHICLEFLAGS_BRAKE       0x40
#define VEHICLEFLAGS_PARK        0x80

/*wheel struct*/
struct wheel_struct2
{
	float x;	//relative pos along down axis -y axis in vehicle coordinates (wheel pos is fixed to -y/+y axis)
	float min_x;	//minimum displacement
	float max_x;	//max displacement
	float connectPos[3];	//vector in vehicle local coordinates that is pos where wheel vector
	float k_spring;			//spring constant
	float k_damp;
	float mass;
};

struct vehicle_physics_struct2
{
	struct wheel_struct2 wheels[4]; //0=front-left 1=front-right 2=rear-right 3=rear-left
	float cg[3];				//center of gravity in world coordinates
	float orientation[9];		//3x3 mat of orientation
	float orientationQ[4];		//same orientation but as a quaternion (x,y,z,w)
	float mass;
	float momentOfInertia[9];	//moment of inertia tensor
	float imomentOfInertia[9]; 	//inverse moment of inertia in local coordinates
	float linearVel[3];
	float angularMomentum[3];	//holds the actual state of angular motion.
	float angularVel[3];		//read-only, in world coordinates, (need this to calculate relative linear velocity at certain spots on the body)
	float throttle;
	float wheelOrientation_X;		//scalar, degrees about X-axis
	float wheelRadius;			//scalar,
	float wheelbase;
	float steeringAngle;		//angle from fwd of front wheels in rads
	float prevSteeringTorque[3];
	int flags;
	int events;
};

#define VEHICLE_SMALLMOTION_LIMIT 0.000001f

#endif
