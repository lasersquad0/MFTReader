//
// Project includes
//
#include "CommandLine.h"

//
// Libs include
//

//
// STDLib/OS includes
//

///////////////////////////////////////////////////////////////////////////////

static COption* ResolveOption(const cli_string &opt, const vector_option_pt &options)
{
    if (opt.empty()) return nullptr; // empty option name is not allowed

    for (const auto &it : options)
    {
        if (it->GetShortName() == opt || ( !(it->GetLongName().empty()) && (it->GetLongName() == opt)) )
        {
            return it;
        }
    }

    return nullptr;
}

void CCommandLine::Clear()
{
    m_Options.clear();
}

bool CCommandLine::AddOption(COption* option)
{
    const cli_string& sn = option->GetShortName();
    const cli_string& ln = option->GetLongName();
    
    if (sn.empty())
    {
        if (HasOption(ln)) return false; // this long option has already been met in command line
    }
    else
    {
        if (HasOption(sn)) return false; // this short option has already been met in command line
    }

    m_Options.push_back(option);
    return true;
}

bool CCommandLine::HasOption(const cli_string &opt)
{
    COption *option = ResolveOption(opt, m_Options);
    return option != nullptr;
}

cli_string CCommandLine::GetOptionValue(const cli_string &opt, size_t index, const cli_string &defaultValue)
{
    COption *option = ResolveOption(opt, m_Options);
    if (option == nullptr) return defaultValue;

    if (index >= option->GetArgs().size()) return defaultValue;
    return option->GetArgs()[index];
}

const vector_string_t& CCommandLine::GetOptionValues(const cli_string& opt)
{
    COption* option = ResolveOption(opt, m_Options);
    if (option == nullptr) return *(new vector_string_t()); // return empty list if option is not found in cmd ***TODO possible memory leak???

    return option->GetArgs();
}

bool CCommandLine::CheckMissingArguments()
{
    for (size_t i = 0; i < m_Options.size(); ++i)
    {
        if (m_Options[i]->NeedsArgs())
            return true;
    }

    return false;
}

vector_option_pt CCommandLine::GetOptionsWithMissingArguments()
{
    vector_option_pt missingArgs;

    for (size_t i = 0; i < m_Options.size(); ++i)
    {
        if (m_Options[i]->NeedsArgs())
        {
            missingArgs.push_back(m_Options[i]);
        }
    }

    return missingArgs;
}

