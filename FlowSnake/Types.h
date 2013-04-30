
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
		//x = int(a * MAX_SHORTF);
		x=a;
	}

	void setY(float a)
	{
		//y = int(a * MAX_SHORTF);
		y=a;
	}

	float getX()
	{
		//return x/MAX_SHORTF;
		return x;
	}

	float getY()
	{
		//return y/MAX_SHORTF;
		return y;
	}

	//short x;
	//short y;
	float x;
	float y;
};