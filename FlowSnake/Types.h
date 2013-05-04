#define MAX_SSHORTF 32767.0f
#define MAX_SHORTF 4294967295.0f
#define MAX_INTF 2147483647.0f 

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
		x = uint(a * MAX_SHORTF);
	}

	void setY(float a)
	{
		y = uint(a * MAX_SHORTF);
	}

	float getX()
	{
		return x/MAX_SHORTF;
	}

	float getY()
	{
		return y/MAX_SHORTF;
	}

	uint x;
	uint y;
};