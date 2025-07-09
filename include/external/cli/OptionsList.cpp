//
// Project includes
//
#include "OptionsList.h"

//
// Libs include
//

//
// STDLib/OS includes
//

///////////////////////////////////////////////////////////////////////////////

static void TrimStr(cli_string& str)
{
    // remove all leading and traling spaces.
    size_t strBegin = str.find_first_not_of(' ');
    size_t strEnd   = str.find_last_not_of(' ');
    str.erase(strEnd + 1, str.size() - strEnd);
    str.erase(0, strBegin);
}

bool COptionsList::VerifyOptionNames(cli_string shortName, cli_string longName)
{
    TrimStr(shortName);
    TrimStr(longName);

    if (shortName.empty()) // if shortName is empty then longName must be non-empty
        return !longName.empty();
    else
        return true;
}

void COptionsList::AddOption(COption& option)
{
    const cli_string& sn = option.GetShortName();
    const cli_string& ln = option.GetLongName();
    
    if (!VerifyOptionNames(sn, ln)) return; // option has invalid short and/or long names (or has incomlete data)

    COption* opt;

    if(sn.empty())
        opt = GetOptionByLongName(ln);
    else 
        opt = GetOptionByShortName(sn);

    if (opt == nullptr)
        m_AllOptions.emplace_back(option); 
    else
        opt->Assign(option);
}

void COptionsList::AddOption(const cli_string &shortName, const cli_string &longName, const cli_string &description, uint16_t numArgs, bool isRequired)
{
    COption o(shortName, longName, description, numArgs, isRequired);
    //o.ShortName(shortName).LongName(longName).Descr(description).NumArgs(numArgs).RequiredArgs(numArgs).Required(isRequired);
    AddOption(o);
}

vector_option_pt COptionsList::GetOptionsWithRequiredArguments()
{
    vector_option_pt ret;

    for (auto &it : m_AllOptions)
    {
        //if (it.HasArgs())
        if(it.GetNumRequiredArgs() > 0)
            ret.emplace_back(&it);
    }

    return ret;
}

vector_option_pt COptionsList::GetRequiredOptions()
{
    vector_option_pt ret;

    for (auto &it : m_AllOptions)
    {
        if (it.IsRequired())
            ret.emplace_back(&it);
    }

    return ret;
}

COption *COptionsList::GetOptionByShortName(const cli_string &name)
{
    for (auto &it : m_AllOptions)
    {
        if (it.GetShortName() == name)
            return &it;
    }

    return nullptr;
}

COption *COptionsList::GetOptionByLongName(const cli_string &name)
{
    for (auto &it : m_AllOptions)
    {
        if (it.GetLongName() == name)
            return &it;
    }

    return nullptr;
}

