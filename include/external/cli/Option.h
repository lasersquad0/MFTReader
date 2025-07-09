#pragma once
//
// Project includes
//

//
// Libs include
//

//
// STDLib/OS includes
//
#include <tchar.h>
#include <string>
#include <vector>
//#include <stdexcept>

///////////////////////////////////////////////////////////////////////////////

#if defined(UNICODE) || defined(_UNICODE)
//#define _T(x)      L ## x
typedef std::wstring cli_string;
#define to_clistring std::to_wstring

#else

//#define _T(x)      x
typedef std::string cli_string;
#define to_clistring std::to_string

#endif

#define DASH  _T("-")
#define DDASH _T("--")

/** @brief Vector that contains strings. Commonly used type in many classes */
typedef std::vector<cli_string> vector_string_t;

class COption
{
public:
    /**
     * @brief Create an option instance with default values. This is used by the parser to check
     * if the program argument is an option name, an option argument or an unknown data.
     */
    COption():m_NumArgs(0),m_NumRequiredArgs(0),m_IsRequired(false) {}

    /**
     * @brief Create an option instance.
     *
     * @param shortName Short name of the option (ex: -v).
     * @param longName Long name of the option (ex: --version).
     * @param description Option description.
     * @param numArgs Maximum number of arguments of the option.
     * @param isRequired True if option is required option, false otherwise.
     */
    COption(const cli_string& shortName, const cli_string& longName, const cli_string& description, uint16_t numArgs, bool isRequired = false);

    /**
     * @brief Update option information.
     *
     * @param shortName Short name of the option (ex: -v).
     * @param longName Long name of the option (ex: --version).
     * @param description Option description.
     * @param numArgs Maximum number of arguments of the option.
     * @param isRequired True if option is required option, false otherwise.
     */
    COption& Assign(const cli_string& shortName, const cli_string& longName, const cli_string& description, uint16_t numArgs, bool isRequired = false);

    /**
     * @brief Update option information from another option.
     *
     * @param opt All fields of this Option are copied to the new option.
     */
    COption& Assign(const COption& opt);

    /**
     * @brief Store the option argument.
     * @param arg The option argument.
     */
    inline COption& AddArg(const cli_string& arg) { m_Args.push_back(arg); return *this; }

    /**
     * @brief Retrieve the option arguments.
     * @return A reference to a vector<string> with the option arguments.
     */
    inline const vector_string_t& GetArgs() { return m_Args; }

    /**
     * @brief Get the maximum number of option arguments.
     * @return The maximum number of arguments for the option.
     */
    inline uint16_t GetNumArgs() { return m_NumArgs; }
    
    /**
     * @brief Set the maximum number of option arguments.
     * @return Reference to this option.
     */
    COption& NumArgs(uint16_t numArgs) 
    {
        m_NumArgs = numArgs;
        if(m_NumRequiredArgs > m_NumArgs)
           m_NumRequiredArgs = m_NumArgs;

        return *this; 
    }
    
    /**
     * @brief Get the number of required arguments for the option.
     * @return The number of required arguments.
     */
    inline uint16_t GetNumRequiredArgs() { return m_NumRequiredArgs; }

    /**
     * @brief Set the number of required arguments for the option.
     * @return Reference to this option.
     * if numRequiredArgs > GetNumArgs() then NumArgs is set to numRequiredArgs 
     */
    COption& RequiredArgs(uint16_t numRequiredArgs) 
    { 
        m_NumRequiredArgs = numRequiredArgs;
        if (m_NumArgs < m_NumRequiredArgs) // m_NumRequiredArgs cannot be larger than m_NumArgs  
            m_NumArgs = m_NumRequiredArgs;

        return *this; 
    }
    
    /**
     * @brief Get the option description.
     * @return A string with the option description.
     */
    inline const cli_string& GetDescription() { return m_Description; }
    COption& Descr(const cli_string& descr) { m_Description = descr; return *this; }
    
    /**
     * @brief Get the option short name (does not contain the dash).
     * @return A string with the short name.
     */
    inline const cli_string& GetShortName() { return m_ShortName; }
    COption& ShortName(const cli_string& shortName) { m_ShortName = shortName; return *this; }
    
    /**
     * @brief Get the option long name (does not contain the dashes).
     * @return A string with the long name.
     */
    inline const cli_string& GetLongName() { return m_LongName; }
    COption& LongName(const cli_string& longName) { m_LongName = longName; return *this; }
    
    /**
     * @brief Get non empty option name either short name or long name (does not contain the dashes).
     * @return A string with the non empty name.
     */
    const cli_string GetNonEmptyName(bool addDash = false);

    /**
     * @brief Check if the option has short nane.
     * @return true if has short name, false otherwise.
     */
    inline bool HasShortName() { return !m_ShortName.empty(); }

    /**
     * @brief Check if the option has long name.
     * @return true if has long name, false otherwise.
     */
    inline bool HasLongName() { return !m_LongName.empty(); }

    /**
     * @brief Check of the option accepts more arguments.
     * @return true if accepts more arguments, false, otherwise.
     */
    inline bool AcceptsArgs() { return m_Args.size() < m_NumArgs; }

    /**
   * @brief Check of the option needs more required arguments.
   * @return true if needs more arguments, false, otherwise.
   */
    inline bool NeedsArgs() { return m_Args.size() < m_NumRequiredArgs; }

    /**
     * @brief Check if the option is required or optional.
     * @return true if required, false otherwise.
     */
    inline bool IsRequired() { return m_IsRequired; }

    /**
    * @brief Make the option itself required or not.
    * @return Reference to this option for further (chained) actions.
    */
    COption& Required(bool required) { m_IsRequired = required; return *this; }
    
    /**
     * @brief Check of the option has arguments.
     * @return True if has arguments, false otherwise.
     */
    inline bool HasArgs() { return m_NumArgs > 0 || m_NumRequiredArgs > 0; }

private:

    /** @brief Option short name (ex: -v). */
    cli_string m_ShortName;

    /** @brief Option long name (ex: --version). */
    cli_string m_LongName;

    /** @brief Human reafable option description. */
    cli_string m_Description;

    /** @brief Is the option required or optional. */
    bool m_IsRequired;

    /** @brief Maximum number of arguments. */
    uint16_t m_NumArgs;

    /** @brief Number of required arguments (but not more than m_NumArgs). */
    uint16_t m_NumRequiredArgs;

    /** @brief List where arguments are stored. */
    vector_string_t m_Args;

    /* prevent copying and assignment */
    COption& operator=(const COption& other) = delete;
 
};

/** @brief Vector that contains pointers to options. */
typedef std::vector<COption*> vector_option_pt;

/** @brief Vector that contains options. */
typedef std::vector<COption> vector_option_t;

