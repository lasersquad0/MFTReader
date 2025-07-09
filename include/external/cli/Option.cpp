//
// Project includes
//
#include "Option.h"

//
// Libs include
//

//
// STDLib/OS includes
//

///////////////////////////////////////////////////////////////////////////////

COption::COption(const cli_string &shortName, const cli_string &longName, const cli_string &description, uint16_t numArgs, bool isRequired/*=false*/)
{
    m_ShortName.assign(shortName);
    m_LongName.assign(longName);
    m_Description.assign(description);
    m_IsRequired = isRequired;
    m_NumArgs = numArgs;
    m_NumRequiredArgs = numArgs;
}

COption& COption::Assign(const cli_string& shortName, const cli_string& longName, const cli_string& description, uint16_t numArgs, bool isRequired/*=false*/)
{
    m_ShortName.assign(shortName);
    m_LongName.assign(longName);
    m_Description.assign(description);
    m_IsRequired = isRequired;
    m_NumArgs = numArgs;
    m_NumRequiredArgs = numArgs;

    return *this;
}

COption& COption::Assign(const COption& opt)
{
    m_ShortName.assign(opt.m_ShortName);
    m_LongName.assign(opt.m_LongName);

    m_Description.assign(opt.m_Description);
    m_IsRequired = opt.m_IsRequired;
    m_NumArgs = opt.m_NumArgs;
    m_NumRequiredArgs = opt.m_NumRequiredArgs;
    
    return *this;
}

const cli_string COption::GetNonEmptyName(bool addDash /* =false*/)
{
    if (addDash)
        if (m_ShortName.empty())
            return DDASH + m_LongName;
        else
            return DASH + m_ShortName;
    else
        if (m_ShortName.empty())
            return m_LongName;
        else
            return m_ShortName;
}

//void COption::Assign(const COption_basic& opt)
//{
//    m_ShortName.assign(opt.m_ShortName);
//    m_LongName.assign(opt.m_LongName);
//
//    m_Description.assign(opt.m_Description);
//    m_IsRequired = opt.m_IsRequired;
//    m_NumArgs = opt.m_NumArgs;
//    m_NumRequiredArgs = opt.m_NumRequiredArgs;
//}
