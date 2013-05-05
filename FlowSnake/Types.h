#define MAX_SSHORTF 32767.0f
#define MAX_USHORTF 65535.0f
#define MAX_SINTF 2147483647.0f 
#define MAX_UINTF 4294967295.0f 

typedef UINT uint;
typedef unsigned short ushort;

struct float2
{
	float x;
	float y;

	float2 operator= (float a)
	{
		x = a;
		y = a;
	}

	float getLength()
	{
		if (x == 0 && y == 0)
			return 0;
		else
			return sqrt(x*x + y*y);
	}

	float2 operator- (float2 a)
	{
		float2 ret = {x - a.x, y - a.y};
		return ret;
	}

	float2 operator+ (float2 a)
	{
		float2 ret = {x + a.x, y + a.y};
		return ret;
	}

	float2 operator/ (float a)
	{
		float2 ret = {x/a, y/a};
		return ret;
	}
	
	float2 operator* (float a)
	{
		float2 ret = {x*a, y*a};
		return ret; 
	}
};

struct short2
{
	void setX(float a)
	{
		x = ushort(a * MAX_USHORTF + 0.5f);
	}

	void setY(float a)
	{
		y = ushort(a * MAX_USHORTF + 0.5f);
	}

	float getX()
	{
		return x/MAX_USHORTF;
	}

	float getY()
	{
		return y/MAX_USHORTF;
	}

	ushort x;
	ushort y;
};

struct Attribs 
{
	ushort hasParent  : 1;
	ushort hasChild   : 1;
	ushort targetID : 14;
};