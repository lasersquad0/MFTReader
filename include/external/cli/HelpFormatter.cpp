//
// Project includes
//
#include "HelpFormatter.h"

//
// Libs include
//

//
// STDLib/OS includes
//

///////////////////////////////////////////////////////////////////////////////

static vector_string_t SplitString(const cli_string &text, const cli_string &delimiter)
{
    vector_string_t retData;
    size_t start = 0, end = text.find_first_of(delimiter);
    size_t delimiterSize = 1; // NOTE(Andrei): Because we use find_first_of

    while(end != cli_string::npos)
    {
        if(end - start)
        {
            retData.push_back(text.substr(start, end - start));
        }

        start = end + delimiterSize;
        end = text.find_first_of(delimiter, start);
    }

    if(start != end)
    {
        retData.push_back(text.substr(start, end));
    }

    return retData;
}

cli_string CHelpFormatter::Format(const cli_string &appName, COptionsList *options)
{
    cli_string helpBuffer(appName + _T(" usage:\n"));
    vector_option_t& opt = options->GetAllOptions();

    vector_string_t argsNames;
    vector_string_t argsParams;

    size_t largestArgsName = 0;
    size_t largestParamsName = 0;

    for (auto &it : opt)
    {
        cli_string shortName = it.GetShortName(), longName = it.GetLongName();
        cli_string auxBuffer;

        auxBuffer.append(_T("-"));
        auxBuffer.append(shortName);

        if (!longName.empty())
        {
            auxBuffer.append(_T(", --"));
            auxBuffer.append(longName);
        }

        largestArgsName = auxBuffer.size() > largestArgsName ? auxBuffer.size() : largestArgsName;
        argsNames.push_back(auxBuffer);
        auxBuffer.clear();

        const uint16_t ARGS_THRESHOLD = 4;
        uint16_t rargs = it.GetNumRequiredArgs();
        uint16_t oargs = it.GetNumArgs();
        if (rargs > ARGS_THRESHOLD || oargs > ARGS_THRESHOLD)
        {
            auxBuffer.append(_T(" <req args>(") + to_clistring(rargs) + _T(")"));
            auxBuffer.append(_T("...<total args>(up to ") + to_clistring(oargs) + _T(")"));
        }
        else
        {
            cli_string s;
            if (oargs - rargs > 0)
                s = _T(" <req arg>");
            else
                s = _T(" <arg>");

            for (size_t n = 0; n < rargs; ++n)
            {
                auxBuffer.append(s);
            }
            for (size_t n = 0; n < oargs-rargs; ++n)
            {
                auxBuffer.append(_T(" <arg>"));
            }
        }

        largestParamsName = auxBuffer.size() > largestParamsName ? auxBuffer.size() : largestParamsName;
        argsParams.push_back(auxBuffer);
        auxBuffer.clear();
    }

    for (size_t i = 0; i < opt.size(); ++i)
    {
        cli_string auxBuffer(argsNames[i]);
        auxBuffer.append(cli_string(largestArgsName - argsNames[i].size(), ' ') + argsParams[i]);

        vector_string_t descriptionLines = SplitString(opt[i].GetDescription(), _T("\n"));
        if (!descriptionLines.empty()) auxBuffer.append(cli_string(largestArgsName + largestParamsName + 1 - auxBuffer.size(), ' ') + descriptionLines[0]);

        for (size_t l = 1; l < descriptionLines.size(); ++l)
        {
            auxBuffer.append(_T("\n"));
            auxBuffer.append(cli_string(largestArgsName + largestParamsName + 1, ' ') + descriptionLines[l]);
        }

        if (i + 1 < opt.size())
        {
            auxBuffer.append(_T("\n"));
        }

        helpBuffer.append(auxBuffer);
    }

    return helpBuffer;
}

