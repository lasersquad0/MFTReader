/*
* Properties.cpp
*
* Copyright 2025, LogEngine2 Project. All rights reserved.
*
* See the COPYING file for the terms of usage and distribution.
*/

//#include <limits.h>
#include <istream>
#include "Properties.h"


LOGENGINE_NS_BEGIN

/*
void Properties::load(std::istream& in) 
{
	Clear();
	
	std::string fullLine, command;
	std::string leftSide, rightSide;
	char linebuf[512]; // assume that we do not have lines greate than 512 symbols
	std::string::size_type length;   
    
    //std::ios_base::iostate state = in.rdstate();
    while (in.getline(linebuf, 512)) //state & std::ios_base::failbit != std::ios_base::failbit)
	{
		fullLine = linebuf;
		
		// if the line contains a # then it is a comment
		// if we find it anywhere other than at the beginning, we assume 
		// there is a command on that line, and if we don't find it at all
		// we assume there is a command on the line (we test for valid 
		// command later) if neither is true, we continue with the next line
		
		length = fullLine.find('#');
		if (length == std::string::npos) 
		{
			command = fullLine;
		}
		else 
		{
			if (length > 0) 
			{
				std::string trimmed = trim(fullLine);
				if(trimmed.length() > 0)
				{
					if(trimmed[0] == '#') //if only spaces (0 or more) before # then this line is a comment. bypass it
						continue;
				}
				else
				{
					continue; // looks like it will never go here
				}
				
				command = fullLine; //.substr(0, length);
				
			}
			else
			{
				continue;
			}
		}	
		
		// check the command and handle it
		length = command.find('=');
		if (length != std::string::npos) 
		{
			leftSide = command.substr(0, length);
			rightSide = command.substr(length + 1, command.size() - length);
			//_substituteVariables(rightSide);
		}
		else
		{
			continue; // bypass all lines not containing '='
		}
		
		// add to the map of properties
		SetValue(trimSPCRLF(leftSide), trimSPCRLF(rightSide)); // trimSPCRLF is required here for Linux since its \n and \r differ from Windows.
	}    
}

void Properties::save(std::ostream& out) 
{
	for(uint i = 0; i < Count(); i++) 
	{
		std::string key = GetKey(i);
		out << key << "=" << GetValue(key) << std::endl;
	}
}
*/

LOGENGINE_INLINE int Properties::GetInt(const std::string& property, int defaultValue /*=0*/) const
{
	if(!IfExists(property))
		return defaultValue;
	
	std::string value = Trim(GetValue(property));
	if(value.size() == 0)
		return defaultValue;
	
	int res = atoi(value.c_str());
	res = res == INT_MAX ? defaultValue : res;
	return res;
}

LOGENGINE_INLINE ulong Properties::GetUInt(const std::string& property, ulong defaultValue /*=0*/) const
{
	if (!IfExists(property))
		return defaultValue;

	std::string value = Trim(GetValue(property));

	if (!isUInt(value)) return defaultValue; // return defaultValue if string is NOT an integer

	ulong res = strtoul(value.c_str(), nullptr, 0);
	res = res > LONG_MAX ? defaultValue : res;
	return res;
}

LOGENGINE_INLINE bool Properties::GetBool(const std::string& property, bool defaultValue /*=false*/) const
{
	std::string* pstr = GetValuePointer(property);
	if (pstr)
		return StrToBool(Trim(GetValue(property)));
	else
		return defaultValue;

	//if(!IfExists(property))
		//return defaultValue;
	
//	std::string value = trim(GetValue(property));
	
	//return StrToBool(value); //(EqualNCase(value, "true") || EqualNCase(value, "1") || EqualNCase(value, "yes"));
}

LOGENGINE_INLINE std::string Properties::GetString(const std::string& property, const std::string& defaultValue /*=""*/) const
{
	std::string* pstr = GetValuePointer(property);
	if (pstr)
		return *pstr;
	else
		return defaultValue;

	//if(!IfExists(property))
	//	return defaultValue;
	
	//return GetValue(property);
}

LOGENGINE_NS_END
