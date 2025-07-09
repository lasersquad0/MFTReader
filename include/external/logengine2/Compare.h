/*
 * Compare.h
 *
 * Copyright 2025, LogEngine2 Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef COMPARE_H
#define COMPARE_H

//#include <exception> //unix
#include <string>
#include <algorithm>
#include "Common.h"

/// Class to compare other classes by operators ==, <, >
// Actually it uses only two operators for comparing: == and >. It means that comparing classes need to implement only two compare operators. 
template<class C>
class Compare
{
public:
	virtual bool eq(const C& a, const C& b) const { return a == b; } // returns True when object a is equal b
	virtual bool lt(const C& a, const C& b) const { return b > a;  } // lt=less than
	virtual bool mt(const C& a, const C& b) const { return a > b;  } // mt=more than
	virtual ~Compare() {}
};

template<class C>
class CompareReverse : public Compare<C>
{
public:
	bool eq(const C& a, const C& b) const override 
	{
		return a == b;
	}

	bool lt(const C& a, const C& b) const override
	{
		return a > b;
	}

	bool mt(const C& a, const C& b) const override
	{
		return b > a;
	}
};

// Class to compare std::string WITHOUT CASE sensitivity
class CompareStringNCase: public Compare<std::string>
{
public:
	bool eq(const std::string& a, const std::string& b) const override // returns True when string a is equal b
	{ 
		return EqualNCase(a, b); 
	}

	bool lt(const std::string& a, const std::string& b) const override
	{
		return CompareNCase(a, b) < 0;

		/*std::string aa = a, bb = b;
		std::transform(aa.begin(), aa.end(), aa.begin(), ::toupper);
		std::transform(bb.begin(), bb.end(), bb.begin(), ::toupper);
		return aa.compare(bb) < 0;*/ 
	}

	bool mt(const std::string& a, const std::string& b) const override
	{ 
		return CompareNCase(a, b) > 0;

		/*std::string aa = a, bb = b;
		std::transform(aa.begin(), aa.end(), aa.begin(), ::toupper);
		std::transform(bb.begin(), bb.end(), bb.begin(), ::toupper);
		return aa.compare(bb) > 0;*/ 
	}
};


#endif //COMPARE_H
