#ifndef ARRAYGRID_H
#define ARRAYGRID_H

#include "grid.h"
#include "io/netcdf.h"

template <typename T> class ArrayGrid : public Grid
{
private:
	T *values;
	
	T *defaultValue;
	
	/** Number of elements in the array on each node */
	unsigned int count;
public:
	ArrayGrid()
	{
		values = 0L;
		defaultValue = 0L;
	}
	
	virtual ~ArrayGrid()
	{
		delete [] values;
		delete [] defaultValue;
	}
	
	unsigned int getVarSize()
	{
		return sizeof(T) * count;
	}
	
	float getFloat(float x, float y)
	{
		return static_cast<float>(*getAt(x, y));
	}
	
	double getDouble(float x, float y)
	{
		return static_cast<double>(*getAt(x, y));
	}
	
	void getBuf(float x, float y, void* buf)
	{
		memcpy(buf, getAt(x, y), getVarSize());
	}
	
protected:
	bool open(io::NetCdf &file)
	{
		if ((file.getVarSize() % sizeof(T)) != 0)
			// We can't create an array, because the variable size
			// isn't a multiple of the basic array type
			return false;
		
		count = file.getVarSize() / sizeof(T);
		
		values = new T[getXDim() * getYDim() * count];
		
		// Not sure if could use another method
		// void* should work, but does not do any data convertion
		file.getVar(static_cast<void*>(values));
		
		defaultValue = new T[count];
		
		file.getDefault(static_cast<void*>(defaultValue));
		
		return true;
	}
	
private:
	const T* getAt(float x, float y)
	{
		x = round((x - getXOffset()) / getXScaling());
		y = round((y - getYOffset()) / getYScaling());
		return getAt(static_cast<long>(x), static_cast<long>(y));
	}
	
	const T* getAt(long x, long y)
	{
		y = y * getXDim() + x;
		
		// Range check
		if (y < 0)
			return defaultValue;
		if (static_cast<unsigned long>(y) >= getXDim() * getYDim())
			return defaultValue;
		
		return &values[y];
	}
	
protected:
	float getAtFloat(long x, long y)
	{
		return static_cast<float>(*getAt(x, y));
	}
};

#endif // ARRAYGRID_H
